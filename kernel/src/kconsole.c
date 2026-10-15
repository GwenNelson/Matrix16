#include "kconsole.h"
#include "kmemmap.h"
#include "utils.h"

#define CONSOLE_COUNT 4
#define ROWS 25
#define COLS 80
#define CELLS (ROWS * COLS)

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
	uint8_t ansi_state; /* 0: text, 1: ESC, 2: CSI, 3: legacy discarded CSI. */
	uint8_t param_index;
	uint16_t params[KCONSOLE_ANSI_PARAMS];
	volatile uint16_t input_head;
	volatile uint16_t input_tail;
	volatile char input[KCONSOLE_INPUT_SIZE];
} console_state_t;

static console_state_t consoles[CONSOLE_COUNT] SEG_SYS_BSS;
static uint8_t active_console;
static void (*kb_save_cb)(uint8_t console_num);
static void (*kb_switch_cb)(uint8_t console_num);

/* Console state lives in SEG_SYS; ordinary kernel data accesses use DS=SEG_KERN. */
static console_state_t __far *console_state(uint8_t page_num) {
	uint16_t offset = (uint16_t)consoles + (uint16_t)page_num * sizeof(console_state_t);
	return (console_state_t __far *)mk_farptr(SEG_SYS, offset);
}

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

uint16_t kconsole_vram_segment(uint8_t page_num) {
	if(page_num >= CONSOLE_COUNT) return VRAM_SEG_INVALID;
	return page_segments[page_num];
}

uint8_t kconsole_active_console(void) {
	return active_console;
}

static void update_cursor(uint8_t page_num) {
	uint16_t ax = 0x0200;
	uint16_t bx = (uint16_t)page_num << 8;
	console_state_t __far *console = console_state(page_num);
	uint16_t dx = ((uint16_t)console->row << 8) | console->col;
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
	console_state_t __far *console = console_state(active_console);
	keyboard.led_mask = 0;
	if(console->caps_lock)
		keyboard.led_mask |= 4;
	if(console->num_lock)
		keyboard.led_mask |= 2;
}

static void clear_cells(uint8_t page_num, uint16_t first, uint16_t end) {
	volatile uint16_t __far *screen = page_memory(page_num);
	uint16_t blank = ((uint16_t)console_state(page_num)->attribute << 8) | ' ';
	for(uint16_t i = first; i < end; i++)
		screen[i] = blank;
}

static void linefeed(uint8_t page_num) {
	console_state_t __far *console = console_state(page_num);
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
	console_state_t __far *console = console_state(page_num);
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
	for(uint8_t i = 0; i < KCONSOLE_ANSI_PARAMS; i++)
		console->params[i] = 0;
	console->input_head = 0;
	console->input_tail = 0;
	clear_cells(page_num, 0, CELLS);
	if(page_num == active_console) {
		select_led_state();
		update_cursor(page_num);
	}
}

void kconsole_set_save_cb(void (*callback)(uint8_t console_num)) {
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
	console_state_t __far *console = console_state(page_num);
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
	console_state_t __far *console = console_state(page_num);
	uint16_t row = console->row;
	uint16_t col = console->col;
	uint16_t position = row * COLS + col;

	switch(command) {
		case 'H':
			row = (console->params[0] ? console->params[0] : 1) - 1;
			col = (console->params[1] ? console->params[1] : 1) - 1;
			break;
		case 'J':
			if(console->params[0] == 2) clear_cells(page_num, 0, CELLS);
			return;
		case 'K':
			if(console->params[0] == 0) clear_cells(page_num, position, (row + 1) * COLS);
			return;
		case 'm':
			if(console->param_index != 0) return;
			if(console->params[0] == 0)
				console->attribute = 0x07;
			else if(console->params[0] >= 30 && console->params[0] <= 37)
				console->attribute = (console->attribute & 0xf8) | ansi_color(console->params[0] - 30);
			else if(console->params[0] >= 40 && console->params[0] <= 47)
				console->attribute = (console->attribute & 0x8f) | (ansi_color(console->params[0] - 40) << 4);
			return;
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
	console_state_t __far *console = console_state(page_num);
	uint8_t byte = (uint8_t)c;
	if(console->ansi_state > 2) console->ansi_state = 0;
	if(byte == 0x1b) {
		console->ansi_state = 1;
		return;
	}
	if(console->ansi_state == 1) {
		console->ansi_state = 0;
		if(c == '[') {
			console->ansi_state = 2;
			console->param_index = 0;
			console->params[0] = 0;
			console->params[1] = 0;
		}
		return;
	}
	if(console->ansi_state == 2) {
		if(byte >= 0x40 && byte <= 0x7e) {
			ansi_command(page_num, c);
			console->ansi_state = 0;
		} else {
			if(c >= '0' && c <= '9') {
				uint16_t value = console->params[console->param_index];
				value = value * 10 + c - '0';
				console->params[console->param_index] = value > 255 ? 255 : value;
			} else if(c == ';' && console->param_index == 0) {
				console->param_index++;
			} else {
				console->ansi_state = 0;
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

void kconsole_puts(uint8_t page_num, char *string) {
	while(*string)
		kconsole_putc(page_num, *string++);
}

void kconsole_putuint(uint8_t console_num, uint16_t n) {
	char digits[5];
	uint8_t count = 0;

	do {
		digits[count++] = '0' + n % 10;
		n /= 10;
	} while(n != 0);

	while(count != 0)
		kconsole_putc(console_num, digits[--count]);
}

bool kconsole_save_state(uint8_t page_num, kconsole_saved_state_t __far *state) {
	if(page_num >= CONSOLE_COUNT || state == 0) return false;

	console_state_t __far *console = console_state(page_num);
	uint16_t tail = console->input_tail;
	uint16_t input_length = (uint16_t)(console->input_head - tail);
	if(input_length > KCONSOLE_INPUT_SIZE) return false;

	state->row = console->row;
	state->col = console->col;
	state->saved_row = console->saved_row;
	state->saved_col = console->saved_col;
	state->attribute = console->attribute;
	state->wrap = console->wrap;
	state->caps_lock = console->caps_lock;
	state->num_lock = console->num_lock;
	state->ansi_state = console->ansi_state;
	state->param_index = console->param_index;
	for(uint8_t i = 0; i < KCONSOLE_ANSI_PARAMS; i++) state->params[i] = console->params[i];
	state->input_length = input_length;
	for(uint16_t i = 0; i < input_length; i++) state->input[i] = console->input[(uint8_t)(tail + i)];

	return true;
}

bool kconsole_restore_state(uint8_t page_num, const kconsole_saved_state_t __far *state) {
	if(page_num >= CONSOLE_COUNT || state == 0) return false;
	if(state->row >= ROWS || state->col >= COLS) return false;
	if(state->saved_row >= ROWS || state->saved_col >= COLS) return false;
	if(state->wrap > 1 || state->caps_lock > 1 || state->num_lock > 1) return false;
	if(state->ansi_state > 3 || state->param_index >= KCONSOLE_ANSI_PARAMS) return false;
	if(state->input_length > KCONSOLE_INPUT_SIZE) return false;
	for(uint8_t i = 0; i < KCONSOLE_ANSI_PARAMS; i++) {
		if(state->params[i] > 999) return false;
	}

	console_state_t __far *console = console_state(page_num);
	console->row = state->row;
	console->col = state->col;
	console->saved_row = state->saved_row;
	console->saved_col = state->saved_col;
	console->attribute = state->attribute;
	console->wrap = state->wrap;
	console->caps_lock = state->caps_lock;
	console->num_lock = state->num_lock;
	console->ansi_state = state->ansi_state;
	console->param_index = state->param_index;
	for(uint8_t i = 0; i < KCONSOLE_ANSI_PARAMS; i++) console->params[i] = state->params[i];
	console->input_tail = 0;
	for(uint16_t i = 0; i < state->input_length; i++) console->input[(uint8_t)i] = state->input[i];
	console->input_head = state->input_length;

	if(page_num == active_console) {
		select_led_state();
		update_cursor(page_num);
	}

	return true;
}

/* One IRQ producer and one foreground consumer. Publish the head only once
 * the whole sequence is stored. The counters permit all 256 bytes to be used. */
static void enqueue(const char *bytes, uint8_t length) {
	console_state_t __far *console = console_state(active_console);
	uint16_t head = console->input_head;
	uint16_t used = (uint16_t)(head - console->input_tail);
	if(length > KCONSOLE_INPUT_SIZE - used)
		return;
	for(uint8_t i = 0; i < length; i++)
		console->input[(uint8_t)(head + i)] = bytes[i];
	console->input_head = head + length;
}

uint16_t kconsole_input_available(uint8_t page_num) {
	if(page_num >= CONSOLE_COUNT) return 0;

	console_state_t __far *console = console_state(page_num);
	return (uint16_t)(console->input_head - console->input_tail);
}

bool kconsole_getc(uint8_t page_num, char *out) {
	if(page_num >= CONSOLE_COUNT || out == 0)
		return false;
	console_state_t __far *console = console_state(page_num);
	uint16_t tail = console->input_tail;
	if(tail == console->input_head)
		return false;
	*out = console->input[(uint8_t)tail];
	console->input_tail = tail + 1;
	return true;
}

uint16_t kconsole_gets(uint8_t page_num, char *buffer, uint16_t size) {
	uint16_t flags;
	uint16_t length = 0;
	char c;

	if(page_num >= CONSOLE_COUNT || buffer == 0 || size == 0) return 0;
	buffer[0] = '\0';
	__asm__ volatile ("pushf; popw %0" : "=r" (flags));

	for(;;) {
		__asm__ volatile ("cli" : : : "memory");
		while(!kconsole_getc(page_num, &c))
			__asm__ volatile ("sti; hlt; cli" : : : "memory");
		__asm__ volatile ("pushw %0; popf" : : "r" (flags) : "memory", "cc");

		if(c == '\n' || c == '\r') {
			kconsole_puts(page_num, "\r\n");
			break;
		}

		if(c == '\b') {
			if(length != 0) {
				length--;
				buffer[length] = '\0';
				kconsole_puts(page_num, "\b \b");
			}
			continue;
		}

		if((uint8_t)c < 0x20 || (uint8_t)c > 0x7e || length >= size - 1)
			continue;

		buffer[length++] = c;
		buffer[length] = '\0';
		kconsole_putc(page_num, c);
	}

	__asm__ volatile ("pushw %0; popf" : : "r" (flags) : "memory", "cc");
	return length;
}

void kconsole_wait_for_key(uint8_t page_num) {
	uint16_t flags;
	char ignored;

	if(page_num >= CONSOLE_COUNT) return;

	__asm__ volatile ("pushf; popw %0; cli" : "=r" (flags) : : "memory");
	while(kconsole_getc(page_num, &ignored))
		;

	while(!kconsole_getc(page_num, &ignored))
		__asm__ volatile ("sti; hlt; cli" : : : "memory");

	__asm__ volatile ("pushw %0; popf" : : "r" (flags) : "memory", "cc");
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
	console_state_t __far *console = console_state(active_console);

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
			kb_save_cb(active_console);
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
