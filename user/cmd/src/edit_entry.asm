BITS 16

section .text.entry

extern edit_main
extern __bss_start
extern __bss_end

global edit_entry

edit_entry:
	cli
	mov ax, cs
	mov ds, ax
	mov es, ax

	; Clear the command's static buffer and other NOLOAD data.
	xor ax, ax
	mov di, __bss_start
	mov cx, __bss_end
	sub cx, di
	cld
	rep stosb

	sti
	call edit_main
	jmp 0x0003
