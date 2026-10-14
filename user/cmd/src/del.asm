BITS 16
ORG 0x8000

CMD_LINE equ 0x0100
SYS_WRITE equ 1
SYS_DELETE equ 10

entry:
	mov si, CMD_LINE

.skip_spaces:
	cmp byte [si], ' '
	jne .start
	inc si
	jmp .skip_spaces

.start:
	cmp byte [si], 0
	je usage_error

	mov di, filename
	mov byte [di], 'A'
	mov byte [di + 1], ':'
	add di, 2

	cmp byte [si + 1], ':'
	jne .copy_name
	cmp byte [si], 'A'
	je .use_drive_prefix
	cmp byte [si], 'B'
	jne usage_error

.use_drive_prefix:
	mov al, [si]
	mov [filename], al
	add si, 2

.copy_name:
	xor cx, cx
.copy_loop:
	lodsb
	test al, al
	je .name_done
	cmp al, ' '
	je usage_error
	cmp al, ':'
	je usage_error
	cmp cx, 25
	jae usage_error
	stosb
	inc cx
	jmp .copy_loop

.name_done:
	test cx, cx
	je usage_error
	mov byte [di], 0

	mov ax, SYS_DELETE
	mov bx, filename
	int 0x80
	or ax, ax
	jnz delete_error

	mov ax, SYS_WRITE
	mov bx, 1
	mov dx, deleted_message
	mov cx, deleted_message_end - deleted_message
	int 0x80
	jmp exit

usage_error:
	mov ax, SYS_WRITE
	mov bx, 1
	mov dx, usage_message
	mov cx, usage_message_end - usage_message
	int 0x80
	jmp exit

delete_error:
	mov ax, SYS_WRITE
	mov bx, 1
	mov dx, error_message
	mov cx, error_message_end - error_message
	int 0x80

exit:
	jmp near 0x0003

usage_message:
	db "Usage: DEL [A:|B:]filename", 10
usage_message_end:

error_message:
	db "Unable to delete file", 10
error_message_end:

deleted_message:
	db "File deleted", 10
deleted_message_end:

filename:
	db "A:"
	times 26 db 0
