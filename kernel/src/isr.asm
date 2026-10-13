bits 16

global install_isr08
global install_isr09
global install_isr80

extern k_timer_callback
extern kconsole_kb_callback
extern k_syscall_callback
extern cur_thread
extern user_kernel_stack_tops

section .data
old_timer_off dw 0
old_timer_seg dw 0

; All three entries use the same 26-byte register frame. The CPU has already
; pushed FLAGS, CS, IP (at SS:SP, in that order of increasing addresses: IP,
; CS, FLAGS). These pushes add ES, DS, DI, SI, BP, a discarded original-SP
; slot, BX, DX, CX, AX ahead of that CPU frame.
%macro SAVE_FRAME 0
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
%endmacro

%macro ENTER_KERNEL 0
	; The register frame is still on the interrupted stack. DS must address
	; kernel data before looking up the running task. SS decides only whether
	; a transition is needed; cur_thread selects the owner's private stack.
	mov ax, cs
	mov ds, ax
	mov bx, ss
	cmp bx, ax
	je %%on_kernel

	xor bx, bx
	mov bl, [cur_thread]
	shl bx, 1
	mov di, [user_kernel_stack_tops + bx]
	mov dx, ss             ; original user SS
	mov si, sp             ; offset of the original 26-byte frame
	mov es, dx             ; ES can read that frame after SS changes
	mov ss, ax
	mov sp, di             ; immediately after MOV SS, on this task's stack
	push dx                ; bridge: user SS
	push si                ; bridge: user frame SP
	lea si, [si + 24]
	mov cx, 13
%%copy_in:
	mov ax, [es:si]
	push ax                ; copy backwards to retain the frame's layout
	sub si, 2
	loop %%copy_in
	mov ax, cs
	mov es, ax
	mov ax, 1
	push ax                ; transition marker below the copied frame
	jmp %%ready

%%on_kernel:
	; A syscall or kernel thread may have live C frames here. Never reset SP.
	mov es, ax
	xor ax, ax
	push ax                ; same marker slot, without moving the frame
%%ready:
%endmacro

%macro LEAVE_KERNEL 0
	pop bx                 ; discard the transition marker
	test bx, bx
	jz %%restore

	; Copy any syscall register changes back to the original user frame.
	; The bridge belongs to this suspended kernel call chain, so a timer
	; switch cannot confuse it with another task's saved user SS:SP.
	mov si, sp
	mov di, [si + 26]      ; original user frame SP
	mov dx, [si + 28]      ; original user SS
	mov es, dx
	mov cx, 13
%%copy_out:
	mov ax, [si]
	mov [es:di], ax
	add si, 2
	add di, 2
	loop %%copy_out
	mov bx, [si]           ; bridge's user frame SP
	mov dx, [si + 2]       ; bridge's user SS
	mov ss, dx
	mov sp, bx             ; IRET itself cannot restore SS:SP

%%restore:
	pop es
	pop ds
	pop di
	pop si
	pop bp
	add sp, 2              ; saved original-SP slot
	pop bx
	pop dx
	pop cx
	pop ax
	iret                   ; restores IP, CS, FLAGS from the CPU frame
%endmacro

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
	SAVE_FRAME
	ENTER_KERNEL

	; The saved BIOS INT 08h handler returns through its IRET to this
	; wrapper. It performs the timer acknowledgement before scheduling.
	pushf
	call far [old_timer_off]
	mov ax, cs
	mov ds, ax
	mov es, ax
	call k_timer_callback
	LEAVE_KERNEL

isr09_wrapper:
	SAVE_FRAME
	ENTER_KERNEL
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

	LEAVE_KERNEL

isr80_wrapper:
	SAVE_FRAME
	ENTER_KERNEL

	mov ax, sp
	add ax, 2              ; skip marker; C receives the register frame
	push ax

	call k_syscall_callback

	add sp,2

	LEAVE_KERNEL
