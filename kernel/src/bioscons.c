#include <stdint.h>

#include "bioscons.h"

uint8_t cur_page_no = 0;

void bios_putchar(char c, uint8_t page_no) {
	uint16_t ax = 0x0e00 | (uint8_t)c;
	uint16_t bx = 0x0000 | page_no;
	__asm__ volatile (
		"int $0x10"
		: "+a" (ax),
		  "+b" (bx)
		:
		: "cc"
	);
}

void bios_swap_page(uint8_t new_page) {
	uint16_t ax = 0x0500 | new_page;
	__asm__ volatile (
		"int $0x10"
		: "+a" (ax)
		:
		: "cc"
	);
}

void bios_puts(char *s) {
	while(*s) {
		switch(*s) {
			case '\t':
				for(int i = 0; i < 8; i++)
					bios_putchar(' ', cur_page_no);
			break;
			case '\n':
				bios_putchar('\r', cur_page_no);
				bios_putchar('\n', cur_page_no);
			break;
			default:
				bios_putchar(*s, cur_page_no);
			break;
		}

		s++;
	}
}

void bios_put_decimal(uint16_t n) {
	char digits[5];
	uint8_t count = 0;

	do {
		digits[count++] = '0' + (n % 10);
		n /= 10;
	} while(n != 0);

	while(count != 0)
		bios_putchar(digits[--count], cur_page_no);
}

void bios_put_hex(uint16_t n) {
	static const char digits[] = "0123456789ABCDEF";
	uint8_t shift = 12;
	uint8_t started = 0;

	bios_puts("0x");
	while(shift != 0 || !started) {
		uint8_t digit = (n >> shift) & 0x0f;

		if(digit != 0 || started || shift == 0) {
			bios_putchar(digits[digit], cur_page_no);
			started = 1;
		}
		if(shift == 0)
			break;
		shift -= 4;
	}
}
