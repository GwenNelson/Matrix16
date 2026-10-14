#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include <syscalls.h>

#include "shell_command.h"

extern uint8_t __shell_command_line_start[];

#define LINE_LENGTH 80

static uint16_t readline(char* line, uint16_t capacity) {
	uint16_t length = 0;
	uint8_t escape_state = 0;
	char c;

	if(capacity == 0) return 0;

	for(;;) {
		if(sys_read(0,&c,1) != 1) continue;

		if(escape_state == 1) {
			escape_state = c == '[' ? 2 : 0;
			continue;
		}

		if(escape_state == 2) {
			if((uint8_t)c >= 0x40 && (uint8_t)c <= 0x7E) escape_state = 0;
			continue;
		}

		if((uint8_t)c == 0x1B) {
			escape_state = 1;
			continue;
		}

		if(c == '\r') continue;
		if(c == '\n') {
			line[length] = '\0';
			sys_write(1,"\r\n",2);
			return length;
		}

		if(c == '\b') {
			if(length != 0) {
				length--;
				sys_write(1,"\b \b",3);
			}
			continue;
		}

		if((uint8_t)c < 0x20 || (uint8_t)c == 0x7F) continue;
		if(length == capacity - 1) continue;

		line[length++] = c;
		sys_write(1,&c,1);
	}
}

static void uppercase(char* string) {
	while(*string != '\0') {
		if(*string >= 'a' && *string <= 'z') *string -= 'a' - 'A';
		string++;
	}
}

void shell_mainloop() {
	char line[LINE_LENGTH];
	char command_path[28];
	char* cmd_params;
	uint16_t cmd_params_len;
	uint16_t command_length;
	uint16_t file_length;

	// this is a seriously dumb hack
	for(;;) {
		printf("A:>");
		readline(line,sizeof(line));

		__shell_command_line_start[0] = '\0';
		uppercase(line);
		cmd_params = strchr(line,' ');
		if(cmd_params != NULL) {
			*cmd_params++ = '\0';
			while(*cmd_params == ' ') cmd_params++;
			cmd_params_len = strnlen(cmd_params,LINE_LENGTH);
			memcpy(__shell_command_line_start,cmd_params,cmd_params_len+1);
		} else {
			cmd_params_len = 0;
		}

		command_length = strnlen(line,22);
		if(command_length == 0 || command_length > 21) {
			printf("Invalid command name %s\n",line);
			continue;
		}
		command_path[0] = 'A';
		command_path[1] = ':';
		memcpy(command_path + 2,line,command_length);
		memcpy(command_path + 2 + command_length,".CMD",sizeof(".CMD"));

		if(sys_load(command_path,(void*)0x8000,&file_length) != 0) {
			printf("Unable to load %s\n",command_path);
			continue;
		}

		shell_run_command();
	}

}

int main(void) {

	uint16_t console_num;

	console_num = sys_get_console();
	printf("Welcome to Matrix16\n");
	printf("You are on console %u\n",console_num);

	shell_mainloop();
}
