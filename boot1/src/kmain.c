#include <stdint.h>

extern uint16_t bios_read_sector(uint16_t es, uint16_t bx, uint16_t cylinder, uint16_t head, uint16_t sector, uint16_t drive);

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
		bios_putchar(*s);
		s++;
	}
}

char buf[512];

void kernel_main(void) {
	bios_puts("\r\n");
	bios_puts("\r\nMatrix16 Kernel loaded!\r\n");
	bios_puts("\r\n");

	bios_puts("About to check reading from ROOTFS floppy...\r\n");
	if(bios_read_sector(0x1000, ((uint16_t)&buf),0,0,1,1) != 0 ) {
		bios_puts("And failed\r\n");
	} else {
		bios_puts((char*)buf);
	}

	for(;;);
}
