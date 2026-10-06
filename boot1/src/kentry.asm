; this is actually our kernel setup code for now

BITS 16

extern kernel_main

kernel_boot:
	; setup the kernel's segments explicitly
	mov ax, 0x1000
	mov ds, ax
	mov es, ax

	cli
	mov ss, ax
	mov sp, 0xFFFE ; our stack is at the end of this segment basically

	call kernel_main

.hang:
	cli
	hlt
	jmp .hang

