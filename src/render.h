#pragma once

#include "montabc.h"
#include "layout.h"

typedef struct Renderer
{
    /* backbuffer for the current client area size */
    HDC memDc;
    HBITMAP memBmp;
    HGDIOBJ oldBmp;
    int bufW, bufH;

    /* cache for the current DPI */
    UINT dpi;
    HFONT font;
    int iconSize, pad, gripDot, gripGap, header, thinBorder, thickBorder;
    int iconList; /* ImageList of iconSize (Icon_ListFor), per frame */
} Renderer;

/* Paints one panel frame: background, grip, tiles with labels.
   hoverClose is the HWND of the window whose close X is highlighted (or NULL). */
void Rnd_Paint(Renderer *r, HWND hwnd, const Layout *lay, HWND activeWindow,
               UINT dpi, HWND hoverClose);

void Rnd_Destroy(Renderer *r);
