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
int32_t file_create(struct dir *parent_dir, char *filename, uint8_t flag)
{
    /* 后续操作的公共缓冲区 */
    void *io_buf = sys_malloc(1024);
    if (io_buf == NULL) 
    {
        printk("in file_creat: sys_malloc for io_buf failed\n");
        return -1;
    }
    
    uint8_t rollback_step = 0;      // 用于记录操作失败回滚的资源状态
    
    /* 为新文件分配anode */
    int32_t inode_no = inode_bitmap_alloc(cur_part);
    if (inode_no == -1) 
    {
        printk("in file_creat: allocate inode failed\n");
        return -1;
    }

    /* file_table数组中的文件描述符的inode会指向他 */
    struct task_struct *cur = running_thread();
    uint32_t *cur_pagedir_bak = cur->pgdir;
    cur->pgdir = NULL;
    /* 以上3行代码执行完后分配的内存位于内核 */
    struct inode *new_file_inode = (struct inode *)sys_malloc(sizeof(struct inode));
    /* 恢复pgdir */
    cur->pgdir = cur_pagedir_bak;

    
    /* 
        下面是原来的代码，但是有一个bug，当是在特权级3调用创建文件的时候，其申请的inode地址是
        用户空间的地址，使用close关闭的时候会出错，因为系统默认是把所有inode地址都是呢和空间。

        struct inode *new_file_inode = (struct inode *)sys_malloc(sizeof(struct inode));
    */
    if (new_file_inode == NULL)
    {
        printk("file_create: sys_malloc for inode failed\n");
        rollback_step = 1;
        goto rollback;
    }
    inode_init(inode_no, new_file_inode);

    /* 返回file_table空闲的下标 */
    int fd_idx = get_free_slot_in_global();
    if (fd_idx == -1)
    {
        printk("exceed max open files\n");
        rollback_step = 2;
        goto rollback;
    }

    file_table[fd_idx].fd_inode = new_file_inode;
    file_table[fd_idx].fd_pos = 0;
    file_table[fd_idx].fd_flag = flag;
    file_table[fd_idx].fd_inode->write_deny = 0;

    struct dir_entry new_dir_entry;
    memset(&new_dir_entry, 0, sizeof(struct dir_entry));

    /* 只会在内存中操作 */
    create_dir_entry(filename, inode_no, FT_REGULAR, &new_dir_entry);

    /* 同步内存数据到硬盘 */   
    /* 在parent_dir下安装新的目录项 */
    if (!sync_dir_entry(parent_dir, &new_dir_entry, io_buf))
    {
        printk("sync dir_entry to disk failed\n");
        rollback_step = 3;
        goto rollback;
    }
    
    memset(io_buf, 0, 1024);   
    /* 将父目录i节点的内容同步到硬盘 */
    inode_sync(cur_part, parent_dir->inode, io_buf);
    
    memset(io_buf, 0, 1024);
    /* 将新创建的文件的i节点同步到硬盘 */
    inode_sync(cur_part, new_file_inode, io_buf);

    /* 将inode_bitmap位图同步到硬盘 */
    bitmap_sync(cur_part, inode_no, INODE_BITMAP);
    
    /* 将创建的文件的i节点添加到open_inodes链表 */
    list_push(&cur_part->open_inodes, &new_file_inode->inode_tag);
    new_file_inode->i_open_cnts = 1;
    
    sys_free(io_buf);
    return pcb_fd_install(fd_idx);

/* 某个操作失败回滚 */
rollback:
    switch (rollback_step)
    {
        case 3:
            /* 失败时将file_table对应的项清空 */
            memset(&file_table[fd_idx], 0, sizeof(struct file));
        case 2:
            sys_free(new_file_inode);
        case 1:
            /* 如果新文件的i节点分配失败，则之前在位图中分配的inode_no也清空 */
            bitmap_set(&cur_part->inode_bitmap, inode_no, 0);
            break;
    }
    sys_free(io_buf);
    return -1;
}

/* 打开编号为inode_no的inode对应的文件，成功返回文件描述符，否则返回-1 */
int32_t file_open(uint32_t inode_no, uint8_t flag)
{
    int fd_idx = get_free_slot_in_global();
    if (fd_idx == -1) 
    {
        printk("exceed max open files\n");
        return -1;
    }

    file_table[fd_idx].fd_inode = inode_open(cur_part, inode_no);
    file_table[fd_idx].fd_pos = 0;          // 默认文件指针指向文件头
    file_table[fd_idx].fd_flag = flag;
    int *write_deny = &file_table[fd_idx].fd_inode->write_deny;

    if (flag & O_WRONLY || flag & O_RDWR)
    {
        /* 如果打开方式包含写操作，判断是否有别的进程打开
           如果是读操作，不考虑write_deny */
        enum intr_status old_status = intr_disable();
        
        if (!(*write_deny))
        {
            /* 没有被其他进程占用 */
            *write_deny = 1;
            intr_set_status(old_status);
        }
        else
        {
            /* 已被别的进程占用 */
            intr_set_status(old_status);
            printk("file can't be write now, try again later\n");
            return -1;
        }
    }
    /* 如果是读文件，直接返回 */
    return pcb_fd_install(fd_idx);
}

/* 关闭文件 */
int32_t file_close(struct file *file)
{
    if (file == NULL) return -1;
    
    file->fd_inode->write_deny = 0;
    inode_close(file->fd_inode);
    file->fd_inode = NULL;          // 使文件结构可用
    return 0;
}

/* 将buf中的count个字节写入到file，成功返回字节数，失败返回-1 */
int32_t file_write(struct file *file, const void *buf, uint32_t count)
{
    if ((file->fd_inode->i_size + count) > (BLOCK_SIZE * 140))
    {
        printk("exceed max file_size 71680 bytes, write file failed\n");
        return -1;
    }   

    uint8_t *io_buf = sys_malloc(BLOCK_SIZE);
    if (io_buf == NULL)
    {
        printk("file_write: sys_malloc for io_buf failed\n");
        return -1;
    }

    /* 用于记录文件的块所在的LBA地址 */
    uint32_t *all_blocks = (uint32_t *)sys_malloc(BLOCK_SIZE + 48);
    if (all_blocks == NULL)
    {
        printk("file_write: sys_malloc for all_blocks failed\n");
        return -1;
    }
    
    const uint8_t *src = buf;           // src指向buf中待写入的数据   
    uint32_t bytes_written = 0;         // 用来记录写入数据的大小
    uint32_t size_left = count;         // 用于记录还没写入数据的大小
    int32_t block_lba = -1;             // 块地址
    uint32_t block_bitmap_idx = 0;      // 用于记录blocks对应block_bitmap中的索引
    uint32_t sec_idx;                   // 用于索引扇区
    uint32_t sec_lba;                   // 扇区地址
    uint32_t sec_off_bytes;             // 扇区内字节偏移
    uint32_t sec_left_bytes;            // 扇区内剩余字节数
    uint32_t chunk_size;                // 每次写入硬盘的数据块大小
    int32_t indirect_block_table;       // 用于获取一级间接表地址
    uint32_t block_idx;                 // 块索引

    /* 判断文件是否是第一次写，如果是，先为其分配一个块 */
    if (file->fd_inode->i_sectors[0] == 0)
    {
        block_lba = block_bitmap_alloc(cur_part);
        if (block_lba == -1)
        {
            printk("file_write: block_bitmap_alloc failed\n");
            return -1;
        }
        file->fd_inode->i_sectors[0] = block_lba;
        
        /* 同步块位图到硬盘 */
        block_bitmap_idx = block_lba - cur_part->sb->data_start_lba;
        ASSERT(block_bitmap_idx != 0);
        bitmap_sync(cur_part, block_bitmap_idx, BLOCK_BITMAP);
    }

    /* 写入count个字节前，该文件已经占用的块数 */
    uint32_t file_has_used_blocks = file->fd_inode->i_size / BLOCK_SIZE + 1;

    /* 以下的计算不管数据是否刚好占用整数被的扇区（如：512，1024，2048）在计算
       的时候都会多加一个扇区，比如文件的大小是1024则i_node记录使用了3个扇区，
       这和下面的是否要添加扇区和写入扇区要一起看 */

    /* 存储count字节后该文件占用的块数 */
    uint32_t file_will_use_blocks = (file->fd_inode->i_size + count) / BLOCK_SIZE + 1;
    ASSERT(file_will_use_blocks <= 140);

    /* 通过此增量判断是否需要分配新扇区，如果为0表示不需要新增扇区 */   
    uint32_t add_blocks = file_will_use_blocks - file_has_used_blocks;
    
    /* 将所有文件块地址收集到all_blocks，后面统一在all_blocks中获取写入扇区地址 */
    if (add_blocks == 0)
    {
        /* 在同一个扇区写入数据，不涉及分配新扇区 */
        if (file_has_used_blocks <= 12)
        {
            /* 文件在前12个直接块内 */
            block_idx = file_has_used_blocks - 1;
            all_blocks[block_idx] = file->fd_inode->i_sectors[block_idx];
        }
        else
        {
            /* 已经占用了一级间接块，需要将间接块地址读进来 */
            ASSERT(file->fd_inode->i_sectors[12] != 0);
            indirect_block_table = file->fd_inode->i_sectors[12];
            ide_read(cur_part->my_disk, indirect_block_table, all_blocks + 12, 1);
        }
    }
    else
    {
        /* 需要新增扇区，涉及分配直接块，一级间接块 */
        if (file_will_use_blocks <= 12)
        {
            /* 情况1：12个直接块够用 */
            block_idx = file_has_used_blocks - 1;
            ASSERT(file->fd_inode->i_sectors[block_idx] != 0);
            all_blocks[block_idx] = file->fd_inode->i_sectors[block_idx];

            /* 将未来使用到的扇区分配好写入到all_blocks */
            block_idx = file_has_used_blocks;
            while (block_idx < file_will_use_blocks)
            {
                block_lba = block_bitmap_alloc(cur_part);
                if (block_lba == -1)
                {
                    printk("file_write: block_bitmap_alloc for situation 1 failed\n");
                    return -1;
                }

                /* 写文件时，不应该存在块未使用但已经分配扇区的情况 */
                ASSERT(file->fd_inode->i_sectors[block_idx] == 0);
                file->fd_inode->i_sectors[block_idx] = all_blocks[block_idx] = block_lba;
                
                /* 同步块位图 */
                block_bitmap_idx = block_lba - cur_part->sb->data_start_lba;
                bitmap_sync(cur_part, block_bitmap_idx, BLOCK_BITMAP);
                
                block_idx++;
            }
        } 
        else if (file_has_used_blocks <= 12 && file_will_use_blocks > 12)
        {
            /* 情况2：旧数据在12个扇区之内，新数据需要创建一级间接块 */
            block_idx = file_has_used_blocks - 1;
            all_blocks[block_idx] = file->fd_inode->i_sectors[block_idx];
            
            /* 创建一级间接表 */
            block_lba = block_bitmap_alloc(cur_part);
            if (block_lba == -1)
            {
                printk("file_write: block_bitmap_alloc for situation 2 failed\n");
                return -1;
            }
            
            /* 分配一级间接表地址 */
            ASSERT(file->fd_inode->i_sectors[12] == 0);
            indirect_block_table = file->fd_inode->i_sectors[12] = block_lba;
            
            block_idx = file_has_used_blocks;
            while (block_idx < file_will_use_blocks)
            {
                block_lba = block_bitmap_alloc(cur_part);
                if (block_lba == -1)
                {
                    printk("file_write: block_bitmap_alloc for situation 2 failed\n");
                    return -1;
                }
                
                if (block_idx < 12)
                {
                    ASSERT(file->fd_inode->i_sectors[block_idx] == 0);
                    file->fd_inode->i_sectors[block_idx] = all_blocks[block_idx] = block_lba;
                }
                else
                {
                    all_blocks[block_idx] = block_lba;
                }

                /* 同步块位图到磁盘 */
                block_bitmap_idx = block_lba - cur_part->sb->data_start_lba;
                bitmap_sync(cur_part, block_bitmap_idx, BLOCK_BITMAP);
                
                block_idx++;
            }
            /* 把一级间接表内容写入磁盘 */
            ide_write(cur_part->my_disk, indirect_block_table, all_blocks + 12, 1);
        }
        else if (file_has_used_blocks > 12)
        {
            /* 情况3：旧数据已经占用间接块了 */
            ASSERT(file->fd_inode->i_sectors[12] != 0);
            indirect_block_table = file->fd_inode->i_sectors[12];
            /* 读取间接表 */
            ide_read(cur_part->my_disk, indirect_block_table, all_blocks + 12, 1);
            
            block_idx = file_has_used_blocks;
            while (block_idx < file_will_use_blocks)
            {
                block_lba = block_bitmap_alloc(cur_part);
                if (block_lba == -1)
                {
                    printk("file_write: block_bitmap_alloc for situation 3 failed\n");
                    return -1;
                }

                all_blocks[block_idx++] = block_lba;
                
                /* 同步block_bitmap */
                block_bitmap_idx = block_lba - cur_part->sb->data_start_lba;
                bitmap_sync(cur_part, block_bitmap_idx, BLOCK_BITMAP);
            }
            /* 回写一级间接表 */
            ide_write(cur_part->my_disk, indirect_block_table, all_blocks + 12, 1);
        }
    }

    int first_write_block = 1;
    file->fd_pos = file->fd_inode->i_size - 1;
    while (bytes_written < count)
    {
        memset(io_buf, 0, BLOCK_SIZE);
        sec_idx = file->fd_inode->i_size / BLOCK_SIZE;
        sec_lba = all_blocks[sec_idx];
        sec_off_bytes = file->fd_inode->i_size % BLOCK_SIZE;
        sec_left_bytes = BLOCK_SIZE - sec_off_bytes;
        
        /* 判断此次写入硬盘的数据大小 */       
        chunk_size = size_left < sec_left_bytes ? size_left : sec_left_bytes;
        if (first_write_block)
        {
            ide_read(cur_part->my_disk, sec_lba, io_buf, 1);
            first_write_block = 0;
        }

        memcpy(io_buf + sec_off_bytes, src, chunk_size);
        ide_write(cur_part->my_disk, sec_lba, io_buf, 1);
        printk("file write at lba 0x%x\n", sec_lba);
        
        src += chunk_size;
        file->fd_inode->i_size += chunk_size;
        file->fd_pos += chunk_size;
        bytes_written += chunk_size;
        size_left -= chunk_size;
    }

    inode_sync(cur_part, file->fd_inode, io_buf);
    sys_free(all_blocks);
    sys_free(io_buf);
    return bytes_written;
}

/* 从文件file中读取count个字节写入buf，成功返回字节数，失败返回-1 */
int32_t file_read(struct file *file, void *buf, uint32_t count)
{
    uint8_t *buf_dst = (uint8_t *)buf;
    uint32_t size = count, size_left = size;

    /* 如果要读取的字节数超过文件的剩余量，就用剩余量作为读取的字节 */
    if ((file->fd_pos + count) > file->fd_inode->i_size)
    {
        size = file->fd_inode->i_size - file->fd_pos;
        size_left = size;
        if (size == 0) return -1;           // 文件到达结尾
    }

    uint8_t *io_buf = sys_malloc(BLOCK_SIZE);
    if (io_buf == NULL)
    {
        printk("file_read: sys_malloc for io_buf failed\n");
        return -1;
    }
    uint32_t *all_blocks = (uint32_t *)sys_malloc(BLOCK_SIZE + 48);
    if (all_blocks == NULL)
    {
        printk("file_read: sys_malloc for all_blocks failed\n");
        return -1;
    }

    uint32_t block_read_start_idx = file->fd_pos / BLOCK_SIZE;          // 要读取的数据所在块的起始扇区
    uint32_t block_read_end_idx = (file->fd_pos + size) / BLOCK_SIZE;   // 要读取数据的结束扇区
    uint32_t read_blocks = block_read_end_idx - block_read_start_idx;
    ASSERT(block_read_start_idx < 139 && block_read_end_idx < 139);
    
    int32_t indirect_block_table;       // 用于获取一级间接表的地址
    uint32_t block_idx;                 // 获取待读取的地址
    
    /* 构建all_blocks，只读区要用到的地址 */
    if (read_blocks == 0)
    {
        /* 读取的数据在同一个扇区 */   
        ASSERT(block_read_end_idx == block_read_start_idx);
        if (block_read_end_idx < 12)
        {
            /* 待读取的数据在直接块内 */
            block_idx = block_read_end_idx;
            all_blocks[block_idx] = file->fd_inode->i_sectors[block_idx];
        }
        else
        {
            /* 读取的数据在一级间接表内 */
            indirect_block_table = file->fd_inode->i_sectors[12];
            ide_read(cur_part->my_disk, indirect_block_table, all_blocks + 12, 1);   
        }
    }
    else
    {
        /* 读取的数据跨越多个扇区 */

        if (block_read_end_idx < 12)
        {
            /* 情况1：起始块和结束块都在直接块中 */
            block_idx = block_read_start_idx;
            while (block_idx <= block_read_end_idx)
            {
                all_blocks[block_idx] = file->fd_inode->i_sectors[block_idx];
                block_idx++;
            }
        }
        else if (block_read_start_idx < 12 && block_read_end_idx >= 12)
        {
            /* 情况2：起始块在直接块中，结束块在一级间接块中 */
            block_idx = block_read_start_idx;
            while (block_idx < 12)
            {
                all_blocks[block_idx] = file->fd_inode->i_sectors[block_idx];
                block_idx++;
            }
            /* 确保一级页表不为空 */
            ASSERT(file->fd_inode->i_sectors[12] != 0);
            
            /* 从磁盘中读取一级间接表 */
            indirect_block_table = file->fd_inode->i_sectors[12];
            ide_read(cur_part->my_disk, indirect_block_table, all_blocks + 12, 1);
        }
        else
        {
            /* 情况3：起始块和结束块都在一级间接块中 */
            ASSERT(file->fd_inode->i_sectors[12] != 0);
            indirect_block_table = file->fd_inode->i_sectors[12];
            ide_read(cur_part->my_disk, indirect_block_table, all_blocks + 12, 1);
        }
    }

    uint32_t sec_idx, sec_lba, sec_off_bytes, sec_left_bytes, chunk_size;
    uint32_t bytes_read = 0;
    while (bytes_read < size)
    {
        sec_idx = file->fd_pos / BLOCK_SIZE;
        sec_lba = all_blocks[sec_idx];
        sec_off_bytes = file->fd_pos % BLOCK_SIZE;
        sec_left_bytes = BLOCK_SIZE - sec_off_bytes;
        chunk_size = size_left < sec_left_bytes ? size_left : sec_left_bytes;

        memset(io_buf, 0, BLOCK_SIZE);
        ide_read(cur_part->my_disk, sec_lba, io_buf, 1);
        memcpy(buf_dst, io_buf + sec_off_bytes, chunk_size);
        
        buf_dst += chunk_size;
        file->fd_pos += chunk_size;
        bytes_read += chunk_size;
        size_left -= chunk_size;
    }
    
    sys_free(all_blocks);
    sys_free(io_buf);
    return bytes_read;
}

