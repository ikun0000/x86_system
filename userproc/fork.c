#include "global.h"
#include "fork.h"
#include "process.h"
#include "memory.h"
#include "interrupt.h"
#include "debug.h"
#include "thread.h"
#include "string.h"
#include "file.h"
#include "pipe.h"

extern void intr_exit(void);

/* 将父进程的PCB，虚拟地址位图拷贝给子进程 */
static int32_t copy_pcb_vaddrbitmap_stack0(struct task_struct *child_thread, struct task_struct *parent_thread)
{
    /* 复制PCB所在的整个页，包含PCB信息和0特权级栈，返回地址等 */
    memcpy(child_thread, parent_thread, PG_SIZE);
    child_thread->pid = fork_pid();
    child_thread->elapsed_ticks = 0;
    child_thread->status = TASK_READY;
    child_thread->ticks = child_thread->priority;
    child_thread->parent_pid = parent_thread->pid;
    child_thread->general_tag.prev = child_thread->general_tag.next = NULL;
    child_thread->all_list_tag.prev = child_thread->all_list_tag.next = NULL;
    block_desc_init(child_thread->u_block_desc);
    
    /* 复制父进程的虚拟地址池和位图 */
    uint32_t bitmap_pg_cnt = DIV_ROUND_UP((0xc0000000 - USER_VADDR_START) / PG_SIZE / 8, PG_SIZE);   
    void *vaddr_btmp = get_kernel_pages(bitmap_pg_cnt);
    if (vaddr_btmp == NULL) return -1;
    /* 此时child_thread->userproc_vaddr.vaddr_bitmap.bits还是指向父进程的虚拟地址位图
       下面将child_thread->userproc_vaddr.vaddr_bitmap.bits指向子进程自己的 */
    memcpy(vaddr_btmp, child_thread->userproc_vaddr.vaddr_bitmap.bits, bitmap_pg_cnt * PG_SIZE);
    child_thread->userproc_vaddr.vaddr_bitmap.bits = vaddr_btmp;
    
    ASSERT(strlen(child_thread->name) < 11);
    strcat(child_thread->name, "_fork");
    return 0;
}

/* 复制子进程的进程体代码和数据以及用户栈 */
static void copy_body_stack3(struct task_struct *child_thread, struct task_struct *parent_thread, void *buf_page)
{
    uint8_t *vaddr_btmp = parent_thread->userproc_vaddr.vaddr_bitmap.bits;
    uint32_t btmp_bytes_len = parent_thread->userproc_vaddr.vaddr_bitmap.btmp_bytes_len;
    uint32_t vaddr_start = parent_thread->userproc_vaddr.vaddr_start;
    uint32_t idx_byte = 0;
    uint32_t idx_bit = 0;
    uint32_t proc_vaddr = 0;

    /* 在父进程的用户空间中查找已经有的数据页 */
    while (idx_byte < btmp_bytes_len)
    {
        /* 8位（8页比较） */
        if (vaddr_btmp[idx_byte])
        {
            idx_bit = 0;
            while (idx_bit < 8)
            {
                /* 1位（一页）比较 */
                if ((BITMAP_MASK << idx_bit) & vaddr_btmp[idx_byte])
                {
                    proc_vaddr = (idx_byte * 8 + idx_bit) * PG_SIZE + vaddr_start;
                    
                    /* 把父进程的用户空间数据通过内核空间中转，复制到子进程的用户空间 */
                    
                    /* Step 1：将父进程的用户数据页拷贝到内核页 */
                    memcpy(buf_page, (void *)proc_vaddr, PG_SIZE);
                    
                    /* Step 2：将页表切换到子进程的页表 */
                    page_dir_activate(child_thread);
                    
                    /* Step 3：申请物理地址并映射到和父进程相同的虚拟地址 */
                    get_a_page_without_opvaddrbitmap(PF_USER, proc_vaddr);
                    
                    /* Step 4：从内核页缓冲区复制到子进程用户空间 */
                    memcpy((void *)proc_vaddr, buf_page, PG_SIZE);
                    
                    /* Step 5：恢复父进程页表 */
                    page_dir_activate(parent_thread);
                }
                
                idx_bit++;
            }
        }
        
        idx_byte++;
    }
}

/* 为子进程构建thread_stack和修改返回值 */
static int32_t build_child_stack(struct task_struct *child_thread)
{
    /* 使子进程pid返回值为0 */   
    /* 获取子进程0特权级栈 */
    struct intr_stack *intr_0_stack = (struct intr_stack *)((uint32_t)child_thread + PG_SIZE - sizeof(struct intr_stack));
    /* 修改子进程返回值为0 */
    intr_0_stack->eax = 0;

    /* 为switch_to构建struct thread_stack，该结构在intr_stack之下 */
    uint32_t *ret_addr_in_thread_stack = (int32_t *)intr_0_stack - 1;
    
    /* 不是必须的 */
    uint32_t *esi_ptr_in_thread_stack = (uint32_t *)intr_0_stack - 2;
    uint32_t *edi_ptr_in_thread_stack = (uint32_t *)intr_0_stack - 3;
    uint32_t *ebx_ptr_in_thread_stack = (uint32_t *)intr_0_stack - 4;

    /* ebp存放的地址是当前esp0的栈帧
       esp = (uint32_t *)intr_0_stack - 5 */
    uint32_t *ebp_ptr_in_thread_stack = (uint32_t *)intr_0_stack - 5;

    /* switch_to的返回地址改为intr_exit，直接从中断返回 */
    *ret_addr_in_thread_stack = (uint32_t)intr_exit;
    
    *ebp_ptr_in_thread_stack = 0;
    *ebx_ptr_in_thread_stack = 0;
    *edi_ptr_in_thread_stack = 0;
    *esi_ptr_in_thread_stack = 0;
    
    /* 把构建的thread_stack的栈顶作为switch_to恢复数据的栈顶 */
    child_thread->self_kstack = ebp_ptr_in_thread_stack;
    return 0;
}

/* 更新inode打开数 */
static void update_inode_open_cnts(struct task_struct *thread)
{
    int32_t local_fd = 3, global_fd = 0;

    while (local_fd < MAX_FILES_OPEN_PER_PROC)
    {
        global_fd = thread->fd_table[local_fd];
        ASSERT(global_fd < MAX_FILE_OPEN);
        if (global_fd != -1)
        {
            if (is_pipe(local_fd)) file_table[global_fd].fd_pos++;
            else file_table[global_fd].fd_inode->i_open_cnts++;
        }
        local_fd++;
    }
}

/* 拷贝父进程本身所占的资源给子进程 */
static int32_t copy_process(struct task_struct *child_thread, struct task_struct *parent_thread)
{
    /* 内核缓冲区，作为父进程和用户进程数据复制的中转 */
    void *buf_page = get_kernel_pages(1);   
    if (buf_page == NULL) return -1;

    /* 复制父进程的PCB，虚拟地址位图，内核栈给子进程 */
    if (copy_pcb_vaddrbitmap_stack0(child_thread, parent_thread) == -1) return -1;

    /* 为子进程创建页表（仅包含内核空间） */
    child_thread->pgdir = create_page_dir();
    if (child_thread->pgdir == NULL) return -1;

    /* 复制父进程用户空间给子进程 */
    copy_body_stack3(child_thread, parent_thread, buf_page);

    /* 构建子进程thread_stack和修改返回地址及返回PID */
    build_child_stack(child_thread);

    /* 更新全局文件打开的次数 */
    update_inode_open_cnts(child_thread);

    mfree_page(PF_KERNEL, buf_page, 1);
    return 0;
}

/* fork子进程，只能由用户进程通过系统调用fork，
   内核线程不可直接调用，原因是要从0特权级栈中获得esp3等 */
pid_t sys_fork(void)
{
    struct task_struct *parent_thread = running_thread();
    struct task_struct *child_thread = get_kernel_pages(1);
    if (child_thread == NULL) return -1;

    ASSERT(INTR_OFF == intr_get_status() && parent_thread->pgdir != NULL);
    
    if (copy_process(child_thread, parent_thread) == -1) return -1;

    ASSERT(!elem_find(&thread_ready_list, &child_thread->general_tag))
    list_append(&thread_ready_list, &child_thread->general_tag);
    ASSERT(!elem_find(&thread_all_list, &child_thread->all_list_tag));
    list_append(&thread_all_list, &child_thread->all_list_tag);

    return child_thread->pid;       // 父进程返回子进程PID
}
