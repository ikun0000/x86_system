#ifndef __USERPROC_EXEC_H
#define __USERPROC_EXEC_H

#include "stdint.h"

int32_t sys_execv(const char *path, const char *argv[]);

#endif
