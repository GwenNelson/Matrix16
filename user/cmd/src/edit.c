#include <stdbool.h>
#include <stdint.h>

#include "stdio.h"
#include "syscalls.h"

#define COMMAND_LINE ((const char*)0x0100)
#define AUX_SEGMENT 0x7000
#define STATIC_BUFFER_SIZE 8192
#define AUX_BUFFER_SIZE 0xFFFF
#define IO_BUFFER_SIZE 512
#define SCREEN_ROWS 24
#define SCREEN_COLS 80

/* The linker keeps this static buffer below the shell's command stack. */
static char static_buffer[STATIC_BUFFER_SIZE];
static char io_buffer[IO_BUFFER_SIZE];
static char display_line[SCREEN_COLS];
static char filename[28];

static uint16_t text_length;
static uint16_t text_capacity = STATIC_BUFFER_SIZE;
static uint16_t cursor_offset;
static uint16_t top_line;
static bool using_aux;

typedef union {
	struct {
		uint16_t offset;
		uint16_t segment;
	} parts;
	char __far *pointer;
} far_address_t;

typedef char *(*shell_getenv_t)(const char *name);

static char __far *aux_byte(uint16_t offset) {
	far_address_t address;

	address.parts.offset = offset;
	address.parts.segment = AUX_SEGMENT;
	return address.pointer;
}

static void write_bytes(const char *bytes, uint16_t count) {
	if(count != 0) (void)sys_write(1, bytes, count);
}

static void write_text(const char *text) {
	uint16_t length = 0;

	while(text[length] != '\0') length++;
	write_bytes(text, length);
}

static uint8_t text_byte(uint16_t offset) {
	if(using_aux) return (uint8_t)*aux_byte(offset);
	return (uint8_t)static_buffer[offset];
}

static void set_text_byte(uint16_t offset, uint8_t value) {
	if(using_aux) *aux_byte(offset) = (char)value;
	else static_buffer[offset] = (char)value;
}

/* Switch only after the fixed command buffer is full. */
static bool grow_buffer(void) {
	uint16_t i;

	if(using_aux) return text_length < AUX_BUFFER_SIZE;
	if(sys_claim_aux() != 0) return false;
	for(i = 0; i < text_length; i++) *aux_byte(i) = static_buffer[i];
	using_aux = true;
	text_capacity = AUX_BUFFER_SIZE;
	return true;
}

static bool append_byte(uint8_t value) {
	if(text_length == 0xFFFF) return false;
	if(text_length == text_capacity && !grow_buffer()) return false;
	set_text_byte(text_length, value);
	text_length++;
	return true;
}

static void reset_buffer(void) {
	text_length = 0;
	cursor_offset = 0;
	top_line = 0;
}

static bool prepare_filename(void) {
	const char *argument = COMMAND_LINE;
	char drive = 'A';
	char *cwd;
	uint16_t name_length = 0;
	uint16_t path_length = 0;
	uint16_t i;

	/* The shell exports getenv through the fixed near-call vector at 0006h. */
	cwd = ((shell_getenv_t)0x0006)("CWD");
	if(cwd != 0 && (cwd[0] == 'A' || cwd[0] == 'B') &&
	   cwd[1] == ':' && cwd[2] == '\0') drive = cwd[0];

	while(*argument == ' ') argument++;
	if(argument[0] == '\0') return false;
	while(argument[name_length] != '\0') name_length++;
	while(name_length != 0 && argument[name_length - 1] == ' ') name_length--;
	if(name_length == 0) return false;

	if(name_length >= 2 && argument[1] == ':') {
		if((argument[0] != 'A' && argument[0] != 'B') || name_length == 2)
			return false;
		drive = argument[0];
		argument += 2;
		name_length -= 2;
	} else {
		filename[path_length++] = drive;
		filename[path_length++] = ':';
	}

	if(name_length > 25) return false;
	for(i = 0; i < name_length; i++) {
		if(argument[i] == ' ' || argument[i] == ':') return false;
		filename[path_length++] = argument[i];
	}
	filename[path_length] = '\0';
	return true;
}

/* Load text using a small near buffer; spill into AUX only when it fills. */
static bool load_file(void) {
	uint16_t handle = sys_open(filename);
	uint16_t count;
	uint16_t i;

	if(handle == SYSCALL_INVALID_HANDLE) return false;
	if(using_aux) {
		if(sys_release_aux() != 0) {
			(void)sys_close(handle);
			return false;
		}
		using_aux = false;
		text_capacity = STATIC_BUFFER_SIZE;
	}
	reset_buffer();

	for(;;) {
		count = sys_read(handle, io_buffer, IO_BUFFER_SIZE);
		if(count == 0) break;
		for(i = 0; i < count; i++) {
			uint8_t value = (uint8_t)io_buffer[i];
			if(value == '\r') continue;
			if(!append_byte(value)) {
				(void)sys_close(handle);
				return false;
			}
		}
	}
	if(sys_close(handle) != 0) return false;
	cursor_offset = 0;
	return true;
}

static bool flush_output(uint16_t handle, uint16_t *used) {
	if(*used == 0) return true;
	if(sys_write(handle, io_buffer, *used) != *used) return false;
	*used = 0;
	return true;
}

static bool output_byte(uint16_t handle, uint8_t value, uint16_t *used) {
	if(*used == IO_BUFFER_SIZE && !flush_output(handle, used)) return false;
	io_buffer[(*used)++] = (char)value;
	return true;
}

/* Save with DOS CR/LF line endings. A new handle starts at byte zero. */
static bool save_file(void) {
	uint16_t handle;
	uint16_t used = 0;
	uint16_t i;
	bool success = true;

	(void)sys_delete(filename);
	handle = sys_create(filename);
	if(handle == SYSCALL_INVALID_HANDLE) return false;

	for(i = 0; i < text_length; i++) {
		uint8_t value = text_byte(i);
		if(value == '\n' && !output_byte(handle, '\r', &used)) {
			success = false;
			break;
		}
		if(!output_byte(handle, value, &used)) {
			success = false;
			break;
		}
	}
	if(success && !flush_output(handle, &used)) success = false;
	if(sys_close(handle) != 0) success = false;
	return success;
}

static uint16_t line_count(void) {
	uint16_t lines = 1;
	uint16_t i;

	for(i = 0; i < text_length; i++)
		if(text_byte(i) == '\n') lines++;
	return lines;
}

static uint16_t line_start(uint16_t wanted_line) {
	uint16_t line = 0;
	uint16_t offset = 0;

	while(line < wanted_line && offset < text_length) {
		if(text_byte(offset++) == '\n') line++;
	}
	return offset;
}

static uint16_t line_end(uint16_t start) {
	while(start < text_length && text_byte(start) != '\n') start++;
	return start;
}

static uint16_t cursor_line(void) {
	uint16_t line = 0;
	uint16_t i;

	for(i = 0; i < cursor_offset; i++)
		if(text_byte(i) == '\n') line++;
	return line;
}

static void put_decimal(char *output, uint16_t *length, uint16_t value) {
	char digits[5];
	uint8_t count = 0;

	do {
		digits[count++] = (char)('0' + value % 10);
		value /= 10;
	} while(value != 0);
	while(count != 0) output[(*length)++] = digits[--count];
}

/* ANSI uses one-based cursor coordinates. */
static void move_cursor(uint8_t row, uint8_t column) {
	char sequence[8];
	uint16_t length = 0;

	sequence[length++] = 27;
	sequence[length++] = '[';
	put_decimal(sequence, &length, (uint16_t)row + 1);
	sequence[length++] = ';';
	put_decimal(sequence, &length, (uint16_t)column + 1);
	sequence[length++] = 'H';
	write_bytes(sequence, length);
}

static void draw_screen(const char *message) {
	uint16_t lines = line_count();
	uint16_t active_line = cursor_line();
	uint16_t active_start;
	uint16_t active_column;
	uint16_t horizontal_start = 0;
	uint8_t row;

	if(active_line < top_line) top_line = active_line;
	if(active_line >= top_line + SCREEN_ROWS)
		top_line = active_line - SCREEN_ROWS + 1;

	write_text("\x1b[2J\x1b[?25h");
	for(row = 0; row < SCREEN_ROWS; row++) {
		uint16_t line = top_line + row;
		uint16_t start;
		uint16_t end;
		uint16_t column = 0;
		uint16_t offset;

		if(line >= lines) break;
		start = line_start(line);
		end = line_end(start);
		if(line == active_line) {
			active_start = start;
			active_column = cursor_offset - start;
			if(active_column >= SCREEN_COLS)
				horizontal_start = active_column - SCREEN_COLS + 1;
		}
		for(column = 0; column < SCREEN_COLS; column++) display_line[column] = ' ';
		offset = start + (line == active_line ? horizontal_start : 0);
		column = 0;
		while(offset < end && column < SCREEN_COLS) {
			uint8_t value = text_byte(offset++);
			display_line[column++] = value >= 0x20 && value <= 0x7e ? (char)value : ' ';
		}
		move_cursor(row, 0);
		write_bytes(display_line, SCREEN_COLS);
	}

	move_cursor(24, 0);
	write_text(message == 0 ? "Ctrl-L Load | Ctrl-S Save | Ctrl-Q Quit" : message);
	active_start = line_start(active_line);
	active_column = cursor_offset - active_start;
	if(active_column >= SCREEN_COLS) active_column = SCREEN_COLS - 1;
	else if(active_column >= horizontal_start) active_column -= horizontal_start;
	move_cursor((uint8_t)(active_line - top_line), (uint8_t)active_column);
}

static void insert_byte(uint8_t value, const char **message) {
	uint16_t i;

	if(text_length == 0xFFFF || (text_length == text_capacity && !grow_buffer())) {
		*message = "No room: AUX segment is unavailable";
		return;
	}
	for(i = text_length; i > cursor_offset; i--)
		set_text_byte(i, text_byte(i - 1));
	set_text_byte(cursor_offset++, value);
	text_length++;
}

static void erase_before_cursor(void) {
	uint16_t i;
	if(cursor_offset == 0) return;
	for(i = cursor_offset - 1; i + 1 < text_length; i++)
		set_text_byte(i, text_byte(i + 1));
	cursor_offset--;
	text_length--;
}

static void erase_at_cursor(void) {
	uint16_t i;
	if(cursor_offset >= text_length) return;
	for(i = cursor_offset; i + 1 < text_length; i++)
		set_text_byte(i, text_byte(i + 1));
	text_length--;
}

static void move_vertical(int direction) {
	uint16_t line = cursor_line();
	uint16_t start = line_start(line);
	uint16_t column = cursor_offset - start;
	uint16_t lines = line_count();
	uint16_t target_line;
	uint16_t target_start;
	uint16_t target_end;

	if(direction < 0) {
		if(line == 0) return;
		target_line = line - 1;
	} else {
		if(line + 1 >= lines) return;
		target_line = line + 1;
	}
	target_start = line_start(target_line);
	target_end = line_end(target_start);
	if(column > target_end - target_start) column = target_end - target_start;
	cursor_offset = target_start + column;
}

enum {
	KEY_OTHER = 256,
	KEY_UP,
	KEY_DOWN,
	KEY_LEFT,
	KEY_RIGHT,
	KEY_DELETE,
	KEY_HOME,
	KEY_END
};

static uint16_t read_key(void) {
	char value;
	char second;
	char third;

	while(sys_read(0, &value, 1) == 0) { }
	if((uint8_t)value != 27) return (uint8_t)value;
	while(sys_read(0, &second, 1) == 0) { }
	if(second != '[') return KEY_OTHER;
	while(sys_read(0, &third, 1) == 0) { }
	if(third == 'A') return KEY_UP;
	if(third == 'B') return KEY_DOWN;
	if(third == 'C') return KEY_RIGHT;
	if(third == 'D') return KEY_LEFT;
	if(third == 'H') return KEY_HOME;
	if(third == 'F') return KEY_END;
	if(third == '3') {
		while(sys_read(0, &value, 1) == 0) { }
		return value == '~' ? KEY_DELETE : KEY_OTHER;
	}
	return KEY_OTHER;
}

static void edit_loop(void) {
	const char *message = 0;
	uint16_t key;

	for(;;) {
		draw_screen(message);
		message = 0;
		key = read_key();

		if(key == 17) { /* Ctrl-Q */
			if(using_aux) {
				if(sys_release_aux() != 0) {
					message = "Could not release AUX; editor remains open";
					continue;
				}
				using_aux = false;
			}
			write_text("\x1b[2J\x1b[H");
			return;
		}
		if(key == 12) { /* Ctrl-L reloads the command-line filename. */
			message = load_file() ? "File loaded" : "Load failed or buffer limit reached";
			continue;
		}
		if(key == 19) { /* Ctrl-S saves the current contents. */
			message = save_file() ? "File saved" : "Save failed";
			continue;
		}

		switch(key) {
			case KEY_LEFT:
				if(cursor_offset != 0) cursor_offset--;
			break;
			case KEY_RIGHT:
				if(cursor_offset < text_length) cursor_offset++;
			break;
			case KEY_UP:
				move_vertical(-1);
			break;
			case KEY_DOWN:
				move_vertical(1);
			break;
			case KEY_DELETE:
				erase_at_cursor();
			break;
			case KEY_HOME:
				cursor_offset = line_start(cursor_line());
			break;
			case KEY_END:
				cursor_offset = line_end(line_start(cursor_line()));
			break;
			case '\b':
				erase_before_cursor();
			break;
			case '\r':
			case '\n':
				insert_byte('\n', &message);
			break;
			default:
				if(key >= 0x20 && key <= 0x7e)
					insert_byte((uint8_t)key, &message);
			break;
		}
	}
}

void edit_main(void) {
	if(!prepare_filename()) {
		printf("Usage: EDIT [A:|B:]filename\n");
		return;
	}
	reset_buffer();
	(void)load_file();
	edit_loop();
}
