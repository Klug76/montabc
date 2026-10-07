#include "util.h"
#include "debug.h"

int Ui_Scale(int logicalPx, UINT dpi)
{
    return MulDiv(logicalPx, (int)dpi, 96);
}

int Util_ClampI(int v, int lo, int hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

double Util_ClampD(double v, double lo, double hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

void *Util_Alloc(SIZE_T bytes)
{
    return HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, bytes);
}

void *Util_Realloc(void *p, SIZE_T bytes)
{
    if (!p)
        return HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, bytes);
    return HeapReAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, p, bytes);
}

void Util_Free(void *p)
{
    if (p)
        HeapFree(GetProcessHeap(), 0, p);
}

BOOL Util_Grow(void **buf, int *cap, int need, int max, SIZE_T elemSize,
               const WCHAR *what)
{
    int newCap;
    void *p;

    if (need <= *cap)
        return TRUE;
    if (need > max)
        return FALSE;
    newCap = *cap ? *cap : 16;
    while (newCap < need)
        newCap *= 2;
    if (newCap > max)
        newCap = max;
    if (newCap < need)
        return FALSE;
    p = Util_Realloc(*buf, (SIZE_T)newCap * elemSize);
    if (!p)
        return FALSE;
    *buf = p;
    *cap = newCap;
    (void)what; /* in Release the log is compiled out — the parameter becomes unused */
    LOG(1, L"grow %s: cap=%d", what, *cap);
    return TRUE;
}

double Util_WtoD(const WCHAR *s)
{
    double whole = 0, frac = 0;
    int seenDot = 0, fracDigits = 0;

    for (; *s; s++)
    {
        if (*s >= L'0' && *s <= L'9')
        {
            if (seenDot)
            {
                frac = frac * 10 + (*s - L'0');
                fracDigits++;
            }
            else
                whole = whole * 10 + (*s - L'0');
        }
        else if (*s == L'.' && !seenDot)
            seenDot = 1;
        else
            break;
    }

    while (fracDigits--)
        frac /= 10;
    return whole + frac;
}

void Util_DtoW(WCHAR *buf, double v)
{
    int tenths = (int)(v * 10 + 0.5);
    wsprintfW(buf, L"%d.%d", tenths / 10, tenths % 10);
}
