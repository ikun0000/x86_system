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
#include "string.h"

/*
void k_thread_a(void *);
void k_thread_b(void *);
void u_prog_a(void);
void u_prog_b(void);
*/

int main(void)
{
    put_char('\n');
    put_str("I am kernel\n");
    init_all();

    intr_enable();

    printf("/dir1/subdir1 create %s!\n", \
    sys_mkdir("/dir1/subdir1") == 0 ? "done" : "fail");

    printf("/dir1 create %s!\n", sys_mkdir("/dir1") == 0 ? "done" : "fail");

    printf("/dir1/subdir1 create %s!\n", \
    sys_mkdir("/dir1/subdir1") == 0 ? "done" : "fail");
    int fd = sys_open("/dir1/subdir1/file2", O_CREAT | O_RDWR);
    if (fd != -1)
    {
        printf("/dir1/subdir1/file2 create done!\n");
        sys_write(fd, "This is file file2\n", 19);
        sys_lseek(fd, 0, SEEK_SET);
        char buf[32] = {0, };
        sys_read(fd, buf, 19);
        printf("/dir1/subdir1/file2: \n%s\n", buf);
        sys_close(fd);
    }
    
/*
    uint32_t fd = sys_open("/file1", O_RDWR);
    printf("open /file1, fd: %d\n", fd);
    char buf[64] = {0, };
    int read_bytes = sys_read(fd, buf, 18);
    printf("1_ read %d bytes:\n%s\n", read_bytes, buf);
    
    memset(buf, 0, 64);
    read_bytes = sys_read(fd, buf, 6);
    printf("2_ read %d bytes:\n%s\n", read_bytes, buf);
    
    memset(buf, 0, 64);
    read_bytes = sys_read(fd, buf, 6);
    printf("3_ read %d bytes:\n%s\n", read_bytes, buf);
    
    printf("_______ close file1 and reopen _______\n");
    sys_lseek(fd, 0, SEEK_SET);
    memset(buf, 0, 64);
    read_bytes = sys_read(fd, buf, 24);
    printf("4_ read %d bytes:\n%s\n", read_bytes, buf);
    
    sys_close(fd);
*/

//    process_execute(u_prog_a, "user_prog_a");    
//    process_execute(u_prog_b, "user_prog_b");
//    thread_start("k_thread_a", 31, k_thread_a, "I am thread_a");
//    thread_start("k_thread_b", 31, k_thread_b, "I am thread_b");
    while (1) thread_yield();
    return 0;
}

/*
void k_thread_a(void *arg)
{
    void *addr = sys_malloc(33);
    console_put_str(" I am thread_a, sys_malloc(33), addr is 0x");
    console_put_int((int)addr);
    console_put_char('\n');
    sys_free(addr);
    console_put_str(" thread_a sys_free\n");
    while (1) asm volatile ("hlt");
}

void k_thread_b(void *arg)
{
    void *addr = sys_malloc(2048);
    console_put_str(" I am thread_b, sys_malloc(33), addr is 0x");
    console_put_int((int)addr);
    console_put_char('\n');
    sys_free(addr);
    console_put_str(" thread_b sys_free\n");
    while (1) asm volatile ("hlt");
}
*/

/*
void k_thread_a(void *arg)
{
    void *addr1 = sys_malloc(256);
    void *addr2 = sys_malloc(255);
    void *addr3 = sys_malloc(254);
    console_put_str(" thread_a malloc addr: 0x");
    console_put_int((int)addr1);
    console_put_char(',');
    console_put_int((int)addr2);
    console_put_char(',');
    console_put_int((int)addr3);
    console_put_char('\n');
    int cpu_delay = 100000;
    while (cpu_delay-- > 0) ;
    sys_free(addr1);
    sys_free(addr2);
    sys_free(addr3);
    console_put_str(" thread_a free all memory\n");
    while(1);
}

void k_thread_b(void *arg)
{
    void *addr1 = sys_malloc(256);
    void *addr2 = sys_malloc(255);
    void *addr3 = sys_malloc(254);
    console_put_str(" thread_b malloc addr: 0x");
    console_put_int((int)addr1);
    console_put_char(',');
    console_put_int((int)addr2);
    console_put_char(',');
    console_put_int((int)addr3);
    console_put_char('\n');
    int cpu_delay = 100000;
    while (cpu_delay-- > 0) ;
    sys_free(addr1);
    sys_free(addr2);
    sys_free(addr3);
    console_put_str(" thread_b free all memory\n");
    while (1);
}

void u_prog_a(void)
{
    void *addr1 = malloc(256);
    void *addr2 = malloc(255);
    void *addr3 = malloc(254);
    printf(" prog_a malloc addr: 0x%x, 0x%x, 0x%x\n", (int)addr1, (int)addr2, (int)addr3);
    int cpu_delay = 100000;
    while (cpu_delay-- > 0);
    free(addr1);
    free(addr2);
    free(addr3);
    printf(" prog_a free all memroy\n");
    while(1);
}

void u_prog_b(void)
{
    void *addr1 = malloc(256);
    void *addr2 = malloc(255);
    void *addr3 = malloc(254);
    printf(" prog_b malloc addr: 0x%x, 0x%x, 0x%x\n", (int)addr1, (int)addr2, (int)addr3);
    int cpu_delay = 100000;
    while (cpu_delay-- > 0);
    free(addr1);
    free(addr2);
    free(addr3);
    printf(" prog_b free all memroy\n");
    while (1);
}
*/
