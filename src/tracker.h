#pragma once

#include "montabc.h"

/* Safety ceiling: memory for items grows as needed,
   windows above this count are not added to the tracker. */
#define TRK_MAX_ITEMS 4096
#define TRK_TITLE_MAX 512

typedef struct WindowItem
{
    HWND hwnd;
    WCHAR title[TRK_TITLE_MAX];
    /* The icon lives in an ImageList (icons.c); only a slot reference is kept here. */
    int iconSlot;  /* index into the lists, -1 = I_IMAGECALLBACK (not loaded) */
    BYTE iconPrio; /* priority of the displayed icon, lower = better */
    BYTE iconReq;  /* WM_GETICON/fallback cascade already sent */
    HMONITOR monitor;
    BOOL minimized;
    double aspect;   /* client width/height, 16/10 if unknown */
    double zoom;     /* 1..5, visible-area center at centerX/centerY (0..1) */
    double centerX;
    double centerY;
} WindowItem;

extern HWND g_foreground;

/* Callback "list/state changed" — panels redraw. */
extern void (*Trk_OnChanged)(void);
/* One-shot "changed" broadcast (e.g. an icon finished loading asynchronously). */
void Trk_NotifyChanged(void);
/* Callback "foreground window changed" — for the switcher's MRU history. */
extern void (*Trk_OnForeground)(HWND hwnd);

void Trk_Start(void);
void Trk_Stop(void);
void Trk_RefreshMonitors(void);
/* Reset the icon state of all items (rebuild the ImageLists). */
void Trk_ResetIcons(void);

int Trk_Count(void);
WindowItem *Trk_At(int index);
WindowItem *Trk_Find(HWND hwnd);

/* Manual item move (drag-reorder), newIndex clamped 0..count-1. */
void Trk_Move(WindowItem *item, int newIndex);
int Trk_IndexOf(const WindowItem *item);
