#pragma once

#include <stdbool.h>
#include <stdint.h>

#define KFILE_INVALID_FD 0xFF

typedef struct file_desc_t {
	uint16_t current_lba;
	uint16_t length;
	uint16_t position;
	uint16_t block_offset;
	uint8_t drive_num;
	bool used;
} file_desc_t;

/* Allocate the global file descriptor table. Valid descriptors are 0-254. */
void kfiles_init(uint16_t descriptor_count);

/*
 * Open a file at offset zero, or return KFILE_INVALID_FD on error. Files whose
 * directory length is MATFS16_FILE_SIZE_UNKNOWN cannot be opened this way.
 */
uint8_t kfile_open(uint8_t drive_num, const char* filename);

/*
 * Read up to count bytes at the file's current position into seg:buf.
 * bytes_read receives the number copied. EOF is a successful short read.
 * If an error follows a partial read, bytes_read reports the bytes already
 * copied and the descriptor remains at its new position.
 */
bool kfile_read(uint8_t fd, uint16_t count, uint16_t seg, uint16_t buf, uint16_t* bytes_read);
