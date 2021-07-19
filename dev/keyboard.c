#include "global.h"
#include "keyboard.h"
#include "print.h"
#include "interrupt.h"
#include "io.h"
#include "ioqueue.h"

#define KBD_BUF_PORT 0x60           // 键盘buffer寄存器端口

/* 用转义字符定义部分控制字符 */
#define esc         '\033'         // hex: \x1b
#define backspace   '\b'
#define tab         '\t'
#define enter       '\r'
#define delete      '\177'          // hex: \x7f

/* 以上不可见字符一律转为0 */
#define char_invisible          0
#define ctrl_l_char             char_invisible
#define ctrl_r_char             char_invisible
#define shift_l_char            char_invisible
#define shift_r_char            char_invisible
#define alt_l_char              char_invisible
#define alt_r_char              char_invisible
#define caps_lock_char          char_invisible

/* 转义控制字符通码和断码 */
#define shift_l_make            0x2a
#define shift_r_make            0x36
#define alt_l_make              0x38
#define alt_r_make              0xe038
#define alt_r_break             0xe0b8
#define ctrl_l_make             0x1d
#define ctrl_r_make             0xe01d
#define ctrl_r_break            0xe09d
#define caps_lock_make          0x3a

/* 定义键盘缓冲区 */
struct ioqueue kbd_buf;

/* 以下变量记录对应按键是否被按下，ext_scancode用于记录makecode是否是0xe0开头 */
static int ctrl_status, shift_status, alt_status, caps_lock_status, ext_scancode;

/* 以通码为索引的二维数组 */
static char keymap[][2] = {
/* 扫描码     未与shift组合     与shift组合 */
/********************************************/
/* 0x00 */    {0, 0},
/* 0x01 */    {esc, esc},
/* 0x02 */    {'1', '!'},
/* 0x03 */    {'2', '@'},
/* 0x04 */    {'3', '#'},
/* 0x05 */    {'4', '$'},
/* 0x06 */    {'5', '%'},
/* 0x07 */    {'6', '^'},
/* 0x08 */    {'7', '&'},
/* 0x09 */    {'8', '*'},
/* 0x0a */    {'9', '('},
/* 0x0b */    {'0', ')'},
/* 0x0c */    {'-', '_'},
/* 0x0d */    {'=', '+'},
/* 0x0e */    {backspace, backspace},
/* 0x0f */    {tab, tab},
/* 0x10 */    {'q', 'Q'},
/* 0x11 */    {'w', 'W'},
/* 0x12 */    {'e', 'E'},
/* 0x13 */    {'r', 'R'},
/* 0x14 */    {'t', 'T'},
/* 0x15 */    {'y', 'Y'},
/* 0x16 */    {'u', 'U'},
/* 0x17 */    {'i', 'I'},
/* 0x18 */    {'o', 'O'},
/* 0x19 */    {'p', 'P'},
/* 0x1a */    {'[', '{'},
/* 0x1b */    {']', '}'},
/* 0x1c */    {enter, enter},
/* 0x1d */    {ctrl_l_char, ctrl_l_char},
/* 0x1e */    {'a', 'A'},
/* 0x1f */    {'s', 'S'},
/* 0x20 */    {'d', 'D'},
/* 0x21 */    {'f', 'F'},
/* 0x22 */    {'g', 'G'},
/* 0x23 */    {'h', 'H'},
/* 0x24 */    {'j', 'J'},
/* 0x25 */    {'k', 'K'},
/* 0x26 */    {'l', 'L'},
/* 0x27 */    {';', ':'},
/* 0x28 */    {'\'', '"'},
/* 0x29 */    {'`', '~'},
/* 0x2a */    {shift_l_char, shift_l_char},
/* 0x2b */    {'\\', '|'},
/* 0x2c */    {'z', 'Z'},
/* 0x2d */    {'x', 'X'},
/* 0x2e */    {'c', 'C'},
/* 0x2f */    {'v', 'V'},
/* 0x30 */    {'b', 'B'},
/* 0x31 */    {'n', 'N'},
/* 0x32 */    {'m', 'M'},
/* 0x33 */    {',', '<'},
/* 0x34 */    {'.', '>'},
/* 0x35 */    {'/', '?'},
/* 0x36 */    {shift_r_char, shift_r_char},
/* 0x37 */    {'*', '*'},
/* 0x38 */    {alt_l_char, alt_l_char},
/* 0x39 */    {' ', ' '},
/* 0x3a */    {caps_lock_char, caps_lock_char}
};

/* 键盘中断处理程序 */
static void intr_keyboard_handler(void)
{
    /* 上一次中断以下几个按键是否被按下 */
    int ctrl_down_last = ctrl_status;
    int shift_down_last = shift_status;
    int caps_lock_last = caps_lock_status;

    int break_code;
    uint16_t scancode = inb(KBD_BUF_PORT);

    /* 如果扫描码是0xe0代表此按键产生多个扫描码，
       所以马上结束中断等待下一个扫描码 */
    if (scancode == 0xe0) 
    {
        ext_scancode = 1;
        return;
    }

    /* 如果上次是0xe0，则合并 */
    if (ext_scancode)
    {
        scancode = ((0xe000) | scancode);
        ext_scancode = false;           // 关闭扩展标志
    }

    /* 如果输入的是通码则为0，如果是断码不为0 */
    break_code = ((scancode & 0x0080) != 0);

    if (break_code)     // 如果是断码
    {
        uint16_t make_code = (scancode &= 0xff7f);

        if (make_code == ctrl_l_make || make_code == ctrl_r_make) ctrl_status = 0;
        else if (make_code == shift_l_make || make_code == shift_r_make) shift_status = 0;
        else if (make_code == alt_l_make || make_code == alt_r_make) alt_status = 0;
        
        return;
    }
    /* 处理keymap中定义的键 */
    else if ((scancode > 0x00 && scancode < 0x3b) || (scancode == alt_r_make) || (scancode == ctrl_r_make)) 
    {
        int shift = 0;
        
        if ((scancode < 0x0e) || (scancode == 0x29)  || \
            (scancode == 0x1a) || (scancode == 0x1b) || \
            (scancode == 0x2b) || (scancode == 0x27) || \
            (scancode == 0x28) || (scancode == 0x33) || \
            (scancode == 0x34) || (scancode == 0x35)) 
        {
            // 数字0～9, - = 
            // ` [ ] \ ; ' , . / 
            
            if (shift_down_last) shift = 1;
        }
        else 
        {
            // 默认为字母键
            /*
            if (shift_down_last && caps_lock_last) shift = 0;       // shift和capslock同时按下
            else if (shift_down_last || caps_lock_last) shift = 1;
            else shift = 0;
            */
            // shift = shift_down_last XOR caps_lock_last
            // shift = (shift_down_last || caps_lock_last) && !(shift_down_last && caps_lock_last);
            shift = shift_down_last ^ caps_lock_last;
        }

        uint8_t index = (scancode &= 0x00ff);
        char cur_char = keymap[index][shift]; 

        if (cur_char)
        {
            /* 快捷键处理 */
            if ((ctrl_down_last && cur_char == 'l') || (ctrl_down_last && cur_char == 'u'))
                cur_char -= 'a';
            
            /* 如果输入缓冲区没满则回显字符到屏幕并加入缓冲区 */
            if (!ioq_full(&kbd_buf)) 
            {
                put_char(cur_char);
                ioq_putchar(&kbd_buf, cur_char);
            }           

            return;
        }

        if (scancode == ctrl_l_make || scancode == ctrl_r_make) ctrl_status = 1;
        else if (scancode == shift_l_make || scancode == shift_r_make) shift_status = 1;
        else if (scancode == alt_l_make || scancode == alt_r_make) alt_status = 1;
        else if (scancode == caps_lock_make) caps_lock_status = !caps_lock_status;
    }
    else
    {
        put_str("unknown key\n");
    }
}

void keyboard_init()
{
    put_str("keyboard_init start...\n");
    ioqueue_init(&kbd_buf);
    register_handler(0x21, intr_keyboard_handler);
    put_str("keyboard_init done.\n");
}
