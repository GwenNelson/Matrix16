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

static void lba2chs(uint16_t lba, uint16_t *cylinder, uint16_t *head, uint16_t *sector) {
	// this is a dumb quick hack, but it works
	*cylinder = lba / 18;
	*head     = (lba % 18) / 9;
	*sector   = (lba % 9) + 1;
}

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
}

void k_syscall_callback(registers_t *regs) {
}

uint16_t fs_read_rootfs_block(uint16_t lba, uint16_t es, uint16_t bx) {
	uint16_t c;
	uint16_t h;
	uint16_t s;
	lba2chs(lba,&c,&h,&s);
	return bios_read_sector(es,bx,c,h,s,1);
}

void fs_check_super(void) {
	bios_puts("Checking rootfs in B:...\r\n");
	if(fs_read_rootfs_block(0,0x1000,(uint16_t)&buf) != 0) {
		bios_puts("Failed to read superblock!\r\n");
	}

	if( (buf[0] != 'M') || (buf[1] != 'A') || (buf[2] != 'T') || (buf[3] != '1') || (buf[4] != '6') ) {
		bios_puts("Bad signature on superblock!\r\n");
	}

	bios_puts("Found a valid Matrix16 filesystem in B:\r\n");
}


void kernel_main(void) {
	bios_puts("\r\n");
	bios_puts("\r\nMatrix16 Kernel loaded!\r\n");
	bios_puts("\r\n");

	install_isr08();
	install_isr80();

	fs_check_super();

	for(;;);
}
