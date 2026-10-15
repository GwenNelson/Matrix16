BITS 16
ORG 0x8000

CMD_LINE equ 0x0100
IO_BUFFER_SIZE equ 512
MAX_FILENAME_LEN equ 25
PATH_BUFFER_SIZE equ 28

SYS_WRITE equ 1
SYS_READ  equ 2
SYS_OPEN  equ 5
SYS_CLOSE equ 6
SYS_CREATE equ 9

entry:
	call load_default_drive

	mov si, CMD_LINE
	call skip_spaces
	mov di, source_path
	call parse_path
	jc usage_error

	call skip_spaces
	cmp byte [si], 0
	je usage_error
	mov di, destination_path
	call parse_destination
	jc usage_error

	call skip_spaces
	cmp byte [si], 0
	jne usage_error

	; Open the source first. Opening the destination with SYS_CREATE will fail
	; if a file with that name already exists, so COPY never overwrites files.
	mov ax, SYS_OPEN
	mov bx, source_path
	int 0x80
	cmp ax, 0xFFFF
	je open_error
	mov [source_handle], ax
	mov byte [source_open], 1

	mov ax, SYS_CREATE
	mov bx, destination_path
	int 0x80
	cmp ax, 0xFFFF
	je create_error
	mov [destination_handle], ax
	mov byte [destination_open], 1

.copy_loop:
	mov ax, SYS_READ
	mov bx, [source_handle]
	mov cx, IO_BUFFER_SIZE
	mov dx, io_buffer
	int 0x80
	test ax, ax
	jz .copy_done
	mov [bytes_read], ax

	mov ax, SYS_WRITE
	mov bx, [destination_handle]
	mov cx, [bytes_read]
	mov dx, io_buffer
	int 0x80
	cmp ax, [bytes_read]
	jne copy_error
	jmp .copy_loop

.copy_done:
	call close_open_files
	cmp byte [close_error], 0
	jne copy_error_message
	mov ax, SYS_WRITE
	mov bx, 1
	mov dx, success_message
	mov cx, success_message_end - success_message
	int 0x80
	jmp exit

usage_error:
	mov ax, SYS_WRITE
	mov bx, 1
	mov dx, usage_message
	mov cx, usage_message_end - usage_message
	int 0x80
	jmp exit

open_error:
	mov ax, SYS_WRITE
	mov bx, 1
	mov dx, open_error_message
	mov cx, open_error_message_end - open_error_message
	int 0x80
	jmp exit

create_error:
	call close_open_files
	mov ax, SYS_WRITE
	mov bx, 1
	mov dx, create_error_message
	mov cx, create_error_message_end - create_error_message
	int 0x80
	jmp exit

copy_error:
	call close_open_files

copy_error_message:
	mov ax, SYS_WRITE
	mov bx, 1
	mov dx, copy_error_text
	mov cx, copy_error_text_end - copy_error_text
	int 0x80
	jmp exit

; Close whichever handles were successfully opened. A close error is saved so
; the second handle is still closed before the caller reports the failure.
close_open_files:
	mov byte [close_error], 0
	cmp byte [destination_open], 0
	je .close_source
	mov byte [destination_open], 0
	mov ax, SYS_CLOSE
	mov bx, [destination_handle]
	int 0x80
	test ax, ax
	jz .close_source
	mov byte [close_error], 1

.close_source:
	cmp byte [source_open], 0
	je .done
	mov byte [source_open], 0
	mov ax, SYS_CLOSE
	mov bx, [source_handle]
	int 0x80
	test ax, ax
	jz .done
	mov byte [close_error], 1

.done:
	ret

; Use the shell's getenv vector to obtain CWD. Keep A: as a fallback if the
; variable is absent or does not contain one of the supported drive names.
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
	je .check_drive_suffix
	cmp al, 'B'
	jne .done

.check_drive_suffix:
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

; Parse one unquoted filename token into DS:DI. An explicit A: or B: prefix
; wins; otherwise the drive from CWD is added. Carry reports invalid input.
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

; A destination containing only A: or B: inherits the source filename.
parse_destination:
	mov al, [si]
	cmp byte [si + 1], ':'
	jne parse_path
	cmp al, 'A'
	je .check_drive_only
	cmp al, 'B'
	jne parse_path

.check_drive_only:
	cmp byte [si + 2], 0
	je .same_name
	cmp byte [si + 2], ' '
	jne parse_path

.same_name:
	mov [di], al
	inc di
	mov byte [di], ':'
	inc di
	add si, 2
	mov bx, source_path + 2

.copy_filename:
	mov al, [bx]
	mov [di], al
	inc bx
	inc di
	test al, al
	jnz .copy_filename
	clc
	ret

exit:
	jmp near 0x0003

usage_message:
	db "Usage: COPY source destination", 10
usage_message_end:

open_error_message:
	db "Unable to open source file", 10
open_error_message_end:

create_error_message:
	db "Unable to create destination; it may already exist", 10
create_error_message_end:

copy_error_text:
	db "Copy failed; destination may be incomplete", 10
copy_error_text_end:

success_message:
	db "Copy complete", 10
success_message_end:

default_drive:
	db 'A'
cwd_name:
	db 'CWD', 0
source_open:
	db 0
destination_open:
	db 0
close_error:
	db 0
source_handle:
	dw 0
destination_handle:
	dw 0
bytes_read:
	dw 0

source_path:
	times PATH_BUFFER_SIZE db 0
destination_path:
	times PATH_BUFFER_SIZE db 0
io_buffer:
	times IO_BUFFER_SIZE db 0
