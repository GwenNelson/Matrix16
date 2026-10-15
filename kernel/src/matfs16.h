#pragma once

#include <stdint.h>

/* Fixed MATFS16 disk layout. */
#define MATFS16_BLOCK_SIZE                 512
#define MATFS16_BLOCK_COUNT                720
#define MATFS16_SUPERBLOCK_LBA               9
#define MATFS16_BLOCKCHAIN_LBA              10
#define MATFS16_BLOCKCHAIN_BLOCK_COUNT       3
#define MATFS16_BLOCKCHAIN_ENTRIES_PER_BLOCK 255
#define MATFS16_DATA_START_LBA              18
#define MATFS16_DATA_BLOCK_COUNT           702

/* A directory filename field is 26 bytes including its terminating NUL. */
#define MATFS16_FILENAME_MAXLEN             26
#define MATFS16_DIRECTORY_ENTRIES_PER_BLOCK 15
#define MATFS16_TASK_SLOT_COUNT               4
#define MATFS16_TASK_SLOT_EMPTY          0xFFFF

/* kernel_first_block value for a formatted disk with no kernel installed. */
#define MATFS16_KERNEL_NOT_INSTALLED     0xFFFF

/* Special values stored in blockchain entries. */
#define MATFS16_CHAIN_END              0xF00F
#define MATFS16_CHAIN_RESERVED         0xFFFD
#define MATFS16_CHAIN_FREE             0xFFFF

/* The directory entry does not provide an exact byte length for this file. */
#define MATFS16_FILE_SIZE_UNKNOWN 0xFFFF

typedef struct __attribute__((packed)) matfs_superblock_t {
	char     magic[5];              /* 0: "MAT16" */
	uint16_t media_type;            /* 5 */
	uint16_t block_size;            /* 7 */
	uint16_t block_count;           /* 9 */
	uint16_t blockchain_len;        /* 11 */
	uint16_t kernel_first_block;    /* 13: absolute LBA */
	uint16_t kernel_block_count;    /* 15 */
	uint16_t root_dir_start;        /* 17: absolute LBA */
	uint16_t root_dir_len;          /* 19 */
	uint16_t backup_super;          /* 21: absolute LBA, 0 if absent */
	uint16_t blockchain_backup;     /* 23: absolute LBA, 0 if absent */
	uint16_t backup_rootdir;        /* 25: absolute LBA, 0 if absent */
	uint16_t task0;                 /* 27: first LBA, 0xFFFF if absent */
	uint16_t task1;                 /* 29 */
	uint16_t task2;                 /* 31 */
	uint16_t task3;                 /* 33 */
	uint16_t checksum;              /* 35 */
	uint8_t  padding[475];          /* 37-511 */
} matfs_superblock_t;

typedef struct __attribute__((packed)) matfs_directory_entry_t {
	uint16_t first_block;
	uint16_t block_count;
	uint16_t logical_size;
	char     name[MATFS16_FILENAME_MAXLEN];
} matfs_directory_entry_t;

typedef struct __attribute__((packed)) matfs_directory_block_t {
	char     magic[4];
	uint16_t prev_block;
	uint16_t next_block;
	uint16_t parent_start;
	uint16_t parent_len;
	uint16_t entry_count;
	uint16_t checksum;
	uint8_t  padding[16];
	matfs_directory_entry_t entries[MATFS16_DIRECTORY_ENTRIES_PER_BLOCK];
} matfs_directory_block_t;

/* Status values returned by the small MATFS16 block-reading interface. */
typedef enum matfs16_status {
	MATFS16_OK = 0,
	MATFS16_END_OF_FILE,
	MATFS16_END_OF_DIRECTORY,
	MATFS16_ERR_INVALID_ARGUMENT,
	MATFS16_ERR_NOT_FOUND,
	MATFS16_ERR_DISK,
	MATFS16_ERR_BAD_SUPERBLOCK,
	MATFS16_ERR_BAD_DIRECTORY,
	MATFS16_ERR_BAD_BLOCKCHAIN,
	MATFS16_ERR_NO_SPACE,
	MATFS16_ERR_FILE_EXISTS,
	MATFS16_ERR_DIRECTORY_FULL,
	MATFS16_ERR_REFERENCED,
	MATFS16_ERR_NOT_IMPLEMENTED
} matfs16_status_t;

/*
 * Copy one zero-based entry from the root directory into caller-owned
 * storage. MATFS16_END_OF_DIRECTORY means that index is past the last entry.
 */
matfs16_status_t matfs16_read_direntry(uint8_t drive_no, uint16_t index, matfs_directory_entry_t *entry);

/* Return the validated root directory's first absolute LBA and block count. */
matfs16_status_t matfs16_locate_root(uint8_t drive_no, uint16_t *lba, uint16_t *block_count);

/*
 * Return the root-directory filename stored in one of the four superblock
 * task slots. filename must provide MATFS16_FILENAME_MAXLEN bytes and result
 * must point to caller-owned pointer storage.
 *
 * A populated slot copies its filename into filename and stores filename in
 * *result. An unused slot stores NULL in *result. Errors are returned as the
 * function's MATFS16 status value.
 */
matfs16_status_t matfs16_read_task_slot(uint8_t drive_no, uint8_t slot, char *filename, char **result);

/*
 * Find filename in the MATFS16 root directory on drive_no.
 *
 * On success, *lba receives the absolute disk LBA of the file's first data
 * block and *length receives its logical length in bytes. A length of
 * MATFS16_FILE_SIZE_UNKNOWN means that MATFS16 does not know the exact byte
 * length. The filename is read only and must be NUL terminated.
 */
matfs16_status_t matfs16_locate_file(
	const char *filename,
	uint8_t drive_no,
	uint16_t *lba,
	uint16_t *length
);

/* Read exactly one 512-byte sector at absolute disk LBA lba into seg:buf. */
matfs16_status_t matfs16_load_block(
	uint8_t drive_no,
	uint16_t lba,
	uint16_t seg,
	uint16_t buf
);

/* Load this drive's three blockchain sectors into the in-memory cache.
 * Calling this again while the cache is valid does not reread the disk. */
matfs16_status_t matfs16_cache_blockchain(uint8_t drive_no);

/* Mark a drive's cache stale after external or direct on-disk changes. */
void matfs16_invalidate_blockchain_cache(uint8_t drive_no);

/* Read adjacent LBAs in one BIOS request. The range must stay within one
 * track and the destination segment; this does not follow or validate a file
 * chain, so chain callers must verify every link before using it. */
matfs16_status_t matfs16_load_contiguous_blocks(
	uint8_t drive_no,
	uint16_t first_lba,
	uint16_t block_count,
	uint16_t seg,
	uint16_t buf
);

/* Write exactly one 512-byte sector at absolute disk LBA lba from seg:buf. */
matfs16_status_t matfs16_store_block(
	uint8_t drive_no,
	uint16_t lba,
	uint16_t seg,
	uint16_t buf
);

/* Create an empty root-directory file. Existing names are left untouched. */
matfs16_status_t matfs16_create_file(uint8_t drive_no, const char *filename);

/* Replace filename with an empty, allocated chain of block_count sectors.
 * logical_size may be MATFS16_FILE_SIZE_UNKNOWN when the exact length does
 * not fit the directory's 16-bit size field. */
matfs16_status_t matfs16_replace_file_blocks(
	uint8_t drive_no,
	const char *filename,
	uint16_t block_count,
	uint16_t logical_size,
	uint16_t *first_lba
);

/* Write one complete sector in an already allocated file chain. */
matfs16_status_t matfs16_write_file_block(
	uint8_t drive_no,
	uint16_t first_lba,
	uint16_t block_index,
	uint16_t seg,
	uint16_t buf
);

/* Remove a file and release its data chain, unless system metadata references it. */
matfs16_status_t matfs16_delete_file(uint8_t drive_no, const char *filename);

/* Append bytes at position (which must equal the current file length). */
matfs16_status_t matfs16_write_file(
	uint8_t drive_no,
	const char *filename,
	uint16_t position,
	uint16_t seg,
	uint16_t buf,
	uint16_t count,
	uint16_t *bytes_written
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
