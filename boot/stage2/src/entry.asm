BITS 16

extern stage2_main
extern __bss_start
extern __bss_end

global stage2_start

global boot_drive_no

stage2_start:
	; setup the segments, we live in the AUX segment, 7000h
	mov ax, 0x7000
	mov ds, ax
	mov es, ax

	; now store the boot drive number, it should have been preserved for us by stage1
	mov [boot_drive_no], dl

	cli
	mov ss, ax
	mov sp, 0xFFFE ; our stack is at the end of this segment basically

	; The loader copies the file image, not the zero-filled .bss section.
	; DS and ES already address SEG_KERN; clear it before entering C.
	xor ax, ax
	mov di, __bss_start
	mov cx, __bss_end
	sub cx, di
	cld
	rep stosb

	call stage2_main

.hang:
	cli
	hlt
	jmp .hang

boot_drive_no:
	db 0
