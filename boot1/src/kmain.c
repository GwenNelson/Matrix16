#include <stdint.h>

typedef struct registers_t {
    uint16_t es;       // Top of stack after segment push
    uint16_t ds;       
    uint16_t di;       // pusha structure starts here
    uint16_t si;
    uint16_t bp;
    uint16_t sp;       // Original SP value before pusha
    uint16_t bx;
    uint16_t dx;
    uint16_t cx;
    uint16_t ax;       // Bottom of pusha (lowest memory address)
} __attribute__((packed)) registers_t;

extern uint16_t bios_read_sector(uint16_t es, uint16_t bx, uint16_t cylinder, uint16_t head, uint16_t sector, uint16_t drive);

extern void install_isr08(void);
extern void install_isr80(void);

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

void k_timer_callback(void) {
	bios_puts(".");
}

void k_syscall_callback(registers_t *regs) {
	bios_puts("K");
}

void kernel_main(void) {
	bios_puts("\r\n");
	bios_puts("\r\nMatrix16 Kernel loaded!\r\n");
	bios_puts("\r\n");

	install_isr08();
	install_isr80();

	for(;;) asm("int $0x80");
}
