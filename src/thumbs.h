#pragma once

#include "montabc.h"
#include "layout.h"

typedef struct ThumbEntry
{
    HWND source;
    HANDLE thumb;
    BOOL customSource; /* выставлен rcSource (zoom>1) */
    BOOL wanted;       /* нужен в текущем кадре */
} ThumbEntry;

/* Владеет DWM-миниатюрами панели: регистрирует для видимых живых тайлов,
   снимает с полосок и ушедших за viewport (виртуализация). */
typedef struct Thumbs
{
    HWND panel;
    ThumbEntry entries[TRK_MAX_ITEMS];
    int count;
} Thumbs;

void Thumbs_Init(Thumbs *T, HWND panel);

/* Один Sync после каждого пересчёта layout. */
void Thumbs_Sync(Thumbs *T, const Layout *lay, RECT client, HWND activeWindow);

void Thumbs_Dispose(Thumbs *T);
