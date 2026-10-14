BITS 16

section .text.entry

extern format_main
extern __bss_start
extern __bss_end

global format_entry

format_entry:
	; The shell has already provided a valid stack in this segment. Set up
	; the data registers required by ordinary C code without replacing it.
	cli
	mov ax, cs
	mov ds, ax
	mov es, ax

	; The flat command binary does not contain its NOLOAD .bss bytes.
	xor ax, ax
	mov di, __bss_start
	mov cx, __bss_end
	sub cx, di
	cld
	rep stosb

	sti
	call format_main
	jmp 0x0003
