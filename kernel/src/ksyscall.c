#include "bioscons.h"
#include "kpanic.h"
#include "ksyscall.h"
#include "kthread.h"
#include "kconsole.h"
#include "utils.h"

static void k_syscall_sys_write(registers_t *regs) {
	uint8_t console;
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
}

static void k_syscall_sys_read(registers_t *regs) {
	uint8_t console;
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
		default:
			kpanic("Unknown syscall");
		break;
	}
}
