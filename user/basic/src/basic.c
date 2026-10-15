#include <stdbool.h>
#include <stdio.h>
#include <stdint.h>

#include "syscalls.h"

#define PROGRAM_BYTES 24576
#define VALUE_BYTES 8192
#define INPUT_BYTES 128
#define STRING_BYTES 128
#define VARIABLE_LIMIT 64
#define NAME_BYTES 9
#define FOR_LIMIT 16
#define GOSUB_LIMIT 16

typedef enum {
	VALUE_NUMBER,
	VALUE_STRING,
	VALUE_ARRAY
} variable_type_t;

typedef struct {
	char name[NAME_BYTES];
	uint8_t type;
	int16_t number;
	uint16_t pool_offset;
	uint16_t count;
} variable_t;

typedef struct {
	uint8_t variable;
	int16_t limit;
	int16_t step;
	uint16_t body_offset;
} for_frame_t;

typedef enum {
	FLOW_NEXT,
	FLOW_JUMP,
	FLOW_END,
	FLOW_ERROR
} flow_t;

typedef struct {
	uint8_t variable;
	bool array;
	uint16_t index;
} target_t;

static char program[PROGRAM_BYTES];
static uint8_t value_pool[VALUE_BYTES];
static variable_t variables[VARIABLE_LIMIT];
static char input_line[INPUT_BYTES];
static char string_temp[STRING_BYTES];
static uint16_t program_length;
static uint16_t value_used;
static uint8_t variable_count;
static const char *parse_at;
static bool parse_error;
static uint16_t jump_offset;
static uint16_t current_line;
static for_frame_t for_stack[FOR_LIMIT];
static uint16_t for_depth;
static uint16_t gosub_stack[GOSUB_LIMIT];
static uint16_t gosub_depth;

static bool load_one_line(char *line);

static void write_text(const char *text) {
	printf("%s",text);
}

static void write_char(char value) {
	(void)sys_write(1, &value, 1);
}

static void write_unsigned(uint16_t value) {
	printf("%u",value);
}

static void write_number(int16_t value) {
	uint16_t magnitude;
	if(value < 0) {
		write_char('-');
		magnitude = (uint16_t)(0 - (uint16_t)value);
	} else magnitude = (uint16_t)value;
	write_unsigned(magnitude);
}

static char upper_char(char value) {
	if(value >= 'a' && value <= 'z') return (char)(value - ('a' - 'A'));
	return value;
}

static bool is_letter(char value) {
	value = upper_char(value);
	return value >= 'A' && value <= 'Z';
}

static bool is_digit(char value) {
	return value >= '0' && value <= '9';
}

static bool is_name_char(char value) {
	return is_letter(value) || is_digit(value);
}

static void skip_spaces(void) {
	while(*parse_at == ' ' || *parse_at == '\t') parse_at++;
}

static bool keyword_at(const char *keyword) {
	const char *p = parse_at;
	while(*keyword != '\0') {
		if(upper_char(*p) != *keyword++) return false;
		p++;
	}
	return !is_name_char(*p) && *p != '$';
}

static bool take_keyword(const char *keyword) {
	skip_spaces();
	if(!keyword_at(keyword)) return false;
	while(*keyword++ != '\0') parse_at++;
	return true;
}

static bool parse_identifier(char name[NAME_BYTES], bool *string_type) {
	uint8_t length = 0;
	skip_spaces();
	if(!is_letter(*parse_at)) {
		parse_error = true;
		return false;
	}
	while(is_name_char(*parse_at)) {
		if(length + 1 >= NAME_BYTES) {
			parse_error = true;
			return false;
		}
		name[length++] = upper_char(*parse_at++);
	}
	*string_type = false;
	if(*parse_at == '$') {
		*string_type = true;
		parse_at++;
	}
	name[length] = '\0';
	return true;
}

static int find_variable(const char *name) {
	uint8_t i;
	for(i = 0; i < variable_count; i++) {
		uint8_t j = 0;
		while(variables[i].name[j] == name[j] && name[j] != '\0') j++;
		if(variables[i].name[j] == '\0' && name[j] == '\0') return i;
	}
	return -1;
}

static int get_variable(const char *name, uint8_t type, bool create) {
	int index = find_variable(name);
	uint8_t i;
	if(index >= 0) {
		if(variables[index].type != type) {
			parse_error = true;
			return -1;
		}
		return index;
	}
	if(!create || variable_count == VARIABLE_LIMIT) {
		parse_error = true;
		return -1;
	}
	index = variable_count++;
	for(i = 0; name[i] != '\0'; i++) variables[index].name[i] = name[i];
	variables[index].name[i] = '\0';
	variables[index].type = type;
	variables[index].number = 0;
	variables[index].pool_offset = 0;
	variables[index].count = 0;
	return index;
}

static int allocate_pool(uint16_t amount) {
	uint16_t i;
	uint16_t start = value_used;
	if(amount > VALUE_BYTES - value_used) {
		parse_error = true;
		return -1;
	}
	value_used += amount;
	for(i = start; i < value_used; i++) value_pool[i] = 0;
	return start;
}

static bool parse_number_literal(int16_t *value, uint16_t maximum) {
	uint16_t number = 0;
	bool found = false;
	skip_spaces();
	while(is_digit(*parse_at)) {
		uint8_t digit = (uint8_t)(*parse_at++ - '0');
		found = true;
		if(number > maximum / 10 ||
		   (number == maximum / 10 && digit > maximum % 10)) {
			parse_error = true;
			return false;
		}
		number = (uint16_t)(number * 10 + digit);
	}
	if(!found) return false;
	*value = (int16_t)number;
	return true;
}

static int16_t parse_expression(void);

static int16_t parse_factor(void) {
	int16_t value;
	char name[NAME_BYTES];
	bool is_string;
	int index;
	skip_spaces();
	if(*parse_at == '(') {
		parse_at++;
		value = parse_expression();
		skip_spaces();
		if(*parse_at++ != ')') parse_error = true;
		return value;
	}
	if(parse_number_literal(&value, 32767)) return value;
	if(parse_error) return 0;
	if(!parse_identifier(name, &is_string)) return 0;
	if(is_string) {
		parse_error = true;
		return 0;
	}
	skip_spaces();
	if(*parse_at == '(') {
		int16_t subscript;
		parse_at++;
		subscript = parse_expression();
		skip_spaces();
		if(*parse_at++ != ')') parse_error = true;
		index = get_variable(name, VALUE_ARRAY, false);
		if(index < 0 || subscript < 0 || (uint16_t)subscript >= variables[index].count) {
			parse_error = true;
			return 0;
		}
		return (int16_t)(value_pool[variables[index].pool_offset + (uint16_t)subscript * 2] |
			(value_pool[variables[index].pool_offset + (uint16_t)subscript * 2 + 1] << 8));
	}
	index = get_variable(name, VALUE_NUMBER, true);
	return index < 0 ? 0 : variables[index].number;
}

static int16_t parse_unary(void) {
	int16_t value;
	skip_spaces();
	if(*parse_at == '+') {
		parse_at++;
		return parse_unary();
	}
	if(*parse_at == '-') {
		uint16_t magnitude = 0;
		parse_at++;
		skip_spaces();
		if(is_digit(*parse_at)) {
			while(is_digit(*parse_at)) {
				uint8_t digit = (uint8_t)(*parse_at++ - '0');
				if(magnitude > 3276 || (magnitude == 3276 && digit > 8)) {
					parse_error = true;
					return 0;
				}
				magnitude = (uint16_t)(magnitude * 10 + digit);
			}
			return (int16_t)(0 - magnitude);
		}
		value = parse_unary();
		return (int16_t)(0 - (uint16_t)value);
	}
	return parse_factor();
}

static int16_t parse_term(void) {
	int16_t value = parse_unary();
	for(;;) {
		uint16_t left;
		int16_t right;
		skip_spaces();
		if(*parse_at == '*') {
			parse_at++;
			right = parse_unary();
			left = (uint16_t)value;
			left = (uint16_t)(left * (uint16_t)right);
			value = (int16_t)left;
		} else if(*parse_at == '/') {
			parse_at++;
			right = parse_unary();
			if(right == 0 || (value == (-32767 - 1) && right == -1)) {
				parse_error = true;
				return 0;
			}
			value = value / right;
		} else break;
	}
	return value;
}

static int16_t parse_expression(void) {
	int16_t value = parse_term();
	for(;;) {
		int16_t right;
		skip_spaces();
		if(*parse_at == '+') {
			parse_at++;
			right = parse_term();
			value = (int16_t)((uint16_t)value + (uint16_t)right);
		} else if(*parse_at == '-') {
			parse_at++;
			right = parse_term();
			value = (int16_t)((uint16_t)value - (uint16_t)right);
		} else break;
	}
	return value;
}

static bool starts_string_expression(void) {
	const char *p = parse_at;
	while(*p == ' ' || *p == '\t') p++;
	if(*p == '"') return true;
	if(!is_letter(*p)) return false;
	while(is_name_char(*p)) p++;
	return *p == '$';
}

static bool parse_string_expression(char output[STRING_BYTES]) {
	uint16_t length = 0;
	char name[NAME_BYTES];
	bool is_string;
	int index;
	skip_spaces();
	if(*parse_at == '"') {
		parse_at++;
		while(*parse_at != '\0' && *parse_at != '"') {
			if(length + 1 >= STRING_BYTES) {
				parse_error = true;
				return false;
			}
			output[length++] = *parse_at++;
		}
		if(*parse_at++ != '"') parse_error = true;
		output[length] = '\0';
		return !parse_error;
	}
	if(!parse_identifier(name, &is_string) || !is_string) {
		parse_error = true;
		return false;
	}
	index = get_variable(name, VALUE_STRING, true);
	if(index < 0) return false;
	if(variables[index].count == 0) {
		int offset = allocate_pool(STRING_BYTES);
		if(offset < 0) return false;
		variables[index].pool_offset = (uint16_t)offset;
		variables[index].count = STRING_BYTES;
	}
	for(length = 0; length < STRING_BYTES; length++) {
		output[length] = (char)value_pool[variables[index].pool_offset + length];
		if(output[length] == '\0') return true;
	}
	parse_error = true;
	return false;
}

static bool parse_target(target_t *target, bool create) {
	char name[NAME_BYTES];
	bool is_string;
	int index;
	int16_t subscript;
	if(!parse_identifier(name, &is_string)) return false;
	if(*parse_at == '(') {
		if(is_string) {
			parse_error = true;
			return false;
		}
		parse_at++;
		subscript = parse_expression();
		skip_spaces();
		if(*parse_at++ != ')') parse_error = true;
		index = get_variable(name, VALUE_ARRAY, false);
		if(index < 0 || subscript < 0 || (uint16_t)subscript >= variables[index].count) {
			parse_error = true;
			return false;
		}
		target->array = true;
		target->index = (uint16_t)subscript;
	} else {
		index = get_variable(name, is_string ? VALUE_STRING : VALUE_NUMBER, create);
		if(index < 0) return false;
		target->array = false;
		target->index = 0;
	}
	target->variable = (uint8_t)index;
	return true;
}

static int16_t target_number(const target_t *target) {
	variable_t *variable = &variables[target->variable];
	uint16_t offset;
	if(!target->array) return variable->number;
	offset = variable->pool_offset + target->index * 2;
	return (int16_t)(value_pool[offset] | (value_pool[offset + 1] << 8));
}

static void set_target_number(const target_t *target, int16_t number) {
	variable_t *variable = &variables[target->variable];
	uint16_t offset;
	if(!target->array) {
		variable->number = number;
		return;
	}
	offset = variable->pool_offset + target->index * 2;
	value_pool[offset] = (uint8_t)number;
	value_pool[offset + 1] = (uint8_t)((uint16_t)number >> 8);
}

static void set_target_string(uint8_t variable_index, const char *value) {
	uint16_t i;
	variable_t *variable = &variables[variable_index];
	if(variable->count == 0) {
		int offset = allocate_pool(STRING_BYTES);
		if(offset < 0) return;
		variable->pool_offset = (uint16_t)offset;
		variable->count = STRING_BYTES;
	}
	for(i = 0; i < STRING_BYTES; i++) {
		value_pool[variable->pool_offset + i] = (uint8_t)value[i];
		if(value[i] == '\0') return;
	}
	parse_error = true;
}

static bool parse_condition(bool *result) {
	int16_t left = parse_expression();
	int16_t right;
	uint8_t relation;
	skip_spaces();
	if(parse_at[0] == '<' && parse_at[1] == '=') {
		parse_at += 2; relation = 1;
	} else if(parse_at[0] == '>' && parse_at[1] == '=') {
		parse_at += 2; relation = 2;
	} else if(parse_at[0] == '<' && parse_at[1] == '>') {
		parse_at += 2; relation = 3;
	} else if(*parse_at == '<') {
		parse_at++; relation = 4;
	} else if(*parse_at == '>') {
		parse_at++; relation = 5;
	} else if(*parse_at == '=') {
		parse_at++; relation = 6;
	} else {
		parse_error = true;
		return false;
	}
	right = parse_expression();
	switch(relation) {
		case 1: *result = left <= right; break;
		case 2: *result = left >= right; break;
		case 3: *result = left != right; break;
		case 4: *result = left < right; break;
		case 5: *result = left > right; break;
		default: *result = left == right; break;
	}
	return true;
}

static bool at_end(void) {
	skip_spaces();
	return *parse_at == '\0';
}

static uint16_t record_size(uint16_t offset) {
	uint16_t size = 2;
	while(offset + size < program_length && program[offset + size] != '\0') size++;
	return size + 1;
}

static uint16_t record_number(uint16_t offset) {
	return (uint8_t)program[offset] | ((uint16_t)(uint8_t)program[offset + 1] << 8);
}

static uint16_t find_line(uint16_t line, bool *found) {
	uint16_t offset = 0;
	while(offset < program_length) {
		uint16_t number = record_number(offset);
		if(number >= line) {
			*found = number == line;
			return offset;
		}
		offset += record_size(offset);
	}
	*found = false;
	return program_length;
}

static void program_delete_all(void) {
	program_length = 0;
	variable_count = 0;
	value_used = 0;
}

static bool store_line(uint16_t line, const char *text) {
	bool found;
	uint16_t position = find_line(line, &found);
	uint16_t old_size = found ? record_size(position) : 0;
	uint16_t text_length = 0;
	uint16_t new_size;
	uint16_t i;
	while(text[text_length] != '\0') text_length++;
	if(text_length == 0) {
		for(i = position + old_size; i < program_length; i++) program[i - old_size] = program[i];
		program_length -= old_size;
		return true;
	}
	new_size = text_length + 3;
	if(new_size > PROGRAM_BYTES - (program_length - old_size)) return false;
	if(new_size > old_size) {
		for(i = program_length; i > position + old_size; i--)
			program[i + new_size - old_size - 1] = program[i - 1];
	} else if(new_size < old_size) {
		for(i = position + old_size; i < program_length; i++)
			program[i - old_size + new_size] = program[i];
	}
	program_length = program_length - old_size + new_size;
	program[position] = (char)line;
	program[position + 1] = (char)(line >> 8);
	for(i = 0; i < text_length; i++) program[position + 2 + i] = text[i];
	program[position + 2 + text_length] = '\0';
	return true;
}

static void write_line_number(uint16_t number) {
	write_unsigned(number);
	write_char(' ');
}

static void list_program(void) {
	uint16_t offset = 0;
	while(offset < program_length) {
		write_line_number(record_number(offset));
		write_text(&program[offset + 2]);
		write_text("\r\n");
		offset += record_size(offset);
	}
}

static void line_error(const char *message, uint16_t line) {
	write_text("? ");
	write_text(message);
	if(line != 0) {
		write_text(" IN ");
		write_unsigned(line);
	}
	write_text("\r\n");
}

static bool read_line(const char *prompt, char *buffer, uint16_t capacity) {
	uint16_t length = 0;
	char ch;
	bool too_long = false;
	uint8_t escape = 0;
	write_text(prompt);
	for(;;) {
		if(sys_read(0, &ch, 1) != 1) continue;
		if(escape == 1) {
			escape = ch == '[' ? 2 : 0;
			continue;
		}
		if(escape == 2) {
			if((uint8_t)ch >= 0x40 && (uint8_t)ch <= 0x7e) escape = 0;
			continue;
		}
		if((uint8_t)ch == 0x1b) { escape = 1; continue; }
		if(ch == '\r') continue;
		if(ch == '\n') {
			buffer[length] = '\0';
			write_text("\r\n");
			if(too_long) line_error("LINE TOO LONG", 0);
			return !too_long;
		}
		if(ch == '\b' || (uint8_t)ch == 0x7f) {
			if(length != 0) {
				length--;
				write_text("\b \b");
			}
			continue;
		}
		if((uint8_t)ch < 0x20 || (uint8_t)ch > 0x7e) continue;
		if(length + 1 >= capacity) { too_long = true; continue; }
		buffer[length++] = ch;
		write_char(ch);
	}
}

static bool normalize_filename(const char *source, char output[29]) {
	uint16_t input_length = 0;
	uint16_t base_start = 0;
	uint16_t base_length;
	uint16_t i;
	bool has_extension = false;
	char drive = 'A';
	while(source[input_length] == ' ' || source[input_length] == '\t') input_length++;
	source += input_length;
	input_length = 0;
	while(source[input_length] != '\0' && source[input_length] != ' ' && source[input_length] != '\t') input_length++;
	if(input_length == 0) return false;
	if(input_length >= 2 &&
	   (source[0] == 'A' || source[0] == 'a' || source[0] == 'B' || source[0] == 'b') &&
	   source[1] == ':') {
		drive = upper_char(source[0]);
		base_start = 2;
	}
	base_length = input_length - base_start;
	if(base_length == 0) return false;
	for(i = base_start; i < input_length; i++) {
		if(source[i] == ':' || source[i] == '/' || source[i] == '\\') return false;
		if(source[i] == '.') has_extension = true;
	}
	if(!has_extension && base_length + 4 > 25) return false;
	if(has_extension && base_length > 25) return false;
	output[0] = drive;
	output[1] = ':';
	for(i = 0; i < base_length; i++) output[i + 2] = upper_char(source[base_start + i]);
	if(!has_extension) {
		output[base_length + 2] = '.';
		output[base_length + 3] = 'B';
		output[base_length + 4] = 'A';
		output[base_length + 5] = 'S';
		output[base_length + 6] = '\0';
	} else output[base_length + 2] = '\0';
	return true;
}

static bool write_all(uint16_t handle, const char *bytes, uint16_t length) {
	return length == 0 || sys_write(handle, bytes, length) == length;
}

static bool save_program(const char *path) {
	char filename[29];
	char line_buffer[INPUT_BYTES + 8];
	uint16_t offset = 0;
	uint16_t handle;
	bool ok = true;
	if(!normalize_filename(path, filename)) return false;
	(void)sys_delete(filename);
	handle = sys_create(filename);
	if(handle == SYSCALL_INVALID_HANDLE) return false;
	while(offset < program_length && ok) {
		uint16_t length = 0;
		uint16_t number = record_number(offset);
		uint16_t value = number;
		char digits[5];
		uint8_t digit_count = 0;
		do {
			digits[digit_count++] = (char)('0' + value % 10);
			value /= 10;
		} while(value != 0);
		while(digit_count != 0) line_buffer[length++] = digits[--digit_count];
		line_buffer[length++] = ' ';
		{
			uint16_t i = 2;
			while(program[offset + i] != '\0') line_buffer[length++] = program[offset + i++];
		}
		line_buffer[length++] = '\r';
		line_buffer[length++] = '\n';
		ok = write_all(handle, line_buffer, length);
		offset += record_size(offset);
	}
	if(sys_close(handle) != 0) ok = false;
	return ok;
}

static bool load_program(const char *path) {
	char filename[29];
	char line[INPUT_BYTES];
	uint16_t handle;
	uint16_t used = 0;
	bool ok = true;
	if(!normalize_filename(path, filename)) return false;
	handle = sys_open(filename);
	if(handle == SYSCALL_INVALID_HANDLE) return false;
	program_delete_all();
	for(;;) {
		char chunk[32];
		uint16_t count = sys_read(handle, chunk, sizeof(chunk));
		uint16_t i;
		if(count == 0) break;
		for(i = 0; i < count; i++) {
			char ch = chunk[i];
			if(ch == '\r') continue;
			if(ch == '\n') {
				line[used] = '\0';
				if(used != 0 && !load_one_line(line)) { ok = false; break; }
				used = 0;
			} else if(used + 1 < sizeof(line)) line[used++] = ch;
			else { ok = false; break; }
		}
		if(!ok) break;
	}
	if(ok && used != 0) {
		line[used] = '\0';
		if(!load_one_line(line)) ok = false;
	}
	if(sys_close(handle) != 0) ok = false;
	if(!ok) program_delete_all();
	return ok;
}

/* Parse one numbered line from a BASIC source file. */
static bool load_one_line(char *line) {
	uint16_t number = 0;
	uint16_t i = 0;
	if(line[0] == '\0') return true;
	while(is_digit(line[i])) {
		uint8_t digit = (uint8_t)(line[i++] - '0');
		if(number > 3276 || (number == 3276 && digit > 7)) return false;
		number = (uint16_t)(number * 10 + digit);
	}
	if(number == 0 || (line[i] != ' ' && line[i] != '\t')) return false;
	while(line[i] == ' ' || line[i] == '\t') i++;
	return store_line(number, &line[i]);
}

static bool parse_line_number_input(const char **text, uint16_t *number, bool *has_number) {
	const char *p = *text;
	uint16_t value = 0;
	*has_number = false;
	while(*p == ' ' || *p == '\t') p++;
	if(!is_digit(*p)) { *text = p; return true; }
	*has_number = true;
	while(is_digit(*p)) {
		uint8_t digit = (uint8_t)(*p++ - '0');
		if(value > 3276 || (value == 3276 && digit > 7)) return false;
		value = (uint16_t)(value * 10 + digit);
	}
	if(*p != '\0' && *p != ' ' && *p != '\t') return false;
	if(value == 0) return false;
	while(*p == ' ' || *p == '\t') p++;
	*number = value;
	*text = p;
	return true;
}

static uint16_t find_target_line(uint16_t line, bool *found) {
	return find_line(line, found);
}

static bool starts_line_keyword(uint16_t offset, const char *keyword) {
	const char *text = &program[offset + 2];
	while(*text == ' ' || *text == '\t') text++;
	while(*keyword != '\0') if(upper_char(*text++) != *keyword++) return false;
	return !is_name_char(*text);
}

static bool matching_next(uint16_t from, uint8_t variable, uint16_t *after) {
	uint16_t offset = from;
	uint8_t nested = 0;
	while(offset < program_length) {
		if(starts_line_keyword(offset, "FOR")) nested++;
		else if(starts_line_keyword(offset, "NEXT")) {
			if(nested == 0) {
			const char *name = &program[offset + 2];
				uint8_t i = 0;
				while(*name == ' ' || *name == '\t') name++;
				name += 4;
				while(*name == ' ' || *name == '\t') name++;
				if(*name == '\0') {
					*after = offset + record_size(offset);
					return true;
				}
				while(is_name_char(*name) && i < NAME_BYTES - 1) {
					if(upper_char(*name++) != variables[variable].name[i++]) return false;
				}
				if(i == 0 || variables[variable].name[i] != '\0' ||
				   (*name != '\0' && *name != ' ' && *name != '\t')) return false;
				while(*name == ' ' || *name == '\t') name++;
				if(*name != '\0') return false;
				*after = offset + record_size(offset);
				return true;
			}
			nested--;
		}
		offset += record_size(offset);
	}
	(void)variable;
	return false;
}

static bool parse_assignment(bool allow_let);
static flow_t execute_statement(bool immediate, uint16_t next_pc);

static void parse_print(void) {
	bool newline = true;
	if(at_end()) { write_text("\r\n"); return; }
	for(;;) {
		if(starts_string_expression()) {
			if(!parse_string_expression(string_temp)) return;
			write_text(string_temp);
		} else {
			int16_t value = parse_expression();
			if(parse_error) return;
			write_number(value);
		}
		skip_spaces();
		if(*parse_at == ';') {
			parse_at++;
			newline = false;
			skip_spaces();
			if(*parse_at == '\0') break;
			continue;
		}
		if(*parse_at == ',') {
			parse_at++;
			write_char('\t');
			newline = true;
			continue;
		}
		if(!at_end()) parse_error = true;
		break;
	}
	if(newline && !parse_error) write_text("\r\n");
}

static bool parse_assignment(bool allow_let) {
	target_t target;
	if(allow_let) (void)take_keyword("LET");
	if(!parse_target(&target, true)) return false;
	skip_spaces();
	if(*parse_at++ != '=') { parse_error = true; return false; }
	if(variables[target.variable].type == VALUE_STRING) {
		if(!parse_string_expression(string_temp)) return false;
		set_target_string(target.variable, string_temp);
	} else set_target_number(&target, parse_expression());
	if(!at_end()) parse_error = true;
	return !parse_error;
}

static bool advance_for_variable(for_frame_t *frame) {
	int16_t current = variables[frame->variable].number;
	int16_t step = frame->step;
	int16_t next;
	if(step > 0) {
		if(current > 32767 - step) return false;
	} else {
		if(current < (-32767 - 1) - step) return false;
	}
	next = (int16_t)((uint16_t)current + (uint16_t)step);
	variables[frame->variable].number = next;
	if(step > 0) return next <= frame->limit;
	return next >= frame->limit;
}

static flow_t execute_statement(bool immediate, uint16_t next_pc) {
	int16_t value;
	char name[NAME_BYTES];
	bool is_string;
	skip_spaces();
	if(take_keyword("REM")) {
		while(*parse_at != '\0') parse_at++;
		return FLOW_NEXT;
	}
	if(take_keyword("PRINT")) { parse_print(); return parse_error ? FLOW_ERROR : FLOW_NEXT; }
	if(take_keyword("INPUT")) {
		target_t target;
		if(!parse_target(&target, true) || target.array) return FLOW_ERROR;
		if(!at_end()) { parse_error = true; return FLOW_ERROR; }
		write_text("? ");
		if(!read_line("", input_line, sizeof(input_line))) return FLOW_ERROR;
		if(variables[target.variable].type == VALUE_STRING) {
			set_target_string(target.variable, input_line);
		} else {
			const char *saved = parse_at;
			parse_at = input_line;
			value = parse_expression();
			if(parse_error || !at_end()) parse_error = true;
			parse_at = saved;
			if(!parse_error) variables[target.variable].number = value;
		}
		return parse_error ? FLOW_ERROR : FLOW_NEXT;
	}
	if(keyword_at("LET")) {
		if(!parse_assignment(true)) return FLOW_ERROR;
		return FLOW_NEXT;
	}
	if(take_keyword("GOTO")) {
		bool found;
		value = parse_expression();
		if(!at_end() || value <= 0 || immediate) return FLOW_ERROR;
		jump_offset = find_target_line((uint16_t)value, &found);
		if(!found) { parse_error = true; return FLOW_ERROR; }
		return FLOW_JUMP;
	}
	if(take_keyword("IF")) {
		bool condition;
		bool found;
		if(!parse_condition(&condition)) return FLOW_ERROR;
		if(!take_keyword("THEN")) return FLOW_ERROR;
		value = parse_expression();
		if(!at_end() || value <= 0) return FLOW_ERROR;
		if(condition) {
			if(immediate) return FLOW_ERROR;
			jump_offset = find_target_line((uint16_t)value, &found);
			if(!found) { parse_error = true; return FLOW_ERROR; }
			return FLOW_JUMP;
		}
		return FLOW_NEXT;
	}
	if(take_keyword("GOSUB")) {
		bool found;
		value = parse_expression();
		if(!at_end() || value <= 0 || immediate || gosub_depth == GOSUB_LIMIT) return FLOW_ERROR;
		jump_offset = find_target_line((uint16_t)value, &found);
		if(!found) { parse_error = true; return FLOW_ERROR; }
		gosub_stack[gosub_depth++] = next_pc;
		return FLOW_JUMP;
	}
	if(take_keyword("RETURN")) {
		if(!at_end() || immediate || gosub_depth == 0) return FLOW_ERROR;
		jump_offset = gosub_stack[--gosub_depth];
		return FLOW_JUMP;
	}
	if(take_keyword("FOR")) {
		int index;
		int16_t first, limit, step = 1;
		if(immediate) return FLOW_ERROR;
		if(!parse_identifier(name, &is_string) || is_string) return FLOW_ERROR;
		index = get_variable(name, VALUE_NUMBER, true);
		if(index < 0) return FLOW_ERROR;
		skip_spaces();
		if(*parse_at++ != '=') return FLOW_ERROR;
		first = parse_expression();
		if(!take_keyword("TO")) return FLOW_ERROR;
		limit = parse_expression();
		if(take_keyword("STEP")) step = parse_expression();
		if(!at_end() || step == 0 || for_depth == FOR_LIMIT) return FLOW_ERROR;
		variables[index].number = first;
		if((step > 0 && first > limit) || (step < 0 && first < limit)) {
			uint16_t after;
			if(!matching_next(next_pc, (uint8_t)index, &after)) return FLOW_ERROR;
			jump_offset = after;
			return FLOW_JUMP;
		}
		for_stack[for_depth].variable = (uint8_t)index;
		for_stack[for_depth].limit = limit;
		for_stack[for_depth].step = step;
		for_stack[for_depth].body_offset = next_pc;
		for_depth++;
		return FLOW_NEXT;
	}
	if(take_keyword("NEXT")) {
		for_frame_t *frame;
		if(immediate || for_depth == 0) return FLOW_ERROR;
		frame = &for_stack[for_depth - 1];
		skip_spaces();
		if(*parse_at != '\0') {
			if(!parse_identifier(name, &is_string) || is_string ||
			   find_variable(name) != frame->variable) return FLOW_ERROR;
		}
		if(!at_end()) return FLOW_ERROR;
		if(advance_for_variable(frame)) {
			jump_offset = frame->body_offset;
			return FLOW_JUMP;
		}
		for_depth--;
		return FLOW_NEXT;
	}
	if(take_keyword("DIM")) {
		if(!parse_identifier(name, &is_string) || is_string) return FLOW_ERROR;
		if(*parse_at++ != '(') return FLOW_ERROR;
		value = parse_expression();
		if(*parse_at++ != ')' || !at_end() || value < 0 || value >= VALUE_BYTES / 2)
			return FLOW_ERROR;
		{
			int index = get_variable(name, VALUE_ARRAY, true);
			int pool_offset;
			uint16_t count = (uint16_t)value + 1;
			if(index < 0 || count > VALUE_BYTES / 2) return FLOW_ERROR;
			pool_offset = allocate_pool(count * 2);
			if(pool_offset < 0) return FLOW_ERROR;
			variables[index].pool_offset = (uint16_t)pool_offset;
			variables[index].count = count;
		}
		return FLOW_NEXT;
	}
	if(take_keyword("END")) return at_end() ? FLOW_END : FLOW_ERROR;
	if(take_keyword("CLS")) {
		if(!at_end()) return FLOW_ERROR;
		write_text("\x1b[2J\x1b[H");
		return FLOW_NEXT;
	}
	if(is_letter(*parse_at)) {
		if(!parse_assignment(false)) return FLOW_ERROR;
		return FLOW_NEXT;
	}
	parse_error = true;
	return FLOW_ERROR;
}

static flow_t execute_line(const char *text, bool immediate, uint16_t next_pc) {
	flow_t flow;
	parse_at = text;
	parse_error = false;
	flow = execute_statement(immediate, next_pc);
	if(parse_error) return FLOW_ERROR;
	if(!at_end() && flow != FLOW_END) return FLOW_ERROR;
	return flow;
}

static void run_program(void) {
	uint16_t pc = 0;
	flow_t flow;
	variable_count = 0;
	value_used = 0;
	for_depth = 0;
	gosub_depth = 0;
	while(pc < program_length) {
		uint16_t size = record_size(pc);
		uint16_t next_pc = pc + size;
		current_line = record_number(pc);
		flow = execute_line(&program[pc + 2], false, next_pc);
		if(flow == FLOW_ERROR) {
			line_error("ERROR", current_line);
			return;
		}
		if(flow == FLOW_END) return;
		pc = flow == FLOW_JUMP ? jump_offset : next_pc;
	}
}

static bool command_is(const char *text, const char *word, const char **argument) {
	const char *p = text;
	while(*word != '\0') {
		if(upper_char(*p) != *word++) return false;
		p++;
	}
	if(is_name_char(*p)) return false;
	while(*p == ' ' || *p == '\t') p++;
	*argument = p;
	return true;
}

int main(void) {
	const char *argument;
	write_text("Matrix16 BASIC 0.1\r\n");
	write_text("16-bit integer BASIC. Type HELP for commands.\r\n");
	for(;;) {
		bool has_number;
		uint16_t line_number;
		const char *text;
		if(!read_line("> ", input_line, sizeof(input_line))) continue;
		text = input_line;
		if(parse_line_number_input(&text, &line_number, &has_number) && has_number) {
			if(!store_line(line_number, text)) line_error("PROGRAM FULL", 0);
			continue;
		}
		if(command_is(input_line, "HELP", &argument)) {
			write_text("Enter numbered lines; a number alone deletes that line.\r\n");
			write_text("LIST NEW RUN LOAD name SAVE name BYE; immediate PRINT/INPUT/LET.\r\n");
			write_text("Statements: PRINT INPUT LET IF GOTO FOR NEXT GOSUB RETURN DIM END REM.\r\n");
			continue;
		}
		if(command_is(input_line, "LIST", &argument)) { list_program(); continue; }
		if(command_is(input_line, "NEW", &argument)) { program_delete_all(); continue; }
		if(command_is(input_line, "RUN", &argument)) { run_program(); continue; }
		if(command_is(input_line, "LOAD", &argument)) {
			if(*argument == '\0' || !load_program(argument)) line_error("LOAD FAILED", 0);
			continue;
		}
		if(command_is(input_line, "SAVE", &argument)) {
			if(*argument == '\0' || !save_program(argument)) line_error("SAVE FAILED", 0);
			continue;
		}
		if(command_is(input_line, "BYE", &argument)) {
			(void)sys_term(SYSCALL_TASK_SELF);
			for(;;) { }
		}
		if(*input_line == '\0') continue;
		if(execute_line(input_line, true, 0) == FLOW_ERROR) line_error("ERROR", 0);
	}
}
