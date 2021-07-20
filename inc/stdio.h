#ifndef __LIB_STDIO_H
#define __LIB_STDIO_H

#include "stdint.h"

typedef void* va_list;

uint32_t printf(const char *fmt, ...);
uint32_t sprintf(char *buf, const char *fmt, ...);
uint32_t vsprintf(char *str, const char *fmt, va_list ap);

#endif
