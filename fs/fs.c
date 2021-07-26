#include "fs.h"
#include "super_block.h"
#include "inode.h"
#include "dir.h"
#include "file.h"
#include "stdint.h"
#include "stdio_kern.h"
#include "list.h"
#include "string.h"
#include "ide.h"
#include "global.h"
#include "debug.h"
#include "memory.h"
#include "thread.h"
#include "console.h"

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

/* 将最上层路径名称解析出来 */
static char *path_parse(char *pathname, char *name_store)
{
    /* 根目录不需要解析 */
    if (pathname[0] == '/')
    {
        /* 跳过多余的'/' */
        while (*(++pathname) == '/');
    }

    while (*pathname != '/' && *pathname != 0) *name_store++ = *pathname++;

    /* 没有下一级目录返回NULL */
    if (pathname[0] == 0) return NULL;
    return pathname;
}

/* 返回路径深度 */
int32_t path_depth_cnt(char *pathname)
{
    ASSERT(pathname != NULL);
    char *p = pathname;
    char name[MAX_FILE_NAME_LEN];
    uint32_t depth = 0;

    p = path_parse(p, name);
    while (name[0]) 
    {
        depth++;
        memset(name, 0, MAX_FILE_NAME_LEN);
        /* p不等于NULL代表还有下一级路径 */
        if (p) p = path_parse(p, name);
    }
    return depth;
}

/* 搜索文件pathname，找到返回inode号，否则返回-1 */
static int search_file(const char *pathname, struct path_search_record *searched_record)
{
    /* 如果查找是如下几个目录直接返回 */
    if (!strcmp(pathname, "/") || !strcmp(pathname, "/.") || !strcmp(pathname, "/.."))
    {
        searched_record->parent_dir = &root_dir;
        searched_record->file_type = FT_DIRECTORY;
        searched_record->searched_path[0] = 0;
        return 0;
    }

    uint32_t path_len = strlen(pathname);
    ASSERT(pathname[0] == '/' && path_len > 1 && path_len < MAX_PATH_LEN);
    char *sub_path = (char *)pathname;
    struct dir *parent_dir = &root_dir;
    struct dir_entry dir_e;
    /* 记录解析的名称 */
    char name[MAX_FILE_NAME_LEN] = {0, };
    
    searched_record->parent_dir = parent_dir;
    searched_record->file_type = FT_UNKNOWN;
    uint32_t parent_inode_no = 0;

    sub_path = path_parse(sub_path, name);
    while (name[0])
    {
        ASSERT(strlen(searched_record->searched_path) < 512);
        
        /* 记录已存在的父目录 */
        strcat(searched_record->searched_path, "/");
        strcat(searched_record->searched_path, name);
        
        /* 在所给的目录中查找文件 */
        if (search_dir_entry(cur_part, parent_dir, name, &dir_e))
        {
            /* 如果找到了 */
            memset(name, 0, MAX_FILE_NAME_LEN);
            
            /* sub_path不为NULL说明还没找到底，继续寻找 */
            if (sub_path) sub_path = path_parse(sub_path, name);

            if (FT_DIRECTORY == dir_e.f_type)
            {
                /* 如果被打开的是目录 */
                parent_inode_no = parent_dir->inode->i_no;
                dir_close(parent_dir);   
                parent_dir = dir_open(cur_part, dir_e.i_no);    // 更新父目录为本目录
                searched_record->parent_dir = parent_dir;
                continue;
            }
            else if (FT_REGULAR == dir_e.f_type)
            {
                /* 查找到的是普通文件 */
                searched_record->file_type = FT_REGULAR;
                return dir_e.i_no;
            }
        }
        else
        {
            /* 找不到目录项时，不关闭parent_dir */
            return -1;
        }
    }
    
    dir_close(searched_record->parent_dir);
    
    /* 保存被查找到目录的直接父目录 */
    searched_record->parent_dir = dir_open(cur_part, parent_inode_no);
    searched_record->file_type = FT_DIRECTORY;
    return dir_e.i_no;
}

/* 打开或创建文件，成功返回文件描述符，失败返回-1 */
int32_t sys_open(const char *pathname, uint8_t flags)
{
    if (pathname[strlen(pathname) - 1] == '/')
    {
        printk("can't open a directory %s\n", pathname);
        return 0;
    }
    ASSERT(flags <= 7);
    int32_t fd = -1;

    struct path_search_record searched_record;
    memset(&searched_record, 0, sizeof(struct path_search_record));
    
    /* 记录目录深度，用于判断中间某个目录不存在的情况 */
    uint32_t pathname_depth = path_depth_cnt((char *)pathname);
    
    /* 先检查文件是否存在 */
    int inode_no = search_file(pathname, &searched_record);
    int found = inode_no != -1 ? 1 : 0;
    
    if (searched_record.file_type == FT_DIRECTORY)
    {
        printk("can't open a directory with open(), use opendir() to instead\n");
        dir_close(searched_record.parent_dir);
        return -1;
    }
    
    uint32_t path_searched_depth = path_depth_cnt(searched_record.searched_path);
    
    /* 判断是否把所有层的目录都扫描了 */
    if (pathname_depth != path_searched_depth)
    {
        /* 中间断层了 */
        printk("cannot access %s: Not a directory, subpath %s is't exist\n", \
                pathname, searched_record.searched_path);
        dir_close(searched_record.parent_dir);
        return -1;
    }

    /* 如果在最后一个路径上没找到，并且不是创建文件选项，直接返回-1 */
    if (!found && !(flags & O_CREAT))
    {
        printk("in path %s, file %s is't exist\n", \
                searched_record.searched_path, (strrchr(searched_record.searched_path, '/') + 1));   
        dir_close(searched_record.parent_dir);
        return -1;
    }
    else if (found && flags & O_CREAT)
    {
        /* 创建文件已存在 */
        printk("%s has already exist!\n", pathname);
        dir_close(searched_record.parent_dir);
        return -1;
    }

    switch (flags & O_CREAT)
    {
        case O_CREAT:
            printk("creating file\n");
            fd = file_create(searched_record.parent_dir, (strrchr(pathname, '/') + 1), flags);
            dir_close(searched_record.parent_dir);
            break;
        
        /* 其余为打开文件，O_RDONLY,O_WRONLY,O_RDWR */
        default:
            fd = file_open(inode_no, flags);
    }   
    
    /* 此时fd指向pcb->fd_table数组中的元素下标 */
    return fd;
}

/* 将文件描述符转化为文件表下标 */
static uint32_t fd_local2global(uint32_t local_fd)
{
    struct task_struct *cur = running_thread();
    int32_t global_fd = cur->fd_table[local_fd];
    ASSERT(global_fd >= 0 && global_fd < MAX_FILE_OPEN);
    return (uint32_t)global_fd;
}

/* 关闭文件描述符fd指向的文件，成功返回0，否则返回-1 */
int32_t sys_close(int32_t fd)
{
    int32_t ret = -1;
    if (fd > 2)
    {
        uint32_t _fd = fd_local2global(fd);
        ret = file_close(&file_table[_fd]);
        running_thread()->fd_table[fd] = -1;
    }

    return ret;
}

/* 将buf中连续的count个字节写入到文件描述符fd，成功返回写入的字节数，失败返回-1 */
int32_t sys_write(int32_t fd, const void *buf, uint32_t count)
{
    if (fd < 0)
    {
        printk("sys_write: fd error\n");
        return -1;
    }

    if (fd == stdout_no)
    {
        char tmp_buf[1024] = {0, };
        memcpy(tmp_buf, buf, count);
        console_put_str(tmp_buf);
        return count;
    }

    uint32_t _fd = fd_local2global(fd);
    struct file *wr_file = &file_table[_fd];
    if (wr_file->fd_flag & O_WRONLY || wr_file->fd_flag & O_RDWR)
    {
        uint32_t bytes_written = file_write(wr_file, buf, count);
        return bytes_written;
    }
    else
    {
        console_put_str("sys_write: not allowd to write file without flag O_RDWR or O_WRONLY\n");
        return -1;
    }
}

/* 从文件描述符fd指向的文件中读取count个字节到buf，成功返回读取的字节数，失败返回-1 */
int32_t sys_read(int32_t fd, void *buf, uint32_t count)
{
    if (fd < 0)
    {
        printk("sys_read: fd error\n");
        return -1;
    }

    ASSERT(buf != NULL);
    uint32_t _fd = fd_local2global(fd);
    return file_read(&file_table[_fd], buf, count);
}

/* 重置文件指针，成功返回新的对于文件头的偏移量，失败返回-1 */
int32_t sys_lseek(int32_t fd, int32_t offset, uint8_t whence)
{
    if (fd < 0)
    {
        printk("sys_lseek: fd error\n");
        return -1;
    }

    ASSERT(whence > 0 && whence < 4);
    uint32_t _fd = fd_local2global(fd);
    struct file *pf = &file_table[_fd];
    int32_t new_pos = 0;            // 新的偏移必须位于文件大小之间
    int32_t file_size = (int32_t)pf->fd_inode->i_size;
    switch (whence)
    {
        case SEEK_SET:
            new_pos = offset;
            break;
        
        case SEEK_CUR:
            new_pos = (int32_t)pf->fd_pos + offset;
            break;

        case SEEK_END:
            new_pos = file_size + offset;
            break;
    }

    if (new_pos < 0 || new_pos > (file_size - 1)) return -1;
    pf->fd_pos = new_pos;
    return pf->fd_pos;
}

/* 删除文件（FT_REGULAR），成功返回0，失败返回-1 */
int32_t sys_unlink(const char *pathname)
{
    ASSERT(strlen(pathname) < MAX_PATH_LEN);

    struct path_search_record searched_record;
    memset(&searched_record, 0, sizeof(struct path_search_record));
    int inode_no = search_file(pathname, &searched_record);
    ASSERT(inode_no != 0);
    if (inode_no == -1)
    {
        printk("file %s not found!\n", pathname);
        dir_close(searched_record.parent_dir);
        return -1;
    }
    if (searched_record.file_type == FT_DIRECTORY)
    {
        printk("can't delete a directory with unlink(), use rmdir() to instead\n");
        dir_close(searched_record.parent_dir);
        return -1;
    }

    /* 是否在打开文件列表中 */
    uint32_t file_idx = 0;
    while (file_idx < MAX_FILE_OPEN)
    {
        if (file_table[file_idx].fd_inode != NULL && 
           (uint32_t)inode_no == file_table[file_idx].fd_inode->i_no) break;   
        file_idx++;
    }
    if (file_idx < MAX_FILE_OPEN)
    {
        dir_close(searched_record.parent_dir);
        printk("file %s is in use, not allow to delete!\n", pathname);
        return -1;
    }
    ASSERT(file_idx == MAX_FILE_OPEN);

    void *io_buf = sys_malloc(SECTOR_SIZE + SECTOR_SIZE);
    if (io_buf == NULL)
    {
        dir_close(searched_record.parent_dir);
        printk("sys_unlink malloc for io_buf failed\n");
        return -1;
    }

    struct dir *parent_dir = searched_record.parent_dir;
    delete_dir_entry(cur_part, parent_dir, inode_no, io_buf);
    inode_release(cur_part, inode_no);
    sys_free(io_buf);
    dir_close(searched_record.parent_dir);
    return 0;
}

/* 创建目录pathname，成功返回0，失败返回1 */
int32_t sys_mkdir(const char *pathname)
{
    uint8_t rollback_step = 0;      // 记录失败后的回滚状态
    void *io_buf = sys_malloc(SECTOR_SIZE * 2);
    if (io_buf == NULL)
    {
        printk("sys_mkdir: sys_malloc for io_buf failed\n");
        return -1;
    }

    struct path_search_record searched_record;
    memset(&searched_record, 0, sizeof(struct path_search_record));
    int inode_no = -1;
    inode_no = search_file(pathname, &searched_record);
    if (inode_no != -1)
    {
        /* 如果存在同名的目录或文件 */
        printk("sys_mkdir: file or directory %s exists!\n", pathname);
        rollback_step = 1;
        goto rollback;
    }
    else
    {
        /* 如果没有判断目录中间是否有断层 */
        uint32_t pathname_depth = path_depth_cnt((char *)pathname);
        uint32_t path_searched_depth = path_depth_cnt(searched_record.searched_path);
        
        if (pathname_depth != path_searched_depth)
        {
            printk("sys_mkdir: can't access %s, subpath %s is't exist\n", pathname, searched_record.searched_path);
            rollback_step = 1;
            goto rollback;
        }
    }

    struct dir *parent_dir = searched_record.parent_dir;
    /* 用户输入的pathname可能在末尾有/ */
    char *dirname = strrchr(searched_record.searched_path, '/') + 1;
    
    inode_no = inode_bitmap_alloc(cur_part);
    if (inode_no == -1)
    {
        printk("sys_mkdir: allocate inode failed\n");
        rollback_step = 1;
        goto rollback;
    }

    struct inode new_dir_inode;
    inode_init(inode_no, &new_dir_inode);

    uint32_t block_bitmap_idx = 0;
    int32_t block_lba = -1;
    block_lba = block_bitmap_alloc(cur_part);
    if (block_lba == -1)
    {
        printk("sys_mkdir: block_bitmap_alloc for create directory failed\n");
        rollback_step = 2;
        goto rollback;
    }
    new_dir_inode.i_sectors[0] = block_lba;
    block_bitmap_idx = block_lba - cur_part->sb->data_start_lba;
    ASSERT(block_bitmap_idx != 0);
    bitmap_sync(cur_part, block_bitmap_idx, BLOCK_BITMAP);

    /* 写入.和..到新的目录中 */
    memset(io_buf, 0, SECTOR_SIZE * 2);
    struct dir_entry *p_de = (struct dir_entry *)io_buf;
    
    memcpy(p_de->filename, ".", 1);
    p_de->i_no = inode_no;
    p_de->f_type = FT_DIRECTORY;

    p_de++;

    memcpy(p_de->filename, "..", 2);
    p_de->i_no = parent_dir->inode->i_no;
    p_de->f_type = FT_DIRECTORY;
    ide_write(cur_part->my_disk, new_dir_inode.i_sectors[0], io_buf, 1);

    new_dir_inode.i_size = 2 * cur_part->sb->dir_entry_size;

    /* 在父目录中添加自己的目录项 */
    struct dir_entry new_dir_entry;
    memset(&new_dir_entry, 0, sizeof(struct dir_entry));
    create_dir_entry(dirname, inode_no, FT_DIRECTORY, &new_dir_entry);
    memset(io_buf, 0, SECTOR_SIZE * 2);
    if (!sync_dir_entry(parent_dir, &new_dir_entry, io_buf))
    {
        printk("sync_mkdir: sync_dir_entry to disk failed!\n");
        rollback_step = 2;
        goto rollback;
    }

    /* 父母路的inode同步到硬盘 */
    memset(io_buf, 0, SECTOR_SIZE * 2);
    inode_sync(cur_part, parent_dir->inode, io_buf);
    
    /* 新建的目录inode同步到硬盘 */
    memset(io_buf, 0, SECTOR_SIZE * 2);
    inode_sync(cur_part, &new_dir_inode, io_buf);
    
    /* 将inode位图同步到硬盘 */
    bitmap_sync(cur_part, inode_no, INODE_BITMAP);
    
    sys_free(io_buf);

    /* 关闭父目录 */
    dir_close(searched_record.parent_dir);
    return 0;

/* 操作失败回滚 */
rollback:
    switch (rollback_step)
    {
        case 2:
            bitmap_set(&cur_part->inode_bitmap, inode_no, 0);
        case 1:
            dir_close(searched_record.parent_dir);
            break;
    }

    sys_free(io_buf);
    return -1;
}

/* 打开目录，成功返回目录结构，失败返回NULL */
struct dir *sys_opendir(const char *pathname)
{
    ASSERT(strlen(pathname) < MAX_PATH_LEN);
    /* 如果是根目录直接返回 */
    if (pathname[0] == '/' && (pathname[1] == 0 || pathname[0] == '.')) return &root_dir;

    /* 先检查要打开的目录是否存在 */
    struct path_search_record searched_record;
    memset(&searched_record, 0, sizeof(struct path_search_record));
    int inode_no = search_file(pathname, &searched_record);
    struct dir *ret = NULL;
    if (inode_no == -1)
    {
        printk("In %s, sub path %s not exist\n", pathname, searched_record.searched_path);
    }
    else
    {
        if (searched_record.file_type == FT_REGULAR)
        {
            printk("%s is regular file!\n", pathname);
        }
        else if (searched_record.file_type == FT_DIRECTORY)
        {
            ret = dir_open(cur_part, inode_no);
        }
    }
    dir_close(searched_record.parent_dir);
    return ret;
}

/* 关闭目录，成功返回0，失败返回-1 */
int32_t sys_closedir(struct dir *dir)
{
    int32_t ret = -1;
    if (dir != NULL)
    {
        dir_close(dir);
        ret = 0;
    }
    return ret;
}

/* 读取目录dir的一个目录项，成功返回目录项地址，到目录尾或出错返回NULL */
struct dir_entry *sys_readdir(struct dir *dir)
{
    ASSERT(dir != NULL);
    return dir_read(dir);
}

/* 把dir目录的游标置0 */
void sys_rewinddir(struct dir *dir)
{
    dir->dir_pos = 0;
}

/* 删除空目录，成功返回0，失败返回-1 */
int32_t sys_rmdir(const char *pathname)
{
    struct path_search_record searched_record;
    memset(&searched_record, 0, sizeof(struct path_search_record));
    int inode_no = search_file(pathname, &searched_record);
    ASSERT(inode_no != 0);
    int retval = -1;
    if (inode_no == -1)
    {
        printk("In %s, sub path %s not exist\n", pathname, searched_record.searched_path);
    }
    else
    {
        if (searched_record.file_type == FT_REGULAR)
        {
            printk("%s is regular file!\n", pathname);
        }
        else
        {
            struct dir *dir = dir_open(cur_part, inode_no);
            if (!dir_is_empty(dir))
            {
                /* 不能删除非空目录 */
                printk("dir %s is not empty, it is not allowed to delete a nonempty directory!\n", pathname);
            }
            else
            {
                if (!dir_remove(searched_record.parent_dir, dir))
                {
                    retval = 0;
                }
            }
            
            dir_close(dir);
        }
    }

    dir_close(searched_record.parent_dir);
    return retval;
}

/* 获取父目录的inode编号 */
static uint32_t get_parent_dir_inode_nr(uint32_t child_inode_nr, void *io_buf)
{
    struct inode *child_dir_inode = inode_open(cur_part, child_inode_nr);
    /* 目录中的..中包含父目录的inode编号，..位于目录块的第0块 */
    uint32_t block_lba = child_dir_inode->i_sectors[0];
    ASSERT(block_lba >= cur_part->sb->data_start_lba);
    inode_close(child_dir_inode);
    ide_read(cur_part->my_disk, block_lba, io_buf, 1);
    struct dir_entry *dir_e = (struct dir_entry *)io_buf;
    /* 第0个目录项是. 。第一个是.. */
    ASSERT(dir_e[1].i_no < 4096 && dir_e[1].f_type == FT_DIRECTORY);
    return dir_e[1].i_no;
}

/* 在inode编号为p_inode_nr的目录中查找inode编号为c_inode_nr的目录名字，
   并将名字存入path，成功返回0，失败返回-1 */
static int get_child_dir_name(uint32_t p_inode_nr, uint32_t c_inode_nr, char *path, void *io_buf)
{
    struct inode *parent_dir_inode = inode_open(cur_part, p_inode_nr);
    uint8_t block_idx = 0;
    uint32_t all_blocks[140] = {0, }, block_cnt = 12;
    while (block_idx < 12)
    {
        all_blocks[block_idx] = parent_dir_inode->i_sectors[block_idx];
        block_idx++;
    }
    if (parent_dir_inode->i_sectors[12])
    {
        ide_read(cur_part->my_disk, parent_dir_inode->i_sectors[12], all_blocks + 12, 1);
        block_cnt = 140;
    }
    inode_close(parent_dir_inode);

    struct dir_entry *dir_e = (struct dir_entry *)io_buf;
    uint32_t dir_entry_size = cur_part->sb->dir_entry_size;
    uint32_t dir_entrys_per_sec = (512 / dir_entry_size);
    block_idx = 0;
    while (block_idx < block_cnt)
    {
        if (all_blocks[block_idx])
        {
            ide_read(cur_part->my_disk, all_blocks[block_idx], io_buf, 1);
            uint8_t dir_e_idx = 0;
            
            while (dir_e_idx < dir_entrys_per_sec)
            {
                if ((dir_e + dir_e_idx)->i_no == c_inode_nr)
                {
                    strcat(path, "/");
                    strcat(path, (dir_e + dir_e_idx)->filename);
                    return 0;
                }
                
                dir_e_idx++;
            }
        }
        
        block_idx++;
    }

    return -1;
}

/* 把当前工作目录的绝对路径写入buf，size是buf的大小
   当buf为NULL时，由操作系统分配存储工作路径的地址并返回，但并不在这里处理
   失败返回NULL */
char *sys_getcwd(char *buf, uint32_t size)
{
    ASSERT(buf != NULL);
    void *io_buf = sys_malloc(SECTOR_SIZE);
    if (io_buf == NULL) return NULL;

    struct task_struct *cur_thread = running_thread();
    int32_t parent_inode_nr = 0;
    int32_t child_inode_nr = cur_thread->cwd_inode_nr;
    ASSERT(child_inode_nr >= 0 && child_inode_nr < 4096);
    
    /* 如果当期那是根目录直接返回 */
    if (child_inode_nr == 0)
    {
        buf[0] = '/';
        buf[1] = 0;
        return buf;
    }

    memset(buf, 0, size);
    /* 全路径缓冲区 */
    char full_path_reverse[MAX_PATH_LEN] = {0, };
    
    /* 从下往上找父目录，知道根目录为止
       停止条件就是child_inode_nr为根目录inode号，即0 */
    while ((child_inode_nr))
    {
        parent_inode_nr = get_parent_dir_inode_nr(child_inode_nr, io_buf);
        if (get_child_dir_name(parent_inode_nr, child_inode_nr, full_path_reverse, io_buf) == -1)
        {
            sys_free(io_buf);
            return NULL;
        }
        child_inode_nr = parent_inode_nr;
    }
    ASSERT(strlen(full_path_reverse) <= size);
    
    /* full_path_reverse的路径是反的
       但是是反一半的，只是位置反了而已 
       比如工作目录是/home/user1/x86_sys/dev存储到full_path_reverse是
       /dev/x86_sys/user1/home */
    char *last_slash;
    while ((last_slash = strrchr(full_path_reverse, '/')))
    {
        uint16_t len = strlen(buf);
        strcpy(buf + len, last_slash);
        /* 把 / 改成0截断字符串 */
        *last_slash = 0;
    }
    sys_free(io_buf);
    return buf;
}

/* 更改当前工作目录为path，成功返回0，失败返回-1 */
int32_t sys_chdir(const char *path)
{
    int32_t ret = -1;
    struct path_search_record searched_record;
    memset(&searched_record, 0, sizeof(struct path_search_record));
    int inode_no = search_file(path, &searched_record);
    if (inode_no != -1)
    {
        if (searched_record.file_type == FT_DIRECTORY)
        {
            running_thread()->cwd_inode_nr = inode_no;
            ret = 0;
        }
        else
        {
            printk("sys_chdir: %s is regular file or other!\n", path);
        }
    }

    dir_close(searched_record.parent_dir);
    return ret;
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

    /* 将当前分区的根目录打开 */
    open_root_dir(cur_part);

    /* 初始化文件表 */
    uint32_t fd_idx = 0;
    while (fd_idx < MAX_FILE_OPEN) file_table[fd_idx++].fd_inode = NULL;
}
