#include "pipe.h"
#include "memory.h"
#include "fs.h"
#include "file.h"
#include "ioqueue.h"
#include "thread.h"

/* pandan文件爱呢描述符local_fd是不是管道 */
int is_pipe(uint32_t local_fd)
{
    uint32_t global_fd = fd_local2global(local_fd);
    return file_table[global_fd].fd_flag == PIPE_FLAG;
}

/* 创建管道，成功返回0，失败返回-1 */
int32_t sys_pipe(int32_t pipefd[2])
{
    int32_t global_fd = get_free_slot_in_global();
    
    /* 申请一页内核空间做环形缓冲区 */
    file_table[global_fd].fd_inode = get_kernel_pages(1);
    
    /* 初始化唤醒缓冲区 */
    ioqueue_init((struct ioqueue *)file_table[global_fd].fd_inode);
    if (file_table[global_fd].fd_inode == NULL) return -1;

    /* 将fd_flag改为管道标志 */
    file_table[global_fd].fd_flag = PIPE_FLAG;
    
    file_table[global_fd].fd_pos = 2;
    pipefd[0] = pcb_fd_install(global_fd);
    pipefd[1] = pcb_fd_install(global_fd);
    return 0;
}

/* 从管道中读取数据 */
uint32_t pipe_read(int32_t fd, void *buf, uint32_t count)
{
    char *buffer = buf;
    uint32_t bytes_read = 0;
    uint32_t global_fd = fd_local2global(fd);
    
    /* 获取管道环形缓冲区 */
    struct ioqueue *ioq = (struct ioqueue *)file_table[global_fd].fd_inode;

    /* 读取数据不能超过队列数据大小，避免阻塞 */
    uint32_t ioq_len = ioq_length(ioq);
    uint32_t size = ioq_len > count ? count : ioq_len;
    while (bytes_read < size)
    {
        *buffer = ioq_getchar(ioq);
        bytes_read++;
        buffer++;
    }
    return bytes_read;
}

/* 往管道中写入数据 */
uint32_t pipe_write(int32_t fd, const void *buf, uint32_t count)
{
    uint32_t bytes_write = 0;
    uint32_t global_fd = fd_local2global(fd);
    struct ioqueue *ioq = (struct ioqueue *)file_table[global_fd].fd_inode;

    /* 如果写入的数据超过队列长度则只写到队列长度，避免阻塞 */
    uint32_t ioq_left = bufsize - ioq_length(ioq);
    uint32_t size = ioq_left > count ? count : ioq_left;

    const char *buffer = buf;
    while (bytes_write < size)
    {
        ioq_putchar(ioq, *buffer);
        bytes_write++;
        buffer++;
    }
    return bytes_write;
}
