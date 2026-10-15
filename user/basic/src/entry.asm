BITS 16

section .text.entry

extern main
extern __bss_start
extern __bss_end

global basic_entry

basic_entry:
	cli
	mov ax, cs
	mov ds, ax
	mov es, ax
	mov ss, ax
	mov sp, 0xFFFE

	; The flat binary omits NOLOAD data. Clear the interpreter's fixed program,
	; variable, and scratch buffers before entering C.
	xor ax, ax
	mov di, __bss_start
	mov cx, __bss_end
	sub cx, di
	cld
	rep stosb

	sti
	call main

.halt:
	jmp .halt
