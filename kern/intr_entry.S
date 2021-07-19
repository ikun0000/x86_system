[bits 32]
%define ERROR_CODE      nop             ; if have ERROR CODE put this
%define ZERO            push 0          ; if no ERROR CODE put this

extern idt_table

SECTION .data
global intr_entry_table
intr_entry_table:

%macro VECTOR 2
SECTION .text
intr%1entry:
    %2
    push ds
    push es
    push fs
    push gs
    pushad
    
    ; send EOI to 8259A master and slaver
    mov al, 0x20
    out 0xa0, al
    out 0x20, al

    ; 调用interrupt.c中的中断处理过程
    push %1
    call [idt_table + %1*4]
    jmp intr_exit

SECTION .data               ; this interrupt process data area
    dd intr%1entry          ; this interrupt process entry point
    
%endmacro    

SECTION .text
global intr_exit
intr_exit:
    add esp, 4
    popad
    pop gs
    pop fs
    pop es
    pop ds
    add esp, 4
    iretd


VECTOR 0x00, ZERO
VECTOR 0x01, ZERO
VECTOR 0x02, ZERO
VECTOR 0x03, ZERO
VECTOR 0x04, ZERO
VECTOR 0x05, ZERO
VECTOR 0x06, ZERO
VECTOR 0x07, ZERO
VECTOR 0x08, ERROR_CODE
VECTOR 0x09, ZERO
VECTOR 0x0a, ERROR_CODE
VECTOR 0x0b, ERROR_CODE
VECTOR 0x0c, ZERO
VECTOR 0x0d, ERROR_CODE
VECTOR 0x0e, ERROR_CODE
VECTOR 0x0f, ZERO
VECTOR 0x10, ZERO
VECTOR 0x11, ERROR_CODE
VECTOR 0x12, ZERO
VECTOR 0x13, ZERO
VECTOR 0x14, ZERO
VECTOR 0x15, ZERO
VECTOR 0x16, ZERO
VECTOR 0x17, ZERO
VECTOR 0x18, ERROR_CODE
VECTOR 0x19, ZERO
VECTOR 0x1a, ERROR_CODE
VECTOR 0x1b, ERROR_CODE
VECTOR 0x1c, ZERO
VECTOR 0x1d, ERROR_CODE
VECTOR 0x1e, ERROR_CODE
VECTOR 0x1f, ZERO

VECTOR 0x20, ZERO       ; 时钟中断
VECTOR 0X21, ZERO       ; 键盘中断
VECTOR 0x22, ZERO       ; 级联
VECTOR 0X23, ZERO       ; 串口2
VECTOR 0x24, ZERO       ; 串口1
VECTOR 0X25, ZERO       ; 并口2
VECTOR 0x26, ZERO       ; 软盘
VECTOR 0X27, ZERO       ; 并口1
VECTOR 0x28, ZERO       ; 实时时钟
VECTOR 0X29, ZERO       ; 重定向
VECTOR 0x2a, ZERO       ; 保留
VECTOR 0X2b, ZERO       ; 保留
VECTOR 0x2c, ZERO       ; ps/2鼠标
VECTOR 0X2d, ZERO       ; fpu浮点单元异常
VECTOR 0x2e, ZERO       ; 硬盘
VECTOR 0X2f, ZERO       ; 保留


; 0x80号中断，0x80中断不使用VECTOR宏定义
[bits 32]
extern syscall_table
section .text
global syscall_handler
syscall_handler:
    ; 保存上下文环境
    push 0              ; 和其他中断一样，压入错误码，统一格式j
    push ds
    push es
    push fs
    push gs
    pushad

    push 0x80           ; 也是保持统一格式
    ; 传入参数
    push edx            ; #3
    push ecx            ; #2
    push ebx            ; #1
    ; 调用子功能处理函数
    call [syscall_table + eax*4]
    add esp, 12
    ; 将返回值存入内核栈的eax位置
    mov [esp + 8*4], eax
    jmp intr_exit       ; 使用intr_exit统一返回

