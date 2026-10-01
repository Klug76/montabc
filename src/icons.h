#pragma once

#include "montabc.h"

/* Иконка окна: WM_GETICON (с таймаутом — окно может висеть) → иконка класса →
   иконка exe процесса. owned=TRUE только для извлечённой из exe (её надо
   DestroyIcon). Возвращает NULL, если иконки нет вовсе. */
HICON Icon_GetWindow(HWND hwnd, BOOL *owned);
