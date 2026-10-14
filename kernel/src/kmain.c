#include <stdint.h>
#include <stdbool.h>
#include "bioscons.h"
#include "biosdisk.h"
#include "isr.h"
#include "kpanic.h"
#include "kthread.h"
#include "matfs16.h"
#include "parsecfg.h"
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

void kernel_main(void) {
	bios_puts("Matrix16 Kernel loaded!\n\n");

	install_isr08();
	install_isr09();
	install_isr80();

	bios_puts("Trying SYSTEM.CFG...\n");
	load_system_cfg();

	bios_puts("Starting up kthread...\n");
	kthread_init();
	bios_puts("Should be ready to start scheduler...\n");
//	kthread_start();
	kpanic("Something went wrong!");

	bios_puts("Starting configured shell A:");
	bios_puts(system_cfg.shell);
	bios_puts("...\n");

	load_default_shell();

	for(;;);
}
