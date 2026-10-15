#include <stdint.h>

#include "stdio.h"
#include "syscalls.h"

#define COMMAND_LINE ((const char*)0x0100)

void diskusage_main(void) {
	const char* argument = COMMAND_LINE;
	uint8_t drive = 0;
	uint16_t free_blocks;

	while(*argument == ' ') argument++;
	if(*argument != '\0') {
		char letter = argument[0];
		if(letter >= 'a' && letter <= 'z') letter -= 'a' - 'A';
		if((letter != 'A' && letter != 'B') || argument[1] != ':' ||
		   (argument[2] != '\0' && argument[2] != ' ')) {
			printf("Usage: DISKUSAGE [A:|B:]\n");
			return;
		}
		drive = letter == 'A' ? 0 : 1;
		argument += 2;
		while(*argument == ' ') argument++;
		if(*argument != '\0') {
			printf("Usage: DISKUSAGE [A:|B:]\n");
			return;
		}
	}

	free_blocks = sys_get_free_blocks(drive);
	if(free_blocks == 0xFFFF) {
		printf("Could not read free space from %s\n", drive == 0 ? "A:" : "B:");
		return;
	}

	printf("%s has %u free blocks\n", drive == 0 ? "A:" : "B:", free_blocks);
}
