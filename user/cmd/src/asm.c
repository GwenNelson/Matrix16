#include <stdbool.h>
#include <stdint.h>

#include "stdio.h"
#include "syscalls.h"

#define COMMAND_LINE ((const char*)0x0100)
#define AUX_SEGMENT 0x7000
#define MAX_LINE 127
#define MAX_SYMBOLS 128
#define MAX_SYMBOL_NAME 31
#define MAX_OUTPUT 0xFFFF
#define IO_SIZE 512

typedef enum {
	OPERAND_NONE,
	OPERAND_REGISTER,
	OPERAND_SEGMENT_REGISTER,
	OPERAND_IMMEDIATE,
	OPERAND_MEMORY
} operand_kind_t;

typedef struct {
	operand_kind_t kind;
	uint8_t reg;
	uint8_t width;
	uint8_t segment_prefix;
	uint8_t modrm;
	uint8_t displacement_size;
	uint16_t displacement;
	uint16_t immediate;
	bool displacement_is_symbolic;
} operand_t;

typedef struct {
	char name[MAX_SYMBOL_NAME + 1];
	uint16_t value;
	bool is_equ;
} symbol_t;

typedef struct {
	uint16_t handle;
	uint16_t used;
	uint16_t index;
	char bytes[IO_SIZE];
} source_t;

typedef union {
	struct {
		uint16_t offset;
		uint16_t segment;
	} parts;
	uint8_t __far *pointer;
} far_byte_pointer_t;

static char source_path[28];
static char output_path[28];
static char line_buffer[MAX_LINE + 1];
static char copy_buffer[IO_SIZE];
static symbol_t symbols[MAX_SYMBOLS];
static uint8_t symbol_count;
static uint16_t line_number;
static uint16_t origin;
static uint16_t output_length;
static bool origin_seen;
static bool emitted_anything;
static bool pass_two;
static bool failed;

static bool equals(const char *left, const char *right);

static void clear_operand(operand_t *operand) {
	operand->kind = OPERAND_NONE;
	operand->reg = 0;
	operand->width = 0;
	operand->segment_prefix = 0;
	operand->modrm = 0;
	operand->displacement_size = 0;
	operand->displacement = 0;
	operand->immediate = 0;
	operand->displacement_is_symbolic = false;
}

static uint8_t __far *aux_pointer(uint16_t offset) {
	far_byte_pointer_t address;
	address.parts.offset = offset;
	address.parts.segment = AUX_SEGMENT;
	return address.pointer;
}

static void skip_spaces(char **text) {
	while(**text == ' ' || **text == '\t') (*text)++;
}

static void trim_right(char *text) {
	uint16_t length = 0;
	while(text[length] != '\0') length++;
	while(length != 0 && (text[length - 1] == ' ' || text[length - 1] == '\t'))
		text[--length] = '\0';
}

static char upper_char(char ch) {
	if(ch >= 'a' && ch <= 'z') return (char)(ch - ('a' - 'A'));
	return ch;
}

static bool identifier_start(char ch) {
	ch = upper_char(ch);
	return (ch >= 'A' && ch <= 'Z') || ch == '_' || ch == '.' || ch == '?';
}

static bool identifier_char(char ch) {
	ch = upper_char(ch);
	return identifier_start(ch) || (ch >= '0' && ch <= '9');
}

static bool read_identifier(char **text, char *name, uint8_t capacity) {
	uint8_t length = 0;
	if(!identifier_start(**text)) return false;
	while(identifier_char(**text)) {
		if(length + 1 >= capacity) return false;
		name[length++] = upper_char(**text);
		(*text)++;
	}
	name[length] = '\0';
	return true;
}

static int find_symbol(const char *name) {
	uint8_t i;
	for(i = 0; i < symbol_count; i++) {
		uint8_t j = 0;
		while(name[j] != '\0' && symbols[i].name[j] == name[j]) j++;
		if(name[j] == '\0' && symbols[i].name[j] == '\0') return i;
	}
	return -1;
}

static bool define_symbol(const char *name, uint16_t value, bool is_equ) {
	uint8_t i = 0;
	int existing = find_symbol(name);
	if(existing >= 0) return false;
	while(name[i] != '\0') i++;
	if(i == 0 || i > MAX_SYMBOL_NAME || symbol_count == MAX_SYMBOLS) return false;
	for(i = 0; name[i] != '\0'; i++) symbols[symbol_count].name[i] = name[i];
	symbols[symbol_count].name[i] = '\0';
	symbols[symbol_count].value = value;
	symbols[symbol_count].is_equ = is_equ;
	symbol_count++;
	return true;
}

static bool parse_number(char **text, uint16_t *value) {
	char *p = *text;
	char *digits = p;
	char *end;
	uint8_t base = 10;
	uint16_t result = 0;
	bool prefixed = false;

	if(p[0] == '0' && (p[1] == 'x' || p[1] == 'X')) {
		base = 16;
		p += 2;
		digits = p;
		prefixed = true;
	} else if(p[0] == '$') {
		base = 16;
		p++;
		digits = p;
		prefixed = true;
	}
	end = p;
	while(identifier_char(*end)) end++;
	if(!prefixed && end > digits && upper_char(end[-1]) == 'H') {
		base = 16;
		end--;
	}
	if(end == digits) return false;
	for(; p < end; p++) {
		uint8_t digit;
		char ch = upper_char(*p);
		if(ch >= '0' && ch <= '9') digit = (uint8_t)(ch - '0');
		else if(ch >= 'A' && ch <= 'F' && base == 16) digit = (uint8_t)(ch - 'A' + 10);
		else return false;
		if(digit >= base || result > (0xFFFF - digit) / base) return false;
		result = (uint16_t)(result * base + digit);
	}
	if(identifier_char(*end)) return false;
	*text = upper_char(end[-1]) == 'H' && !prefixed ? end + 1 : end;
	*value = result;
	return true;
}

static bool parse_expression(char **text, uint16_t *value, bool allow_forward,
	                         bool *known, bool *used_symbol) {
	bool subtract = false;
	bool first = true;
	uint16_t result = 0;
	bool all_known = true;
	bool has_symbol = false;

	for(;;) {
		char name[MAX_SYMBOL_NAME + 1];
		uint16_t term = 0;
		bool term_known = true;
		skip_spaces(text);
		if(**text == '+' || **text == '-') {
			subtract = **text == '-';
			(*text)++;
			skip_spaces(text);
		} else if(!first) break;

		if(parse_number(text, &term)) {
			/* Numeric term. */
		} else if(read_identifier(text, name, sizeof(name))) {
			int index = find_symbol(name);
			has_symbol = true;
			if(index < 0) {
				if(!allow_forward) return false;
				term_known = false;
			} else term = symbols[index].value;
		} else return false;

		if(!term_known) all_known = false;
		if(term_known) result = subtract ? (uint16_t)(result - term) : (uint16_t)(result + term);
		first = false;
		subtract = false;
		skip_spaces(text);
		if(**text != '+' && **text != '-') break;
	}
	*value = result;
	*known = all_known;
	*used_symbol = has_symbol;
	return true;
}

static bool source_open(source_t *source) {
	source->handle = sys_open(source_path);
	if(source->handle == SYSCALL_INVALID_HANDLE) return false;
	source->used = 0;
	source->index = 0;
	return true;
}

static void source_close(source_t *source) {
	if(source->handle != SYSCALL_INVALID_HANDLE) {
		(void)sys_close(source->handle);
		source->handle = SYSCALL_INVALID_HANDLE;
	}
}

static int source_getc(source_t *source) {
	if(source->index == source->used) {
		source->used = sys_read(source->handle, source->bytes, IO_SIZE);
		source->index = 0;
		if(source->used == 0) return -1;
	}
	return (uint8_t)source->bytes[source->index++];
}

static bool source_getline(source_t *source, char *line) {
	uint16_t length = 0;
	bool got_data = false;
	int ch;

	while((ch = source_getc(source)) >= 0) {
		got_data = true;
		if(ch == '\n') break;
		if(ch == '\r') continue;
		if(ch == 0) {
			failed = true;
			printf("ASM: line %u: NUL in source file\n", line_number + 1);
			return false;
		}
		if(length == MAX_LINE) {
			failed = true;
			printf("ASM: line %u: line is too long\n", line_number + 1);
			return false;
		}
		line[length++] = (char)ch;
	}
	if(!got_data && ch < 0) return false;
	line[length] = '\0';
	line_number++;
	return true;
}

static bool emit_byte(uint8_t value) {
	if(output_length == MAX_OUTPUT || output_length > (uint16_t)(0xFFFF - origin)) {
		failed = true;
		return false;
	}
	if(pass_two) *aux_pointer(output_length) = value;
	output_length++;
	emitted_anything = true;
	return true;
}

static bool emit_word(uint16_t value) {
	return emit_byte((uint8_t)value) && emit_byte((uint8_t)(value >> 8));
}

static uint16_t current_address(void) {
	return (uint16_t)(origin + output_length);
}

static void skip_separators(char **text) {
	skip_spaces(text);
	if(**text == ',') {
		(*text)++;
		skip_spaces(text);
	}
}

static bool parse_string(char **text) {
	char quote = **text;
	if(quote != '\'' && quote != '"') return false;
	(*text)++;
	while(**text != quote) {
		if(**text == '\0') return false;
		if(!emit_byte((uint8_t)**text)) return false;
		(*text)++;
	}
	(*text)++;
	return true;
}

static bool parse_data_list(char **text, bool words) {
	bool need_value = true;
	for(;;) {
		uint16_t value;
		bool known;
		bool used_symbol;
		skip_spaces(text);
		if(**text == '\0') return !need_value;
		if(!words && (**text == '\'' || **text == '"')) {
			if(!parse_string(text)) return false;
		} else {
			if(!parse_expression(text, &value, !pass_two, &known, &used_symbol)) return false;
			if(pass_two && !known) return false;
			if(words) {
				if(!emit_word(value)) return false;
			} else if(!emit_byte((uint8_t)value)) return false;
		}
		need_value = false;
		skip_spaces(text);
		if(**text == '\0') return true;
		if(**text != ',') return false;
		(*text)++;
		need_value = true;
	}
}

static bool parse_register(const char *text, uint8_t *reg, uint8_t *width) {
	static const char *names8[] = {"AL", "CL", "DL", "BL", "AH", "CH", "DH", "BH"};
	static const char *names16[] = {"AX", "CX", "DX", "BX", "SP", "BP", "SI", "DI"};
	uint8_t i;
	for(i = 0; i < 8; i++) {
		if(text[0] == names8[i][0] && text[1] == names8[i][1] && text[2] == '\0') {
			*reg = i;
			*width = 1;
			return true;
		}
		if(text[0] == names16[i][0] && text[1] == names16[i][1] && text[2] == '\0') {
			*reg = i;
			*width = 2;
			return true;
		}
	}
	return false;
}

static bool parse_segment_register(const char *text, uint8_t *reg) {
	if(text[0] == 'E' && text[1] == 'S' && text[2] == '\0') *reg = 0;
	else if(text[0] == 'C' && text[1] == 'S' && text[2] == '\0') *reg = 1;
	else if(text[0] == 'S' && text[1] == 'S' && text[2] == '\0') *reg = 2;
	else if(text[0] == 'D' && text[1] == 'S' && text[2] == '\0') *reg = 3;
	else return false;
	return true;
}

static bool parse_ea(char *expression, operand_t *operand) {
	char *p = expression;
	char token[MAX_SYMBOL_NAME + 1];
	int first_reg = -1;
	int second_reg = -1;
	uint16_t displacement = 0;
	bool have_displacement = false;
	bool symbolic_displacement = false;
	bool negative = false;
	uint8_t ea;

	while(*p != '\0') {
		uint8_t reg;
		uint8_t width;
		uint16_t value;
		bool known;
		bool used_symbol;
		char *term_start;
		char *term_end;
		char saved;
		skip_spaces(&p);
		if(*p == '+' || *p == '-') {
			negative = *p == '-';
			p++;
			skip_spaces(&p);
		}
		term_start = p;
		term_end = p;
		while(*term_end != '\0' && *term_end != '+' && *term_end != '-') term_end++;
		saved = *term_end;
		*term_end = '\0';
		while(*term_start == ' ' || *term_start == '\t') term_start++;
		{
			char *tail = term_start;
			while(*tail != '\0' && *tail != ' ' && *tail != '\t') tail++;
			if(*tail == '\0') {
				uint8_t n = 0;
				while(term_start[n] != '\0' && n < MAX_SYMBOL_NAME) {
					token[n] = upper_char(term_start[n]);
					n++;
				}
				token[n] = '\0';
			} else token[0] = '\0';
		}
		if(token[0] != '\0' && parse_register(token, &reg, &width) && width == 2 &&
		   (reg == 3 || reg == 5 || reg == 6 || reg == 7)) {
			*term_end = saved;
			if(first_reg < 0) first_reg = reg;
			else if(second_reg < 0) second_reg = reg;
			else return false;
			if(negative) return false;
		} else {
			char *term = term_start;
			if(!parse_expression(&term, &value, !pass_two, &known, &used_symbol)) {
				*term_end = saved;
				return false;
			}
			skip_spaces(&term);
			if(*term != '\0' || (pass_two && !known)) {
				*term_end = saved;
				return false;
			}
			if(negative) displacement = (uint16_t)(displacement - value);
			else displacement = (uint16_t)(displacement + value);
			have_displacement = true;
			symbolic_displacement = symbolic_displacement || used_symbol;
		}
		*term_end = saved;
		p = term_end;
		negative = false;
		while(*p == ' ' || *p == '\t') p++;
		if(*p == '\0') break;
		if(*p != '+' && *p != '-') return false;
	}

	if(first_reg < 0) {
		if(!have_displacement) return false;
		operand->modrm = 0x06;
		operand->displacement_size = 2;
		operand->displacement = displacement;
		operand->displacement_is_symbolic = symbolic_displacement;
		return true;
	}
	if(second_reg >= 0) {
		if(first_reg == 3 && second_reg == 6) ea = 0;
		else if(first_reg == 3 && second_reg == 7) ea = 1;
		else if(first_reg == 5 && second_reg == 6) ea = 2;
		else if(first_reg == 5 && second_reg == 7) ea = 3;
		else if(first_reg == 6 && second_reg == 3) ea = 0;
		else if(first_reg == 7 && second_reg == 3) ea = 1;
		else if(first_reg == 6 && second_reg == 5) ea = 2;
		else if(first_reg == 7 && second_reg == 5) ea = 3;
		else return false;
	} else {
		ea = first_reg == 6 ? 4 : first_reg == 7 ? 5 : first_reg == 5 ? 6 : 7;
	}
	if(!have_displacement && ea != 6) {
		operand->modrm = ea;
		operand->displacement_size = 0;
	} else if(!have_displacement && ea == 6) {
		operand->modrm = (uint8_t)(0x40 | (ea));
		operand->displacement_size = 1;
		operand->displacement = 0;
	} else if(!symbolic_displacement && (displacement <= 0x007F || displacement >= 0xFF80)) {
		operand->modrm = (uint8_t)(0x40 | ea);
		operand->displacement_size = 1;
		operand->displacement = displacement;
	} else {
		operand->modrm = (uint8_t)(0x80 | ea);
		operand->displacement_size = 2;
		operand->displacement = displacement;
	}
	operand->displacement_is_symbolic = symbolic_displacement;
	return true;
}

static bool parse_operand(char *text, operand_t *operand) {
	char *p = text;
	uint8_t width = 0;
	uint8_t reg;
	char *open;
	char *close;
	char *colon = 0;
	bool known;
	bool used_symbol;

	while(*p == ' ' || *p == '\t') p++;
	if(upper_char(p[0]) == 'B' && upper_char(p[1]) == 'Y' && upper_char(p[2]) == 'T' && upper_char(p[3]) == 'E' && (p[4] == ' ' || p[4] == '\t')) {
		operand->width = 1;
		p += 4;
		skip_spaces(&p);
	} else if(upper_char(p[0]) == 'W' && upper_char(p[1]) == 'O' && upper_char(p[2]) == 'R' && upper_char(p[3]) == 'D' && (p[4] == ' ' || p[4] == '\t')) {
		operand->width = 2;
		p += 4;
		skip_spaces(&p);
	}
	open = 0;
	close = 0;
	for(char *q = p; *q != '\0'; q++) {
		if(*q == '[') {
			if(open != 0) return false;
			open = q;
		} else if(*q == ']') {
			if(open == 0 || close != 0) return false;
			close = q;
		} else if(*q == ':' && open == 0) colon = q;
	}
	if(open != 0) {
		char memory_expression[MAX_LINE + 1];
		char *prefix_end = open;
		uint16_t length;
		if(close == 0) return false;
		while(prefix_end > p && (prefix_end[-1] == ' ' || prefix_end[-1] == '\t')) prefix_end--;
		if(colon != 0) {
			char segname[3];
			if(colon - p != 2 || prefix_end != colon + 1) return false;
			segname[0] = upper_char(p[0]);
			segname[1] = upper_char(p[1]);
			segname[2] = '\0';
			if(!parse_segment_register(segname, &reg) || reg == 1) return false;
			operand->segment_prefix = reg == 0 ? 0x26 : reg == 2 ? 0x36 : 0x3E;
		} else if(prefix_end != p) return false;
		for(char *q = close + 1; *q != '\0'; q++)
			if(*q != ' ' && *q != '\t') return false;
		length = (uint16_t)(close - open - 1);
		if(length == 0 || length >= sizeof(memory_expression)) return false;
		for(uint16_t i = 0; i < length; i++) memory_expression[i] = open[i + 1];
		memory_expression[length] = '\0';
		operand->kind = OPERAND_MEMORY;
		return parse_ea(memory_expression, operand);
	}
	{
		char copy[MAX_SYMBOL_NAME + 1];
		uint8_t i = 0;
		while(p[i] != '\0' && p[i] != ' ' && p[i] != '\t') {
			if(i >= MAX_SYMBOL_NAME) break;
			copy[i] = upper_char(p[i]);
			i++;
		}
		copy[i] = '\0';
		if(i != 0 && (p[i] == '\0' || p[i] == ' ' || p[i] == '\t')) {
			if(parse_register(copy, &reg, &width)) {
				if(operand->width != 0 && operand->width != width) return false;
				operand->kind = OPERAND_REGISTER;
				operand->reg = reg;
				operand->width = width;
				return true;
			}
			if(parse_segment_register(copy, &reg)) {
				operand->kind = OPERAND_SEGMENT_REGISTER;
				operand->reg = reg;
				if(operand->width != 0 && operand->width != 2) return false;
				operand->width = 2;
				return true;
			}
		}
	}
	operand->kind = OPERAND_IMMEDIATE;
	if(!parse_expression(&p, &operand->immediate, !pass_two, &known, &used_symbol)) return false;
	if(pass_two && !known) return false;
	skip_spaces(&p);
	return *p == '\0';
}

static bool emit_modrm_operand(operand_t *operand, uint8_t reg_field) {
	uint8_t modrm;
	if(operand->kind == OPERAND_REGISTER) modrm = (uint8_t)(0xC0 | (reg_field << 3) | operand->reg);
	else if(operand->kind == OPERAND_MEMORY) modrm = (uint8_t)((operand->modrm & 0xC7) | (reg_field << 3));
	else return false;
	if(!emit_byte(modrm)) return false;
	if(operand->kind == OPERAND_MEMORY && operand->displacement_size != 0) {
		if(!emit_byte((uint8_t)operand->displacement)) return false;
		if(operand->displacement_size == 2 && !emit_byte((uint8_t)(operand->displacement >> 8))) return false;
	}
	return true;
}

static bool split_operands(char *text, char **first, char **second, uint8_t *count) {
	uint8_t bracket_depth = 0;
	char quote = 0;
	char *comma = 0;
	for(char *p = text; *p != '\0'; p++) {
		if(quote != 0) {
			if(*p == quote) quote = 0;
		} else if(*p == '\'' || *p == '"') quote = *p;
		else if(*p == '[') bracket_depth++;
		else if(*p == ']') {
			if(bracket_depth == 0) return false;
			bracket_depth--;
		} else if(*p == ',' && bracket_depth == 0) {
			if(comma != 0) return false;
			comma = p;
		}
	}
	if(quote != 0 || bracket_depth != 0) return false;
	if(comma == 0) {
		*first = text;
		*second = 0;
		*count = *text == '\0' ? 0 : 1;
	} else {
		*comma = '\0';
		*first = text;
		*second = comma + 1;
		*count = 2;
		trim_right(*first);
		while(**second == ' ' || **second == '\t') (*second)++;
		trim_right(*second);
	}
	return true;
}

static uint8_t condition_code(const char *name) {
	static const char *conditions[16][3] = {
		{"JO", 0, 0}, {"JNO", 0, 0}, {"JB", "JC", "JNAE"}, {"JAE", "JNB", "JNC"},
		{"JE", "JZ", 0}, {"JNE", "JNZ", 0}, {"JBE", "JNA", 0}, {"JA", "JNBE", 0},
		{"JS", 0, 0}, {"JNS", 0, 0}, {"JP", "JPE", 0}, {"JNP", "JPO", 0},
		{"JL", "JNGE", 0}, {"JGE", "JNL", 0}, {"JLE", "JNG", 0}, {"JG", "JNLE", 0}
	};
	for(uint8_t i = 0; i < 16; i++)
		for(uint8_t j = 0; j < 3; j++)
			if(conditions[i][j] != 0) {
				uint8_t k = 0;
				while(name[k] != '\0' && conditions[i][j][k] == name[k]) k++;
				if(name[k] == '\0' && conditions[i][j][k] == '\0') return i;
			}
	return 0xFF;
}

static bool emit_relative8(uint8_t opcode, uint16_t target) {
	uint16_t next = (uint16_t)(current_address() + 2);
	uint16_t displacement = (uint16_t)(target - next);
	if(pass_two && displacement > 0x007F && displacement < 0xFF80) return false;
	return emit_byte(opcode) && emit_byte((uint8_t)displacement);
}

static bool emit_near_relative(uint8_t opcode, uint16_t target) {
	uint16_t next = (uint16_t)(current_address() + 3);
	uint16_t displacement = (uint16_t)(target - next);
	return emit_byte(opcode) && emit_word(displacement);
}

static bool assemble_instruction(char *mnemonic, char *operands) {
	char *first_text;
	char *second_text;
	uint8_t count;
	operand_t first;
	operand_t second;
	uint8_t condition;
	uint8_t group;
	uint8_t opcode;
	uint16_t target;
	bool known;
	bool used_symbol;

	clear_operand(&first);
	clear_operand(&second);
	if(!split_operands(operands, &first_text, &second_text, &count)) return false;
	if(count > 0 && (!parse_operand(first_text, &first))) return false;
	if(count > 1 && (!parse_operand(second_text, &second))) return false;
	if(count == 0 && operands[0] != '\0') return false;

	/* Segment overrides are instruction prefixes, so emit them before opcodes. */
	if(first.kind == OPERAND_MEMORY && first.segment_prefix != 0) {
		if(!emit_byte(first.segment_prefix)) return false;
		first.segment_prefix = 0;
	}
	if(second.kind == OPERAND_MEMORY && second.segment_prefix != 0) {
		if(!emit_byte(second.segment_prefix)) return false;
		second.segment_prefix = 0;
	}

	if(mnemonic[0] == 'J' && (condition = condition_code(mnemonic)) != 0xFF) {
		if(count != 1 || first.kind != OPERAND_IMMEDIATE) return false;
		return emit_relative8((uint8_t)(0x70 + condition), first.immediate);
	}
	if(equals(mnemonic, "LOOP") || equals(mnemonic, "LOOPE") || equals(mnemonic, "LOOPZ") ||
	   equals(mnemonic, "LOOPNE") || equals(mnemonic, "LOOPNZ") || equals(mnemonic, "JCXZ")) {
		if(count != 1 || first.kind != OPERAND_IMMEDIATE) return false;
		if(equals(mnemonic, "LOOP")) opcode = 0xE2;
		else if(equals(mnemonic, "LOOPE") || equals(mnemonic, "LOOPZ")) opcode = 0xE1;
		else if(equals(mnemonic, "LOOPNE") || equals(mnemonic, "LOOPNZ")) opcode = 0xE0;
		else opcode = 0xE3;
		return emit_relative8(opcode, first.immediate);
	}
	if(equals(mnemonic, "JMP") || equals(mnemonic, "CALL")) {
		if(count != 1) return false;
		if(first.kind == OPERAND_IMMEDIATE) {
			if(equals(mnemonic, "JMP")) return emit_near_relative(0xE9, first.immediate);
			return emit_near_relative(0xE8, first.immediate);
		}
		if(first.width != 0 && first.width != 2) return false;
		if(first.kind != OPERAND_REGISTER && first.kind != OPERAND_MEMORY) return false;
		if(!emit_byte(0xFF)) return false;
		return emit_modrm_operand(&first, equals(mnemonic, "JMP") ? 4 : 2);
	}
	if(equals(mnemonic, "INT")) {
		if(count != 1 || first.kind != OPERAND_IMMEDIATE || (pass_two && first.immediate > 0xFF)) return false;
		return emit_byte(0xCD) && emit_byte((uint8_t)first.immediate);
	}
	if(equals(mnemonic, "RET") || equals(mnemonic, "RETN")) {
		if(count == 0) return emit_byte(0xC3);
		if(count != 1 || first.kind != OPERAND_IMMEDIATE) return false;
		return emit_byte(0xC2) && emit_word(first.immediate);
	}
	if(equals(mnemonic, "IRET")) return count == 0 && emit_byte(0xCF);
	if(equals(mnemonic, "NOP")) return count == 0 && emit_byte(0x90);
	if(equals(mnemonic, "HLT")) return count == 0 && emit_byte(0xF4);
	if(equals(mnemonic, "CLC")) return count == 0 && emit_byte(0xF8);
	if(equals(mnemonic, "STC")) return count == 0 && emit_byte(0xF9);
	if(equals(mnemonic, "CLI")) return count == 0 && emit_byte(0xFA);
	if(equals(mnemonic, "STI")) return count == 0 && emit_byte(0xFB);
	if(equals(mnemonic, "CLD")) return count == 0 && emit_byte(0xFC);
	if(equals(mnemonic, "STD")) return count == 0 && emit_byte(0xFD);
	if(equals(mnemonic, "MOVSB")) return count == 0 && emit_byte(0xA4);
	if(equals(mnemonic, "MOVSW")) return count == 0 && emit_byte(0xA5);
	if(equals(mnemonic, "CMPSB")) return count == 0 && emit_byte(0xA6);
	if(equals(mnemonic, "CMPSW")) return count == 0 && emit_byte(0xA7);
	if(equals(mnemonic, "STOSB")) return count == 0 && emit_byte(0xAA);
	if(equals(mnemonic, "STOSW")) return count == 0 && emit_byte(0xAB);
	if(equals(mnemonic, "LODSB")) return count == 0 && emit_byte(0xAC);
	if(equals(mnemonic, "LODSW")) return count == 0 && emit_byte(0xAD);
	if(equals(mnemonic, "SCASB")) return count == 0 && emit_byte(0xAE);
	if(equals(mnemonic, "SCASW")) return count == 0 && emit_byte(0xAF);
	if(equals(mnemonic, "REP") || equals(mnemonic, "REPE") || equals(mnemonic, "REPZ") ||
	   equals(mnemonic, "REPNE") || equals(mnemonic, "REPNZ")) return false;

	if(equals(mnemonic, "PUSH") || equals(mnemonic, "POP")) {
		bool push = equals(mnemonic, "PUSH");
		if(count != 1) return false;
		if(first.kind == OPERAND_SEGMENT_REGISTER) {
			static const uint8_t push_seg[] = {0x06, 0x0E, 0x16, 0x1E};
			static const uint8_t pop_seg[] = {0x07, 0x00, 0x17, 0x1F};
			if(!push && first.reg == 1) return false;
			return emit_byte(push ? push_seg[first.reg] : pop_seg[first.reg]);
		}
		if(first.width != 0 && first.width != 2) return false;
		if(first.kind == OPERAND_REGISTER) return emit_byte((uint8_t)((push ? 0x50 : 0x58) + first.reg));
		if(!push && first.kind == OPERAND_MEMORY)
			return emit_byte(0x8F) && emit_modrm_operand(&first, 0);
		return false;
	}

	if(equals(mnemonic, "MOV")) {
		if(count != 2) return false;
		if(first.kind == OPERAND_SEGMENT_REGISTER) {
			if(second.width != 0 && second.width != 2) return false;
			if(first.reg == 1) return false; /* MOV cannot load CS. */
			return emit_byte(0x8E) && emit_modrm_operand(&second, first.reg);
		}
		if(second.kind == OPERAND_SEGMENT_REGISTER) {
			if(first.width != 0 && first.width != 2) return false;
			return emit_byte(0x8C) && emit_modrm_operand(&first, second.reg);
		}
		if(second.kind == OPERAND_IMMEDIATE && (first.kind == OPERAND_REGISTER || first.kind == OPERAND_MEMORY)) {
			uint8_t width = first.width;
			if(width == 0) return false;
			if(first.kind == OPERAND_REGISTER) {
				opcode = (uint8_t)((width == 1 ? 0xB0 : 0xB8) + first.reg);
				return emit_byte(opcode) && (width == 1 ? emit_byte((uint8_t)second.immediate) : emit_word(second.immediate));
			}
			return emit_byte(width == 1 ? 0xC6 : 0xC7) && emit_modrm_operand(&first, 0) &&
			       (width == 1 ? emit_byte((uint8_t)second.immediate) : emit_word(second.immediate));
		}
		if(first.kind == OPERAND_REGISTER && (second.kind == OPERAND_REGISTER || second.kind == OPERAND_MEMORY)) {
			if(first.width != second.width) return false;
			return emit_byte(first.width == 1 ? 0x8A : 0x8B) && emit_modrm_operand(&second, first.reg);
		}
		if((first.kind == OPERAND_MEMORY) && second.kind == OPERAND_REGISTER) {
			if(first.width != 0 && first.width != second.width) return false;
			first.width = second.width;
			return emit_byte(second.width == 1 ? 0x88 : 0x89) && emit_modrm_operand(&first, second.reg);
		}
		return false;
	}

	{
		static const char *alu_names[] = {"ADD", "OR", "ADC", "SBB", "AND", "SUB", "XOR", "CMP"};
		for(group = 0; group < 8; group++) if(equals(mnemonic, alu_names[group])) break;
		if(group < 8) {
			uint8_t width;
			if(count != 2) return false;
			if(second.kind == OPERAND_IMMEDIATE && (first.kind == OPERAND_REGISTER || first.kind == OPERAND_MEMORY)) {
				width = first.width;
				if(width == 0) return false;
				if(width == 1) opcode = 0x80;
				else opcode = 0x81;
				return emit_byte(opcode) && emit_modrm_operand(&first, group) &&
				       (opcode == 0x80 || opcode == 0x83 ? emit_byte((uint8_t)second.immediate) : emit_word(second.immediate));
			}
			if(first.kind == OPERAND_REGISTER && (second.kind == OPERAND_REGISTER || second.kind == OPERAND_MEMORY)) {
				if(first.width != second.width) return false;
				width = first.width;
				return emit_byte((uint8_t)(group * 8 + (width == 1 ? 2 : 3))) && emit_modrm_operand(&second, first.reg);
			}
			if((first.kind == OPERAND_MEMORY || first.kind == OPERAND_REGISTER) && second.kind == OPERAND_REGISTER) {
				if(first.width != second.width) return false;
				width = second.width;
				return emit_byte((uint8_t)(group * 8 + (width == 1 ? 0 : 1))) && emit_modrm_operand(&first, second.reg);
			}
			return false;
		}
	}

	if(equals(mnemonic, "TEST")) {
		if(count != 2) return false;
		if(second.kind == OPERAND_IMMEDIATE && (first.kind == OPERAND_REGISTER || first.kind == OPERAND_MEMORY)) {
			if(first.width == 0) return false;
			return emit_byte(first.width == 1 ? 0xF6 : 0xF7) && emit_modrm_operand(&first, 0) &&
			       (first.width == 1 ? emit_byte((uint8_t)second.immediate) : emit_word(second.immediate));
		}
		if(first.kind == OPERAND_REGISTER && (second.kind == OPERAND_REGISTER || second.kind == OPERAND_MEMORY)) {
			if(first.width != second.width) return false;
			return emit_byte(first.width == 1 ? 0x84 : 0x85) && emit_modrm_operand(&second, first.reg);
		}
		if((first.kind == OPERAND_MEMORY || first.kind == OPERAND_REGISTER) && second.kind == OPERAND_REGISTER) {
			if(first.width != second.width) return false;
			return emit_byte(first.width == 1 ? 0x84 : 0x85) && emit_modrm_operand(&first, second.reg);
		}
		return false;
	}

	if(equals(mnemonic, "INC") || equals(mnemonic, "DEC")) {
		bool inc = equals(mnemonic, "INC");
		if(count != 1 || (first.kind != OPERAND_REGISTER && first.kind != OPERAND_MEMORY)) return false;
		if(first.width == 0) return false;
		if(first.kind == OPERAND_REGISTER && first.width == 2)
			return emit_byte((uint8_t)((inc ? 0x40 : 0x48) + first.reg));
		return emit_byte(first.width == 1 ? 0xFE : 0xFF) && emit_modrm_operand(&first, inc ? 0 : 1);
	}
	if(equals(mnemonic, "NEG") || equals(mnemonic, "NOT")) {
		if(count != 1 || (first.kind != OPERAND_REGISTER && first.kind != OPERAND_MEMORY) || first.width == 0) return false;
		return emit_byte(first.width == 1 ? 0xF6 : 0xF7) && emit_modrm_operand(&first, equals(mnemonic, "NOT") ? 2 : 3);
	}

	return false;
}

static bool equals(const char *left, const char *right) {
	uint8_t i = 0;
	while(left[i] != '\0' && right[i] != '\0' && upper_char(left[i]) == right[i]) i++;
	return left[i] == '\0' && right[i] == '\0';
}

static bool parse_line(char *line) {
	char *p = line;
	char *line_start = line;
	char name[MAX_SYMBOL_NAME + 1];
	char mnemonic[MAX_SYMBOL_NAME + 1];
	uint8_t length;
	uint16_t value;
	bool known;
	bool used_symbol;

	/* Remove comments, but leave semicolons inside quoted DB strings alone. */
	{
		char quote = 0;
		for(char *q = line; *q != '\0'; q++) {
			if(quote != 0) {
				if(*q == quote) quote = 0;
			} else if(*q == '\'' || *q == '"') quote = *q;
			else if(*q == ';') {
				*q = '\0';
				break;
			}
		}
	}
	trim_right(line);
	skip_spaces(&p);
	if(*p == '\0') return true;

	/* A label can precede a statement on the same source line. */
	if(identifier_start(*p)) {
		char *after_name;
		if(!read_identifier(&p, name, sizeof(name))) return false;
		after_name = p;
		skip_spaces(&p);
		if(*p == ':') {
			if(!pass_two) {
				if(!define_symbol(name, current_address(), false)) return false;
			} else {
				int index = find_symbol(name);
				if(index < 0 || symbols[index].is_equ || symbols[index].value != current_address()) return false;
			}
			p++;
			skip_spaces(&p);
			if(*p == '\0') return true;
		} else {
			p = after_name;
			/* EQU is the only statement whose name precedes its directive. */
			char *q = p;
			skip_spaces(&q);
			if(identifier_start(*q)) {
				char directive[MAX_SYMBOL_NAME + 1];
				if(read_identifier(&q, directive, sizeof(directive)) && equals(directive, "EQU")) {
					skip_spaces(&q);
					if(!parse_expression(&q, &value, false, &known, &used_symbol)) return false;
					skip_spaces(&q);
					if(*q != '\0') return false;
					if(pass_two) {
						int index = find_symbol(name);
						return index >= 0 && symbols[index].is_equ && symbols[index].value == value;
					}
					return define_symbol(name, value, true);
				}
			}
			p = line_start;
		}
	}

	/* The fallback above returns to the start of the line, including indentation. */
	skip_spaces(&p);
	if(!read_identifier(&p, mnemonic, sizeof(mnemonic))) return false;
	for(length = 0; mnemonic[length] != '\0'; length++) mnemonic[length] = upper_char(mnemonic[length]);
	skip_spaces(&p);
	if(equals(mnemonic, "ORG")) {
		if(origin_seen || emitted_anything) return false;
		if(!parse_expression(&p, &value, false, &known, &used_symbol) || !known) return false;
		skip_spaces(&p);
		if(*p != '\0') return false;
		if(pass_two) {
			if(origin != value) return false;
		} else origin = value;
		origin_seen = true;
		return true;
	}
	if(equals(mnemonic, "DB") || equals(mnemonic, "DW"))
		return parse_data_list(&p, equals(mnemonic, "DW"));
	return assemble_instruction(mnemonic, p);
}

static bool run_pass(bool second_pass) {
	source_t source;
	bool success = true;
	pass_two = second_pass;
	line_number = 0;
	output_length = 0;
	emitted_anything = false;
	origin_seen = false;
	if(!source_open(&source)) return false;
	while(source_getline(&source, line_buffer)) {
		if(!parse_line(line_buffer)) {
			printf("ASM: line %u: invalid or unsupported syntax\n", line_number);
			failed = true;
			success = false;
			break;
		}
	}
	source_close(&source);
	return success && !failed;
}

static bool normalize_path(const char *input, char *output, uint8_t capacity, char default_drive) {
	uint16_t length = 0;
	uint16_t out = 0;
	uint16_t i;
	char drive = default_drive;
	while(input[length] == ' ' || input[length] == '\t') length++;
	input += length;
	length = 0;
	while(input[length] != '\0' && input[length] != ' ' && input[length] != '\t') length++;
	if(length == 0) return false;
	if(input[1] == ':') {
		drive = upper_char(input[0]);
		if((drive != 'A' && drive != 'B') || length == 2) return false;
		input += 2;
		length -= 2;
	} else {
		if(capacity < 3) return false;
		output[out++] = drive;
		output[out++] = ':';
	}
	if(length == 0 || length > 25 || out + length >= capacity) return false;
	for(i = 0; i < length; i++) {
		if(input[i] == ':' || input[i] == '"' || input[i] == '\'') return false;
		output[out++] = upper_char(input[i]);
	}
	output[out] = '\0';
	return true;
}

static bool parse_command_line(void) {
	const char *p = COMMAND_LINE;
	char first[28];
	char second[28];
	char *cwd;
	char default_drive = 'A';
	uint16_t length = 0;
	uint16_t i;
	char *(*getenv_vector)(const char *) = (char *(*)(const char *))0x0006;

	cwd = getenv_vector("CWD");
	if(cwd != 0 && (cwd[0] == 'A' || cwd[0] == 'B') && cwd[1] == ':' && cwd[2] == '\0')
		default_drive = cwd[0];
	while(*p == ' ' || *p == '\t') p++;
	while(p[length] != '\0' && p[length] != ' ' && p[length] != '\t') length++;
	if(length == 0 || length >= sizeof(first)) return false;
	for(i = 0; i < length; i++) first[i] = p[i];
	first[length] = '\0';
	p += length;
	while(*p == ' ' || *p == '\t') p++;
	length = 0;
	while(p[length] != '\0' && p[length] != ' ' && p[length] != '\t') length++;
	if(length == 0 || length >= sizeof(second)) return false;
	for(i = 0; i < length; i++) second[i] = p[i];
	second[length] = '\0';
	p += length;
	while(*p == ' ' || *p == '\t') p++;
	if(*p != '\0') return false;
	return normalize_path(first, source_path, sizeof(source_path), default_drive) &&
	       normalize_path(second, output_path, sizeof(output_path), default_drive);
}

static bool output_file_exists(void) {
	uint16_t handle = sys_open(output_path);
	if(handle == SYSCALL_INVALID_HANDLE) return false;
	(void)sys_close(handle);
	return true;
}

static bool write_output(void) {
	uint16_t handle;
	uint16_t offset = 0;
	uint16_t count;
	uint16_t i;
	bool success = true;

	handle = sys_create(output_path);
	if(handle == SYSCALL_INVALID_HANDLE) return false;
	while(offset < output_length) {
		count = output_length - offset;
		if(count > IO_SIZE) count = IO_SIZE;
		for(i = 0; i < count; i++) copy_buffer[i] = (char)*aux_pointer((uint16_t)(offset + i));
		if(sys_write(handle, copy_buffer, count) != count) {
			success = false;
			break;
		}
		offset += count;
	}
	if(sys_close(handle) != 0) success = false;
	if(!success) (void)sys_delete(output_path);
	return success;
}

static void cleanup_aux(void) {
	(void)sys_release_aux();
}

void asm_main(void) {
	if(!parse_command_line()) {
		printf("Usage: ASM source.asm output.bin\n");
		return;
	}
	if(equals(source_path, output_path)) {
		printf("ASM: source and output must be different files\n");
		return;
	}
	if(output_file_exists()) {
		printf("ASM: output exists; delete it first\n");
		return;
	}
	if(sys_claim_aux() != 0) {
		printf("ASM: SEG_AUX is already in use\n");
		return;
	}

	symbol_count = 0;
	origin = 0;
	origin_seen = false;
	failed = false;
	if(!run_pass(false)) {
		if(!failed) printf("ASM: cannot read source file\n");
		cleanup_aux();
		return;
	}
	if(!run_pass(true)) {
		if(!failed) printf("ASM: cannot read source file\n");
		cleanup_aux();
		return;
	}
	if(output_length == 0) {
		printf("ASM: source produced no output\n");
		cleanup_aux();
		return;
	}
	if(!write_output()) printf("ASM: could not write output file\n");
	else printf("ASM: wrote %u bytes\n", output_length);
	cleanup_aux();
}
