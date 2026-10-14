BITS 16
ORG 0x8000

	; CLS implementation goes here. The shell calls offset 8000h with a near
	; CALL, so this temporary stub simply returns to it.
	ret
