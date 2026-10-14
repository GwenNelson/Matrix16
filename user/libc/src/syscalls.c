#include "syscalls.h"

void sys_write(const char* buffer, uint16_t length) {
	uint16_t ax = SYS_WRITE;

	__asm__ volatile (
		"int $0x80"
		: "+a" (ax)
		: "b" ((uint16_t)1), "c" (length), "d" ((uint16_t)buffer)
		: "memory", "cc"
	);
}

uint16_t sys_read(char* buffer, uint16_t length) {
	uint16_t ax = SYS_READ;

	__asm__ volatile (
		"int $0x80"
		: "+a" (ax)
		: "b" ((uint16_t)0), "c" (length), "d" ((uint16_t)buffer)
		: "memory", "cc"
	);

	return ax;
}

uint16_t sys_get_console(void) {
	uint16_t ax = SYS_GET_CONSOLE;

	__asm__ volatile (
		"int $0x80"
		: "+a" (ax)
		:
		: "memory", "cc"
	);

	return ax;
}
