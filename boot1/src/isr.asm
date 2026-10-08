bits 16

global install_isr08
global install_isr09
global install_isr80

extern k_timer_callback
extern kconsole_kb_callback
extern k_syscall_callback

section .data
old_timer_off dw 0
old_timer_seg dw 0

install_isr80:
	xor ax,ax
	mov es,ax

	cli
	mov word [es:0x0200], isr80_wrapper
	mov word [es:0x0202], cs
	sti
	ret


install_isr08:
	; save segment registers we mess with
	push ds
	push es

	; point ES to segment 0x0000
	xor ax,ax
	mov es,ax

	; IVT offset....
	mov di, 0x0020  ; IVT offset for ISR 08h (PIT)

	cli

	; save the old BIOS nonsense
	mov ax, [es:0x0020]
	mov [old_timer_off], ax
	mov ax, [es:0x0022]
	mov [old_timer_seg], ax

	mov word [es:di],   isr08_wrapper ; store offset
	mov word [es:di+2], cs            ; store segment
	sti

	pop es
	pop ds
	ret

install_isr09:
	pushf
	cli
	push ax
	push es
	xor ax, ax
	mov es, ax
	mov word [es:0x0024], isr09_wrapper ; IRQ1 vector
	mov word [es:0x0026], cs
	pop es
	pop ax
	popf
	ret

isr08_wrapper:
	push ax
	push cx
	push dx
	push bx
	mov ax, sp
	add ax, 8
	push ax
	push bp
	push si
	push di
	push ds
	push es

	; enter kernel segment (CS is set for us by the CPU to whatever we stored in IVT)
	mov ax, cs
	mov ds, ax
	mov es, ax

	mov al, 0x20
	out 0x20, al          ; PIC EOI before a possible context switch
	call k_timer_callback

	pop es
	pop ds
	pop di
	pop si
	pop bp
	add sp, 2
	pop bx
	pop dx
	pop cx
	pop ax

	iret

isr09_wrapper:
	push ax
	push bx
	push cx
	push dx
	push si
	push di
	push bp
	push ds
	push es

	mov ax, cs
	mov ds, ax
	mov es, ax
	in al, 0x60
	xor ah, ah
	push ax
	call kconsole_kb_callback
	add sp, 2

	; Release the XT keyboard latch, then acknowledge IRQ1 at the PIC.
	in al, 0x61
	mov ah, al
	or al, 0x80
	out 0x61, al
	mov al, ah
	out 0x61, al
	mov al, 0x20
	out 0x20, al

	pop es
	pop ds
	pop bp
	pop di
	pop si
	pop dx
	pop cx
	pop bx
	pop ax
	iret

isr80_wrapper:
	push ax
	push cx
	push dx
	push bx
	mov ax, sp
	add ax, 8
	push ax
	push bp
	push si
	push di
	push ds
	push es

	mov ax, cs
	mov ds, ax
	mov es, ax

	mov ax, sp
	push ax

	call k_syscall_callback

	add sp,2

	pop es
	pop ds
	pop di
	pop si
	pop bp
	add sp, 2
	pop bx
	pop dx
	pop cx
	pop ax
	iret
