BITS 16

section .text.entry

extern diskusage_main
extern __bss_start
extern __bss_end

global diskusage_entry

diskusage_entry:
	cli
	mov ax, cs
	mov ds, ax
	mov es, ax

	xor ax, ax
	mov di, __bss_start
	mov cx, __bss_end
	sub cx, di
	cld
	rep stosb

	sti
	call diskusage_main
	jmp 0x0003
