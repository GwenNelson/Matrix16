#pragma once

#include <stdint.h>

#include "kconsole.h"
#include "kthread.h"

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
	TASK_ERR_BAD_COMPRESSION,
	TASK_ERR_NO_SPACE,
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
task_status_t task_restore(uint8_t drive_no, uint16_t first_lba,
	                       const task_buffers_t *buffers,
	                       task_state_t __far *state);

/* Replace a suspended user task from a TSK file, loading directly into its
 * assigned segment. On success its restored frame replaces the old one and
 * its open file handles are closed. If loading fails, the old task is
 * terminated because its segment may have been partially overwritten.
 * buffers->vram receives the restored screen image. */
task_status_t task_replace(kthread_id_t task_id, uint8_t drive_no,
	                       uint16_t first_lba, uint16_t task_segment,
	                       uint8_t console, const task_buffers_t *buffers,
	                       task_state_t __far *state);

/* End a suspended user task and close all of its open file handles. */
bool task_terminate(kthread_id_t task_id);

/* Save a suspended user task as a raw-memory TSK v1 file. The named file is
 * replaced, and the task's user frame is copied from memory_segment:resume_sp.
 * vram and scratch have the same size and segment constraints as task_restore. */
task_status_t task_save(uint8_t drive_no, const char *filename,
	                    const task_buffers_t *buffers,
	                    const task_state_t __far *state);
