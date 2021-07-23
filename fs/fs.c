#include "fs.h"
#include "super_block.h"
#include "inode.h"
#include "dir.h"
#include "stdint.h"
#include "stdio_kern.h"
#include "list.h"
#include "string.h"
#include "ide.h"
#include "global.h"
#include "debug.h"
#include "memory.h"

struct partition *cur_part;     // 默认情况下操作系统使用的分区

/* 在分区链表中找到名为part_name的分区，并将其指针复制给cur_part */
static int mount_partition(struct list_elem *pelem, int arg)
{
    char *part_name = (char *)arg;
    struct partition *part = elem2entry(struct partition, part_tag, pelem);
    
    if (!strcmp(part->name, part_name))
    {
        // 找到了
        cur_part = part;
        struct disk *hd = cur_part->my_disk;
        struct super_block *sb_buf = (struct super_block *)sys_malloc(SECTOR_SIZE);
        
        /* 在内存中创建cur_part的超级块 */
        cur_part->sb = (struct super_block *)sys_malloc(sizeof(struct super_block));
        if (cur_part->sb == NULL) PANIC("alloc memory failed!");
        
        /* 读入超级块，并复制到cur_part->sb指向的内存 */
        memset(sb_buf, 0, SECTOR_SIZE);
        ide_read(hd, cur_part->start_lba + 1, sb_buf, 1); 
        memcpy(cur_part->sb, sb_buf, sizeof(struct super_block));

        /* 将硬盘上的块位图读入内存 */
        cur_part->block_bitmap.bits = (uint8_t *)sys_malloc(sb_buf->block_bitmap_sects * SECTOR_SIZE);
        if (cur_part->block_bitmap.bits == NULL) PANIC("alloc memory failed!");
        cur_part->block_bitmap.btmp_bytes_len = sb_buf->block_bitmap_sects * SECTOR_SIZE;
        ide_read(hd, sb_buf->block_bitmap_lba, cur_part->block_bitmap.bits, sb_buf->block_bitmap_sects);
        
        /* 将硬盘上的inode位图读入内存 */
        cur_part->inode_bitmap.bits = (uint8_t *)sys_malloc(sb_buf->inode_bitmap_sects * SECTOR_SIZE);
        if (cur_part->inode_bitmap.bits == NULL) PANIC("alloc memory failed!");
        cur_part->inode_bitmap.btmp_bytes_len = sb_buf->inode_bitmap_sects * SECTOR_SIZE;
        ide_read(hd, sb_buf->inode_bitmap_lba, cur_part->inode_bitmap.bits, sb_buf->inode_bitmap_sects);

        list_init(&cur_part->open_inodes);
        printk("mount %s done!\n", part->name);
        
        /* 已经挂在完毕后返回1让list_traversal停止遍历 */
        return 1;
    }

    /* 还没找到，让list_traversal继续遍历 */
    return 0;       
}

/* 格式化分区，即初始化分区的元信息，创建文件系统 */
static void partition_format(struct partition *part)
{
    /* 一个块一个扇区 */
    uint32_t boot_sector_sects = 1;
    uint32_t super_block_sects = 1;
    uint32_t inode_bitmap_sects = DIV_ROUND_UP(MAX_FILES_PER_PART, BITS_PER_SECTOR);
    uint32_t inode_table_sects = DIV_ROUND_UP(((sizeof(struct inode) * MAX_FILES_PER_PART)), SECTOR_SIZE);
    uint32_t used_sects = boot_sector_sects + super_block_sects + inode_bitmap_sects + inode_table_sects;
    uint32_t free_sects = part->sec_cnt - used_sects;

    /* 简单处理块位图占用的扇区数 */
    uint32_t block_bitmap_sects;
    block_bitmap_sects = DIV_ROUND_UP(free_sects, BITS_PER_SECTOR);
    /* 位图中位的长度，也是可用块的数量 */
    uint32_t block_bitmap_bit_len = free_sects - block_bitmap_sects;
    block_bitmap_sects = DIV_ROUND_UP(block_bitmap_bit_len, BITS_PER_SECTOR);

    /* 超级块初始化 */   
    struct super_block sb;
    sb.magic = 0x19590318;
    sb.sec_cnt = part->sec_cnt;
    sb.inode_cnt = MAX_FILES_PER_PART;
    sb.part_lba_base = part->start_lba;

    /* 第0个是引导块，第1个是超级块 */
    sb.block_bitmap_lba = sb.part_lba_base + 2;
    sb.block_bitmap_sects = block_bitmap_sects;
    
    sb.inode_bitmap_lba = sb.block_bitmap_lba + sb.block_bitmap_sects;
    sb.inode_bitmap_sects = inode_bitmap_sects;
    
    sb.inode_table_lba = sb.inode_bitmap_lba + sb.inode_bitmap_sects;
    sb.inode_table_sects = inode_table_sects;

    sb.data_start_lba = sb.inode_table_lba + sb.inode_table_sects;
    sb.root_inode_no = 0;
    sb.dir_entry_size = sizeof(struct dir_entry);   
    
    printk("%s info:\n", part->name);
    printk("    magic: 0x%x\n    part_lba_base: 0x%x\n    all_sectors: 0x%x\n    inode_cnt: 0x%x\n    block_bitmap_lba: 0x%x\n    block_bitmap_sectors: 0x%x\n    inode_bitmap_lba: 0x%x\n    inode_bitmap_sectors: 0x%x\n    inode_table_lba: 0x%x\n    inode_table_sectors: 0x%x\n    data_start_lba: 0x%x\n", sb.magic, sb.part_lba_base, sb.sec_cnt, sb.inode_cnt, sb.block_bitmap_lba, sb.block_bitmap_sects, sb.inode_bitmap_lba, sb.inode_bitmap_sects, sb.inode_table_lba, sb.inode_table_sects, sb.data_start_lba);
    
    struct disk *hd = part->my_disk;

    /* Step 1: 创建超级块写入本分区第一个扇区 */
    ide_write(hd, part->start_lba + 1, &sb, 1);
    printk("    super_block_lba: 0x%x\n", part->start_lba + 1);
    
    /* 找出块位图、inode节点位图、inode节点数组最大的做缓存 */
    uint32_t buf_size = (sb.block_bitmap_sects >= sb.inode_bitmap_sects ? sb.block_bitmap_sects : sb.inode_bitmap_sects);
    buf_size = (buf_size >= sb.inode_table_sects ? buf_size : sb.inode_table_sects) * SECTOR_SIZE;
    uint8_t *buf = (uint8_t *)sys_malloc(buf_size);

    /* Step 2: 将块位图初始化并写入sb.block_bitmap_lba */   
    buf[0] |= 0x01;         // 第0个块留作根目录
    uint32_t block_bitmap_last_byte = block_bitmap_bit_len / 8;
    uint8_t block_bitmap_last_bit = block_bitmap_bit_len % 8;
    /* 块位图最后一个扇区剩余字节数 */
    uint32_t last_size = SECTOR_SIZE - (block_bitmap_last_byte % SECTOR_SIZE);
    
    /* 设置超出实际扇区数的部分为已占用 */
    memset(&buf[block_bitmap_last_byte], 0xff, last_size);
    
    /* 将上一步最后覆盖的最后一字节的有效位重置0 */
    uint8_t bit_idx = 0;
    while (bit_idx <= block_bitmap_last_bit) buf[block_bitmap_last_byte] &= ~(1 << bit_idx++);
    
    ide_write(hd, sb.block_bitmap_lba, buf, sb.block_bitmap_sects);

    /* Step 3: 将inode位图初始化并写入sb.inode_bitmap_lba */
    memset(buf, 0, buf_size);
    buf[0] |= 0x1;              // 第0个inode分配给根目录
    /* 由于只支持一个分区最多4096个文件，所以sb.inode_bitmap_sects等于512
       正好一个扇区，不存在没有使用的位 */
    ide_write(hd, sb.inode_bitmap_lba, buf, sb.inode_bitmap_sects);

    /* Step 4: 将inode数组初始化并写入sb.inode_table_lba */
    memset(buf, 0, buf_size);
    struct inode *i = (struct inode *)buf;
    i->i_size = sb.dir_entry_size * 2;      // .和..
    i->i_no = 0;                            // 根目录占用数组中0
    i->i_sectors[0] = sb.data_start_lba; 
    ide_write(hd, sb.inode_table_lba, buf, sb.inode_table_sects);

    /* Step 5: 根目录初始化并写入sb.data_start_lba */
    memset(buf, 0, buf_size);
    struct dir_entry *p_de = (struct dir_entry *)buf; 
    
    /* 初始化当前目录 */   
    memcpy(p_de->filename, ".", 1);
    p_de->i_no = 0;
    p_de->f_type = FT_DIRECTORY;
    p_de++;

    memcpy(p_de->filename, "..", 2);   
    p_de->i_no = 0;             // 根目录的父目录还是自己
    p_de->f_type = FT_DIRECTORY;

    ide_write(hd, sb.data_start_lba, buf, 1);
    
    printk("    root_dir_lba: 0x%x\n", sb.data_start_lba);
    printk("%s format done\n", part->name);
    sys_free(buf);
}
    
void filesys_init() 
{
    uint8_t channel_no = 0, dev_no, part_idx = 0;
    struct super_block *sb_buf = (struct super_block *)sys_malloc(SECTOR_SIZE);

    if (sb_buf == NULL) PANIC("alloc memory failed!");

    printk("searching filesystem......\n");
    
    while (channel_no < channel_cnt)
    {
        dev_no = 0;
        while (dev_no < 2)
        {
            if (dev_no == 0)
            {
                // 扩过系统裸盘
                dev_no++;
                continue;
            }
            
            struct disk *hd = &(channels[channel_no].devices[dev_no]);
            struct partition *part = hd->prim_parts;
            while (part_idx < 12)
            {
                // 4个主分区和8个逻辑分区
                if (part_idx == 4) part = hd->logic_parts;
                
                if (part->sec_cnt != 0)
                {
                    memset(sb_buf, 0, SECTOR_SIZE);
                    
                    /* 读取分区的超级块，根据magic number判断是否存在文件系统 */
                    ide_read(hd, part->start_lba + 1, sb_buf, 1);
                    
                    if (sb_buf->magic == 0x19590318)
                    {
                        printk("%s has filesystem\n", part->name);
                    }
                    else
                    {
                        printk("formatting %s`s partition %s......\n", hd->name, part->name);
                        partition_format(part);
                    }
                }
                
                part_idx++;
                part++;
            }

            dev_no++;
        }
    
        channel_no++;
    }

    sys_free(sb_buf);

    /* 确定默认挂载分区 */
    char default_part[8] = "sdb1";
    /* 挂载分区 */
    list_traversal(&partition_list, mount_partition, (int)default_part);
}
