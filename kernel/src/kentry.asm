; this is actually our kernel setup code for now

BITS 16

extern kernel_main
extern __bss_start
extern __bss_end
extern __aux_bss_start
extern __aux_bss_end
extern SEG_SYS
extern SEG_KERN
extern kernel_stack_top

global kernel_start

kernel_start:
	; setup the kernel's segments explicitly
	mov ax, 0x1000
	mov ds, ax
	mov es, ax

	cli
	mov ss, ax
	mov sp, kernel_stack_top

	; Auxiliary BSS has offsets within SEG_SYS and no bytes in the loaded
	; kernel image. Clear it through ES before returning ES to SEG_KERN.
	mov ax, SEG_SYS
	mov es, ax
	mov di, __aux_bss_start
	mov cx, __aux_bss_end
	sub cx, di
	cld
	rep stosb

	; The loader also omits ordinary BSS. DS still addresses SEG_KERN, and ES
	; must be restored to SEG_KERN before clearing it and entering kernel C.
	mov ax, SEG_KERN
	mov es, ax
	xor ax, ax
	mov di, __bss_start
	mov cx, __bss_end
	sub cx, di
	rep stosb

	call kernel_main

.hang:
	cli
	hlt
	jmp .hang
