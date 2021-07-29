#include "wait_exit.h"
#include "global.h"
#include "debug.h"
#include "thread.h"
#include "list.h"
#include "stdio_kern.h"
#include "memory.h"
#include "bitmap.h"
#include "fs.h"

/* 释放用户进程资源
   1 页表中对应的物理页
   2 虚拟内存池本身占用的物理页
   3 关闭打开的文件 
*/
static void release_proc_resource(struct task_struct *release_thread)
{
    uint32_t *pgdir_vaddr = release_thread->pgdir;
    uint16_t user_pde_nr = 768, pde_idx = 0;
    uint32_t pde = 0;
    uint32_t *v_pde_ptr = NULL;

    uint16_t user_pte_nr = 1024, pte_idx = 0;
    uint32_t pte = 0;
    uint32_t *v_pte_ptr = NULL;

    uint32_t *first_pte_vaddr_in_pde = NULL;
    uint32_t pg_phy_addr = 0;

    /* 回收页表用户空间的页面 */      
    while (pde_idx < user_pde_nr)
    {
        v_pde_ptr = pgdir_vaddr + pde_idx;
        pde = *v_pde_ptr;
        if (pde & 0x00000001)
        {
            /* 如果页目录的P位为1代表该目录向之下可能有页表项 */
            first_pte_vaddr_in_pde = pte_ptr(pde_idx * 0x400000);       // 一个页目录项容量为4MB
            pte_idx = 0;
            while (pte_idx < user_pte_nr)
            {
                v_pte_ptr = first_pte_vaddr_in_pde + pte_idx;
                pte = *v_pte_ptr;
                if (pte & 0x00000001)
                {
                    /* 页表项P为为1 */
                    pg_phy_addr = pte & 0xfffff000;
                    free_a_phy_page(pg_phy_addr);
                }
                pte_idx++;
            }

            /* 将PDE对应的项在物理内存池中清0 */
            pg_phy_addr = pde & 0xfffff000;
            free_a_phy_page(pg_phy_addr);
        }
        pde_idx++;
    }

    /* 回收用户虚拟地池占用的内存 */
    uint32_t bitmap_pg_cnt = (release_thread->userproc_vaddr.vaddr_bitmap.btmp_bytes_len) / PG_SIZE;;
    uint8_t *user_vaddr_pool_bitmap = release_thread->userproc_vaddr.vaddr_bitmap.bits;
    mfree_page(PF_KERNEL, user_vaddr_pool_bitmap, bitmap_pg_cnt);

    /* 关闭打开的所有文件 */
    uint8_t fd_idx = 3;
    while (fd_idx < MAX_FILES_OPEN_PER_PROC)
    {
        if (release_thread->fd_table[fd_idx] != -1) sys_close(fd_idx);
        fd_idx++;
    }
}

/* 查找pelem的parent_pid是否是ppid，成功返回1，否则返回0，list_traversal的回调函数 */
static int find_child(struct list_elem *pelem, int32_t ppid)
{
    struct task_struct *pthread = elem2entry(struct task_struct, all_list_tag, pelem);
    if (pthread->parent_pid == ppid) return 1;
    return 0;
}

/* 查找状态为TASK_HANGING且parent_pid为ppid的任务，list_traversal的回调函数 */
static int find_hanging_child(struct list_elem *pelem, int32_t ppid)
{
    struct task_struct *pthread = elem2entry(struct task_struct, all_list_tag, pelem);
    if (pthread->parent_pid == ppid && pthread->status == TASK_HANGING) return 1;
    return 0;
}

/* 将父进程是ppid的子进程过继给init，list_traversal的回调函数 */
static int init_adapt_a_child(struct list_elem *pelem, int32_t ppid)
{
    struct task_struct *pthread = elem2entry(struct task_struct, all_list_tag, pelem);
    if (pthread->parent_pid == ppid) pthread->parent_pid = 1;
    return 0;
}

/* 等待子进程调用exit，将子进程状态保存到status指向的变量
   成功返回子进程的PID，失败返回-1 */
pid_t sys_wait(int32_t *status)
{
    struct task_struct *parent_thread = running_thread();

    while (1)
    {
        /* 优先处理已经是挂起状态的任务 */
        struct list_elem *child_elem = list_traversal(&thread_all_list, find_hanging_child, parent_thread->pid);
        if (child_elem != NULL) 
        {
            struct task_struct *child_thread = elem2entry(struct task_struct, all_list_tag, child_elem);
            *status = child_thread->exit_status;
            
            /* thread_exit之后，PCB会被回收 */
            uint16_t child_pid = child_thread->pid;

            /* 从就绪队列和全局队列移除子进程表项 */
            thread_exit(child_thread, 0);       // 需要返回，不执行调度
            /* 进程表项PCB是进程最后的资源，至此进程彻底死透了 */
            return child_pid;
        }

        /* 判断是否有子进程 */
        child_elem = list_traversal(&thread_all_list, find_child, parent_thread->pid);
        if (child_elem == NULL) return -1;
        /* 若子进程还没执行完，则自己挂起，直到任意一个子进程调用exit唤醒 */
        else thread_block(TASK_WAITING);
    }
}

/* 子进程自我了断调用 */
void sys_exit(int32_t status)
{
    struct task_struct *child_thread = running_thread();
    child_thread->exit_status = status;
    if (child_thread->parent_pid == -1)
    {
        PANIC("sys_exit: child_thread->parent_pid is -1\n");
    }
    
    /* 将当前进程所有的子进程都过继给init */
    list_traversal(&thread_all_list, init_adapt_a_child, child_thread->pid);
    
    /* 回收当前进程的资源 */
    release_proc_resource(child_thread);
    
    /* 如果父进程正在等待子进程把父进程唤醒 */
    struct task_struct *parent_thread = pid2thread(child_thread->parent_pid);
    if (parent_thread->status == TASK_WAITING) thread_unblock(parent_thread);
    
    /* 自己挂起，等待父进程收尸（回收PCB） */
    thread_block(TASK_HANGING);
}
