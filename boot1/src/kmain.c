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

typedef enum task_state_t {
	LOADED  = 0,
	RUNNING = 1,
	FROZEN  = 2,
} task_state_t;

struct task_context_t {
    uint16_t ax, bx, cx, dx;
    uint16_t si, di, bp;
    uint16_t sp, ip, flags;
} task_context_t;

typedef struct task_t {
	task_context_t ctx;
	task_state_t state;
} task_t;

typedef struct console_t {
	uint8_t  bios_page_no;
	uint16_t vram_phys;
} console_t;

extern uint16_t bios_read_sector(uint16_t es, uint16_t bx, uint16_t cylinder, uint16_t head, uint16_t sector, uint16_t drive);

static uint8_t cur_page_no = 0;

static void bios_putchar(char c, uint8_t page_no) {
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

static void bios_swap_page(uint8_t new_page) {
	uint16_t ax = 0x0500 | new_page;
	__asm__ volatile (
		"int $0x10"
		: "+a" (ax)
		:
		: "cc"
	);
}

static void bios_puts(char* s) {
	while(*s) {
		switch(*s) {
			case '\t':
				for(int i=0; i<8; i++) bios_putchar(' ',cur_page_no);
			break;
			case '\n':
				bios_putchar('\r',cur_page_no);
				bios_putchar('\n',cur_page_no);
			break;
			default:
				bios_putchar(*s, cur_page_no);
			break;
		}

		s++;
	}
}



void kpanic(char* msg) {
	bios_puts("Kernal panic!\n");
	bios_puts("\tREASON: "); bios_puts(msg); bios_puts("\n");
	bios_puts("\n\n");
	bios_puts("IT IS IMPOSSIBLE TO CONTINUE, SYSTEM HALTING\n");
	for(;;) asm("cli; hlt");
}

static void lba2chs(uint16_t lba, uint16_t *cylinder, uint16_t *head, uint16_t *sector) {
	// this is a dumb quick hack, but it works
	*cylinder = lba / 18;
	*head     = (lba % 18) / 9;
	*sector   = (lba % 9) + 1;
}

extern void install_isr08(void);
extern void install_isr80(void);

char buf[512];


static inline void __far *
mk_farptr(uint16_t segment, uint16_t offset)
{
    union {
        struct {
            uint16_t offset;
            uint16_t segment;
        } parts;
        void __far *ptr;
    } u;

    u.parts.offset = offset;
    u.parts.segment = segment;
    return u.ptr;
}
void k_timer_callback(void) {
}



#define SYS_WRITE 1

void k_syscall_sys_write(registers_t *regs) {
	if(regs->bx != 1) {
		kpanic("Attempted SYS_WRITE to unknown handle!"); // Probably want a generic error handler for user programs instead of always kpanic
	}
	char __far *buf = mk_farptr(regs->ds,regs->cx);
	for(int i=0; i < regs->dx; i++) {
		bios_putchar(buf[i],cur_page_no); // TODO - should check what page no the task is assigned
	}
}

void k_syscall_callback(registers_t *regs) {
	switch(regs->ax) {
		case SYS_WRITE:
			k_syscall_sys_write(regs);
		break;
		default:
			kpanic("Unknown syscall");
		break;
	}
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
		bios_putchar(digits[--count],cur_page_no);
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
			bios_putchar(digits[digit],cur_page_no);
			started = 1;
		}
		if(shift == 0)
			break;
		shift -= 4;
	}
}

static rootfs_dir_t* dir_buf;

static rootfs_dir_t known_files[32];

bool fs_mount_rootdir(uint16_t start, uint16_t sec_count) {
	if(fs_read_rootfs_block(start,0x1000,(uint16_t)&buf) != 0) {
		bios_puts("Failed to read rootdir!\n");
		return false;
	}
	dir_buf = (rootfs_dir_t*)buf;
	
	int i=0;
	for(i=0; i<16; i++) { /* for now we only bother with the first sector */
		if(dir_buf[i].name[0] != '\0') {
			bios_puts("\t Found file: ");
			bios_puts(dir_buf[i].name);  // I know this is shitty code, i should use a memcpy or something, stfu
			bios_puts("\n");
			known_files[i].start_lba = dir_buf[i].start_lba;
			known_files[i].end_lba   = dir_buf[i].end_lba;
			known_files[i].size      = dir_buf[i].size;
			for(int j = 0; j < sizeof(known_files[i].name); j++)
				known_files[i].name[j] = dir_buf[i].name[j];
		}
	}
	return true;
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

static int strcmp(const char *a, const char *b) {
	while(*a != '\0' && *a == *b) {
		a++;
		b++;
	}
	return (uint8_t)*a - (uint8_t)*b;
}

static int strncmp(const char *a, const char *b, uint16_t n) {
	while(n != 0) {
		uint8_t ca = (uint8_t)*a++;
		uint8_t cb = (uint8_t)*b++;

		if(ca != cb)
			return ca - cb;
		if(ca == '\0')
			return 0;
		n--;
	}
	return 0;
}

static char *strchr(const char *s, int c) {
	uint8_t target = (uint8_t)c;

	for(;;) {
		if((uint8_t)*s == target)
			return (char*)s;
		if(*s == '\0')
			return 0;
		s++;
	}
}

static char *strcpy(char *dest, const char *src) {
	char *result = dest;

	while((*dest++ = *src++) != '\0')
		;
	return result;
}

bool fs_locate_file(char* name, uint16_t es, char* buf, uint16_t *len) {
	if(name == 0 || len == 0)
		return false;

	for(uint16_t i = 0; i < 16; i++) {
		if(known_files[i].name[0] == '\0')
			continue;

		if(strcmp(name, known_files[i].name) == 0) {
			uint16_t dest = (uint16_t)(uintptr_t)buf;

			for(uint16_t lba = known_files[i].start_lba;
			    lba < known_files[i].end_lba; lba++) {
				if(fs_read_rootfs_block(lba, es, dest) != 0)
					return false;
				dest += 512;
			}

			*len = known_files[i].size;
			return true;
		}
	}

	return false;
}

char system_cfg_buf[1024]; // yes, this will crash horribly if SYSTEM.CFG is too big, i know
uint16_t system_cfg_len;

char system_cfg_shell[26];
uint16_t system_cfg_consoles = 4;

void load_system_cfg(void) {
	if(!fs_locate_file("SYSTEM.CFG",0x1000,(char*)system_cfg_buf,&system_cfg_len)) {
		bios_puts("Could not open SYSTEM.CFG, using defaults....\n");
		strcpy(system_cfg_shell, "SHELL.PRG");
		return;
	}

	system_cfg_buf[system_cfg_len] = '\0';
	char *line = system_cfg_buf;
	while(*line != '\0') {
		char *next = strchr(line, '\n');
		if(next != 0) {
			*next = '\0';
			next++;
		} else {
			next = strchr(line, '\0');
		}

		char *cr = strchr(line, '\r');
		if(cr != 0)
			*cr = '\0';

		if(strncmp(line, "SHELL=", 6) == 0) {
			strcpy(system_cfg_shell, line + 6);
		} else if(strncmp(line, "CONSOLES=", 9) == 0) {
			char *value = line + 9;
			char *digit = value;
			uint16_t consoles = 0;
			uint8_t valid = 1;

			while(*digit != '\0') {
				if(*digit < '0' || *digit > '9') {
					valid = 0;
					break;
				}
				consoles = consoles * 10 + (*digit - '0');
				digit++;
			}
			if(valid && digit != value)
				system_cfg_consoles = consoles;
		}

		line = next;
	}

	if(system_cfg_consoles > 4)
		kpanic("maximum of 4 consoles allowed!");
}



// the shell is configured to run at 2000:0000 - that is, the very first task after the kernel itself
void load_default_shell() {
	uint16_t shell_len;
	if(!fs_locate_file(system_cfg_shell,0x2000,(char*)0x0000, &shell_len)) {
		kpanic("MISSING SHELL!");
	}
	// if we get here, yay! it should be possible to just JMP to it
	__asm__ volatile (
	    "ljmp $0x2000, $0x0000"
	);	
}

void kernel_main(void) {
	bios_puts("Matrix16 Kernel loaded!\n\n");

	install_isr08();
	install_isr80();

	bool got_rootfs = false;

	if(fs_check_super()) {
		bios_puts("Attempting to mount rootfs...\n");
		got_rootfs = fs_mount_rootdir(rootfs_dir_start,rootfs_dir_sectors);
	}

	if(got_rootfs) {
		load_system_cfg();
	}

	bios_puts("Starting configured shell B:");
	bios_puts(system_cfg_shell);
	bios_puts("...\n");

	load_default_shell();

	for(;;);
}
