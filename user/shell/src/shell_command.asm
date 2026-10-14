BITS 16

section .text.shell_command

global shell_run_command
global shell_return

shell_run_command:
	; This routine is entered with a near CALL from the shell. Save SP while
	; it still points at that CALL's return address. shell_return can then
	; restore this exact context and RET to the shell's caller.
	mov [cs:saved_ax], ax
	mov [cs:saved_bx], bx
	mov [cs:saved_cx], cx
	mov [cs:saved_dx], dx
	mov [cs:saved_si], si
	mov [cs:saved_di], di
	mov [cs:saved_bp], bp
	mov [cs:saved_sp], sp
	mov [cs:saved_ds], ds
	mov [cs:saved_es], es
	mov [cs:saved_ss], ss

	pushf
	pop ax
	mov [cs:saved_flags], ax
	mov ax, [cs:saved_ax]

	; The shell has already loaded the flat command image here. JMP keeps the
	; original shell return address at the saved stack position.
	jmp 0x8000

shell_return:
	; A command reaches this routine through the fixed vector at 0003h. Do
	; not use the command's current stack: it may be in the middle of a call
	; chain. MOV SS followed immediately by MOV SP restores the saved stack
	; without allowing an interrupt between the two instructions.
	cli
	mov ax, [cs:saved_ss]
	mov ss, ax
	mov sp, [cs:saved_sp]

	mov ds, [cs:saved_ds]
	mov es, [cs:saved_es]
	mov bx, [cs:saved_bx]
	mov cx, [cs:saved_cx]
	mov dx, [cs:saved_dx]
	mov si, [cs:saved_si]
	mov di, [cs:saved_di]
	mov bp, [cs:saved_bp]

	; Restore FLAGS after the stack is valid. Preserve AX across the temporary
	; push so the shell receives precisely the context it had before launch.
	mov ax, [cs:saved_flags]
	push ax
	popf
	mov ax, [cs:saved_ax]
	ret

section .bss.shell_command nobits alloc noexec write align=2

saved_ax:	resw 1
saved_bx:	resw 1
saved_cx:	resw 1
saved_dx:	resw 1
saved_si:	resw 1
saved_di:	resw 1
saved_bp:	resw 1
saved_sp:	resw 1
saved_ds:	resw 1
saved_es:	resw 1
saved_ss:	resw 1
saved_flags:	resw 1
