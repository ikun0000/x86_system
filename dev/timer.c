#include "timer.h"
#include "io.h"
#include "print.h"
#include "interrupt.h"
#include "thread.h"
#include "debug.h"

#define IRQ0_FREQUENCY      100
#define INPUT_FREQUENCY     1193180
#define COUNTER0_VALUE      INPUT_FREQUENCY / IRQ0_FREQUENCY
#define COUNTER0_PORT       0x40
#define COUNTER0_NO         0
#define COUNTER_MODE        2
#define READ_WRITE_LATCH    3
#define PIT_CONTROL_PORT    0x43

/* 多少毫秒发生一次中断 */
#define mil_seconds_per_intr    (1000 / IRQ0_FREQUENCY)

uint32_t ticks;             // 内核自中断开启以来的总共滴答数

/* 把操作的计数器counter_no、读写属性rwl、计数器模式counter_mode
   写入模式控制寄存器，并初始化counter_port为counter_value */
static void frequency_set(uint8_t counter_port, \
                        uint8_t counter_no, \
                        uint8_t rwl, 
                        uint8_t counter_mode, \
                        uint16_t counter_value)
{
    outb(PIT_CONTROL_PORT, (uint8_t)(counter_no << 6 | rwl << 4 | counter_mode << 1));
    outb(counter_port, (uint8_t)counter_value);             // low 8 bit
    outb(counter_port, (uint8_t)(counter_value >> 8));     // high 8 bit
}

static void intr_timer_handler(void)
{
    struct task_struct *cur_thread = running_thread();
    ASSERT(cur_thread->stack_magic == 0x19870916);      // 检测堆栈是否溢出

    cur_thread->elapsed_ticks++;                        // 线程占用的cpu时间
    ticks++;

    if (cur_thread->ticks == 0) schedule(); // 如果cpu时间片到了就切换任务
    else cur_thread->ticks--;
}

/* 以tick为单位的sleep */
static void ticks_to_sleep(uint32_t sleep_ticks)
{
    uint32_t start_tick = ticks;

    /* 若间隔的tick数不够让出CPU */
    while (ticks - start_tick < sleep_ticks) thread_yield();
}

/* 以毫秒为单位sleep */
void mtime_sleep(uint32_t m_seconds)
{
    uint32_t sleep_ticks = DIV_ROUND_UP(m_seconds, mil_seconds_per_intr);
    ASSERT(sleep_ticks > 0);   
    ticks_to_sleep(sleep_ticks);
}

/* initialize PIT 8253 */
void timer_init() 
{
    put_str("timer_init start...\n");
    frequency_set(COUNTER0_PORT, COUNTER0_NO, READ_WRITE_LATCH, COUNTER_MODE, COUNTER0_VALUE);
    register_handler(0x20, intr_timer_handler);
    put_str("timer_init done.\n");
}
