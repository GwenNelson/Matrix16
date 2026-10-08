; this is actually our kernel setup code for now

BITS 16

extern kernel_main
extern __bss_start
extern __bss_end

global kernel_start

kernel_start:
	; setup the kernel's segments explicitly
	mov ax, 0x1000
	mov ds, ax
	mov es, ax

	cli
	mov ss, ax
	mov sp, 0xFFFE ; our stack is at the end of this segment basically

	; The loader copies the file image, not the zero-filled .bss section.
	; DS and ES already address SEG_KERN; clear it before entering C.
	xor ax, ax
	mov di, __bss_start
	mov cx, __bss_end
	sub cx, di
	cld
	rep stosb

	call kernel_main

.hang:
	cli
	hlt
	jmp .hang
