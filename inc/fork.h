#ifndef __USERPROC_FORK_H
#define __USERPROC_FORK_H

#include "thread.h"

/* fork子进程，只能由用户进程通过系统调用fork，
   内核线程不可直接调用，原因是要从0特权级栈中获得esp3等 */
pid_t sys_fork(void);

#endif
