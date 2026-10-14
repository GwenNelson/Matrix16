BITS 16
ORG 0x8000

MAX_FILENAME_LEN equ 26
FILE_BUF_SIZE equ 512  ; unlike TYPE.ASM, this is EXACT
CMD_LINE equ 0x0100

SYS_WRITE equ 1
SYS_READ  equ 2

SYS_OPEN  equ 5
SYS_CLOSE equ 6


entry:
	call clear_buf

	; let's first check if we have A: or B: or a null string
	call check_cmdline

	; if we get here, let's open the directory
	mov bx, CMD_LINE
	mov ax, SYS_OPEN
	int 0x80

	; check for errors
	cmp ax,-1
	je open_error

	mov word [fd_var],ax

.loop:
	mov bx, word [fd_var]

	; now do the SYS_READ of the first block
	mov dx, file_buf
	mov cx, FILE_BUF_SIZE
	mov ax, SYS_READ
	int 0x80

	; ax should now have the number of bytes read - if not exactly 1 block, we're done cos something broke
	cmp ax,FILE_BUF_SIZE
	jne .done

	; if we get here, we should have the directory block!
	; we should find the 15 directory entries beginning after the 32-byte header
	mov si,file_buf+32
	call dump_entries

	; loop back round
	jmp .loop

.done:
	; if we get here, we should be able to now just close the file and we're done
	mov bx, word [fd_var]
	mov ax, SYS_CLOSE
	int 0x80

	call nl
	jmp exit

dump_entries:
	mov cx,15
	; si has the address of 15 entries, cx is our "remaining" counter
.loop:
	push si
	add si,6

	cmp byte [si],0
	je .advance

	push cx
	call strout
	call nl
	pop cx

.advance:
	pop si
	add si,32

	dec cx
	test cx,cx
	jnz .loop

	ret

usage_error:
	mov ax,SYS_WRITE
	mov bx,1
	mov dx,usage_error_str
	mov cx,usage_error_str_end-usage_error_str
	int 0x80
	jmp exit

open_error:
	mov ax,SYS_WRITE
	mov bx,1
	mov dx,open_error_str
	mov cx,open_error_str_end-open_error_str
	int 0x80
	jmp exit

exit:
	jmp near 0x0003

nl:
	mov ax,SYS_WRITE
	mov bx,1
	mov dx,nlstr
	mov cx,1
	int 0x80
	ret

clear_buf:
	cld
	push ax
	push cx
	push di
	push es

	push ds
	pop es  
	xor ax,ax
	mov di,file_buf
	mov cx, FILE_BUF_SIZE
	rep stosb

	pop es
	pop di
	pop cx
	pop ax
	ret

check_cmdline:
	mov al, [CMD_LINE]

	test al, al
	jz .empty

	cmp al, 'A'
	je .check_colon
	cmp al, 'B'
	je .check_colon
	jmp .invalid

.check_colon:
	cmp byte [CMD_LINE+1], ':'
	jne .invalid

	cmp byte [CMD_LINE+2], 0
	jne .invalid

	jmp .valid

.empty:
	mov byte [CMD_LINE], 'A'
	mov byte [CMD_LINE+1], ':'
	mov byte [CMD_LINE+2], 0
	jmp .valid

.invalid:
	jmp usage_error
.valid:
	ret

strout:
	push ax
	push bx
	push cx
	push dx
	push si

	mov dx,si
	xor cx,cx
.loop:
	cmp byte [si],0
	je .write
	inc si
	inc cx
	jmp .loop

.write:
	test cx,cx
	jz .done
	mov ax,SYS_WRITE
	mov bx,1
	int 0x80

.done:
	pop si
	pop dx
	pop cx
	pop bx
	pop ax
	ret

strcpy:
	cld
.loop:
	lodsb
	stosb
	test al, al
	jnz .loop
	ret

fd_var:
	dw 0

usage_error_str:
	db "Usage: DIR [A: | B:]",10
usage_error_str_end:

open_error_str:
	db "Error opening file",10
open_error_str_end:

nlstr:
	db 10

filename_buf:
	db "A:"
	resb MAX_FILENAME_LEN

file_buf:
	resb FILE_BUF_SIZE
