#pragma once

#include <stdint.h>

#include "kconsole.h"

/* Task images contain a suspended user context only. They never contain a
 * syscall continuation or a kernel stack. */

typedef enum task_status_t {
	TASK_OK = 0,
	TASK_ERR_INVALID_ARGUMENT,
	TASK_ERR_DISK,
	TASK_ERR_BAD_BLOCKCHAIN,
	TASK_ERR_TRUNCATED,
	TASK_ERR_TRAILING_BLOCKS,
	TASK_ERR_BAD_HEADER,
	TASK_ERR_UNSUPPORTED_VERSION,
	TASK_ERR_UNSUPPORTED_CONTEXT,
	TASK_ERR_UNSUPPORTED_ENCODING,
	TASK_ERR_INVALID_CONTEXT,
	TASK_ERR_NOT_IMPLEMENTED
} task_status_t;

typedef struct task_far_buffer_t {
	uint16_t segment;
	uint16_t offset;
} task_far_buffer_t;

typedef struct task_buffers_t {
	uint16_t memory_segment;
	task_far_buffer_t vram;
	task_far_buffer_t scratch;
} task_buffers_t;

typedef struct task_state_t {
	uint16_t resume_sp;
	uint16_t screen_mode;
	uint16_t vram_size;
	uint16_t memory_encoding;
	kconsole_saved_state_t console;
} task_state_t;

/*
 * Restore a task whose header is at first_lba. The caller supplies the task's
 * destination segment, a 4096-byte VRAM buffer, and a 512-byte scratch buffer.
 * The buffers must not wrap their respective segments.
 *
 * This function does not assign a console or register the task with the
 * scheduler. On success, state->resume_sp identifies the restored interrupt
 * frame which the caller may later give to the scheduler.
 */
task_status_t task_restore(uint8_t drive_no, uint16_t first_lba, const task_buffers_t *buffers, task_state_t *state);

/*
 * Reserved for saving a suspended user task into an already allocated MATFS16
 * chain. MATFS16 does not provide writes yet, so this returns
 * TASK_ERR_NOT_IMPLEMENTED.
 */
task_status_t task_save(uint8_t drive_no, uint16_t first_lba, const task_buffers_t *buffers, const task_state_t *state);
