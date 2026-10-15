BITS 16
ORG 0x8000

SYS_WRITE equ 1
SHELL_RETURN_VECTOR equ 0x0003

start:
	mov ax, SYS_WRITE
	mov bx, 1                  ; stdout
	mov cx, message_end - message
	mov dx, message
	int 0x80
	jmp SHELL_RETURN_VECTOR

message:
	db "Hello from a .CMD", 13, 10
message_end:
