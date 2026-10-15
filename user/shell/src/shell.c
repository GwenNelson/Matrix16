#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include <syscalls.h>

#include "shell_command.h"

extern uint8_t __shell_command_line_start[];
extern uint8_t __shell_environment_start[];
extern uint8_t __shell_environment_end[];

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

static int environment_name_length(const char* name, uint16_t* length) {
	uint16_t capacity = (uint16_t)(__shell_environment_end - __shell_environment_start);
	uint16_t i;

	if(name == 0 || name[0] == '\0') return -1;

	for(i = 0; i < capacity; i++) {
		if(name[i] == '=') return -1;
		if(name[i] == '\0') {
			*length = i;
			return 0;
		}
	}

	return -1;
}

/*
 * Find name in the packed NAME=VALUE\0...\0 environment. On success,
 * entry receives the matching entry or NULL, and end receives the final NUL.
 */
static int environment_find(const char* name, uint16_t name_length, uint8_t** entry, uint8_t** end) {
	uint8_t* current = __shell_environment_start;
	uint8_t* match = 0;

	while(current < __shell_environment_end) {
		uint16_t remaining = (uint16_t)(__shell_environment_end - current);
		uint16_t entry_length;
		uint16_t entry_name_length = 0;

		if(*current == '\0') {
			*entry = match;
			*end = current;
			return match == 0 ? 0 : 1;
		}

		entry_length = strnlen((const char*)current, remaining);
		if(entry_length == remaining) return -1;

		while(entry_name_length < entry_length && current[entry_name_length] != '=') entry_name_length++;
		if(entry_name_length == entry_length) return -1;

		if(entry_name_length == name_length && strncmp((const char*)current, name, name_length) == 0) {
			match = current;
		}

		current += entry_length + 1;
	}

	return -1;
}

char* getenv(const char* name) {
	uint8_t* entry;
	uint8_t* end;
	uint16_t name_length;
	uint16_t i;

	if(environment_name_length(name, &name_length) != 0) return 0;
	if(environment_find(name, name_length, &entry, &end) != 1) return 0;

	for(i = 0; entry[i] != '='; i++)
		;
	return (char*)(entry + i + 1);
}

/* Return zero on success, or -1 for invalid input, malformed data, or no space. */
int setenv(const char* name, const char* value) {
	uint8_t* entry;
	uint8_t* end;
	uint8_t* tail;
	uint16_t capacity = (uint16_t)(__shell_environment_end - __shell_environment_start);
	uint16_t name_length;
	uint16_t value_length;
	uint16_t entry_length;
	uint16_t old_size = 0;
	uint16_t new_size;
	uint16_t used_size;
	uint16_t tail_size;
	uint16_t i;
	int found;

	if(value == 0) return -1;
	if(environment_name_length(name, &name_length) != 0) return -1;

	value_length = strnlen(value, capacity);
	if(value_length == capacity) return -1;
	new_size = name_length + 1 + value_length + 1;

	found = environment_find(name, name_length, &entry, &end);
	if(found < 0) return -1;

	if(found != 0) {
		entry_length = strnlen((const char*)entry, (uint16_t)(__shell_environment_end - entry));
		old_size = entry_length + 1;
		tail = entry + old_size;
	} else {
		entry = end;
		tail = end;
	}

	used_size = (uint16_t)(end - __shell_environment_start) + 1;
	if(new_size > capacity - (used_size - old_size)) return -1;

	tail_size = (uint16_t)(end - tail) + 1;
	if(new_size > old_size) {
		for(i = tail_size; i != 0; i--) entry[new_size + i - 1] = tail[i - 1];
	} else if(new_size < old_size) {
		for(i = 0; i < tail_size; i++) entry[new_size + i] = tail[i];
	}

	for(i = 0; i < name_length; i++) entry[i] = name[i];
	entry[name_length] = '=';
	for(i = 0; i < value_length; i++) entry[name_length + 1 + i] = value[i];
	entry[new_size - 1] = '\0';
	return 0;
}

void shell_mainloop() {
	char line[LINE_LENGTH];
	char command_path[28];
	char* command_name;
	char* cmd_params;
	char* cwd;
	char command_drive[2];
	uint16_t command_length;
	uint16_t file_length;

	for(;;) {
		cwd = getenv("CWD");
		if(cwd == NULL || (strcmp(cwd, "A:") != 0 && strcmp(cwd, "B:") != 0)) {
			printf("Invalid directory; resetting to A:\n");
			if(setenv("CWD", "A:") == 0) cwd = getenv("CWD");
			if(cwd == NULL || (strcmp(cwd, "A:") != 0 && strcmp(cwd, "B:") != 0)) cwd = "A:";
		}
		printf("%s>",cwd);
		readline(line,sizeof(line));

		__shell_command_line_start[0] = '\0';
		uppercase(line);
		if(strcmp(line, "A:") == 0 || strcmp(line, "B:") == 0) {
			setenv("CWD", line);
			continue;
		}

		cmd_params = strchr(line, ' ');
		if(cmd_params != NULL) {
			*cmd_params++ = '\0';
			while(*cmd_params == ' ') cmd_params++;
			memcpy(__shell_command_line_start, cmd_params, strnlen(cmd_params, LINE_LENGTH) + 1);
		}

		command_drive[0] = cwd[0];
		command_drive[1] = ':';
		command_name = line;
		if(line[1] == ':' && (line[0] == 'A' || line[0] == 'B')) {
			command_drive[0] = line[0];
			command_name = line + 2;
		} else if(strchr(line, ':') != NULL) {
			printf("Invalid command path %s\n", line);
			continue;
		}

		command_length = strnlen(command_name, 22);
		if(command_length == 0 || command_length > 21) {
			printf("Invalid command name %s\n",line);
			continue;
		}

		command_path[0] = command_drive[0];
		command_path[1] = command_drive[1];
		memcpy(command_path + 2, command_name, command_length);
		memcpy(command_path + 2 + command_length,".CMD",sizeof(".CMD"));

		if(sys_load(command_path,(void*)0x8000,&file_length) != 0) {
			printf("Unable to load %s\n",command_path);
			continue;
		}

		shell_run_command();
	}

}

// setup default environment
void setup_env(void) {
	setenv("CWD","A:");
}

int main(void) {
	setup_env();
	uint16_t console_num;

	console_num = sys_get_console();
	printf("Welcome to Matrix16\n");
	printf("You are on console %u\n",console_num);

	shell_mainloop();
}
