#include "bioscons.h"
#include "kpanic.h"
#include "ksyscall.h"
#include "kthread.h"
#include "kconsole.h"
#include "kfiles.h"
#include "matfs16.h"
#include "utils.h"

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

	while(kconsole_input_available(console) == 0) kthread_yield();

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
	if(regs->bx < 3 || regs->bx > (uint16_t)KFILE_INVALID_FD + 2) {
		regs->ax = 1;
		return;
	}

	regs->ax = kfile_close((uint8_t)(regs->bx - 3)) ? 0 : 1;
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

void k_syscall_callback(registers_t *regs) {
	kthread_id_t thread = kthread_cur_thread();
	if(thread < KTHREAD_USER_TSK0_ID || thread > KTHREAD_USER_RSV_ID) kpanic("Syscall from non-user thread!");

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
		case SYS_WRITE_BLOCK:
			k_syscall_sys_write_block(regs);
		break;
		case SYS_READ_BLOCK:
			k_syscall_sys_read_block(regs);
		break;
		default:
			kpanic("Unknown syscall");
		break;
	}
}
