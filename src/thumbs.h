#pragma once

#include "montabc.h"
#include "layout.h"

typedef struct ThumbEntry
{
    HWND source;
    HANDLE thumb;
    BOOL customSource; /* rcSource is set (zoom>1) */
    BOOL wanted;       /* needed in the current frame */
} ThumbEntry;

/* Owns the panel's DWM thumbnails: registers them for visible live tiles,
   unregisters for strips and tiles scrolled past the viewport (virtualization). */
typedef struct Thumbs
{
    HWND panel;
    ThumbEntry *entries; /* capacity in cap, grows as needed */
    int count;
    int cap;
} Thumbs;

void Thumbs_Init(Thumbs *T, HWND panel);

/* One Sync after each layout recomputation. */
void Thumbs_Sync(Thumbs *T, const Layout *lay, RECT client, HWND activeWindow);

void Thumbs_Dispose(Thumbs *T);
