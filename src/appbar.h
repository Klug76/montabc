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

/* Согласовывает с shell полосу заданной ширины у края монитора
   и возвращает прямоугольник, в который нужно поставить окно. */
RECT AppBar_SetPos(AppBar *ab, DockEdge edge, RECT monitor, int width);
