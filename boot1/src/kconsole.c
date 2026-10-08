#include "kconsole.h"

#define CONSOLE_COUNT 4
#define ROWS 25
#define COLS 80
#define CELLS (ROWS * COLS)
#define INPUT_SIZE 256
#define ANSI_PARAMS 4

/* Screen contents stay in VRAM; each page occupies 4096 bytes. */
static const uint16_t page_segments[CONSOLE_COUNT] = {
	0xb800, 0xb900, 0xba00, 0xbb00
};

typedef struct console_state_t {
	uint8_t row;
	uint8_t col;
	uint8_t saved_row;
	uint8_t saved_col;
	uint8_t attribute;
	bool wrap;
	bool caps_lock;
	bool num_lock;
	uint8_t ansi_state; /* 0: text, 1: ESC, 2: CSI, 3: discarded CSI. */
	uint8_t param_index;
	uint16_t params[ANSI_PARAMS];
	volatile uint16_t input_head;
	volatile uint16_t input_tail;
	volatile char input[INPUT_SIZE];
} console_state_t;

static console_state_t consoles[CONSOLE_COUNT];
static uint8_t active_console;
static void (*kb_save_cb)(void);
static void (*kb_switch_cb)(uint8_t console_num);

/* Modifiers describe the physical keyboard, locks belong to each console. */
static struct {
	uint8_t shift;
	uint8_t ctrl;
	uint8_t alt;
	bool caps_held;
	bool num_held;
	bool extended;
	uint8_t pause_bytes;
	uint8_t led_mask;
	volatile uint8_t led_reply;
} keyboard;

static uint8_t inb(uint16_t port) {
	uint8_t value;
	__asm__ volatile ("inb %1, %0" : "=a" (value) : "d" (port));
	return value;
}

static void outb(uint16_t port, uint8_t value) {
	__asm__ volatile ("outb %0, %1" : : "a" (value), "d" (port));
}

static volatile uint16_t __far *page_memory(uint8_t page_num) {
	return (volatile uint16_t __far *)((uint32_t)page_segments[page_num] << 16);
}

static void update_cursor(uint8_t page_num) {
	uint16_t ax = 0x0200;
	uint16_t bx = (uint16_t)page_num << 8;
	uint16_t dx = ((uint16_t)consoles[page_num].row << 8) | consoles[page_num].col;
	__asm__ volatile ("int $0x10" : "+a" (ax), "+b" (bx), "+d" (dx) : : "cc", "memory");
}

/* Optional AT keyboard LED support. Replies arrive through the existing IRQ1
 * path, so no scan codes are consumed here. Timeouts also permit XT keyboards. */
static bool keyboard_command(uint8_t command) {
	uint16_t timeout = 10000;
	while(inb(0x64) & 2) {
		if(--timeout == 0)
			return false;
	}
	keyboard.led_reply = 0;
	outb(0x60, command);
	timeout = 10000;
	while(keyboard.led_reply == 0) {
		if(--timeout == 0)
			return false;
	}
	return keyboard.led_reply == 0xfa;
}

static void update_leds(void) {
	uint16_t flags;
	__asm__ volatile ("pushf; popw %0" : "=r" (flags));
	if(!(flags & 0x0200))
		return;
	if(keyboard_command(0xed))
		keyboard_command(keyboard.led_mask);
}

static void select_led_state(void) {
	console_state_t *console = &consoles[active_console];
	keyboard.led_mask = 0;
	if(console->caps_lock)
		keyboard.led_mask |= 4;
	if(console->num_lock)
		keyboard.led_mask |= 2;
}

static void clear_cells(uint8_t page_num, uint16_t first, uint16_t end) {
	volatile uint16_t __far *screen = page_memory(page_num);
	uint16_t blank = ((uint16_t)consoles[page_num].attribute << 8) | ' ';
	for(uint16_t i = first; i < end; i++)
		screen[i] = blank;
}

static void linefeed(uint8_t page_num) {
	console_state_t *console = &consoles[page_num];
	uint8_t row = console->row;
	if(row < ROWS - 1) {
		row++;
	} else {
		volatile uint16_t __far *screen = page_memory(page_num);
		for(uint16_t i = 0; i < CELLS - COLS; i++)
			screen[i] = screen[i + COLS];
		clear_cells(page_num, CELLS - COLS, CELLS);
	}
	kconsole_move_cursor(page_num, row, console->col);
}

void kconsole_init(uint8_t page_num) {
	if(page_num >= CONSOLE_COUNT)
		return;
	console_state_t *console = &consoles[page_num];
	console->row = 0;
	console->col = 0;
	console->saved_row = 0;
	console->saved_col = 0;
	console->attribute = 0x07;
	console->wrap = false;
	console->caps_lock = false;
	console->num_lock = false;
	console->ansi_state = 0;
	console->param_index = 0;
	for(uint8_t i = 0; i < ANSI_PARAMS; i++)
		console->params[i] = 0;
	console->input_head = 0;
	console->input_tail = 0;
	clear_cells(page_num, 0, CELLS);
	if(page_num == active_console) {
		select_led_state();
		update_cursor(page_num);
	}
}

void kconsole_set_save_cb(void (*callback)(void)) {
	kb_save_cb = callback;
}

void kconsole_set_switch_cb(void (*callback)(uint8_t console_num)) {
	kb_switch_cb = callback;
}

void kconsole_switchto(uint8_t page_num) {
	if(page_num >= CONSOLE_COUNT)
		return;
	uint16_t ax = 0x0500 | page_num;
	__asm__ volatile ("int $0x10" : "+a" (ax) : : "cc", "memory");
	active_console = page_num;
	update_cursor(page_num);
	select_led_state();
	update_leds();
}

void kconsole_move_cursor(uint8_t page_num, uint8_t row, uint8_t col) {
	if(page_num >= CONSOLE_COUNT)
		return;
	console_state_t *console = &consoles[page_num];
	console->row = row < ROWS ? row : ROWS - 1;
	console->col = col < COLS ? col : COLS - 1;
	console->wrap = false;
	if(page_num == active_console)
		update_cursor(page_num);
}

/* ANSI color bit order is RGB; PC text attributes use BGR. */
static uint8_t ansi_color(uint8_t color) {
	return ((color & 1) << 2) | (color & 2) | ((color & 4) >> 2);
}

static void ansi_command(uint8_t page_num, char command) {
	console_state_t *console = &consoles[page_num];
	uint16_t amount = console->params[0];
	uint16_t row = console->row;
	uint16_t col = console->col;
	uint16_t position = row * COLS + col;
	if(amount == 0)
		amount = 1;

	switch(command) {
		case 'A':
			row = amount > row ? 0 : row - amount;
			break;
		case 'B':
			row += amount;
			break;
		case 'C':
			col += amount;
			break;
		case 'D':
			col = amount > col ? 0 : col - amount;
			break;
		case 'H':
		case 'f':
			row = (console->params[0] ? console->params[0] : 1) - 1;
			col = (console->params[1] ? console->params[1] : 1) - 1;
			break;
		case 'J':
			switch(console->params[0]) {
				case 0: clear_cells(page_num, position, CELLS); break;
				case 1: clear_cells(page_num, 0, position + 1); break;
				case 2: clear_cells(page_num, 0, CELLS); break;
			}
			return;
		case 'K':
			switch(console->params[0]) {
				case 0: clear_cells(page_num, position, (row + 1) * COLS); break;
				case 1: clear_cells(page_num, row * COLS, position + 1); break;
				case 2: clear_cells(page_num, row * COLS, (row + 1) * COLS); break;
			}
			return;
		case 'm':
			for(uint8_t i = 0; i <= console->param_index; i++) {
				uint16_t value = console->params[i];
				if(value == 0)
					console->attribute = 0x07;
				else if(value == 1)
					console->attribute |= 8;
				else if(value == 22)
					console->attribute &= ~8;
				else if(value >= 30 && value <= 37)
					console->attribute = (console->attribute & 0xf8) | ansi_color(value - 30);
				else if(value >= 40 && value <= 47)
					console->attribute = (console->attribute & 0x8f) | (ansi_color(value - 40) << 4);
				else if(value == 39)
					console->attribute = (console->attribute & 0xf8) | 7;
				else if(value == 49)
					console->attribute &= 0x8f;
			}
			return;
		case 's':
			console->saved_row = row;
			console->saved_col = col;
			return;
		case 'u':
			row = console->saved_row;
			col = console->saved_col;
			break;
		default:
			return;
	}
	if(row >= ROWS)
		row = ROWS - 1;
	if(col >= COLS)
		col = COLS - 1;
	kconsole_move_cursor(page_num, row, col);
}

void kconsole_putc(uint8_t page_num, char c) {
	if(page_num >= CONSOLE_COUNT)
		return;
	console_state_t *console = &consoles[page_num];
	uint8_t byte = (uint8_t)c;
	if(byte == 0x1b) {
		console->ansi_state = 1;
		return;
	}
	if(console->ansi_state == 1) {
		console->ansi_state = 0;
		if(c == '[') {
			console->ansi_state = 2;
			console->param_index = 0;
			for(uint8_t i = 0; i < ANSI_PARAMS; i++)
				console->params[i] = 0;
		}
		return;
	}
	if(console->ansi_state >= 2) {
		if(byte >= 0x40 && byte <= 0x7e) {
			if(console->ansi_state == 2)
				ansi_command(page_num, c);
			console->ansi_state = 0;
		} else if(console->ansi_state == 2) {
			if(c >= '0' && c <= '9') {
				uint16_t value = console->params[console->param_index];
				value = value * 10 + c - '0';
				console->params[console->param_index] = value > 999 ? 999 : value;
			} else if(c == ';' && console->param_index < ANSI_PARAMS - 1) {
				console->param_index++;
			} else {
				console->ansi_state = 3; /* Discard unsupported/oversized CSI. */
			}
		}
		return;
	}

	switch(c) {
		case '\r':
			kconsole_move_cursor(page_num, console->row, 0);
			return;
		case '\n': /* ANSI LF preserves the column; CR is separate. */
			linefeed(page_num);
			return;
		case '\b':
			kconsole_move_cursor(page_num, console->row, console->col ? console->col - 1 : 0);
			return;
		case '\t':
			kconsole_move_cursor(page_num, console->row, (console->col + 8) & ~7);
			return;
	}
	if(byte < 0x20 || byte == 0x7f)
		return;
	if(console->wrap) {
		console->col = 0;
		linefeed(page_num);
	}
	volatile uint16_t __far *screen = page_memory(page_num);
	screen[(uint16_t)console->row * COLS + console->col] = ((uint16_t)console->attribute << 8) | byte;
	if(console->col == COLS - 1) {
		kconsole_move_cursor(page_num, console->row, console->col);
		console->wrap = true;
	} else {
		kconsole_move_cursor(page_num, console->row, console->col + 1);
	}
}

/* One IRQ producer and one foreground consumer. Publish the head only once
 * the whole sequence is stored. The counters permit all 256 bytes to be used. */
static void enqueue(const char *bytes, uint8_t length) {
	console_state_t *console = &consoles[active_console];
	uint16_t head = console->input_head;
	uint16_t used = (uint16_t)(head - console->input_tail);
	if(length > INPUT_SIZE - used)
		return;
	for(uint8_t i = 0; i < length; i++)
		console->input[(uint8_t)(head + i)] = bytes[i];
	console->input_head = head + length;
}

bool kconsole_getc(uint8_t page_num, char *out) {
	if(page_num >= CONSOLE_COUNT || out == 0)
		return false;
	console_state_t *console = &consoles[page_num];
	uint16_t tail = console->input_tail;
	if(tail == console->input_head)
		return false;
	*out = console->input[(uint8_t)tail];
	console->input_tail = tail + 1;
	return true;
}

static const char normal_keys[58] = {
	[0x01] = 27,
	[0x02] = '1', [0x03] = '2', [0x04] = '3', [0x05] = '4',
	[0x06] = '5', [0x07] = '6', [0x08] = '7', [0x09] = '8',
	[0x0a] = '9', [0x0b] = '0', [0x0c] = '-', [0x0d] = '=',
	[0x0e] = '\b', [0x0f] = '\t',
	[0x10] = 'q', [0x11] = 'w', [0x12] = 'e', [0x13] = 'r',
	[0x14] = 't', [0x15] = 'y', [0x16] = 'u', [0x17] = 'i',
	[0x18] = 'o', [0x19] = 'p', [0x1a] = '[', [0x1b] = ']',
	[0x1c] = '\n',
	[0x1e] = 'a', [0x1f] = 's', [0x20] = 'd', [0x21] = 'f',
	[0x22] = 'g', [0x23] = 'h', [0x24] = 'j', [0x25] = 'k',
	[0x26] = 'l', [0x27] = ';', [0x28] = '\'', [0x29] = '`',
	[0x2b] = '\\',
	[0x2c] = 'z', [0x2d] = 'x', [0x2e] = 'c', [0x2f] = 'v',
	[0x30] = 'b', [0x31] = 'n', [0x32] = 'm', [0x33] = ',',
	[0x34] = '.', [0x35] = '/', [0x37] = '*', [0x39] = ' '
};

static const char shifted_keys[58] = {
	[0x02] = '!', [0x03] = '@', [0x04] = '#', [0x05] = '$',
	[0x06] = '%', [0x07] = '^', [0x08] = '&', [0x09] = '*',
	[0x0a] = '(', [0x0b] = ')', [0x0c] = '_', [0x0d] = '+',
	[0x1a] = '{', [0x1b] = '}', [0x27] = ':', [0x28] = '"',
	[0x29] = '~', [0x2b] = '|', [0x33] = '<', [0x34] = '>',
	[0x35] = '?'
};

static void navigation_key(uint8_t code) {
	char sequence[4] = { 27, '[', 0, '~' };
	uint8_t length = 3;
	switch(code) {
		case 0x47: sequence[2] = 'H'; break;
		case 0x48: sequence[2] = 'A'; break;
		case 0x49: sequence[2] = '5'; length = 4; break;
		case 0x4b: sequence[2] = 'D'; break;
		case 0x4d: sequence[2] = 'C'; break;
		case 0x4f: sequence[2] = 'F'; break;
		case 0x50: sequence[2] = 'B'; break;
		case 0x51: sequence[2] = '6'; length = 4; break;
		case 0x52: sequence[2] = '2'; length = 4; break;
		case 0x53: sequence[2] = '3'; length = 4; break;
		default: return;
	}
	enqueue(sequence, length);
}

void kconsole_kb_callback(uint8_t scancode) {
	if(scancode == 0xfa || scancode == 0xfe) {
		keyboard.led_reply = scancode;
		return;
	}
	if(keyboard.pause_bytes != 0) {
		keyboard.pause_bytes--;
		return;
	}
	if(scancode == 0xe1) {
		keyboard.pause_bytes = 5;
		return;
	}
	if(scancode == 0xe0) {
		keyboard.extended = true;
		return;
	}
	bool extended = keyboard.extended;
	keyboard.extended = false;
	bool released = (scancode & 0x80) != 0;
	uint8_t code = scancode & 0x7f;
	uint8_t bit = extended ? 2 : 1;
	console_state_t *console = &consoles[active_console];

	if(code == 0x1d || code == 0x38) {
		uint8_t *modifier = code == 0x1d ? &keyboard.ctrl : &keyboard.alt;
		if(released)
			*modifier &= ~bit;
		else
			*modifier |= bit;
		return;
	}
	if(!extended && (code == 0x2a || code == 0x36)) {
		bit = code == 0x2a ? 1 : 2;
		if(released)
			keyboard.shift &= ~bit;
		else
			keyboard.shift |= bit;
		return;
	}
	if(!extended && (code == 0x3a || code == 0x45)) {
		bool *held = code == 0x3a ? &keyboard.caps_held : &keyboard.num_held;
		if(released) {
			*held = false;
		} else if(!*held) {
			*held = true;
			if(code == 0x3a)
				console->caps_lock = !console->caps_lock;
			else
				console->num_lock = !console->num_lock;
			select_led_state();
		}
		return;
	}
	if(released)
		return;
	if(!extended && keyboard.ctrl && keyboard.alt) {
		if(code == 0x1f && kb_save_cb != 0) {
			kb_save_cb();
			return;
		}
		if(code >= 0x3b && code <= 0x3e) {
			if(kb_switch_cb != 0)
				kb_switch_cb(code - 0x3b);
			return;
		}
	}
	if(code >= 0x47 && code <= 0x53) {
		static const char keypad[] = "789-456+1230.";
		if(!extended && (code == 0x4a || code == 0x4e ||
		    console->num_lock != (keyboard.shift != 0))) {
			char c = keypad[code - 0x47];
			enqueue(&c, 1);
		} else {
			navigation_key(code);
		}
		return;
	}
	if(extended && code != 0x1c && code != 0x35)
		return;
	if(code >= sizeof(normal_keys))
		return;
	char c = normal_keys[code];
	if(c == 0)
		return;
	if(!extended && keyboard.shift && shifted_keys[code] != 0)
		c = shifted_keys[code];
	if(c >= 'a' && c <= 'z') {
		if(keyboard.ctrl)
			c = c - 'a' + 1;
		else if(keyboard.shift || console->caps_lock)
			c = c - 'a' + 'A';
	} else if(keyboard.ctrl) {
		if(c >= '[' && c <= '_')
			c -= '@';
		else if(c == ' ' || c == '@')
			c = 0;
	}
	enqueue(&c, 1);
}
