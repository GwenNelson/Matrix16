bits 16
; uint16_t bios_read_sector(uint16_t es, uint16_t bx,
;                           uint16_t cylinder, uint16_t head,
;                           uint16_t sector, uint16_t drive);
; uint16_t bios_write_sector(uint16_t es, uint16_t bx,
;                            uint16_t cylinder, uint16_t head,
;                            uint16_t sector, uint16_t drive);

global bios_read_sector
global bios_read_sectors

bios_read_sector:
    push bp
    mov  bp, sp

    ; BIOS implementations may clobber registers beyond AX. Keep the caller's
    ; C register and data-segment state intact around the interrupt.
    push bx
    push es
    push ds
    push cx
    push dx
    push si
    push di

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
    pop  di
    pop  si
    pop  dx
    pop  cx
    pop  ds
    pop  es
    pop  bx
    pop  bp
    ret

; Read count sectors. The caller must keep the transfer within one track and
; one 64 KiB segment. Return nonzero for a BIOS error or a short transfer.
bios_read_sectors:
    push bp
    mov  bp, sp

    ; Match the single-sector wrapper's preservation of the C environment.
    push bx
    push es
    push ds
    push cx
    push dx
    push si
    push di

    mov  ax, [bp+4]      ; ES
    mov  es, ax
    mov  bx, [bp+6]      ; BX destination offset

    mov  ch, [bp+8]      ; cylinder
    mov  dh, [bp+10]     ; head
    mov  cl, [bp+12]     ; sector
    mov  dl, [bp+14]     ; drive
    mov  al, [bp+16]     ; sector count
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
    mov  ax, 0x0100       ; generic error: BIOS transferred too few sectors

.multi_done:
    pop  di
    pop  si
    pop  dx
    pop  cx
    pop  ds
    pop  es
    pop  bx
    pop  bp
    ret

global bios_write_sector

bios_write_sector:
    push bp
    mov  bp, sp

    ; Keep the kernel C environment intact across BIOS implementations that
    ; clobber registers beyond the documented result in AX.
    push bx
    push es
    push ds
    push cx
    push dx
    push si
    push di

    mov  ax, [bp+4]      ; ES
    mov  es, ax
    mov  bx, [bp+6]      ; BX source offset

    mov  ch, [bp+8]      ; cylinder
    mov  dh, [bp+10]     ; head
    mov  cl, [bp+12]     ; sector
    mov  dl, [bp+14]     ; drive

    mov  ax, 0x0301      ; AH=03 write, AL=1 sector
    int  0x13
    jc   .write_error

    xor  ax, ax           ; return 0
    jmp  .write_done

.write_error:
    mov  al, ah           ; return BIOS error code
    xor  ah, ah

.write_done:
    pop  di
    pop  si
    pop  dx
    pop  cx
    pop  ds
    pop  es
    pop  bx
    pop  bp
    ret
