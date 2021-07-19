%include "bootloader.inc"

SECTION MBR vstart=0x7c00
    mov ax, cs
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov fs, ax
    mov sp, 0x7c00
    mov ax, 0xb800
    mov es, ax

; clear screen, us int 0x10 volume up all row
; AH function number = 0x06
; AL = volume up line count (0 is all)
; BH = line attribute
; CL:CH = Left-Up coordinate
; DL:DH = Right-Down coordinate
    mov ax, 0x0600
    mov bx, 0x0700
    mov cx, 0               ; (0, 0)
    mov dx, 0x184f          ; (80, 25)
    int 0x10

; print the string
    mov si, message
    mov di, 0
    mov cx, 5
    mov ah, 10100100B
l1:
    mov al, [si]
    mov [es:di], ax
    inc si
    add di, 2
    loop l1

    ; read loader from disk to memory
    mov eax, LOADER_START_SECTOR
    mov bx, LOADER_BASE_ADDR
    mov cx, 4
    call rd_disk_m_16

    jmp LOADER_BASE_ADDR+0x300


; read disk
; eax = LBA sector number
; bx = dst address
; cx = count of sector
rd_disk_m_16:
    mov esi, eax
    mov di, cx   

    ; set port 0x1f2, count of sector
    mov dx, 0x1f2
    mov al, cl
    out dx, al

    mov eax, esi

    ; set port 0x1f3 ~ 0x1f6, write LBA addr
    inc dx          ; 0x1f3
    out dx, al

    shr eax, 8
    inc dx          ; 0x1f4
    out dx, al

    shr eax, 8
    inc dx          ; 0x1f5
    out dx, al
    
    shr eax, 8
    inc dx          ; 0x1f6
    and al, 00001111B   ; clear high 4 bit
    or al, 11100000B    ; use LBA method, master disk
    out dx, al

    ; set port 0x1f7, set write command
    inc dx          ; 0x1f7
    mov al, 0x20    ; write command
    out dx, al 
    
.not_ready:
    in al, dx
    and al, 0x88
    cmp al, 0x08
    jnz .not_ready

    ; read data from port 0x1f0
    mov ax, di
    mov dx, 256
    mul dx
    mov cx, ax

    mov dx, 0x1f0
.go_on_read:
    in ax, dx
    mov [bx], ax
    add bx, 2
    loop .go_on_read
    
    ret   

;----------------------------------------------------
    message db "1 MBR"
    
    times 510-($-$$) db 0
                     db 0x55, 0xaa
