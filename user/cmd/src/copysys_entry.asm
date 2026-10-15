BITS 16

section .text.entry

extern copysys_main
extern __bss_start
extern __bss_end

global copysys_entry

copysys_entry:
	cli
	mov ax, cs
	mov ds, ax
	mov es, ax

	; Clear the command's NOLOAD data before calling C.
	xor ax, ax
	mov di, __bss_start
	mov cx, __bss_end
	sub cx, di
	cld
	rep stosb

	sti
	call copysys_main
	jmp 0x0003
