#pragma once

#include <stdint.h>

#include <syscalls.inc>

void sys_write(const char* buffer, uint16_t length);
uint16_t sys_read(char* buffer, uint16_t length);
uint16_t sys_get_console(void);
