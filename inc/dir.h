#ifndef __FS_DIR_H
#define __FS_DIR_H

#include "stdint.h"
#include "inode.h"
#include "fs.h"
#include "ide.h"
#include "global.h"

#define MAX_FILE_NAME_LEN       16      // 最大文件名长度

/* 目录结构 */
struct dir 
{
    struct inode *inode;
    uint32_t dir_pos;           // 记录目录内偏移
    uint8_t dir_buf[512];       // 目录数据缓冲区
};

/* 目录项结构 */
struct dir_entry
{
    char filename[MAX_FILE_NAME_LEN];   // 文件名称
    uint32_t i_no;                      // i节点
    enum file_types f_type;             // 文件类型
};

extern struct dir root_dir;         // 根目录

void open_root_dir(struct partition *part);
struct dir *dir_open(struct partition *part, uint32_t inode_no);
void dir_close(struct dir *dir);
int search_dir_entry(struct partition *part, struct dir *pdir, const char *name, struct dir_entry *dir_e);
void create_dir_entry(char *filename, uint32_t inode_no, uint8_t file_type, struct dir_entry *p_de);
int sync_dir_entry(struct dir* parent_dir, struct dir_entry *p_de, void *io_buf);

#endif
