BITS 16

section .text.entry

extern main
extern __bss_start
extern __bss_end

global shell_start

shell_start:
	; A restored task begins with all of its segments referring to the same
	; 64 KiB task image. Establish that environment explicitly for C and put
	; the stack at the top of the segment before enabling interrupts.
	cli
	mov ax, cs
	mov ds, ax
	mov es, ax
	mov ss, ax
	mov sp, 0xFFFE

	; The flat binary contains initialized data only, so clear the NOLOAD .bss
	; range before calling C.
	xor ax, ax
	mov di, __bss_start
	mov cx, __bss_end
	sub cx, di
	cld
	rep stosb

	sti
	call main

.hang:
	jmp .hang
