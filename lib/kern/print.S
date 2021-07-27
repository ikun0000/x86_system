TI_GDT          equ 0
RPL_0           equ 0
SELECTOR_VIDEO  equ (0x0003 << 3) + TI_GDT + RPL_0

SECTION .data
; buffer to translate
put_int_buffer      dq 0

[bits 32]
SECTION .text

global put_str
put_str:
    push ebx
    push ecx

    xor ecx, ecx
    mov ebx, [esp + 12]
.goon:
    mov cl, [ebx]
    cmp cl, 0
    jz .str_over
    push ecx
    call put_char
    add esp, 4
    inc ebx
    jmp .goon

.str_over:
    pop ecx
    pop ebx
    ret


global put_char
; print the char in stack first arg to cursor
put_char:
    pushad
    
    ; video selector
    mov ax, SELECTOR_VIDEO
    mov gs, ax

    ; get the cursor position
    mov dx, 0x3d4
    mov al, 0x0e
    out dx, al
    inc dx              ; dx=0x3d5
    in al, dx           ; get high 8 bits
    mov ah, al

    dec dx              ; dx=0x3d4
    mov al, 0x0f
    out dx, al
    inc dx              ; dx=0x3d5
    in al, dx

    ; backup cursor position
    mov bx, ax
    
    ; get print char
    mov ecx, [esp + 36]         ; 8 * 4 + 4
    
    cmp cl, 0xd                 ; is CR
    jz .is_carriage_return
    cmp cl, 0xa                 ; is LF
    jz .is_line_feed

    cmp cl, 0x8                 ; is BS
    jz .is_backspace
    jmp .put_other

.is_backspace:
    dec bx
    shl bx, 1                   ; bx *= 2
    mov byte [gs:bx], 0x20
    shr bx, 1                   ; bx /= 2
    jmp .set_cursor
    
.put_other:
    shl bx, 1
    mov [gs:bx], cl             ; put char
    inc bx
    mov byte [gs:bx], 0x07           ; char attribute, black background, white front
    shr bx, 1
    inc bx
    cmp bx, 2000
    jl .set_cursor              ; if lower that 2000 set cursor, else execute CRLF

; CRLF
.is_line_feed:
.is_carriage_return:
    xor dx, dx
    mov ax, bx
    mov si, 80
    div si                      ; ax is line number, dx is column number
    
    sub bx, dx                  ; that set cursor to line start
    
.is_carriage_return_end:
    add bx, 80
    cmp bx, 2000
    
.is_line_feed_end:
    jl .set_cursor

.roll_screen:
    cld
    mov ecx, 960                ; ((2000-80)*2)/4=960

    mov esi, 0xc00b80a0         ; second line start
    mov edi, 0xc00b8000         ; first line start
    rep movsd

    mov ebx, 3840               ; last line start
    mov ecx, 80

.cls:
    mov word [gs:ebx], 0x0720
    add ebx, 2
    loop .cls
    mov bx, 1920                ; cursor set to 1920

.set_cursor:
    mov dx, 0x3d4   
    mov al, 0x0e
    out dx, al
    inc dx                      ; dx=0x3d5
    mov al, bh
    out dx, al
    
    dec dx                      ; dx=0x3d4
    mov al, 0x0f
    out dx, al
    inc dx                      ; dx=0x3d5
    mov al, bl
    out dx, al
 
.put_char_done:
    popad
    ret


global put_int
put_int:
    pushad
    mov ebp, esp
    mov eax, [ebp+9*4]
    mov edx, eax
    mov edi, 7              ; offset of the buffer
    mov ecx, 8              ; 32bit / 4 = 8
    mov ebx, put_int_buffer ; base of buffer
    
.dword_translate:
    and edx, 0x0000000f     ; clear high bits
    
    cmp edx, 9
    jg .is_A2F
    add edx, '0'
    jmp .store
.is_A2F:
    sub edx, 10
    add edx, 'A'
    
.store:
    mov [ebx+edi], dl
    dec edi
    shr eax, 4
    mov edx, eax
    loop .dword_translate
    
.ready_to_print:
    inc edi                 ; before this instruction edi is 0xffffffff
.skip_prefix_0:
    cmp edi, 8              ; is ninth char
    je .full0

.go_on_skip:
    mov cl, [put_int_buffer+edi] 
    inc edi
    cmp cl, '0'
    je .skip_prefix_0
    dec edi                 ; if not zero restore previous
    jmp .put_each_num

.full0:
    mov cl, '0'
.put_each_num:
    push ecx
    call put_char
    add esp, 4
    inc edi
    mov cl, [put_int_buffer+edi]
    cmp edi, 8
    jl .put_each_num

    popad
    ret


global set_cursor
set_cursor:
    pushad

    mov ebx, [esp+36]
    mov dx, 0x3d4
    mov al, 0x0e
    out dx, al
    inc dx              ; dx=0x3d5
    mov al, bh
    out dx, al
    
    dec dx              ; dx=0x3d4
    mov al, 0x0f
    out dx, al
    inc dx              ; dx=0x3d5
    mov al, bl
    out dx, al

    popad
    ret

global cls_screen
cls_screen:
    pushad

    mov ax, SELECTOR_VIDEO
    mov gs, ax
    
    mov ebx, 0
    mov ecx, 80*25
.cls:
    mov word [gs:ebx], 0x0720
    add ebx, 2
    loop .cls
    mov ebx, 0
    
.set_cursor:
    mov dx, 0x3d4
    mov al, 0x0e
    out dx, al
    inc dx          ; dx = 0x3d5
    mov al, bh
    out dx, al
    
    dec dx          ; dx = 0x3d4
    mov al, 0x0f
    out dx, al
    inc dx          ; dx = 0x3d5
    mov al, bl
    out dx, al

    popad
    ret
