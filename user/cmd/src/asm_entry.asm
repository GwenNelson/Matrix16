BITS 16

section .text.entry

extern asm_main
extern __bss_start
extern __bss_end

global asm_entry

asm_entry:
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
	call asm_main
	jmp 0x0003
