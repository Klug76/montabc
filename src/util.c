#include "util.h"

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
