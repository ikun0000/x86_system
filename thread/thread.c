#include "stdint.h"
#include "thread.h"
#include "string.h"
#include "global.h"
#include "memory.h"
#include "debug.h"
#include "interrupt.h"
#include "print.h"
#include "process.h"
#include "sync.h"

struct task_struct *main_thread;        // 主线程PCB
struct task_struct *idle_thread;        // idle线程
struct list thread_ready_list;          // 就绪队列
struct list thread_all_list;            // 所有任务队列
struct lock pid_lock;                   // 分配PID锁
static struct list_elem *thread_tag;    // 用于保存队列中的线程节点

extern void switch_to(struct task_struct *cur, struct task_struct *next);


/* 系统空闲时运行的线程 */
static void idle(void *arg UNUSED)
{
    while (1) 
    {
        thread_block(TASK_BLOCKED);
        // 执行hlt时必须保证IF位为1,不然接收不到中断就无法调度线程
        asm volatile ("sti; hlt": : :"memory");
    }
}

/* 获取当前线程的PCB指针 */
struct task_struct *running_thread() 
{
    uint32_t esp;
    asm volatile ("movl %%esp, %0": "=g"(esp));
    return (struct task_struct *)(esp & 0xfffff000);
}

static void kernel_thread(thread_func *function, void *func_arg)
{
    /* 执行线程前开中断，避免接收不到中断而导致其他线程无法被调度 */
    intr_enable();
    function(func_arg);
}

/* 分配pid */
static pid_t allocate_pid(void)
{
    static pid_t next_pid = 0;      // 静态变量，会一直存在
    lock_acquire(&pid_lock);
    next_pid++;
    lock_release(&pid_lock);
    return next_pid;
}

/* 初始化线程栈，将待执行的函数和参数放到thread_stack中相应的位置 */
void thread_create(struct task_struct *pthread, thread_func *function, void *func_arg)
{
    /* 先预留中断使用的栈空间 */
    pthread->self_kstack -= sizeof(struct intr_stack);
    
    /* 再预留出线程栈空间 */
    pthread->self_kstack -= sizeof(struct thread_stack);
    struct thread_stack *kthread_stack = (struct thread_stack *)pthread->self_kstack;
    kthread_stack->eip = kernel_thread;
    kthread_stack->function = function;
    kthread_stack->func_arg = func_arg;
    kthread_stack->ebp = kthread_stack->ebx = kthread_stack->esi = kthread_stack->edi = 0;
}

/* 初始化线程基本信息 */
void init_thread(struct task_struct *pthread, char *name, int prio)
{
    memset(pthread, 0, sizeof(*pthread));
    pthread->pid = allocate_pid();
    strcpy(pthread->name, name);        /* !!!!!!!! overflow !!!!!!!! */

    if (pthread == main_thread) pthread->status = TASK_RUNNING;
    else pthread->status = TASK_READY;

    pthread->ticks = prio;
    pthread->elapsed_ticks = 0;
    pthread->pgdir = NULL;
    pthread->priority = prio;
    /* self_kstack是线程自己在内核状态下使用的栈顶地址 
       task_struct就是PCB，也就是一个自然页的首地址，
       而PCB和栈共用一个内存页，所以栈顶就是pthread加上自然页的长度 */
    pthread->self_kstack = (uint32_t *)((uint32_t)pthread + PG_SIZE);

    /* 预留标准输入输出和标准错误输出 */
    pthread->fd_table[0] = 0;
    pthread->fd_table[1] = 1;
    pthread->fd_table[2] = 2;
    /* 其余置0 */
    uint8_t fd_idx = 3;
    while (fd_idx < MAX_FILES_OPEN_PER_PROC) pthread->fd_table[fd_idx++] = -1;

    pthread->cwd_inode_nr = 0;              /* 以根目录作为默认工作目录 */
    pthread->stack_magic = 0x19870916;      /* 自定义魔数 */
}

struct task_struct *thread_start(char *name, int prio, thread_func *function, void *func_arg)
{
    struct task_struct *thread = get_kernel_pages(1);
    
    init_thread(thread, name, prio);
    thread_create(thread, function, func_arg);

    /* 确保之前不再队列中 */
    ASSERT(!elem_find(&thread_ready_list, &thread->general_tag));
    /* 加入就绪线程队列 */
    list_append(&thread_ready_list, &thread->general_tag);

    /* 确保之前不队列中 */
    ASSERT(!elem_find(&thread_all_list, &thread->all_list_tag));
    /* 加入全部线程队列 */
    list_append(&thread_all_list, &thread->all_list_tag);

    return thread;
}

/* 将kernel中的main函数完善为主线程 */
static void make_main_thread(void)
{
    /* 因为main函数早就运行了，loader.S中设置esp为0xc009f000，
       这里就是main的PCB，所以他返回的地址是0xc009e000 */
    main_thread = running_thread();
    init_thread(main_thread, "main", 31);

    /* main函数是当前线程，而当前线程不在thread_ready_list中，
       所以只将其添加到thread_all_list中 */
    ASSERT(!elem_find(&thread_all_list, &main_thread->all_list_tag));
    list_append(&thread_all_list, &main_thread->all_list_tag);
}

/* 实现任务调度 */
void schedule() 
{
    ASSERT(intr_get_status() == INTR_OFF);
    
    struct task_struct *cur = running_thread();
    if (cur->status == TASK_RUNNING) 
    {
        /* 如果程序只是cpu时间片到了将其加入到就绪队列尾 */
        ASSERT(!elem_find(&thread_ready_list, &cur->general_tag));
        list_append(&thread_ready_list, &cur->general_tag);
        cur->ticks = cur->priority;
        cur->status = TASK_READY;
    } 
    else
    {
        /* 因为其他事情被调度 */
    }

    /* 如果任务队列没有可运行的线程则唤醒idle线程 */
    if (list_empty(&thread_ready_list)) thread_unblock(idle_thread);

    ASSERT(!list_empty(&thread_ready_list));
    thread_tag = NULL;
    /* 将就绪队列第一个线程弹出并调度上cpu */
    thread_tag = list_pop(&thread_ready_list);
    struct task_struct *next = elem2entry(struct task_struct, general_tag, thread_tag);
    next->status = TASK_RUNNING;
    /* 激活即将运行的程序的ESP和页表 */
    process_activate(next);
    switch_to(cur, next);
}

/* 当前线程自己阻塞，并标志其状态stat */
void thread_block(enum task_status stat) 
{
    ASSERT(((stat == TASK_BLOCKED) || (stat == TASK_WAITING) || (stat == TASK_HANGING)));

    enum intr_status old_status = intr_disable();
    struct task_struct *cur_thread = running_thread();
    cur_thread->status = stat;      // 更新状态
    schedule();                     // 将当前线程换下处理器
    intr_set_status(old_status);    // 当前线线程解除阻塞后才恢复中断状态
}

/* 将线程pthread解除阻塞 */
void thread_unblock(struct task_struct *pthread)
{
    enum intr_status old_status = intr_disable();
    ASSERT(((pthread->status == TASK_BLOCKED) || (pthread->status == TASK_WAITING) || (pthread->status == TASK_HANGING)));
    
    if (pthread->status != TASK_READY)
    {
        ASSERT(!elem_find(&thread_ready_list, &pthread->general_tag));
        if (elem_find(&thread_ready_list, &pthread->general_tag))
            PANIC("thread_unblock: blocked thread in ready_list\n");
        /* 放到队列最前面以尽快调度 */
        list_push(&thread_ready_list, &pthread->general_tag);
        pthread->status = TASK_READY;
    }
    intr_set_status(old_status);
}

/* 主动让出CPU，换其他线程执行
   和thread_block不同的是thread_block只会唤醒下一个线程，不管当前线程
   thread_yield会把当前线程重新放回到ready队列中 */
void thread_yield(void)
{
    struct task_struct *cur = running_thread();
    enum intr_status old_status = intr_disable();
    ASSERT(!elem_find(&thread_ready_list, &cur->general_tag));
    list_append(&thread_ready_list, &cur->general_tag);
    cur->status = TASK_READY;
    schedule();
    intr_set_status(old_status);
}

/* 初始化线程环境 */
void thread_init(void)
{
    put_str("thread_init start...\n");
    list_init(&thread_ready_list);
    list_init(&thread_all_list);
    lock_init(&pid_lock);
    /* 将当前main线程初始化为线程 */
    make_main_thread();
    idle_thread = thread_start("idle", 10, idle, NULL);
    put_str("thread_init done.\n");
}
