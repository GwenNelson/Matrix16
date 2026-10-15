#include "matfs16.h"
#include "utils.h"
#include "biosdisk.h"
#include "kmemmap.h"
#include "kthread.h"
#include "string.h"

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

static volatile uint8_t bios_lock;

static uint8_t blockchain_cache[2][MATFS16_BLOCKCHAIN_BLOCK_COUNT][MATFS16_BLOCK_SIZE];
static uint8_t blockchain_cache_valid[2];

static uint8_t matfs16_thread_scratch[KTHREAD_MAX_ID + 1][MATFS16_BLOCK_SIZE]
	__attribute__((aligned(MATFS16_BLOCK_SIZE))) SEG_SYS_BSS;
static uint8_t matfs16_bootstrap_scratch[MATFS16_BLOCK_SIZE]
	__attribute__((aligned(MATFS16_BLOCK_SIZE))) SEG_SYS_BSS;

typedef struct matfs16_write_cursor_t {
	uint16_t first_lba;
	uint16_t block_index;
	uint16_t current_lba;
	uint8_t drive_no;
	bool valid;
} matfs16_write_cursor_t;

typedef struct matfs16_directory_cursor_t {
	uint16_t root_lba;
	uint16_t root_blocks;
	uint16_t entry_count;
	uint16_t current_lba;
	uint16_t previous_lba;
	uint16_t block_index;
} matfs16_directory_cursor_t;

/* Sequential block writes (such as task serialization) keep their chain
 * position per thread, so each next block needs only one chain lookup. */
static matfs16_write_cursor_t write_cursors[KTHREAD_MAX_ID + 1];

static uint8_t __far *get_scratch_buf(void);
static uint16_t scratch_offset(void);
static matfs16_status_t load_scratch_block(uint8_t drive_no, uint16_t lba);
static matfs16_status_t store_scratch_block(uint8_t drive_no, uint16_t lba);
static uint16_t directory_checksum(const uint8_t __far *bytes);
static uint16_t blockchain_checksum(const uint8_t __far *bytes);
static matfs16_status_t directory_cursor_start(uint8_t drive_no,
	                                            matfs16_directory_cursor_t* cursor,
	                                            matfs_directory_block_t __far **directory);
static matfs16_status_t directory_cursor_next(uint8_t drive_no,
	                                           matfs16_directory_cursor_t* cursor,
	                                           matfs_directory_block_t __far **directory);

static uint16_t read_u16(const uint8_t __far *bytes) {
	return (uint16_t)bytes[0] | ((uint16_t)bytes[1] << 8);
}

static bool filename_matches_far(const char __far *stored, const char *wanted) {
	while(*stored && *wanted) {
		if(*stored++ != *wanted++) return false;
	}
	return *stored == *wanted;
}

static void copy_filename_from_far(char *destination, const char __far *source) {
	for(uint8_t i = 0; i < MATFS16_FILENAME_MAXLEN; i++) {
		destination[i] = source[i];
		if(source[i] == '\0') return;
	}
	destination[MATFS16_FILENAME_MAXLEN - 1] = '\0';
}

static matfs_directory_entry_t __far *kernel_entry_far(const matfs_directory_entry_t *entry) {
	return (matfs_directory_entry_t __far *)mk_farptr(SEG_KERN,
	                                               (uint16_t)entry);
}

static uint8_t __far *cached_blockchain_block(uint8_t drive_no, uint16_t block) {
	return (uint8_t __far *)mk_farptr(SEG_KERN,
	                                (uint16_t)blockchain_cache[drive_no][block]);
}

static bool verify_super(matfs_superblock_t __far *super) {
	uint16_t task_first[4];
	uint16_t i;
	uint16_t checksum;

	if((super->magic[0] != 'M') || (super->magic[1] != 'A') || (super->magic[2] != 'T') || (super->magic[3] != '1') || (super->magic[4] != '6')) return false;

	if(super->media_type     != 0) return false;
	if(super->block_size     != MATFS16_BLOCK_SIZE) return false;
	if(super->block_count    != MATFS16_BLOCK_COUNT) return false;
	if(super->blockchain_len != MATFS16_BLOCKCHAIN_BLOCK_COUNT) return false;

	/* 0xFFFF with a zero count means no kernel has been installed yet.
	 * Otherwise, a zero count marks a fragmented kernel. */
	if(super->kernel_first_block == MATFS16_KERNEL_NOT_INSTALLED) {
		if(super->kernel_block_count != 0) return false;
	} else {
		if(super->kernel_first_block < MATFS16_DATA_START_LBA || super->kernel_first_block >= MATFS16_BLOCK_COUNT) return false;
		if(super->kernel_block_count > 128) return false;
		if(super->kernel_block_count != 0 && super->kernel_block_count > MATFS16_BLOCK_COUNT - super->kernel_first_block) return false;
	}

	/* A zero root pointer means that the disk has no usable filesystem. */
	if(super->root_dir_start == 0) {
		if(super->root_dir_len != 0) return false;
	} else {
		if(super->root_dir_start < MATFS16_DATA_START_LBA || super->root_dir_start >= MATFS16_BLOCK_COUNT) return false;
		if(super->root_dir_len == 0 || super->root_dir_len > MATFS16_BLOCK_COUNT - super->root_dir_start) return false;
	}

	/* Backup pointers are optional absolute LBAs. */
	if(super->backup_super != 0 && super->backup_super >= MATFS16_BLOCK_COUNT) return false;
	if(super->blockchain_backup != 0 && (super->blockchain_backup >= MATFS16_BLOCK_COUNT || super->blockchain_backup > MATFS16_BLOCK_COUNT - MATFS16_BLOCKCHAIN_BLOCK_COUNT)) return false;
	if(super->backup_rootdir != 0 && (super->backup_rootdir < 13 || super->backup_rootdir >= MATFS16_DATA_START_LBA)) return false;

	/* Each populated task pointer names its first data block. */
	task_first[0] = super->task0;
	task_first[1] = super->task1;
	task_first[2] = super->task2;
	task_first[3] = super->task3;
	for(i = 0; i < 4; i++) {
		if(task_first[i] != 0xFFFF && (task_first[i] < MATFS16_DATA_START_LBA || task_first[i] >= MATFS16_BLOCK_COUNT)) return false;
	}

	/* Superblock padding must be zero. */
	for(i = 0; i < sizeof(super->padding); i++) {
		if(super->padding[i] != 0) return false;
	}

	/* The checksum is the 16-bit sum from media_type through task3. */
	checksum = super->media_type + super->block_size + super->block_count +
	           super->blockchain_len + super->kernel_first_block +
	           super->kernel_block_count + super->root_dir_start +
	           super->root_dir_len + super->backup_super +
	           super->blockchain_backup + super->backup_rootdir +
	           super->task0 + super->task1 + super->task2 + super->task3;
	if(checksum != super->checksum) return false;

	return true;
}

static bool verify_directory_block(matfs_directory_block_t __far *directory) {
	const uint8_t __far *bytes = (const uint8_t __far *)directory;
	uint16_t i;

	if(directory->magic[0] != 'D' || directory->magic[1] != 'I' || directory->magic[2] != 'R' || directory->magic[3] != '\0') return false;

	if(directory_checksum(bytes) != directory->checksum) return false;

	for(i = 0; i < sizeof(directory->padding); i++) {
		if(directory->padding[i] != 0) return false;
	}

	return true;
}

static bool verify_rootdir(matfs_directory_block_t __far *rootdir) {
	if(!verify_directory_block(rootdir)) return false;

	/* The first root block points back to itself. */
	if(rootdir->parent_start < MATFS16_DATA_START_LBA || rootdir->parent_start >= MATFS16_BLOCK_COUNT) return false;
	if(rootdir->prev_block != rootdir->parent_start) return false;

	if(rootdir->parent_len == 0 || rootdir->parent_len > MATFS16_DATA_BLOCK_COUNT) return false;
	if(rootdir->entry_count > rootdir->parent_len * MATFS16_DIRECTORY_ENTRIES_PER_BLOCK) return false;

	if(rootdir->parent_len == 1) {
		if(rootdir->next_block != MATFS16_CHAIN_END) return false;
	} else {
		if(rootdir->next_block < MATFS16_DATA_START_LBA || rootdir->next_block >= MATFS16_BLOCK_COUNT) return false;
	}

	return true;
}

static bool verify_file_entry(matfs_directory_entry_t __far *entry) {
	uint16_t name_length;

	for(name_length = 0; name_length < MATFS16_FILENAME_MAXLEN &&
	    entry->name[name_length] != '\0'; name_length++);
	if(name_length == 0)
		return entry->first_block == 0 && entry->block_count == 0 &&
		       entry->logical_size == 0;
	if(name_length == MATFS16_FILENAME_MAXLEN) return false;

	/* Empty files have no allocated block. */
	if(entry->block_count == 0) return entry->first_block == 0 && entry->logical_size == 0;

	if(entry->first_block < MATFS16_DATA_START_LBA || entry->first_block >= MATFS16_BLOCK_COUNT) return false;
	if(entry->block_count > MATFS16_DATA_BLOCK_COUNT) return false;

	/* Keep the capacity calculation within 16 bits. */
	if(entry->logical_size != MATFS16_FILE_SIZE_UNKNOWN && entry->block_count < 128 && entry->logical_size > entry->block_count * MATFS16_BLOCK_SIZE) return false;

	return true;
}

static void copy_directory_entry(matfs_directory_entry_t __far *destination,
	                            const matfs_directory_entry_t __far *source) {
	uint16_t i;

	destination->first_block = source->first_block;
	destination->block_count = source->block_count;
	destination->logical_size = source->logical_size;
	for(i = 0; i < MATFS16_FILENAME_MAXLEN; i++) destination->name[i] = source->name[i];
}

static bool verify_blockchain_block(const uint8_t __far *blockchain) {
	return blockchain_checksum(blockchain) == read_u16(blockchain);
}

static uint16_t directory_checksum(const uint8_t __far *bytes) {
	uint16_t checksum = 0;
	uint16_t offset;

	for(offset = 0; offset < MATFS16_BLOCK_SIZE; offset += 2) {
		if(offset != 14) checksum += read_u16(bytes + offset);
	}

	return checksum;
}

static uint16_t blockchain_checksum(const uint8_t __far *bytes) {
	uint16_t checksum = 0;
	uint16_t offset;

	for(offset = 2; offset < MATFS16_BLOCK_SIZE; offset += 2) checksum += read_u16(bytes + offset);
	return checksum;
}

static uint16_t blockchain_entry_offset(uint16_t relative_block) {
	return 2 + (relative_block % MATFS16_BLOCKCHAIN_ENTRIES_PER_BLOCK) * 2;
}

static matfs16_status_t set_blockchain_entry(uint8_t drive_no, uint16_t relative_block, uint16_t value) {
	uint8_t __far *scratch_buf = get_scratch_buf();
	uint16_t blockchain_lba;
	matfs16_status_t status;

	if(relative_block >= MATFS16_DATA_BLOCK_COUNT) return MATFS16_ERR_INVALID_ARGUMENT;
	blockchain_lba = MATFS16_BLOCKCHAIN_LBA + relative_block / MATFS16_BLOCKCHAIN_ENTRIES_PER_BLOCK;
	status = load_scratch_block(drive_no, blockchain_lba);
	if(status != MATFS16_OK) return status;
	if(!verify_blockchain_block(scratch_buf)) return MATFS16_ERR_BAD_BLOCKCHAIN;

		*(uint16_t __far *)(scratch_buf + blockchain_entry_offset(relative_block)) = value;
		*(uint16_t __far *)scratch_buf = blockchain_checksum(scratch_buf);
	return store_scratch_block(drive_no, blockchain_lba);
}

static matfs16_status_t allocate_data_block(uint8_t drive_no, uint16_t* lba) {
	uint8_t __far *scratch_buf = get_scratch_buf();
	uint16_t sector_index;
	uint16_t relative_block;
	uint16_t offset;
	matfs16_status_t status;

	if(lba == NULL) return MATFS16_ERR_INVALID_ARGUMENT;

	for(sector_index = 0; sector_index < MATFS16_BLOCKCHAIN_BLOCK_COUNT; sector_index++) {
		status = load_scratch_block(drive_no, MATFS16_BLOCKCHAIN_LBA + sector_index);
		if(status != MATFS16_OK) return status;
		if(!verify_blockchain_block(scratch_buf)) return MATFS16_ERR_BAD_BLOCKCHAIN;

		for(offset = 2; offset < MATFS16_BLOCK_SIZE; offset += 2) {
			relative_block = sector_index * MATFS16_BLOCKCHAIN_ENTRIES_PER_BLOCK + (offset - 2) / 2;
			if(relative_block >= MATFS16_DATA_BLOCK_COUNT) break;
			if(read_u16(scratch_buf + offset) != MATFS16_CHAIN_FREE) continue;

			*(uint16_t __far *)(scratch_buf + offset) = MATFS16_CHAIN_END;
			*(uint16_t __far *)scratch_buf = blockchain_checksum(scratch_buf);
			status = store_scratch_block(drive_no, MATFS16_BLOCKCHAIN_LBA + sector_index);
			if(status != MATFS16_OK) return status;

			for(offset = 0; offset < MATFS16_BLOCK_SIZE; offset++) scratch_buf[offset] = 0;
			*lba = MATFS16_DATA_START_LBA + relative_block;
			return store_scratch_block(drive_no, *lba);
		}
	}

	return MATFS16_ERR_NO_SPACE;
}

static matfs16_status_t find_directory_entry(uint8_t drive_no, const char* filename,
	                                            uint16_t* entry_lba, uint8_t* entry_slot,
	                                            uint16_t* entry_index,
	                                            matfs_directory_entry_t* entry,
	                                            bool scan_all_blocks) {
	matfs16_directory_cursor_t cursor;
	matfs_directory_block_t __far *directory;
	matfs16_status_t status;
	uint16_t first_entry;
	uint16_t entries_in_block;
	uint16_t i;

	status = directory_cursor_start(drive_no, &cursor, &directory);
	if(status != MATFS16_OK) return status;

	for(;;) {
		first_entry = cursor.block_index * MATFS16_DIRECTORY_ENTRIES_PER_BLOCK;
		entries_in_block = MATFS16_DIRECTORY_ENTRIES_PER_BLOCK;

		for(i = 0; i < entries_in_block; i++) {
			if(!verify_file_entry(&directory->entries[i])) return MATFS16_ERR_BAD_DIRECTORY;
			if(directory->entries[i].name[0] == '\0') continue;
			if(filename_matches_far(directory->entries[i].name, filename)) {
				if(entry_lba != NULL) *entry_lba = cursor.current_lba;
				if(entry_slot != NULL) *entry_slot = (uint8_t)i;
				if(entry_index != NULL) *entry_index = first_entry + i;
				if(entry != NULL) copy_directory_entry(kernel_entry_far(entry),
				                                      &directory->entries[i]);
				return MATFS16_OK;
			}
		}

		if(cursor.block_index + 1 == cursor.root_blocks) {
			if(scan_all_blocks) {
				uint16_t hinted_next_lba = directory->next_block;
				uint16_t next_lba;
				status = matfs16_next_block(drive_no, cursor.current_lba, &next_lba);
				if(status != MATFS16_END_OF_FILE) {
					if(status == MATFS16_OK) return MATFS16_ERR_BAD_BLOCKCHAIN;
					return status;
				}
				if(hinted_next_lba != MATFS16_CHAIN_END) return MATFS16_ERR_BAD_DIRECTORY;
			}
			break;
		}

		status = directory_cursor_next(drive_no, &cursor, &directory);
		if(status == MATFS16_END_OF_DIRECTORY) break;
		if(status != MATFS16_OK) return status;
	}

	return MATFS16_ERR_NOT_FOUND;
}

static matfs16_status_t update_file_entry(uint8_t drive_no, const char* filename,
	                                        uint16_t first_block, uint16_t block_count,
	                                        uint16_t logical_size) {
	uint8_t __far *scratch_buf = get_scratch_buf();
	uint16_t directory_lba;
	uint8_t entry_slot;
	matfs_directory_block_t __far * directory;
	matfs16_status_t status;

	status = find_directory_entry(drive_no, filename, &directory_lba, &entry_slot, NULL, NULL, false);
	if(status != MATFS16_OK) return status;
	status = load_scratch_block(drive_no, directory_lba);
	if(status != MATFS16_OK) return status;
	directory = (matfs_directory_block_t __far *)scratch_buf;
	directory->entries[entry_slot].first_block = first_block;
	directory->entries[entry_slot].block_count = block_count;
	directory->entries[entry_slot].logical_size = logical_size;
	directory->checksum = directory_checksum(scratch_buf);
	return store_scratch_block(drive_no, directory_lba);
}

static matfs16_status_t set_directory_entry_count(uint8_t drive_no, uint16_t root_lba,
	                                               uint16_t root_blocks, uint16_t count) {
	uint8_t __far *scratch_buf = get_scratch_buf();
	uint16_t current_lba = root_lba;
	uint16_t block_index;
	uint16_t next_lba;
	matfs16_status_t status;
	matfs_directory_block_t __far * directory;

	for(block_index = 0; block_index < root_blocks; block_index++) {
		status = load_scratch_block(drive_no, current_lba);
		if(status != MATFS16_OK) return status;
		directory = (matfs_directory_block_t __far *)scratch_buf;
		if(!verify_directory_block(directory)) return MATFS16_ERR_BAD_DIRECTORY;
		directory->entry_count = count;
		directory->checksum = directory_checksum(scratch_buf);
		status = store_scratch_block(drive_no, current_lba);
		if(status != MATFS16_OK) return status;

		if(block_index + 1 < root_blocks) {
			status = matfs16_next_block(drive_no, current_lba, &next_lba);
			if(status != MATFS16_OK) return MATFS16_ERR_BAD_BLOCKCHAIN;
			current_lba = next_lba;
		}
	}

	return MATFS16_OK;
}

static matfs16_status_t get_blockchain_entry(uint8_t drive_no, uint16_t relative_block,
	                                           uint16_t* value) {
	uint8_t __far *scratch_buf = get_scratch_buf();
		uint8_t __far *blockchain;
	uint16_t blockchain_block;
	matfs16_status_t status;

	if(drive_no > 1 || relative_block >= MATFS16_DATA_BLOCK_COUNT || value == NULL) return MATFS16_ERR_INVALID_ARGUMENT;
	blockchain_block = relative_block / MATFS16_BLOCKCHAIN_ENTRIES_PER_BLOCK;
	if(blockchain_cache_valid[drive_no]) {
		blockchain = cached_blockchain_block(drive_no, blockchain_block);
	} else {
		status = load_scratch_block(drive_no, MATFS16_BLOCKCHAIN_LBA + blockchain_block);
		if(status != MATFS16_OK) return status;
		if(!verify_blockchain_block(scratch_buf)) return MATFS16_ERR_BAD_BLOCKCHAIN;
		blockchain = scratch_buf;
	}

	*value = read_u16(blockchain + blockchain_entry_offset(relative_block));
	return MATFS16_OK;
}

static matfs16_status_t verify_file_chain(uint8_t drive_no,
	                                      const matfs_directory_entry_t* entry) {
	uint16_t current_lba = entry->first_block;
	uint16_t relative_block;
	uint16_t value;
	uint16_t i;
	matfs16_status_t status;

	if(entry->block_count == 0) return MATFS16_OK;
	for(i = 0; i < entry->block_count; i++) {
		if(current_lba < MATFS16_DATA_START_LBA || current_lba >= MATFS16_BLOCK_COUNT) return MATFS16_ERR_BAD_BLOCKCHAIN;
		relative_block = current_lba - MATFS16_DATA_START_LBA;
		status = get_blockchain_entry(drive_no, relative_block, &value);
		if(status != MATFS16_OK) return status;
		if(i + 1 == entry->block_count) {
			if(value != MATFS16_CHAIN_END) return MATFS16_ERR_BAD_BLOCKCHAIN;
		} else {
			if(value >= MATFS16_DATA_BLOCK_COUNT) return MATFS16_ERR_BAD_BLOCKCHAIN;
			current_lba = MATFS16_DATA_START_LBA + value;
		}
	}

	return MATFS16_OK;
}

static void acquire_bios_lock(void) {
	uint8_t previous;

	do {
		previous = 1;
		__asm__ volatile (
			"xchgb %0, %1"
			: "+q" (previous), "+m" (bios_lock)
			:
			: "memory"
		);

		if(previous != 0) kthread_yield();
	} while(previous != 0);
}

static void release_bios_lock(void) {
	__asm__ volatile (
		"movb $0, %0"
		: "=m" (bios_lock)
		:
		: "memory"
	);
}

static uint16_t scratch_offset(void) {
	kthread_id_t thread = kthread_cur_thread();

	/* Use the bootstrap buffer before the scheduler has selected a thread. */
	if(thread == KTHREAD_INVALID_ID || thread > KTHREAD_MAX_ID)
		return (uint16_t)&matfs16_bootstrap_scratch;

	return (uint16_t)matfs16_thread_scratch +
	       (uint16_t)thread * MATFS16_BLOCK_SIZE;
}

static uint8_t __far *get_scratch_buf(void) {
	return (uint8_t __far *)mk_farptr(SEG_SYS, scratch_offset());
}

static matfs16_status_t load_scratch_block(uint8_t drive_no, uint16_t lba) {
	return matfs16_load_block(drive_no, lba, SEG_SYS, scratch_offset());
}

static matfs16_status_t store_scratch_block(uint8_t drive_no, uint16_t lba) {
	return matfs16_store_block(drive_no, lba, SEG_SYS, scratch_offset());
}

/* Read the superblock and return the primary root directory location. */
static matfs16_status_t read_root_info(uint8_t drive_no, uint16_t* root_lba,
	                                   uint16_t* root_blocks) {
	uint8_t __far *scratch_buf = get_scratch_buf();
	matfs_superblock_t __far * super;
	matfs16_status_t status;

	status = load_scratch_block(drive_no, MATFS16_SUPERBLOCK_LBA);
	if(status != MATFS16_OK) return status;

	super = (matfs_superblock_t __far *)scratch_buf;
	if(!verify_super(super)) return MATFS16_ERR_BAD_SUPERBLOCK;
	if(super->root_dir_start == 0) return MATFS16_ERR_NOT_FOUND;

	*root_lba = super->root_dir_start;
	*root_blocks = super->root_dir_len;
	return MATFS16_OK;
}

/* Load and validate one root-directory block using the shared scratch buffer. */
static matfs16_status_t load_directory_block(uint8_t drive_no,
	                                          matfs16_directory_cursor_t* cursor,
	                                          matfs_directory_block_t __far ** directory_out) {
	uint8_t __far *scratch_buf = get_scratch_buf();
	matfs_directory_block_t __far * directory;
	matfs16_status_t status;

	status = load_scratch_block(drive_no, cursor->current_lba);
	if(status != MATFS16_OK) return status;

	directory = (matfs_directory_block_t __far *)scratch_buf;
	if(cursor->block_index == 0) {
		if(!verify_rootdir(directory)) return MATFS16_ERR_BAD_DIRECTORY;
		cursor->entry_count = directory->entry_count;
	} else if(!verify_directory_block(directory)) {
		return MATFS16_ERR_BAD_DIRECTORY;
	}

	if(directory->parent_start != cursor->root_lba ||
	   directory->parent_len != cursor->root_blocks ||
	   directory->entry_count != cursor->entry_count ||
	   directory->prev_block != cursor->previous_lba) {
		return MATFS16_ERR_BAD_DIRECTORY;
	}

	*directory_out = directory;
	return MATFS16_OK;
}

/* Start at the first directory block, checking the superblock and root. */
static matfs16_status_t directory_cursor_start(uint8_t drive_no,
	                                            matfs16_directory_cursor_t* cursor,
	                                            matfs_directory_block_t __far ** directory) {
	matfs16_status_t status;

	status = read_root_info(drive_no, &cursor->root_lba, &cursor->root_blocks);
	if(status != MATFS16_OK) return status;

	cursor->entry_count = 0;
	cursor->current_lba = cursor->root_lba;
	cursor->previous_lba = cursor->root_lba;
	cursor->block_index = 0;
	return load_directory_block(drive_no, cursor, directory);
}

/* Advance one block, checking that the directory hint agrees with the chain. */
static matfs16_status_t directory_cursor_next(uint8_t drive_no,
	                                           matfs16_directory_cursor_t* cursor,
	                                           matfs_directory_block_t __far ** directory) {
	matfs_directory_block_t __far * current =
		(matfs_directory_block_t __far *)get_scratch_buf();
	uint16_t hinted_next_lba = current->next_block;
	uint16_t next_lba;
	matfs16_status_t status;

	if(cursor->block_index + 1 >= cursor->root_blocks) return MATFS16_END_OF_DIRECTORY;
	status = matfs16_next_block(drive_no, cursor->current_lba, &next_lba);
	if(status == MATFS16_END_OF_FILE) return MATFS16_ERR_BAD_BLOCKCHAIN;
	if(status != MATFS16_OK) return status;
	if(hinted_next_lba != next_lba) return MATFS16_ERR_BAD_DIRECTORY;

	cursor->previous_lba = cursor->current_lba;
	cursor->current_lba = next_lba;
	cursor->block_index++;
	return load_directory_block(drive_no, cursor, directory);
}

matfs16_status_t matfs16_locate_root(uint8_t drive_no, uint16_t* lba, uint16_t* block_count) {
	matfs16_directory_cursor_t cursor;
	matfs_directory_block_t __far * root;
	matfs16_status_t status;

	if(lba == NULL || block_count == NULL) return MATFS16_ERR_INVALID_ARGUMENT;
	status = directory_cursor_start(drive_no, &cursor, &root);
	if(status != MATFS16_OK) return status;
	*lba = cursor.root_lba;
	*block_count = cursor.root_blocks;
	return MATFS16_OK;
}

matfs16_status_t matfs16_read_direntry(uint8_t drive_no, uint16_t index, matfs_directory_entry_t* entry) {
	matfs16_directory_cursor_t cursor;
	matfs_directory_block_t __far * directory;
	matfs_directory_entry_t __far *selected_entry;
	matfs16_status_t status;
	uint16_t entry_index;
	uint16_t live_index = 0;

	if(entry == NULL) return MATFS16_ERR_INVALID_ARGUMENT;
	status = directory_cursor_start(drive_no, &cursor, &directory);
	if(status != MATFS16_OK) return status;
	if(index >= cursor.entry_count) return MATFS16_END_OF_DIRECTORY;

	for(;;) {
		for(entry_index = 0; entry_index < MATFS16_DIRECTORY_ENTRIES_PER_BLOCK; entry_index++) {
			selected_entry = (matfs_directory_entry_t __far *)&directory->entries[entry_index];
			if(!verify_file_entry(selected_entry)) return MATFS16_ERR_BAD_DIRECTORY;
			if(selected_entry->name[0] == '\0') continue;
			if(live_index++ == index) {
				copy_directory_entry(kernel_entry_far(entry), selected_entry);
				return MATFS16_OK;
			}
		}

		if(cursor.block_index + 1 >= cursor.root_blocks) break;
		status = directory_cursor_next(drive_no, &cursor, &directory);
		if(status != MATFS16_OK) return status;
	}

	return MATFS16_ERR_BAD_DIRECTORY;
}

matfs16_status_t matfs16_read_task_slot(uint8_t drive_no, uint8_t slot, char* filename, char** result) {
	uint8_t __far *scratch_buf;
	matfs_superblock_t __far * super;
	matfs_directory_entry_t entry;
	matfs16_status_t status;
	uint16_t first_block;
	uint16_t index = 0;

	if(result == NULL || filename == NULL || slot >= MATFS16_TASK_SLOT_COUNT) return MATFS16_ERR_INVALID_ARGUMENT;
	*result = NULL;

	scratch_buf = get_scratch_buf();
	status = load_scratch_block(drive_no, MATFS16_SUPERBLOCK_LBA);
	if(status != MATFS16_OK) return status;

	super = (matfs_superblock_t __far *)scratch_buf;
	if(!verify_super(super)) return MATFS16_ERR_BAD_SUPERBLOCK;

	if(slot == 0) first_block = super->task0;
	else if(slot == 1) first_block = super->task1;
	else if(slot == 2) first_block = super->task2;
	else first_block = super->task3;

	if(first_block == MATFS16_TASK_SLOT_EMPTY) return MATFS16_OK;

	for(;;) {
		status = matfs16_read_direntry(drive_no, index, &entry);
		if(status == MATFS16_END_OF_DIRECTORY) return MATFS16_ERR_NOT_FOUND;
		if(status != MATFS16_OK) return status;

		if(entry.first_block == first_block) {
			copy_filename_from_far(filename, entry.name);
			*result = filename;
			return MATFS16_OK;
		}

		index++;
	}
}

matfs16_status_t matfs16_locate_file(const char* filename, uint8_t drive_no, uint16_t* lba, uint16_t* length) {
	matfs_directory_entry_t entry;
	matfs16_status_t status;

	if(filename == NULL || lba == NULL || length == NULL) return MATFS16_ERR_INVALID_ARGUMENT;
	if(filename[0] == '\0') return MATFS16_ERR_INVALID_ARGUMENT;
	if(strnlen(filename, MATFS16_FILENAME_MAXLEN) == MATFS16_FILENAME_MAXLEN) return MATFS16_ERR_INVALID_ARGUMENT;

	status = find_directory_entry(drive_no, filename, NULL, NULL, NULL, &entry, true);
	if(status != MATFS16_OK) return status;
	*lba = entry.first_block;
	*length = entry.logical_size;
	return MATFS16_OK;
}

matfs16_status_t matfs16_load_block(uint8_t drive_no, uint16_t lba, uint16_t seg, uint16_t buf) {
	return matfs16_load_contiguous_blocks(drive_no, lba, 1, seg, buf);
}

matfs16_status_t matfs16_load_contiguous_blocks(uint8_t drive_no, uint16_t first_lba,
	                                              uint16_t block_count, uint16_t seg,
	                                              uint16_t buf) {
	uint16_t c;
	uint16_t h;
	uint16_t s;
	uint16_t bios_error;
	uint16_t bytes_available;

	if(drive_no > 1 || block_count == 0 || first_lba >= MATFS16_BLOCK_COUNT ||
	   block_count > MATFS16_BLOCK_COUNT - first_lba) return MATFS16_ERR_INVALID_ARGUMENT;
	if(block_count > 9 - (first_lba % 9)) return MATFS16_ERR_INVALID_ARGUMENT;

	/* Offset zero can address the whole segment; nonzero offsets have less room. */
	if(buf != 0) {
		bytes_available = (uint16_t)(0 - buf);
		if(block_count > bytes_available / MATFS16_BLOCK_SIZE) return MATFS16_ERR_INVALID_ARGUMENT;
	}

	lba2chs(first_lba, &c, &h, &s);
	acquire_bios_lock();
	if(block_count == 1) {
		bios_error = bios_read_sector(seg, buf, c, h, s, drive_no);
	} else {
		bios_error = bios_read_sectors(seg, buf, c, h, s, drive_no, block_count);
	}
	release_bios_lock();

	if(bios_error != 0) return MATFS16_ERR_DISK;
	return MATFS16_OK;
}

matfs16_status_t matfs16_cache_blockchain(uint8_t drive_no) {
	uint16_t block;
	matfs16_status_t status;

	if(drive_no > 1) return MATFS16_ERR_INVALID_ARGUMENT;
	if(blockchain_cache_valid[drive_no]) return MATFS16_OK;

	for(block = 0; block < MATFS16_BLOCKCHAIN_BLOCK_COUNT; block++) {
		uint8_t* destination = blockchain_cache[drive_no][block];
		status = matfs16_load_block(drive_no, MATFS16_BLOCKCHAIN_LBA + block,
		                            SEG_KERN, (uint16_t)destination);
		if(status != MATFS16_OK) return status;
		if(!verify_blockchain_block((const uint8_t __far *)
		                            mk_farptr(SEG_KERN, (uint16_t)destination)))
			return MATFS16_ERR_BAD_BLOCKCHAIN;
	}

	blockchain_cache_valid[drive_no] = 1;
	return MATFS16_OK;
}

void matfs16_invalidate_blockchain_cache(uint8_t drive_no) {
	if(drive_no <= 1) blockchain_cache_valid[drive_no] = 0;
}

matfs16_status_t matfs16_count_free_blocks(uint8_t drive_no, uint16_t* count) {
	matfs16_status_t status;
	uint16_t relative_block;
	uint16_t value;
	uint16_t free_blocks = 0;

	if(drive_no > 1 || count == NULL) return MATFS16_ERR_INVALID_ARGUMENT;
	status = matfs16_cache_blockchain(drive_no);
	if(status != MATFS16_OK) return status;

	for(relative_block = 0; relative_block < MATFS16_DATA_BLOCK_COUNT; relative_block++) {
		value = read_u16(cached_blockchain_block(drive_no,
		                  relative_block / MATFS16_BLOCKCHAIN_ENTRIES_PER_BLOCK) +
		                  blockchain_entry_offset(relative_block));
		if(value == MATFS16_CHAIN_FREE) free_blocks++;
	}

	*count = free_blocks;
	return MATFS16_OK;
}

matfs16_status_t matfs16_store_block(uint8_t drive_no, uint16_t lba, uint16_t seg, uint16_t buf) {
	uint16_t c;
	uint16_t h;
	uint16_t s;
	uint16_t bios_error;

	if(drive_no > 1 || lba >= MATFS16_BLOCK_COUNT || buf > 0xFE00) return MATFS16_ERR_INVALID_ARGUMENT;
	if(lba >= MATFS16_BLOCKCHAIN_LBA &&
	   lba < MATFS16_BLOCKCHAIN_LBA + MATFS16_BLOCKCHAIN_BLOCK_COUNT) {
		matfs16_invalidate_blockchain_cache(drive_no);
		for(uint16_t i = 0; i <= KTHREAD_MAX_ID; i++) write_cursors[i].valid = false;
	}

	lba2chs(lba, &c, &h, &s);
	acquire_bios_lock();
	bios_error = bios_write_sector(seg, buf, c, h, s, drive_no);
	release_bios_lock();

	if(bios_error != 0) return MATFS16_ERR_DISK;

	return MATFS16_OK;
}

matfs16_status_t matfs16_create_file(uint8_t drive_no, const char* filename) {
	matfs16_directory_cursor_t cursor;
	matfs_directory_block_t __far * directory;
	matfs_directory_entry_t __far *entry;
	matfs16_status_t status;
	uint16_t name_length;
	uint16_t entry_index;
	uint16_t i;

	if(drive_no > 1 || filename == NULL || filename[0] == '\0' ||
	   strnlen(filename, MATFS16_FILENAME_MAXLEN) == MATFS16_FILENAME_MAXLEN) {
		return MATFS16_ERR_INVALID_ARGUMENT;
	}

	status = find_directory_entry(drive_no, filename, NULL, NULL, NULL, NULL, false);
	if(status == MATFS16_OK) return MATFS16_ERR_FILE_EXISTS;
	if(status != MATFS16_ERR_NOT_FOUND) return status;

	status = directory_cursor_start(drive_no, &cursor, &directory);
	if(status != MATFS16_OK) return status;
	if(cursor.entry_count >= cursor.root_blocks * MATFS16_DIRECTORY_ENTRIES_PER_BLOCK)
		return MATFS16_ERR_DIRECTORY_FULL;

	name_length = strnlen(filename, MATFS16_FILENAME_MAXLEN);
	for(;;) {
		for(entry_index = 0; entry_index < MATFS16_DIRECTORY_ENTRIES_PER_BLOCK; entry_index++) {
			entry = &directory->entries[entry_index];
			if(!verify_file_entry(entry)) return MATFS16_ERR_BAD_DIRECTORY;
			if(entry->name[0] != '\0') continue;

			entry->first_block = 0;
			entry->block_count = 0;
			entry->logical_size = 0;
			for(i = 0; i < MATFS16_FILENAME_MAXLEN; i++)
				entry->name[i] = i < name_length ? filename[i] : '\0';

			directory->checksum = directory_checksum((const uint8_t __far *)directory);
			status = store_scratch_block(drive_no, cursor.current_lba);
			if(status != MATFS16_OK) return status;
			return set_directory_entry_count(drive_no, cursor.root_lba,
			                                 cursor.root_blocks, cursor.entry_count + 1);
		}

		if(cursor.block_index + 1 >= cursor.root_blocks) break;
		status = directory_cursor_next(drive_no, &cursor, &directory);
		if(status != MATFS16_OK) return status;
	}

	return MATFS16_ERR_DIRECTORY_FULL;
}

static void release_allocated_chain(uint8_t drive_no, uint16_t first_lba,
	                                uint16_t block_count) {
	uint16_t current_lba = first_lba;

	for(uint16_t i = 0; i < block_count; i++) {
		uint16_t value;
		if(get_blockchain_entry(drive_no, current_lba - MATFS16_DATA_START_LBA,
		                        &value) != MATFS16_OK) return;
		if(set_blockchain_entry(drive_no, current_lba - MATFS16_DATA_START_LBA,
		                        MATFS16_CHAIN_FREE) != MATFS16_OK) return;
		if(i + 1 < block_count) {
			if(value >= MATFS16_DATA_BLOCK_COUNT) return;
			current_lba = MATFS16_DATA_START_LBA + value;
		}
	}
}

matfs16_status_t matfs16_replace_file_blocks(uint8_t drive_no,
	                                         const char* filename,
	                                         uint16_t block_count,
	                                         uint16_t logical_size,
	                                         uint16_t* first_lba) {
	matfs16_status_t status;
	uint16_t allocated = 0;
	uint16_t first = 0;
	uint16_t previous = 0;

	if(drive_no > 1 || filename == NULL || first_lba == NULL ||
	   block_count == 0 || block_count > MATFS16_DATA_BLOCK_COUNT ||
	   (logical_size != MATFS16_FILE_SIZE_UNKNOWN && logical_size == 0)) {
		return MATFS16_ERR_INVALID_ARGUMENT;
	}

	status = matfs16_delete_file(drive_no, filename);
	if(status != MATFS16_OK && status != MATFS16_ERR_NOT_FOUND) return status;
	status = matfs16_create_file(drive_no, filename);
	if(status != MATFS16_OK) return status;

	while(allocated < block_count) {
		uint16_t current;
		status = allocate_data_block(drive_no, &current);
		if(status != MATFS16_OK) {
			if(allocated != 0) release_allocated_chain(drive_no, first, allocated);
			return status;
		}
		if(allocated == 0) first = current;
		else {
			status = set_blockchain_entry(drive_no,
				previous - MATFS16_DATA_START_LBA,
				current - MATFS16_DATA_START_LBA);
			if(status != MATFS16_OK) {
				(void)set_blockchain_entry(drive_no,
					current - MATFS16_DATA_START_LBA, MATFS16_CHAIN_FREE);
				release_allocated_chain(drive_no, first, allocated);
				return status;
			}
		}
		previous = current;
		allocated++;
	}

	status = update_file_entry(drive_no, filename, first, block_count, logical_size);
	if(status != MATFS16_OK) {
		release_allocated_chain(drive_no, first, block_count);
		return status;
	}
	*first_lba = first;
	return MATFS16_OK;
}

matfs16_status_t matfs16_write_file_block(uint8_t drive_no, uint16_t first_lba,
	                                      uint16_t block_index, uint16_t seg,
	                                      uint16_t buf) {
	uint16_t current_lba = first_lba;
	kthread_id_t thread = kthread_cur_thread();
	matfs16_write_cursor_t* cursor = NULL;
	matfs16_status_t status;

	if(drive_no > 1 || first_lba < MATFS16_DATA_START_LBA ||
	   first_lba >= MATFS16_BLOCK_COUNT || block_index >= MATFS16_DATA_BLOCK_COUNT ||
	   buf > 0xFE00) return MATFS16_ERR_INVALID_ARGUMENT;

	if(thread <= KTHREAD_MAX_ID) cursor = &write_cursors[thread];
	if(block_index != 0 && cursor != NULL && cursor->valid &&
	   cursor->drive_no == drive_no && cursor->first_lba == first_lba &&
	   cursor->block_index + 1 == block_index) {
		current_lba = cursor->current_lba;
		status = matfs16_next_block(drive_no, current_lba, &current_lba);
		if(status != MATFS16_OK) {
			cursor->valid = false;
			return MATFS16_ERR_BAD_BLOCKCHAIN;
		}
	} else {
		for(uint16_t i = 0; i < block_index; i++) {
			status = matfs16_next_block(drive_no, current_lba, &current_lba);
			if(status != MATFS16_OK) {
				if(cursor != NULL) cursor->valid = false;
				return MATFS16_ERR_BAD_BLOCKCHAIN;
			}
		}
	}

	status = matfs16_store_block(drive_no, current_lba, seg, buf);
	if(cursor != NULL) {
		if(status == MATFS16_OK) {
			cursor->drive_no = drive_no;
			cursor->first_lba = first_lba;
			cursor->block_index = block_index;
			cursor->current_lba = current_lba;
			cursor->valid = true;
		} else {
			cursor->valid = false;
		}
	}
	return status;
}

matfs16_status_t matfs16_delete_file(uint8_t drive_no, const char* filename) {
	matfs_directory_entry_t target;
	uint8_t changed_blockchain[MATFS16_BLOCKCHAIN_BLOCK_COUNT];
	uint8_t __far *scratch_buf;
	matfs_directory_block_t __far *directory;
	matfs16_status_t status;
	uint16_t directory_lba;
	uint16_t root_lba;
	uint16_t root_blocks;
	uint16_t entry_count;
	uint8_t entry_slot;
	uint16_t current_lba;
	uint16_t relative_block;
	uint16_t value;
	uint16_t blockchain_block;
	uint16_t i;

	if(drive_no > 1 || filename == NULL || filename[0] == '\0' ||
	   strnlen(filename, MATFS16_FILENAME_MAXLEN) == MATFS16_FILENAME_MAXLEN) {
		return MATFS16_ERR_INVALID_ARGUMENT;
	}

	/* Reload the chain table before using it as the in-memory update buffer. */
	matfs16_invalidate_blockchain_cache(drive_no);
	status = matfs16_cache_blockchain(drive_no);
	if(status != MATFS16_OK) return status;

	status = matfs16_locate_root(drive_no, &root_lba, &root_blocks);
	if(status != MATFS16_OK) return status;
	status = find_directory_entry(drive_no, filename, &directory_lba, &entry_slot,
	                              NULL, &target, false);
	if(status != MATFS16_OK) return status;
	status = verify_file_chain(drive_no, &target);
	if(status != MATFS16_OK) return status;

	scratch_buf = get_scratch_buf();
	status = load_scratch_block(drive_no, directory_lba);
	if(status != MATFS16_OK) return status;
	directory = (matfs_directory_block_t __far *)scratch_buf;
	if(!verify_directory_block(directory)) return MATFS16_ERR_BAD_DIRECTORY;
	entry_count = directory->entry_count;
	if(entry_count == 0) return MATFS16_ERR_BAD_DIRECTORY;
	if(!verify_file_entry(&directory->entries[entry_slot]) ||
	   !filename_matches_far(directory->entries[entry_slot].name, filename))
		return MATFS16_ERR_NOT_FOUND;

	/* A NUL first character marks this directory slot as deleted. */
	directory->entries[entry_slot].name[0] = '\0';
	directory->entries[entry_slot].first_block = 0;
	directory->entries[entry_slot].block_count = 0;
	directory->entries[entry_slot].logical_size = 0;
	directory->checksum = directory_checksum(scratch_buf);
	status = store_scratch_block(drive_no, directory_lba);
	if(status != MATFS16_OK) {
		matfs16_invalidate_blockchain_cache(drive_no);
		return status;
	}
	status = set_directory_entry_count(drive_no, root_lba, root_blocks,
	                                   entry_count - 1);
	if(status != MATFS16_OK) {
		matfs16_invalidate_blockchain_cache(drive_no);
		return status;
	}

	for(i = 0; i < MATFS16_BLOCKCHAIN_BLOCK_COUNT; i++) changed_blockchain[i] = false;
	current_lba = target.first_block;
	for(i = 0; i < target.block_count; i++) {
		relative_block = current_lba - MATFS16_DATA_START_LBA;
		blockchain_block = relative_block / MATFS16_BLOCKCHAIN_ENTRIES_PER_BLOCK;
		value = read_u16(cached_blockchain_block(drive_no,
		                  blockchain_block) +
		                  blockchain_entry_offset(relative_block));
		changed_blockchain[blockchain_block] = true;
		*(uint16_t __far *)(cached_blockchain_block(drive_no,
		  blockchain_block) +
		  blockchain_entry_offset(relative_block)) = MATFS16_CHAIN_FREE;
		if(i + 1 < target.block_count) current_lba = MATFS16_DATA_START_LBA + value;
	}

	for(i = 0; i < MATFS16_BLOCKCHAIN_BLOCK_COUNT; i++) {
		if(!changed_blockchain[i]) continue;
		*(uint16_t __far *)cached_blockchain_block(drive_no, i) =
			blockchain_checksum(cached_blockchain_block(drive_no, i));
		status = matfs16_store_block(drive_no, MATFS16_BLOCKCHAIN_LBA + i,
		                             SEG_KERN,
		                             (uint16_t)blockchain_cache[drive_no][i]);
		if(status != MATFS16_OK) return status;
	}

	return MATFS16_OK;
}

matfs16_status_t matfs16_rename_file(uint8_t drive_no, const char* old_name, const char* new_name) {
	uint8_t __far *scratch_buf = get_scratch_buf();
	matfs_directory_block_t __far * directory;
	matfs16_status_t status;
	uint16_t directory_lba;
	uint8_t entry_slot;
	uint16_t i;
	uint16_t new_length;

	if(drive_no > 1 || old_name == NULL || new_name == NULL ||
	   old_name[0] == '\0' || new_name[0] == '\0') return MATFS16_ERR_INVALID_ARGUMENT;
	if(strnlen(old_name, MATFS16_FILENAME_MAXLEN) == MATFS16_FILENAME_MAXLEN ||
	   (new_length = strnlen(new_name, MATFS16_FILENAME_MAXLEN)) == MATFS16_FILENAME_MAXLEN)
		return MATFS16_ERR_INVALID_ARGUMENT;
	status = find_directory_entry(drive_no, old_name, &directory_lba, &entry_slot, NULL, NULL, false);
	if(status != MATFS16_OK) return status;
	if(strcmp(old_name, new_name) == 0) return MATFS16_OK;
	status = find_directory_entry(drive_no, new_name, NULL, NULL, NULL, NULL, false);
	if(status == MATFS16_OK) return MATFS16_ERR_FILE_EXISTS;
	if(status != MATFS16_ERR_NOT_FOUND) return status;

	status = load_scratch_block(drive_no, directory_lba);
	if(status != MATFS16_OK) return status;
	directory = (matfs_directory_block_t __far *)scratch_buf;
	if(!verify_directory_block(directory)) return MATFS16_ERR_BAD_DIRECTORY;

	for(i = 0; i < MATFS16_FILENAME_MAXLEN; i++) directory->entries[entry_slot].name[i] = '\0';
	for(i = 0; i < new_length; i++) directory->entries[entry_slot].name[i] = new_name[i];
	directory->checksum = directory_checksum(scratch_buf);
	return store_scratch_block(drive_no, directory_lba);
}

matfs16_status_t matfs16_write_file(uint8_t drive_no, const char* filename, uint16_t position, uint16_t* last_lba, uint16_t seg, uint16_t buf, uint16_t count, uint16_t* bytes_written) {
	uint8_t __far *scratch_buf = get_scratch_buf();
	uint8_t __far* source = (uint8_t __far*)mk_farptr(seg, buf);
	matfs_directory_entry_t entry;
	matfs16_status_t status;
	uint16_t current_position = position;
	uint16_t remaining = count;
	uint16_t written = 0;
	uint16_t block_index;
	uint16_t block_offset;
	uint16_t current_lba;
	uint16_t previous_lba;
	uint16_t next_lba;
	uint16_t chunk;
	uint16_t i;
	bool new_block;

	if(bytes_written == NULL || last_lba == NULL) return MATFS16_ERR_INVALID_ARGUMENT;
	*bytes_written = 0;
	if(drive_no > 1 || filename == NULL || buf > 0xFE00 ||
	   (count != 0 && count - 1 > 0xFFFF - buf)) return MATFS16_ERR_INVALID_ARGUMENT;
	if(count == 0) return MATFS16_OK;
	if(position == MATFS16_FILE_SIZE_UNKNOWN || count > 0xFFFE - position) return MATFS16_ERR_NO_SPACE;

	status = find_directory_entry(drive_no, filename, NULL, NULL, NULL, &entry, false);
	if(status != MATFS16_OK) return status;
	if(entry.logical_size == MATFS16_FILE_SIZE_UNKNOWN) return MATFS16_ERR_INVALID_ARGUMENT;
	if(position != entry.logical_size) return MATFS16_ERR_INVALID_ARGUMENT;

	while(remaining != 0) {
		block_index = current_position / MATFS16_BLOCK_SIZE;
		block_offset = current_position % MATFS16_BLOCK_SIZE;
		new_block = block_index == entry.block_count;
		if(block_index > entry.block_count) return MATFS16_ERR_BAD_BLOCKCHAIN;

		if(new_block) {
			previous_lba = entry.block_count == 0 ? 0 : *last_lba;
			if(entry.block_count != 0 && previous_lba == 0) {
				previous_lba = entry.first_block;
				for(i = 1; i < entry.block_count; i++) {
					status = matfs16_next_block(drive_no, previous_lba, &next_lba);
					if(status != MATFS16_OK) return MATFS16_ERR_BAD_BLOCKCHAIN;
					previous_lba = next_lba;
				}
				*last_lba = previous_lba;
			}
			if(entry.block_count != 0 &&
			   (previous_lba < MATFS16_DATA_START_LBA || previous_lba >= MATFS16_BLOCK_COUNT))
				return MATFS16_ERR_BAD_BLOCKCHAIN;
			status = allocate_data_block(drive_no, &current_lba);
			if(status != MATFS16_OK) return status;
		} else {
			current_lba = entry.first_block;
			for(i = 0; i < block_index; i++) {
				status = matfs16_next_block(drive_no, current_lba, &next_lba);
				if(status != MATFS16_OK) return MATFS16_ERR_BAD_BLOCKCHAIN;
				current_lba = next_lba;
			}
			status = load_scratch_block(drive_no, current_lba);
			if(status != MATFS16_OK) return status;
		}

		chunk = MATFS16_BLOCK_SIZE - block_offset;
		if(chunk > remaining) chunk = remaining;
		for(i = 0; i < chunk; i++) scratch_buf[block_offset + i] = source[written + i];
		status = store_scratch_block(drive_no, current_lba);
		if(status != MATFS16_OK) return status;

		if(new_block) {
			if(previous_lba != 0) {
				status = set_blockchain_entry(drive_no,
					previous_lba - MATFS16_DATA_START_LBA,
					current_lba - MATFS16_DATA_START_LBA);
				if(status != MATFS16_OK) return status;
			} else {
				entry.first_block = current_lba;
			}
			entry.block_count++;
		}

		current_position += chunk;
		entry.logical_size = current_position;
		status = update_file_entry(drive_no, filename, entry.first_block,
		                           entry.block_count, entry.logical_size);
		if(status != MATFS16_OK) return status;
		if(new_block) *last_lba = current_lba;

		written += chunk;
		remaining -= chunk;
		*bytes_written = written;
	}

	return MATFS16_OK;
}

matfs16_status_t matfs16_next_block(uint8_t drive_no, uint16_t lba, uint16_t* next) {
	matfs16_status_t status;
	uint16_t relative_block;
	uint16_t next_relative;

	if(next == NULL) return MATFS16_ERR_INVALID_ARGUMENT;

	if(lba < MATFS16_DATA_START_LBA || lba >= MATFS16_BLOCK_COUNT) return MATFS16_ERR_INVALID_ARGUMENT;

	/* Convert the absolute LBA to its relative entry and read it from cache. */
	relative_block = lba - MATFS16_DATA_START_LBA;
	status = get_blockchain_entry(drive_no, relative_block, &next_relative);
	if(status != MATFS16_OK) return status;

	if(next_relative == MATFS16_CHAIN_END) return MATFS16_END_OF_FILE;

	if(next_relative == MATFS16_CHAIN_FREE)       return MATFS16_ERR_BAD_BLOCKCHAIN;
	if(next_relative == MATFS16_CHAIN_RESERVED)   return MATFS16_ERR_BAD_BLOCKCHAIN;
	if(next_relative >= MATFS16_DATA_BLOCK_COUNT) return MATFS16_ERR_BAD_BLOCKCHAIN;	

	*next = MATFS16_DATA_START_LBA + next_relative;
	return MATFS16_OK;
}
