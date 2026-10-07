#pragma once

#include "montabc.h"

#ifdef MONTABC_TINY
/* The compiler emits memset/memcpy calls even without the CRT — provide our own. */
void *memset(void *dst, int val, size_t count);
void *memcpy(void *dst, const void *src, size_t count);
#endif

int Ui_Scale(int logicalPx, UINT dpi);
int Util_ClampI(int v, int lo, int hi);
double Util_ClampD(double v, double lo, double hi);

/* Process heap instead of the CRT: blocks are zero-filled; realloc on failure
   returns NULL and does not corrupt the original block. */
void *Util_Alloc(SIZE_T bytes);
void *Util_Realloc(void *p, SIZE_T bytes);
void Util_Free(void *p);

/* Buffer growth by doubling (start at 16) up to the max element ceiling.
   what — name for the growth log. On failure FALSE, *buf and *cap untouched. */
BOOL Util_Grow(void **buf, int *cap, int need, int max, SIZE_T elemSize,
               const WCHAR *what);

/* Parser/formatter for percentages like "10.5" (custom format from settings.ini). */
double Util_WtoD(const WCHAR *s);
void Util_DtoW(WCHAR *buf, double v);
