bits 16

global switch_to

section .text

; void switch_to(uint16_t ss, uint16_t sp, uint16_t *old_ss,
;                uint16_t *old_sp, uint16_t first_run)
; A new thread's SP points to the interrupt frame built by kthread_setup().
; An existing thread's SP points to this routine's saved C call frame.
switch_to:
	push bp
	mov bp, sp
	push bx
	push si
	push di
	push ds
	push es

	mov si, [bp + 8]       ; where to save the outgoing SS
	test si, si
	jz .load
	mov di, [bp + 10]      ; where to save the outgoing SP
	mov ax, ss
	mov [si], ax
	mov ax, sp
	mov [di], ax

	.load:
	mov cx, [bp + 12]      ; nonzero for a new thread
	mov ax, [bp + 4]       ; target SS
	mov dx, [bp + 6]       ; target SP
	mov ss, ax
	mov sp, dx
	test cx, cx
	jz .resume

	; Match isr08_wrapper's register epilogue before IRET.
	pop es
	pop ds
	pop di
	pop si
	pop bp
	add sp, 2              ; synthetic saved-SP slot
	pop bx
	pop dx
	pop cx
	pop ax
	iret

.resume:
	pop es
	pop ds
	pop di
	pop si
	pop bx
	pop bp
	ret
