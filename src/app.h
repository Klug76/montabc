#pragma once

#include "montabc.h"
#include "monitors.h"

typedef struct Panel Panel;

void App_Exit(void);

/* Activates a window via the MRU switcher (with the Alt trick under foreground lock). */
void App_Activate(HWND hwnd);
void App_ActivateMostRecentExcept(HWND except);

/* A panel was dropped onto another monitor: if it is free — move there.
   May destroy panel — do not use it after the call. */
void App_MovePanel(Panel *panel, HMONITOR target, int cursorX);

/* Rebuilds panels to match the current monitors and settings (idempotent). */
void App_RefreshDisplays(void);

/* Restores appbar strips lost by the shell: after sleep/hibernation
   and explorer restart (events are caught by Host_WndProc in main.c). */
void App_ReregisterAppBars(void);
BOOL App_HealAppBars(void);

/* Deferred (timer-debounced) rebuild of icon ImageLists: system
   events (WM_SETTINGCHANGE with a change of SM_CXSMICON/SM_CYSMICON,
   WM_THEMECHANGED, WM_DPICHANGED) make the icons stale. */
void App_ScheduleIconRebuild(void);

/* "Hide/restore all" from the tray; panel state is preserved. */
void App_ToggleHidden(void);
BOOL App_IsHidden(void);

/* From the tray menu: enabled state and edge of a specific monitor's panel.
   App_SetEnabled may destroy that monitor's panel. */
void App_SetEnabled(const WCHAR *device, BOOL enabled);
void App_SetEdge(const WCHAR *device, DockEdge edge);

/* System monitors, left to right (for the tray menu). */
int App_DisplayCount(void);
const DisplayInfo *App_Display(int index);
