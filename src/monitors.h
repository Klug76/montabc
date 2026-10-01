#pragma once

#include "montabc.h"

typedef struct DisplayInfo
{
    HMONITOR hMon;
    WCHAR device[CCHDEVICENAME];
    RECT rc; /* полный прямоугольник монитора, не рабочая область */
    BOOL primary;
} DisplayInfo;

/* Заполняет out мониторами, отсортированными слева направо. */
void Monitors_Enum(DisplayInfo *out, int maxCount, int *count);
