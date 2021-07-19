%include "bootloader.inc"

SECTION loader align=16 vstart=LOADER_BASE_ADDR

LOADER_STACK_TOP    equ LOADER_BASE_ADDR
    
; create GDT and descriptor
    GDT_BASE:           dd 0x00000000
                        dd 0x00000000

    CODE_DESC:          dd 0x0000ffff
                        dd DESC_CODE_HIGH4

    DATA_STACK_DESC:    dd 0x0000ffff
                        dd DESC_DATA_HIGH4

    VIDEO_DESC:         dd 0x80000007
                        dd DESC_VIDEO_HIGH4

    GDT_SIZE    equ $ - GDT_BASE
    GDT_LIMIT   equ GDT_SIZE - 1
    ; prepare 60 descriptor
    times 60 dq 0

    ; save memory in byte
    ; offset of file 0x200
    ; offset of memory 0xb00
    total_mem_bytes dd 0

    SELECTOR_CODE   equ (0x0001 << 3) + TI_GDT + RPL0
    SELECTOR_DATA   equ (0x0002 << 3) + TI_GDT + RPL0
    SELECTOR_VIDEO  equ (0x0003 << 3) + TI_GDT + RPL0

    ; GDT pointer
    gdt_ptr         dw GDT_LIMIT
                    dd GDT_BASE

    ards_buf        times 227 db 0
    ards_nr         dw 0

    loadermsg       db '2 loader in real.'

;---------------------------------------------------------

; offset file 0x300
; offset memory 0xc00
loader_start:
    mov sp, LOADER_BASE_ADDR
    ; print message
    mov ax, 0
    mov es, ax

    ; int 15h eax=0xe820 edx=0x534d4150
    xor ebx, ebx
    mov edx, 0x534d4150
    mov di, ards_buf
.e820_mem_get_loop:
    mov eax, 0x0000e820
    mov ecx, 20
    int 0x15
    jc .e820_failed_so_try_e801
    add di, cx
    inc word [ards_nr]
    cmp ebx, 0
    jnz .e820_mem_get_loop

    ; find max e820 struct and that is the physical memory
    mov cx, [ards_nr]
    mov ebx, ards_buf
    xor edx, edx            ; save max memory count
.find_max_memory_area:
    mov eax, [ebx]          ; base_addr_low
    add eax, [ebx+8]        ; length_low
    add ebx, 20
    cmp edx, eax
    jge .next_ards
    mov edx, eax
.next_ards:
    loop .find_max_memory_area
    jmp .mem_get_ok

;    mov cx, [ards_nr]
;    mov ebx, ards_buf
;    xor edx, edx            ; save memroy count
;.calc_mem:
;    cmp dword [ebx+16], 1
;    jne .next_region
;    add edx, [ebx+8]
;.next_region:
;    add ebx, 20
;    loop .calc_mem
;    jmp .mem_get_ok

    ; int 15h eax=0xe801 
.e820_failed_so_try_e801:
    mov ax, 0xe801
    int 0x15
    jc .e801_failed_so_try88
    
    mov cx, 0x400
    mul cx                      ; result in dx:ax
    ; merge result
    shl edx, 16
    and eax, 0x0000ffff
    or edx, eax
    add edx, 0x100000
    mov esi, edx

    xor eax, eax
    mov ax, bx
    mov ecx, 0x10000
    mul ecx
    add esi, eax
    
    mov edx, esi
    jmp .mem_get_ok

    ; int 15h ah=0x88
.e801_failed_so_try88:
    mov ah, 0x88
    int 0x15
    jc .error_hlt

    and eax, 0x0000ffff
    mov cx, 0x400
    mul cx
    shl edx, 16
    or edx, eax
    add edx, 0x100000

.mem_get_ok:
    mov [total_mem_bytes], edx
    
    jmp .print_msg_entry_protected_mode

.error_hlt:
    jmp $

.print_msg_entry_protected_mode:
    mov bp, loadermsg
    mov cx, 17
    mov ax, 0x1301
    mov bx, 0x001f
    mov dx, 0x0300
    int 0x10

    ; open A20
    in al, 0x92
    or al, 0000_0010B
    out 0x92, al

    ; load GDT
    lgdt [gdt_ptr] 

    ; set cr0.PE
    mov eax, cr0
    or eax, 1
    mov cr0, eax

    ; flash assembly line
    jmp dword SELECTOR_CODE:p_mode_start

[bits 32]
p_mode_start:
    mov ax, SELECTOR_DATA
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov esp, LOADER_STACK_TOP
    mov ax, SELECTOR_VIDEO
    mov gs, ax

    mov byte [gs:160], 'P'

    ; read kernel from disk to memory
    mov eax, KERNEL_START_SECTOR
    mov ebx, KERNEL_BIN_BASE_ADDR
    mov ecx, 200
    call rd_disk_m_32

    call setup_page

    sgdt [gdt_ptr]
    mov ebx, [gdt_ptr + 2]
    ; set video descriptor 
    or dword [ebx + 0x18 + 4], 0xc0000000
    add dword [gdt_ptr + 2], 0xc0000000
    add esp, 0xc0000000

    ; setup cr3
    mov eax, PAGE_DIR_TABLE_POS
    mov cr3, eax

    ; enable cr0.PG
    mov eax, cr0
    or eax, 0x80000000
    mov cr0, eax
    
    lgdt [gdt_ptr]
    
    mov byte [gs:320], 'V'

    jmp SELECTOR_CODE:enter_kernel
enter_kernel:
    call kernel_init
    mov esp, 0xc009f000
    jmp KERNEL_ENTRY_POINT


; init kernel
kernel_init:
    xor eax, eax
    xor ebx, ebx            ; ebx record program header table address
    xor ecx, ecx            ; cx record program header count
    xor edx, edx            ; dx record program header size
    
    mov dx, [KERNEL_BIN_BASE_ADDR + 42]         ; e_phentsize
    mov ebx, [KERNEL_BIN_BASE_ADDR + 28]        ; e_phoff
    
    add ebx, KERNEL_BIN_BASE_ADDR + 32          ; in memory linear address 
    mov cx, [KERNEL_BIN_BASE_ADDR + 44]         ; e_phnum
    dec cx
    and ecx, 0x0000ffff
    
.each_segment:
    cmp byte [ebx + 0], PT_NULL
    je .PTNULL
    mov eax, [ebx + 8]
    and eax, 0xc0000000
    cmp eax, 0xc0000000
    jne .PTNULL
    
    push dword [ebx + 16]                       ; p_filesz
    mov eax, [ebx + 4]                          ; p_offset
    add eax, KERNEL_BIN_BASE_ADDR               ; in memory linear address
    push eax
    push dword [ebx + 8]                        ; p_vaddr
    call mem_cpy
    add esp, 12
.PTNULL:
    add ebx, edx
    loop .each_segment
    
    ret 


; copy memory
; mem_cpy(dst, src, size)
mem_cpy:
    cld
    push ebp
    mov ebp, esp
    push ecx
    
    mov edi, [ebp+8]        ; dst    
    mov esi, [ebp+12]       ; src
    mov ecx, [ebp+16]       ; size
    rep movsb

    pop ecx
    pop ebp
    ret


; setup kernel page directory
setup_page:
    mov ecx, 4096
    mov esi, 0
.clear_page_dir:
    mov byte [PAGE_DIR_TABLE_POS + esi], 0
    inc esi
    loop .clear_page_dir

    ; create Page Directory Entry
.create_pde:
    mov eax, PAGE_DIR_TABLE_POS
    add eax, 0x1000                 ; first page table address
    mov ebx, eax

    ; setup first PDE 
    or eax, PG_US_U | PG_RW_W | PG_P
    mov [PAGE_DIR_TABLE_POS + 0x0], eax
    mov [PAGE_DIR_TABLE_POS + 0xc00], eax
    
    ; set the last entry pointer to self(PD)
    sub eax, 0x1000
    mov [PAGE_DIR_TABLE_POS + 4092], eax

    ; setup Page Table Entry
    mov ecx, 256                ; loop count, 1MB / 4KB = 256
    mov esi, 0
    mov edx, PG_US_U | PG_RW_W | PG_P
.create_pte:
    mov [ebx + esi*4], edx
    add edx, 4096
    inc esi 
    loop .create_pte
    
    ; create kernel another PDE
    mov eax, PAGE_DIR_TABLE_POS
    add eax, 0x2000
    or eax, PG_US_U | PG_RW_W | PG_P
    mov ebx, PAGE_DIR_TABLE_POS
    mov ecx, 254
    mov esi, 769                    ; index of second PDE
.create_kernel_pde:
    mov [ebx + esi*4], eax
    inc esi
    add eax, 0x1000
    loop .create_kernel_pde
    ret


; read disk data to memory
; eax=LBA sector number
; ebx=dst address
; ecx=count of sector
rd_disk_m_32:
    mov esi, eax
    mov edi, ecx
    
    ; set read sector count
    mov dx, 0x1f2
    mov al, cl
    out dx, al          ; dx=0x1f2
    mov eax, esi

    ; write LBA address
    inc dx              ; dx=0x1f3
    out dx, al

    shr eax, 8
    inc dx              ; dx=0x1f4
    out dx, al
    
    shr eax, 8
    inc dx              ; dx=0x1f5
    out dx, al
    
    shr eax, cl
    and al, 0x0f
    or al, 0xe0
    inc dx              ; dx=0x1f6
    out dx, al

    inc dx              ; dx=0x1f7
    mov al, 0x20
    out dx, al
    
    ; wait dis ready and not busy
.not_ready:
    in al, dx
    and al, 0x88
    cmp al, 0x08
    jnz .not_ready

    mov eax, edi
    mov edx, 256
    mul edx
    mov ecx, eax
    mov dx, 0x1f0
.go_on_read:
    in ax, dx
    mov [ebx], ax
    add ebx, 2
    loop .go_on_read
    
    ret
    
