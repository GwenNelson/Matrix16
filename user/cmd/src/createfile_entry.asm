BITS 16

section .text.entry

extern createfile_main
extern __bss_start
extern __bss_end

global createfile_entry

createfile_entry:
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
	call createfile_main
	jmp 0x0003
