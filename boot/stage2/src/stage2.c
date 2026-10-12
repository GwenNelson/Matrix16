#include <stdint.h>

static void bios_putchar(char c) {
        uint16_t ax = 0x0e00 | (uint8_t)c;
        uint16_t bx = 0x0000;
        __asm__ volatile (
                "int $0x10"
                : "+a" (ax),
                  "+b" (bx)
                :
                : "cc"
        );
}

static void bios_puts(char* s) {
        while(*s) {
                switch(*s) {
                        case '\t':
                                for(int i=0; i<8; i++) bios_putchar(' ');
                        break;
                        case '\n':
                                bios_putchar('\r');
                                bios_putchar('\n');
                        break;
                        default:
                                bios_putchar(*s);
                        break;
                }

                s++;
        }
}

void stage2_main(void) {
	bios_puts("Matrix16-Boot stage2\n");
	for(;;);
}
