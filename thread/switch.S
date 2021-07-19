[bits 32]
SECTION .text
global switch_to
switch_to:
    ; 栈中此处是返回地址
    push esi
    push edi
    push ebx
    push ebp

    mov eax, [esp + 20]         ; 在栈中获取参数cur
    mov [eax], esp              ; 保存栈顶指针esp，task_struct的self_kstack字段

    ; 以上是备份线程环境，下面是恢复线程环境
    ; 执行到这里已经切换了任务了
    
    mov eax, [esp + 24]         ; 得到参数中的next
    mov esp, [eax]              ; 使用next的self_kstack字段恢复栈

    pop ebp
    pop ebx
    pop edi
    pop esi
    ret                         ; 未由中断进入，第一次执行会返回kernel_thread
