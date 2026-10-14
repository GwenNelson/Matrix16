#include "parsecfg.h"
#include "bioscons.h"
#include "string.h"

#include <stdbool.h>
#include <stdint.h>

#define SYSTEM_CFG_MAX_FILE_SIZE 4095
#define SYSTEM_CFG_MAX_LINE_LENGTH 255

system_cfg_t system_cfg;
bool cfg_errors;

static void report_system_cfg_error(char* message) {
	cfg_errors = true;
	bios_puts("SYSTEM.CFG: ");
	bios_puts(message);
	bios_puts("\nUsing internal SYSTEM.CFG defaults.\n");
}

static void report_system_cfg_line_error(uint16_t line_number, char* message) {
	cfg_errors = true;
	bios_puts("SYSTEM.CFG line ");
	bios_put_decimal(line_number);
	bios_puts(": ");
	bios_puts(message);
	bios_puts("\nUsing internal SYSTEM.CFG defaults.\n");
}

static void report_system_cfg_disk_error(char* operation, matfs16_status_t status) {
	cfg_errors = true;
	bios_puts("SYSTEM.CFG: ");
	bios_puts(operation);
	bios_puts(" (MATFS16 error ");
	bios_put_decimal((uint16_t)status);
	bios_puts(")\nUsing internal SYSTEM.CFG defaults.\n");
}

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

static bool parse_system_cfg_uint16(char* value, uint16_t* result) {
	uint16_t number = 0;

	if(*value == '\0') return false;

	while(*value != '\0') {
		uint8_t digit;

		if(*value < '0' || *value > '9') return false;
		digit = (uint8_t)(*value - '0');
		if(number > 6553 || (number == 6553 && digit > 5)) return false;

		number = number * 10 + digit;
		value++;
	}

	*result = number;
	return true;
}

static void set_system_cfg_defaults(system_cfg_t* config) {
	uint16_t i;

	strcpy(config->shell, "SHELL.TSK");
	config->default_action = SYSTEM_CFG_DEFAULT_SHELL;
	config->start_delay = SYSTEM_CFG_DEFAULT_START_DELAY;

	for(i = 0; i < SYSTEM_CFG_TASK_COUNT; i++) {
		config->tasks[i].source = SYSTEM_CFG_SOURCE_NONE;
		config->tasks[i].console = (system_cfg_console_t)i;
		config->tasks[i].filename[0] = '\0';
	}

	for(i = 0; i < 4; i++) config->tasks[i].source = SYSTEM_CFG_SOURCE_SUPER_A;
}

void reset_system_cfg_defaults(void) {
	set_system_cfg_defaults(&system_cfg);
	cfg_errors = false;
}

static void copy_system_cfg(system_cfg_t* destination, system_cfg_t* source) {
	uint16_t i;

	strcpy(destination->shell, source->shell);
	destination->default_action = source->default_action;
	destination->start_delay = source->start_delay;

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

static char* parse_system_cfg_source(char* value, uint16_t task_number, system_cfg_task_t* task) {
	if(strcmp(value, "NONE") == 0) {
		task->source = SYSTEM_CFG_SOURCE_NONE;
		task->filename[0] = '\0';
		return 0;
	}

	if(strcmp(value, "SUPERA") == 0 || strcmp(value, "SUPERB") == 0) {
		if(task_number > 3) return "SUPERA and SUPERB are invalid for this task slot";
		task->source = value[5] == 'A' ? SYSTEM_CFG_SOURCE_SUPER_A : SYSTEM_CFG_SOURCE_SUPER_B;
		task->filename[0] = '\0';
		return 0;
	}

	if((value[0] != 'A' && value[0] != 'B') || value[1] != ':') return "invalid task source";
	if(!valid_system_cfg_filename(value + 2)) return "invalid task filename";

	task->source = value[0] == 'A' ? SYSTEM_CFG_SOURCE_FILE_A : SYSTEM_CFG_SOURCE_FILE_B;
	strcpy(task->filename, value + 2);
	return 0;
}

static char* parse_system_cfg_line(char* line, system_cfg_t* config, bool* task_seen, bool* shell_seen, bool* default_seen, bool* start_delay_seen) {
	char* comment;
	char* equals;
	char* key;
	char* value;
	char* comma;
	char* option_key;
	char* option_value;
	char* source_error;
	uint16_t task_number;

	comment = strchr(line, ';');
	if(comment != 0) *comment = '\0';

	line = trim_system_cfg_field(line);
	if(*line == '\0') return 0;

	equals = strchr(line, '=');
	if(equals == 0) return "statement is missing '='";
	*equals = '\0';
	key = trim_system_cfg_field(line);
	value = trim_system_cfg_field(equals + 1);
	if(*key == '\0') return "statement has no keyword";
	if(*value == '\0') return "statement has no value";

	if(strcmp(key, "SHELL") == 0) {
		if(*shell_seen) return "SHELL is defined more than once";
		if(!valid_system_cfg_filename(value)) return "invalid shell filename";
		strcpy(config->shell, value);
		*shell_seen = true;
		return 0;
	}

	if(strcmp(key, "DEFAULT") == 0) {
		if(*default_seen) return "DEFAULT is defined more than once";
		if(strcmp(value, "SHELL") == 0) config->default_action = SYSTEM_CFG_DEFAULT_SHELL;
		else if(strcmp(value, "NONE") == 0) config->default_action = SYSTEM_CFG_DEFAULT_NONE;
		else return "DEFAULT must be SHELL or NONE";
		*default_seen = true;
		return 0;
	}

	if(strcmp(key, "STARTDELAY") == 0) {
		if(*start_delay_seen) return "STARTDELAY is defined more than once";
		if(!parse_system_cfg_uint16(value, &config->start_delay)) return "STARTDELAY must be a decimal number from 0 to 65535";
		*start_delay_seen = true;
		return 0;
	}

	if(strncmp(key, "TASK", 4) != 0 || key[4] < '0' || key[4] > '5' || key[5] != '\0') return "unknown statement";
	task_number = key[4] - '0';
	if(task_seen[task_number]) return "task is defined more than once";

	comma = strchr(value, ',');
	if(comma != 0) {
		*comma = '\0';
		option_key = trim_system_cfg_field(comma + 1);
		if(strchr(option_key, ',') != 0) return "task contains too many fields";

		equals = strchr(option_key, '=');
		if(equals == 0) return "invalid task option";
		*equals = '\0';
		option_value = trim_system_cfg_field(equals + 1);
		option_key = trim_system_cfg_field(option_key);
		if(strcmp(option_key, "CONSOLE") != 0) return "unknown task option";
		if(!parse_system_cfg_console(option_value, &config->tasks[task_number].console)) return "invalid console";
	}

	value = trim_system_cfg_field(value);
	if(*value == '\0') return "task has no source";
	source_error = parse_system_cfg_source(value, task_number, &config->tasks[task_number]);
	if(source_error != 0) return source_error;
	if(config->tasks[task_number].source == SYSTEM_CFG_SOURCE_NONE && comma != 0) return "NONE cannot have a console";

	task_seen[task_number] = true;
	return 0;
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
	bool start_delay_seen = false;
	uint8_t block[MATFS16_BLOCK_SIZE];
	char line[SYSTEM_CFG_MAX_LINE_LENGTH + 1];
	matfs16_status_t status;
	uint16_t lba;
	uint16_t next_lba;
	uint16_t file_length;
	uint16_t remaining;
	uint16_t block_length;
	uint16_t line_length = 0;
	uint16_t line_number = 1;
	uint16_t i;
	char* parse_error;

	cfg_errors = false;
	set_system_cfg_defaults(&system_cfg);
	set_system_cfg_defaults(&parsed_config);
	for(i = 0; i < SYSTEM_CFG_TASK_COUNT; i++) task_seen[i] = false;

	status = matfs16_locate_file("SYSTEM.CFG", 0, &lba, &file_length);
	if(status == MATFS16_ERR_NOT_FOUND) return;
	if(status != MATFS16_OK) {
		report_system_cfg_disk_error("could not locate file", status);
		return;
	}
	if(file_length == MATFS16_FILE_SIZE_UNKNOWN) {
		report_system_cfg_error("file has no exact logical size");
		return;
	}
	if(file_length > SYSTEM_CFG_MAX_FILE_SIZE) {
		report_system_cfg_error("file exceeds 4095 bytes");
		return;
	}

	remaining = file_length;
	while(remaining != 0) {
		status = matfs16_load_block(0, lba, 0x1000, (uint16_t)block);
		if(status != MATFS16_OK) {
			report_system_cfg_disk_error("could not read file", status);
			return;
		}

		block_length = remaining < MATFS16_BLOCK_SIZE ? remaining : MATFS16_BLOCK_SIZE;
		for(i = 0; i < block_length; i++) {
			uint8_t c = block[i];

			if(c == '\r') {
				report_system_cfg_line_error(line_number, "carriage returns are not permitted");
				return;
			}
			if(c == '\0') {
				report_system_cfg_line_error(line_number, "NUL bytes are not permitted");
				return;
			}
			if(c == 0x1A) {
				report_system_cfg_line_error(line_number, "DOS end-of-file bytes are not permitted");
				return;
			}
			if(c == '\n') {
				line[line_length] = '\0';
				parse_error = parse_system_cfg_line(line, &parsed_config, task_seen, &shell_seen, &default_seen, &start_delay_seen);
				if(parse_error != 0) {
					report_system_cfg_line_error(line_number, parse_error);
					return;
				}
				line_length = 0;
				line_number++;
				continue;
			}

			if(line_length == SYSTEM_CFG_MAX_LINE_LENGTH) {
				report_system_cfg_line_error(line_number, "line exceeds 255 bytes");
				return;
			}
			line[line_length++] = (char)c;
		}

		if(remaining <= MATFS16_BLOCK_SIZE) break;
		remaining -= MATFS16_BLOCK_SIZE;

		status = matfs16_next_block(0, lba, &next_lba);
		if(status != MATFS16_OK) {
			report_system_cfg_disk_error("could not follow file block chain", status);
			return;
		}
		lba = next_lba;
	}

	if(line_length != 0) {
		line[line_length] = '\0';
		parse_error = parse_system_cfg_line(line, &parsed_config, task_seen, &shell_seen, &default_seen, &start_delay_seen);
		if(parse_error != 0) {
			report_system_cfg_line_error(line_number, parse_error);
			return;
		}
	}

	if(!validate_system_cfg_consoles(&parsed_config)) {
		report_system_cfg_error("more than one task uses the same console");
		return;
	}
	copy_system_cfg(&system_cfg, &parsed_config);
}
