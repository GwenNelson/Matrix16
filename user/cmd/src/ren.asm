BITS 16
ORG 0x8000

CMD_LINE equ 0x0100
SYS_WRITE equ 1
SYS_RENAME equ 12
MAX_FILENAME_LEN equ 25
PATH_SIZE equ 28

entry:
	call load_default_drive
	mov si, CMD_LINE
	call skip_spaces
	mov di, source_path
	mov al, [default_drive]
	call parse_path
	jc usage_error

	call skip_spaces
	mov di, destination_path
	mov al, [source_path]
	call parse_path
	jc usage_error
	call skip_spaces
	cmp byte [si], 0
	jne usage_error

	mov ax, SYS_RENAME
	mov bx, source_path
	mov dx, destination_path
	int 0x80
	test ax, ax
	jnz rename_error

	mov dx, success_message
	mov cx, success_message_end - success_message
	jmp print_message

usage_error:
	mov dx, usage_message
	mov cx, usage_message_end - usage_message
	jmp print_message

rename_error:
	mov dx, error_message
	mov cx, error_message_end - error_message

print_message:
	mov ax, SYS_WRITE
	mov bx, 1
	int 0x80
	jmp near 0x0003

; CWD supplies the default source drive; A: is used if CWD is unavailable.
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

; Parse one filename token. AL is the default drive letter. A valid explicit
; A: or B: prefix overrides it. Carry signals an invalid or empty name.
parse_path:
	mov [di], al
	inc di
	mov byte [di], ':'
	inc di
	mov al, [si]
	cmp byte [si + 1], ':'
	jne .copy_name
	cmp al, 'A'
	je .explicit_drive
	cmp al, 'B'
	jne .copy_name
.explicit_drive:
	mov [di - 2], al
	add si, 2

.copy_name:
	xor cx, cx
.copy_loop:
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
	jmp .copy_loop
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
	db "Usage: REN [A:|B:]oldname [A:|B:]newname", 10
usage_message_end:
error_message:
	db "Rename failed; check names, drive, and open files", 10
error_message_end:
success_message:
	db "File renamed", 10
success_message_end:

default_drive:
	db 'A'
cwd_name:
	db 'CWD', 0
source_path:
	times PATH_SIZE db 0
destination_path:
	times PATH_SIZE db 0
