#include "global.h"
#include "file.h"
#include "fs.h"
#include "super_block.h"
#include "inode.h"
#include "stdio_kern.h"
#include "memory.h"
#include "debug.h"
#include "interrupt.h"
#include "string.h"
#include "thread.h"

#define DEFAULT_SECS    1

/* 打开文件表 */
struct file file_table[MAX_FILE_OPEN];

/* 从文件表中获取一个空闲位置，成功返回下标，失败返回-1 */
int32_t get_free_slot_in_global(void)
{
    uint32_t fd_idx = 3;
    while (fd_idx < MAX_FILE_OPEN)
    {
        if (file_table[fd_idx].fd_inode == NULL) break;
        fd_idx++;
    }
    if (fd_idx == MAX_FILE_OPEN)
    {
        printk("exceed max open files\n");
        return -1;
    }
    return fd_idx;
}

/* 将全局描述符的下标安装到进程或线程自己的文件描述符数组
   成功返回安装在PCD中文件描述符表的下标，失败返回-1 */
int32_t pcb_fd_install(uint32_t globa_fd_idx)
{
    struct task_struct *cur = running_thread();
    uint8_t local_fd_idx = 3;       // 跨过stdin，stdout，stderr
    while (local_fd_idx < MAX_FILES_OPEN_PER_PROC)
    {
        if (cur->fd_table[local_fd_idx] == -1)      // -1表示空闲 
        {
            cur->fd_table[local_fd_idx] = globa_fd_idx;
            break;
        }
        local_fd_idx++;
    }
    if (local_fd_idx == MAX_FILES_OPEN_PER_PROC)
    {
        printk("exceed max open files_per_proc\n");
        return -1;
    }
    return local_fd_idx;
}

/* 分配一个i节点，成功返回i节点号，失败返回-1 */
int32_t inode_bitmap_alloc(struct partition *part)
{
    int32_t bit_idx = bitmap_scan(&part->inode_bitmap, 1);
    if (bit_idx == -1) return -1;
    
    bitmap_set(&part->inode_bitmap, bit_idx, 1);
    return bit_idx;
}

/* 分配1个扇区，返回其扇区lba地址，失败返回-1 */
int32_t block_bitmap_alloc(struct partition *part)
{
    int32_t bit_idx = bitmap_scan(&part->block_bitmap, 1);
    if (bit_idx == -1) return -1;
    
    bitmap_set(&part->block_bitmap, bit_idx, 1);
    /* 次函数返回不是位图索引，而是具体可用的扇区LBA地址 */
    return (part->sb->data_start_lba + bit_idx);
}

/* 将内存中的bitmap第bit_idx位所在的512字节扇区同步到硬盘 */
void bitmap_sync(struct partition *part, uint32_t bit_idx, uint8_t btmp_type)
{
    uint32_t off_sec = bit_idx / 4096;          // 本i节点索引相对与位图扇区的偏移扇区
    uint32_t off_size = off_sec * BLOCK_SIZE;   // 本i节点索引相对于位图的字节偏移
    uint32_t sec_lba;
    uint8_t *bitmap_off;   

    /* 需要被同步到硬盘的位图只有inode_bitmap和block_bitmap */
    switch (btmp_type)
    {
        case INODE_BITMAP:
            sec_lba = part->sb->inode_bitmap_lba + off_sec;
            bitmap_off = part->inode_bitmap.bits + off_size;
            break;   
        
        case BLOCK_BITMAP:
            sec_lba = part->sb->block_bitmap_lba + off_sec;
            bitmap_off = part->block_bitmap.bits + off_size;
            break;
    }
    ide_write(part->my_disk, sec_lba, bitmap_off, 1);
}

/* 创建文件，成功返回文件描述符，失败返回-1 */
int32_t file_create(struct dir *parent_dir, char *filename, uint8_t flag);
