#pragma once

#include "montabc.h"

typedef struct DisplayInfo
{
    HMONITOR hMon;
    WCHAR device[CCHDEVICENAME];
    RECT rc; /* full monitor rectangle, not the work area */
    BOOL primary;
} DisplayInfo;

/* Fills out with monitors sorted left to right. */
void Monitors_Enum(DisplayInfo *out, int maxCount, int *count);
