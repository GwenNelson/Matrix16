#pragma once

#include <stdint.h>

#include <syscalls.inc>

uint16_t sys_write(uint16_t handle, const char* buffer, uint16_t length);
uint16_t sys_read(uint16_t handle, char* buffer, uint16_t length);
uint16_t sys_get_console(void);
uint16_t sys_load(const char* filename, void* buffer, uint16_t* length);
uint16_t sys_open(const char* filename);
uint16_t sys_create(const char* filename);
uint16_t sys_delete(const char* filename);
uint16_t sys_close(uint16_t handle);
uint16_t sys_copy(uint16_t source_handle, uint16_t destination_handle);
uint16_t sys_get_free_blocks(uint8_t drive_no);
uint16_t sys_write_block(uint8_t drive_no, uint16_t lba, const void* buffer);
uint16_t sys_read_block(uint8_t drive_no, uint16_t lba, void* buffer);
/* filename includes A: or B:. Return 0 on success, 1 on failure. A missing
 * file leaves the target alone; a later load error leaves it terminated.
 * target is SYSCALL_TASK_SELF or slot 0-3; slot 4 is reserved. A successful
 * self-exec starts the new task and does not return to this call. */
uint16_t sys_exec(const char* filename, uint8_t target);
/* Return 0 on success, 1 if the target slot is invalid or already empty.
 * target is SYSCALL_TASK_SELF or slot 0-3; slot 4 is reserved. Terminating
 * self does not return. */
uint16_t sys_term(uint8_t target);
/* Claim returns 0 if acquired, 1 if already claimed. Release returns 0 only
 * when this task owns the claim; claims are also released on task exit. */
uint16_t sys_claim_aux(void);
uint16_t sys_release_aux(void);
