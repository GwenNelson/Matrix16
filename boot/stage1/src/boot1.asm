; This is the bootsector AKA stage1 bootloader

BITS 16
ORG 0x7C00

start:
	; setup segment registers so DS:0x7C00-ish refs makes sense
	xor ax,ax
	mov ds,ax

	; setup our stack
	cli
	mov ss,ax
	mov sp,0x7c00

	; setup our flags correctly
	cld

	; we want interrupts thanks
	sti
	
	; check if we're in A:
	cmp dl,0
	jne .wrong_drive

	mov si,starting_msg
	call puts

	; now let's load stage2 into the right place
	; it's at C=0 H=0 S=2 onwards, but BIOS INT 13h is not zero indexed annoyingly
	; we want to load to es:bx, so let's set that up first

	mov ax,7000h
	mov es,ax

	; now setup the BIOS params
	mov ch, 0 ; cyl 0
	mov cl, 2 ; sector 2 (1 is bootsector)
	mov dh, 0 ; head 0
	mov dl, 0 ; drive 0 (A:)
	mov bx, 0 ; offset into BX
	mov ah,02h ; read sectors call
	mov al,08h ; number of sectors to read
	int 0x13
	jc .error
	jmp .done

.done:
	; far jump into the stage2 bootloader
	jmp word 0x7000:0000

.error:
	mov si,fail_msg
	call puts
	jmp $

.wrong_drive:
	mov si,wrong_drive_msg
	call puts
	jmp $ ; infinite loop

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

starting_msg:
	db "Starting Matrix16...",13,10,0
wrong_drive_msg:
	db "Wrong drive, insert in A:, reboot",0
fail_msg:
	db 13,10,"ERROR READING DISK",0

	
; pad out the bootsector
times 510 - ($ - $$) db 0

; BIOS boot signature
dw 0xAA55
