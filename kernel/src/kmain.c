#include <stdint.h>
#include <stdbool.h>
#include "bioscons.h"
#include "biosdisk.h"
#include "isr.h"
#include "kpanic.h"
#include "kthread.h"
#include "kconsole.h"
#include "kmemmap.h"
#include "matfs16.h"
#include "parsecfg.h"
#include "string.h"
#include "utils.h"
#include "waitkey.h"
#include "task.h"

volatile uint16_t timer_ticks;

void k_timer_callback(void) {
	timer_ticks++;
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
//	__asm__ volatile (
//	    "ljmp $0x2000, $0x0000"
//	);	
}

uint8_t buf[512];

static void sleepfor(uint16_t ticks) {
	uint16_t flags;
	uint16_t start;

	__asm__ volatile ("pushf; popw %0; cli" : "=r" (flags) : : "memory");
	start = timer_ticks;

	while((uint16_t)(timer_ticks - start) < ticks)
		__asm__ volatile ("sti; hlt; cli" : : : "memory");

	__asm__ volatile ("pushw %0; popf" : : "r" (flags) : "memory", "cc");
}

static void do_start_delay(void) {
	uint16_t remaining = system_cfg.start_delay;
	uint8_t ticks_until_dot = 5;
	char ignored;

	if(remaining == 0) return;

	while(kconsole_getc(PAGENUM_VC0, &ignored))
		;

	bios_puts("Press any key to start immediately");

	while(remaining != 0) {
		if(kconsole_getc(PAGENUM_VC0, &ignored)) break;

		sleepfor(1);
		remaining--;

		if(kconsole_getc(PAGENUM_VC0, &ignored)) break;
		if(--ticks_until_dot == 0) {
			bios_puts(".");
			ticks_until_dot = 5;
		}
	}

	bios_puts("\n");
}

static void skip_systemcfg_prompt(void) {
	volatile uint16_t __far *bios_ticks;
	uint16_t flags;
	uint16_t key;
	uint16_t previous_tick;
	uint16_t remaining = system_cfg.start_delay;
	bool answered = false;

	if(remaining == 0) return;

	bios_ticks = (volatile uint16_t __far *)mk_farptr(0x0040, 0x006c);
	__asm__ volatile ("pushf; popw %0; sti" : "=r" (flags) : : "memory");

	while(bios_poll_key(&key))
		;

	bios_puts("Press F8 to skip SYSTEM.CFG...");
	previous_tick = *bios_ticks;

	while(remaining != 0) {
		if(bios_poll_key(&key) && (uint8_t)(key >> 8) == 0x42) {
			bios_puts("\nSkip SYSTEM.CFG? (Y/N) ");

			while(!answered) {
				if(!bios_poll_key(&key)) {
					__asm__ volatile ("hlt" : : : "memory");
					continue;
				}

				if((uint8_t)key == 'Y' || (uint8_t)key == 'y') {
					bios_puts("Y\n");
					reset_system_cfg_defaults();
					answered = true;
				} else if((uint8_t)key == 'N' || (uint8_t)key == 'n') {
					bios_puts("N\n");
					answered = true;
				}
			}
			break;
		}

		if(*bios_ticks != previous_tick) {
			previous_tick = *bios_ticks;
			remaining--;
			continue;
		}

		__asm__ volatile ("hlt" : : : "memory");
	}

	if(!answered) bios_puts("\n");
	__asm__ volatile ("pushw %0; popf" : : "r" (flags) : "memory", "cc");
}

void kernel_main(void) {
	bios_puts("Matrix16 Kernel loaded!\n\n");

	bios_puts("Trying SYSTEM.CFG...\n");
	load_system_cfg();
	skip_systemcfg_prompt();

	if(cfg_errors) {
		bios_puts("Errors in SYSTEM.CFG, will use defaults, press any key to continue...\n");
		bios_wait_for_key();
	}

	bios_puts("\n\n\n");

	install_isr08();
	install_isr09();
	install_isr80();

	bios_puts("Starting task scheduler...\n");
	kthread_init();

	bios_puts("Loading tasks...\n");

	// okay, let's startup task0 first...
	// for now, i'm gonna make task0 always the shell

	load_default_shell();
	kthread_setup(KTHREAD_USER_TSK0_ID,SEG_USER0,0x0000,0xFFFE,PAGENUM_VC0);

	// let's make task1 HELLO.TSK as a test, so first locate it...
	
	uint16_t hello_lba;
	uint16_t hello_len;
	if(matfs16_locate_file("HELLO.TSK", 0, &hello_lba, &hello_len) != MATFS16_OK) kpanic("MISSING HELLO.TSK!");

	// now we have it, we now need to parse the header and load it, so let's setup the buffers
	task_buffers_t hello_buffers = { .memory_segment = SEG_USER1,
					 .vram    = { .segment = 0xb900, .offset = 0x0000},
					 .scratch = { .segment = SEG_KERN, .offset = (uint16_t)&buf}};

	task_state_t hello_state;

	// now we can load the thing
	task_status_t status = task_restore(0,hello_lba,&hello_buffers,&hello_state);

	if(status != TASK_OK) {
		kpanic("FAILED TO LOAD HELLO.TSK!");
	}

	// give the user a moment, then we can start...
	do_start_delay();
	
	kconsole_init(PAGENUM_VC0);

	// if we get here, yay - let's setup the console for it correctly, and create the thread for it
	if(!kconsole_restore_state(PAGENUM_VC1,&hello_state.console)) kpanic("FAILED TO RESTORE CONSOLE!");
	kthread_setup(KTHREAD_USER_TSK1_ID,SEG_USER1,0x0000,hello_state.resume_sp,PAGENUM_VC1);


	kthread_start();
	kpanic("Something went wrong!");



	for(;;);
}
