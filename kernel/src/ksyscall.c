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
	char __far *buf = mk_farptr(regs->ds, regs->cx);
	for(int i = 0; i < regs->dx; i++) {
		kconsole_putc(console,buf[i]);
//		bios_putchar(buf[i], console);
	}
}

void k_syscall_callback(registers_t *regs) {
	kthread_id_t thread = kthread_cur_thread();
	if(thread < KTHREAD_USER_TSK0_ID || thread > KTHREAD_USER_RSV_ID) kpanic("Syscall from non-user thread!");

	switch(regs->ax) {
		case SYS_WRITE:
			k_syscall_sys_write(regs);
		break;
		default:
			kpanic("Unknown syscall");
		break;
	}
}
