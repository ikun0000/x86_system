#include "stdio.h"
#include "interrupt.h"
#include "global.h"
#include "string.h"
#include "syscall.h"
#include "print.h"

/* 把ap指向最后一个固定参数 */
#define va_start(ap, v)     ap = (va_list)&v
/* ap指向下一个参数并返回其值 */
#define va_arg(ap, t)       *((t*)(ap += 4))
/* 清空ap */
#define va_end(ap)          ap = NULL

/* 把整数转化为字符串 */
static void itoa(uint32_t value, char **buf_ptr_addr, uint8_t base)
{
    uint32_t m = value % base;      // 求模，最先掉下来的是最低位
    uint32_t i = value / base;      // 取整
    // 商不为0继续调用
    if (i) itoa(i, buf_ptr_addr, base);
    // 使用递归最先写入低地址的是最高位
    if (m < 10) *((*buf_ptr_addr)++) = m + '0';     // 余数落入0～9
    else *((*buf_ptr_addr)++) = m - 10 + 'A';       // 余数落入A～F
}

/* 格式化打印字符串 */
uint32_t printf(const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    char buf[1024] = {0, };
    vsprintf(buf, fmt, args);
    va_end(args);
    return write(1, buf, strlen(buf));
}

/* 格式化输出到字符串buf中 */
uint32_t sprintf(char *buf, const char *fmt, ...)
{
    va_list args;
    uint32_t retval;
    va_start(args, fmt);
    retval = vsprintf(buf, fmt, args);
    va_end(args);
    return retval;
}

/* 将参数ap安装fmt格式输出到str中并返回str的长度 */
uint32_t vsprintf(char *str, const char *fmt, va_list ap)
{
    char *buf_ptr = str;
    const char *index_ptr = fmt;
    char index_char = *index_ptr;
    int32_t arg_int;
    char *arg_str;
    
    while (index_char)
    {
        if (index_char != '%')
        {
            *(buf_ptr++) = index_char;
            index_char = *(++index_ptr);
            continue;
        }    
        index_char = *(++index_ptr);    // 获取%后的字符
        switch (index_char)
        {
            case 's':
                arg_str = va_arg(ap, char *);
                strcpy(buf_ptr, arg_str);
                buf_ptr += strlen(arg_str);
                index_char = *(++index_ptr);
                break;
            
            case 'c':
                *(buf_ptr++) = va_arg(ap, char);
                index_char = *(++index_ptr);
                break;

            case 'd':
                arg_int = va_arg(ap, int);
                /* 如果是负数则转化为正数在再前面加上‘-’ */
                if (arg_int < 0)
                {
                    arg_int = 0 - arg_int;
                    *(buf_ptr++) = '-';
                }
                itoa(arg_int, &buf_ptr, 10);
                index_char = *(++index_ptr);
                break;
            
            case 'x':
                arg_int = va_arg(ap, int);
                itoa(arg_int, &buf_ptr, 16);
                index_char = *(++index_ptr);
                break;
        }
    }

    return strlen(str);
}
