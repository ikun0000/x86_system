#ifndef __DEVICE_IOQUEUE_H
#define __DEVICE_IOQUEUE_H

#include "stdint.h"
#include "thread.h"
#include "sync.h"

#define bufsize 64

struct ioqueue 
{
    struct lock lock;

    /*
    缓冲区不满时生产者写数据，缓冲区满时消费者处理数据
    用于记录在缓冲区上等待的生产者和消费者
    */
    struct task_struct *producer;
    struct task_struct *consumer;

    char buf[bufsize];
    int32_t head;           // 队首，数据往队首写入
    int32_t tail;           // 队尾，数据从队尾读出
};

void ioqueue_init(struct ioqueue *ioq);
int ioq_full(struct ioqueue *ioq);
char ioq_getchar(struct ioqueue *ioq);
void ioq_putchar(struct ioqueue *ioq, char byte);
uint32_t ioq_length(struct ioqueue *ioq);

#endif
