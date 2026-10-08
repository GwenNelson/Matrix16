bits 16

global switch_to

section .text

; void switch_to(uint16_t ss, uint16_t sp)
; The target SP must point to an interrupt-return frame:
;   ES, DS, DI, SI, BP, saved-SP, BX, DX, CX, AX, IP, CS, FLAGS
switch_to:
	push bp
	mov bp, sp

	mov ax, [bp + 4]       ; target SS
	mov dx, [bp + 6]       ; target SP
	mov ss, ax
	mov sp, dx
	iret
