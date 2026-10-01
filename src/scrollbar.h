#pragma once

#include "montabc.h"
#include "panel.h"

/* Полупрозрачный оверлей-скроллбар для трекпадов. Отдельное layered
   owned-popup-окно: owned-окно в z-order всегда выше владельца — а значит,
   и выше DWM-превью, компонуемых поверх панели. Зона крестиков «дырявая»
   (HTTRANSPARENT) — клик по ✕ проходит сквозь скроллбар в панель. */
typedef struct ScrollBar
{
    HWND hwnd;
    HWND panelHwnd;
    Panel *panel; /* панель Panel непрозрачна для scrollbar.c — API через функции */

    int totalHeight, viewportHeight, scrollOffset;
    int minThumbPx, width, height;
    BOOL visible, dragging;
    int dragAnchor; /* расстояние от точки захвата до верха бегунка */

    /* кешированная 32bpp-поверхность для UpdateLayeredWindow */
    HDC dibDc;
    HBITMAP dib;
    HGDIOBJ dibOld;
    DWORD *bits;
    int surfaceW, surfaceH;
} ScrollBar;

void ScrollBar_Create(ScrollBar *sb, Panel *panel, HWND owner, HINSTANCE hInst);
void ScrollBar_Destroy(ScrollBar *sb);

/* Полоса у внешнего края панели (экранные координаты), от «ручки» до низа. */
void ScrollBar_Layout(ScrollBar *sb, RECT panelScreen, UINT dpi, BOOL dockRight, int topOffset);

/* Синхронизация с лентой; вызывается панелью после пересчёта раскладки. */
void ScrollBar_Update(ScrollBar *sb, int totalHeight, int viewportHeight,
                      int scrollOffset, BOOL pointerNearby);
void ScrollBar_UpdateVisibility(ScrollBar *sb, BOOL pointerNearby);
