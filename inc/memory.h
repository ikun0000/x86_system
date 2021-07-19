#ifndef __KERNEL_MEMORY_H
#define __KERNEL_MEMORY_H

#include "global.h"
#include "bitmap.h"

/* 内存池标记 */
enum pool_flags 
{
    PF_KERNEL = 1,          // 内核内存池
    PF_USER   = 2           // 用户内存池
};

#define PG_P_1  1
#define PG_P_0  0
#define PG_RW_R 0
#define PG_RW_W 2
#define PG_US_S 0
#define PG_US_U 4

/* 虚拟地址池，用于虚拟地址管理 */
struct virtual_addr 
{
    struct bitmap vaddr_bitmap;         // 虚拟地址位图结构
    uint32_t vaddr_start;               // 虚拟地址起始地址
};


extern struct pool kernel_pool, user_pool;
void mem_init(void);
void *get_kernel_pages(uint32_t pg_cnt);
void *malloc_page(enum pool_flags pf, uint32_t pg_cnt);
void malloc_init(void);
uint32_t *pte_ptr(uint32_t vaddr);
uint32_t *pde_ptr(uint32_t vaddr);
uint32_t addr_v2p(uint32_t vaddr);
void *get_a_page(enum pool_flags pf, uint32_t vaddr);
void *get_user_pages(uint32_t pg_cnt);

#endif
