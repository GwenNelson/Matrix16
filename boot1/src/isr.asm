bits 16

global install_isr08
global install_isr80

extern k_timer_callback
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

isr08_wrapper:
	pusha
	push ds
	push es

	; enter kernel segment (CS is set for us by the CPU to whatever we stored in IVT)
	mov ax, cs
	mov ds, ax
	mov es, ax

	call k_timer_callback


;	mov al, 0x20
;	out 0x20, al          ; PIC EOI	

	pop es
	pop ds
	popa

	jmp far [cs:old_timer_off]
	iret

isr80_wrapper:
	pusha
	push ds
	push es

	mov ax, cs
	mov ds, ax
	mov es, ax

	push sp

	call k_syscall_callback

	add sp,2

	pop es
	pop ds
	popa
	iret
