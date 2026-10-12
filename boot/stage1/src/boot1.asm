; This is the boot floppy for Matrix16
; It's basically just a boot sector and then the kernel, when compiled this should produce a 380 KiB floppy image

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

	; now let's load the kernel into the right place
	; our kernel is at C=0 H=0 S=2 onwards, but BIOS INT 13h is not zero indexed annoyingly
	; we want to load to es:bx, so let's set that up first

	mov ax,1000h
	mov es,ax

	; let's put the remaining sectors into SI cos it's convenient
	mov si, KERNEL_SECTORS

	; now setup the BIOS params
	mov ch, 0 ; cyl 0
	mov cl, 2 ; sector 2 (1 is bootsector)
	mov dh, 0 ; head 0
	mov dl, 0 ; drive 0 (A:)
	mov bx, 0 ; offset into BX

.read_next:
	mov ah,02h ; read sectors call
	mov al,01h ; number of sectors to read

	push si ; save SI so BIOS can't fuck with it
	int 0x13
	pop si ; restore SI here
	jc .error

	dec si
	jz .done   ; if SI==0, we're done

	add bx,512  ; move along data buffer

	inc cl      ; move to next sector
	cmp cl,10   ; if CL < 10, continue (valid sector, no need to move head/cylinder)
	jl .check_done

.next_head:
	mov cl, 1       ; reset sector back to 1
	inc dh          ; move to next head
	cmp dh, 2       ; if we're < head 2, (e.g still H=0 or H=1) continue
	jl .check_done

.next_cyl:
	mov dh, 0	; reset head back to 0
	inc ch 		; move to next cylinder

.check_done:
	; we don't actually need to check anything here as such
	jmp .read_next


.done:
	; far jump into the kernel
	jmp word 0x1000:0000

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


; the actual kernel itself now (on all remaining sectors)
kernel_start:
incbin "build/kernel.bin"
kernel_end:

KERNEL_BYTES   equ kernel_end - kernel_start
KERNEL_SECTORS equ (KERNEL_BYTES + 511) / 512

; pad out the image

times (360 * 1024) - ($ - $$) db 0
