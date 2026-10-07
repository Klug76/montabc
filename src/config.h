#pragma once

#include "montabc.h"

#define CFG_DEFAULT_WIDTH 10.0
#define CFG_MIN_WIDTH 3.0
#define CFG_MAX_WIDTH 50.0

typedef struct MonitorCfg
{
    WCHAR device[CCHDEVICENAME]; /* key — \\.\DISPLAY1 from MONITORINFOEXW */
    DockEdge edge;
    double widthPct;
    BOOL enabled;
} MonitorCfg;

void Cfg_Load(void);
void Cfg_Save(void);

/* Monitor settings; a missing entry is created by copying an
   already configured panel, otherwise from the [default] section. */
MonitorCfg *Cfg_For(const WCHAR *device);
