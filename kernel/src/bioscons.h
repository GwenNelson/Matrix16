#pragma once

#include <stdint.h>

extern uint8_t cur_page_no;

void bios_putchar(char c, uint8_t page_no);
void bios_swap_page(uint8_t new_page);
void bios_puts(char *s);
void bios_put_decimal(uint16_t n);
void bios_put_hex(uint16_t n);
