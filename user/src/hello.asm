BITS 16
ORG 0

start:
	; setup our segments first
	mov ax,cs
	mov ds,ax

	mov ax, 1              ; SYS_WRITE
	mov bx, 1              ; stdout
	mov dx, message
	mov cx, message_end - message
	int 0x80

	mov ax, 1              ; SYS_WRITE
	mov bx, 1              ; stdout
	mov dx, console_message
	mov cx, console_message_end - console_message
	int 0x80

.hang:
	hlt
	jmp .hang

message:
	db 0x1b,"[?25l"
	db "Hello from Matrix16! You are on console 0",10,0
message_end:

console_message:
	db "Try Ctrl-Alt-F1 through Ctrl-Alt-F4 to switch consoles.",10,10
	db "You can also press ctrl-alt-S to open the save menu",10,10
	db "BASIC is on console 1, and shell on the other consoles",10
console_message_end:
