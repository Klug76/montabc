#pragma once

#include "montabc.h"
#include "config.h"
#include "monitors.h"

typedef struct Panel Panel;

BOOL Panel_Create(HINSTANCE hInst, const DisplayInfo *display, MonitorCfg *cfg, Panel **out);
void Panel_Destroy(Panel *p);
void Panel_Invalidate(Panel *p);

/* Монитор переехал/сменил разрешение. */
void Panel_SetDisplay(Panel *p, const DisplayInfo *display);

/* Имя устройства монитора панели (ключ настроек, \\.\DISPLAY1). */
const WCHAR *Panel_GetDevice(const Panel *p);

/* Рабочая область монитора залезла под панель — shell потерял полосу. */
BOOL Panel_IsWorkAreaBroken(const Panel *p);

/* Перерегистрация appbar'а с нуля: после сна/гибернации, рестарта explorer. */
void Panel_Reregister(Panel *p);

/* Пересогласовать полосу appbar'а и поставить окно. */
void Panel_UpdatePosition(Panel *p);

/* Абсолютный скролл ленты (колесо и драг скроллбара). */
void Panel_SetScrollOffset(Panel *p, int offset);

/* Курсор над панелью (или её скроллбаром) — показать скроллбар. */
void Panel_PointerSeen(Panel *p);
void Panel_PointerMaybeGone(Panel *p);

/* Клик по этой точке панели попадает в крестик закрытия? */
BOOL Panel_IsOverClose(Panel *p, int x, int y);

/* Пересогласовать полосу appbar'а и поставить окно. */
void Panel_UpdatePosition(Panel *p);
