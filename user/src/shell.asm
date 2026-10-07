BITS 16
ORG 0

start:
	; setup our segments first
	mov ax,cs
	mov ds,ax

    mov ax, 1              ; SYS_WRITE
    mov bx, 1              ; stdout
    mov cx, message
    mov dx, message_end - message
    int 0x80

.hang:
    jmp .hang

message:
    db "Matrix16 Shell> "
message_end:
