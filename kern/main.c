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
#include "assert.h"
#include "shell.h"

void init(void);

int main(void)
{
    put_str("\nI am kernel\n");
    init_all();
    intr_enable();

    cls_screen();
    printf("Welcome to x86_system v0.11\n\n");
    console_put_str("[root@localhost /]# ");

    while (1);
    return 0;
}

void init(void)
{
    uint32_t ret_pid = fork();
    if (ret_pid)
    {
        while (1);
    }
    else
    {
        my_shell();
    }
    panic("init: should not be here");
}

