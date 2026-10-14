#include "syscalls.h"

void sys_write(uint16_t handle, const char* buffer, uint16_t length) {
	uint16_t ax = SYS_WRITE;

	__asm__ volatile (
		"int $0x80"
		: "+a" (ax)
		: "b" (handle), "c" (length), "d" ((uint16_t)buffer)
		: "memory", "cc"
	);
}

uint16_t sys_read(uint16_t handle, char* buffer, uint16_t length) {
	uint16_t ax = SYS_READ;

	__asm__ volatile (
		"int $0x80"
		: "+a" (ax)
		: "b" (handle), "c" (length), "d" ((uint16_t)buffer)
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

uint16_t sys_load(const char* filename, void* buffer, uint16_t* length) {
	uint16_t ax = SYS_LOAD;
	uint16_t cx;

	__asm__ volatile (
		"int $0x80"
		: "+a" (ax), "=c" (cx)
		: "b" ((uint16_t)filename), "d" ((uint16_t)buffer)
		: "memory", "cc"
	);

	if(length != 0) *length = cx;
	return ax;
}
