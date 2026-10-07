#include <stdint.h>
#include <stdbool.h>

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

typedef struct rootfs_dir_t {
	uint16_t start_lba;
	uint16_t end_lba;
	uint16_t size;
	char name[26];
} __attribute__((packed)) rootfs_dir_t;

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

static void bios_put_decimal(uint16_t n) {
	char digits[5];
	uint8_t count = 0;

	do {
		digits[count++] = '0' + (n % 10);
		n /= 10;
	} while(n != 0);

	while(count != 0) {
		bios_putchar(digits[--count]);
	}
}

static void bios_put_hex(uint16_t n) {
	static const char digits[] = "0123456789ABCDEF";
	uint8_t shift = 12;
	uint8_t started = 0;

	bios_puts("0x");
	while(shift != 0 || !started) {
		uint8_t digit = (n >> shift) & 0x0f;

		if(digit != 0 || started || shift == 0) {
			bios_putchar(digits[digit]);
			started = 1;
		}
		if(shift == 0)
			break;
		shift -= 4;
	}
}


bool fs_mount_rootdir(uint16_t start, uint16_t sec_count) {
	if(fs_read_rootfs_block(start,0x1000,(uint16_t)&buf) != 0) {
		bios_puts("Failed to read rootdir!\n");
		return false;
	}
	rootfs_dir_t* dir_buf = (rootfs_dir_t*)buf;
	int i=0;
	for(i=0; i<16; i++) {
		if(dir_buf[i].name[0] != '\0') {
			bios_puts("\t Found file: ");
			bios_puts(dir_buf[i].name);  // I know this is shitty code, i should use a memcpy or something, stfu
			bios_puts("\n");
		}
	}

}

static uint16_t rootfs_dir_start;
static uint16_t rootfs_dir_sectors;

bool fs_check_super(void) {
	bios_puts("Checking rootfs in B:...\n");
	if(fs_read_rootfs_block(0,0x1000,(uint16_t)&buf) != 0) {
		bios_puts("Failed to read superblock!\n");
		return false;
	}

	if( (buf[0] != 'M') || (buf[1] != 'A') || (buf[2] != 'T') || (buf[3] != '1') || (buf[4] != '6') ) {
		bios_puts("Bad signature on superblock!\n");
		return false;
	}

	if( (buf[5] != '\0') || (buf[6] != '\0') || (buf[7] != '\0') ) {
		bios_puts("Bad padding bytes on superblock!\n");
		return false;
	}

	uint16_t* dir_start   = (uint16_t*)&(buf[8]);
	uint16_t* dir_sectors = (uint16_t*)&(buf[10]);

	bios_puts("Found a valid Matrix16 filesystem superblock in B:\n");

	bios_puts("\tdir_start: ");
	bios_put_decimal(*dir_start);
	bios_puts(", dir_sectors: ");
	bios_put_decimal(*dir_sectors);
	bios_puts("\n");

	rootfs_dir_start   = *dir_start;
	rootfs_dir_sectors = *dir_sectors;
	return true;
}

void kernel_main(void) {
	bios_puts("Matrix16 Kernel loaded!\n\n");

	install_isr08();
	install_isr80();

	if(fs_check_super()) {
		bios_puts("Attempting to mount rootfs...\n");
		(void)fs_mount_rootdir(rootfs_dir_start,rootfs_dir_sectors);
	}

	for(;;);
}
