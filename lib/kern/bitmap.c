#include "bitmap.h"
#include "string.h"
#include "print.h"
#include "interrupt.h"
#include "debug.h"

/* 将位图初始化 */
void bitmap_init(struct bitmap *btmp)
{
    memset(btmp->bits, 0, btmp->btmp_bytes_len);
}

/* 判断bit_idx位是否位1，若为1返回非0，否则返回0 */
int bitmap_scan_test(struct bitmap *btmp, uint32_t bit_idx)
{
    // uint32_t byte_idx = bit_idx / 8;
    uint32_t byte_idx = bit_idx >> 3;
    // uint32_t bit_odd = bit_idx % 8;
    uint32_t bit_odd = bit_idx & 0x00000007;
    return (btmp->bits[byte_idx] & (BITMAP_MASK << bit_odd));
}

/* 在位图中申请连续cnt个位，成功则返回其起始下标，失败返回-1 */
int bitmap_scan(struct bitmap *btmp, uint32_t cnt)
{
    uint32_t idx_byte = 0;
    /* 先逐个字节比较 */
    while ((0xff == btmp->bits[idx_byte]) && (idx_byte < btmp->btmp_bytes_len)) idx_byte++;

    ASSERT(idx_byte < btmp->btmp_bytes_len);
    if (idx_byte == btmp->btmp_bytes_len)           // 找不到可用空间
    {
        return -1;
    }

    /* 在某个字节的位中找到空闲位置，逐位比较那个是空闲位 */
    int idx_bit = 0;
    while ((uint8_t)(BITMAP_MASK << idx_bit) & btmp->bits[idx_byte]) idx_bit++;
    
    // int bit_idx_start = idx_byte * 8 + idx_bit;
    int bit_idx_start = (idx_byte << 3) + idx_bit;
    if (cnt == 1)
    {
        return bit_idx_start;
    }

    uint32_t bit_left = ((btmp->btmp_bytes_len << 3) - bit_idx_start);   
    uint32_t next_bit = bit_idx_start + 1;
    uint32_t count = 1;

    bit_idx_start = -1;
    while (bit_left-- > 0)
    {
        if (!(bitmap_scan_test(btmp, next_bit))) count++;       // 如果下一位为0
        else count = 0;

        if (count == cnt) 
        {
            bit_idx_start = next_bit - cnt + 1;
            break;
        }
        next_bit++;
    }
    return bit_idx_start;
}

/* 将位图btmp的bit_idx位置为value */
void bitmap_set(struct bitmap *btmp, uint32_t bit_idx, int8_t value)
{
    ASSERT((value == 0) || (value == 1));
    uint32_t byte_idx = bit_idx >> 3;
    uint32_t bit_odd = bit_idx & 0x00000007;
    
    if (value) btmp->bits[byte_idx] |= (BITMAP_MASK << bit_odd);
    else btmp->bits[byte_idx] &= ~(uint8_t)(BITMAP_MASK << bit_odd);
}
