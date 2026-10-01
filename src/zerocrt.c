#include "util.h"

#ifdef MONTABC_TINY
/* Отменяем intrinsic-статус, чтобы определить функции самостоятельно. */
#pragma function(memset, memcpy)

/* Требуется компоновщику при использовании floating point без CRT. */
int _fltused = 0;

void *memset(void *dst, int val, size_t count)
{
    unsigned char *p = (unsigned char *)dst;
    while (count--)
        *p++ = (unsigned char)val;
    return dst;
}

void *memcpy(void *dst, const void *src, size_t count)
{
    unsigned char *d = (unsigned char *)dst;
    const unsigned char *s = (const unsigned char *)src;
    while (count--)
        *d++ = *s++;
    return dst;
}
#endif
