#include <stdbool.h>
#include <stdint.h>

#define SUPERBLOCK_LOC		9
#define SUPERBLOCK_BACKUP_LOC 	14
#define SEG_KERN		0x1000
#define SEG_AUX			0x7000

#define MAT16FS_MEDIA_TYPE_360KB_FLOPPY  0
#define MAT16FS_360KB_FLOPPY_BLOCK_COUNT 720
#define MAT16FS_BLOCKSIZE		 512
#define MAT16FS_KERNEL_NOT_INSTALLED 0xFFFF


extern uint8_t boot_drive_no;
extern uint16_t bios_read_sector(uint16_t es, uint16_t bx, uint16_t cylinder, uint16_t head, uint16_t sector, uint16_t drive);
extern uint16_t bios_read_sectors(uint16_t es, uint16_t bx, uint16_t cylinder,
                                  uint16_t head, uint16_t sector,
                                  uint16_t drive, uint16_t count);

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

static bool read_lba_sector(uint16_t segment, uint16_t offset,
                            uint16_t drive, uint16_t lba) {
	uint16_t cylinder;
	uint16_t head;
	uint16_t sector;

	if(lba >= MAT16FS_360KB_FLOPPY_BLOCK_COUNT) return false;

	lba2chs(lba, &cylinder, &head, &sector);
	return bios_read_sector(segment, offset, cylinder, head, sector, drive) == 0;
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
	uint16_t task_first[4];
	uint16_t i;
	uint32_t checksum;

	if((super->magic[0] != 'M') || (super->magic[1] != 'A') || (super->magic[2] != 'T') || (super->magic[3] != '1') || (super->magic[4] != '6')) return false;

	if(super->media_type     != MAT16FS_MEDIA_TYPE_360KB_FLOPPY) return false;
	if(super->block_size     != MAT16FS_BLOCKSIZE) return false;
	if(super->block_count    != MAT16FS_360KB_FLOPPY_BLOCK_COUNT) return false;
	if(super->blockchain_len != 3) return false;

	/* 0xFFFF with a zero count means formatting finished before COPYSYS
	 * installed a kernel. Otherwise, zero count means a fragmented kernel. */
	if(super->kernel_first_block == MAT16FS_KERNEL_NOT_INSTALLED) {
		if(super->kernel_block_count != 0) return false;
	} else {
		if(super->kernel_first_block < 18 ||
		   super->kernel_first_block >= MAT16FS_360KB_FLOPPY_BLOCK_COUNT) return false;
		if(super->kernel_block_count > 128) return false;
		if(super->kernel_block_count != 0 &&
		   super->kernel_block_count > MAT16FS_360KB_FLOPPY_BLOCK_COUNT - super->kernel_first_block) return false;
	}

	/* A zero root pointer disables the filesystem; otherwise its directory
	 * chain must start in the data area and fit within the disk. */
	if(super->root_dir_start == 0) {
		if(super->root_dir_len != 0) return false;
	} else {
		if(super->root_dir_start < 18 ||
		   super->root_dir_start >= MAT16FS_360KB_FLOPPY_BLOCK_COUNT) return false;
		if(super->root_dir_len == 0 ||
		   super->root_dir_len > MAT16FS_360KB_FLOPPY_BLOCK_COUNT - super->root_dir_start) return false;
	}

	/* Backup metadata pointers are optional absolute LBAs and may be located
	 * anywhere on this disk. The blockchain backup occupies three sectors. */
	if(super->backup_super != 0 &&
	   super->backup_super >= MAT16FS_360KB_FLOPPY_BLOCK_COUNT) return false;
	if(super->blockchain_backup != 0 &&
	   (super->blockchain_backup >= MAT16FS_360KB_FLOPPY_BLOCK_COUNT ||
	    super->blockchain_backup > MAT16FS_360KB_FLOPPY_BLOCK_COUNT - 3)) return false;
	if(super->backup_rootdir != 0 &&
	   (super->backup_rootdir < 13 || super->backup_rootdir >= 18)) return false;

	/* Each populated task pointer names its first data block. */
	task_first[0] = super->task0;
	task_first[1] = super->task1;
	task_first[2] = super->task2;
	task_first[3] = super->task3;
	for(i = 0; i < 4; i++) {
		if(task_first[i] != 0xFFFF &&
		   (task_first[i] < 18 || task_first[i] >= MAT16FS_360KB_FLOPPY_BLOCK_COUNT)) return false;
	}

	/* The format requires all bytes following the checksum to be zero. */
	for(i = 0; i < sizeof(super->padding); i++) {
		if(super->padding[i] != 0) return false;
	}

	/* fsgen sums the 16-bit fields from media_type through task3, modulo
	 * 65536, and stores the result in checksum. */
	checksum = super->media_type + super->block_size + super->block_count +
	           super->blockchain_len + super->kernel_first_block +
	           super->kernel_block_count + super->root_dir_start +
	           super->root_dir_len + super->backup_super +
	           super->blockchain_backup + super->backup_rootdir +
	           super->task0 + super->task1 + super->task2 + super->task3;
	if((uint16_t)checksum != super->checksum) return false;

	kernel_first_block = super->kernel_first_block;
	contiguous_kernel = super->kernel_block_count != 0;

	return true;
}

static bool load_contiguous_kernel(void) {
	uint16_t block_count = super_buf.kernel_block_count;
	uint16_t loaded = 0;

	if(!contiguous_kernel) return false;
	while(loaded < block_count) {
		uint16_t lba = kernel_first_block + loaded;
		uint16_t offset = loaded * MAT16FS_BLOCKSIZE;
		uint16_t count = block_count - loaded;
		uint16_t track_remaining = 9 - (lba % 9);
		uint16_t segment_remaining;
		uint16_t cylinder;
		uint16_t head;
		uint16_t sector;

		/* Keep each BIOS request inside one track and the kernel segment. */
		if(count > track_remaining) count = track_remaining;
		if(offset != 0) {
			segment_remaining = (uint16_t)(0 - offset) / MAT16FS_BLOCKSIZE;
			if(count > segment_remaining) count = segment_remaining;
		}
		if(count == 0) return false;

		lba2chs(lba, &cylinder, &head, &sector);
		if(bios_read_sectors(SEG_KERN, offset, cylinder, head, sector,
		                     boot_drive_no, count) != 0) return false;

		/* Show one progress dot for each sector confirmed by the BIOS call. */
		for(uint16_t i = 0; i < count; i++) bios_putchar('.');
		loaded += count;
	}

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
	if(kernel_first_block == MAT16FS_KERNEL_NOT_INSTALLED) {
		bios_puts("\tNo kernel installed. Use COPYSYS before booting this disk.\n");
		for(;;);
	}
	if(contiguous_kernel) {
		bios_puts("\tLoad contiguous kernel: ");
		if(!load_contiguous_kernel()) {
			bios_puts("FAIL\n");
			for(;;);
		} else {
			bios_puts("OK\n");
		}
	} else {
		bios_puts("\n\nnon-contiguous kernel not yet supported!\n");
		for(;;);
	}

	bios_puts("\n\nSwitching to kernel...\n");

	__asm__ volatile ("ljmp $0x1000, $0x0000");

	for(;;);
}
