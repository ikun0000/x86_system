#include "inode.h"
#include "fs.h"
#include "file.h"
#include "global.h"
#include "debug.h"
#include "memory.h"
#include "interrupt.h"
#include "list.h"
#include "stdio_kern.h"
#include "string.h"
#include "super_block.h"
#include "thread.h"

/* 用来存储inode位置 */
struct inode_position
{
    int two_sec;            // inode是否跨扇区
    uint32_t sec_lba;       // inode所在的扇区号
    uint32_t off_size;      // inode在扇区内的字节偏移量
};

/* 获取inode所在的扇区和扇区内的偏移量 */
static void inode_locate(struct partition *part, uint32_t inode_no, struct inode_position *inode_pos) 
{
    ASSERT(inode_no < 4096);
    uint32_t inode_table_lba = part->sb->inode_table_lba;
    uint32_t inode_size = sizeof(struct inode);
    uint32_t off_size = inode_no * inode_size;      // 第inode_no的i节点对于inode_table_lba的字节偏移量
    uint32_t off_sec = off_size / 512;              // i节点对于inode_table_lba的扇区偏移量
    uint32_t off_size_in_sec = off_size % 512;      // i节点在其扇区内的字节偏移量

    /* 判断此i节点是否跨越两个扇区 */   
    uint32_t left_in_sec = 512 - off_size_in_sec;
    if (left_in_sec < inode_size) inode_pos->two_sec = 1;
    else inode_pos->two_sec = 0;
    
    inode_pos->sec_lba = inode_table_lba + off_sec;
    inode_pos->off_size = off_size_in_sec;
}

/* 根据i节点号返回相应的i节点 */
struct inode *inode_open(struct partition *part, uint32_t inode_no)
{
    /* 先在已打开的inode链表中找inode */
    struct list_elem *elem = part->open_inodes.head.next;
    struct inode *inode_found;
    while (elem != &part->open_inodes.tail)
    {
        inode_found = elem2entry(struct inode, inode_tag, elem);
        if (inode_found->i_no == inode_no) 
        {
            inode_found->i_open_cnts++;
            return inode_found;
        }
        elem = elem->next;
    }

    /* 由于open_inodes链表找不到，所以要在硬盘读入此inode并加入到链表 */
    struct inode_position inode_pos;
    inode_locate(part, inode_no, &inode_pos);

    /* 因为inode节点被所有进程共享，所以其分配的内存必须位于内核区，所以这么干 */
    struct task_struct *cur = running_thread();
    uint32_t *cur_pagedir_bak = cur->pgdir;
    cur->pgdir = NULL;
    /* 以上3行代码执行完后分配的内存位于内核 */
    inode_found = (struct inode *)sys_malloc(sizeof(struct inode));
    /* 恢复pgdir */
    cur->pgdir = cur_pagedir_bak;

    char *inode_buf;
    if (inode_pos.two_sec) 
    {
        inode_buf = (char *)sys_malloc(1024);
        ide_read(part->my_disk, inode_pos.sec_lba, inode_buf, 2);
    }
    else
    {
        inode_buf = (char *)sys_malloc(512);
        ide_read(part->my_disk, inode_pos.sec_lba, inode_buf, 1);
    }
    memcpy(inode_found, inode_buf + inode_pos.off_size, sizeof(struct inode));
    
    /* 执行该函数大概率之后会接着使用这个inode，所以插入队首 */
    list_push(&part->open_inodes, &inode_found->inode_tag);
    inode_found->i_open_cnts = 1;
    
    sys_free(inode_buf);
    return inode_found;
}

/* 将inode写入到分区part */
void inode_sync(struct partition *part, struct inode *inode, void *io_buf)
{
    /* io_buf是在内存中的I/O缓冲区 */
    uint8_t inode_no = inode->i_no;
    struct inode_position inode_pos;
    /* 获取inode信息到inode_pos */
    inode_locate(part, inode_no, &inode_pos);
    ASSERT(inode_pos.sec_lba <= (part->start_lba + part->sec_cnt));
    
    /* inode中的inode_tag和i_open_cnts只在内存中有用，用于记录多进程共享文件 */
    struct inode pure_inode;
    memcpy(&pure_inode, inode, sizeof(struct inode));   

    pure_inode.i_open_cnts = 0;
    pure_inode.write_deny = 0;          // 为0保证硬盘中读出时可写
    pure_inode.inode_tag.prev = pure_inode.inode_tag.next = NULL;

    char *inode_buf = (char *)io_buf;
    if (inode_pos.two_sec)
    {
        /* 如果inode跨越两个扇区 */
        ide_read(part->my_disk, inode_pos.sec_lba, inode_buf, 2);
        /* 写入inode到缓冲区对应偏移 */
        memcpy((inode_buf + inode_pos.off_size), &pure_inode, sizeof(struct inode));
        /* 把I/O缓冲区的记录写回到硬盘 */
        ide_write(part->my_disk, inode_pos.sec_lba, inode_buf, 2);
    }
    else
    {
        /* inode全部在一个扇区 */
        ide_read(part->my_disk, inode_pos.sec_lba, inode_buf, 1);
        memcpy((inode_buf + inode_pos.off_size), &pure_inode, sizeof(struct inode));
        ide_write(part->my_disk, inode_pos.sec_lba, inode_buf, 1);
    }
}

/* 关闭inode或减少inode打开次数 */
void inode_close(struct inode *inode)
{
    enum intr_status old_status = intr_disable();
    if (--(inode->i_open_cnts) == 0)
    {
        // 如果关闭后打开次数为0则释放
        list_remove(&inode->inode_tag);
        
        /* 以下操作跟打开inode类似，因为inode在内核空间，
           为了能释放内核空间的内存暂时把该线程的pgdir指向NULL */
        struct task_struct *cur = running_thread();    
        uint32_t *cur_pagedir_bak = cur->pgdir;
        cur->pgdir = NULL;
        sys_free(inode);
        cur->pgdir = cur_pagedir_bak;
    }

    intr_set_status(old_status);
}

/* 初始化new_inode */
void inode_init(uint32_t inode_no, struct inode *new_inode)
{
    new_inode->i_no = inode_no;
    new_inode->i_size = 0;
    new_inode->i_open_cnts = 0;
    new_inode->write_deny = 0;
    
    /* 初始化块索引数组 */
    uint8_t sec_idx = 0;
    while (sec_idx < 13) new_inode->i_sectors[sec_idx++] = 0;
}


