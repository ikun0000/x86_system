#ifndef __FS_INODE_H
#define __FS_INODE_H

#include "stdint.h"
#include "list.h"

/* inode结构 */
struct inode 
{
    uint32_t i_no;                  // inode编号
    
    /* 当inode代表普通文件时表示文件的大小
       当inode代表目录时表示目录项大小的总和 */
    uint32_t i_size;

    uint32_t i_open_cnts;           // 此文件打开的次数
    int write_deny;                 // 写文件互斥
    
    uint32_t i_sectors[13];         // 0～11是直接块，12是一级间接块指针
    struct list_elem inode_tag;
};

#endif
