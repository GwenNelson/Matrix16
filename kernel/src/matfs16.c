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

static uint8_t matfs16_thread_scratch[KTHREAD_MAX_ID + 1][MATFS16_BLOCK_SIZE]
	__attribute__((aligned(MATFS16_BLOCK_SIZE)));
static uint8_t matfs16_bootstrap_scratch[MATFS16_BLOCK_SIZE]
	__attribute__((aligned(MATFS16_BLOCK_SIZE)));

static uint16_t read_u16(const uint8_t* bytes) {
	return (uint16_t)bytes[0] | ((uint16_t)bytes[1] << 8);
}

static bool verify_super(matfs_superblock_t* super) {
	uint16_t task_first[4];
	uint16_t i;
	uint32_t checksum;

	if((super->magic[0] != 'M') || (super->magic[1] != 'A') || (super->magic[2] != 'T') || (super->magic[3] != '1') || (super->magic[4] != '6')) return false;

	if(super->media_type     != 0) return false;
	if(super->block_size     != MATFS16_BLOCK_SIZE) return false;
	if(super->block_count    != MATFS16_BLOCK_COUNT) return false;
	if(super->blockchain_len != MATFS16_BLOCKCHAIN_BLOCK_COUNT) return false;

	/* A zero kernel block count marks a fragmented kernel. */
	if(super->kernel_first_block < MATFS16_DATA_START_LBA || super->kernel_first_block >= MATFS16_BLOCK_COUNT) return false;
	if(super->kernel_block_count > 128) return false;
	if(super->kernel_block_count != 0 && super->kernel_block_count > MATFS16_BLOCK_COUNT - super->kernel_first_block) return false;

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
	if((uint16_t)checksum != super->checksum) return false;

	return true;
}

static bool verify_directory_block(matfs_directory_block_t* directory) {
	uint8_t* bytes = (uint8_t*)directory;
	uint16_t calculated_checksum = 0;
	uint16_t i;

	if(directory->magic[0] != 'D' || directory->magic[1] != 'I' || directory->magic[2] != 'R' || directory->magic[3] != '\0') return false;

	/* Sum every word except the checksum at byte offset 14. */
	for(i = 0; i < MATFS16_BLOCK_SIZE; i += 2) {
		if(i != 14) calculated_checksum += read_u16(bytes + i);
	}
	if(calculated_checksum != directory->checksum) return false;

	for(i = 0; i < sizeof(directory->padding); i++) {
		if(directory->padding[i] != 0) return false;
	}

	return true;
}

static bool verify_rootdir(matfs_directory_block_t* rootdir) {
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

static bool verify_file_entry(matfs_directory_entry_t* entry) {
	uint16_t name_length;

	name_length = strnlen(entry->name, MATFS16_FILENAME_MAXLEN);
	if(name_length == 0 || name_length == MATFS16_FILENAME_MAXLEN) return false;

	/* Empty files have no allocated block. */
	if(entry->block_count == 0) return entry->first_block == 0 && entry->logical_size == 0;

	if(entry->first_block < MATFS16_DATA_START_LBA || entry->first_block >= MATFS16_BLOCK_COUNT) return false;
	if(entry->block_count > MATFS16_DATA_BLOCK_COUNT) return false;

	/* Keep the capacity calculation within 16 bits. */
	if(entry->logical_size != MATFS16_FILE_SIZE_UNKNOWN && entry->block_count < 128 && entry->logical_size > entry->block_count * MATFS16_BLOCK_SIZE) return false;

	return true;
}

static void copy_directory_entry(matfs_directory_entry_t* destination, matfs_directory_entry_t* source) {
	uint16_t i;

	destination->first_block = source->first_block;
	destination->block_count = source->block_count;
	destination->logical_size = source->logical_size;
	for(i = 0; i < MATFS16_FILENAME_MAXLEN; i++) destination->name[i] = source->name[i];
}

static bool verify_blockchain_block(uint8_t* blockchain) {
	uint16_t calculated_checksum = 0;
	uint16_t offset;

	for(offset = 2; offset < MATFS16_BLOCK_SIZE; offset += 2) {
		calculated_checksum += read_u16(blockchain + offset);
	}

	return calculated_checksum == read_u16(blockchain);
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

static uint8_t* get_scratch_buf(void) {
	kthread_id_t thread = kthread_cur_thread();

	/* Use the bootstrap buffer before the scheduler has selected a thread. */
	if(thread == KTHREAD_INVALID_ID || thread > KTHREAD_MAX_ID) return matfs16_bootstrap_scratch;

	return matfs16_thread_scratch[thread];
}

matfs16_status_t matfs16_read_direntry(uint8_t drive_no, uint16_t index, matfs_directory_entry_t* entry) {
	uint8_t* scratch_buf;
	matfs_superblock_t* super;
	matfs_directory_block_t* directory;
	matfs_directory_entry_t* selected_entry;
	matfs16_status_t status;
	uint16_t root_start;
	uint16_t root_length;
	uint16_t total_entries = 0;
	uint16_t current_lba;
	uint16_t previous_lba;
	uint16_t next_lba;
	uint16_t hinted_next_lba;
	uint16_t target_block;
	uint16_t target_entry;
	uint16_t block_index;

	if(entry == NULL) return MATFS16_ERR_INVALID_ARGUMENT;

	/* Load the superblock and remember where the root directory begins. */
	scratch_buf = get_scratch_buf();
	status = matfs16_load_block(drive_no, MATFS16_SUPERBLOCK_LBA, SEG_KERN, (uint16_t)scratch_buf);
	if(status != MATFS16_OK) return status;

	super = (matfs_superblock_t*)scratch_buf;
	if(!verify_super(super)) return MATFS16_ERR_BAD_SUPERBLOCK;

	root_start = super->root_dir_start;
	root_length = super->root_dir_len;
	if(root_start == 0) return MATFS16_ERR_NOT_FOUND;

	target_block = index / MATFS16_DIRECTORY_ENTRIES_PER_BLOCK;
	target_entry = index % MATFS16_DIRECTORY_ENTRIES_PER_BLOCK;
	current_lba = root_start;
	previous_lba = root_start;

	/* Follow the root chain until the block containing index is reached. */
	for(block_index = 0; block_index < root_length; block_index++) {
		status = matfs16_load_block(drive_no, current_lba, SEG_KERN, (uint16_t)scratch_buf);
		if(status != MATFS16_OK) return status;

		directory = (matfs_directory_block_t*)scratch_buf;
		if(block_index == 0) {
			if(!verify_rootdir(directory)) return MATFS16_ERR_BAD_DIRECTORY;
			total_entries = directory->entry_count;
			if(index >= total_entries) return MATFS16_END_OF_DIRECTORY;
		} else {
			if(!verify_directory_block(directory)) return MATFS16_ERR_BAD_DIRECTORY;
		}

		if(directory->parent_start != root_start || directory->parent_len != root_length || directory->entry_count != total_entries || directory->prev_block != previous_lba) return MATFS16_ERR_BAD_DIRECTORY;

		if(block_index == target_block) {
			selected_entry = &directory->entries[target_entry];
			if(!verify_file_entry(selected_entry)) return MATFS16_ERR_BAD_DIRECTORY;
			copy_directory_entry(entry, selected_entry);
			return MATFS16_OK;
		}

		/* The requested entry is later, so this block must have a successor. */
		if(block_index + 1 == root_length) return MATFS16_ERR_BAD_DIRECTORY;

		hinted_next_lba = directory->next_block;
		status = matfs16_next_block(drive_no, current_lba, &next_lba);
		if(status == MATFS16_END_OF_FILE) return MATFS16_ERR_BAD_BLOCKCHAIN;
		if(status != MATFS16_OK) return status;
		if(hinted_next_lba != next_lba) return MATFS16_ERR_BAD_DIRECTORY;

		previous_lba = current_lba;
		current_lba = next_lba;
	}

	return MATFS16_ERR_BAD_DIRECTORY;
}

matfs16_status_t matfs16_read_task_slot(uint8_t drive_no, uint8_t slot, char* filename, char** result) {
	uint8_t* scratch_buf;
	matfs_superblock_t* super;
	matfs_directory_entry_t entry;
	matfs16_status_t status;
	uint16_t first_block;
	uint16_t index = 0;

	if(result == NULL || filename == NULL || slot >= MATFS16_TASK_SLOT_COUNT) return MATFS16_ERR_INVALID_ARGUMENT;
	*result = NULL;

	scratch_buf = get_scratch_buf();
	status = matfs16_load_block(drive_no, MATFS16_SUPERBLOCK_LBA,
	                            SEG_KERN, (uint16_t)scratch_buf);
	if(status != MATFS16_OK) return status;

	super = (matfs_superblock_t*)scratch_buf;
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
			strcpy(filename, entry.name);
			*result = filename;
			return MATFS16_OK;
		}

		index++;
	}
}

matfs16_status_t matfs16_locate_file(const char* filename, uint8_t drive_no, uint16_t* lba, uint16_t* length) {
	uint8_t* scratch_buf;
	matfs_superblock_t* super;
	matfs_directory_block_t* directory;
	matfs16_status_t status;
	uint16_t root_start;
	uint16_t root_length;
	uint16_t total_entries = 0;
	uint16_t current_lba;
	uint16_t previous_lba;
	uint16_t next_lba;
	uint16_t hinted_next_lba;
	uint16_t block_index;
	uint16_t first_entry;
	uint16_t entries_in_block;
	uint16_t entry_index;

	/* Validate the filename and output pointers. */

	if(filename == NULL || lba == NULL || length == NULL) return MATFS16_ERR_INVALID_ARGUMENT;
	if(filename[0] == '\0') return MATFS16_ERR_INVALID_ARGUMENT;
	if(strnlen(filename, MATFS16_FILENAME_MAXLEN) == MATFS16_FILENAME_MAXLEN) return MATFS16_ERR_INVALID_ARGUMENT;

	/* Load and validate the fixed superblock at LBA 9. */

	scratch_buf = get_scratch_buf();
	status = matfs16_load_block(drive_no, MATFS16_SUPERBLOCK_LBA,
	                            SEG_KERN, (uint16_t)scratch_buf);
	if(status != MATFS16_OK) return status;

	super = (matfs_superblock_t*)scratch_buf;
	if(!verify_super(super)) return MATFS16_ERR_BAD_SUPERBLOCK;

	/* Keep the root location before reusing the scratch buffer. */
	root_start = super->root_dir_start;
	root_length = super->root_dir_len;
	if(root_start == 0) return MATFS16_ERR_NOT_FOUND;

	/* Walk the root-directory chain for exactly root_length blocks. */
	current_lba = root_start;
	previous_lba = root_start;

	for(block_index = 0; block_index < root_length; block_index++) {
		status = matfs16_load_block(drive_no, current_lba, SEG_KERN,
		                            (uint16_t)scratch_buf);
		if(status != MATFS16_OK) return status;

		directory = (matfs_directory_block_t*)scratch_buf;
		if(block_index == 0) {
			if(!verify_rootdir(directory)) return MATFS16_ERR_BAD_DIRECTORY;
			total_entries = directory->entry_count;
		} else {
			if(!verify_directory_block(directory)) return MATFS16_ERR_BAD_DIRECTORY;
		}

		/* Check the redundant directory ownership and previous-block fields. */
		if(directory->parent_start != root_start || directory->parent_len != root_length || directory->entry_count != total_entries || directory->prev_block != previous_lba) return MATFS16_ERR_BAD_DIRECTORY;

		/* entry_count covers the whole directory, not just this sector. */
		first_entry = block_index * MATFS16_DIRECTORY_ENTRIES_PER_BLOCK;
		if(first_entry >= total_entries) {
			entries_in_block = 0;
		} else {
			entries_in_block = total_entries - first_entry;
			if(entries_in_block > MATFS16_DIRECTORY_ENTRIES_PER_BLOCK) entries_in_block = MATFS16_DIRECTORY_ENTRIES_PER_BLOCK;
		}

		for(entry_index = 0; entry_index < entries_in_block; entry_index++) {
			matfs_directory_entry_t* entry = &directory->entries[entry_index];

			if(!verify_file_entry(entry)) return MATFS16_ERR_BAD_DIRECTORY;

			if(strcmp(entry->name, filename) == 0) {
				*lba = entry->first_block;
				*length = entry->logical_size;
				return MATFS16_OK;
			}
		}

		/* Save this hint before the blockchain read reuses the buffer. */
		hinted_next_lba = directory->next_block;

		status = matfs16_next_block(drive_no, current_lba, &next_lba);

		if(block_index + 1 == root_length) {
			if(status != MATFS16_END_OF_FILE) {
				if(status == MATFS16_OK) return MATFS16_ERR_BAD_BLOCKCHAIN;
				return status;
			}
			if(hinted_next_lba != MATFS16_CHAIN_END) return MATFS16_ERR_BAD_DIRECTORY;
		} else {
			if(status == MATFS16_END_OF_FILE) return MATFS16_ERR_BAD_BLOCKCHAIN;
			if(status != MATFS16_OK) return status;
			if(hinted_next_lba != next_lba) return MATFS16_ERR_BAD_DIRECTORY;

			previous_lba = current_lba;
			current_lba = next_lba;
		}
	}

	return MATFS16_ERR_NOT_FOUND;
}

matfs16_status_t matfs16_load_block(uint8_t drive_no, uint16_t lba, uint16_t seg, uint16_t buf) {
	uint16_t c;
	uint16_t h;
	uint16_t s;
	lba2chs(lba,&c,&h,&s);
	acquire_bios_lock();
	uint16_t bios_error = bios_read_sector(seg,buf,c,h,s,drive_no);
	release_bios_lock();

	if(bios_error != 0) return MATFS16_ERR_DISK;

	return MATFS16_OK;
}

matfs16_status_t matfs16_next_block(uint8_t drive_no, uint16_t lba, uint16_t* next) {
	uint8_t* scratch_buf;
	matfs16_status_t status;
	uint16_t relative_block;
	uint16_t blockchain_block;
	uint16_t entry_in_block;
	uint16_t entry_offset;
	uint16_t next_relative;

	if(next == NULL) return MATFS16_ERR_INVALID_ARGUMENT;

	if(lba < MATFS16_DATA_START_LBA || lba >= MATFS16_BLOCK_COUNT) return MATFS16_ERR_INVALID_ARGUMENT;

	/* Convert the absolute LBA into a sector and entry in the blockchain. */
	relative_block   = lba - MATFS16_DATA_START_LBA;
	blockchain_block = relative_block / MATFS16_BLOCKCHAIN_ENTRIES_PER_BLOCK;
	entry_in_block   = relative_block % MATFS16_BLOCKCHAIN_ENTRIES_PER_BLOCK;

	scratch_buf = get_scratch_buf();
	status = matfs16_load_block(drive_no, MATFS16_BLOCKCHAIN_LBA + blockchain_block, SEG_KERN, (uint16_t)scratch_buf);

	if(status != MATFS16_OK) return status;

	if(!verify_blockchain_block(scratch_buf)) return MATFS16_ERR_BAD_BLOCKCHAIN;

	entry_offset = 2 + entry_in_block * sizeof(uint16_t);
	next_relative = read_u16(scratch_buf + entry_offset);

	if(next_relative == MATFS16_CHAIN_END) return MATFS16_END_OF_FILE;

	if(next_relative == MATFS16_CHAIN_FREE)       return MATFS16_ERR_BAD_BLOCKCHAIN;
	if(next_relative == MATFS16_CHAIN_RESERVED)   return MATFS16_ERR_BAD_BLOCKCHAIN;
	if(next_relative >= MATFS16_DATA_BLOCK_COUNT) return MATFS16_ERR_BAD_BLOCKCHAIN;	

	*next = MATFS16_DATA_START_LBA + next_relative;
	return MATFS16_OK;
}
