BITS 16
ORG 0x8000

CMD_LINE equ 0x0100
SYS_WRITE equ 1
SYS_TERM equ 15

entry:
	mov si, CMD_LINE

.skip_leading_spaces:
	cmp byte [si], ' '
	jne .parse_slot
	inc si
	jmp .skip_leading_spaces

.parse_slot:
	mov al, [si]
	cmp al, '0'
	jb usage_error
	cmp al, '4'
	ja usage_error
	inc si

.skip_trailing_spaces:
	cmp byte [si], ' '
	jne .check_end
	inc si
	jmp .skip_trailing_spaces

.check_end:
	cmp byte [si], 0
	jne usage_error

	xor bx, bx
	sub al, '0'
	mov bl, al
	mov ax, SYS_TERM
	int 0x80
	test ax, ax
	jnz terminate_error

	mov dx, success_message
	mov cx, success_message_end - success_message
	jmp print_message

usage_error:
	mov dx, usage_message
	mov cx, usage_message_end - usage_message
	jmp print_message

terminate_error:
	mov dx, error_message
	mov cx, error_message_end - error_message

print_message:
	mov ax, SYS_WRITE
	mov bx, 1
	int 0x80
	jmp near 0x0003

usage_message:
	db "Usage: TERMINATE task-slot (0-4)", 10
usage_message_end:

error_message:
	db "Unable to terminate that task", 10
error_message_end:

success_message:
	db "Task terminated", 10
success_message_end:
