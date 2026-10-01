#pragma once

#include "montabc.h"

#define CFG_DEFAULT_WIDTH 10.0
#define CFG_MIN_WIDTH 3.0
#define CFG_MAX_WIDTH 50.0

typedef struct MonitorCfg
{
    WCHAR device[CCHDEVICENAME]; /* ключ — \\.\DISPLAY1 из MONITORINFOEXW */
    DockEdge edge;
    double widthPct;
    BOOL enabled;
} MonitorCfg;

void Cfg_Load(void);
void Cfg_Save(void);

/* Настройки монитора; отсутствующая запись заводится по образцу
   уже настроенной панели, иначе — из секции [default]. */
MonitorCfg *Cfg_For(const WCHAR *device);
