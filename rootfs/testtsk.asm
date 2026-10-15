BITS 16
ORG 0

SYS_WRITE equ 1

start:
	mov ax, cs
	mov ds, ax
	mov ax, SYS_WRITE
	mov bx, 1                  ; stdout
	mov cx, message_end - message
	mov dx, message
	int 0x80

halt:
	hlt
	jmp halt

message:
	db "Hello, I am a task", 13, 10
message_end:
