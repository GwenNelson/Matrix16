#pragma once

#include <stdbool.h>
#include <stdint.h>

extern uint8_t cur_page_no;

void bios_putchar(char c, uint8_t page_no);
void bios_swap_page(uint8_t new_page);
void bios_puts(char *s);
void bios_put_decimal(uint16_t n);
void bios_put_hex(uint16_t n);

/* Consume one buffered BIOS key. The result is BIOS AX: scan code in the high
 * byte and ASCII in the low byte. Returns false when no key is waiting. */
bool bios_poll_key(uint16_t *key);
