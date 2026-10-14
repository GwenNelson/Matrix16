#pragma once

#include <stdint.h>
#include <stdbool.h>

#define KCONSOLE_INPUT_SIZE 256
#define KCONSOLE_ANSI_PARAMS 4
#define VRAM_SEG_INVALID 0xFFFF
#define CONSOLE_INVALID 0xFF

enum {
	PAGENUM_VC0 = 0,
	PAGENUM_VC1 = 1,
	PAGENUM_VC2 = 2,
	PAGENUM_VC3 = 3
};

/* Logical per-console state suitable for task save and restore. VRAM, the
 * active console, callbacks, and physical keyboard state are not included. */
typedef struct kconsole_saved_state_t {
	uint8_t row;
	uint8_t col;
	uint8_t saved_row;
	uint8_t saved_col;
	uint8_t attribute;
	uint8_t wrap;
	uint8_t caps_lock;
	uint8_t num_lock;
	uint8_t ansi_state;
	uint8_t param_index;
	uint16_t params[KCONSOLE_ANSI_PARAMS];
	uint16_t input_length;
	char input[KCONSOLE_INPUT_SIZE];
} kconsole_saved_state_t;

/* Initialize pages before enabling keyboard input. Uses 80x25 color text mode. */
void kconsole_init(uint8_t page_num);
uint16_t kconsole_vram_segment(uint8_t page_num);
void kconsole_set_save_cb(void (*callback)(uint8_t console_num));
void kconsole_set_switch_cb(void (*callback)(uint8_t console_num));

/* These functions may call the BIOS for the visible page: use outside ISRs.
 * switchto also attempts an LED update when interrupts are enabled. */
void kconsole_switchto(uint8_t page_num);
void kconsole_move_cursor(uint8_t page_num, uint8_t row, uint8_t col);
void kconsole_putc(uint8_t page_num, char c);
void kconsole_puts(uint8_t page_num, char *string);

/* These functions copy only private logical state and never read or write the
 * console's VRAM. The caller must ensure the console is not being modified. */
bool kconsole_save_state(uint8_t page_num, kconsole_saved_state_t *state);
bool kconsole_restore_state(uint8_t page_num, const kconsole_saved_state_t *state);

/* XT set 1, US layout. Registered callbacks run here in IRQ context: they must
 * defer BIOS calls, console switching, disk access, and other blocking work. */
void kconsole_kb_callback(uint8_t scancode);

/* Nonblocking, single consumer per console. Input is never echoed here. */
uint16_t kconsole_input_available(uint8_t page_num);
bool kconsole_getc(uint8_t page_num, char *out);

/* Discard queued input, then sleep until a fresh input byte arrives. This must
 * be called from ordinary kernel context after the keyboard ISR is installed. */
void kconsole_wait_for_key(uint8_t page_num);
