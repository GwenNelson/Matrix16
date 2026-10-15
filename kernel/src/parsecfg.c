#include "parsecfg.h"
#include "bioscons.h"
#include "kfiles.h"
#include "string.h"

#include <stdbool.h>
#include <stdint.h>

#define SYSTEM_CFG_MAX_FILE_SIZE 4095
#define SYSTEM_CFG_MAX_LINE_LENGTH 63

#define SYSTEM_CFG_SEEN_SHELL       0x0040
#define SYSTEM_CFG_SEEN_DEFAULT     0x0080
#define SYSTEM_CFG_SEEN_START_DELAY 0x0100
#define SYSTEM_CFG_SEEN_FILES       0x0200
#define SYSTEM_CFG_SEEN_ERROR       0x0400

system_cfg_t system_cfg;
bool cfg_errors;

static void set_system_cfg_defaults(system_cfg_t* config);

static void report_system_cfg_error(uint16_t line_number, char* message) {
	set_system_cfg_defaults(&system_cfg);
	cfg_errors = true;

	bios_puts("SYSTEM.CFG");
	if(line_number != 0) {
		bios_puts(" line ");
		bios_put_decimal(line_number);
	}
	bios_puts(": ");
	bios_puts(message);
	bios_puts("; using defaults.\n");
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
	config->error_filename[0] = '\0';
	config->default_action = SYSTEM_CFG_DEFAULT_SHELL;
	config->error_action = SYSTEM_CFG_ERROR_DEFAULT;
	config->start_delay = SYSTEM_CFG_DEFAULT_START_DELAY;
	config->files = SYSTEM_CFG_DEFAULT_FILES;

	for(i = 0; i < SYSTEM_CFG_TASK_COUNT; i++) {
		config->tasks[i].source = i < 4 ? SYSTEM_CFG_SOURCE_SUPER_A : SYSTEM_CFG_SOURCE_NONE;
		config->tasks[i].console = (system_cfg_console_t)i;
		config->tasks[i].filename[0] = '\0';
	}
}

void reset_system_cfg_defaults(void) {
	set_system_cfg_defaults(&system_cfg);
	cfg_errors = false;
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

	if((value[0] == 'A' || value[0] == 'B') && value[1] == ':') {
		if(!valid_system_cfg_filename(value + 2)) return false;
		task->source = value[0] == 'A' ? SYSTEM_CFG_SOURCE_FILE_A : SYSTEM_CFG_SOURCE_FILE_B;
		strcpy(task->filename, value + 2);
		return true;
	}

	if(!valid_system_cfg_filename(value)) return false;
	task->source = SYSTEM_CFG_SOURCE_FILE_A;
	strcpy(task->filename, value);
	return true;
}

static char* parse_system_cfg_line(char* line, system_cfg_t* config, uint16_t* seen) {
	char* equals;
	char* key;
	char* value;
	char* comma;
	char* option_key;
	char* option_value;
	uint16_t task_number;
	uint16_t seen_bit;

	if(*line == '\0') return 0;

	equals = strchr(line, '=');
	if(equals == 0) return "invalid statement";
	*equals = '\0';
	key = line;
	value = equals + 1;
	if(*key == '\0' || *value == '\0') return "invalid statement";

	if(strcmp(key, "SHELL") == 0) {
		if(*seen & SYSTEM_CFG_SEEN_SHELL) return "duplicate statement";
		if(!valid_system_cfg_filename(value)) return "invalid shell filename";
		strcpy(config->shell, value);
		*seen |= SYSTEM_CFG_SEEN_SHELL;
		return 0;
	}

	if(strcmp(key, "DEFAULT") == 0) {
		if(*seen & SYSTEM_CFG_SEEN_DEFAULT) return "duplicate statement";
		if(strcmp(value, "SHELL") == 0) config->default_action = SYSTEM_CFG_DEFAULT_SHELL;
		else if(strcmp(value, "NONE") == 0) config->default_action = SYSTEM_CFG_DEFAULT_NONE;
		else return "invalid statement";
		*seen |= SYSTEM_CFG_SEEN_DEFAULT;
		return 0;
	}

	if(strcmp(key, "ERROR") == 0) {
		if(*seen & SYSTEM_CFG_SEEN_ERROR) return "duplicate statement";
		if(strcmp(value, "DEFAULT") == 0) config->error_action = SYSTEM_CFG_ERROR_DEFAULT;
		else if(strcmp(value, "REPORT") == 0) config->error_action = SYSTEM_CFG_ERROR_REPORT;
		else if(strcmp(value, "NONE") == 0) config->error_action = SYSTEM_CFG_ERROR_NONE;
		else if(strcmp(value, "SHELL") == 0) config->error_action = SYSTEM_CFG_ERROR_SHELL;
		else {
			if((value[0] == 'A' || value[0] == 'B') && value[1] == ':') {
				if(!valid_system_cfg_filename(value + 2)) return "invalid error filename";
				config->error_action = value[0] == 'A' ? SYSTEM_CFG_ERROR_FILE_A : SYSTEM_CFG_ERROR_FILE_B;
				strcpy(config->error_filename, value + 2);
			} else {
				if(!valid_system_cfg_filename(value)) return "invalid error filename";
				config->error_action = SYSTEM_CFG_ERROR_FILE_A;
				strcpy(config->error_filename, value);
			}
		}
		*seen |= SYSTEM_CFG_SEEN_ERROR;
		return 0;
	}

	if(strcmp(key, "STARTDELAY") == 0) {
		if(*seen & SYSTEM_CFG_SEEN_START_DELAY) return "duplicate statement";
		if(!parse_system_cfg_uint16(value, &config->start_delay)) return "invalid statement";
		*seen |= SYSTEM_CFG_SEEN_START_DELAY;
		return 0;
	}

	if(strcmp(key, "FILES") == 0) {
		if(*seen & SYSTEM_CFG_SEEN_FILES) return "duplicate statement";
		if(!parse_system_cfg_uint16(value, &config->files)) return "invalid statement";
		if(config->files > SYSTEM_CFG_MAX_FILES) return "invalid statement";
		if(config->files > kfiles_max_descriptors()) return "FILES exceeds kernel heap capacity";
		*seen |= SYSTEM_CFG_SEEN_FILES;
		return 0;
	}

	if(strncmp(key, "TASK", 4) != 0 || key[4] < '0' || key[4] > '4' || key[5] != '\0') return "unknown statement";
	task_number = key[4] - '0';
	seen_bit = (uint16_t)1 << task_number;
	if(*seen & seen_bit) return "duplicate statement";

	comma = strchr(value, ',');
	if(comma != 0) {
		*comma = '\0';
		option_key = comma + 1;
		if(strchr(option_key, ',') != 0) return "invalid statement";

		equals = strchr(option_key, '=');
		if(equals == 0) return "invalid statement";
		*equals = '\0';
		option_value = equals + 1;
		if(strcmp(option_key, "CONSOLE") != 0) return "invalid statement";
		if(!parse_system_cfg_console(option_value, &config->tasks[task_number].console)) return "invalid statement";
	}

	if(*value == '\0') return "task has no source";
	if(!parse_system_cfg_source(value, task_number, &config->tasks[task_number])) return "invalid task source";
	if(config->tasks[task_number].source == SYSTEM_CFG_SOURCE_NONE && comma != 0) return "invalid statement";

	*seen |= seen_bit;
	return 0;
}

static bool validate_system_cfg_consoles(system_cfg_t* config) {
	bool console_used[SYSTEM_CFG_CONSOLE_COUNT];
	uint16_t i;

	for(i = 0; i < SYSTEM_CFG_CONSOLE_COUNT; i++) console_used[i] = false;

	for(i = 0; i < SYSTEM_CFG_TASK_COUNT; i++) {
		if(config->tasks[i].source == SYSTEM_CFG_SOURCE_NONE) continue;
		if(config->tasks[i].source == SYSTEM_CFG_SOURCE_DEFAULT && config->default_action == SYSTEM_CFG_DEFAULT_NONE) continue;
		if(console_used[config->tasks[i].console]) return false;
		console_used[config->tasks[i].console] = true;
	}

	return true;
}

void load_system_cfg(void) {
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
	uint16_t seen = 0;
	uint16_t i;
	char* parse_error;
	bool in_comment = false;

	reset_system_cfg_defaults();

	status = matfs16_locate_file("SYSTEM.CFG", 0, &lba, &file_length);
	if(status == MATFS16_ERR_NOT_FOUND) return;
	if(status != MATFS16_OK) {
		report_system_cfg_error(0, "disk error");
		return;
	}
	if(file_length == MATFS16_FILE_SIZE_UNKNOWN) {
		report_system_cfg_error(0, "invalid file");
		return;
	}
	if(file_length > SYSTEM_CFG_MAX_FILE_SIZE) {
		report_system_cfg_error(0, "invalid file");
		return;
	}

	remaining = file_length;
	while(remaining != 0) {
		status = matfs16_load_block(0, lba, 0x1000, (uint16_t)block);
		if(status != MATFS16_OK) {
			report_system_cfg_error(0, "disk error");
			return;
		}

		block_length = remaining < MATFS16_BLOCK_SIZE ? remaining : MATFS16_BLOCK_SIZE;
		for(i = 0; i < block_length; i++) {
			uint8_t c = block[i];

			if(c == '\n') {
				line[line_length] = '\0';
				parse_error = parse_system_cfg_line(line, &system_cfg, &seen);
				if(parse_error != 0) {
					report_system_cfg_error(line_number, parse_error);
					return;
				}
				line_length = 0;
				line_number++;
				in_comment = false;
				continue;
			}
			if(in_comment) continue;
			if(c == ';') {
				in_comment = true;
				continue;
			}
			if(c == ' ' || c == '\t') continue;
			if(c == '\r' || c == '\0' || c == 0x1A) {
				report_system_cfg_error(line_number, "invalid character");
				return;
			}

			if(line_length == SYSTEM_CFG_MAX_LINE_LENGTH) {
				report_system_cfg_error(line_number, "line too long");
				return;
			}
			line[line_length++] = (char)c;
		}

		if(remaining <= MATFS16_BLOCK_SIZE) break;
		remaining -= MATFS16_BLOCK_SIZE;

		status = matfs16_next_block(0, lba, &next_lba);
		if(status != MATFS16_OK) {
			report_system_cfg_error(0, "disk error");
			return;
		}
		lba = next_lba;
	}

	if(line_length != 0) {
		line[line_length] = '\0';
		parse_error = parse_system_cfg_line(line, &system_cfg, &seen);
		if(parse_error != 0) {
			report_system_cfg_error(line_number, parse_error);
			return;
		}
	}

	if(!validate_system_cfg_consoles(&system_cfg)) {
		report_system_cfg_error(0, "conflicting consoles");
		return;
	}
}
