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
uint16_t sys_write_block(uint8_t drive_no, uint16_t lba, const void* buffer);
uint16_t sys_read_block(uint8_t drive_no, uint16_t lba, void* buffer);
