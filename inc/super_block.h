#ifndef __FS_SUPER_BLOCK_H
#define __FS_SUPER_BLOCK_H

#include "stdint.h"

/* 超级块 */
struct super_block 
{
    uint32_t magic;                     // 用于标识文件系统类型
    uint32_t sec_cnt;                   // 本分区总共扇区数
    uint32_t inode_cnt;                 // 本分区中inode的数量
    uint32_t part_lba_base;             // 本分区起始lba地址

    uint32_t block_bitmap_lba;          // 块位图的起始lba地址
    uint32_t block_bitmap_sects;        // 块位图占用的扇区数

    uint32_t inode_bitmap_lba;          // i节点的位图起始lba地址
    uint32_t inode_bitmap_sects;        // i节点位图占用的扇区数

    uint32_t inode_table_lba;           // i节点表的起始lba地址
    uint32_t inode_table_sects;         // i节点表占用的扇区数

    uint32_t data_start_lba;            // 数据区起始的第一个扇区号
    uint32_t root_inode_no;             // 根目录所在的i节点号
    uint32_t dir_entry_size;            // 目录项大小

    uint8_t pad[460];                   // 填充一个扇区的大小
}__attribute__((packed));

#endif
