BITS 16
ORG 0x8000

; this is kinda dumb, but it's a good test
call cls_start
jmp near 0x0003

cls_start:
        mov ax, 1
        mov bx, 1
        mov dx, cls_seq
        mov cx, cls_seq_end - cls_seq
        int 0x80

	ret

cls_seq:
	db 0x1B, "[2J", 0x1B, "[H"
cls_seq_end:
