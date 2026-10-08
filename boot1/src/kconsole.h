#pragma once

#include <stdint.h>
#include <stdbool.h>

enum {
	PAGENUM_VC0 = 0,
	PAGENUM_VC1 = 1,
	PAGENUM_VC2 = 2,
	PAGENUM_VC3 = 3
};

/* Initialize pages before enabling keyboard input. Uses 80x25 color text mode. */
void kconsole_init(uint8_t page_num);
void kconsole_set_save_cb(void (*callback)(void));
void kconsole_set_switch_cb(void (*callback)(uint8_t console_num));

/* These functions may call the BIOS for the visible page: use outside ISRs.
 * switchto also attempts an LED update when interrupts are enabled. */
void kconsole_switchto(uint8_t page_num);
void kconsole_move_cursor(uint8_t page_num, uint8_t row, uint8_t col);
void kconsole_putc(uint8_t page_num, char c);

/* XT set 1, US layout. Registered callbacks run here in IRQ context: they must
 * defer BIOS calls, console switching, disk access, and other blocking work. */
void kconsole_kb_callback(uint8_t scancode);

/* Nonblocking, single consumer per console. Input is never echoed here. */
bool kconsole_getc(uint8_t page_num, char *out);
