#include "kfiles.h"
#include "kheap.h"
#include "kmemmap.h"
#include "kthread.h"
#include "matfs16.h"
#include "utils.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

static file_desc_t* file_descriptors;
static uint16_t file_descriptor_count;
static uint8_t* file_scratch;
static volatile uint8_t file_lock;

static void acquire_file_lock(void) {
	uint8_t previous;

	do {
		previous = 1;
		__asm__ volatile (
			"xchgb %0, %1"
			: "+q" (previous), "+m" (file_lock)
			:
			: "memory"
		);

		if(previous != 0) kthread_yield();
	} while(previous != 0);
}

static void release_file_lock(void) {
	__asm__ volatile (
		"movb $0, %0"
		: "=m" (file_lock)
		:
		: "memory"
	);
}

static void copy_to_far_buffer(uint16_t seg, uint16_t buf, const uint8_t* source, uint16_t count) {
	uint8_t __far* destination = (uint8_t __far*)mk_farptr(seg, buf);
	uint16_t i;

	for(i = 0; i < count; i++) destination[i] = source[i];
}

void kfiles_init(uint16_t descriptor_count) {
	uint16_t i;

	if(descriptor_count > 255) descriptor_count = 255;

	file_descriptor_count = descriptor_count;
	file_descriptors = descriptor_count == 0 ? NULL : (file_desc_t*)kheap_alloc(descriptor_count * sizeof(file_desc_t));
	file_scratch = descriptor_count == 0 ? NULL : (uint8_t*)kheap_alloc(MATFS16_BLOCK_SIZE);
	file_lock = 0;

	for(i = 0; i < file_descriptor_count; i++) file_descriptors[i].used = false;
}

uint8_t kfile_open(uint8_t drive_num, const char* filename) {
	matfs16_status_t status;
	uint16_t first_lba;
	uint16_t length;
	uint16_t fd;

	if(filename == NULL) return KFILE_INVALID_FD;

	status = matfs16_locate_file(filename, drive_num, &first_lba, &length);
	if(status != MATFS16_OK || length == MATFS16_FILE_SIZE_UNKNOWN) return KFILE_INVALID_FD;

	acquire_file_lock();

	for(fd = 0; fd < file_descriptor_count; fd++) {
		if(file_descriptors[fd].used) continue;

		file_descriptors[fd].current_lba = first_lba;
		file_descriptors[fd].length = length;
		file_descriptors[fd].position = 0;
		file_descriptors[fd].block_offset = 0;
		file_descriptors[fd].drive_num = drive_num;
		file_descriptors[fd].owner = kthread_cur_thread();
		file_descriptors[fd].used = true;
		release_file_lock();
		return (uint8_t)fd;
	}

	release_file_lock();
	return KFILE_INVALID_FD;
}

bool kfile_close(uint8_t fd) {
	acquire_file_lock();

	if(fd >= file_descriptor_count || !file_descriptors[fd].used) {
		release_file_lock();
		return false;
	}

	file_descriptors[fd].used = false;
	release_file_lock();
	return true;
}

bool kfile_read(uint8_t fd, uint16_t count, uint16_t seg, uint16_t buf, uint16_t* bytes_read) {
	file_desc_t* descriptor;
	matfs16_status_t status;
	uint16_t available;
	uint16_t remaining;
	uint16_t chunk;
	uint16_t next_lba;

	if(bytes_read == NULL) return false;
	*bytes_read = 0;

	acquire_file_lock();

	if(fd >= file_descriptor_count || !file_descriptors[fd].used || file_descriptors[fd].owner != kthread_cur_thread()) {
		release_file_lock();
		return false;
	}

	descriptor = &file_descriptors[fd];
	available = descriptor->length - descriptor->position;
	remaining = count < available ? count : available;

	/* The bytes actually copied must fit before the end of the segment. */
	if(remaining != 0 && remaining - 1 > 0xFFFF - buf) {
		release_file_lock();
		return false;
	}

	while(remaining != 0) {
		if(descriptor->block_offset == MATFS16_BLOCK_SIZE) {
			status = matfs16_next_block(descriptor->drive_num, descriptor->current_lba, &next_lba);
			if(status != MATFS16_OK) {
				release_file_lock();
				return false;
			}
			descriptor->current_lba = next_lba;
			descriptor->block_offset = 0;
		}

		status = matfs16_load_block(descriptor->drive_num, descriptor->current_lba, SEG_KERN, (uint16_t)file_scratch);
		if(status != MATFS16_OK) {
			release_file_lock();
			return false;
		}

		chunk = MATFS16_BLOCK_SIZE - descriptor->block_offset;
		if(chunk > remaining) chunk = remaining;

		copy_to_far_buffer(seg, buf + *bytes_read, file_scratch + descriptor->block_offset, chunk);
		descriptor->block_offset += chunk;
		descriptor->position += chunk;
		*bytes_read += chunk;
		remaining -= chunk;
	}

	release_file_lock();
	return true;
}
