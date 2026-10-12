#include <stdbool.h>
#include <stdint.h>

#define SUPERBLOCK_LOC		9
#define SUPERBLOCK_BACKUP_LOC 	14
#define SEG_KERN		0x1000
#define SEG_AUX			0x7000

#define MAT16FS_MEDIA_TYPE_360KB_FLOPPY  0
#define MAT16FS_360KB_FLOPPY_BLOCK_COUNT 720
#define MAT16FS_BLOCKSIZE		 512


extern uint8_t boot_drive_no;
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

static void lba2chs(uint16_t lba, uint16_t *cylinder, uint16_t *head, uint16_t *sector) {
        // this is a dumb quick hack, but it works
        *cylinder = lba / 18;
        *head     = (lba % 18) / 9;
        *sector   = (lba % 9) + 1;
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

typedef struct __attribute__((packed)) matfs_superblock_t {
      char     magic[5];              // 0: "MAT16"
      uint16_t media_type;            // 5
      uint16_t block_size;            // 7
      uint16_t block_count;           // 9
      uint16_t blockchain_len;        // 11
      uint16_t kernel_first_block;    // 13: absolute LBA
      uint16_t kernel_block_count;    // 15
      uint16_t root_dir_start;        // 17: absolute LBA
      uint16_t root_dir_len;          // 19
      uint16_t backup_super;          // 21: absolute LBA, 0 if absent
      uint16_t blockchain_backup;     // 23: absolute LBA, 0 if absent
      uint16_t backup_rootdir;        // 25: absolute LBA, 0 if absent
      uint16_t task0;                 // 27: first LBA, 0xFFFF if absent
      uint16_t task1;                 // 29
      uint16_t task2;                 // 31
      uint16_t task3;                 // 33
      uint16_t checksum;              // 35
      uint8_t  padding[475];          // 37–511
} matfs_superblock_t;

static matfs_superblock_t super_buf;

static uint16_t kernel_first_block;
static bool     contiguous_kernel;

static bool verify_super(matfs_superblock_t* super) {
	if((super->magic[0] != 'M') || (super->magic[1] != 'A') || (super->magic[2] != 'T') || (super->magic[3] != '1') || (super->magic[4] != '6')) return false;

	if(super->media_type     != MAT16FS_MEDIA_TYPE_360KB_FLOPPY)   return false;
	if(super->block_size     != MAT16FS_BLOCKSIZE) return false;
	if(super->block_count    != MAT16FS_360KB_FLOPPY_BLOCK_COUNT) return false;
	if(super->blockchain_len != 3)   return false;

	if(super->kernel_first_block > super->block_count) return false;

	if(super->kernel_block_count == 0) {
		contiguous_kernel = true;
	}

	if(super->kernel_block_count > 128) return false;

	

	return true;
}

static bool try_read_super() {
	uint16_t cylinder;
	uint16_t head;
	uint16_t sector;
	lba2chs(SUPERBLOCK_LOC, &cylinder, &head, &sector);
	if(bios_read_sector(SEG_AUX,(uint16_t)&super_buf,cylinder,head,sector,boot_drive_no) != 0) return false;
	return true;
}

static bool try_read_super_backup() {
	uint16_t cylinder;
	uint16_t head;
	uint16_t sector;
	lba2chs(SUPERBLOCK_BACKUP_LOC, &cylinder, &head, &sector);
	if(bios_read_sector(SEG_AUX,(uint16_t)&super_buf,cylinder,head,sector,boot_drive_no) != 0) return false;
	return true;
}

void stage2_main(void) {
	bios_puts("\nMatrix16-Boot stage2\n\n");

	bios_puts("Superblock:\n");
	bios_puts("\tRead superblock:        ");
	if(!try_read_super()) {
		bios_puts("FAIL\n");
		bios_puts("\tRead backup superblock: ");
		if(!try_read_super_backup()) {
			bios_puts("FAIL\n");
			bios_puts("\nBad disk, can not boot\n");
			for(;;);
		} else {
			bios_puts("OK\n");
		}
	} else {
		bios_puts("OK\n");
	}

	bios_puts("\tValidate superblock:    ");
	if(!verify_super(&super_buf)) {
		bios_puts("FAIL\n");
		bios_puts("\nBad superblock, can not boot\n");
		for(;;);
	}
	bios_puts("OK\n");

	bios_puts("Kernel:\n");

	for(;;);
}
