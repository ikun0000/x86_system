#include "shell.h"
#include "stdint.h"
#include "fs.h"
#include "file.h"
#include "syscall.h"
#include "stdio.h"
#include "global.h"
#include "assert.h"
#include "string.h"
#include "buildin_cmd.h"

#define cmd_len     128     // 最大输入128字符命令
#define MAX_ARG_NR  16      // 加上命令最多支持15个参数

/* 输入命令的缓冲区 */
static char cmd_line[cmd_len] = {0, };

/* 清洗路径时缓冲 */
char final_path[MAX_PATH_LEN] = {0, };

/* 用于记录当前目录 */
char cwd_cache[64] = {0, };

char *argv[MAX_ARG_NR];
int32_t argc = -1;

/* 输出提示符 */
void print_prompt(void)
{
    printf("[root@localhost %s]# ", cwd_cache);
}

/* 从键盘缓冲区中最多读入count个字节到buf */
static void readline(char *buf, int32_t count)
{
    assert(buf != NULL && count > 0);
    char *pos = buf;

    while (read(stdin_no, pos, 1) != -1 && (pos - buf) < count)
    {
        /* 在不出错的情况下找到回车后才返回 */
        switch (*pos)
        {
            case '\n':
            case '\r':
                *pos = 0;
                putchar('\n');
                return;

            case '\b':
                /* 阻止删除非本次输入的信息 */
                if (buf[0] != '\b')
                {               
                    --pos;
                    putchar('\b');
                }
                break;
            
            /* ctrl + l 清屏 */
            case 'l' - 'a':
                /* 将当前字符 'l' - 'a' 修改为0 */
                *pos = 0;
                /* 清除屏幕 */
                clear();
                /* 打印提示符 */
                print_prompt();
                /* 打印之前输入的命令 */
                printf("%s", buf);
                break;
                   
            /* ctrl + u 清除输入 */
            case 'u' - 'a':
                while (buf != pos)
                {
                    putchar('\b');
                    *(pos--) = 0;
                }
                break;

            default:
                putchar(*pos);
                pos++;
        }
    }
    printf("readline: can't find enter_key in the cmd_line, max num of char is 128\n");
}

/* 分析字符串cmd_str中的token为分隔符的单词，将各单词的指针存入argv数组 */
static int32_t cmd_parse(char *cmd_str, char **argv, char token)
{
    assert(cmd_str != NULL);
    uint32_t arg_idx = 0;
    
    while (arg_idx < MAX_ARG_NR)
    {
        argv[arg_idx] = NULL;
        arg_idx++;
    }
    
    char *next = cmd_str;
    int32_t argc = 0;
    
    /* 外层循环处理整个命令行 */
    while (*next)
    {
        /* 去除中间的空格 */
        while (*next == token) next++;

        /* 最后一个参数后面有空格的情况 */
        if (*next == 0) break;

        argv[argc] = next;

        /* 内层循环处理命令行中间的参数 */
        while (*next && *next != token) next++;
        
        /* 如果没结束，是token字符，则改成0截断字符串 */
        if (*next) *next++ = 0;

        /* 避免argv越界 */
        if (argc > MAX_ARG_NR) return -1;
        argc++;
    }
    
    return argc;
}

/* 简单的shell */
void my_shell(void)
{
    cwd_cache[0] = '/';
    while (1)
    {
        print_prompt();
        memset(cmd_line, 0, cmd_len);
        readline(cmd_line, cmd_len);
        if (cmd_line[0] == 0) continue;
        argc = -1;
        argc = cmd_parse(cmd_line, argv, ' ');
        if (argc == -1)
        {
            printf("num of arguments exceed %d\n", MAX_ARG_NR);
            continue;
        }

        char buf[MAX_PATH_LEN] = {0};
        int32_t arg_idx = 0;
        while (arg_idx < argc)
        {
            make_clear_abs_path(argv[arg_idx], buf);
            printf("%s -> %s\n", argv[arg_idx], buf);
            arg_idx++;
        }
    }
    panic("my_shell: should not be here");
}
