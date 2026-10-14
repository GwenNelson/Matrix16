#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "matfs16.h"

#define SYSTEM_CFG_TASK_COUNT 6
#define SYSTEM_CFG_DEFAULT_START_DELAY 55

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
	uint16_t start_delay;
	system_cfg_task_t tasks[SYSTEM_CFG_TASK_COUNT];
} system_cfg_t;

extern system_cfg_t system_cfg;
extern bool cfg_errors;

void reset_system_cfg_defaults(void);
void load_system_cfg(void);
