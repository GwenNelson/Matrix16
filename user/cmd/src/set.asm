BITS 16
ORG 0x8000

CMD_LINE equ 0x0100
ENV_START equ 0x0400
ENV_END equ 0x0800

SYS_WRITE equ 1

entry:
	cmp byte [CMD_LINE],0
	je list_environment

	call parse_assignment
	jc usage_error
	call update_environment
	jc full_error
	jmp near 0x0003

list_environment:
	mov si,ENV_START
.loop:
	cmp byte [si],0
	je exit
	call strout
	call nl
	; strout preserves SI; scan to the entry's NUL before continuing.
.advance:
	cmp byte [si],0
	je .next_entry
	inc si
	jmp .advance
.next_entry:
	inc si
	jmp .loop

parse_assignment:
	; The command line must contain a nonempty NAME followed by '='.
	mov si,CMD_LINE
	xor cx,cx
.scan:
	cmp byte [si],0
	je .invalid
	cmp byte [si],'='
	je .found_equals
	cmp byte [si],' '
	je .invalid
	inc si
	inc cx
	jmp .scan

.found_equals:
	test cx,cx
	jz .invalid
	mov [name_length],cx
	inc si
	mov [value_start],si
	xor cx,cx
.value_scan:
	cmp byte [si],0
	je .value_done
	inc si
	inc cx
	jmp .value_scan
.value_done:
	mov [value_length],cx
	clc
	ret
.invalid:
	stc
	ret

update_environment:
	; Locate the existing variable, or the first NUL that ends the list.
	mov di,ENV_START
.find_entry:
	cmp byte [di],0
	je .append
	mov [entry_start],di
	mov si,di
	xor cx,cx
.find_equals:
	cmp byte [si],0
	je .malformed
	cmp byte [si],'='
	je .compare_name
	inc si
	inc cx
	jmp .find_equals

.compare_name:
	cmp cx,[name_length]
	jne .next_entry
	mov [entry_equals],si
	mov si,CMD_LINE
	mov di,[entry_start]
	mov cx,[name_length]
	cld
	repe cmpsb
	jne .next_entry

	; Find the old entry's NUL and the environment's terminating double NUL.
	mov si,[entry_equals]
	inc si
.old_value_end:
	cmp byte [si],0
	je .old_entry_end
	inc si
	jmp .old_value_end
.old_entry_end:
	mov ax,si
	sub ax,[entry_start]
	inc ax
	mov [old_size],ax

	mov di,ENV_START
.find_environment_end:
	cmp byte [di],0
	je .environment_end_found
.scan_entry_end:
	cmp byte [di],0
	je .next_end_entry
	inc di
	jmp .scan_entry_end
.next_end_entry:
	inc di
	jmp .find_environment_end
.environment_end_found:
	mov [environment_end],di

	; tail begins after the matched entry and includes both terminating NULs.
	mov ax,[entry_start]
	add ax,[old_size]
	mov [tail_start],ax
	mov ax,di
	sub ax,[tail_start]
	add ax,2
	mov [tail_size],ax

	; Compute the replacement size: NAME, '=', VALUE, and its NUL.
	mov ax,[name_length]
	add ax,[value_length]
	add ax,2
	mov [new_size],ax

	; Ensure the new double-NUL end remains inside the environment region.
	mov ax,[environment_end]
	sub ax,[old_size]
	add ax,[new_size]
	inc ax
	cmp ax,ENV_END
	jae .no_space

	; Move the remaining entries and final double NUL to make room or close it.
	mov ax,[new_size]
	cmp ax,[old_size]
	je .write_entry
	ja .grow

.shrink:
	mov ax,[old_size]
	sub ax,[new_size]
	mov si,[tail_start]
	mov di,si
	sub di,ax
	mov cx,[tail_size]
	cld
	rep movsb
	jmp .write_entry

.grow:
	sub ax,[old_size]
	mov bx,ax
	mov si,[tail_start]
	add si,[tail_size]
	dec si
	mov di,si
	add di,bx
	mov cx,[tail_size]
	std
	rep movsb
	cld

.write_entry:
	; Copy NAME=VALUE\0 into the space at entry_start.
	mov di,[entry_start]
	mov si,CMD_LINE
	mov cx,[name_length]
	cld
	rep movsb
	mov al,'='
	stosb
	mov si,[value_start]
	mov cx,[value_length]
	rep movsb
	xor al,al
	stosb
	clc
	ret

.next_entry:
	mov di,[entry_start]
.next_entry_scan:
	cmp byte [di],0
	je .next_entry_start
	inc di
	jmp .next_entry_scan
.next_entry_start:
	inc di
	jmp .find_entry

.append:
	; The current DI points at the first NUL. Preserve the second NUL after
	; the new entry and reject an update that would run past ENV_END.
	mov [entry_start],di
	mov ax,[name_length]
	add ax,[value_length]
	add ax,2
	mov [new_size],ax
	add ax,di
	cmp ax,ENV_END
	jae .no_space

	mov di,[entry_start]
	mov si,CMD_LINE
	mov cx,[name_length]
	cld
	rep movsb
	mov al,'='
	stosb
	mov si,[value_start]
	mov cx,[value_length]
	rep movsb
	xor al,al
	stosb
	clc
	ret

.malformed:
.no_space:
	stc
	ret

strout:
	push ax
	push bx
	push cx
	push dx
	push si

	mov dx,si
	xor cx,cx
.length:
	cmp byte [si],0
	je .write
	inc si
	inc cx
	jmp .length
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

nl:
	push ax
	push bx
	push cx
	push dx
	mov ax,SYS_WRITE
	mov bx,1
	mov dx,newline
	mov cx,1
	int 0x80
	pop dx
	pop cx
	pop bx
	pop ax
	ret

usage_error:
	mov si,usage_message
	call strout
	call nl
	jmp exit

full_error:
	mov si,full_message
	call strout
	call nl

exit:
	jmp near 0x0003

name_length:
	dw 0
value_length:
	dw 0
value_start:
	dw 0
entry_start:
	dw 0
entry_equals:
	dw 0
old_size:
	dw 0
new_size:
	dw 0
environment_end:
	dw 0
tail_start:
	dw 0
tail_size:
	dw 0

newline:
	db 10
usage_message:
	db "Usage: SET NAME=VALUE",0
full_message:
	db "Environment full or invalid",0
