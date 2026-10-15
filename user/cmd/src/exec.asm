BITS 16
ORG 0x8000

CMD_LINE equ 0x0100
MAX_FILENAME_LEN equ 25
PATH_SIZE equ 28

SYS_WRITE equ 1
SYS_EXEC equ 14
TASK_SELF equ 0x00FF

entry:
	call load_default_drive
	mov si, CMD_LINE
	call skip_spaces
	mov di, filename
	call parse_path
	jc usage_error

	call skip_spaces
	cmp byte [si], 0
	je .run_in_shell

	mov al, [si]
	cmp al, '0'
	jb usage_error
	cmp al, '4'
	ja usage_error
	inc si
	call skip_spaces
	cmp byte [si], 0
	jne usage_error

	xor cx, cx
	sub al, '0'
	mov cl, al
	jmp .run

.run_in_shell:
	mov cx, TASK_SELF

.run:
	mov ax, SYS_EXEC
	mov bx, filename
	int 0x80
	test ax, ax
	jnz exec_error

	; Replacing the current shell normally does not return. This path is for
	; successful execution in another task slot.
	mov dx, success_message
	mov cx, success_message_end - success_message
	jmp print_message

usage_error:
	mov dx, usage_message
	mov cx, usage_message_end - usage_message
	jmp print_message

exec_error:
	mov dx, error_message
	mov cx, error_message_end - error_message

print_message:
	mov ax, SYS_WRITE
	mov bx, 1
	int 0x80
	jmp near 0x0003

; Use CWD as the default drive, falling back to A: if it is absent or invalid.
load_default_drive:
	mov byte [default_drive], 'A'
	mov ax, cwd_name
	push ax
	call 0x0006
	add sp, 2
	test ax, ax
	jz .done
	mov si, ax
	mov al, [si]
	cmp al, 'A'
	je .check_suffix
	cmp al, 'B'
	jne .done
.check_suffix:
	cmp byte [si + 1], ':'
	jne .done
	cmp byte [si + 2], 0
	jne .done
	mov [default_drive], al
.done:
	ret

skip_spaces:
	cmp byte [si], ' '
	jne .done
	inc si
	jmp skip_spaces
.done:
	ret

; Copy one filename token into DS:DI. An explicit A: or B: prefix overrides
; CWD; carry reports an empty or invalid token.
parse_path:
	mov al, [si]
	cmp byte [si + 1], ':'
	jne .use_default_drive
	cmp al, 'A'
	je .explicit_drive
	cmp al, 'B'
	je .explicit_drive

.use_default_drive:
	mov al, [default_drive]
	mov [di], al
	inc di
	mov byte [di], ':'
	inc di
	jmp .copy_filename

.explicit_drive:
	mov [di], al
	inc di
	mov byte [di], ':'
	inc di
	add si, 2

.copy_filename:
	xor cx, cx
.copy_character:
	mov al, [si]
	test al, al
	je .finish
	cmp al, ' '
	je .finish
	cmp al, ':'
	je .invalid
	cmp cx, MAX_FILENAME_LEN
	jae .invalid
	mov [di], al
	inc di
	inc si
	inc cx
	jmp .copy_character

.finish:
	test cx, cx
	jz .invalid
	mov byte [di], 0
	clc
	ret

.invalid:
	stc
	ret

usage_message:
	db "Usage: EXEC filename [task-slot 0-4]", 10
usage_message_end:
error_message:
	db "Unable to execute that task", 10
error_message_end:
success_message:
	db "Task started in selected slot", 10
success_message_end:

default_drive:
	db 'A'
cwd_name:
	db 'CWD', 0
filename:
	times PATH_SIZE db 0
