#include "buildin_cmd.h"
#include "syscall.h"
#include "stdio.h"
#include "string.h"
#include "fs.h"
#include "global.h"
#include "dir.h"
#include "shell.h"
#include "assert.h"

/* ls命令内建函数 */
void buildin_ls(uint32_t argc, char **argv)
{
    char *pathname = NULL;
    struct stat file_stat;
    memset(&file_stat, 0, sizeof(struct stat));
    int long_info = 0;
    uint32_t arg_path_nr = 0;
    uint32_t arg_idx = 1;
    
    while (arg_idx < argc)
    {
        if (argv[arg_idx][0] == '-')
        {
            /* 如果有选项 */
            if (!strcmp("-l", argv[arg_idx]))
            {
                long_info = 1;
            }
            else if (!strcmp("-h", argv[arg_idx]))
            {
                printf("usage: -l list all infomation about the file.\n-h for help\nlist all files in the current directory if no option\n");
                return;
            }
            else
            {
                printf("ls: invalid option %s\nTry `ls -h` for more information.\n", argv[arg_idx]);
                return;
            }
        }
        else
        {
            if (arg_path_nr == 0)
            {
                pathname = argv[arg_idx];
                arg_path_nr = 1;
            }
            else
            {
                printf("ls: only support one path\n");
                return;
            }
        }

        arg_idx++;
    }

    if (pathname == NULL)
    {
        if (NULL != getcwd(final_path, MAX_PATH_LEN))
        {
            pathname = final_path;
        }
        else
        {
            printf("ls: getcwd for default path failed\n");
            return;
        }
    }
    else
    {
        make_clear_abs_path(pathname, final_path);
        pathname = final_path;
    }

    if (stat(pathname, &file_stat) == -1)
    {
        printf("ls: cannot access %s: No such file or directory\n", pathname);
        return;
    }

    if (file_stat.st_filetype == FT_DIRECTORY)
    {
        struct dir *dir = opendir(pathname);
        struct dir_entry *dir_e = NULL;
        char sub_pathname[MAX_PATH_LEN] = {0, };
        uint32_t pathname_len = strlen(pathname);
        uint32_t last_char_idx = pathname_len - 1;
        memcpy(sub_pathname, pathname, pathname_len);
        
        if (sub_pathname[last_char_idx] != '/')
        {
            sub_pathname[pathname_len] = '/';
            pathname_len++;
        }
        rewinddir(dir);
        if (long_info)
        {
            char ftype;
            printf("total: %d\n", file_stat.st_size);

            while ((dir_e = readdir(dir)))
            {
                ftype = 'd';
                if (dir_e->f_type == FT_REGULAR) ftype = '-';
                sub_pathname[pathname_len] = 0;
                strcat(sub_pathname, dir_e->filename);
                memset(&file_stat, 0, sizeof(struct stat));
                
                if (stat(sub_pathname, &file_stat) == -1)
                {
                    printf("ls: cannot access %s: No such file or directory\n", dir_e->filename);
                    return;
                }
                printf("%c  %d  %d  %s\n", ftype, dir_e->i_no, file_stat.st_size, dir_e->filename);
            }
        }
        else
        {
            while ((dir_e = readdir(dir)))
            {
                printf("%s ", dir_e->filename);
            }
            printf("\n");
        }

        closedir(dir);
    }
    else
    {
        if (long_info)
        {
            printf("-  %d  %d  %s\n", file_stat.st_ino, file_stat.st_size, pathname);
        }
        else
        {
            printf("%s\n", pathname);
        }
    }
}

/* cd命令内建函数 */
char *buildin_cd(uint32_t argc, char **argv)
{
    if (argc > 2)
    {
        printf("cd: only support 1 argument!\n");
        return NULL;
    }   

    if (argc == 1)
    {
        final_path[0] = '/';
        final_path[1] = 0;
    }
    else
    {
        make_clear_abs_path(argv[1], final_path);
    }

    if (chdir(final_path) == -1)
    {
        printf("cd: no such directory %s\n", final_path);
        return NULL;
    }
    
    return final_path;
}

/* mkdir命令内建函数 */
int32_t buildin_mkdir(uint32_t argc, char **argv)
{
    int32_t ret = -1;
    
    if (argc != 2)
    {
        printf("mkdir: only support 1 argument!\n");
    }
    else
    {
        make_clear_abs_path(argv[1], final_path);
        if (strcmp("/", final_path))
        {
            if (mkdir(final_path) == 0) ret = 0;
            else printf("mkdir: create directory %s failed.\n", argv[1]);
        }
    }

    return ret;
}

/* rmdir内建命令函数 */
int32_t buildin_rmdir(uint32_t argc, char **argv)
{
    int32_t ret = -1;

    if (argc != 2)
    {
        printf("rmdir: only support 1 argument!\n");
    }
    else
    {
        make_clear_abs_path(argv[1], final_path);
        if (strcmp("/", final_path))
        {
            if (rmdir(final_path) == 0) ret = 0;
            else printf("rmdir: remove %s failed.\n", argv[1]);
        }
    }

    return ret;
}

/* touch命令内建函数 */
int32_t buildin_touch(uint32_t argc, char **argv)
{
    int32_t ret = -1;
    
    if (argc != 2)
    {
        printf("touch: only support 1 argument!\n");
    }
    else
    {
        make_clear_abs_path(argv[1], final_path);
        if (final_path[strlen(final_path) - 1] == '/')
        {
            printf("touch: cannot create directory %s\n", final_path);
        }
        else
        {
            int fd = open(final_path, O_CREAT);
            if (fd > 2) { close(fd); ret = 0; }
            else printf("touch: cannot create file %s\n", final_path);
        }
    }

    return ret;
}

/* rm命令内建函数 */
int32_t buildin_rm(uint32_t argc, char **argv)
{
    int32_t ret = -1;

    if (argc != 2)
    {
        printf("rm: only support 1 argument!\n");
    }
    else
    {
        make_clear_abs_path(argv[1], final_path);
        if (strcmp("/", final_path))
        {
            if (unlink(final_path) == 0) ret = 0;
            else printf("rm: delete %s failed.\n", argv[1]);
        }
    }

    return ret;
}

/* pwd命令内建函数 */
void buildin_pwd(uint32_t argc, char **argv)
{
    if (argc != 1)
    {
        printf("pwd: no argument support!\n");
        return;
    }
    else
    {
        if (NULL != getcwd(final_path, MAX_PATH_LEN))
        {
            printf("%s\n", final_path);
        }
        else
        {
            printf("pwd: get current work directory failed.\n");
        }
    }
}

/* ps命令内建函数 */
void buildin_ps(uint32_t argc, char **argv)
{
    if (argc != 1)
    {
        printf("ps: no argument support!\n");
        return;
    }
    ps();
}

/* clear命令内建函数 */
void buildin_clear(uint32_t argc, char **argv)
{
    if (argc != 1)
    {
        printf("clear: no argument support!\n");
        return;
    }   

    clear();
}

/* cat命令内建函数 */
void buildin_cat(uint32_t argc, char **argv)
{
    if (argc > 2)
    {
        printf("cat: argument error\n");
        return;
    }
    
    if (argc == 1)
    {
        char buf[512] = {0, };
        read(0, buf, 512); 
        printf("%s", buf);
        return;
    }

    int buf_size = 1024;
    char abs_path[MAX_PATH_LEN] = {0, };
    void *buf = malloc(buf_size);
    if (buf == NULL) 
    {
        printf("cat: malloc memory failed\n");
        return;
    }

    if (argv[1][0] != '/')
    {
        getcwd(abs_path, MAX_PATH_LEN);
        strcat(abs_path, "/");
        strcat(abs_path, argv[1]);
    }
    else
    {
        strcpy(abs_path, argv[1]);
    }

    int fd = open(abs_path, O_RDONLY);
    if (fd == -1)
    {
        printf("cat: open: open %s failed\n", argv[1]);
        return;
    }

    int read_bytes = 0;
    while (1)
    {
        read_bytes = read(fd, buf, buf_size);
        if (read_bytes == -1) break;
        write(1, buf, read_bytes);
    }
    
    free(buf);
    close(fd);
}

/* echo命令内建函数 */
void buildin_echo(uint32_t argc, char **argv)
{
    if (!(argc == 2 || argc == 4))
    {
        printf("usage: `echo <content> [>|>> filename] `\n");
        return;
    }

    if (argc == 2)
    {
        printf("%s\n", argv[1]);
    }
    else
    {
        char abs_path[MAX_PATH_LEN] = {0, };
        if (argv[3][0] != '/') 
        {
            getcwd(abs_path, MAX_PATH_LEN);
            strcat(abs_path, "/");
            strcat(abs_path, argv[3]);
        }
        else
        {
            strcpy(abs_path, argv[3]);
        }

        int fd = open(abs_path, O_WRONLY);
        if (fd == -1)
        {
            printf("echo: open: open %s failed\n", argv[3]);
            return;
        }

        if (!strcmp(">", argv[2]))
        {
            /* 没有编写截断文件长度的系统调用，干脆直接删了再建一个同名的文件 */
            close(fd);
            unlink(abs_path);
            fd = open(abs_path, O_CREAT | O_WRONLY);
            if (fd == -1)
            {
                printf("echo: open: open %s failed\n", argv[3]);
                return;
            }
            write(fd, argv[1], strlen(argv[1]));
        }
        else if (!strcmp(">>", argv[2]))
        {
            lseek(fd, -1, SEEK_END);
            write(fd, argv[1], strlen(argv[1]));
        }
        else
        {
            printf("echo: relocation failed!\n");
        }
        close(fd);
    }
}

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
