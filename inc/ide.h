#ifndef __DEVICE_IDE_H
#define __DEVICE_IDE_H

#include "stdint.h"
#include "sync.h"
#include "bitmap.h"
#include "super_block.h"

/* 分区结构 */
struct partition 
{
    uint32_t start_lba;             // 起始扇区
    uint32_t sec_cnt;               // 扇区数
    struct disk *my_disk;           // 分区所属的硬盘
    struct list_elem part_tag;      // 用于队列中的标记
    char name[8];                   // 分区名称
    struct super_block *sb;         // 本分区的超级块
    struct bitmap block_bitmap;     // 块位图
    struct bitmap inode_bitmap;     // i节点位图
    struct list open_inodes;        // 本分区打开的i节点队列
};

/* 硬盘结构 */
struct disk
{
    char name[8];                       // 本硬盘名称
    struct ide_channel *my_channel;     // 此硬盘归属哪个ide通道
    uint8_t dev_no;                     // 主盘（0）或从盘（1）
    struct partition prim_parts[4];     // 主分区结构
    struct partition logic_parts[8];    // 理论上逻辑分区无上限，这里仅支持8个
};

/* ata通道结构 */
struct ide_channel
{
    char name[8];                  // 本ata通道名称
    uint16_t port_base;             // 本通道起始端口
    uint8_t irq_no;                 // 本通道的中断号
    struct lock lock;               // 通道锁
    int expecting_intr;             // 表示等待硬盘中断
    struct semaphore disk_done;     // 用于阻塞，唤醒驱动程序
    struct disk devices[2];         // 通道的主盘和从盘
};

extern uint8_t channel_cnt;
extern struct ide_channel channels[]; 
extern struct list partition_list;

void ide_init(void);
void ide_read(struct disk *hd, uint32_t lba, void *buf, uint32_t sec_cnt);
void ide_write(struct disk *hd, uint32_t lba, void *buf, uint32_t sec_cnt);
void intr_hd_handler(uint8_t irq_no);

#endif
