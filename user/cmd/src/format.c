#include <stdbool.h>
#include <stdint.h>

#include "stdio.h"
#include "syscalls.h"

#define COMMAND_LINE ((const char*)0x0100)

#define MATFS_BLOCK_SIZE 512
#define MATFS_BLOCK_COUNT 720
#define MATFS_SUPER_LBA 9
#define MATFS_BLOCKCHAIN_LBA 10
#define MATFS_BLOCKCHAIN_SECTORS 3
#define MATFS_RESERVED_LBA 13
#define MATFS_RESERVED_SECTORS 5
#define MATFS_DATA_LBA 18
#define MATFS_DATA_BLOCKS 702
#define MATFS_CHAIN_ENTRIES_PER_SECTOR 255
#define MATFS_CHAIN_END 0xF00F
#define MATFS_CHAIN_FREE 0xFFFF
#define MATFS_KERNEL_NOT_INSTALLED 0xFFFF

static uint8_t sector_buffer[MATFS_BLOCK_SIZE];

static void clear_sector(void) {
	uint16_t i;

	for(i = 0; i < MATFS_BLOCK_SIZE; i++) sector_buffer[i] = 0;
}

static void put_u16(uint16_t offset, uint16_t value) {
	sector_buffer[offset] = (uint8_t)value;
	sector_buffer[offset + 1] = (uint8_t)(value >> 8);
}

static uint16_t get_u16(uint16_t offset) {
	return (uint16_t)sector_buffer[offset] |
	       ((uint16_t)sector_buffer[offset + 1] << 8);
}

static uint16_t checksum_sector(uint16_t skipped_offset) {
	uint16_t checksum = 0;
	uint16_t offset;

	for(offset = 0; offset < MATFS_BLOCK_SIZE; offset += 2) {
		if(offset != skipped_offset) checksum += get_u16(offset);
	}

	return checksum;
}

static uint16_t checksum_superblock(void) {
	uint16_t checksum = 0;
	uint16_t offset;

	/* The packed fields start at byte 5, after the five-byte magic. */
	for(offset = 5; offset < 35; offset += 2) {
		checksum += (uint16_t)sector_buffer[offset] |
		            ((uint16_t)sector_buffer[offset + 1] << 8);
	}

	return checksum;
}

static bool write_sector(uint8_t drive, uint16_t lba) {
	return sys_write_block(drive, lba, sector_buffer) == 0;
}

static bool parse_drive(uint8_t* drive, char* letter) {
	const char* argument = COMMAND_LINE;
	uint8_t first;

	while(*argument == ' ') argument++;
	first = (uint8_t)*argument;
	if(first >= 'a' && first <= 'z') first -= 'a' - 'A';
	if((first != 'A' && first != 'B') || argument[1] != ':') return false;
	argument += 2;
	while(*argument == ' ') argument++;
	if(*argument != '\0') return false;

	*drive = first == 'A' ? 0 : 1;
	*letter = (char)first;
	return true;
}

static bool confirm_format(char letter) {
	char answer;

	if(letter == 'A') printf("WARNING: this erases all MATFS16 data on drive A:\n");
	else printf("WARNING: this erases all MATFS16 data on drive B:\n");
	printf("Press Y to continue, any other key cancels.\n");
	while(sys_read(0, &answer, 1) == 0) { }
	if(answer == 'y') answer = 'Y';
	if(answer == 'Y') return true;

	printf("Format cancelled.\n");
	return false;
}

static bool write_blockchain(uint8_t drive) {
	uint16_t sector_index;
	uint16_t entry;
	uint16_t relative_block;

	for(sector_index = 0; sector_index < MATFS_BLOCKCHAIN_SECTORS; sector_index++) {
		clear_sector();
		for(entry = 0; entry < MATFS_CHAIN_ENTRIES_PER_SECTOR; entry++) {
			relative_block = sector_index * MATFS_CHAIN_ENTRIES_PER_SECTOR + entry;
			if(relative_block >= MATFS_DATA_BLOCKS) break;

			put_u16(2 + entry * 2,
			        relative_block == 0 ? MATFS_CHAIN_END : MATFS_CHAIN_FREE);
		}

		put_u16(0, checksum_sector(0));
		if(!write_sector(drive, MATFS_BLOCKCHAIN_LBA + sector_index)) return false;
	}

	return true;
}

static bool clear_reserved_sectors(uint8_t drive) {
	uint16_t i;

	for(i = 0; i < MATFS_RESERVED_SECTORS; i++) {
		clear_sector();
		if(!write_sector(drive, MATFS_RESERVED_LBA + i)) return false;
	}

	return true;
}

static bool write_root_directory(uint8_t drive) {
	clear_sector();
	sector_buffer[0] = 'D';
	sector_buffer[1] = 'I';
	sector_buffer[2] = 'R';
	sector_buffer[3] = '\0';
	put_u16(4, MATFS_DATA_LBA);       /* prev_block points to the root itself */
	put_u16(6, MATFS_CHAIN_END);      /* this root directory has one block */
	put_u16(8, MATFS_DATA_LBA);       /* parent_start points to the root itself */
	put_u16(10, 1);			      /* root directory block count */
	put_u16(12, 0);			      /* no files yet */
	put_u16(14, checksum_sector(14));

	return write_sector(drive, MATFS_DATA_LBA);
}

static bool write_superblock(uint8_t drive) {
	clear_sector();
	sector_buffer[0] = 'M';
	sector_buffer[1] = 'A';
	sector_buffer[2] = 'T';
	sector_buffer[3] = '1';
	sector_buffer[4] = '6';
	put_u16(5, 0);                              /* 360 KiB floppy */
	put_u16(7, MATFS_BLOCK_SIZE);
	put_u16(9, MATFS_BLOCK_COUNT);
	put_u16(11, MATFS_BLOCKCHAIN_SECTORS);
	put_u16(13, MATFS_KERNEL_NOT_INSTALLED);
	put_u16(15, 0);				      /* no kernel installed */
	put_u16(17, MATFS_DATA_LBA);
	put_u16(19, 1);
	put_u16(21, 0);				      /* no backup superblock */
	put_u16(23, 0);				      /* no backup blockchain */
	put_u16(25, 0);				      /* no emergency root directory */
	put_u16(27, MATFS_CHAIN_FREE);
	put_u16(29, MATFS_CHAIN_FREE);
	put_u16(31, MATFS_CHAIN_FREE);
	put_u16(33, MATFS_CHAIN_FREE);
	put_u16(35, checksum_superblock());

	return write_sector(drive, MATFS_SUPER_LBA);
}

void format_main(void) {
	uint8_t drive;
	char drive_letter;

	if(!parse_drive(&drive, &drive_letter)) {
		printf("Usage: FORMAT A: or FORMAT B:\n");
		return;
	}
	if(!confirm_format(drive_letter)) return;

	printf("Formatting...\n");

	/* Invalidate the old filesystem first, then write its replacement metadata.
	 * Keep the final superblock write last so it only advertises completed data. */
	clear_sector();
	if(!write_sector(drive, MATFS_SUPER_LBA) ||
	   !clear_reserved_sectors(drive) ||
	   !write_blockchain(drive) ||
	   !write_root_directory(drive) ||
	   !write_superblock(drive)) {
		printf("Disk write failed; the disk may be partially formatted.\n");
		return;
	}

	printf("Format complete. Use COPYSYS before booting this disk.\n");
}
