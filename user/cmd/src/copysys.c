#include <stdbool.h>
#include <stdint.h>

#include "stdio.h"
#include "syscalls.h"

#define COMMAND_LINE ((const char*)0x0100)

#define BLOCK_SIZE 512
#define DISK_BLOCK_COUNT 720
#define SUPERBLOCK_LBA 9
#define BLOCKCHAIN_LBA 10
#define BLOCKCHAIN_BLOCKS 3
#define DATA_START_LBA 18
#define KERNEL_MAX_BLOCKS 128
#define KERNEL_NOT_INSTALLED 0xFFFF
#define CHAIN_END 0xF00F

typedef struct __attribute__((packed)) matfs_superblock {
	char magic[5];
	uint16_t media_type;
	uint16_t block_size;
	uint16_t block_count;
	uint16_t blockchain_len;
	uint16_t kernel_first_block;
	uint16_t kernel_block_count;
	uint16_t root_dir_start;
	uint16_t root_dir_len;
	uint16_t backup_super;
	uint16_t blockchain_backup;
	uint16_t backup_rootdir;
	uint16_t task0;
	uint16_t task1;
	uint16_t task2;
	uint16_t task3;
	uint16_t checksum;
	uint8_t padding[475];
} matfs_superblock_t;

static uint8_t boot_image[9][BLOCK_SIZE];
static uint8_t sector_buffer[BLOCK_SIZE];
static uint8_t blockchain[BLOCKCHAIN_BLOCKS][BLOCK_SIZE];
static matfs_superblock_t target_superblock;

static const char source_kernel[] = "A:KERNEL.BIN";
static const char target_kernel[] = "B:KERNEL.BIN";
static const char source_root[] = "A:";
static const char target_root[] = "B:";

static uint16_t get_u16(const uint8_t* bytes) {
	return (uint16_t)bytes[0] | ((uint16_t)bytes[1] << 8);
}

static void put_u16(uint8_t* bytes, uint16_t value) {
	bytes[0] = (uint8_t)value;
	bytes[1] = (uint8_t)(value >> 8);
}

static uint16_t superblock_checksum(const matfs_superblock_t* superblock) {
	const uint8_t* bytes = (const uint8_t*)superblock;
	uint16_t checksum = 0;
	uint16_t offset;

	for(offset = 5; offset < 35; offset += 2) checksum += get_u16(bytes + offset);
	return checksum;
}

static bool valid_superblock(const matfs_superblock_t* superblock) {
	uint16_t i;

	if(superblock->magic[0] != 'M' || superblock->magic[1] != 'A' ||
	   superblock->magic[2] != 'T' || superblock->magic[3] != '1' ||
	   superblock->magic[4] != '6') return false;
	if(superblock->media_type != 0 || superblock->block_size != BLOCK_SIZE ||
	   superblock->block_count != DISK_BLOCK_COUNT ||
	   superblock->blockchain_len != BLOCKCHAIN_BLOCKS) return false;
	if(superblock->root_dir_start < DATA_START_LBA ||
	   superblock->root_dir_start >= DISK_BLOCK_COUNT ||
	   superblock->root_dir_len == 0 ||
	   superblock->root_dir_len > DISK_BLOCK_COUNT - superblock->root_dir_start) return false;
	if(superblock->checksum != superblock_checksum(superblock)) return false;
	for(i = 0; i < sizeof(superblock->padding); i++) {
		if(superblock->padding[i] != 0) return false;
	}
	return true;
}

static bool valid_directory_block(const uint8_t* block) {
	uint16_t checksum = 0;
	uint16_t offset;

	if(block[0] != 'D' || block[1] != 'I' || block[2] != 'R' || block[3] != 0) return false;
	for(offset = 0; offset < BLOCK_SIZE; offset += 2) {
		if(offset != 14) checksum += get_u16(block + offset);
	}
	return checksum == get_u16(block + 14);
}

static bool directory_name_matches(const uint8_t* field, const char* name) {
	uint16_t i;

	for(i = 0; i < 26; i++) {
		if(field[i] != (uint8_t)name[i]) return false;
		if(name[i] == '\0') return true;
	}
	return false;
}

static bool find_root_file(const char* root_path, const char* name,
	                       uint16_t* first_block, uint16_t* block_count,
	                       uint16_t* logical_size) {
	uint16_t handle = sys_open(root_path);
	uint16_t bytes_read;
	uint16_t entry;
	bool found = false;

	if(handle == SYSCALL_INVALID_HANDLE) return false;

	for(;;) {
		bytes_read = sys_read(handle, (char*)sector_buffer, BLOCK_SIZE);
		if(bytes_read == 0) break;
		if(bytes_read != BLOCK_SIZE || !valid_directory_block(sector_buffer)) break;

		for(entry = 0; entry < 15; entry++) {
			const uint8_t* item = sector_buffer + 32 + entry * 32;

			if(item[6] == 0 || !directory_name_matches(item + 6, name)) continue;
			*first_block = get_u16(item);
			*block_count = get_u16(item + 2);
			*logical_size = get_u16(item + 4);
			found = true;
			break;
		}
		if(found) break;
	}

	if(sys_close(handle) != 0) return false;
	return found;
}

static bool read_source_bootloader(void) {
	uint16_t lba;

	for(lba = 0; lba < 9; lba++) {
		if(sys_read_block(0, lba, boot_image[lba]) != 0) return false;
	}
	return boot_image[0][510] == 0x55 && boot_image[0][511] == 0xAA;
}

static bool copy_kernel_file(uint16_t expected_size) {
	uint16_t source_handle = sys_open(source_kernel);
	uint16_t target_handle;
	uint16_t copied = 0;
	uint16_t chunk;
	bool close_failed = false;

	if(source_handle == SYSCALL_INVALID_HANDLE) {
		printf("Cannot find KERNEL.BIN on A:\n");
		return false;
	}

	target_handle = sys_create(target_kernel);
	if(target_handle == SYSCALL_INVALID_HANDLE) {
		sys_close(source_handle);
		printf("Cannot create B:KERNEL.BIN; use a freshly formatted disk.\n");
		return false;
	}

	while(copied < expected_size) {
		uint16_t wanted = expected_size - copied;
		if(wanted > BLOCK_SIZE) wanted = BLOCK_SIZE;
		chunk = sys_read(source_handle, (char*)sector_buffer, wanted);
		if(chunk != wanted || sys_write(target_handle, (const char*)sector_buffer, chunk) != chunk) {
			sys_close(target_handle);
			sys_close(source_handle);
			printf("Kernel copy failed; B: may need to be formatted again.\n");
			return false;
		}
		copied += chunk;
	}

	if(sys_close(target_handle) != 0) close_failed = true;
	if(sys_close(source_handle) != 0) close_failed = true;
	if(close_failed) {
		printf("Could not close a kernel file handle.\n");
		return false;
	}
	return true;
}

static bool load_blockchain(uint8_t drive) {
	uint16_t i;
	uint16_t offset;
	uint16_t checksum;

	for(i = 0; i < BLOCKCHAIN_BLOCKS; i++) {
		if(sys_read_block(drive, BLOCKCHAIN_LBA + i, blockchain[i]) != 0) return false;
		checksum = 0;
		for(offset = 2; offset < BLOCK_SIZE; offset += 2) checksum += get_u16(blockchain[i] + offset);
		if(checksum != get_u16(blockchain[i])) return false;
	}
	return true;
}

static bool kernel_chain_is_contiguous(uint8_t drive, uint16_t first_lba, uint16_t blocks) {
	uint16_t i;
	uint16_t relative;
	uint16_t next;

	if(!load_blockchain(drive)) return false;
	for(i = 0; i < blocks; i++) {
		relative = (first_lba - DATA_START_LBA) + i;
		next = get_u16(blockchain[relative / 255] + 2 + (relative % 255) * 2);
		if(i + 1 == blocks) {
			if(next != CHAIN_END) return false;
		} else if(next != relative + 1) {
			return false;
		}
	}
	return true;
}

static bool install_kernel_metadata(uint16_t first_block, uint16_t blocks) {
	uint8_t* bytes = (uint8_t*)&target_superblock;

	if(sys_read_block(1, SUPERBLOCK_LBA, &target_superblock) != 0 ||
	   !valid_superblock(&target_superblock)) return false;
	if(target_superblock.kernel_first_block != KERNEL_NOT_INSTALLED ||
	   target_superblock.kernel_block_count != 0) return false;

	target_superblock.kernel_first_block = first_block;
	target_superblock.kernel_block_count = blocks;
	target_superblock.checksum = superblock_checksum(&target_superblock);
	put_u16(bytes + 35, target_superblock.checksum);
	return sys_write_block(1, SUPERBLOCK_LBA, &target_superblock) == 0;
}

static bool parse_destination(void) {
	const char* argument = COMMAND_LINE;

	while(*argument == ' ') argument++;
	if(argument[0] != 'B' || argument[1] != ':') {
		if(argument[0] == 'A' && argument[1] == ':') {
			printf("Only B: is supported as a COPYSYS destination.\n");
		} else {
			printf("Usage: COPYSYS B:\n");
		}
		return false;
	}
	argument += 2;
	while(*argument == ' ') argument++;
	if(*argument != '\0') {
		printf("Usage: COPYSYS B:\n");
		return false;
	}
	return true;
}

static bool confirm_copy(void) {
	char answer;

	printf("WARNING: this installs boot sectors and KERNEL.BIN on B:.\n");
	printf("Use a formatted floppy. Press Y to continue; any other key cancels.\n");
	while(sys_read(0, &answer, 1) == 0) { }
	if(answer == 'y') answer = 'Y';
	if(answer == 'Y') return true;
	printf("COPYSYS cancelled.\n");
	return false;
}

void copysys_main(void) {
	matfs_superblock_t source_superblock;
	uint16_t source_first;
	uint16_t source_blocks;
	uint16_t kernel_size;
	uint16_t target_first;
	uint16_t target_blocks;
	uint16_t target_size;
	uint16_t expected_blocks;
	uint16_t lba;

	if(!parse_destination()) return;
	if(sys_read_block(0, SUPERBLOCK_LBA, &source_superblock) != 0 ||
	   !valid_superblock(&source_superblock) ||
	   source_superblock.kernel_first_block < DATA_START_LBA ||
	   source_superblock.kernel_first_block >= DISK_BLOCK_COUNT ||
	   source_superblock.kernel_block_count == 0 ||
	   source_superblock.kernel_block_count > KERNEL_MAX_BLOCKS ||
	   source_superblock.kernel_block_count > DISK_BLOCK_COUNT - source_superblock.kernel_first_block) {
		printf("A: does not contain a valid installed kernel.\n");
		return;
	}
	if(!read_source_bootloader()) {
		printf("Could not read a valid bootloader from A:.\n");
		return;
	}
	if(!find_root_file(source_root, "KERNEL.BIN", &source_first, &source_blocks, &kernel_size) ||
	   source_first != source_superblock.kernel_first_block ||
	   source_blocks != source_superblock.kernel_block_count ||
	   kernel_size == 0xFFFF || kernel_size == 0 ||
	   (kernel_size / BLOCK_SIZE + (kernel_size % BLOCK_SIZE != 0)) != source_blocks ||
	   !kernel_chain_is_contiguous(0, source_first, source_blocks)) {
		printf("Cannot find a usable KERNEL.BIN on A:.\n");
		return;
	}

	if(sys_read_block(1, SUPERBLOCK_LBA, &target_superblock) != 0 ||
	   !valid_superblock(&target_superblock) ||
	   target_superblock.kernel_first_block != KERNEL_NOT_INSTALLED ||
	   target_superblock.kernel_block_count != 0) {
		printf("B: must be a formatted disk with no kernel installed.\n");
		return;
	}
	if(!confirm_copy()) return;

	printf("Copying KERNEL.BIN...\n");
	if(!copy_kernel_file(kernel_size)) return;
	if(!find_root_file(target_root, "KERNEL.BIN", &target_first, &target_blocks, &target_size) ||
	   target_size != kernel_size || target_blocks != source_blocks ||
	   target_first < DATA_START_LBA || target_first >= DISK_BLOCK_COUNT ||
	   target_blocks == 0 || target_blocks > KERNEL_MAX_BLOCKS ||
	   target_blocks > DISK_BLOCK_COUNT - target_first ||
	   !kernel_chain_is_contiguous(1, target_first, target_blocks)) {
		printf("B:KERNEL.BIN is not a usable contiguous kernel; format B: again.\n");
		return;
	}
	if(!install_kernel_metadata(target_first, target_blocks)) {
		printf("Could not update B: superblock; format B: again.\n");
		return;
	}

	printf("Copying bootloader...\n");
	for(lba = 0; lba < 9; lba++) {
		if(sys_write_block(1, lba, boot_image[lba]) != 0) {
			printf("Bootloader copy failed; B: may not boot.\n");
			return;
		}
	}
	printf("COPYSYS complete. B: should now be bootable.\n");
}
