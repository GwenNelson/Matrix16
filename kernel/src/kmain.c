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
#include "mtsk16.h"
#include "kheap.h"
#include "kfiles.h"
#include "8086.h"

volatile uint16_t timer_ticks;

static void save_menu_cb(uint8_t console_num) {
	kthread_id_t thread = kthread_from_console(console_num);
	if(thread < KTHREAD_USER_TSK0_ID || thread > KTHREAD_USER_TSK3_ID) return;
	kthread_set_pending_save_menu(thread,true);
}

void k_timer_callback(registers_t *regs) {
	timer_ticks++;
	if(regs->cs != SEG_KERN) {
		kthread_id_t current = kthread_cur_thread();
		if(current >= KTHREAD_USER_TSK0_ID && current <= KTHREAD_USER_TSK3_ID &&
		   kthread_is_pending_save_menu(current)) {
			if(kthread_handoff_save_menu(current, (uint16_t *)regs)) return;
		}
	}
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

kconsole_saved_state_t task_console_states[4] SEG_SYS_BSS; // we only have 4 VCs max
static bool task_console_state_valid[4];
static uint16_t next_free_task_segment = SEG_USER0;
/* Startup task loading and the later save menu use this at separate times. */
uint8_t kernel_vram_scratch[MTSK16_VRAM_SIZE] SEG_SYS_BSS;
static task_state_t startup_task_state SEG_SYS_BSS;

typedef struct startup_loaded_task_t {
	bool valid;
	uint8_t drive_num;
	uint8_t console;
	uint16_t segment;
	uint16_t resume_sp;
	char filename[MATFS16_FILENAME_MAXLEN];
} startup_loaded_task_t;

static startup_loaded_task_t startup_loaded_tasks[4] SEG_SYS_BSS;
static bool startup_blockchain_cache_attempted[2];

extern void copy_words_between_segments(uint16_t source_seg, uint16_t dest_seg,
	                                    uint16_t word_count);

static startup_loaded_task_t __far *startup_loaded_task_table(void) {
	return (startup_loaded_task_t __far *)mk_farptr(SEG_SYS,
	                                               (uint16_t)startup_loaded_tasks);
}

static kconsole_saved_state_t __far *task_console_state(uint8_t console) {
	uint16_t offset = (uint16_t)task_console_states +
	                  (uint16_t)console * sizeof(kconsole_saved_state_t);
	return (kconsole_saved_state_t __far *)mk_farptr(SEG_SYS, offset);
}

static task_state_t __far *startup_task_state_ptr(void) {
	return (task_state_t __far *)mk_farptr(SEG_SYS,
	                                     (uint16_t)&startup_task_state);
}

static void copy_console_state(kconsole_saved_state_t __far *destination,
	                           const kconsole_saved_state_t __far *source) {
	destination->row = source->row;
	destination->col = source->col;
	destination->saved_row = source->saved_row;
	destination->saved_col = source->saved_col;
	destination->attribute = source->attribute;
	destination->wrap = source->wrap;
	destination->caps_lock = source->caps_lock;
	destination->num_lock = source->num_lock;
	destination->ansi_state = source->ansi_state;
	destination->param_index = source->param_index;
	for(uint8_t i = 0; i < KCONSOLE_ANSI_PARAMS; i++)
		destination->params[i] = source->params[i];
	destination->input_length = source->input_length;
	for(uint16_t i = 0; i < KCONSOLE_INPUT_SIZE; i++)
		destination->input[i] = source->input[i];
}

static bool startup_filename_matches(const char __far *stored,
	                                 const char *requested) {
	while(*stored && *requested) {
		if(*stored++ != *requested++) return false;
	}
	return *stored == *requested;
}

static void startup_filename_copy(char __far *destination, const char *source) {
	for(uint8_t i = 0; i < MATFS16_FILENAME_MAXLEN; i++) {
		destination[i] = source[i];
		if(source[i] == '\0') return;
	}
	destination[MATFS16_FILENAME_MAXLEN - 1] = '\0';
}

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

static void ensure_startup_blockchain_cache(uint8_t drive_num) {
	matfs16_status_t status;

	if(drive_num > 1 || startup_blockchain_cache_attempted[drive_num]) return;
	startup_blockchain_cache_attempted[drive_num] = true;
	status = matfs16_cache_blockchain(drive_num);
	if(status != MATFS16_OK) bios_puts("Warning: using uncached blockchain reads\n");
}

static startup_loaded_task_t __far *find_loaded_startup_task(const char* filename,
	                                                        uint8_t drive_num) {
	startup_loaded_task_t __far *tasks = startup_loaded_task_table();
	for(uint8_t i = 0; i < 4; i++) {
		if(tasks[i].valid && tasks[i].drive_num == drive_num &&
		   startup_filename_matches(tasks[i].filename, filename)) {
			return &tasks[i];
		}
	}
	return 0;
}

static void remember_loaded_startup_task(const char* filename, uint8_t drive_num,
	                                     uint16_t segment, uint8_t console,
	                                     uint16_t resume_sp, kthread_id_t thread_id) {
	uint8_t slot = thread_id - KTHREAD_USER_TSK0_ID;
	startup_loaded_task_t __far *loaded;

	if(slot >= 4) return;
	loaded = &startup_loaded_task_table()[slot];
	loaded->drive_num = drive_num;
	loaded->console = console;
	loaded->segment = segment;
	loaded->resume_sp = resume_sp;
	startup_filename_copy(loaded->filename, filename);
	loaded->valid = true;
}

static bool copy_loaded_startup_task(const char* filename, uint8_t drive_num,
	                                 uint16_t segment, kthread_id_t thread_id,
	                                 uint8_t console) {
	startup_loaded_task_t __far *source = find_loaded_startup_task(filename, drive_num);
	uint8_t slot = thread_id - KTHREAD_USER_TSK0_ID;
	uint16_t source_vram;
	uint16_t dest_vram;
	mtsk16_context_isr_v1_t __far *context;

	if(source == 0 || slot >= 4) return false;
	source_vram = kconsole_vram_segment(source->console);
	dest_vram = kconsole_vram_segment(console);
	if(source_vram == VRAM_SEG_INVALID || dest_vram == VRAM_SEG_INVALID) return false;

	bios_puts("Copying task ");
	bios_puts((char*)filename);
	bios_puts(" from its loaded slot\n");
	copy_words_between_segments(source->segment, segment, 0x8000);
	copy_words_between_segments(source_vram, dest_vram,
	                            MTSK16_VRAM_SIZE / sizeof(uint16_t));

	context = (mtsk16_context_isr_v1_t __far *)mk_farptr(segment, source->resume_sp);
	context->es = segment;
	context->ds = segment;
	context->cs = segment;

	copy_console_state(task_console_state(console),
	                   task_console_state(source->console));
	task_console_state_valid[console] = true;
	kthread_setup_restored(thread_id, segment, source->resume_sp, console);
	remember_loaded_startup_task(filename, drive_num, segment, console,
	                             source->resume_sp, thread_id);
	return true;
}

bool startup_task(const char* filename, uint8_t drive_num, uint16_t seg, kthread_id_t thread_id, uint8_t console) {
	uint16_t task_lba;
	uint16_t task_len;

	if(copy_loaded_startup_task(filename, drive_num, seg, thread_id, console)) return true;
	ensure_startup_blockchain_cache(drive_num);
	if(matfs16_locate_file(filename,drive_num,&task_lba,&task_len) != MATFS16_OK) return false;

	uint16_t vram_seg = kconsole_vram_segment(console);
	if(vram_seg == VRAM_SEG_INVALID) return false;

	uint8_t scratch_buf[512];

	task_buffers_t t_buffers = { .memory_segment = seg,
				   .vram    = { .segment = SEG_SYS, .offset = (uint16_t)kernel_vram_scratch},
				   .scratch = { .segment = SEG_KERN, .offset = (uint16_t)&scratch_buf}};

	task_state_t __far *t_state = startup_task_state_ptr();
	bios_puts("Loading task ");
	bios_puts((char*)filename);
	bios_puts(drive_num == 0 ? " from A: " : " from B: ");
	task_status_t status = task_restore(drive_num, task_lba, &t_buffers, t_state);
	bios_puts("\n");

	if(status != TASK_OK) return false;

	volatile uint16_t __far *vram =
		(volatile uint16_t __far *)mk_farptr(vram_seg, 0);
	uint16_t __far *saved_vram =
		(uint16_t __far *)mk_farptr(SEG_SYS, (uint16_t)kernel_vram_scratch);
	for(uint16_t i = 0; i < MTSK16_VRAM_SIZE / sizeof(uint16_t); i++) vram[i] = saved_vram[i];

	// stash the console state, so we can restore it correctly after all tasks are loaded, before we actually start them up
	copy_console_state(task_console_state(console),
	                   &t_state->console);
	task_console_state_valid[console] = true;

	// setup the thread
	kthread_setup_restored(thread_id,seg,t_state->resume_sp,console);
	remember_loaded_startup_task(filename, drive_num, seg, console,
	                             t_state->resume_sp, thread_id);
	return true;
}

static bool startup_default_shell(uint8_t task_number, uint16_t task_segment,
	                              uint8_t console) {
	if(system_cfg.default_action == SYSTEM_CFG_DEFAULT_NONE) return false;
	if(system_cfg.default_action != SYSTEM_CFG_DEFAULT_SHELL)
		kpanic("startup_all_tasks() - invalid default action");

	return startup_task(system_cfg.shell, 0, task_segment,
	                    KTHREAD_USER_TSK0_ID + task_number, console) ||
	       startup_task(system_cfg.shell, 1, task_segment,
	                    KTHREAD_USER_TSK0_ID + task_number, console);
}

static bool startup_error_action(uint8_t task_number, uint16_t task_segment,
	                             uint8_t console) {
	switch(system_cfg.error_action) {
		case SYSTEM_CFG_ERROR_DEFAULT:
			if(system_cfg.default_action == SYSTEM_CFG_DEFAULT_NONE) return false;
			return startup_default_shell(task_number, task_segment, console);
		case SYSTEM_CFG_ERROR_REPORT:
			report_startup_error("Could not start configured task");
			return false;
		case SYSTEM_CFG_ERROR_NONE:
			return false;
		case SYSTEM_CFG_ERROR_SHELL:
			if(startup_default_shell(task_number, task_segment, console)) return true;
			report_startup_error("Could not load the error shell from drive A or B");
			return false;
		case SYSTEM_CFG_ERROR_FILE_A:
			if(startup_task(system_cfg.error_filename, 0, task_segment,
			                KTHREAD_USER_TSK0_ID + task_number, console)) return true;
			break;
		case SYSTEM_CFG_ERROR_FILE_B:
			if(startup_task(system_cfg.error_filename, 1, task_segment,
			                KTHREAD_USER_TSK0_ID + task_number, console)) return true;
			break;
		default:
			kpanic("startup_all_tasks() - invalid error action");
	}

	report_startup_error("Could not load the configured error task");
	return false;
}

static void startup_configured_task(uint8_t task_number) {
	uint8_t task_file_drive = 0;
	uint8_t console = (uint8_t)system_cfg.tasks[task_number].console;
	uint16_t task_segment;
	char task_filename[MATFS16_FILENAME_MAXLEN];
	char* task_slot_filename;
	matfs16_status_t status;
	bool source_ready = true;
	bool task_loaded = false;

	if(system_cfg.tasks[task_number].source == SYSTEM_CFG_SOURCE_NONE) return;
	if(system_cfg.tasks[task_number].source == SYSTEM_CFG_SOURCE_DEFAULT &&
	   system_cfg.default_action == SYSTEM_CFG_DEFAULT_NONE) return;
	if(console >= SYSTEM_CFG_CONSOLE_COM1) {
		report_startup_error("Serial consoles are not supported yet");
		return;
	}

	task_segment = allocate_task_segment();
	if(task_segment == 0) kpanic("No free task segment");

	switch(system_cfg.tasks[task_number].source) {
		case SYSTEM_CFG_SOURCE_DEFAULT:
			task_loaded = startup_default_shell(task_number, task_segment, console);
			if(!task_loaded) report_startup_error("Could not start default shell from drive A or B");
			break;
		case SYSTEM_CFG_SOURCE_SUPER_A:
			task_file_drive = 0;
			status = matfs16_read_task_slot(0, task_number, task_filename,
			                                &task_slot_filename);
			if(status != MATFS16_OK || task_slot_filename == NULL) source_ready = false;
			break;
		case SYSTEM_CFG_SOURCE_SUPER_B:
			task_file_drive = 1;
			status = matfs16_read_task_slot(1, task_number, task_filename,
			                                &task_slot_filename);
			if(status != MATFS16_OK || task_slot_filename == NULL) source_ready = false;
			break;
		case SYSTEM_CFG_SOURCE_FILE_A:
			task_file_drive = 0;
			strcpy(task_filename, system_cfg.tasks[task_number].filename);
			break;
		case SYSTEM_CFG_SOURCE_FILE_B:
			task_file_drive = 1;
			strcpy(task_filename, system_cfg.tasks[task_number].filename);
			break;
		default:
			release_task_segment(task_segment);
			kpanic("startup_all_tasks() - invalid state");
	}

	if(source_ready && system_cfg.tasks[task_number].source != SYSTEM_CFG_SOURCE_DEFAULT)
		task_loaded = startup_task(task_filename, task_file_drive, task_segment,
		                          KTHREAD_USER_TSK0_ID + task_number, console);

	if(!task_loaded && system_cfg.tasks[task_number].source != SYSTEM_CFG_SOURCE_DEFAULT) {
		task_loaded = startup_error_action(task_number, task_segment, console);
	}

	if(!task_loaded) release_task_segment(task_segment);
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
	ensure_startup_blockchain_cache(0);

	startup_all_tasks();

	for(int i=0; i<4; i++) {
		if(task_console_state_valid[i]) {
			kconsole_restore_state(i, task_console_state(i));
		} else {
			kconsole_init(i);
		}
	}

	kconsole_set_save_cb(save_menu_cb);
	kthread_start();
	kpanic("Something went wrong!");

	for(;;);
}
