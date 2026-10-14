BITS 16
ORG 0x8000

SYS_WRITE equ 1
SYS_READ equ 2

entry:
    mov word [current], help_text
    mov word [remaining], help_text_end - help_text

.next_page:
    cmp word [remaining], 0
    je .exit

    mov ax, [current]
    mov [page_start], ax
    mov word [line_count], 0

    ; Find a page boundary at 24 newline characters, or at the end of text.
.scan:
    cmp word [remaining], 0
    je .write_page

    mov si, [current]
    cmp byte [si], 10
    jne .advance
    inc word [line_count]

.advance:
    inc word [current]
    dec word [remaining]
    cmp word [line_count], 24
    jb .scan

.write_page:
    mov dx, [page_start]
    mov ax, [current]
    sub ax, dx
    mov cx, ax
    mov ax, SYS_WRITE
    mov bx, 1
    int 0x80

    cmp word [remaining], 0
    je .exit

    mov ax, SYS_WRITE
    mov bx, 1
    mov dx, more_text
    mov cx, more_text_end - more_text
    int 0x80

    ; SYS_READ uses handle 0 for console input in the current kernel ABI.
    mov ax, SYS_READ
    xor bx, bx
    mov dx, key_buffer
    mov cx, 1
    int 0x80

    ; Erase the prompt in place. Advancing to another line here would scroll
    ; the screen before the next page, hiding a line at each page boundary.
    mov ax, SYS_WRITE
    mov bx, 1
    mov dx, more_clear
    mov cx, more_clear_end - more_clear
    int 0x80
    jmp .next_page

.exit:
    jmp near 0x0003

current:
    dw 0
remaining:
    dw 0
page_start:
    dw 0
line_count:
    dw 0
key_buffer:
    db 0

more_text:
    db "--more--"
more_text_end:
more_clear:
    db 13, "        ", 13
more_clear_end:

help_text:
    incbin "src/help.txt"
help_text_end:
