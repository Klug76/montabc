#pragma once

#include "montabc.h"

#define TRK_MAX_ITEMS 512
#define TRK_TITLE_MAX 512

typedef struct WindowItem
{
    HWND hwnd;
    WCHAR title[TRK_TITLE_MAX];
    HICON icon;
    BOOL ownsIcon;
    HMONITOR monitor;
    BOOL minimized;
    double aspect;   /* ширина/высота клиента, 16/10 если неизвестна */
    double zoom;     /* 1..5, центр видимой области centerX/centerY (0..1) */
    double centerX;
    double centerY;
} WindowItem;

extern HWND g_foreground;

/* Колбэк «список/состояние изменились» — панели перерисовываются. */
extern void (*Trk_OnChanged)(void);
/* Колбэк «сменилось foreground-окно» — для MRU-истории переключателя. */
extern void (*Trk_OnForeground)(HWND hwnd);

void Trk_Start(void);
void Trk_Stop(void);
void Trk_RefreshMonitors(void);

int Trk_Count(void);
WindowItem *Trk_At(int index);
WindowItem *Trk_Find(HWND hwnd);

/* Ручное перемещение элемента (drag-reorder), newIndex clamp 0..count-1. */
void Trk_Move(WindowItem *item, int newIndex);
int Trk_IndexOf(const WindowItem *item);
