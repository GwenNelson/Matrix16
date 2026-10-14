#pragma once

#include <stdint.h>

/* Status values returned by the small MATFS16 block-reading interface. */
typedef enum matfs16_status {
	MATFS16_OK = 0,
	MATFS16_END_OF_FILE,
	MATFS16_ERR_INVALID_ARGUMENT,
	MATFS16_ERR_NOT_FOUND,
	MATFS16_ERR_DISK,
	MATFS16_ERR_BAD_SUPERBLOCK,
	MATFS16_ERR_BAD_DIRECTORY,
	MATFS16_ERR_BAD_BLOCKCHAIN,
	MATFS16_ERR_NOT_IMPLEMENTED
} matfs16_status_t;

/*
 * Find filename in the MATFS16 root directory on drive_no.
 *
 * On success, *lba receives the absolute disk LBA of the file's first data
 * block. The filename is read only and must be NUL terminated.
 */
matfs16_status_t matfs16_locate_file(
	const char *filename,
	uint8_t drive_no,
	uint16_t *lba
);

/* Read exactly one 512-byte sector at absolute disk LBA lba into seg:buf. */
matfs16_status_t matfs16_load_block(
	uint8_t drive_no,
	uint16_t lba,
	uint16_t seg,
	uint16_t buf
);

/*
 * Follow the blockchain entry for the data block at absolute disk LBA lba.
 *
 * On success, *next receives the absolute disk LBA of the next data block.
 * MATFS16_END_OF_FILE means that lba is the last block in the chain.
 */
matfs16_status_t matfs16_next_block(
	uint8_t drive_no,
	uint16_t lba,
	uint16_t *next
);
