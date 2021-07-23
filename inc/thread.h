#ifndef __THREAD_THREAD_H
#define __THREAD_THREAD_H

#include "stdint.h"
#include "list.h"
#include "bitmap.h"
#include "memory.h"

#define MAX_FILES_OPEN_PER_PROC     8

/* 线程函数类型 */
typedef void thread_func(void *);
typedef int16_t pid_t;

/* 进程或线程状态 */
enum task_status 
{
    TASK_RUNNING,
    TASK_READY,
    TASK_BLOCKED,
    TASK_WAITING,
    TASK_HANGING,
    TASK_DIED
};

/* 中断栈
   用于在中断发生时保护程序的上下文环境
   中断发生时会按照此结构压入寄存器上下文
   此结构位于进程或线程自己的内核空间的顶端
 */
struct intr_stack 
{
    uint32_t vec_no;        // 中断号
    uint32_t edi;
    uint32_t esi;
    uint32_t ebp;
    uint32_t esp_dummy;     // esp会不断变化，会被popad忽略
    uint32_t ebx;
    uint32_t edx;
    uint32_t ecx;
    uint32_t eax;
    uint32_t gs;
    uint32_t fs;
    uint32_t es;
    uint32_t ds;

    /* 以下由cpu从低特权级进入高特权级时压入 */
    uint32_t err_code;
    void (*eip)(void);
    uint32_t cs;
    uint32_t eflags;
    void *esp;
    uint32_t ss;
};

/* 线程栈
   线程自己的栈，用于存储线程中执行的函数   
*/
struct thread_stack 
{
    uint32_t ebp;
    uint32_t ebx;
    uint32_t edi;
    uint32_t esi;

    /* 第一次执行时指向线程函数的入口地址，其他时候指向switch_to的返回地址 */
    void (*eip) (thread_func *func, void *func_arg);
    
    /* 以下内容被cpu第一次调度才使用 */
    /* 用于占位，是返回地址 */
    void (*unused_retaddr);
    thread_func *function;      // 要调用的函数名
    void *func_arg;             // 调用的函数的参数
};

struct task_struct 
{
    uint32_t *self_kstack;                          // 各个内核线程都有自己的内核栈
    pid_t pid;                                      // 进程/线程的PID
    enum task_status status;                        // 线程的状态
    uint8_t priority;                               // 线程优先级
    char name[16];                                  // 线程名
    uint8_t ticks;                                  // 每次在处理器上执行的滴答数
    uint32_t elapsed_ticks;                         // 自任务启动后所使用的cpu滴答数
    uint32_t fd_table[MAX_FILES_OPEN_PER_PROC];     // 文件描述符数组
    struct list_elem general_tag;                   // 线程在一般队列中的节点
    struct list_elem all_list_tag;                  // 用于thread_all_list中的节点
    uint32_t *pgdir;                                // 线程自己页表的虚拟地址
    struct virtual_addr userproc_vaddr;             // 用户进程虚拟地址
    struct mem_block_desc u_block_desc[DESC_CNT];   // 用户进程内存块描述符
    uint32_t stack_magic;                           // 用于做栈的边界标记，用于检查栈溢出
};

extern struct list thread_ready_list;
extern struct list thread_all_list;

void thread_create(struct task_struct *pthread, thread_func *function, void *func_arg);
void init_thread(struct task_struct *pthread, char *name, int prio);
struct task_struct *thread_start(char *name, int prio, thread_func *function, void *func_arg);
struct task_struct *running_thread(void);
void schedule(void);
void thread_init(void);
void thread_block(enum task_status stat);
void thread_unblock(struct task_struct *pthread);
void thread_yield(void);

#endif
