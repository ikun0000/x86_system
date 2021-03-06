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

static void help(void)
{
    printf(" buildin commands:\n");
    printf("        ls: show all directory and file information\n");
    printf("        cd: change current work directory\n");
    printf("        mkdir: create a directory\n");
    printf("        rmdir: remove a empty directory\n");
    printf("        touch: create a file\n");
    printf("        rm: remove a file\n");
    printf("        echo: print message to screen or redirect to file\n");
    printf("        cat: show file content\n");
    printf("        pwd: show current work directory\n");
    printf("        ps: show process and thread information\n");
    printf("        clear: clear screen\n");
    printf("        help: show this message\n");
    printf(" shortcut key:\n");
    printf("        ctrl+l: clear screen\n");
    printf("        ctrl+w: clear input\n");
    
    putchar('\n');
}

/* 执行命令 */
static void cmd_execute(int argc, char *argv[])
{
    if (!strcmp("ls", argv[0])) buildin_ls(argc, argv);
    else if (!strcmp("cd", argv[0]))
    {
        if (buildin_cd(argc, argv) != NULL)
        {
            memset(cwd_cache, 0, MAX_PATH_LEN);
            strcpy(cwd_cache, final_path);
        }
    }
    else if (!strcmp("pwd", argv[0])) buildin_pwd(argc, argv);
    else if (!strcmp("ps", argv[0])) buildin_ps(argc, argv);
    else if (!strcmp("clear", argv[0])) buildin_clear(argc, argv);
    else if (!strcmp("mkdir", argv[0])) buildin_mkdir(argc, argv);
    else if (!strcmp("rmdir", argv[0])) buildin_rmdir(argc, argv);
    else if (!strcmp("touch", argv[0])) buildin_touch(argc, argv);
    else if (!strcmp("rm", argv[0])) buildin_rm(argc, argv);
    else if (!strcmp("cat", argv[0])) buildin_cat(argc, argv);
    else if (!strcmp("echo", argv[0])) buildin_echo(argc, argv);
    else if (!strcmp("help", argv[0])) help();
    else printf("my_shell: command not found: %s\n", argv[0]);
}

/* 简单的shell */
void my_shell(void)
{
    cwd_cache[0] = '/';
    while (1)
    {
        print_prompt();
        memset(final_path, 0, MAX_PATH_LEN);
        memset(cmd_line, 0, cmd_len);
        readline(cmd_line, cmd_len);
        if (cmd_line[0] == 0) continue;

        /* 针对管道处理 */
        char *pipe_symbol = strchr(cmd_line, '|');
        if (pipe_symbol)
        {
            /* 输入命令有管道 */
            /* 生成管道 */
            int32_t fd[2] = {-1, -1};
            pipe(fd);
            /* 让标准输出指向管道的输入 */
            dup2(fd[1], 1);
            
            /* 第一个命令，把|用0截断 */
            char *each_cmd = cmd_line;
            pipe_symbol = strchr(each_cmd, '|');
            *pipe_symbol = 0;

            /* 执行第一个命令，命令的输出会写入到管道中 */
            argc = -1;
            argc = cmd_parse(each_cmd, argv, ' ');
            cmd_execute(argc, argv);

            /* 跨过原来第一个 |  */
            each_cmd = pipe_symbol + 1;
            
            /* 将标准输入指向刚才的管道 */
            dup2(fd[0], 0);
            
            /* 处理中间的命令，他们的输入输出都会在管道中进行 */
            while ((pipe_symbol = strchr(each_cmd, '|')))
            {
                *pipe_symbol = 0;
                argc = -1;
                argc = cmd_parse(each_cmd, argv, ' ');
                cmd_execute(argc, argv);
                each_cmd = pipe_symbol + 1;
            }

            /* 将标准输出恢复到屏幕 */
            dup2(1, 1);
        
            /* 处理最后一个命令 */
            argc = -1;
            argc = cmd_parse(each_cmd, argv, ' ');
            cmd_execute(argc, argv);
            
            /* 恢复标准输入为键盘 */
            dup2(0, 0);
            
            close(fd[0]);
            close(fd[1]);
        }
        else
        {
            /* 输入命令没有管道 */
            argc = -1;
            argc = cmd_parse(cmd_line, argv, ' ');
            if (argc == -1)
            {
                printf("num of arguments exceed %d\n", MAX_ARG_NR);
                continue;
            }
            cmd_execute(argc, argv);
        }

        int32_t arg_idx = 0;
        while (arg_idx < MAX_ARG_NR)
        {
            argv[arg_idx] = NULL;
            arg_idx++;
        } 
    }
    panic("my_shell: should not be here");
}
