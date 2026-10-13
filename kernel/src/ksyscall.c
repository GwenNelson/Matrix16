#include "bioscons.h"
#include "kpanic.h"
#include "ksyscall.h"
#include "utils.h"

static void k_syscall_sys_write(registers_t *regs) {
	if(regs->bx != 1) {
		kpanic("Attempted SYS_WRITE to unknown handle!"); // Probably want a generic error handler for user programs instead of always kpanic
	}
	char __far *buf = mk_farptr(regs->ds, regs->cx);
	for(int i = 0; i < regs->dx; i++) {
		bios_putchar(buf[i], cur_page_no); // TODO - should check what page no the task is assigned
	}
}

void k_syscall_callback(registers_t *regs) {
	switch(regs->ax) {
		case SYS_WRITE:
			k_syscall_sys_write(regs);
		break;
		default:
			kpanic("Unknown syscall");
		break;
	}
}
