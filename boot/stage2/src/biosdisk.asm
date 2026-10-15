bits 16
; uint16_t bios_read_sector(uint16_t es, uint16_t bx,
;                           uint16_t cylinder, uint16_t head,
;                           uint16_t sector, uint16_t drive);

global bios_read_sector
global bios_read_sectors

bios_read_sector:
    push bp
    mov  bp, sp

    push bx
    push es

    mov  ax, [bp+4]      ; ES
    mov  es, ax
    mov  bx, [bp+6]      ; BX destination offset

    mov  ch, [bp+8]      ; cylinder
    mov  dh, [bp+10]     ; head
    mov  cl, [bp+12]     ; sector
    mov  dl, [bp+14]     ; drive

    mov  ax, 0x0201      ; AH=02 read, AL=1 sector
    int  0x13
    jc   .error

    xor  ax, ax           ; return 0
    jmp  .done

.error:
    mov  al, ah           ; BIOS error code
    xor  ah, ah           ; return it as uint16_t

.done:
    pop  es
    pop  bx
    pop  bp
    ret

; Read count sectors with INT 13h AH=02. The C caller keeps the request
; within one track and within the destination segment.
bios_read_sectors:
    push bp
    mov  bp, sp

    push bx
    push es

    mov  ax, [bp+4]      ; ES
    mov  es, ax
    mov  bx, [bp+6]      ; BX destination offset

    mov  ch, [bp+8]      ; cylinder
    mov  dh, [bp+10]     ; head
    mov  cl, [bp+12]     ; sector
    mov  dl, [bp+14]     ; drive
    mov  al, [bp+16]     ; sector count (at most nine here)
    mov  ah, 0x02        ; BIOS read sectors
    int  0x13
    jc   .multi_error

    cmp  al, [bp+16]
    jne  .short_transfer
    xor  ax, ax
    jmp  .multi_done

.multi_error:
    mov  al, ah           ; BIOS error code
    xor  ah, ah
    jmp  .multi_done

.short_transfer:
    mov  ax, 0x0100       ; generic error: short transfer

.multi_done:
    pop  es
    pop  bx
    pop  bp
    ret
