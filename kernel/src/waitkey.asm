; wait_for_key
; Discard pending BIOS keystrokes, then wait
; for a fresh keypress.
;
; Requires BIOS keyboard IRQ handler to be active.
; Clobbers: AX, FLAGS
bits 16

global bios_wait_for_key

bios_wait_for_key:
	pushf
	sti
.flush:
	mov ah, 0x01
	int 0x16             ; Check keyboard buffer
	jz .wait             ; Empty? Wait for new key

	xor ah, ah
	int 0x16             ; Discard pending key
	jmp .flush

.wait:
	xor ah, ah
	int 0x16             ; Block until key pressed
	popf
	ret
