#include "print.h"
#include "init.h"
#include "thread.h"
#include "interrupt.h"
#include "console.h"
#include "process.h"
#include "syscall_init.h"
#include "syscall.h"
#include "stdio.h"
#include "fs.h"
#include "dir.h"

void init(void);

int main(void)
{
    put_str("\nI am kernel\n");
    init_all();

    while (1);
    return 0;
}

void init(void)
{
    uint32_t ret_pid = fork();
    if (ret_pid)
    {
        printf("I am father, my pid is %d, child pid is %d\n", getpid(), ret_pid);
    }
    else
    {
        printf("I am child, my pid is %d, ret pid is %d\n", getpid(), ret_pid);
    }
    while (1);
}

