#include "monitors.h"

typedef struct
{
    DisplayInfo *out;
    int max;
    int count;
} EnumCtx;

static BOOL CALLBACK EnumProc(HMONITOR hMon, HDC hdc, LPRECT clip, LPARAM lp)
{
    EnumCtx *ctx = (EnumCtx *)lp;
    MONITORINFOEXW mi;

    (void)hdc;
    (void)clip;

    if (ctx->count >= ctx->max)
        return FALSE;

    mi.cbSize = sizeof(mi);
    if (GetMonitorInfoW(hMon, (MONITORINFO *)&mi))
    {
        DisplayInfo *d = &ctx->out[ctx->count++];
        d->hMon = hMon;
        lstrcpynW(d->device, mi.szDevice, CCHDEVICENAME);
        d->rc = mi.rcMonitor;
        d->primary = (mi.dwFlags & MONITORINFOF_PRIMARY) != 0;
    }
    return TRUE;
}

void Monitors_Enum(DisplayInfo *out, int maxCount, int *count)
{
    EnumCtx ctx;
    int i, j;

    ctx.out = out;
    ctx.max = maxCount;
    ctx.count = 0;
    EnumDisplayMonitors(NULL, NULL, EnumProc, (LPARAM)&ctx);

    /* слева направо, при равенстве — сверху вниз; вставками: мониторов мало */
    for (i = 1; i < ctx.count; i++)
    {
        DisplayInfo key = out[i];
        for (j = i - 1; j >= 0; j--)
        {
            if (out[j].rc.left < key.rc.left ||
                (out[j].rc.left == key.rc.left && out[j].rc.top <= key.rc.top))
                break;
            out[j + 1] = out[j];
        }
        out[j + 1] = key;
    }

    *count = ctx.count;
}
