#include "ide.h"
#include "sync.h"
#include "io.h"
#include "stdio.h"
#include "stdio_kern.h"
#include "interrupt.h"
#include "memory.h"
#include "debug.h"
#include "console.h"
#include "timer.h"
#include "string.h"
#include "list.h"

/* 定义硬盘各端口的端口号 */
#define reg_data(channel)       (channel->port_base + 0)
#define reg_error(channel)      (channel->port_base + 1)
#define reg_sect_cnt(channel)   (channel->port_base + 2)
#define reg_lba_l(channel)      (channel->port_base + 3)
#define reg_lba_m(channel)      (channel->port_base + 4)
#define reg_lba_h(channel)      (channel->port_base + 5)
#define reg_dev(channel)        (channel->port_base + 6)
#define reg_status(channel)     (channel->port_base + 7)
#define reg_cmd(channel)        (reg_status(channel))
#define reg_alt_status(channel) (channel->port_base + 0x206)
#define reg_ctl(channel)        reg_alt_status(channel)

/* reg_alt_status寄存器的一些关键位 */
#define BIT_STAT_BSY        0x80        // 硬盘忙
#define BIT_STAT_DRDY       0x40        // 驱动器准备好
#define BIT_STAT_DRQ        0x8         // 数据传输准备好

/* device寄存器的一些关键位 */
#define BIT_DEV_MBS         0xa0        // 第7位和第5位固定为1
#define BIT_DEV_LBA         0x40        // LBA模式
#define BIT_DEV_DEV         0x10        // 读取从盘 

/* 部分硬盘操作指令 */
#define CMD_IDENTIFY        0xec        // identify指令
#define CMD_READ_SECTOR     0x20        // 读取扇区指令
#define CMD_WRITE_SECTOR    0x30        // 写入扇区指令

/* 定义可读取最大扇区数，debug用 */
#define max_lba ((80*1024*1024/512) - 1)    // 80M

uint8_t channel_cnt;                    // 按硬盘数计算通道数
struct ide_channel channels[2];         // 当前有两个IDE通道

/* 用于记录总扩展分区的起始LBA，初始为0，partition_scan以此为起始标记 */
int32_t ext_lba_base = 0;

uint8_t p_no = 0, l_no = 0;             // 记录硬盘主分区和逻辑分区的下标

struct list partition_list;             // 分区队列

/* 分区表存储结构 */
struct partition_table_entry 
{
    uint8_t bootable;               // 是否引导扇区
    uint8_t start_head;             // 起始磁头号
    uint8_t start_sec;              // 起始扇区号
    uint8_t start_chs;              // 起始柱面号
    uint8_t fs_type;                // 分区类型
    uint8_t end_head;               // 结束磁头号
    uint8_t end_sec;                // 结束扇区号
    uint8_t end_chs;                // 结束柱面号
    uint32_t start_lba;             // 本分区的起始lba地址
    uint32_t sec_cnt;               // 本分区占用的扇区数
}__attribute__((packed));

/* 引导扇区，MBR或EBR结构 */
struct boot_sector 
{
    uint8_t other[446];                                 // 引导代码
    struct partition_table_entry partition_table[4];    // 分区表
    uint16_t signature;                                 // 结束标志0x55, 0xAA
}__attribute__((packed));

/* 选择读写的硬盘 */
static void select_disk(struct disk *hd)
{
    uint8_t reg_device = BIT_DEV_MBS | BIT_DEV_LBA;
    /* 若是从盘则置DEV位为1 */
    if (hd->dev_no == 1) reg_device |= BIT_DEV_DEV;

    outb(reg_dev(hd->my_channel), reg_device);
}

/* 向硬盘控制器写入起始扇区地址和读取/写入的扇区数 */
static void select_sector(struct disk *hd, uint32_t lba, uint8_t sec_cnt)
{
    ASSERT(lba <= max_lba);
    struct ide_channel *channel = hd->my_channel;

    /* 写入扇区数 */
    outb(reg_sect_cnt(channel), sec_cnt);

    /* 写入LBA地址 */
    outb(reg_lba_l(channel), lba);
    outb(reg_lba_m(channel), lba >> 8);
    outb(reg_lba_h(channel), lba >> 16);
    outb(reg_dev(channel), BIT_DEV_MBS | BIT_DEV_LBA | (hd->dev_no == 1 ? BIT_DEV_DEV : 0) | lba >> 24);
}

/* 向通道发送命令 */
static void cmd_out(struct ide_channel *channel, uint8_t cmd)
{
    /* 只要向硬盘发送命令都标记，硬盘中断根据他来判断 */
    channel->expecting_intr = 1;
    outb(reg_cmd(channel), cmd);
}

/* 从硬盘读入sec_cnt个扇区数据到buf */
static void read_from_sector(struct disk *hd, void *buf, uint8_t sec_cnt)
{
    uint32_t size_in_byte;
    /* LBA28一次最多支持256个扇区，因为sec_cnt最大值为255，所以用0代表256 */
    if (sec_cnt == 0) size_in_byte = 256 * 512;
    else size_in_byte = sec_cnt * 512;

    insw(reg_data(hd->my_channel), buf, size_in_byte / 2);
}

/* 将buf中的sec_cnt个扇区的数据写入硬盘 */
static void write2sector(struct disk *hd, void *buf, uint8_t sec_cnt)
{
    uint32_t size_in_byte;
    /* LBA28一次最多支持256个扇区，因为sec_cnt最大值为255，所以用0代表256 */
    if (sec_cnt == 0) size_in_byte = 256 * 512;
    else size_in_byte = sec_cnt * 512;
    
    outsw(reg_data(hd->my_channel), buf, size_in_byte / 2);
}

/* 等待30秒，如果中途检测到硬盘不忙则立刻返回，返回值为0表示数据未准备好，非0则代表数据准好 */
static int busy_wait(struct disk *hd)
{
    struct ide_channel *channel = hd->my_channel;
    uint16_t time_limit = 30 * 1000;
    while (time_limit -= 10 >= 0) 
    {
        if (!(inb(reg_status(channel)) & BIT_STAT_BSY)) return (inb(reg_status(channel)) & BIT_STAT_DRQ);
        else mtime_sleep(10);
    }
    return 0;
}

/* 从硬盘读取sec_cnt个扇区到buf中 */
void ide_read(struct disk *hd, uint32_t lba, void *buf, uint32_t sec_cnt)
{
    ASSERT(lba <= max_lba);
    ASSERT(sec_cnt > 0);
    lock_acquire(&hd->my_channel->lock);

    /* Setp 1: 选择操作的硬盘 */
    select_disk(hd);
    
    uint32_t secs_op;           // 每次操作的扇区数
    uint32_t secs_done = 0;     // 已完成的扇区数
    while (secs_done < sec_cnt)
    {
        if ((secs_done + 256) <= sec_cnt) secs_op = 256;
        else secs_op = sec_cnt - secs_done;

        /* Step 2: 写入待读取的扇区数和起始LBA地址 */
        select_sector(hd, lba + secs_done, secs_op);
        
        /* Step 3: 向cmd寄存器写入读取命令 */
        cmd_out(hd->my_channel, CMD_READ_SECTOR);
        
        /* 写入读取命令后此时硬盘正在准备数据，这里把自己阻塞，
           等待硬盘把数据准备好后发送中断唤醒本线程 */
        sema_down(&hd->my_channel->disk_done);

        /* Step 4: 检测硬盘状态是否可读 */
        if (!busy_wait(hd)) 
        {
            /* 若失败 */
            char error[64];
            sprintf(error, "%s read sector %d failed!!!\n", hd->name, lba);
            PANIC(error);
        }

        /* Step 5: 把数据从磁盘读取内存缓冲区 */
        read_from_sector(hd, (void *)((uint32_t)buf + secs_done * 512), secs_op);
        secs_done += secs_op;
    }
    lock_release(&hd->my_channel->lock);
}

/* 将buf中的sec_cnt个扇区的数据写入到硬盘 */
void ide_write(struct disk *hd, uint32_t lba, void *buf, uint32_t sec_cnt)
{
    ASSERT(lba <= max_lba);   
    ASSERT(sec_cnt > 0);
    lock_acquire(&hd->my_channel->lock);

    /* Step 1: 写入操作的硬盘 */
    select_disk(hd);

    uint32_t secs_op;           // 每次操作的扇区数
    uint32_t secs_done = 0;     // 已经完成的扇区数
    while (secs_done < sec_cnt)
    {
        if ((secs_done + 256) <= sec_cnt) secs_op = 256;
        else secs_op = sec_cnt - secs_done;

        /* Step 2: 写入待读取的扇区数和起始LBA地址 */
        select_sector(hd, lba + secs_done, secs_op);
        
        /* Step 3: 向cmd寄存器写入写入命令 */
        cmd_out(hd->my_channel, CMD_WRITE_SECTOR);
        
        /* Step 4: 检测硬盘状态是否就绪 */
        if (!busy_wait(hd)) 
        {
            /* 若失败 */
            char error[64];
            sprintf(error, "%s read sector %d failed!!!\n", hd->name, lba);
            PANIC(error);
        }

        /* Step 5: 将数据写入到硬盘 */
        write2sector(hd, (void *)((uint32_t)buf + secs_done * 512), secs_op);
        
        /* 在硬盘响应期间阻塞自己 */
        sema_down(&hd->my_channel->disk_done);
        secs_done += secs_op;
    }
    lock_release(&hd->my_channel->lock);
}

/* 将dst中len个相邻的字节交换位置存入buf */
static void swap_pairs_bytes(const char *dst, char *buf, uint32_t len)
{
    uint8_t idx;
    for (idx = 0; idx < len; idx += 2)
    {
        buf[idx + 1] = *dst++;
        buf[idx] = *dst++;
    }
    buf[idx] = '\0';
}

/* 获取硬盘参数信息 */
static void identify_disk(struct disk *hd)
{
    char id_info[512];
    select_disk(hd);
    cmd_out(hd->my_channel, CMD_IDENTIFY);
    /* 发送命令后阻塞自己，等硬盘准备好后唤醒继续执行 */
    sema_down(&hd->my_channel->disk_done);

    if (!busy_wait(hd))
    {
        char error[64];
        sprintf(error, "%d identify failed!!!\n", hd->name);
        PANIC(error);
    }   
    read_from_sector(hd, id_info, 1);
    
    char buf[64];
    uint8_t sn_start = 10 * 2, sn_len = 20, md_start = 27 * 2, md_len = 40;
    swap_pairs_bytes(&id_info[sn_start], buf, sn_len);
    printk("    disk %s info:\n      SN: %s\n", hd->name, buf);
    memset(buf, 0, sizeof(buf));
    swap_pairs_bytes(&id_info[md_start], buf, md_len);
    printk("      MODULE: %s\n", buf);
    uint32_t sectors = *(uint32_t *)&id_info[60 * 2];
    printk("      SECTORS: %d\n", sectors);
    printk("      CAPACITY: %dMB\n", sectors * 512 / 1024 / 1024);
}

/* 扫描硬盘hd中地址为ext_lba的扇区中的所有分区 */
static void partition_scan(struct disk *hd, uint32_t ext_lba)
{
    struct boot_sector *bs = sys_malloc(sizeof(struct boot_sector));
    ide_read(hd, ext_lba, bs, 1);
    uint8_t part_idx = 0;
    struct partition_table_entry *p = bs->partition_table;

    while (part_idx++ < 4)
    {
        if (p->fs_type == 0x5)
        {
            // 若为扩展分区           
            /* 子扩展分区的start_lba是相对于主引导分区中的总扩展分区的地址 */
            if (ext_lba_base != 0) partition_scan(hd, p->start_lba + ext_lba_base);
            else
            {
                /* ext_lba_base为0表示第一次读取引导块，也就是主引导扇区
                   所以记录下扩展分区的起始lba地址 */
                ext_lba_base = p->start_lba;
                partition_scan(hd, p->start_lba);
            }
        }
        else if (p->fs_type != 0)
        {
            // 如果是有效的分区类型
            if (ext_lba == 0)
            {
                // 此时全是主分区    
                hd->prim_parts[p_no].start_lba = ext_lba + p->start_lba ;  
                hd->prim_parts[p_no].sec_cnt = p->sec_cnt;
                hd->prim_parts[p_no].my_disk = hd;
                list_append(&partition_list, &hd->prim_parts[p_no].part_tag);
                sprintf(hd->prim_parts[p_no].name, "%s%d", hd->name, p_no + 1);
                p_no++;
                ASSERT(p_no < 4);
            }
            else
            {
                hd->logic_parts[l_no].start_lba = ext_lba + p->start_lba;   
                hd->logic_parts[l_no].sec_cnt = p->sec_cnt;
                hd->logic_parts[l_no].my_disk = hd;
                list_append(&partition_list, &hd->logic_parts[l_no].part_tag);
                sprintf(hd->logic_parts[l_no].name, "%s%d", hd->name, l_no + 5);
                l_no++;
                if (l_no >= 8) break;       // 仅支持8个逻辑分区
            }
        }
        p++;
    }

    sys_free(bs);
}

/* 打印分区信息 */
static int partition_info(struct list_elem *pelem, int arg UNUSED)
{
    struct partition *part = elem2entry(struct partition, part_tag, pelem);
    printk("    %s start_lba: 0x%x, sec_cnt: 0x%x\n", part->name, part->start_lba, part->sec_cnt);
    /* list_traversal格式要求 */
    return 0;
}

/* 硬盘中断处理程序 */
void intr_hd_handler(uint8_t irq_no)
{
    ASSERT(irq_no == 0x2e || irq_no == 0x2f);
    uint8_t ch_no = irq_no - 0x2e;
    struct ide_channel *channel = &channels[ch_no];
    ASSERT(channel->irq_no == irq_no);

    /* 每次读写硬盘时会申请锁，保证了一个通道在读写的时候始终是统一块硬盘 */
    if (channel->expecting_intr) 
    {
        channel->expecting_intr = false;
        sema_up(&channel->disk_done);   
        
        /* 读取状态寄存器使硬盘认为此次中断已被处理，从而硬盘可以执行新的读写，
           除了该方法外还可以发送reset命令 */
        inb(reg_status(channel));
    }
}

void ide_init(void)
{
    printk("ide_init start...\n");
    
    memset(channels, 0, sizeof(channels));

    /* 获取计算机插入的硬盘数，这个数据保存在BIOS数据区物理地址0x475处 */
    uint8_t hd_cnt = *((uint8_t *)0x475);
    ASSERT(hd_cnt > 0);
    list_init(&partition_list);
    channel_cnt = DIV_ROUND_UP(hd_cnt, 2);      // 反正不是1就是2
    struct ide_channel *channel;
    uint8_t channel_no = 0, dev_no = 0;

    /* 处理每个通道上的硬盘 */
    while (channel_no < channel_cnt)
    {
        channel = &(channels[channel_no]);
        sprintf(channel->name, "ide%d", channel_no);
        
        /* 反正8259A只有两个通道，不是1就是0 */
        switch (channel_no)
        {
            case 0:
                channel->port_base = 0x1f0;         // ide0通道上的起始端口是0x1f0
                channel->irq_no = 0x20 + 14;        // 8259A倒数第二个引脚
                break;
            case 1:
                channel->port_base = 0x170;         // ide1通道上的起始端口号是0x170
                channel->irq_no = 0x20 + 15;        // 8259A最后一个引脚
                break;
        }        

        channel->expecting_intr = 0;                // 未向硬盘写入指令不期望硬盘中断
        lock_init(&channel->lock);
        
        /* 初始化信号量为0目的是向硬盘控制器写入数据后硬盘驱动sema_down此信号量阻塞，
           直到硬盘完成后发送中断，由中断控制器sema_up此信号量唤醒等待的线程 */
        sema_init(&channel->disk_done, 0);

        register_handler(channel->irq_no, intr_hd_handler);

        /* 分别获取两个硬盘的参数和分区信息 */
        while (dev_no < 2)
        {
            struct disk *hd = &channel->devices[dev_no];
            hd->my_channel = channel;
            hd->dev_no = dev_no;
            sprintf(hd->name, "sd%c", 'a' + channel_no * 2 + dev_no);
            identify_disk(hd);
            /* 内核在0个盘中，是裸盘，不处理 */
            if (dev_no != 0) partition_scan(hd, 0);
            
            p_no = 0, l_no = 0;
            dev_no++;
        }
        
        dev_no = 0;         // 硬盘驱动器号置0，为下一个channel的两个硬盘初始化
        channel_no++;
    }
    
    printk("\n    all partition info\n");
    /* 打印分区信息 */
    list_traversal(&partition_list, partition_info, (int)NULL);
    
    printk("ide_init done.\n");
}

