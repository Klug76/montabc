#pragma once

#include "montabc.h"
#include "layout.h"

typedef struct Renderer
{
    /* backbuffer на текущий размер клиентской области */
    HDC memDc;
    HBITMAP memBmp;
    HGDIOBJ oldBmp;
    int bufW, bufH;

    /* кеш на текущий DPI */
    UINT dpi;
    HFONT font;
    int iconSize, pad, gripDot, gripGap, header, thinBorder, thickBorder;
} Renderer;

/* Отрисовка кадра панели: фон, гриппер, тайлы с подписями.
   hoverClose — HWND окна, чей крестик закрытия подсвечен (или NULL). */
void Rnd_Paint(Renderer *r, HWND hwnd, const Layout *lay, HWND activeWindow,
               UINT dpi, HWND hoverClose);

void Rnd_Destroy(Renderer *r);
