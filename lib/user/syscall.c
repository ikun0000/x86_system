#include "syscall.h"

/* 无参数系统调用 */
#define _syscall0(NUMBER)                           \
({                                                  \
    int retval;                                     \
    asm volatile ("int $0x80"                       \
                  :"=a"(retval)                     \
                  :"a"(NUMBER)                      \
                  :"memory");                       \
    retval;                                         \
})

/* 一个参数的系统调用 */
#define _syscall1(NUMBER, ARG1)                     \
({                                                  \
    int retval;                                     \
    asm volatile ("int $0x80"                       \
                  :"=a"(retval)                     \
                  :"a"(NUMBER), "b"(ARG1)           \
                  :"memory");                       \
    retval;                                         \
})

/* 两个参数的系统调用 */
#define _syscall2(NUMBER, ARG1, ARG2)                \
({                                                  \
    int retval;                                     \
    asm volatile ("int $0x80"                       \
                  :"=a"(retval)                     \
                  :"a"(NUMBER), "b"(ARG1), "c"(ARG2)\
                  :"memory");                       \
    retval;                                         \
})

/*三个参数的系统调用*/
#define _syscall3(NUMBER, ARG1, ARG2, ARG3)         \
({                                                  \
    int retval;                                     \
    asm volatile ("int $0x80"                       \
                  :"=a"(retval)                     \
                  :"a"(NUMBER), "b"(ARG1),          \
                   "c"(ARG2), "d"(ARG3)             \
                  :"memory");                       \
    retval;                                         \
})

/* 获取当前进程的PID */
uint32_t getpid()
{
    return _syscall0(SYS_GETPID);
}

/* 打印字符串 */
uint32_t write(int32_t fd, const void *buf, uint32_t count)
{
    return _syscall3(SYS_WRITE, fd, buf, count);
}

/* 申请size字节大小的内存，并返回其起始地址，失败返回NULL */
void *malloc(uint32_t size)
{
    return (void *)_syscall1(SYS_MALLOC, size);
}

/* 释放ptr指向的内存 */
void free(void *ptr)
{
    _syscall1(SYS_FREE, ptr);
}

/* 复制当前进程镜像 */
int16_t fork(void)
{
    return _syscall0(SYS_FORK);
}

/* 从文件描述符fd中读取count个字节到buf */
int32_t read(int32_t fd, void *buf, uint32_t count)
{
    return _syscall3(SYS_READ, fd, buf, count);
}

/* 输出一个字符 */
void putchar(char char_ascii)
{
    _syscall1(SYS_PUTCHAR, char_ascii);
}

/* 清空屏幕 */
void clear(void)
{
    _syscall0(SYS_CLEAR);
}
