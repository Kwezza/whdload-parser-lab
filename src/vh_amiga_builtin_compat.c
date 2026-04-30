#include <stdarg.h>
#include <stdio.h>
#include <string.h>

size_t __builtin_strlen(const char *s)
{
    return strlen(s);
}

void *__builtin_memcpy(void *dst, const void *src, size_t n)
{
    return memcpy(dst, src, n);
}

void *__builtin_memset(void *dst, int c, size_t n)
{
    return memset(dst, c, n);
}

int __builtin_strcmp(const char *a, const char *b)
{
    return strcmp(a, b);
}

int __builtin_printf(const char *fmt, ...)
{
    va_list args;
    int rc;

    va_start(args, fmt);
    rc = vprintf(fmt, args);
    va_end(args);
    return rc;
}