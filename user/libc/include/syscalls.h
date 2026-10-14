#pragma once

#include <stdint.h>

#include <syscalls.inc>

void sys_write(uint16_t handle, const char* buffer, uint16_t length);
uint16_t sys_read(uint16_t handle, char* buffer, uint16_t length);
uint16_t sys_get_console(void);
uint16_t sys_load(const char* filename, void* buffer, uint16_t* length);
uint16_t sys_open(const char* filename);
uint16_t sys_close(uint16_t handle);
