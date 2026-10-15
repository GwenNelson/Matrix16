#include "bioscons.h"
#include "kpanic.h"
#include "ksyscall.h"
#include "kthread.h"
#include "kconsole.h"
#include "kaux.h"
#include "kfiles.h"
#include "kmemmap.h"
#include "matfs16.h"
#include "mtsk16.h"
#include "task.h"
#include "utils.h"

static uint8_t task_exec_scratch[MATFS16_BLOCK_SIZE] SEG_SYS_BSS;
static task_state_t task_exec_state SEG_SYS_BSS;
static volatile uint8_t task_exec_lock;

static task_state_t __far *task_exec_state_ptr(void) {
	return (task_state_t __far *)mk_farptr(SEG_SYS, (uint16_t)&task_exec_state);
}

static bool acquire_task_exec_lock(kthread_id_t caller) {
	for(;;) {
		uint16_t flags;
		kthread_critical_enter(flags);
		if(!task_exec_lock) {
			task_exec_lock = 1;
			kthread_critical_exit(flags);
			return true;
		}
		kthread_critical_exit(flags);
		if(kthread_is_pending_termination(caller)) return false;
		kthread_yield();
	}
}

static void release_task_exec_lock(void) {
	uint16_t flags;
	kthread_critical_enter(flags);
	task_exec_lock = 0;
	kthread_critical_exit(flags);
}

static bool copy_task_path(registers_t *regs, char *filename, uint8_t *drive_no) {
	char __far *path = mk_farptr(regs->ds, regs->bx);
	uint16_t i;

	if(path[0] == 'A') *drive_no = 0;
	else if(path[0] == 'B') *drive_no = 1;
	else return false;
	if(path[1] != ':') return false;

	for(i = 0; i < MATFS16_FILENAME_MAXLEN; i++) {
		filename[i] = path[i + 2];
		if(filename[i] == '\0') break;
	}
	return i < MATFS16_FILENAME_MAXLEN && filename[0] != '\0';
}

static bool resolve_task_target(uint16_t selector, kthread_id_t caller,
	                            kthread_id_t *target, bool *is_self,
	                            uint16_t *segment, uint8_t *console) {
	if(selector == SYSCALL_TASK_SELF) *target = caller;
	else if(selector < SYSCALL_TASK_SLOT_COUNT)
		*target = KTHREAD_USER_TSK0_ID + (kthread_id_t)selector;
	else return false;

	if(*target < KTHREAD_USER_TSK0_ID || *target > KTHREAD_USER_TSK3_ID)
		return false;
	if(!kthread_task_info(*target, segment, console) || *console >= 4)
		return false;
	*is_self = *target == caller;
	return true;
}

static bool wait_for_task_exit(kthread_id_t target, kthread_id_t caller) {
	if(!kthread_task_is_active(target)) {
		kfiles_close_task_files(target);
		return true;
	}

	kthread_set_pending_termination(target, true);
	while(kthread_task_is_active(target)) {
		if(kthread_is_pending_termination(caller)) return false;
		kthread_yield();
	}
	kfiles_close_task_files(target);
	return true;
}

static void show_terminated_task(uint8_t console, char *message) {
	kconsole_init(console);
	kconsole_puts(console, message);
}

static void fail_exec_target(kthread_id_t target, bool is_self, uint8_t console) {
	if(is_self) {
		show_terminated_task(console, "Task could not be loaded; slot terminated.\r\n");
		task_terminate_current();
	}
	(void)wait_for_task_exit(target, kthread_cur_thread());
	show_terminated_task(console, "Task could not be loaded; slot empty.\r\n");
}

static void k_syscall_sys_write(registers_t *regs) {
	uint8_t console;
	uint16_t bytes_written;

	if(regs->bx > 2) {
		regs->ax = 0;
		if(regs->bx <= (uint16_t)KFILE_INVALID_FD + 2 &&
		   kfile_write((uint8_t)(regs->bx - 3), regs->cx, regs->ds, regs->dx, &bytes_written)) {
			regs->ax = bytes_written;
		}
		return;
	}

	if(!kthread_current_console(&console)) kpanic("User thread has no console!");

	if(regs->bx != 1) {
		kpanic("Attempted SYS_WRITE to unknown handle!"); // Probably want a generic error handler for user programs instead of always kpanic
	}
	char __far *buf = mk_farptr(regs->ds, regs->dx);
	for(int i = 0; i < regs->cx; i++) {
		if(buf[i]=='\n') {
			kconsole_puts(console,"\r\n");
		} else {
			kconsole_putc(console,buf[i]);
		}
	}
	regs->ax = regs->cx;
}

static void k_syscall_sys_read(registers_t *regs) {
	uint8_t console;
	uint16_t bytes_read;

	// actual file descriptors
	if(regs->bx > 2) {
		regs->ax = 0;
		if(regs->bx > (uint16_t)KFILE_INVALID_FD + 2) return;

		if(kfile_read((uint8_t)(regs->bx - 3), regs->cx, regs->ds, regs->dx, &bytes_read)) {
			regs->ax = bytes_read;
		}
		return;
	}

	// console IO
	if(!kthread_current_console(&console)) kpanic("User thread has no console!");

	if(regs->bx != 0) {
		kpanic("ATTEMPTED SYS_READ on unknown handle!");
	}
	char __far *buf = mk_farptr(regs->ds, regs->dx);
	uint16_t read = 0;
	char c;

	if(regs->cx == 0) {
		regs->ax = 0;
		return;
	}

	while(kconsole_input_available(console) == 0) {
		kthread_yield();
		if(kthread_is_pending_termination(kthread_cur_thread())) {
			regs->ax = 0;
			return;
		}
		if(kthread_is_pending_save_menu(kthread_cur_thread())) {
			regs->ax = 0;
			return;
		}
	}

	while(read < regs->cx && kconsole_getc(console,&c)) {
		buf[read] = c;
		read++;
	}

	regs->ax = read;
}

static void k_syscall_sys_get_console(registers_t *regs) {
	uint8_t console;

	if(kthread_current_console(&console)) regs->ax = console;
	else regs->ax = SYSCALL_CONSOLE_INVALID;
}

/* Load a file named in DS:BX into DS:DX. The filename begins with A: or B:. */
static void k_syscall_sys_load(registers_t *regs) {
	char __far *user_filename = mk_farptr(regs->ds, regs->bx);
	char filename[MATFS16_FILENAME_MAXLEN];
	matfs16_status_t status;
	uint8_t drive_no;
	uint16_t current_lba;
	uint16_t next_lba;
	uint16_t length;
	uint16_t block_count;
	uint16_t block_index;
	uint16_t destination = regs->dx;
	uint16_t i;

	regs->ax = 1;
	regs->cx = 0;

	if(user_filename[0] == 'A') drive_no = 0;
	else if(user_filename[0] == 'B') drive_no = 1;
	else return;
	if(user_filename[1] != ':') return;

	/* Copy the filename out of user memory before loading over the caller's
	 * destination buffer. MATFS16 names contain at most 25 characters plus
	 * their terminating NUL. */
	for(i = 0; i < MATFS16_FILENAME_MAXLEN; i++) {
		filename[i] = user_filename[i + 2];
		if(filename[i] == '\0') break;
	}
	if(i == MATFS16_FILENAME_MAXLEN || filename[0] == '\0') return;

	if(matfs16_cache_blockchain(drive_no) != MATFS16_OK) return;
	status = matfs16_locate_file(filename, drive_no, &current_lba, &length);
	if(status != MATFS16_OK) return;
	if(length == MATFS16_FILE_SIZE_UNKNOWN) return;

	block_count = length / MATFS16_BLOCK_SIZE;
	if(length % MATFS16_BLOCK_SIZE != 0) block_count++;

	/* matfs16_load_block() writes a complete sector, including the padding
	 * after the logical end of the file. Ensure every such write remains in
	 * the caller's 64 KiB segment. */
	if(block_count != 0) {
		if(destination > 0xFE00) return;
		if(block_count - 1 > (0xFE00 - destination) / MATFS16_BLOCK_SIZE) return;
	}

	for(block_index = 0; block_index < block_count; block_index++) {
		status = matfs16_load_block(drive_no, current_lba, regs->ds, destination);
		if(status != MATFS16_OK) return;

		status = matfs16_next_block(drive_no, current_lba, &next_lba);
		if(block_index + 1 == block_count) {
			if(status != MATFS16_END_OF_FILE) return;
		} else {
			if(status != MATFS16_OK) return;
			current_lba = next_lba;
			destination += MATFS16_BLOCK_SIZE;
		}
	}

	regs->cx = length;
	regs->ax = 0;
}

/* Open a file named in DS:BX. The filename begins with A: or B:. */
static void k_syscall_sys_open(registers_t* regs) {
	char __far* user_filename = mk_farptr(regs->ds, regs->bx);
	char filename[MATFS16_FILENAME_MAXLEN];
	uint8_t drive_num;
	uint8_t fd;
	uint16_t i;

	regs->ax = SYSCALL_INVALID_HANDLE;

	if(user_filename[0] == 'A') drive_num = 0;
	else if(user_filename[0] == 'B') drive_num = 1;
	else return;
	if(user_filename[1] != ':') return;

	for(i = 0; i < MATFS16_FILENAME_MAXLEN; i++) {
		filename[i] = user_filename[i + 2];
		if(filename[i] == '\0') break;
	}
	if(i == MATFS16_FILENAME_MAXLEN) return;

	if(matfs16_cache_blockchain(drive_num) != MATFS16_OK) return;
	fd = kfile_open(drive_num, filename);
	if(fd == KFILE_INVALID_FD) return;

	regs->ax = (uint16_t)fd + 3;
}

/* Create a new file named in DS:BX. The filename begins with A: or B:. */
static void k_syscall_sys_create(registers_t* regs) {
	char __far* user_filename = mk_farptr(regs->ds, regs->bx);
	char filename[MATFS16_FILENAME_MAXLEN];
	uint8_t drive_num;
	uint8_t fd;
	uint16_t i;

	regs->ax = SYSCALL_INVALID_HANDLE;
	if(user_filename[0] == 'A') drive_num = 0;
	else if(user_filename[0] == 'B') drive_num = 1;
	else return;
	if(user_filename[1] != ':') return;

	for(i = 0; i < MATFS16_FILENAME_MAXLEN; i++) {
		filename[i] = user_filename[i + 2];
		if(filename[i] == '\0') break;
	}
	if(i == MATFS16_FILENAME_MAXLEN || filename[0] == '\0') return;

	fd = kfile_create(drive_num, filename);
	if(fd == KFILE_INVALID_FD) return;
	regs->ax = (uint16_t)fd + 3;
}

/* Delete a file named in DS:BX. The filename begins with A: or B:. */
static void k_syscall_sys_delete(registers_t* regs) {
	char __far* user_filename = mk_farptr(regs->ds, regs->bx);
	char filename[MATFS16_FILENAME_MAXLEN];
	uint8_t drive_num;
	uint16_t i;

	regs->ax = 1;
	if(user_filename[0] == 'A') drive_num = 0;
	else if(user_filename[0] == 'B') drive_num = 1;
	else return;
	if(user_filename[1] != ':') return;

	for(i = 0; i < MATFS16_FILENAME_MAXLEN; i++) {
		filename[i] = user_filename[i + 2];
		if(filename[i] == '\0') break;
	}
	if(i == MATFS16_FILENAME_MAXLEN || filename[0] == '\0') return;

	regs->ax = kfile_delete(drive_num, filename) ? 0 : 1;
}

static void k_syscall_sys_close(registers_t* regs) {
	uint8_t drive_num;
	uint8_t fd;

	if(regs->bx < 3 || regs->bx > (uint16_t)KFILE_INVALID_FD + 2) {
		regs->ax = 1;
		return;
	}

	fd = (uint8_t)(regs->bx - 3);
	if(!kfile_close(fd, &drive_num)) {
		regs->ax = 1;
		return;
	}

	matfs16_invalidate_blockchain_cache(drive_num);
	regs->ax = matfs16_cache_blockchain(drive_num) == MATFS16_OK ? 0 : 1;
}

static void k_syscall_sys_copy(registers_t* regs) {
	uint8_t source_fd;
	uint8_t destination_fd;

	if(regs->bx < 3 || regs->bx > (uint16_t)KFILE_INVALID_FD + 2 ||
	   regs->cx < 3 || regs->cx > (uint16_t)KFILE_INVALID_FD + 2) {
		regs->ax = 1;
		return;
	}

	source_fd = (uint8_t)(regs->bx - 3);
	destination_fd = (uint8_t)(regs->cx - 3);
	regs->ax = kfile_copy(source_fd, destination_fd) ? 0 : 1;
}

static void k_syscall_sys_rename(registers_t* regs) {
	char __far* old_path = mk_farptr(regs->ds, regs->bx);
	char __far* new_path = mk_farptr(regs->ds, regs->dx);
	char old_name[MATFS16_FILENAME_MAXLEN];
	char new_name[MATFS16_FILENAME_MAXLEN];
	uint8_t drive_num;
	uint16_t i;

	regs->ax = 1;
	if(old_path[0] == 'A') drive_num = 0;
	else if(old_path[0] == 'B') drive_num = 1;
	else return;
	if(old_path[1] != ':' || new_path[0] != old_path[0] || new_path[1] != ':') return;

	for(i = 0; i < MATFS16_FILENAME_MAXLEN; i++) {
		old_name[i] = old_path[i + 2];
		if(old_name[i] == '\0') break;
	}
	if(i == MATFS16_FILENAME_MAXLEN || old_name[0] == '\0') return;

	for(i = 0; i < MATFS16_FILENAME_MAXLEN; i++) {
		new_name[i] = new_path[i + 2];
		if(new_name[i] == '\0') break;
	}
	if(i == MATFS16_FILENAME_MAXLEN || new_name[0] == '\0') return;

	regs->ax = kfile_rename(drive_num, old_name, new_name) ? 0 : 1;
}

static void k_syscall_sys_get_free_blocks(registers_t* regs) {
	matfs16_status_t status;
	uint16_t count;

	if(regs->bx > 1) {
		regs->ax = 0xFFFF;
		return;
	}
	status = matfs16_count_free_blocks((uint8_t)regs->bx, &count);
	regs->ax = status == MATFS16_OK ? count : 0xFFFF;
}

static void k_syscall_sys_write_block(registers_t* regs) {
	matfs16_status_t status;

	if(regs->bx > 1 || regs->cx >= MATFS16_BLOCK_COUNT || regs->dx > 0xFE00) {
		regs->ax = 1;
		return;
	}

	status = matfs16_store_block((uint8_t)regs->bx, regs->cx, regs->ds, regs->dx);
	regs->ax = (status == MATFS16_OK) ? 0 : 1;
}

static void k_syscall_sys_read_block(registers_t* regs) {
	matfs16_status_t status;

	if(regs->bx > 1 || regs->cx >= MATFS16_BLOCK_COUNT || regs->dx > 0xFE00) {
		regs->ax = 1;
		return;
	}

	status = matfs16_load_block((uint8_t)regs->bx, regs->cx, regs->ds, regs->dx);
	regs->ax = (status == MATFS16_OK) ? 0 : 1;
}

/* DS:BX names a TSK file; CX selects slot 0-3 or SYSCALL_TASK_SELF. */
static void k_syscall_sys_exec(registers_t *regs) {
	char filename[MATFS16_FILENAME_MAXLEN];
	uint8_t drive_no;
	uint8_t console;
	uint16_t segment;
	uint16_t first_lba;
	uint16_t file_length;
	uint16_t vram_segment;
	uint16_t i;
	kthread_id_t caller = kthread_cur_thread();
	kthread_id_t target;
	bool is_self;
	matfs16_status_t fs_status;
	task_status_t task_status;
	task_buffers_t buffers;
	task_state_t __far *state = task_exec_state_ptr();

	regs->ax = 1;
	if(!copy_task_path(regs, filename, &drive_no) ||
	   !resolve_task_target(regs->cx, caller, &target, &is_self,
	                        &segment, &console)) return;
	vram_segment = kconsole_vram_segment(console);
	if(vram_segment == VRAM_SEG_INVALID) return;

	/* A missing file leaves the old task alone. Other lookup errors terminate
	 * the target because its requested execution could not be completed safely. */
	fs_status = matfs16_cache_blockchain(drive_no);
	if(fs_status != MATFS16_OK) {
		fail_exec_target(target, is_self, console);
		return;
	}
	fs_status = matfs16_locate_file(filename, drive_no, &first_lba, &file_length);
	(void)file_length;
	if(fs_status == MATFS16_ERR_NOT_FOUND) return;
	if(fs_status != MATFS16_OK) {
		fail_exec_target(target, is_self, console);
		return;
	}
	/* Reserve AUX before stopping or overwriting the target task. */
	if(!kaux_claim_kernel(caller)) return;

	/* Let an external target finish any active syscall before discarding its
	 * kernel stack, so BIOS calls and file operations can release their locks. */
	if(!is_self && !wait_for_task_exit(target, caller)) {
		kaux_release_kernel(caller);
		return;
	}
	if(!acquire_task_exec_lock(caller)) {
		kaux_release_kernel(caller);
		return;
	}

	/* A concurrent exec may have filled this slot while we waited for the
	 * shared decompression state. Stop that new occupant before replacing it. */
	if(!is_self && kthread_task_is_active(target) &&
	   !wait_for_task_exit(target, caller)) {
		release_task_exec_lock();
		kaux_release_kernel(caller);
		return;
	}

	fs_status = matfs16_cache_blockchain(drive_no);
	if(fs_status != MATFS16_OK) {
		release_task_exec_lock();
		kaux_release_kernel(caller);
		fail_exec_target(target, is_self, console);
		return;
	}

	buffers.memory_segment = segment;
	buffers.vram.segment = vram_segment;
	buffers.vram.offset = 0;
	buffers.scratch.segment = SEG_SYS;
	buffers.scratch.offset = (uint16_t)task_exec_scratch;
	task_status = task_restore_claimed(drive_no, first_lba, &buffers, state);
	if(task_status != TASK_OK) {
		release_task_exec_lock();
		kaux_release_kernel(caller);
		fail_exec_target(target, is_self, console);
		return;
	}

	/* Restore the saved console before making a replacement slot runnable. */
	(void)kconsole_restore_state(console, &state->console);
	kfiles_close_task_files(target);
	if(is_self) {
		/* The syscall ISR owns a kernel-stack frame. Redirect its bridge to the
		 * restored user frame so LEAVE_KERNEL returns to the new task entry. */
		uint16_t __far *context = (uint16_t __far *)mk_farptr(segment,
		                                                    state->resume_sp);
		uint16_t *frame = (uint16_t *)regs;
		uint16_t *bridge = frame + 13;
		for(i = 0; i < 13; i++) frame[i] = context[i];
		bridge[0] = state->resume_sp;
		bridge[1] = segment;
		kthread_set_pending_save_menu(target, false);
	} else {
		kthread_setup_restored(target, segment, state->resume_sp, console);
		regs->ax = 0;
	}
	release_task_exec_lock();
	kaux_release_kernel(caller);
}

/* BX selects slot 0-3 or SYSCALL_TASK_SELF. */
static void k_syscall_sys_term(registers_t *regs) {
	uint8_t console;
	kthread_id_t caller = kthread_cur_thread();
	kthread_id_t target;
	bool is_self;

	regs->ax = 1;
	if(!resolve_task_target(regs->bx, caller, &target, &is_self,
	                        0, &console)) return;
	if(is_self) {
		show_terminated_task(console, "Task terminated.\r\n");
		task_terminate_current();
	}
	if(!kthread_task_is_active(target)) return;
	if(!wait_for_task_exit(target, caller)) return;
	show_terminated_task(console, "Task terminated.\r\n");
	regs->ax = 0;
}

static void k_syscall_sys_claim_aux(registers_t *regs) {
	regs->ax = kaux_claim_user(kthread_cur_thread()) ? 0 : 1;
}

static void k_syscall_sys_release_aux(registers_t *regs) {
	regs->ax = kaux_release_user(kthread_cur_thread()) ? 0 : 1;
}

void k_syscall_callback(registers_t *regs) {
	kthread_id_t thread = kthread_cur_thread();
	if(thread < KTHREAD_USER_TSK0_ID || thread > KTHREAD_USER_RSV_ID) kpanic("Syscall from non-user thread!");
	kthread_set_in_syscall(thread, true);

	switch(regs->ax) {
		case SYS_WRITE:
			k_syscall_sys_write(regs);
		break;
		case SYS_READ:
			k_syscall_sys_read(regs);
		break;
		case SYS_GET_CONSOLE:
			k_syscall_sys_get_console(regs);
		break;
		case SYS_LOAD:
			k_syscall_sys_load(regs);
		break;
		case SYS_OPEN:
			k_syscall_sys_open(regs);
		break;
		case SYS_CREATE:
			k_syscall_sys_create(regs);
		break;
		case SYS_DELETE:
			k_syscall_sys_delete(regs);
		break;
		case SYS_CLOSE:
			k_syscall_sys_close(regs);
		break;
		case SYS_COPY:
			k_syscall_sys_copy(regs);
		break;
		case SYS_RENAME:
			k_syscall_sys_rename(regs);
		break;
		case SYS_GET_FREE_BLOCKS:
			k_syscall_sys_get_free_blocks(regs);
		break;
		case SYS_WRITE_BLOCK:
			k_syscall_sys_write_block(regs);
		break;
		case SYS_READ_BLOCK:
			k_syscall_sys_read_block(regs);
		break;
		case SYS_EXEC:
			k_syscall_sys_exec(regs);
		break;
		case SYS_TERM:
			k_syscall_sys_term(regs);
		break;
		case SYS_CLAIM_AUX:
			k_syscall_sys_claim_aux(regs);
		break;
		case SYS_RELEASE_AUX:
			k_syscall_sys_release_aux(regs);
		break;
		default:
			kpanic("Unknown syscall");
		break;
	}
	kthread_set_in_syscall(thread, false);
	if(kthread_is_pending_termination(thread))
		task_terminate_current();
	if(kthread_is_pending_save_menu(thread))
		kthread_handoff_save_menu(thread, (uint16_t *)regs);
}
