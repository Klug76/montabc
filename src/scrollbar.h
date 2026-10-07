#pragma once

#include "montabc.h"
#include "panel.h"

/* Semi-transparent overlay scrollbar for trackpads. A separate layered
   owned-popup window: an owned window is always above its owner in z-order —
   and thus above the DWM thumbnails composed over the panel. The close (✕) zone
   is "perforated" (HTTRANSPARENT) — a click on ✕ passes through the scrollbar into the panel. */
typedef struct ScrollBar
{
    HWND hwnd;
    HWND panelHwnd;
    Panel *panel; /* the Panel struct is opaque to scrollbar.c — access via functions */

    int totalHeight, viewportHeight, scrollOffset;
    int minThumbPx, width, height;
    BOOL visible, dragging;
    int dragAnchor; /* distance from the grab point to the top of the thumb */

    /* cached 32bpp surface for UpdateLayeredWindow */
    HDC dibDc;
    HBITMAP dib;
    HGDIOBJ dibOld;
    DWORD *bits;
    int surfaceW, surfaceH;
} ScrollBar;

void ScrollBar_Create(ScrollBar *sb, Panel *panel, HWND owner, HINSTANCE hInst);
void ScrollBar_Destroy(ScrollBar *sb);

/* Band along the panel's outer edge (screen coordinates), from the "handle" to the bottom. */
void ScrollBar_Layout(ScrollBar *sb, RECT panelScreen, UINT dpi, BOOL dockRight, int topOffset);

/* Synchronization with the filmstrip; called by the panel after layout recomputation. */
void ScrollBar_Update(ScrollBar *sb, int totalHeight, int viewportHeight,
                      int scrollOffset, BOOL pointerNearby);
void ScrollBar_UpdateVisibility(ScrollBar *sb, BOOL pointerNearby);
