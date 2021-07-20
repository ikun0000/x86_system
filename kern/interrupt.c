#include "interrupt.h"
#include "stdint.h"
#include "global.h"
#include "io.h"
#include "print.h"

#define PIC_M_CTRL 0x20     // 8259A master control port
#define PIC_M_DATA 0X21     // 8259A master data port
#define PIC_S_CTRL 0xa0     // 8259A slaver control port
#define PIC_S_DATA 0xa1     // 8259A slaver data port

#define IDT_DESC_CNT 0x81   // current support interrupt count

#define EFLAGS_IF 0x00000200        // EFLAGS IF = 1
#define GET_EFLAGS(EFLAG_VAR) asm volatile("pushfl; popl %0": "=g"(EFLAG_VAR))

extern uint32_t syscall_handler(void);

struct gate_desc 
{
    uint16_t    func_offset_low_word;
    uint16_t    selector;
    uint8_t     dcount;
    uint8_t     attribute;
    uint16_t    func_offset_high_word;  
}__attribute__((packed));

static void make_idt_desc(struct gate_desc *p_gdesc, uint8_t attr, intr_handler function);
static struct gate_desc idt[IDT_DESC_CNT];

// 对应中断的描述
char *intr_name[IDT_DESC_CNT];
// 真正的中断处理程序
intr_handler idt_table[IDT_DESC_CNT];
// in kernel.S定义，中断处理的入口 
extern intr_handler intr_entry_table[IDT_DESC_CNT];

/* 初始化PIC，现在是8259A芯片 */
static void pic_init(void)
{
    /* init master */
    outb(PIC_M_CTRL, 0x11);     // ICW1: 边沿触发，级联8259,需要ICW4
    outb(PIC_M_DATA, 0x20);     // ICW2: 起始中断向量号0x20
    outb(PIC_M_DATA, 0x04);     // ICW3: IR2 connect slaver
    outb(PIC_M_DATA, 0x01);     // ICW4: 8086 mode, normal EOI

    /* init slaver */
    outb(PIC_S_CTRL, 0x11);     // ICW1: 边沿触发，级联8259,需要ICW4
    outb(PIC_S_DATA, 0x28);     // ICW2: 起始中断向量号0x28
    outb(PIC_S_DATA, 0x02);     // ICW3: connect master to IR2
    outb(PIC_S_DATA, 0x01);     // ICW4: 8086 mode, normal EOI

    /* open master IR0 */
    outb(PIC_M_DATA, 0xfc);
    outb(PIC_S_DATA, 0xff);
    
    put_str("    pic_init done.\n");
}

/* general interrupt controller */
static void general_intr_handler(uint8_t vec_nr)
{
    /* IRQ7和IRQ15会产生伪中断，无需处理，这是8259A的最后一个引脚 */
    if (vec_nr == 0x27 || vec_nr == 0x2f) return;
    
    set_cursor(0);
    int cursor_pos = 0;
    while (cursor_pos < 320)
    {
        put_char(' ');
        cursor_pos++;
    }
    
    set_cursor(0);
    put_str("!!!!! exception message begin !!!!!\n");
    set_cursor(88);
    put_str(intr_name[vec_nr]);
    /* 如果是缺页异常则打印地址 */
    if (vec_nr == 14)
    {
        int page_fault_vaddr = 0;
        /* cr2会在page falut时放入地址 */
        asm volatile ("movl %%cr2, %0": "=r"(page_fault_vaddr));
        put_str("\n        page fault addr is 0x"); put_int(page_fault_vaddr);
    }
    put_str("\n!!!!! exception message end !!!!!\n");
    
    while (1);
}

/* 完成一般中断处理程序的注册以及异常名注册 */
static void exception_init(void)
{
    int i;
    for (i = 0; i < IDT_DESC_CNT; i++)
    {
        idt_table[i] = general_intr_handler;
        intr_name[i] = "unknown";
    }
    
    intr_name[0] = "#DE Divide Error"; 
    intr_name[1] = "#DB Debug Exception";
    intr_name[2] = "#NMI Interrupt";
    intr_name[3] = "#BP Breakpoint Exception";
    intr_name[4] = "#OF Overflow Exception";
    intr_name[5] = "#BR BOUND Range Exceeded Exception";
    intr_name[6] = "#UD Invalid Opcode Exception";
    intr_name[7] = "#NM Device Not Available Exception";
    intr_name[8] = "#DF Double Fault Exception";
    intr_name[9] = "Coprocessor Segment Overrun";
    intr_name[10] = "#TS Invalid TSS Exception";
    intr_name[11] = "#NP Segment Not Present";
    intr_name[12] = "#SS Stack Fault Exception";
    intr_name[13] = "#GP General Protection Exception";
    intr_name[14] = "#PF Page-Fault Exception";
    // 第15项是Intel保留项，未使用
    intr_name[16] = "#MF x87 FPU Floating-Point Error";
    intr_name[17] = "#AC Alignment Check Exception";
    intr_name[18] = "#MC Machine-Check Exception";
    intr_name[19] = "#XF SIMD Floating-Point Exception";
}

/* create interrupt gate descriptor */
static void make_idt_desc(struct gate_desc *p_gdesc, uint8_t attr, intr_handler function)
{
    p_gdesc->func_offset_low_word = (uint32_t)function & 0x0000ffff;
    p_gdesc->selector = SELECTOR_K_CODE;
    p_gdesc->dcount = 0;
    p_gdesc->attribute = attr;
    p_gdesc->func_offset_high_word = ((uint32_t)function & 0xffff0000) >> 16;
}

static void idt_desc_init(void)
{
    int i, lastindex = IDT_DESC_CNT - 1;
    for (i = 0; i < IDT_DESC_CNT; i++)
    {
        make_idt_desc(&idt[i], IDT_DESC_ATTR_DPL0, intr_entry_table[i]);
    }
    
    /* 单独处理系统调用，系统调用对应中断们DPL为3，中断处理函数为syscall_handler */
    make_idt_desc(&idt[lastindex], IDT_DESC_ATTR_DPL3, syscall_handler);
    put_str("    idt_desc_init done.\n");
}

/* work all interrupt init */
void idt_init() 
{
    put_str("idt_init start...\n");
    idt_desc_init();
    exception_init();
    pic_init();

    /* load IDT */
    uint64_t idt_operand = ((sizeof(idt) - 1) | ((uint64_t)(uint32_t)idt << 16));
    asm volatile ("lidt %0": :"m"(idt_operand):);
    put_str("idt_init done.\n");
}

/* 打开中断并返回之前的状态 */
enum intr_status intr_enable()
{
    enum intr_status old_status;
    if (INTR_ON == intr_get_status()) 
    {
        old_status = INTR_ON;
        return old_status;
    }
    else
    {
        old_status = INTR_OFF;
        asm volatile ("sti");       // 使用sti指令置位IF
        return old_status;
    }
}

/* 关闭中断并返回之前的状态 */
enum intr_status intr_disable() 
{
    enum intr_status old_status;
    if (INTR_ON == intr_get_status())
    {
        old_status = INTR_ON;
        asm volatile ("cli");       // 使用cli指令清除IF
        return old_status;
    }
    else
    {
        old_status = INTR_OFF;
        return old_status;
    }
}

/* 将中断状态设置为status */
enum intr_status intr_set_status(enum intr_status status)
{
    return (status & INTR_ON) ? intr_enable() : intr_disable();
}

/* 获取当前中断状态 */
enum intr_status intr_get_status() 
{
    uint32_t eflags = 0;
    GET_EFLAGS(eflags);
    return (EFLAGS_IF & eflags) ? INTR_ON : INTR_OFF;
}

/* 在中断处理程序表中注册中断处理程序 */
void register_handler(uint8_t vector_no, intr_handler function) 
{
    idt_table[vector_no] = function;
}
