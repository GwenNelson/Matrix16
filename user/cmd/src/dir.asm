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

	; Select a drive and a filename pattern. A: is the default drive.
	call parse_cmdline

	; if we get here, let's open the directory
	mov bx, drive_name
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
	push cx
	push si
	add si,6

	cmp byte [si],0
	je .advance

	mov di,[pattern_ptr]
	call matches_pattern
	jc .advance

	call strout
	call nl

.advance:
	pop si
	pop cx
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


; Parse an optional A:/B: prefix and leave the filename pattern in place.
; Without a prefix, use the shell's CWD; an empty pattern means all files.
parse_cmdline:
	mov byte [drive_name], 'A'
	mov si, CMD_LINE
	mov word [pattern_ptr], all_files_pattern

	; getenv is exposed through the shell's fixed near-call vector at 0006h.
	mov ax, cwd_name
	push ax
	call 0x0006
	add sp, 2
	test ax, ax
	jz .check_args
	mov si, ax
	mov al, [si]
	cmp al, 'A'
	je .use_cwd
	cmp al, 'B'
	jne .check_args
.use_cwd:
	mov [drive_name], al

.check_args:

	mov al, [CMD_LINE]
	test al, al
	jz .done

	cmp byte [CMD_LINE+1], ':'
	jne .pattern_only

	cmp al, 'A'
	je .drive_prefix
	cmp al, 'B'
	jne .invalid_drive

.drive_prefix:
	mov [drive_name], al
	add si, 2
	cmp byte [si], 0
	je .done
	mov [pattern_ptr], si
	ret

.pattern_only:
	mov [pattern_ptr], si
	ret

.invalid_drive:
	jmp usage_error

.done:
	ret

; Match a NUL-terminated directory name at SI against the pattern at DI.
; '*' matches zero or more characters and '?' matches exactly one.
; Carry is clear for a match and set for no match.
matches_pattern:
	push ax
	push bx
	push si
	push di
	mov word [star_pattern], 0
	mov word [star_name], 0

.match:
	mov al, [di]
	cmp al, '*'
	jne .not_star
	inc di
	mov [star_pattern], di
	mov [star_name], si
	jmp .match

.not_star:
	mov bl, [si]
	test al, al
	je .pattern_end
	cmp al, '?'
	je .question
	cmp al, bl
	je .advance
	jmp .try_star

.question:
	test bl, bl
	jz .try_star

.advance:
	inc si
	inc di
	jmp .match

.pattern_end:
	test bl, bl
	jz .matched

.try_star:
	cmp word [star_pattern], 0
	je .not_matched
	mov si, [star_name]
	cmp byte [si], 0
	je .not_matched
	inc si
	mov [star_name], si
	mov di, [star_pattern]
	jmp .match

.matched:
	clc
	jmp .return

.not_matched:
	stc

.return:
	pop di
	pop si
	pop bx
	pop ax
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
	db "Usage: DIR [A:|B:][pattern]",10
usage_error_str_end:

open_error_str:
	db "Error opening file",10
open_error_str_end:

nlstr:
	db 10

drive_name:
	db 'A', ':', 0
cwd_name:
	db 'CWD', 0
all_files_pattern:
	db '*', 0
pattern_ptr:
	dw 0
star_pattern:
	dw 0
star_name:
	dw 0

file_buf:
	resb FILE_BUF_SIZE
