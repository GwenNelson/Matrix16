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
#include "kheap.h"
#include "kfiles.h"

volatile uint16_t timer_ticks;

void k_timer_callback(void) {
	timer_ticks++;
	kthread_schedule();
}

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

	while(kconsole_getc(PAGENUM_VC0, &ignored));

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

kconsole_saved_state_t task_console_states[4]; // we only have 4 VCs max
static bool task_console_state_valid[4];
static uint16_t next_free_task_segment = SEG_USER0;

static uint16_t allocate_task_segment(void) {
	uint16_t segment;

	if(next_free_task_segment > SEG_USER3) return 0;
	segment = next_free_task_segment;
	next_free_task_segment += 0x1000;
	return segment;
}

static void release_task_segment(uint16_t segment) {
	if(next_free_task_segment == segment + 0x1000) next_free_task_segment = segment;
}

static void report_startup_error(char* message) {
	bios_puts("Error: ");
	bios_puts(message);
	bios_puts("\nPress any key to continue...");
	kconsole_wait_for_key(PAGENUM_VC0);
	bios_puts("\n");
}

bool startup_task(const char* filename, uint8_t drive_num, uint16_t seg, kthread_id_t thread_id, uint8_t console) {
	uint16_t task_lba;
	uint16_t task_len;

	if(matfs16_locate_file(filename,drive_num,&task_lba,&task_len) != MATFS16_OK) return false;

	uint16_t vram_seg = kconsole_vram_segment(console);
	if(vram_seg == VRAM_SEG_INVALID) return false;

	uint8_t scratch_buf[512];

	task_buffers_t t_buffers = { .memory_segment = seg,
				   .vram    = { .segment = vram_seg, .offset = 0x0000},
				   .scratch = { .segment = SEG_KERN, .offset = (uint16_t)&scratch_buf}};

	task_state_t t_state;
	task_status_t status = task_restore(drive_num,task_lba,&t_buffers,&t_state);

	if(status != TASK_OK) return false;

	// stash the console state, so we can restore it correctly after all tasks are loaded, before we actually start them up
	task_console_states[console] = t_state.console;
	task_console_state_valid[console] = true;

	// setup the thread
	kthread_setup_restored(thread_id,seg,t_state.resume_sp,console);
	return true;
}

static void startup_configured_task(uint8_t task_number) {
	bool try_drive_b = false;
	uint8_t task_file_drive;
	uint8_t console = (uint8_t)system_cfg.tasks[task_number].console;
	uint16_t task_segment;
	char task_filename[MATFS16_FILENAME_MAXLEN];
	char* task_slot_filename;
	matfs16_status_t status;

	switch(system_cfg.tasks[task_number].source) {
		case SYSTEM_CFG_SOURCE_DEFAULT:
			if(system_cfg.default_action == SYSTEM_CFG_DEFAULT_NONE) return;
			if(system_cfg.default_action != SYSTEM_CFG_DEFAULT_SHELL) kpanic("startup_all_tasks() - invalid default action");
			task_file_drive = 0;
			strcpy(task_filename,system_cfg.shell);
			try_drive_b = true;
			break;
		case SYSTEM_CFG_SOURCE_NONE:
			return;
		case SYSTEM_CFG_SOURCE_SUPER_A:
			task_file_drive = 0;
			status = matfs16_read_task_slot(0,task_number,task_filename,&task_slot_filename);
			if(status != MATFS16_OK) {
				report_startup_error("Could not read task slot from drive A");
				return;
			}
			if(task_slot_filename == NULL) {
				report_startup_error("Task slot is empty on drive A");
				return;
			}
			break;
		case SYSTEM_CFG_SOURCE_SUPER_B:
			task_file_drive = 1;
			status = matfs16_read_task_slot(1,task_number,task_filename,&task_slot_filename);
			if(status != MATFS16_OK) {
				report_startup_error("Could not read task slot from drive B");
				return;
			}
			if(task_slot_filename == NULL) {
				report_startup_error("Task slot is empty on drive B");
				return;
			}
			break;
		case SYSTEM_CFG_SOURCE_FILE_A:
			task_file_drive = 0;
			strcpy(task_filename,system_cfg.tasks[task_number].filename);
			break;
		case SYSTEM_CFG_SOURCE_FILE_B:
			task_file_drive = 1;
			strcpy(task_filename,system_cfg.tasks[task_number].filename);
			break;
		default:
			kpanic("startup_all_tasks() - invalid state");
	}

	task_segment = allocate_task_segment();
	if(task_segment == 0) kpanic("No free task segment");

	if(!startup_task(task_filename, task_file_drive, task_segment, KTHREAD_USER_TSK0_ID + task_number, console) &&
	   (!try_drive_b || !startup_task(task_filename, 1, task_segment, KTHREAD_USER_TSK0_ID + task_number, console))) {
		release_task_segment(task_segment);
		if(try_drive_b) report_startup_error("Could not start default shell from drive A or B");
		else report_startup_error("Could not start configured task");
	}
}

void startup_all_tasks(void) {
	uint8_t i;

	/* BIOS startup diagnostics are written to visible page VC0. Load tasks for
	 * the other consoles first, so any diagnostics they produce happen before
	 * the VC0 task restores its saved VRAM. */
	for(i = 0; i < 4; i++) {
		if(system_cfg.tasks[i].console != SYSTEM_CFG_CONSOLE_VC0) startup_configured_task(i);
	}

	/* Restore VC0 last so its saved screen replaces all earlier diagnostics.
	 * If VC0 has no task, kernel_main() initializes and clears it instead. */
	for(i = 0; i < 4; i++) {
		if(system_cfg.tasks[i].console == SYSTEM_CFG_CONSOLE_VC0) startup_configured_task(i);
	}
}

void kernel_main(void) {
	bios_puts("Starting Matrix16...\n");

	load_system_cfg();
	skip_systemcfg_prompt();

	if(cfg_errors) {
		bios_puts("Errors in SYSTEM.CFG, defaults loaded\n");
	}

	kheap_init();
	kfiles_init(system_cfg.files);

	install_isr08();
	install_isr09();
	install_isr80();

	kthread_init();

	do_start_delay();

	startup_all_tasks();

	for(int i=0; i<4; i++) {
		if(task_console_state_valid[i]) {
			kconsole_restore_state(i,&task_console_states[i]);
		} else {
			kconsole_init(i);
		}
	}

	kthread_start();
	kpanic("Something went wrong!");

	for(;;);
}
