#include "savemenu.h"
#include "kconsole.h"
#include "mtsk16.h"
#include "utils.h"
#include "kfiles.h"
#include "kmemmap.h"
#include "matfs16.h"
#include "task.h"

extern uint8_t kernel_vram_scratch[MTSK16_VRAM_SIZE];

/* This is temporary handoff storage, separate from each task's saved state. */
static kconsole_saved_state_t saved_console_state;
static uint8_t saved_active_console;
static uint8_t task_save_scratch[MATFS16_BLOCK_SIZE];
static task_state_t task_save_state;
static uint16_t task_save_resume_sp;

static uint16_t disable_interrupts(void) {
	uint16_t flags;
	__asm__ volatile ("pushf; popw %0; cli" : "=r" (flags) : : "memory");
	return flags;
}

static void restore_interrupts(uint16_t flags) {
	__asm__ volatile ("pushw %0; popf" : : "r" (flags) : "memory", "cc");
}

static bool save_task_console(uint8_t console) {
	uint16_t flags = disable_interrupts();
	uint16_t vram_segment = kconsole_vram_segment(console);
	volatile uint16_t __far *vram;

	if(vram_segment == VRAM_SEG_INVALID ||
	   !kconsole_save_state(console, &saved_console_state)) {
		restore_interrupts(flags);
		return false;
	}

	saved_active_console = kconsole_active_console();
	vram = (volatile uint16_t __far *)mk_farptr(vram_segment, 0);
	for(uint16_t i = 0; i < MTSK16_VRAM_SIZE / sizeof(uint16_t); i++)
		((uint16_t*)kernel_vram_scratch)[i] = vram[i];

	restore_interrupts(flags);
	return true;
}

static void restore_task_console(uint8_t console) {
	uint16_t flags = disable_interrupts();
	uint16_t vram_segment = kconsole_vram_segment(console);
	volatile uint16_t __far *vram =
		(volatile uint16_t __far *)mk_farptr(vram_segment, 0);

	for(uint16_t i = 0; i < MTSK16_VRAM_SIZE / sizeof(uint16_t); i++)
		vram[i] = ((uint16_t*)kernel_vram_scratch)[i];
	(void)kconsole_restore_state(console, &saved_console_state);
	restore_interrupts(flags);

	/* The menu may have selected another BIOS text page. Put the task's page
	 * back onscreen and refresh its cursor and keyboard-lock LEDs. */
	kconsole_switchto(saved_active_console);
}

void savemenu_run(kthread_id_t task_id, uint16_t task_segment, uint8_t console,
	              uint16_t resume_sp) {
	task_save_resume_sp = resume_sp;
	if(!save_task_console(console)) return;
	savemenu_entry(task_id, task_segment, console);
	restore_task_console(console);
}

void savemenu_savetask(kthread_id_t task_id, uint16_t task_segment, uint8_t console) {
	task_buffers_t buffers;
	task_status_t status;

	if(task_has_open_files(task_id)) {
		kconsole_puts(console,"Can not save - task has open file handles\r\n");
		return;
	}

	buffers.memory_segment = task_segment;
	buffers.vram.segment = SEG_KERN;
	buffers.vram.offset = (uint16_t)kernel_vram_scratch;
	buffers.scratch.segment = SEG_KERN;
	buffers.scratch.offset = (uint16_t)task_save_scratch;
	task_save_state.resume_sp = task_save_resume_sp;
	task_save_state.screen_mode = MTSK16_SCREEN_TEXT_80X25_COLOR;
	task_save_state.vram_size = MTSK16_VRAM_SIZE;
	task_save_state.memory_encoding = MTSK16_MEMORY_RAW;
	task_save_state.console = saved_console_state;

	status = task_save(0, "SAVED.TSK", &buffers, &task_save_state);
	if(status == TASK_OK) kconsole_puts(console,"Saved task to A:SAVED.TSK\r\n");
	else kconsole_puts(console,"Could not save task\r\n");
	kconsole_puts(console,"Press any key to return\r\n");
	kconsole_wait_for_key(console);
}

void savemenu_entry(kthread_id_t task_id, uint16_t task_segment, uint8_t console) {
	// first clear the screen and drain keyboard buffer
	char ignored;
	while(kconsole_getc(console, &ignored));
	kconsole_puts(console,"\x01b[2J\x01B[H\n\r");
	kconsole_move_cursor(console,0,0);

	kconsole_puts(console,"Matrix16 task save\r\n");
	kconsole_puts(console,"\r\n");
	kconsole_puts(console,"1 - Save task\r\n");
	kconsole_puts(console,"2 - Replace task\r\n");
	kconsole_puts(console,"3 - Terminate task\r\n");
	kconsole_puts(console,"4 - Return to task\r\n");
	kconsole_puts(console,"\n");
	kconsole_puts(console,"Select an option\r\n");

	for(;;) {
		char c='\0';
		while( (c != '1') && (c != '2') && (c != '3') && (c != '4')) {
			while(!kconsole_getc(console,&c));
		}
	
		switch(c) {
			case '1':
				savemenu_savetask(task_id,task_segment,console);
			break;
			case '2':
			break;
			case '3':
			break;
			case '4':
			return;
			break;
		}
	}
}
