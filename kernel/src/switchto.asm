bits 16

global switch_to
global copy_initial_frame
global copy_words_between_segments

section .text

; void copy_initial_frame(uint16_t seg, uint16_t offset,
;                         const uint16_t *frame)
; GCC-IA16 needs DS=SS=SEG_KERN in C. Copy through ES so an initial user
; interrupt frame can be placed in its own SS without changing C's DS.
copy_initial_frame:
	push bp
	mov bp, sp
	push es
	push si
	push di
	push cx
	pushf
	mov ax, [bp + 4]
	mov es, ax
	mov di, [bp + 6]
	mov si, [bp + 8]
	mov cx, 13
	cld
	rep movsw
	popf
	pop cx
	pop di
	pop si
	pop es
	pop bp
	ret

; void copy_words_between_segments(uint16_t source_seg, uint16_t dest_seg,
;                                  uint16_t word_count)
; Copy from offset zero in one segment to offset zero in another. This can
; copy a complete 64 KiB task image with word_count=0x8000.
copy_words_between_segments:
	push bp
	mov bp, sp
	push ds
	push es
	push si
	push di
	push cx
	pushf

	mov ax, [bp + 4]
	mov ds, ax
	mov ax, [bp + 6]
	mov es, ax
	xor si, si
	xor di, di
	mov cx, [bp + 8]
	cld
	rep movsw

	popf
	pop cx
	pop di
	pop si
	pop es
	pop ds
	pop bp
	ret

; void switch_to(uint16_t ss, uint16_t sp, uint16_t *old_ss,
;                uint16_t *old_sp, uint16_t first_run)
; A new thread's SP points to an initial or restored interrupt frame.
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
