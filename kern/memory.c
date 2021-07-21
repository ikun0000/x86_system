#include "memory.h"
#include "bitmap.h"
#include "stdint.h"
#include "global.h"
#include "print.h"
#include "debug.h"
#include "string.h"
#include "sync.h"
#include "thread.h"
#include "interrupt.h"

#define MEM_BITMAP_BASE     0xc009a000
#define K_HEAP_START        0xc0100000

/* 根据虚拟地址获取其在PDT/PT中的索引 */
#define PDE_IDX(addr) ((addr & 0xffc00000) >> 22)
#define PTE_IDX(addr) ((addr & 0x003ff000) >> 12)

/* 内存池结构 */
struct pool 
{
    struct bitmap pool_bitmap;          // 本内存池的位图结构，用于管理物理内存
    uint32_t phy_addr_start;            // 本内存池管理的物理内存的起始地址
    uint32_t pool_size;                 // 本内存池的字节容量
    struct lock lock;                   // 申请内存时互斥
};

/* 内存仓库arena元信息 */
struct arena 
{
    struct mem_block_desc *desc;        // 此arena关联的mem_block_desc
    uint32_t cnt;                       // 当large为1时代表arena页框数，为0时表示空闲的mem_block数量
    int large;                          // 大内存模式（1024b）
};

struct mem_block_desc k_block_descs[DESC_CNT];       // 内核内存块描述符数
struct pool kernel_pool, user_pool;     // 管理内核物理内存和用户物理内存
struct virtual_addr kernel_vaddr;       // 给内核分配虚拟地址

/* 在pf表示的虚拟内存池中申请pg_cnt个虚拟页，成功返回虚拟页的起始地址，失败返回NULL */
static void *vaddr_get(enum pool_flags pf, uint32_t pg_cnt) 
{
    int vaddr_start = 0, bit_idx_start = -1;
    uint32_t cnt = 0;
    if (pf == PF_KERNEL)
    {
        bit_idx_start = bitmap_scan(&kernel_vaddr.vaddr_bitmap, pg_cnt);
        if (bit_idx_start == -1) return NULL;
        while (cnt < pg_cnt) bitmap_set(&kernel_vaddr.vaddr_bitmap, bit_idx_start + cnt++, 1);
        // vaddr_start = kernel_vaddr.vaddr_start + bit_idx_start * PG_SIZE;
        vaddr_start = kernel_vaddr.vaddr_start + (bit_idx_start << 12);
    }
    else
    {
        // 分配用户内存
        struct task_struct *cur = running_thread();
        bit_idx_start = bitmap_scan(&cur->userproc_vaddr.vaddr_bitmap, pg_cnt);
        if (bit_idx_start == -1) return NULL;
        while (cnt < pg_cnt) bitmap_set(&cur->userproc_vaddr.vaddr_bitmap, bit_idx_start + cnt++, 1);
        // vaddr_start = cur->userproc_vaddr.vaddr_start + (bit_idx_start * PG_SIZE);
        vaddr_start = cur->userproc_vaddr.vaddr_start + (bit_idx_start << 12);
        /* 0xc0000000 - PG_SIZE是用户3特权级的栈 */
        ASSERT((uint32_t)vaddr_start < (0xc0000000 - PG_SIZE));
    }
    return (void *)vaddr_start;
}

/* 得到虚拟地址vaddr对应的pte指针 */
uint32_t *pte_ptr(uint32_t vaddr)
{
    /*
    先访问页目录自己
    再用vaddr的pde作为pte
    最后根据vaddr的pte的索引乘4得到
    */
    uint32_t *pte = (uint32_t *)(0xffc00000 + \
    ((vaddr & 0xffc00000) >> 10) + \
    PTE_IDX(vaddr) * 4);
    return pte;
}

/* 得到虚拟地址vaddr对应的pde指针 */
uint32_t *pde_ptr(uint32_t vaddr)
{
    /* 0xfffff000是页目录自身的起始地址 */
    uint32_t *pde = (uint32_t *)(0xfffff000 + PDE_IDX(vaddr) * 4);
    return pde;
}

/* 在m_pool指向的物理内存池中分配一个物理页，成功返回页的起始地址，失败返回NULL */
static void *pmalloc(struct pool *m_pool)
{
    int bit_idx = bitmap_scan(&m_pool->pool_bitmap, 1);
    if (bit_idx == -1) return NULL;

    bitmap_set(&m_pool->pool_bitmap, bit_idx, 1);
    // uint32_t page_phyaddr = ((bit_idx * PG_SIZE) + m_pool->phy_addr_start);
    uint32_t page_phyaddr = ((bit_idx << 12) + m_pool->phy_addr_start);
    return (void *)page_phyaddr;
}

/* 在页表中添加虚拟地址到物理地址的映射 */
static void page_table_add(void *_vaddr, void *_page_phyaddr)
{
    uint32_t vaddr = (uint32_t)_vaddr, page_phyaddr = (uint32_t)_page_phyaddr;
    uint32_t *pde = pde_ptr(vaddr);
    uint32_t *pte = pte_ptr(vaddr);
    
    if (*pde & 0x00000001)          // 该虚拟地址的pde表项是否存在
    {
        ASSERT(!(*pte & 0x00000001));   // 对应页表项pte必须不存在

        if (!(*pte & 0x00000001))       // 创建该虚拟地址对应的页表项pte
        {
            *pte = (page_phyaddr | PG_US_U | PG_RW_W | PG_P_1);
        }
        else                            // 页表已经存在，被重新分配
        {
            PANIC("pte repeat");
            *pte = (page_phyaddr | PG_US_U | PG_RW_W | PG_P_1);
        }
    }
    else                            // 对应虚拟地址的pde页目录项还没创建页表
    {
        // 页目录中没有该虚拟地址对应的页表，所以分配一个物理页作为页表
        uint32_t pde_phyaddr = (uint32_t)pmalloc(&kernel_pool);
        *pde = (pde_phyaddr | PG_US_U | PG_RW_W | PG_P_1);
        
        memset((void *)((int)pte & 0xfffff000), 0, PG_SIZE);
        ASSERT(!(*pte & 0x00000001));
        *pte = (page_phyaddr | PG_US_U | PG_RW_W | PG_P_1);
    }
}

/* 分配pg_cnt个页空间，成功返回起始虚拟地址，失败返回NULL */
void *malloc_page(enum pool_flags pf, uint32_t pg_cnt)
{
    ASSERT(pg_cnt > 0 && pg_cnt < 3840);
    
    /* 
    1. 通过vaddr_get在虚拟内存池中获取虚拟地址
    2. 使用pmalloc在物理内存池中获取物理内存地址
    3. 使用page_table_add将虚拟地址映射到物理地址    
    */

    void *vaddr_start = vaddr_get(pf, pg_cnt);
    if (vaddr_start == NULL) return NULL;

    uint32_t vaddr = (uint32_t)vaddr_start, cnt = pg_cnt;
    struct pool *mem_pool = pf & PF_KERNEL ? &kernel_pool : &user_pool;

    /* 因为虚拟地址是连续的，物理地址可能不是连续的，所以物理地址必须一页一页分配 */
    while (cnt-- > 0)
    {
        void *page_phyaddr = pmalloc(mem_pool);
        if (page_phyaddr == NULL) return NULL;      // 此时物理内存不足
        page_table_add((void *)vaddr, page_phyaddr);
        vaddr += PG_SIZE;
    }
    return vaddr_start;
}

/* 从内核物理内存池中申请pg_cnt页内存，成功返回虚拟地址，失败返回NULL */
void *get_kernel_pages(uint32_t pg_cnt)
{
    void *vaddr = malloc_page(PF_KERNEL, pg_cnt);
    if (vaddr != NULL) 
    {
        memset(vaddr, 0, pg_cnt * PG_SIZE);
    }
    return vaddr;
}

/* 在用户空间中申请4K内存，并返回其虚拟地址 */
void *get_user_pages(uint32_t pg_cnt)
{
    lock_acquire(&user_pool.lock);
    void *vaddr = malloc_page(PF_USER, pg_cnt);
    memset(vaddr, 0, pg_cnt * PG_SIZE);
    lock_release(&user_pool.lock);
    return vaddr;
}

/* 将地址vaddr和pf池中的物理地址关联，仅支持一页空间分配 */
void *get_a_page(enum pool_flags pf, uint32_t vaddr)
{
    struct pool *mem_pool = pf & PF_KERNEL ? &kernel_pool : &user_pool;
    lock_acquire(&mem_pool->lock);
    
    /* 把虚拟地址对应的位图置1 */
    struct task_struct *cur = running_thread();
    int32_t bit_idx = -1;
    
    /* 如果当前用户进程申请用户内存，就修改用户自己的虚拟地址位图 */
    if (cur->pgdir != NULL && pf == PF_USER)
    {
        bit_idx = (vaddr - cur->userproc_vaddr.vaddr_start) / PG_SIZE;
        ASSERT(bit_idx > 0);
        bitmap_set(&cur->userproc_vaddr.vaddr_bitmap, bit_idx, 1);
    }
    else if (cur->pgdir == NULL && pf == PF_KERNEL)
    {
        /* 如果当前线程申请的是内核内存则修改kernel_vaddr */
        bit_idx = (vaddr - kernel_vaddr.vaddr_start) / PG_SIZE;
        ASSERT(bit_idx > 0);
        bitmap_set(&kernel_vaddr.vaddr_bitmap, bit_idx, 1);
    }
    else 
    {
        PANIC("get_a_page: not allow kernel alloc userspace or user alloc kernelspace by get_a_page");
    }

    void *page_phyaddr = pmalloc(mem_pool);
    if (page_phyaddr == NULL) return NULL;
    page_table_add((void *)vaddr, page_phyaddr);
    
    lock_release(&mem_pool->lock);
    return (void *)vaddr;
}

/* 等到虚拟地址映射的物理地址 */
uint32_t addr_v2p(uint32_t vaddr)
{
    uint32_t *pte = pte_ptr(vaddr);
    return ((*pte & 0xfffff000) + (vaddr & 0x00000fff));
}
    
static void mem_pool_init(uint32_t all_mem)
{
    put_str("    mem_pool_init start...\n");
    
    // 1个页目录表和随后的255个页表
    // uint32_t page_table_size = PG_SIZE * 256;
    uint32_t page_table_size = PG_SIZE << 8;
    uint32_t used_mem = page_table_size + 0x100000;
    uint32_t free_mem = all_mem - used_mem;
    // uint16_t all_free_pages = free_mem / PG_SIZE;
    uint16_t all_free_pages = free_mem >> 12;

    // uint16_t kernel_free_pages = all_free_pages / 2;
    uint16_t kernel_free_pages = all_free_pages >> 1;
    uint16_t user_free_pages = all_free_pages - kernel_free_pages;

    /* 余数不做处理，会丢失内存。好处是不用做内存越界检查*/
    // uint32_t kbm_length = kernel_free_pages / 8
    uint32_t kbm_length = kernel_free_pages >> 3;
    // uint32_t ubm_length = user_free_pages / 8
    uint32_t ubm_length = user_free_pages >> 3;

    // 内核和用户程序的可分配起始物理地址
    uint32_t kp_start = used_mem;
    // uint32_t up_start = kp_start + kernel_free_pages * PG_SIZE;
    uint32_t up_start = kp_start + (kernel_free_pages << 12);

    kernel_pool.phy_addr_start = kp_start;
    user_pool.phy_addr_start = up_start;

    // kernel_pool.pool_size = kernel_free_pages * PG_SIZE;
    kernel_pool.pool_size = kernel_free_pages << 12;
    // user_pool.pool_size = user_free_pages * PG_SIZE;
    user_pool.pool_size = user_free_pages << 12;
    
    kernel_pool.pool_bitmap.btmp_bytes_len = kbm_length;
    user_pool.pool_bitmap.btmp_bytes_len = ubm_length;

    /* 当前只有32MB内存，这里位图总共1KB，所以只需要2KB存放bitmap */
    kernel_pool.pool_bitmap.bits = (void *)MEM_BITMAP_BASE;
    user_pool.pool_bitmap.bits = (void *)(MEM_BITMAP_BASE + kbm_length);

    put_str("    kernel_pool_bitmap_start: 0x"); put_int((int)kernel_pool.pool_bitmap.bits);
    put_str(", kernel_pool_phy_addr_start: 0x"); put_int(kernel_pool.phy_addr_start);
    put_char('\n');
    put_str("    user_pool_bitmap_start: 0x"); put_int((int)user_pool.pool_bitmap.bits);
    put_str(", user_pool_phy_addr_start: 0x"); put_int(user_pool.phy_addr_start);
    put_char('\n');
    
    /* 将位图置0 */
    bitmap_init(&kernel_pool.pool_bitmap);
    bitmap_init(&user_pool.pool_bitmap);

    lock_init(&kernel_pool.lock);
    lock_init(&user_pool.lock);

    /* 初始化内核虚拟地址位图，维护内核虚拟地址，与内核内存池大小一致 */
    kernel_vaddr.vaddr_bitmap.btmp_bytes_len = kbm_length;
    
    /* 内核虚拟地址池的位图位于内核物理地址内存池位图和用户物理内存池位图之后 */
    kernel_vaddr.vaddr_bitmap.bits = (void *)(MEM_BITMAP_BASE + kbm_length + ubm_length);
    
    kernel_vaddr.vaddr_start = K_HEAP_START;
    bitmap_init(&kernel_vaddr.vaddr_bitmap);
    put_str("    mem_pool_init done\n");
}

/* 为malloc做准备 */
void block_desc_init(struct mem_block_desc *desc_array)
{
    uint16_t desc_idx, block_size = 16;
    
    /* 初始化mem_block_desc描述符 */
    for (desc_idx = 0; desc_idx < DESC_CNT; desc_idx++)
    {
        desc_array[desc_idx].block_size = block_size;
        desc_array[desc_idx].blocks_per_arena = (PG_SIZE - sizeof(struct arena)) / block_size;
        list_init(&desc_array[desc_idx].free_list);
        block_size *= 2;
        // block_size <<= 1;           // 下一个desc的块大小
    }
}

/* 返回arena中第idx个内存块地址 */
static struct mem_block *arena2block(struct arena *a, uint32_t idx)
{
    return (struct mem_block *)((uint32_t)a + sizeof(struct arena) + idx * a->desc->block_size);
}

/* 返回内存块b所在的arena地址 */
static struct arena *block2arena(struct mem_block *b)
{
    return (struct arena *)((uint32_t)b & 0xfffff000);
}

/* 在堆中申请size字节内存 */
void *sys_malloc(uint32_t size)
{
    enum pool_flags PF;
    struct pool *mem_pool;
    uint32_t pool_size;
    struct mem_block_desc *descs;
    struct task_struct *cur_thread = running_thread();

    /* 判断哪个内存池 */
    if (cur_thread->pgdir == NULL)
    {
        /* 为内核线程 */
        PF = PF_KERNEL;
        pool_size = kernel_pool.pool_size;
        mem_pool = &kernel_pool;
        descs = k_block_descs;
    }
    else 
    {
        /* 为用户线程 */
        PF = PF_USER;
        pool_size = user_pool.pool_size;
        mem_pool = &user_pool;
        descs = cur_thread->u_block_desc;
    }
    
    /* 如果申请的内存不在内存池容量范围内则返回NULL */
    if (!(size > 0 && size < pool_size)) return NULL;
    struct arena *a;
    struct mem_block *b;
    lock_acquire(&mem_pool->lock);
    
    /* 超过大内存块1024直接返回页框 */
    if (size > 1024)
    {
        uint32_t page_cnt = DIV_ROUND_UP(size + sizeof(struct arena), PG_SIZE);

        a = malloc_page(PF, page_cnt);
        if (a != NULL) 
        { 
            memset(a, 0, page_cnt * PG_SIZE);

            /* 对于分配大内存框，将desc置为NULL，cnt置为页框数，large置1 */
            a->desc = NULL;
            a->cnt = page_cnt;
            a->large = 1;
            lock_release(&mem_pool->lock);
            return (void *)(a + 1);     // 跨过arena大小
        }
        else
        {
            lock_release(&mem_pool->lock);
            return NULL;
        }
    }
    else
    {
        /* 内存分配小于1024 */
        uint8_t desc_idx;

        /* 在内存块描述符中找到合适大小的内存块描述符 */
        for (desc_idx = 0; desc_idx < DESC_CNT; desc_idx++)
        {
            if (size <= descs[desc_idx].block_size) break;
        }

        /* 如果mem_block_desc中的free_list中已经没有可用的mem_block则创建新的arena */
        if (list_empty(&descs[desc_idx].free_list))
        {
            a = malloc_page(PF, 1);
            if (a == NULL)
            {
                lock_release(&mem_pool->lock);
                return NULL;
            }
            memset(a, 0, PG_SIZE);
            
            /* 设置新的arena的desc指向对应的描述符，large=0，cnt为对应的块数 */
            a->desc = &descs[desc_idx];
            a->large = 0;
            a->cnt = descs[desc_idx].blocks_per_arena;
            uint32_t block_idx;
            
            enum intr_status old_status = intr_disable();

            /* 将arena拆分成内存块，添加到内存块描述符free_list中 */
            for (block_idx = 0; block_idx < descs[desc_idx].blocks_per_arena; block_idx++)
            {
                b = arena2block(a, block_idx);
                ASSERT(!elem_find(&a->desc->free_list, &b->free_elem));
                list_append(&a->desc->free_list, &b->free_elem);
            }
            intr_set_status(old_status);
        }
        
        /* 开始分配内存块 */
        b = elem2entry(struct mem_block, free_elem, list_pop(&(descs[desc_idx].free_list)));
        memset(b, 0, descs[desc_idx].block_size);

        a = block2arena(b);         // 获取内存块b对应arena
        a->cnt--;                   // 空闲块减1
        lock_release(&mem_pool->lock);
        return (void *)b;
    }
}

/* 释放物理地址回地址池 */
void pfree(uint32_t pg_phy_addr)
{
    struct pool *mem_pool;
    uint32_t bit_idx = 0;
    if (pg_phy_addr >= user_pool.phy_addr_start) 
    {
        /* 如果传入的物理地址大于用户地址池起始地址则该地址属于用户地址池 */
        mem_pool = &user_pool;
        // bit_idx = (pg_phy_addr - user_pool.phy_addr_start) / PG_SIZE;
        bit_idx = (pg_phy_addr - user_pool.phy_addr_start) >> 12;
    }
    else 
    {
        /* 是内核地址池 */
        mem_pool = &kernel_pool;
        // bit_idx = (pg_phy_addr - kernel_pool.phy_addr_start) / PG_SIZE;
        bit_idx = (pg_phy_addr - kernel_pool.phy_addr_start) >> 12;
    }
    /* 将对应页的位置0即释放内存 */
    bitmap_set(&mem_pool->pool_bitmap, bit_idx, 0); 
}

/* 去除页表中vaddr的映射，只会删除pte */
static void page_table_pte_remove(uint32_t vaddr) 
{
    uint32_t *pte = pte_ptr(vaddr);
    *pte &= ~PG_P_1;        // 将PTE的P位置0
    /* 需要更新TLB，否则该记录很有可能还在TLB中 */
    asm volatile ("invlpg %0": :"m"(vaddr):"memory");
}

/* 在虚拟地址池释放_vaddr起始的连续pg_cnt个内存页 */
static void vaddr_remove(enum pool_flags pf, void *_vaddr, uint32_t pg_cnt)
{
    uint32_t bit_idx_start = 0, vaddr = (uint32_t)_vaddr, cnt = 0;

    if (pf == PF_KERNEL)
    {
        // bit_idx_start = (vaddr - kernel_vaddr.vaddr_start) / PG_SIZE;
        bit_idx_start = (vaddr - kernel_vaddr.vaddr_start) >> 12;
        while (cnt < pg_cnt) bitmap_set(&kernel_vaddr.vaddr_bitmap, bit_idx_start + cnt++, 0);
    }
    else 
    {
        struct task_struct *cur_thread = running_thread();
        // bit_idx_start = (vaddr - cur_thread->userproc_vaddr.vaddr_start) / PG_SIZE;
        bit_idx_start = (vaddr - cur_thread->userproc_vaddr.vaddr_start) >> 12;
        while (cnt < pg_cnt) bitmap_set(&cur_thread->userproc_vaddr.vaddr_bitmap, bit_idx_start + cnt++, 0);
    }
}

/* 释放以vaddr为起始地址的cnt个物理页面 */
void mfree_page(enum pool_flags pf, void *_vaddr, uint32_t pg_cnt)
{
    uint32_t pg_phy_addr;
    uint32_t vaddr = (int32_t)_vaddr, page_cnt = 0;
    ASSERT(pg_cnt >= 1 && vaddr % PG_SIZE == 0);   
    pg_phy_addr = addr_v2p(vaddr);

    /* 保证释放的内存是低1MB+1k页目录+1k页表 */
    ASSERT((pg_phy_addr % PG_SIZE) == 0 && pg_phy_addr >= 0x102000);

    if (pg_phy_addr >= user_pool.phy_addr_start)
    {
        /* 用户地址空间 */
        vaddr -= PG_SIZE;
        while (page_cnt < pg_cnt)
        {
            vaddr += PG_SIZE;
            pg_phy_addr = addr_v2p(vaddr);
            
            /* 确保这是用户空间的地址 */
            ASSERT((pg_phy_addr % PG_SIZE) == 0 && pg_phy_addr >= user_pool.phy_addr_start);
            
            /* 先释放对应的物理页 */
            pfree(pg_phy_addr);
            /* 再清除虚拟地址到物理地址的映射 */
            page_table_pte_remove(vaddr);
            
            page_cnt++;
        }

        /* 清除对应虚拟地址位图的位 */
        vaddr_remove(pf, _vaddr, pg_cnt);
    }
    else 
    {
        /* 内核地址空间 */
        vaddr -= PG_SIZE;
        while (page_cnt < pg_cnt)
        {
            vaddr += PG_SIZE;
            pg_phy_addr = addr_v2p(vaddr);
            
            /* 确保释放的地址属于内核地址池 */
            ASSERT((pg_phy_addr % PG_SIZE) == 0 && 
                    pg_phy_addr >= kernel_pool.phy_addr_start && 
                    pg_phy_addr < user_pool.phy_addr_start);

            /* 先释放物理内存页 */
            pfree(pg_phy_addr);
            /* 删除页表中的地址映射 */
            page_table_pte_remove(vaddr);
            
            page_cnt++;
        }
        
        /* 清空虚拟地址池位图对应的位 */
        vaddr_remove(pf, _vaddr, pg_cnt);
    }
}

/* 回收内存地址ptr */
void sys_free(void *ptr)
{
    ASSERT(ptr != NULL);
    if (ptr != NULL)
    {
        enum pool_flags PF;
        struct pool *mem_pool;

        /* 判断是内核线程还是进程 */
        if (running_thread()->pgdir == NULL)
        {
            ASSERT((uint32_t)ptr >= K_HEAP_START);
            PF = PF_KERNEL;
            mem_pool = &kernel_pool;
        }
        else
        {
            PF = PF_USER;
            mem_pool = &user_pool;
        }

        lock_acquire(&mem_pool->lock);
        struct mem_block *b = ptr;
        struct arena *a = block2arena(b);

        ASSERT(a->large == 0 || a->large == 1);
        if (a->desc == NULL && a->large == 1)
        {
            /* 分配大于1024的大内存 */
            mfree_page(PF, a, a->cnt);
        }
        else 
        {
            /* 小于1024的小内存 */
            /* 先将内存块回收到free_list中 */
            list_append(&a->desc->free_list, &b->free_elem);
        
            /* 判断此arena是否全部空闲，如果是则释放这个arena */
            /* 把mem_blcok放回到free_list中后把cnt自增1，这里的把cnt自增1可以放到外面做 */
            if (++(a->cnt) == a->desc->blocks_per_arena)
            {
                uint32_t block_idx;
                for (block_idx = 0; block_idx < a->desc->blocks_per_arena; block_idx++) 
                {
                    struct mem_block *b = arena2block(a, block_idx);
                    ASSERT(elem_find(&a->desc->free_list, &b->free_elem));
                    list_remove(&b->free_elem);
                }
                mfree_page(PF, a, 1);
            }
        }
        lock_release(&mem_pool->lock);
    }
}
 
void mem_init()
{
    put_str("mem_init start...\n");
    // 0xb00地址在loader.S中定义
    uint32_t mem_bytes_total = (*(uint32_t *)0xb00);
    mem_pool_init(mem_bytes_total);
    block_desc_init(k_block_descs);
    put_str("mem_init done.\n");
}
