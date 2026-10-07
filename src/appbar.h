#pragma once

#include "montabc.h"

typedef struct AppBar
{
    HWND hwnd;
    UINT callbackMsg;
    BOOL registered;
} AppBar;

void AppBar_Init(AppBar *ab, HWND hwnd, UINT callbackMsg);
void AppBar_Register(AppBar *ab);
void AppBar_Unregister(AppBar *ab);

/* Negotiates with the shell a strip of the given width at the monitor edge
   and returns the rectangle where the window should be placed. */
RECT AppBar_SetPos(AppBar *ab, DockEdge edge, RECT monitor, int width);
