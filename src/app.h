#pragma once

#include "montabc.h"
#include "monitors.h"

typedef struct Panel Panel;

void App_Exit(void);

/* Активация окна через MRU-переключатель (с Alt-трюком при foreground lock). */
void App_Activate(HWND hwnd);
void App_ActivateMostRecentExcept(HWND except);

/* Панель бросили на другой монитор: если он свободен — переезд.
   Может уничтожить panel — после вызова к ней не обращаться. */
void App_MovePanel(Panel *panel, HMONITOR target, int cursorX);

/* Пересборка панелей под текущие мониторы и настройки (идемпотентна). */
void App_RefreshDisplays(void);

/* «Скрыть/вернуть всё» из трея; состояние панелей сохраняется. */
void App_ToggleHidden(void);
BOOL App_IsHidden(void);

/* Из меню трея: включённость и край панели конкретного монитора.
   App_SetEnabled может уничтожить панель этого монитора. */
void App_SetEnabled(const WCHAR *device, BOOL enabled);
void App_SetEdge(const WCHAR *device, DockEdge edge);

/* Мониторы системы, слева направо (для меню трея). */
int App_DisplayCount(void);
const DisplayInfo *App_Display(int index);
