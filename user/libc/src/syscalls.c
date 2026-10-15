#include "syscalls.h"

uint16_t sys_write(uint16_t handle, const char* buffer, uint16_t length) {
	uint16_t ax = SYS_WRITE;

	__asm__ volatile (
		"int $0x80"
		: "+a" (ax)
		: "b" (handle), "c" (length), "d" ((uint16_t)buffer)
		: "memory", "cc"
	);

	return ax;
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

uint16_t sys_open(const char* filename) {
	uint16_t ax = SYS_OPEN;

	__asm__ volatile (
		"int $0x80"
		: "+a" (ax)
		: "b" ((uint16_t)filename)
		: "memory", "cc"
	);

	return ax;
}

uint16_t sys_create(const char* filename) {
	uint16_t ax = SYS_CREATE;

	__asm__ volatile (
		"int $0x80"
		: "+a" (ax)
		: "b" ((uint16_t)filename)
		: "memory", "cc"
	);

	return ax;
}

uint16_t sys_delete(const char* filename) {
	uint16_t ax = SYS_DELETE;

	__asm__ volatile (
		"int $0x80"
		: "+a" (ax)
		: "b" ((uint16_t)filename)
		: "memory", "cc"
	);

	return ax;
}

uint16_t sys_close(uint16_t handle) {
	uint16_t ax = SYS_CLOSE;

	__asm__ volatile (
		"int $0x80"
		: "+a" (ax)
		: "b" (handle)
		: "memory", "cc"
	);

	return ax;
}

uint16_t sys_copy(uint16_t source_handle, uint16_t destination_handle) {
	uint16_t ax = SYS_COPY;

	__asm__ volatile (
		"int $0x80"
		: "+a" (ax)
		: "b" (source_handle), "c" (destination_handle)
		: "memory", "cc"
	);

	return ax;
}

uint16_t sys_get_free_blocks(uint8_t drive_no) {
	uint16_t ax = SYS_GET_FREE_BLOCKS;

	__asm__ volatile (
		"int $0x80"
		: "+a" (ax)
		: "b" ((uint16_t)drive_no)
		: "memory", "cc"
	);

	return ax;
}

uint16_t sys_write_block(uint8_t drive_no, uint16_t lba, const void* buffer) {
	uint16_t ax = SYS_WRITE_BLOCK;

	__asm__ volatile (
		"int $0x80"
		: "+a" (ax)
		: "b" ((uint16_t)drive_no), "c" (lba), "d" ((uint16_t)buffer)
		: "memory", "cc"
	);

	return ax;
}

uint16_t sys_read_block(uint8_t drive_no, uint16_t lba, void* buffer) {
	uint16_t ax = SYS_READ_BLOCK;

	__asm__ volatile (
		"int $0x80"
		: "+a" (ax)
		: "b" ((uint16_t)drive_no), "c" (lba), "d" ((uint16_t)buffer)
		: "memory", "cc"
	);

	return ax;
}
