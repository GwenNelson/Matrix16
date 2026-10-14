#pragma once

#include <stdint.h>

#include <syscalls.inc>

void sys_write(uint16_t handle, const char* buffer, uint16_t length);
uint16_t sys_read(uint16_t handle, char* buffer, uint16_t length);
uint16_t sys_get_console(void);
