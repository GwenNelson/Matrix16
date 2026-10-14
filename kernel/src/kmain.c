#include <stdint.h>
#include <stdbool.h>
#include "bioscons.h"
#include "biosdisk.h"
#include "isr.h"
#include "kpanic.h"
#include "kthread.h"
#include "matfs16.h"
#include "string.h"
#include "utils.h"

typedef struct rootfs_dir_t {
	uint16_t start_lba;
	uint16_t end_lba;
	uint16_t size;
	char name[26];
} __attribute__((packed)) rootfs_dir_t;

char buf[512];


void k_timer_callback(void) {
	kthread_schedule();
}

#define SYSTEM_CFG_TASK_COUNT 6
#define SYSTEM_CFG_MAX_FILE_SIZE 4095
#define SYSTEM_CFG_MAX_LINE_LENGTH 255

typedef enum system_cfg_source_t {
	SYSTEM_CFG_SOURCE_NONE,
	SYSTEM_CFG_SOURCE_SUPER_A,
	SYSTEM_CFG_SOURCE_SUPER_B,
	SYSTEM_CFG_SOURCE_FILE_A,
	SYSTEM_CFG_SOURCE_FILE_B
} system_cfg_source_t;

typedef enum system_cfg_console_t {
	SYSTEM_CFG_CONSOLE_VC0,
	SYSTEM_CFG_CONSOLE_VC1,
	SYSTEM_CFG_CONSOLE_VC2,
	SYSTEM_CFG_CONSOLE_VC3,
	SYSTEM_CFG_CONSOLE_COM1,
	SYSTEM_CFG_CONSOLE_COM2
} system_cfg_console_t;

typedef enum system_cfg_default_t {
	SYSTEM_CFG_DEFAULT_NONE,
	SYSTEM_CFG_DEFAULT_SHELL
} system_cfg_default_t;

typedef struct system_cfg_task_t {
	system_cfg_source_t source;
	system_cfg_console_t console;
	char filename[MATFS16_FILENAME_MAXLEN];
} system_cfg_task_t;

typedef struct system_cfg_t {
	char shell[MATFS16_FILENAME_MAXLEN];
	system_cfg_default_t default_action;
	system_cfg_task_t tasks[SYSTEM_CFG_TASK_COUNT];
} system_cfg_t;

system_cfg_t system_cfg;

static char* trim_system_cfg_field(char* field) {
	char* end;

	while(*field == ' ' || *field == '\t') field++;

	end = strchr(field, '\0');
	while(end != field && (end[-1] == ' ' || end[-1] == '\t')) end--;
	*end = '\0';

	return field;
}

static bool valid_system_cfg_filename(const char* filename) {
	uint16_t length;
	uint16_t i;

	length = strnlen(filename, MATFS16_FILENAME_MAXLEN);
	if(length == 0 || length == MATFS16_FILENAME_MAXLEN) return false;

	for(i = 0; i < length; i++) {
		uint8_t c = (uint8_t)filename[i];

		if(c <= ' ' || c > '~') return false;
		if(c == ',' || c == ';' || c == ':' || c == '"' || c == '\\') return false;
	}

	return true;
}

static void set_system_cfg_defaults(system_cfg_t* config) {
	uint16_t i;

	strcpy(config->shell, "SHELL.TSK");
	config->default_action = SYSTEM_CFG_DEFAULT_SHELL;

	for(i = 0; i < SYSTEM_CFG_TASK_COUNT; i++) {
		config->tasks[i].source = SYSTEM_CFG_SOURCE_NONE;
		config->tasks[i].console = (system_cfg_console_t)i;
		config->tasks[i].filename[0] = '\0';
	}

	for(i = 0; i < 4; i++) config->tasks[i].source = SYSTEM_CFG_SOURCE_SUPER_A;
}

static void copy_system_cfg(system_cfg_t* destination, system_cfg_t* source) {
	uint16_t i;

	strcpy(destination->shell, source->shell);
	destination->default_action = source->default_action;

	for(i = 0; i < SYSTEM_CFG_TASK_COUNT; i++) {
		destination->tasks[i].source = source->tasks[i].source;
		destination->tasks[i].console = source->tasks[i].console;
		strcpy(destination->tasks[i].filename, source->tasks[i].filename);
	}
}

static bool parse_system_cfg_console(char* value, system_cfg_console_t* console) {
	if(strcmp(value, "VC0") == 0) *console = SYSTEM_CFG_CONSOLE_VC0;
	else if(strcmp(value, "VC1") == 0) *console = SYSTEM_CFG_CONSOLE_VC1;
	else if(strcmp(value, "VC2") == 0) *console = SYSTEM_CFG_CONSOLE_VC2;
	else if(strcmp(value, "VC3") == 0) *console = SYSTEM_CFG_CONSOLE_VC3;
	else if(strcmp(value, "COM1") == 0) *console = SYSTEM_CFG_CONSOLE_COM1;
	else if(strcmp(value, "COM2") == 0) *console = SYSTEM_CFG_CONSOLE_COM2;
	else return false;

	return true;
}

static bool parse_system_cfg_source(char* value, uint16_t task_number, system_cfg_task_t* task) {
	if(strcmp(value, "NONE") == 0) {
		task->source = SYSTEM_CFG_SOURCE_NONE;
		task->filename[0] = '\0';
		return true;
	}

	if(strcmp(value, "SUPERA") == 0 || strcmp(value, "SUPERB") == 0) {
		if(task_number > 3) return false;
		task->source = value[5] == 'A' ? SYSTEM_CFG_SOURCE_SUPER_A : SYSTEM_CFG_SOURCE_SUPER_B;
		task->filename[0] = '\0';
		return true;
	}

	if((value[0] != 'A' && value[0] != 'B') || value[1] != ':') return false;
	if(!valid_system_cfg_filename(value + 2)) return false;

	task->source = value[0] == 'A' ? SYSTEM_CFG_SOURCE_FILE_A : SYSTEM_CFG_SOURCE_FILE_B;
	strcpy(task->filename, value + 2);
	return true;
}

static bool parse_system_cfg_line(char* line, system_cfg_t* config, bool* task_seen, bool* shell_seen, bool* default_seen) {
	char* comment;
	char* equals;
	char* key;
	char* value;
	char* comma;
	char* option_key;
	char* option_value;
	uint16_t task_number;

	comment = strchr(line, ';');
	if(comment != 0) *comment = '\0';

	line = trim_system_cfg_field(line);
	if(*line == '\0') return true;

	equals = strchr(line, '=');
	if(equals == 0) return false;
	*equals = '\0';
	key = trim_system_cfg_field(line);
	value = trim_system_cfg_field(equals + 1);
	if(*key == '\0' || *value == '\0') return false;

	if(strcmp(key, "SHELL") == 0) {
		if(*shell_seen || !valid_system_cfg_filename(value)) return false;
		strcpy(config->shell, value);
		*shell_seen = true;
		return true;
	}

	if(strcmp(key, "DEFAULT") == 0) {
		if(*default_seen) return false;
		if(strcmp(value, "SHELL") == 0) config->default_action = SYSTEM_CFG_DEFAULT_SHELL;
		else if(strcmp(value, "NONE") == 0) config->default_action = SYSTEM_CFG_DEFAULT_NONE;
		else return false;
		*default_seen = true;
		return true;
	}

	if(strncmp(key, "TASK", 4) != 0 || key[4] < '0' || key[4] > '5' || key[5] != '\0') return false;
	task_number = key[4] - '0';
	if(task_seen[task_number]) return false;

	comma = strchr(value, ',');
	if(comma != 0) {
		*comma = '\0';
		option_key = trim_system_cfg_field(comma + 1);
		if(strchr(option_key, ',') != 0) return false;

		equals = strchr(option_key, '=');
		if(equals == 0) return false;
		*equals = '\0';
		option_value = trim_system_cfg_field(equals + 1);
		option_key = trim_system_cfg_field(option_key);
		if(strcmp(option_key, "CONSOLE") != 0) return false;
		if(!parse_system_cfg_console(option_value, &config->tasks[task_number].console)) return false;
	}

	value = trim_system_cfg_field(value);
	if(!parse_system_cfg_source(value, task_number, &config->tasks[task_number])) return false;
	if(config->tasks[task_number].source == SYSTEM_CFG_SOURCE_NONE && comma != 0) return false;

	task_seen[task_number] = true;
	return true;
}

static bool validate_system_cfg_consoles(system_cfg_t* config) {
	bool console_used[SYSTEM_CFG_TASK_COUNT];
	uint16_t i;

	for(i = 0; i < SYSTEM_CFG_TASK_COUNT; i++) console_used[i] = false;

	for(i = 0; i < SYSTEM_CFG_TASK_COUNT; i++) {
		if(config->tasks[i].source == SYSTEM_CFG_SOURCE_NONE) continue;
		if(console_used[config->tasks[i].console]) return false;
		console_used[config->tasks[i].console] = true;
	}

	return true;
}

void load_system_cfg(void) {
	system_cfg_t parsed_config;
	bool task_seen[SYSTEM_CFG_TASK_COUNT];
	bool shell_seen = false;
	bool default_seen = false;
	uint8_t block[MATFS16_BLOCK_SIZE];
	char line[SYSTEM_CFG_MAX_LINE_LENGTH + 1];
	matfs16_status_t status;
	uint16_t lba;
	uint16_t next_lba;
	uint16_t file_length;
	uint16_t remaining;
	uint16_t block_length;
	uint16_t line_length = 0;
	uint16_t i;

	set_system_cfg_defaults(&system_cfg);
	set_system_cfg_defaults(&parsed_config);
	for(i = 0; i < SYSTEM_CFG_TASK_COUNT; i++) task_seen[i] = false;

	status = matfs16_locate_file("SYSTEM.CFG", 0, &lba, &file_length);
	if(status == MATFS16_ERR_NOT_FOUND) return;
	if(status != MATFS16_OK) goto use_defaults;
	if(file_length == MATFS16_FILE_SIZE_UNKNOWN || file_length > SYSTEM_CFG_MAX_FILE_SIZE) goto invalid_config;

	remaining = file_length;
	while(remaining != 0) {
		status = matfs16_load_block(0, lba, 0x1000, (uint16_t)block);
		if(status != MATFS16_OK) goto use_defaults;

		block_length = remaining < MATFS16_BLOCK_SIZE ? remaining : MATFS16_BLOCK_SIZE;
		for(i = 0; i < block_length; i++) {
			uint8_t c = block[i];

			if(c == '\r' || c == '\0' || c == 0x1A) goto invalid_config;
			if(c == '\n') {
				line[line_length] = '\0';
				if(!parse_system_cfg_line(line, &parsed_config, task_seen, &shell_seen, &default_seen)) goto invalid_config;
				line_length = 0;
				continue;
			}

			if(line_length == SYSTEM_CFG_MAX_LINE_LENGTH) goto invalid_config;
			line[line_length++] = (char)c;
		}

		if(remaining <= MATFS16_BLOCK_SIZE) break;
		remaining -= MATFS16_BLOCK_SIZE;

		status = matfs16_next_block(0, lba, &next_lba);
		if(status != MATFS16_OK) goto use_defaults;
		lba = next_lba;
	}

	if(line_length != 0) {
		line[line_length] = '\0';
		if(!parse_system_cfg_line(line, &parsed_config, task_seen, &shell_seen, &default_seen)) goto invalid_config;
	}

	if(!validate_system_cfg_consoles(&parsed_config)) goto invalid_config;
	copy_system_cfg(&system_cfg, &parsed_config);
	return;

invalid_config:
	bios_puts("Invalid SYSTEM.CFG, using defaults....\n");
	return;

use_defaults:
	bios_puts("Could not read SYSTEM.CFG, using defaults....\n");
}



// the shell is configured to run at 2000:0000 - that is, the very first task after the kernel itself
void load_default_shell() {
	matfs16_status_t status;
	uint16_t lba;
	uint16_t next_lba;
	uint16_t offset = 0;
	uint16_t shell_len;

	status = matfs16_locate_file(system_cfg.shell, 0, &lba, &shell_len);
	if(status != MATFS16_OK || shell_len == 0) kpanic("MISSING SHELL!");

	for(;;) {
		status = matfs16_load_block(0, lba, 0x2000, offset);
		if(status != MATFS16_OK) kpanic("FAILED TO LOAD SHELL!");

		status = matfs16_next_block(0, lba, &next_lba);
		if(status == MATFS16_END_OF_FILE) break;
		if(status != MATFS16_OK) kpanic("FAILED TO LOAD SHELL!");
		if(offset == 0xFE00) kpanic("SHELL IS TOO LARGE!");

		lba = next_lba;
		offset += MATFS16_BLOCK_SIZE;
	}

	// if we get here, yay! it should be possible to just JMP to it
	__asm__ volatile (
	    "ljmp $0x2000, $0x0000"
	);	
}

void dump_dir(void) {
	matfs_directory_entry_t entry;
	matfs16_status_t status;
	uint16_t index = 0;

	bios_puts("Directory listing for A:\n");
	for(;;) {
		status = matfs16_read_direntry(0, index, &entry);
		if(status == MATFS16_END_OF_DIRECTORY) break;

		if(status != MATFS16_OK) {
			bios_puts("MATFS16 directory error: ");
			bios_put_decimal((uint16_t)status);
			bios_puts("\n");
			return;
		}

		bios_puts("\t");
		bios_puts(entry.name);
		bios_puts("\n");
		index++;
	}
}

void kernel_main(void) {
	bios_puts("Matrix16 Kernel loaded!\n\n");

	install_isr08();
	install_isr09();
	install_isr80();

	// gonna test matfs16 here, then come back to the other stuff
	dump_dir();


/*	bios_puts("Starting up kthread...\n");
	kthread_init();
	bios_puts("Should be ready to start schedule...\n");
	kthread_start();
	kpanic("Something went wrong!");*/

	bios_puts("Trying SYSTEM.CFG...\n");
	load_system_cfg();

	bios_puts("Starting configured shell A:");
	bios_puts(system_cfg.shell);
	bios_puts("...\n");

	load_default_shell();

	for(;;);
}
