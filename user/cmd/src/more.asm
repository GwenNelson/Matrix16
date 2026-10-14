BITS 16
ORG 0x8000

CMD_LINE equ 0x0100
PAGE_SIZE equ (80 * 24) - 1

SYS_WRITE equ 1
SYS_READ equ 2
SYS_OPEN equ 5
SYS_CLOSE equ 6

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

	mov ax, SYS_OPEN
	mov bx, filename
	int 0x80
	cmp ax, 0xffff
	je open_error
	mov [file_handle], ax

.read_page:
	mov ax, SYS_READ
	mov bx, [file_handle]
	mov cx, PAGE_SIZE
	mov dx, page_buffer
	int 0x80
	test ax, ax
	jz .close_file
	mov [page_length], ax

	mov ax, SYS_WRITE
	mov bx, 1
	mov cx, [page_length]
	mov dx, page_buffer
	int 0x80

	cmp word [page_length], PAGE_SIZE
	jne .close_file

	; The leading LF keeps the prompt below this byte-sized page.
	mov ax, SYS_WRITE
	mov bx, 1
	mov cx, more_prompt_end - more_prompt
	mov dx, more_prompt
	int 0x80

	; Console input uses handle zero; any one byte continues the display.
	mov ax, SYS_READ
	xor bx, bx
	mov cx, 1
	mov dx, key_buffer
	int 0x80

	; Erase the prompt without adding another line before the next page.
	mov ax, SYS_WRITE
	mov bx, 1
	mov cx, clear_prompt_end - clear_prompt
	mov dx, clear_prompt
	int 0x80
	jmp .read_page

.close_file:
	mov ax, SYS_CLOSE
	mov bx, [file_handle]
	int 0x80
	jmp exit

usage_error:
	mov ax, SYS_WRITE
	mov bx, 1
	mov cx, usage_message_end - usage_message
	mov dx, usage_message
	int 0x80
	jmp exit

open_error:
	mov ax, SYS_WRITE
	mov bx, 1
	mov cx, error_message_end - error_message
	mov dx, error_message
	int 0x80

exit:
	jmp near 0x0003

file_handle:
	dw 0
page_length:
	dw 0
key_buffer:
	db 0

usage_message:
	db "Usage: MORE [A:|B:]filename", 10
usage_message_end:
error_message:
	db "Error opening file", 10
error_message_end:
more_prompt:
	db 10, "--more--"
more_prompt_end:
clear_prompt:
	db 13, "        ", 13
clear_prompt_end:

filename:
	db "A:"
	times 26 db 0
page_buffer:
	times PAGE_SIZE db 0
