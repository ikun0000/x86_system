#include "dir.h"
#include "stdint.h"
#include "inode.h"
#include "file.h"
#include "fs.h"
#include "stdio_kern.h"
#include "global.h"
#include "debug.h"
#include "memory.h"
#include "string.h"
#include "interrupt.h"
#include "super_block.h"

struct dir root_dir;            // 根目录

/* 打开根目录 */
void open_root_dir(struct partition *part)
{
    root_dir.inode = inode_open(part, part->sb->root_inode_no);
    root_dir.dir_pos = 0;
}

/* 在分区part上打开i节点为inode_no的目录并返回目录指针 */
struct dir *dir_open(struct partition *part, uint32_t inode_no)
{
    struct dir *pdir = (struct dir *)sys_malloc(sizeof(struct dir));
    pdir->inode = inode_open(part, inode_no);
    pdir->dir_pos = 0;
    return pdir;
}

/* 在part分区内的pdir目录内寻找名为name的文件或目录
   找到返回1，并将其目录项存入dir_e，否则返回0 */
int search_dir_entry(struct partition *part, struct dir *pdir, const char *name, struct dir_entry *dir_e)
{
    uint32_t block_cnt = 140;       // 12个直接块+128个一级间接块
    
    /* 12个直接块大小+128个一级间接块大小 */
    uint32_t *all_blocks = (uint32_t *)sys_malloc(48 + 512);
    if (all_blocks == NULL) 
    {
        printk("search_dir_entry: sys_malloc for all_blocks failed");
        return 0;
    }

    uint32_t block_idx = 0;
    while (block_idx < 12)
    {
        all_blocks[block_idx] = pdir->inode->i_sectors[block_idx];
        block_idx++;
    }
    block_idx = 0;

    if (pdir->inode->i_sectors[12] != 0)        // 若还有一级间接表 
        ide_read(part->my_disk, pdir->inode->i_sectors[12], all_blocks + 12, 1);

   /* 此时all_block存储的是pdir的所有目录项的扇区lba地址 */ 
    
    /* 写目录项的时候已保证目录项不会跨扇区 */
    uint8_t *buf = (uint8_t *)sys_malloc(SECTOR_SIZE);
    struct dir_entry *p_de = (struct dir_entry *)buf;
    uint32_t dir_entry_size = part->sb->dir_entry_size;
    uint32_t dir_entry_cnt = SECTOR_SIZE / dir_entry_size;  // 1扇区可容纳的目录项
    
    /* 开始在所有块中查找符合条件的目录 */
    while (block_idx < block_cnt)
    {
        /* 块地址为0表示块中无数据 */       
        if (all_blocks[block_idx] == 0)
        {
            block_idx++;
            continue;
        }
        ide_read(part->my_disk, all_blocks[block_idx], buf, 1);
        
        uint32_t dir_entry_idx = 0;
        /* 遍历一个扇区的目录项 */
        while (dir_entry_idx < dir_entry_cnt)
        {
            /* 找到了直接复制这个目录项 */
            if (!strcmp(p_de->filename, name))
            {
                memcpy(dir_e, p_de, dir_entry_size);
                sys_free(buf);
                sys_free(all_blocks);
                return 1;
            }

            dir_entry_idx++;
            p_de++;
        }
        
        block_idx++;
        p_de = (struct dir_entry *)buf;         // 从新让p_de指向buf开头，进行下一个block的遍历
        memset(buf, 0, SECTOR_SIZE);            // 清空缓冲区
    }

    sys_free(buf);   
    sys_free(all_blocks);
    return 0;
}

/* 关闭目录 */
void dir_close(struct dir *dir)
{
    /* 根目录不能关闭 */
    if (dir == &root_dir) return;

    inode_close(dir->inode);
    sys_free(dir);
}

/* 在内存中初始化目录项p_de */
void create_dir_entry(char *filename, uint32_t inode_no, uint8_t file_type, struct dir_entry *p_de)
{
    ASSERT(strlen(filename) <= MAX_FILE_NAME_LEN);

    /* 初始化目录项 */
    memcpy(p_de->filename, filename, strlen(filename));
    p_de->i_no = inode_no;
    p_de->f_type = file_type;
}

/* 将目录项p_de写入到父目录parent_dir中 */
int sync_dir_entry(struct dir* parent_dir, struct dir_entry *p_de, void *io_buf)
{
    struct inode *dir_inode = parent_dir->inode;
    uint32_t dir_size = dir_inode->i_size;
    uint32_t dir_entry_size = cur_part->sb->dir_entry_size;

    /* dir_size应该是dir_entry_size的整数倍 */
    ASSERT(dir_size % dir_entry_size == 0);
    
    uint32_t dir_entrys_per_sec = (512 / dir_entry_size);        // 每扇区容纳的目录项数
    int32_t block_lba = -1;

    /* 记录父目录的内容所在扇区 */
    uint8_t block_idx = 0;
    uint32_t all_blocks[140] = {0, };

    /* 复制前12个直接项 */
    while (block_idx < 12)
    {
        all_blocks[block_idx] = dir_inode->i_sectors[block_idx];
        block_idx++;
    }
    
    struct dir_entry *dir_e = (struct dir_entry *)io_buf;
    int32_t block_bitmap_idx = -1;

    /* 开始遍历所有块寻找目录项空位，
       若扇区中没有空位在不超过文件大小的情况下申请新的扇区存放新的目录项 */
    block_idx = 0;
    while (block_idx < 140)
    {
        block_bitmap_idx = -1;
        if (all_blocks[block_idx] == 0)
        {
            // 之前的block都满了，但是该目录还有空闲的block
            block_lba = block_bitmap_alloc(cur_part);
            if (block_lba == -1) 
            {
                printk("alloc block bitmap for sync_dir_entry failed\n");
                return 0;
            }

            /* 每分配一个块就同步一次bitmap */   
            block_bitmap_idx = block_lba - cur_part->sb->data_start_lba;
            ASSERT(block_bitmap_idx != -1);
            bitmap_sync(cur_part, block_bitmap_idx, BLOCK_BITMAP);

            block_bitmap_idx = -1;
            if (block_idx < 12)
            {
                // 直接块
                dir_inode->i_sectors[block_idx] = all_blocks[block_idx] = block_lba;
            }
            else if (block_idx == 12)
            {
                // 直接块全部分配完毕，所以必须分配一级间接块
                dir_inode->i_sectors[12] = block_lba;           // 之前分配的块作为一级间接表的位置
                block_lba = -1;
                block_lba = block_bitmap_alloc(cur_part);       // 在分配一个块作为目录的数据块  
                if (block_lba == -1)
                {
                    /* 如果分配失败释放一级块的地址 */
                    block_bitmap_idx = dir_inode->i_sectors[12] - cur_part->sb->data_start_lba;
                    bitmap_set(&cur_part->block_bitmap, block_bitmap_idx, 0);
                    dir_inode->i_sectors[12] = 0;
                    printk("alloc block bitmap for sync_dir_entry failed!\n");
                    return 0;
                }
                
                /* 同步block_bitmap到磁盘 */
                block_bitmap_idx = block_lba - cur_part->sb->data_start_lba;
                ASSERT(block_bitmap_idx != -1);
                bitmap_sync(cur_part, block_bitmap_idx, BLOCK_BITMAP);
                
                all_blocks[12] = block_lba;
                /* 把分配的第0个间接块地址写入到磁盘的一级块表中 */
                ide_write(cur_part->my_disk, dir_inode->i_sectors[12], all_blocks + 12, 1);
            }
            else
            {
                /* 间接块没有分配 */
                all_blocks[block_idx] = block_lba;
                /* 把新分配的第block_idx-12间接块写入一级间接表 */
                ide_write(cur_part->my_disk, dir_inode->i_sectors[12], all_blocks + 12, 1);
            }

            /* 将新的目录向p_de写入到新分配的间接表块 */
            memset(io_buf, 0, 512);
            memcpy(io_buf, p_de, dir_entry_size);   
            ide_write(cur_part->my_disk, all_blocks[block_idx], io_buf, 1);
            dir_inode->i_size += dir_entry_size;
            return 1;
        }

        /* 如果block_idx块已经存在，将其读进入内存中，然后在该块中查找空位 */
        ide_read(cur_part->my_disk, all_blocks[block_idx], io_buf, 1);
        /* 在扇区内查找空目录项 */
        uint8_t dir_entry_idx = 0;
        while (dir_entry_idx < dir_entrys_per_sec)
        {
            if ((dir_e + dir_entry_idx)->f_type == FT_UNKNOWN)
            {
                // FT_UNKNOWN为0，初始化和删除文件都会把f_type设置为FT_UNKNOWN
                memcpy(dir_e + dir_entry_idx, p_de, dir_entry_size);
                ide_write(cur_part->my_disk, all_blocks[block_idx], io_buf, 1);
                
                dir_inode->i_size += dir_entry_size;
                return 1;
            }
            
            dir_entry_idx++;
        }
        
        block_idx++;
    }
    
    printk("directory is full!\n");
    return 0;
}

/* 把分区part的目录pdir中编号为inode_no的目录项删除 */
int delete_dir_entry(struct partition *part, struct dir *pdir, uint32_t inode_no, void *io_buf)
{
    struct inode *dir_inode = pdir->inode;
    uint32_t block_idx = 0, all_blocks[140] = {0, };

    /* 收集目录全部块地址 */
    while (block_idx < 12)
    {
        all_blocks[block_idx] = dir_inode->i_sectors[block_idx];
        block_idx++;
    }
    if (dir_inode->i_sectors[12]) ide_read(part->my_disk, dir_inode->i_sectors[12], all_blocks + 12, 1);

    /* 目录项存储是保证不会跨多个扇区 */
    uint32_t dir_entry_size = part->sb->dir_entry_size;
    uint32_t dir_entrys_per_sec = (SECTOR_SIZE / dir_entry_size);
    struct dir_entry *dir_e = (struct dir_entry *)io_buf;
    struct dir_entry *dir_entry_found = NULL;
    uint8_t dir_entry_idx, dir_entry_cnt;
    int is_dir_first_block = 0;

    /* 遍历所有块找目录项 */
    block_idx = 0;
    while (block_idx < 140)
    {
        is_dir_first_block = 0;
        if (all_blocks[block_idx] == 0)
        {
            block_idx++;
            continue;
        }
        dir_entry_idx = dir_entry_cnt = 0;
        memset(io_buf, 0, SECTOR_SIZE);
        /* 读取目录项所在的扇区 */
        ide_read(part->my_disk, all_blocks[block_idx], io_buf, 1);
        
        /* 遍历该扇区所有目录项 */
        while (dir_entry_idx < dir_entrys_per_sec)
        {
            if ((dir_e + dir_entry_idx)->f_type != FT_UNKNOWN)
            {
                if (!strcmp((dir_e + dir_entry_idx)->filename, "."))
                {
                    is_dir_first_block = 1;
                }
                else if (strcmp((dir_e + dir_entry_idx)->filename, ".") &&
                         strcmp((dir_e + dir_entry_idx)->filename, ".."))
                {
                    /* 排除.和..目录项 */
                    dir_entry_cnt++;            // 统计此扇区的目录项个数，用于判断是否需要回收扇区
                    if ((dir_e + dir_entry_idx)->i_no == inode_no)
                    {
                        /* 找到目标的目录项，继续遍历统计目录项数 */
                        ASSERT(dir_entry_found == NULL);
                        dir_entry_found = dir_e + dir_entry_idx;
                    }
                }
            }
            dir_entry_idx++;
        }

        /* 如果此扇区没有找到目标目录项，继续到下一个扇区 */       
        if (dir_entry_found == NULL) 
        {
            block_idx++;
            continue;
        }

        ASSERT(dir_entry_cnt >= 1);
        /* 除了第一个扇区外其余扇区如果只有目录项自己则回收扇区 */
        if (dir_entry_cnt == 1 && !is_dir_first_block)
        {
            /* 在块位图中回收块 */
            uint32_t block_bitmap_idx = all_blocks[block_idx] - part->sb->data_start_lba;
            bitmap_set(&part->block_bitmap, block_bitmap_idx, 0);
            bitmap_sync(cur_part, block_bitmap_idx, BLOCK_BITMAP);

            /* 将块地址从数组的i_sectors中去除 */
            if (block_idx < 12)
            {
                dir_inode->i_sectors[block_idx] = 0;
            }
            else
            {
                /* 块在一级间接表中 */
                uint32_t indirect_blocks = 0;
                uint32_t indirect_block_idx = 12;
                /* 判断一级间接表中块的数量，如果只有一个则连同一级间接表一起清空 */
                while (indirect_block_idx < 140)
                {
                    if (all_blocks[indirect_block_idx] != 0)
                    {
                        indirect_blocks++;
                    }
                }
                ASSERT(indirect_blocks >= 1);

                if (indirect_blocks > 1)
                {
                    /* 间接表中不知一个块 */
                    all_blocks[block_idx] = 0;
                    ide_write(part->my_disk, dir_inode->i_sectors[12], all_blocks + 12, 1);
                }
                else
                {
                    /* 间接表中只有一个块 */
                    block_bitmap_idx = dir_inode->i_sectors[12] - part->sb->data_start_lba;
                    bitmap_set(&part->block_bitmap, block_bitmap_idx, 0);
                    bitmap_sync(cur_part, block_bitmap_idx, BLOCK_BITMAP);
                    
                    dir_inode->i_sectors[12] = 0;
                }
            }
        }
        else
        {
            /* 直接将目录项清空 */
            memset(dir_entry_found, 0, dir_entry_size);
            ide_write(part->my_disk, all_blocks[block_idx], io_buf, 1);
        }

        /* 更改i节点信息同步硬盘 */
        ASSERT(dir_inode->i_size >= dir_entry_size);
        dir_inode->i_size -= dir_entry_size;
        memset(io_buf, 0, SECTOR_SIZE * 2);
        inode_sync(part, dir_inode, io_buf);

        return 1;
    }
    
    /* 没有找到目标目录项 */
    return 0;
}
