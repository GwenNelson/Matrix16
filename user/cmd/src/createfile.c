#include <stdint.h>

#include "stdio.h"
#include "syscalls.h"

#define COMMAND_LINE ((const char*)0x0100)
#define CREATEFILE_BUFFER_SIZE 512

static char file_buffer[CREATEFILE_BUFFER_SIZE];

static uint16_t prepare_filename(char* path) {
	const char* argument = COMMAND_LINE;
	uint16_t length = 0;
	uint16_t i;

	while(*argument == ' ') argument++;
	while(argument[length] != '\0') length++;
	while(length != 0 && argument[length - 1] == ' ') length--;
	if(length == 0) return 0;
	for(i = 0; i < length; i++) if(argument[i] == ' ') return 0;

	if(length >= 2 && argument[1] == ':') {
		if(argument[0] != 'A' && argument[0] != 'B') return 0;
		if(length == 2 || length > 27) return 0;
		for(i = 0; i <= length; i++) path[i] = argument[i];
	} else {
		if(length > 25) return 0;
		path[0] = 'A';
		path[1] = ':';
		for(i = 0; i < length; i++) path[i + 2] = argument[i];
		path[length + 2] = '\0';
	}

	return 1;
}

void createfile_main(void) {
	char path[28];
	char input;
	char echo[2];
	uint16_t handle;
	uint16_t buffered = 0;
	uint16_t count;

	if(!prepare_filename(path)) {
		printf("Usage: CREATEFILE [A:|B:]filename\n");
		return;
	}

	handle = sys_create(path);
	if(handle == SYSCALL_INVALID_HANDLE) {
		printf("Unable to create file\n");
		return;
	}

	printf("Enter text; Ctrl-Z or Ctrl-D finishes.\n");

	for(;;) {
		if(sys_read(0, &input, 1) == 0) continue;
		if(input == 0x1A || input == 0x04) break;

		echo[0] = input;
		echo[1] = '\0';
		printf("%s", echo);
		file_buffer[buffered++] = input;
		if(buffered != CREATEFILE_BUFFER_SIZE) continue;

		count = sys_write(handle, file_buffer, buffered);
		if(count != buffered) {
			printf("Disk write failed\n");
			buffered = 0;
			break;
		}
		buffered = 0;
	}

	if(buffered != 0) {
		count = sys_write(handle, file_buffer, buffered);
		if(count != buffered) {
			printf("Disk write failed\n");
		}
	}

	printf("\n");
	sys_close(handle);
}
