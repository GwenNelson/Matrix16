#include "kfiles.h"
#include "kheap.h"
#include "kmemmap.h"
#include "kthread.h"
#include "matfs16.h"
#include "string.h"
#include "utils.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

static file_desc_t* file_descriptors;
static uint16_t file_descriptor_count;
static uint8_t* file_scratch;
static volatile uint8_t file_lock;

extern uint8_t kernel_heap_start;
extern uint8_t kernel_heap_end;

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

static uint16_t directory_bytes_available(file_desc_t* descriptor) {
	if(descriptor->blocks_remaining == 0) return 0;
	if(descriptor->blocks_remaining > 128) return 0xFFFF;
	if(descriptor->blocks_remaining == 128) {
		if(descriptor->block_offset == 0) return 0xFFFF;
		return 0 - descriptor->block_offset;
	}

	return descriptor->blocks_remaining * MATFS16_BLOCK_SIZE - descriptor->block_offset;
}

void kfiles_init(uint16_t descriptor_count) {
	uint16_t i;

	if(descriptor_count > kfiles_max_descriptors())
		descriptor_count = kfiles_max_descriptors();

	file_descriptor_count = descriptor_count;
	file_descriptors = descriptor_count == 0 ? NULL : (file_desc_t*)kheap_alloc(descriptor_count * sizeof(file_desc_t));
	file_scratch = descriptor_count == 0 ? NULL : (uint8_t*)kheap_alloc(MATFS16_BLOCK_SIZE);
	file_lock = 0;

	for(i = 0; i < file_descriptor_count; i++) file_descriptors[i].used = false;
}

uint16_t kfiles_max_descriptors(void) {
	uint16_t heap_start = (uint16_t)&kernel_heap_start;
	uint16_t heap_end = (uint16_t)&kernel_heap_end;
	uint16_t heap_bytes;
	uint16_t descriptor_bytes;
	uint16_t maximum;

	if(heap_end <= heap_start) return 0;
	heap_bytes = heap_end - heap_start;
	if(heap_bytes < MATFS16_BLOCK_SIZE) return 0;

	/* Leave room for both allocations made when at least one descriptor is
	 * configured: the descriptor table and one 512-byte I/O scratch block. */
	maximum = (heap_bytes - MATFS16_BLOCK_SIZE) / sizeof(file_desc_t);
	if(maximum > KFILES_MAX_DESCRIPTORS) maximum = KFILES_MAX_DESCRIPTORS;

	/* kheap_alloc rounds odd allocation sizes up to an even byte count. */
	while(maximum != 0) {
		descriptor_bytes = maximum * sizeof(file_desc_t);
		if(descriptor_bytes & 1) descriptor_bytes++;
		if(descriptor_bytes <= heap_bytes - MATFS16_BLOCK_SIZE) break;
		maximum--;
	}

	return maximum;
}

bool task_has_open_files(kthread_id_t id) {
	for(uint16_t fd = 0; fd < file_descriptor_count; fd++) {
		if(file_descriptors[fd].used && file_descriptors[fd].owner == id)
			return true;
	}
	return false;
}

uint8_t kfile_open(uint8_t drive_num, const char* filename) {
	matfs16_status_t status;
	uint16_t first_lba;
	uint16_t length;
	uint16_t block_count = 0;
	uint16_t fd;
	bool directory;

	if(filename == NULL) return KFILE_INVALID_FD;

	directory = filename[0] == '\0';
	if(directory) {
		status = matfs16_locate_root(drive_num, &first_lba, &block_count);
		length = 0;
	} else {
		status = matfs16_locate_file(filename, drive_num, &first_lba, &length);
	}
	if(status != MATFS16_OK) return KFILE_INVALID_FD;
	if(!directory && length == MATFS16_FILE_SIZE_UNKNOWN) return KFILE_INVALID_FD;

	acquire_file_lock();

	for(fd = 0; fd < file_descriptor_count; fd++) {
		if(file_descriptors[fd].used) continue;

		file_descriptors[fd].current_lba = first_lba;
		file_descriptors[fd].write_tail_lba = 0;
		file_descriptors[fd].length = length;
		file_descriptors[fd].position = 0;
		file_descriptors[fd].block_offset = 0;
		file_descriptors[fd].blocks_remaining = block_count;
		file_descriptors[fd].drive_num = drive_num;
		file_descriptors[fd].owner = kthread_cur_thread();
		if(directory) file_descriptors[fd].filename[0] = '\0';
		else strcpy(file_descriptors[fd].filename, filename);
		file_descriptors[fd].directory = directory;
		file_descriptors[fd].writable = false;
		file_descriptors[fd].used = true;
		release_file_lock();
		return (uint8_t)fd;
	}

	release_file_lock();
	return KFILE_INVALID_FD;
}

uint8_t kfile_create(uint8_t drive_num, const char* filename) {
	matfs16_status_t status;
	uint16_t fd;
	uint16_t i;
	uint16_t name_length;

	if(filename == NULL) return KFILE_INVALID_FD;
	name_length = strnlen(filename, MATFS16_FILENAME_MAXLEN);
	if(name_length == 0 || name_length == MATFS16_FILENAME_MAXLEN) return KFILE_INVALID_FD;

	acquire_file_lock();
	for(fd = 0; fd < file_descriptor_count; fd++) {
		if(file_descriptors[fd].used) continue;
		file_descriptors[fd].owner = kthread_cur_thread();
		file_descriptors[fd].writable = true;
		file_descriptors[fd].directory = false;
		file_descriptors[fd].drive_num = drive_num;
		file_descriptors[fd].position = 0;
		file_descriptors[fd].length = 0;
		file_descriptors[fd].current_lba = 0;
		file_descriptors[fd].write_tail_lba = 0;
		file_descriptors[fd].block_offset = 0;
		file_descriptors[fd].blocks_remaining = 0;
		for(i = 0; i <= name_length; i++) file_descriptors[fd].filename[i] = filename[i];

		status = matfs16_create_file(drive_num, filename);
		if(status != MATFS16_OK) {
			release_file_lock();
			return KFILE_INVALID_FD;
		}

		/* Publish the descriptor only after its owner and fields are ready. */
		file_descriptors[fd].used = true;
		release_file_lock();
		return (uint8_t)fd;
	}

	release_file_lock();
	return KFILE_INVALID_FD;
}

bool kfile_delete(uint8_t drive_num, const char* filename) {
	uint16_t fd;
	matfs16_status_t status;

	if(filename == NULL) return false;
	acquire_file_lock();
	for(fd = 0; fd < file_descriptor_count; fd++) {
		if(file_descriptors[fd].used && file_descriptors[fd].drive_num == drive_num &&
		   (file_descriptors[fd].directory || strcmp(file_descriptors[fd].filename, filename) == 0)) {
			release_file_lock();
			return false;
		}
	}

	status = matfs16_delete_file(drive_num, filename);
	release_file_lock();
	return status == MATFS16_OK;
}

bool kfile_rename(uint8_t drive_num, const char* old_name, const char* new_name) {
	uint16_t fd;
	matfs16_status_t status;

	if(drive_num > 1 || old_name == NULL || new_name == NULL) return false;
	acquire_file_lock();
	for(fd = 0; fd < file_descriptor_count; fd++) {
		if(!file_descriptors[fd].used || file_descriptors[fd].drive_num != drive_num) continue;
		if(file_descriptors[fd].directory ||
		   strcmp(file_descriptors[fd].filename, old_name) == 0 ||
		   strcmp(file_descriptors[fd].filename, new_name) == 0) {
			release_file_lock();
			return false;
		}
	}

	status = matfs16_rename_file(drive_num, old_name, new_name);
	release_file_lock();
	return status == MATFS16_OK;
}

bool kfile_close(uint8_t fd, uint8_t *drive_num) {
	if(drive_num == NULL) return false;

	acquire_file_lock();

	if(fd >= file_descriptor_count || !file_descriptors[fd].used) {
		release_file_lock();
		return false;
	}

	*drive_num = file_descriptors[fd].drive_num;
	file_descriptors[fd].used = false;
	release_file_lock();
	return true;
}

static bool kfile_read_locked(uint8_t fd, uint16_t count, uint16_t seg, uint16_t buf, uint16_t* bytes_read) {
	file_desc_t* descriptor;
	matfs16_status_t status;
	uint16_t available;
	uint16_t remaining;
	uint16_t chunk;
	uint16_t next_lba;

	if(bytes_read == NULL) return false;
	*bytes_read = 0;

	if(fd >= file_descriptor_count || !file_descriptors[fd].used || file_descriptors[fd].owner != kthread_cur_thread()) {
		return false;
	}

	descriptor = &file_descriptors[fd];
	if(descriptor->directory) available = directory_bytes_available(descriptor);
	else available = descriptor->length - descriptor->position;
	remaining = count < available ? count : available;

	/* The bytes actually copied must fit before the end of the segment. */
	if(remaining != 0 && remaining - 1 > 0xFFFF - buf) {
		return false;
	}

	while(remaining != 0) {
		if(descriptor->block_offset == MATFS16_BLOCK_SIZE) {
			status = matfs16_next_block(descriptor->drive_num, descriptor->current_lba, &next_lba);
			if(status != MATFS16_OK) {
				return false;
			}
			if(descriptor->directory) descriptor->blocks_remaining--;
			descriptor->current_lba = next_lba;
			descriptor->block_offset = 0;
		}

		status = matfs16_load_block(descriptor->drive_num, descriptor->current_lba, SEG_KERN, (uint16_t)file_scratch);
		if(status != MATFS16_OK) {
			return false;
		}

		chunk = MATFS16_BLOCK_SIZE - descriptor->block_offset;
		if(chunk > remaining) chunk = remaining;

		copy_to_far_buffer(seg, buf + *bytes_read, file_scratch + descriptor->block_offset, chunk);
		descriptor->block_offset += chunk;
		if(!descriptor->directory) descriptor->position += chunk;
		*bytes_read += chunk;
		remaining -= chunk;
	}

	return true;
}

bool kfile_read(uint8_t fd, uint16_t count, uint16_t seg, uint16_t buf, uint16_t* bytes_read) {
	bool result;

	acquire_file_lock();
	result = kfile_read_locked(fd, count, seg, buf, bytes_read);
	release_file_lock();
	return result;
}

static bool kfile_write_locked(uint8_t fd, uint16_t count, uint16_t seg, uint16_t buf, uint16_t* bytes_written) {
	file_desc_t* descriptor;
	matfs16_status_t status;
	uint16_t written = 0;

	if(bytes_written == NULL) return false;
	*bytes_written = 0;
	if(count != 0 && (buf > 0xFE00 || count - 1 > 0xFFFF - buf)) return false;

	if(fd >= file_descriptor_count || !file_descriptors[fd].used ||
	   file_descriptors[fd].owner != kthread_cur_thread() ||
	   !file_descriptors[fd].writable || file_descriptors[fd].directory) {
		return false;
	}

	descriptor = &file_descriptors[fd];
	status = matfs16_write_file(descriptor->drive_num, descriptor->filename,
	                            descriptor->position, &descriptor->write_tail_lba,
	                            seg, buf, count, &written);
	descriptor->position += written;
	descriptor->length += written;
	*bytes_written = written;
	return status == MATFS16_OK;
}

bool kfile_write(uint8_t fd, uint16_t count, uint16_t seg, uint16_t buf, uint16_t* bytes_written) {
	bool result;

	acquire_file_lock();
	result = kfile_write_locked(fd, count, seg, buf, bytes_written);
	release_file_lock();
	return result;
}

bool kfile_copy(uint8_t source_fd, uint8_t destination_fd) {
	file_desc_t* source;
	file_desc_t* destination;
	uint16_t bytes_read;
	uint16_t bytes_written;
	uint16_t chunk;
	bool success = false;

	acquire_file_lock();
	if(source_fd >= file_descriptor_count || destination_fd >= file_descriptor_count ||
	   source_fd == destination_fd || !file_descriptors[source_fd].used ||
	   !file_descriptors[destination_fd].used) goto done;

	source = &file_descriptors[source_fd];
	destination = &file_descriptors[destination_fd];
	if(source->owner != kthread_cur_thread() || destination->owner != kthread_cur_thread() ||
	   source->directory || destination->directory || !destination->writable) goto done;
	if(source->drive_num == destination->drive_num &&
	   strcmp(source->filename, destination->filename) == 0) goto done;

	while(source->position < source->length) {
		chunk = source->length - source->position;
		if(chunk > MATFS16_BLOCK_SIZE) chunk = MATFS16_BLOCK_SIZE;

		if(!kfile_read_locked(source_fd, chunk, SEG_KERN,
		                      (uint16_t)file_scratch, &bytes_read) || bytes_read != chunk) goto done;
		if(!kfile_write_locked(destination_fd, bytes_read, SEG_KERN,
		                       (uint16_t)file_scratch, &bytes_written) ||
		   bytes_written != bytes_read) goto done;
	}

	success = true;

done:
	release_file_lock();
	return success;
}
