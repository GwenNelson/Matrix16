BITS 16
ORG 0x8000

FILE_BUF_SIZE equ 32
CMD_ENV equ 0x0400 

SYS_WRITE equ 1
SYS_READ  equ 2

SYS_OPEN  equ 5
SYS_CLOSE equ 6


entry:
	call clear_buf
	call nl
	; first we need to open the file 
	mov bx,CMD_ENV ; filename is just in the environment, a C string
	mov ax,SYS_OPEN
	int 0x80
	
	; check for errors
	cmp ax,-1
	je open_error

	mov word [fd_var],ax
	; fd should now be stored in fd_var, and in AX

.loop:
	mov bx, word [fd_var]

	; now do the SYS_READ
	mov dx, file_buf
	mov cx, FILE_BUF_SIZE
	mov ax, SYS_READ
	int 0x80

	; ax should now have the number of bytes read

	; so let's see if it's got anything left,
	test ax,ax
	jz .done

	mov cx, ax
	
	; setup other params for SYS_WRITE
	mov bx, 1
	mov dx, file_buf
	mov ax, SYS_WRITE
	int 0x80

	jmp .loop

.done:
	; if we get here, we should be able to now just close the file and we're done
	mov bx, word [fd_var]
	mov ax, SYS_CLOSE
	int 0x80

	jmp exit

open_error:
	mov ax,SYS_WRITE
	mov bx,1
	mov dx,open_error_str
	mov cx,open_error_str_end-open_error_str
	int 0x80

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

fd_var:
	dw 0

open_error_str:
	db "Error opening file",10
open_error_str_end:

nlstr:
	db 10

file_buf:
	resb FILE_BUF_SIZE
