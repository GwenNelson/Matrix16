; this is actually our kernel setup code for now

BITS 16
ORG 0

kernel_boot:
	mov al,'E'
	out 0xE9,al

	; setup the kernel's segments explicitly
	mov ax, 0x1000
	mov ds, ax
	mov es, ax

	mov al,'R'
	out 0xE9,al

	cli
	mov ss, ax
	mov sp, 0xFFFE ; our stack is at the end of this segment basically
	sti

	; now we can do shit
	times (1024 * 5) nop

	mov si,first_message
	call puts
	
	jmp $ ; temporary, cos we want to make sure the first sector load is working first
	times (1024 * 24) nop

	mov si,second_message
	call puts

	; let's just loop here forever now
	jmp $

; input is DS:SI
puts:
	lodsb ; equivalent to moving byte from DS:SI to AL, then incrementing SI
	test al,al
	jz .done

	mov ah, 0x0e
	mov bh, 0x00
	int 0x10
	jmp puts
.done:
	ret

first_message:
	db "Matrix16 Kernel started!",13,10,0

second_message:
	db "That worked apparently",13,10,0
