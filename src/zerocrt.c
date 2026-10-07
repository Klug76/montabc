#include "util.h"

#ifdef MONTABC_TINY
/* Remove intrinsic status so we can define the functions ourselves. */
#pragma function(memset, memcpy)

/* Required by the linker when floating point is used without the CRT. */
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
