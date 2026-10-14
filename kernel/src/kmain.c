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

char system_cfg_buf[1024]; // yes, this will crash horribly if SYSTEM.CFG is too big, i know
uint16_t system_cfg_len;

char system_cfg_shell[26];
uint16_t system_cfg_consoles = 4;

void load_system_cfg(void) {
	matfs16_status_t status;
	uint16_t lba;
	uint16_t next_lba;
	uint16_t offset = 0;
	uint16_t remaining;

	status = matfs16_locate_file("SYSTEM.CFG", 0, &lba, &system_cfg_len);
	if(status != MATFS16_OK) goto use_defaults;
	if(system_cfg_len == MATFS16_FILE_SIZE_UNKNOWN || system_cfg_len >= sizeof(system_cfg_buf)) goto use_defaults;

	remaining = system_cfg_len;
	while(remaining != 0) {
		status = matfs16_load_block(0, lba, 0x1000, (uint16_t)system_cfg_buf + offset);
		if(status != MATFS16_OK) goto use_defaults;
		if(remaining <= MATFS16_BLOCK_SIZE) break;

		status = matfs16_next_block(0, lba, &next_lba);
		if(status != MATFS16_OK) goto use_defaults;

		lba = next_lba;
		offset += MATFS16_BLOCK_SIZE;
		remaining -= MATFS16_BLOCK_SIZE;
	}

	system_cfg_buf[system_cfg_len] = '\0';
	char *line = system_cfg_buf;
	while(*line != '\0') {
		char *next = strchr(line, '\n');
		if(next != 0) {
			*next = '\0';
			next++;
		} else {
			next = strchr(line, '\0');
		}

		char *cr = strchr(line, '\r');
		if(cr != 0)
			*cr = '\0';

		if(strncmp(line, "SHELL=", 6) == 0) {
			strcpy(system_cfg_shell, line + 6);
		} else if(strncmp(line, "CONSOLES=", 9) == 0) {
			char *value = line + 9;
			char *digit = value;
			uint16_t consoles = 0;
			uint8_t valid = 1;

			while(*digit != '\0') {
				if(*digit < '0' || *digit > '9') {
					valid = 0;
					break;
				}
				consoles = consoles * 10 + (*digit - '0');
				digit++;
			}
			if(valid && digit != value)
				system_cfg_consoles = consoles;
		}

		line = next;
	}

	if(system_cfg_consoles > 4)
		kpanic("maximum of 4 consoles allowed!");
	return;

use_defaults:
	bios_puts("Could not open SYSTEM.CFG, using defaults....\n");
	strcpy(system_cfg_shell, "SHELL.PRG");
}



// the shell is configured to run at 2000:0000 - that is, the very first task after the kernel itself
void load_default_shell() {
	matfs16_status_t status;
	uint16_t lba;
	uint16_t next_lba;
	uint16_t offset = 0;
	uint16_t shell_len;

	status = matfs16_locate_file(system_cfg_shell, 0, &lba, &shell_len);
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
	bios_puts(system_cfg_shell);
	bios_puts("...\n");

	load_default_shell();

	for(;;);
}
