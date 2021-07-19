#include "ioqueue.h"
#include "interrupt.h"
#include "global.h"
#include "debug.h"

/* 初始化队列 */
void ioqueue_init(struct ioqueue *ioq)
{
    lock_init(&ioq->lock);
    ioq->producer = ioq->consumer = NULL;
    ioq->head = ioq->tail = 0;
}

/* 返回pos在缓冲区的下一个位置索引 */
static int32_t next_pos(int32_t pos) 
{
    return (pos + 1) % bufsize;
}

/* 判断队列是否已满 */
int ioq_full(struct ioqueue *ioq)
{
    ASSERT(intr_get_status() == INTR_OFF);   
    return next_pos(ioq->head) == ioq->tail;
}

/* 判断队列首否为空 */
static int ioq_empty(struct ioqueue *ioq)
{
    ASSERT(intr_get_status() == INTR_OFF);
    return ioq->head == ioq->tail;
}

/* 使当前生产者或者消费者在队列上等待 */
static void ioq_wait(struct task_struct **waiter) 
{
    ASSERT(*waiter == NULL && waiter != NULL);
    *waiter = running_thread();
    thread_block(TASK_BLOCKED);
}

/* 唤醒waiter */
static void wakeup(struct task_struct **waiter)
{
    ASSERT(*waiter != NULL);
    thread_unblock(*waiter);
    *waiter = NULL;
}

/* 消费者在队列中获取一个字符 */
char ioq_getchar(struct ioqueue *ioq)
{
    ASSERT(intr_get_status() == INTR_OFF);

    /* 如果当前队列为空则把当前线程记录在consumer上，
       让生产者知道唤醒哪个消费者 */
    while (ioq_empty(ioq))
    {
        lock_acquire(&ioq->lock);
        ioq_wait(&ioq->consumer);
        lock_release(&ioq->lock);
    }   

    /* 获取数据 */
    char byte = ioq->buf[ioq->tail];
    ioq->tail = next_pos(ioq->tail);
    
    /* 如果有生产者在等待则唤醒生产者 */
    if (ioq->producer != NULL) wakeup(&ioq->producer);
    
    return byte;
}

/* 消费者在队列中消费一个数据 */
void ioq_putchar(struct ioqueue *ioq, char byte)
{
    ASSERT(intr_get_status() == INTR_OFF);

    /* 如果队列已满则当前生产者阻塞，并记录到producer上，
       以便后续被唤醒 */
    while (ioq_full(ioq))
    {
        lock_acquire(&ioq->lock);
        ioq_wait(&ioq->producer);
        lock_release(&ioq->lock);
    }
    
    /* 写入数据 */
    ioq->buf[ioq->head] = byte;
    ioq->head = next_pos(ioq->head);
    
    /* 唤醒等待的消费者 */
    if (ioq->consumer != NULL) wakeup(&ioq->consumer);
}
