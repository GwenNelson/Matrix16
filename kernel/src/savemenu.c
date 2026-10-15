#include "savemenu.h"
#include "kconsole.h"
#include "mtsk16.h"
#include "utils.h"
#include "kfiles.h"
#include "kmemmap.h"
#include "matfs16.h"
#include "task.h"
#include "kaux.h"

extern uint8_t kernel_vram_scratch[MTSK16_VRAM_SIZE];

/* This is temporary handoff storage, separate from each task's saved state. */
static kconsole_saved_state_t saved_console_state SEG_SYS_BSS;
static uint8_t saved_active_console;
static uint8_t task_save_scratch[MATFS16_BLOCK_SIZE];
static task_state_t task_save_state SEG_SYS_BSS;
static uint8_t task_replace_vram[MTSK16_VRAM_SIZE] SEG_SYS_BSS;
static uint16_t task_save_resume_sp;
static bool menu_task_changed;

static kconsole_saved_state_t __far *saved_console_state_ptr(void) {
	return (kconsole_saved_state_t __far *)mk_farptr(SEG_SYS,
	                                               (uint16_t)&saved_console_state);
}

static task_state_t __far *task_save_state_ptr(void) {
	return (task_state_t __far *)mk_farptr(SEG_SYS,
	                                    (uint16_t)&task_save_state);
}

static uint8_t __far *task_replace_vram_ptr(void) {
	return (uint8_t __far *)mk_farptr(SEG_SYS,
	                               (uint16_t)task_replace_vram);
}

static void copy_saved_console_state(kconsole_saved_state_t __far *destination,
	                                 const kconsole_saved_state_t __far *source) {
	destination->row = source->row;
	destination->col = source->col;
	destination->saved_row = source->saved_row;
	destination->saved_col = source->saved_col;
	destination->attribute = source->attribute;
	destination->cursor_visible = source->cursor_visible;
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
	uint16_t __far *scratch;

	if(vram_segment == VRAM_SEG_INVALID ||
	   !kconsole_save_state(console, saved_console_state_ptr())) {
		restore_interrupts(flags);
		return false;
	}

	saved_active_console = kconsole_active_console();
	vram = (volatile uint16_t __far *)mk_farptr(vram_segment, 0);
	scratch = (uint16_t __far *)mk_farptr(SEG_SYS, (uint16_t)kernel_vram_scratch);
	for(uint16_t i = 0; i < MTSK16_VRAM_SIZE / sizeof(uint16_t); i++)
		scratch[i] = vram[i];

	restore_interrupts(flags);
	return true;
}

static void restore_task_console(uint8_t console) {
	uint16_t flags = disable_interrupts();
	uint16_t vram_segment = kconsole_vram_segment(console);
	volatile uint16_t __far *vram =
		(volatile uint16_t __far *)mk_farptr(vram_segment, 0);
	uint16_t __far *scratch =
		(uint16_t __far *)mk_farptr(SEG_SYS, (uint16_t)kernel_vram_scratch);

	for(uint16_t i = 0; i < MTSK16_VRAM_SIZE / sizeof(uint16_t); i++)
		vram[i] = scratch[i];
	(void)kconsole_restore_state(console, saved_console_state_ptr());
	restore_interrupts(flags);

	/* The menu may have selected another BIOS text page. Put the task's page
	 * back onscreen and refresh its cursor and keyboard-lock LEDs. */
	kconsole_switchto(saved_active_console);
}

static void show_empty_task_console(uint8_t console, char *message) {
	uint8_t __far *saved_vram =
		(uint8_t __far *)mk_farptr(SEG_SYS, (uint16_t)kernel_vram_scratch);

	for(uint16_t i = 0; i < MTSK16_VRAM_SIZE; i++) saved_vram[i] = 0;
	kconsole_init(console);
	kconsole_puts(console, message);
	menu_task_changed = true;
}

void savemenu_run(kthread_id_t task_id, uint16_t task_segment, uint8_t console,
	              uint16_t resume_sp) {
	task_save_resume_sp = resume_sp;
	menu_task_changed = false;
	if(!save_task_console(console)) return;
	savemenu_entry(task_id, task_segment, console);
	if(menu_task_changed) {
		kconsole_switchto(saved_active_console);
	} else {
		restore_task_console(console);
	}
}

void savemenu_savetask(kthread_id_t task_id, uint16_t task_segment, uint8_t console) {
	task_buffers_t buffers;
	task_status_t  status;
	task_state_t __far *state = task_save_state_ptr();

	if(!kthread_task_is_active(task_id)) {
		kconsole_puts(console,"Can not save - task slot is empty\r\n");
		goto finish;
	}

	if(kaux_user_claimed_by(task_id)) {
		kconsole_puts(console,"Can not save - task has claimed the auxiliary segment\r\n");
		goto finish;
	}

	if(task_has_open_files(task_id)) {
		kconsole_puts(console,"Can not save - task has open file handles\r\n");
		goto finish;
	}

	uint16_t a_free;
	matfs16_count_free_blocks(0,&a_free);
	uint16_t b_free;
	matfs16_count_free_blocks(1,&b_free);

	kconsole_puts(console,"\x01b[2J\x01B[H\n\r");
	kconsole_move_cursor(console,0,0);
	kconsole_puts(console,"Choose floppy to save to (137 blocks needed):\r\n");

	kconsole_puts(console,"0 - cancel\r\n");
	kconsole_puts(console,"1 - A: "); kconsole_putuint(console,a_free); kconsole_puts(console, " blocks free\r\n");
	kconsole_puts(console,"2 - B: "); kconsole_putuint(console,b_free); kconsole_puts(console, " blocks free\r\n"); 
	kconsole_puts(console,"\n");

	char c='\0';
	while((c != '0') && (c != '1') && (c != '2')) {
		while(!kconsole_getc(console,&c));
	}	
	if(c=='0') goto finish;

	uint8_t drive_num = c-49; // turn it into the drive number

	char filename[26];
	kconsole_puts(console,"Filename (minus extension):");
	uint16_t retval = kconsole_gets(console,filename,22);
	if(retval==0) {
		kconsole_puts(console,"Invalid filename - cancelling save\r\n");
		goto finish;
	}

	filename[retval]   = '.';
	filename[retval+1] = 'T';
	filename[retval+2] = 'S';
	filename[retval+3] = 'K';
	filename[retval+4] = '\0';

	kconsole_puts(console, "Saving to ");
	if(drive_num==0) kconsole_puts(console,"A:");
	if(drive_num==1) kconsole_puts(console,"B:");
	kconsole_puts(console,"...\r\n");

	{
		uint16_t existing_lba;
		uint16_t existing_length;
		matfs16_status_t fs_status = matfs16_locate_file(filename, drive_num, &existing_lba, &existing_length);

		if(fs_status == MATFS16_OK) {
			kconsole_puts(console, "File exists. Overwrite it? (Y/N)\r\n");
			c = '\0';
			while(c != 'Y' && c != 'y' && c != 'N' && c != 'n') {
				while(!kconsole_getc(console, &c));
			}
			if(c == 'N' || c == 'n') {
				goto finish;
			}
			fs_status = matfs16_delete_file(drive_num, filename);
			if(fs_status != MATFS16_OK) {
				kconsole_puts(console, "Could not remove existing file\r\n");
				goto finish;
			}
		} else if(fs_status != MATFS16_ERR_NOT_FOUND) {
			kconsole_puts(console, "Could not check existing file\r\n");
			goto finish;
		}
	}

	buffers.memory_segment  = task_segment;
	buffers.vram.segment    = SEG_SYS;
	buffers.vram.offset     = (uint16_t)kernel_vram_scratch;
	buffers.scratch.segment = SEG_KERN;
	buffers.scratch.offset  = (uint16_t)task_save_scratch;

	state->resume_sp       = task_save_resume_sp;
	state->screen_mode     = MTSK16_SCREEN_TEXT_80X25_COLOR;
	state->vram_size       = MTSK16_VRAM_SIZE;
	state->memory_encoding = MTSK16_MEMORY_RAW;
	copy_saved_console_state(&state->console, saved_console_state_ptr());

	status = task_save(drive_num, filename, &buffers, state);
	if(status == TASK_OK) {
		kconsole_puts(console,"Saved task!\r\n");
	} else {
		kconsole_puts(console,"Could not save task, unknown error\r\n");
		goto finish;
	}

	kconsole_puts(console,"Assign new task to a superblock slot on drive ");
	if(drive_num==0) kconsole_puts(console,"A:");
	if(drive_num==1) kconsole_puts(console,"B:");
	kconsole_puts(console," ?\r\n");
	kconsole_puts(console,"0 - save to slot 0\r\n");
	kconsole_puts(console,"1 - save to slot 1\r\n");
	kconsole_puts(console,"2 - save to slot 2\r\n");
	kconsole_puts(console,"3 - save to slot 3\r\n");
	kconsole_puts(console,"\n");
	kconsole_puts(console,"Any other key to return\r\n");

	while(!kconsole_getc(console,&c));
	if((c=='0') || (c=='1') || (c=='2') || (c=='3')) {
		uint8_t superslot = c-48;
		uint16_t first_block;
		uint16_t file_length;
		matfs16_status_t fs_status = matfs16_locate_file(filename, drive_num, &first_block, &file_length);
		if(fs_status != MATFS16_OK || matfs16_write_task_slot(drive_num, superslot, first_block) != MATFS16_OK) {
			kconsole_puts(console, "Could not assign task slot\r\n");
			goto finish;
		}
	} else {
		return;
	}

finish:
	kconsole_puts(console,"Press any key to return\r\n");
	kconsole_wait_for_key(console);
}

void savemenu_replacetask(kthread_id_t task_id,uint16_t task_segment,uint8_t console) {
	char filename[MATFS16_FILENAME_MAXLEN];
	char *slot_filename;
	char c = '\0';
	uint8_t drive_num;
	uint16_t first_lba;
	uint16_t file_length;
	matfs16_status_t fs_status;
	task_buffers_t buffers;
	task_state_t __far *state = task_save_state_ptr();
	task_status_t task_status;
	uint16_t flags;
	uint16_t vram_segment;
	volatile uint16_t __far *vram;
	uint16_t __far *replacement;

	kconsole_puts(console,"\x01b[2J\x01B[H\n\r");
	kconsole_move_cursor(console,0,0);
	kconsole_puts(console,"Replace task\r\n\r\n");
	kconsole_puts(console,"Choose source drive:\r\n");
	kconsole_puts(console,"0 - cancel\r\n");
	kconsole_puts(console,"1 - A:\r\n");
	kconsole_puts(console,"2 - B:\r\n");
	while(c != '0' && c != '1' && c != '2') {
		while(!kconsole_getc(console, &c));
	}
	if(c == '0') return;
	drive_num = c - '1';

	c = '\0';
	kconsole_puts(console,"\r\nLoad from:\r\n");
	kconsole_puts(console,"0 - cancel\r\n");
	kconsole_puts(console,"1 - superblock task slot\r\n");
	kconsole_puts(console,"2 - filename\r\n");
	while(c != '0' && c != '1' && c != '2') {
		while(!kconsole_getc(console, &c));
	}
	if(c == '0') return;

	matfs16_invalidate_blockchain_cache(drive_num);
	fs_status = matfs16_cache_blockchain(drive_num);
	if(fs_status != MATFS16_OK) {
		kconsole_puts(console,"Could not read filesystem on that drive\r\n");
		goto failed;
	}

	if(c == '1') {
		c = '\0';
		kconsole_puts(console,"Task slot (0-3): ");
		while(c < '0' || c > '3') {
			while(!kconsole_getc(console, &c));
		}
		fs_status = matfs16_read_task_slot(drive_num, c - '0', filename,
		                                  &slot_filename);
		if(fs_status != MATFS16_OK || slot_filename == 0) {
			kconsole_puts(console,"No task file in that slot\r\n");
			goto failed;
		}
	} else {
		kconsole_puts(console,"Filename: ");
		if(kconsole_gets(console, filename, MATFS16_FILENAME_MAXLEN) == 0) {
			kconsole_puts(console,"Invalid filename\r\n");
			goto failed;
		}
	}

	fs_status = matfs16_locate_file(filename, drive_num, &first_lba,
	                                &file_length);
	if(fs_status != MATFS16_OK) {
		kconsole_puts(console,"Could not find that file\r\n");
		goto failed;
	}
	(void)file_length;

	buffers.memory_segment = task_segment;
	buffers.vram.segment = SEG_SYS;
	buffers.vram.offset = (uint16_t)task_replace_vram;
	buffers.scratch.segment = SEG_KERN;
	buffers.scratch.offset = (uint16_t)task_save_scratch;
	task_status = task_replace(task_id, drive_num, first_lba, task_segment,
	                           console, &buffers, state);
	if(task_status != TASK_OK) {
		show_empty_task_console(console,
			"Task load failed; slot terminated. Press Ctrl-Alt-S for menu.");
		return;
	}

	/* Install the replacement's saved screen only after its memory and thread
	 * frame have committed. The original menu snapshot is left untouched. */
	flags = disable_interrupts();
	vram_segment = kconsole_vram_segment(console);
	vram = (volatile uint16_t __far *)mk_farptr(vram_segment, 0);
	replacement = (uint16_t __far *)task_replace_vram_ptr();
	for(uint16_t i = 0; i < MTSK16_VRAM_SIZE / sizeof(uint16_t); i++)
		vram[i] = replacement[i];
	(void)kconsole_restore_state(console, &state->console);
	restore_interrupts(flags);
	menu_task_changed = true;
	return;

failed:
	kconsole_puts(console,"Press any key to return to the menu\r\n");
	kconsole_wait_for_key(console);
}

void savemenu_terminatetask(kthread_id_t task_id, uint16_t task_segment, uint8_t console) {
	(void)task_segment;
	if(!task_terminate(task_id)) {
		kconsole_puts(console,"Task slot is already empty\r\n");
		kconsole_puts(console,"Press any key to return to the menu\r\n");
		kconsole_wait_for_key(console);
		return;
	}

	/* Do not let savemenu_run restore the terminated task's old display. */
	show_empty_task_console(console,
		"Task terminated, press Ctrl-Alt-S for menu.");
}

void savemenu_entry(kthread_id_t task_id, uint16_t task_segment, uint8_t console) {
	// first clear the screen and drain keyboard buffer
	char ignored;
	while(kconsole_getc(console, &ignored));
	for(;;) {
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

		char c='\0';
		while( (c != '1') && (c != '2') && (c != '3') && (c != '4')) {
			while(!kconsole_getc(console,&c));
		}
	
		switch(c) {
			case '1':
				savemenu_savetask(task_id,task_segment,console);
			break;
			case '2':
				savemenu_replacetask(task_id,task_segment,console);
				if(menu_task_changed) return;
			break;
			case '3':
				savemenu_terminatetask(task_id,task_segment,console);
				if(menu_task_changed) return;
			break;
			case '4':
				return;
			break;
		}
	}
}
