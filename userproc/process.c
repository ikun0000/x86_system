#include "process.h"
#include "global.h"
#include "debug.h"
#include "memory.h"
#include "thread.h"
#include "list.h"
#include "tss.h"
#include "interrupt.h"
#include "string.h"
#include "console.h"

extern void intr_exit(void);

/* 创建用户进程 */
void process_execute(void *filename, char *name)
{
    /* pcb内核的数据结构，由内核来维护进程信息，所以需要在内核内存池中申请 */
    struct task_struct *thread = get_kernel_pages(1);
    init_thread(thread, name, default_prio);
    create_user_vaddr_bitmap(thread);
    thread_create(thread, start_process, filename);
    thread->pgdir = create_page_dir();
    block_desc_init(thread->u_block_desc);
    
    enum intr_status old_status = intr_disable();
    ASSERT(!elem_find(&thread_ready_list, &thread->general_tag));
    list_append(&thread_ready_list, &thread->general_tag);

    ASSERT(!elem_find(&thread_all_list, &thread->all_list_tag))
    list_append(&thread_all_list, &thread->all_list_tag);
    intr_set_status(old_status);
}

/* 构建用户pr进程初始上下文信息 */
void start_process(void *filename_)
{
    void *function = filename_;
    struct task_struct *cur = running_thread();
    cur->self_kstack += sizeof(struct thread_stack);
    struct intr_stack *proc_stack = (struct intr_stack *)cur->self_kstack;
    proc_stack->edi = proc_stack->esi = proc_stack->ebp = proc_stack->esp_dummy = 0;
    proc_stack->ebx = proc_stack->edx = proc_stack->ecx = proc_stack->eax = 0;
    proc_stack->gs = 0;         // 用户态不用
    proc_stack->ds = proc_stack->es = proc_stack->fs = SELECTOR_U_DATA;
    proc_stack->eip = function;     // 待执行的用户程序地址
    proc_stack->cs = SELECTOR_U_CODE;
    proc_stack->eflags = (EFLAGS_IOPL_0 | EFLAGS_MBS | EFLAGS_IF_1);
    proc_stack->esp = (void *)((uint32_t)get_a_page(PF_USER, USER_STACK3_VADDR) + PG_SIZE);
    proc_stack->ss = SELECTOR_U_DATA;
    asm volatile ("movl %0, %%esp; jmp intr_exit": :"g"(proc_stack): "memory");
}

/* 激活线程或进程的页表，更新TSS中的esp0为进程的特权级0的栈 */
void process_activate(struct task_struct *p_thread)
{
    ASSERT(p_thread != NULL);
    
    /* 激活该进程或线程的页表 */
    page_dir_activate(p_thread);
    
    /* 内核本身就在特权级0下运行，在任务切换时不会从TSS中获取0特权级的栈 */
    if (p_thread->pgdir) update_tss_esp(p_thread);
}

/* 激活页表 */
void page_dir_activate(struct task_struct *p_thread)
{
    /* 执行此函数的有可能是进程或者线程，之所以每次任务切换都激活页表是防止
       属于不同进程的线程访问错误的地址空间 */
    
    /* 如果是内核线程需要重新填充页表为0x100000 */
    uint32_t pagedir_phy_addr = 0x100000;   // 内核页目录的物理地址
    if (p_thread->pgdir != NULL)            // 用户进程有自己的页目录，只有内核线程为NULL
        pagedir_phy_addr = addr_v2p((uint32_t)p_thread->pgdir);
    
    /* 更新夜幕路寄存器CR3 */   
    asm volatile ("movl %0, %%cr3": :"r"(pagedir_phy_addr): "memory");
}

/* 创建页目录表，将当前页目录的内核地址部分的pde复制，
   成功返回页目录的虚拟地址，否则返回-1 */
uint32_t *create_page_dir(void)
{
    /* 用户进程的页目录不能让进程访问，所以要在内核空间中申请 */
    uint32_t *page_dir_vaddr = get_kernel_pages(1);
    if (page_dir_vaddr == NULL)
    {
        console_put_str("create_page_dir: get_kernel_page failed!\n");
        return NULL;
    }   
    
    /* 复制内核页目录项 */
    memcpy((uint32_t *)((uint32_t)page_dir_vaddr + 0x300*4), (uint32_t *)(0xfffff000 + 0x300*4), 1024);
    
    /* 更新页目录地址，使其最后一项指向自己，即访问虚拟地址0xfffff000是指向本页目录的物理地址 */
    uint32_t new_page_dir_phy_addr = addr_v2p((uint32_t)page_dir_vaddr);
    page_dir_vaddr[1023] = new_page_dir_phy_addr | PG_US_U | PG_RW_W | PG_P_1;

    return page_dir_vaddr;
}

/* 创建用户进程虚拟地址位图 */
void create_user_vaddr_bitmap(struct task_struct *user_proc)
{
    user_proc->userproc_vaddr.vaddr_start = USER_VADDR_START;
    uint32_t bitmap_pg_cnt = DIV_ROUND_UP((0xc0000000 - USER_VADDR_START) / PG_SIZE / 8, PG_SIZE);
    user_proc->userproc_vaddr.vaddr_bitmap.bits = get_kernel_pages(bitmap_pg_cnt);
    user_proc->userproc_vaddr.vaddr_bitmap.btmp_bytes_len = (0xc0000000 - USER_VADDR_START) / PG_SIZE / 8;
    bitmap_init(&user_proc->userproc_vaddr.vaddr_bitmap);
}
