#pragma once

#include "montabc.h"
#include "tracker.h"

#define LAY_HEADER_LOGICAL 14 /* «ручка» перетаскивания панели сверху */

typedef struct LayoutItem
{
    WindowItem *win;
    RECT bounds;  /* весь тайл: подпись + превью */
    RECT preview; /* зона превью (только для живых) */
    RECT label;   /* полоса подписи: иконка + заголовок */
    BOOL isStrip; /* полоска свёрнутого окна */
} LayoutItem;

typedef struct Layout
{
    LayoutItem items[TRK_MAX_ITEMS];
    int count;
    int totalHeight;

    /* предвычисленные размеры на текущий DPI */
    UINT dpi;
    int header, strip, label, gap, padding, minPreview;
} Layout;

/* Раскладка ленты своего монитора; результат — Layout::items. */
void Layout_Compute(Layout *L, WindowItem **items, int count, RECT client,
                    UINT dpi, int scrollOffset);

/* Вписывает прямоугольник с данным аспектом внутрь ячейки по центру
   (DWM сам сохраняет аспект, но прижимает к левому верхнему углу). */
RECT Layout_FitRect(RECT cell, double aspect);

/* Квадратная зона крестика закрытия у правого края подписи. */
RECT Layout_CloseRect(RECT label);
