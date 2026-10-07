#pragma once

#include "montabc.h"
#include "config.h"
#include "monitors.h"

typedef struct Panel Panel;

BOOL Panel_Create(HINSTANCE hInst, const DisplayInfo *display, MonitorCfg *cfg, Panel **out);
void Panel_Destroy(Panel *p);
void Panel_Invalidate(Panel *p);

/* The monitor moved/changed resolution. */
void Panel_SetDisplay(Panel *p, const DisplayInfo *display);

/* Panel monitor device name (settings key, \\.\DISPLAY1). */
const WCHAR *Panel_GetDevice(const Panel *p);

/* The monitor's work area slid under the panel — the shell lost the band. */
BOOL Panel_IsWorkAreaBroken(const Panel *p);

/* Re-register the appbar from scratch: after sleep/hibernation, explorer restart. */
void Panel_Reregister(Panel *p);

/* Renegotiate the appbar band and position the window. */
void Panel_UpdatePosition(Panel *p);

/* Absolute ribbon scroll (wheel and scrollbar drag). */
void Panel_SetScrollOffset(Panel *p, int offset);

/* Cursor over the panel (or its scrollbar) — show the scrollbar. */
void Panel_PointerSeen(Panel *p);
void Panel_PointerMaybeGone(Panel *p);

/* Does a click at this point of the panel hit the close X? */
BOOL Panel_IsOverClose(Panel *p, int x, int y);

/* Renegotiate the appbar band and position the window. */
void Panel_UpdatePosition(Panel *p);
