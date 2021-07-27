#include "syscall.h"
#include "stdio.h"
#include "string.h"
#include "fs.h"
#include "global.h"
#include "dir.h"
#include "shell.h"
#include "assert.h"

/* 将路径old_abs_path中的.和..转化为绝对路径存入new_abs_path */
static void wash_path(char *old_abs_path, char *new_abs_path)
{
    assert(old_abs_path[0] == '/');
    char name[MAX_FILE_NAME_LEN] = {0, };
    char *sub_path = old_abs_path;
    sub_path = path_parse(sub_path, name);
    if (name[0] == 0)
    {
        /* 只输入了 / */
        new_abs_path[0] = '/';
        new_abs_path[1] = 0;
        return;
    }
    new_abs_path[0] = 0;
    strcat(new_abs_path, "/");
    while (name[0])
    {
        if (!strcmp("..", name))
        {
            /* 如果是上一级目录 */
            char *slash_ptr = strrchr(new_abs_path, '/');
            
            /* 如果没达到顶层目录将右边的 / 替换为0，这样截断字符串相当于截断最后的目录 */
            if (slash_ptr != new_abs_path) *slash_ptr = 0;
            /* 如果达到顶层目录直接返回根目录 */
            else *(slash_ptr + 1) = 0;
        }
        else if (strcmp(".", name))
        {
            /* 如果路径不是 . 则拼接新路径 */

            /* 第一次拼接不会执行 */
            if (strcmp(new_abs_path, "/")) strcat(new_abs_path, "/");
            strcat(new_abs_path, name);
        }
        /* 如果是 . 的话不需要处理 */

        memset(name, 0, MAX_FILE_NAME_LEN);       
        if (sub_path) sub_path = path_parse(sub_path, name);
    }

}

/* 将path处理成不含..和.的绝对路径存放到wash_buf中 */
void make_clear_abs_path(char *path, char *wash_buf)
{
    char abs_path[MAX_PATH_LEN] = {0, };
    if (path[0] != '/')
    {
        memset(abs_path, 0, MAX_PATH_LEN);
        if (getcwd(abs_path, MAX_PATH_LEN) != NULL)
        {
            /* 如果abs_path表示当前目录不是根目录 */
            if (!((abs_path[0] == '/') && (abs_path[1] == 0))) strcat(abs_path, "/");
        }
    }

    strcat(abs_path, path);
    wash_path(abs_path, wash_buf);
}
