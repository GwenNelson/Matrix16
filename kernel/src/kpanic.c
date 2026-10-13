#include "bioscons.h"
#include "kpanic.h"

void kpanic(char *msg) {
	bios_puts("Kernel panic!\n");
	bios_puts("\tREASON: ");
	bios_puts(msg);
	bios_puts("\n\n\n");
	bios_puts("IT IS IMPOSSIBLE TO CONTINUE, SYSTEM HALTING\n");
	for(;;)
		asm("cli; hlt");
}
