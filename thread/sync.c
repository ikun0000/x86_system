#include "sync.h"
#include "list.h"
#include "global.h"
#include "debug.h"
#include "interrupt.h"
#include "thread.h"

/* 初始化信号量 */
void sema_init(struct semaphore *psema, uint8_t value)
{
    psema->value = value;
    list_init(&psema->waiters);     // 初始化信号量等待队列
}

void sema_down(struct semaphore *psema)
{
    enum intr_status old_status = intr_disable();

    while (psema->value == 0)
    {
        ASSERT(!elem_find(&psema->waiters, &(running_thread()->general_tag)));
        
        /* 当前线程不应该在信号量的等待队列中 */
        if (elem_find(&psema->waiters, &(running_thread()->general_tag)))
            PANIC("sema_down: thread blocked has been in waiters_list\n");    
            
        /* 信号量等于0，则该线程把自己加入到等待队列并阻塞 */
        list_append(&psema->waiters, &(running_thread()->general_tag));
        thread_block(TASK_BLOCKED);   
    }

    /* 若value值不为0或被唤醒后 */
    psema->value--;
    ASSERT(psema->value == 0);
    
    intr_set_status(old_status);
}

void sema_up(struct semaphore *psema)
{
    enum intr_status old_status = intr_disable();
    
    ASSERT(psema->value == 0);
    if (!list_empty(&psema->waiters))
    {
        struct task_struct *thread_blocked = elem2entry(struct task_struct, general_tag, list_pop(&psema->waiters));
        thread_unblock(thread_blocked);
    }
    
    psema->value++;
    ASSERT(psema->value == 1);
    
    intr_set_status(old_status);
}

/* 初始化锁 */
void lock_init(struct lock *plock) 
{
    plock->holder = NULL;
    plock->holder_repeat_nr = 0;
    sema_init(&plock->semaphore, 1);       // 初始化锁的信号量
}

/* 获取锁 */
void lock_acquire(struct lock *plock)
{
    if (plock->holder != running_thread())
    {
        sema_down(&plock->semaphore);
        plock->holder = running_thread();
        ASSERT(plock->holder_repeat_nr == 0);
        plock->holder_repeat_nr = 1;
    } 
    else
    {
        plock->holder_repeat_nr++;
    }
}

/* 释放锁 */
void lock_release(struct lock *plock)
{
    ASSERT(plock->holder == running_thread());
    if (plock->holder_repeat_nr > 1)
    {
        plock->holder_repeat_nr--;
        return;
    }
    ASSERT(plock->holder_repeat_nr == 1);
    
    plock->holder = NULL;           // 把锁的持有者置空
    plock->holder_repeat_nr = 0;
    sema_up(&plock->semaphore);
}
