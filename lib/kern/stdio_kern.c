#include "global.h"
#include "stdio_kern.h"
#include "print.h"
#include "stdio.h"
#include "console.h"

#define va_start(args, first_fix) args = (va_list)&first_fix;
#define va_end(args) args = NULL

/* 提供给内核使用的格式化输出函数 */
void printk(const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    char buf[1024] = {0, };
    vsprintf(buf, fmt, args);
    va_end(args);
    console_put_str(buf);
}
