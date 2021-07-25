#ifndef __FS_FS_H
#define __FS_FS_H

#include "stdint.h"
#include "ide.h"

#define MAX_FILES_PER_PART      4096            // 每个分区支持的最大文件数
#define BITS_PER_SECTOR         4096            // 每个扇区的位数
#define SECTOR_SIZE             512             // 扇区字节大小 
#define BLOCK_SIZE              SECTOR_SIZE     // 块字节大小

#define MAX_PATH_LEN            512             // 路径最大长度

/* 文件类型 */
enum file_types 
{
    FT_UNKNOWN,         // 不支持文件类型
    FT_REGULAR,         // 普通文件
    FT_DIRECTORY        // 目录文件
};

/* 打开文件选项 */
enum oflags
{
    O_RDONLY,           // 只读
    O_WRONLY,           // 只写
    O_RDWR,             // 读写
    O_CREAT = 4         // 创建
};

struct path_search_record
{
    char searched_path[MAX_PATH_LEN];       // 查找文件的路径
    struct dir *parent_dir;                 // 查找文件的直接父目录
    enum file_types file_type;              // 找到的文件类型
};

extern struct partition *cur_part;

void filesys_init(void);
int32_t path_depth_cnt(char *pathname);
int32_t sys_open(const char *pathname, uint8_t flags);
int32_t sys_close(int32_t fd);
int32_t sys_write(int32_t fd, const void *buf, uint32_t count);
int32_t sys_read(int32_t fd, void *buf, uint32_t count);

#endif
