#include "layout.h"
#include "util.h"

#define STRIP_HEIGHT_LOGICAL 22
#define LABEL_HEIGHT_LOGICAL 18
#define GAP_LOGICAL 6
#define PADDING_LOGICAL 8
#define MIN_PREVIEW_HEIGHT_LOGICAL 48
/* не выше 1.6 ширины (очень «портретные» окна) */
#define MAX_PREVIEW_HEIGHT_FACTOR_NUM 8
#define MAX_PREVIEW_HEIGHT_FACTOR_DEN 5

static void SetDpi(Layout *L, UINT dpi)
{
    if (dpi == L->dpi)
        return;
    L->dpi = dpi;
    L->header = Ui_Scale(LAY_HEADER_LOGICAL, dpi);
    L->strip = Ui_Scale(STRIP_HEIGHT_LOGICAL, dpi);
    L->label = Ui_Scale(LABEL_HEIGHT_LOGICAL, dpi);
    L->gap = Ui_Scale(GAP_LOGICAL, dpi);
    L->padding = Ui_Scale(PADDING_LOGICAL, dpi);
    L->minPreview = Ui_Scale(MIN_PREVIEW_HEIGHT_LOGICAL, dpi);
}

void Layout_Compute(Layout *L, WindowItem **items, int count, RECT client,
                    UINT dpi, int scrollOffset)
{
    int width, left, y, i;

    SetDpi(L, dpi);
    L->count = 0;
    L->totalHeight = 0;

    width = client.right - client.left - 2 * L->padding;
    if (width <= 0)
        return;

    left = client.left + L->padding;
    y = L->header + L->padding - scrollOffset;

    for (i = 0; i < count; i++)
    {
        WindowItem *item = items[i];
        LayoutItem *li = &L->items[L->count];

        if (item->minimized)
        {
            li->bounds.left = left;
            li->bounds.top = y;
            li->bounds.right = left + width;
            li->bounds.bottom = y + L->strip;
            li->label = li->bounds;
            li->preview.left = li->preview.top = li->preview.right = li->preview.bottom = 0;
            li->isStrip = TRUE;
            y = li->bounds.bottom + L->gap;
        }
        else
        {
            double aspect = item->aspect > 0.05 ? item->aspect : 16.0 / 10.0;
            int maxPreview = width * MAX_PREVIEW_HEIGHT_FACTOR_NUM / MAX_PREVIEW_HEIGHT_FACTOR_DEN;
            int previewHeight = Util_ClampI((int)(width / aspect + 0.5), L->minPreview, maxPreview);

            /* Заголовок сверху, превью под ним */
            li->bounds.left = left;
            li->bounds.top = y;
            li->bounds.right = left + width;
            li->bounds.bottom = y + L->label + previewHeight;
            li->label = li->bounds;
            li->label.bottom = y + L->label;
            li->preview = li->bounds;
            li->preview.top = y + L->label;
            li->isStrip = FALSE;
            y = li->bounds.bottom + L->gap;
        }

        li->win = item;
        L->count++;
    }

    L->totalHeight = y + scrollOffset + L->padding - (count > 0 ? L->gap : 0);
}

RECT Layout_FitRect(RECT cell, double aspect)
{
    int cellW = cell.right - cell.left;
    int cellH = cell.bottom - cell.top;
    int w, h, x, yy;

    if (cellW <= 0 || cellH <= 0 || aspect <= 0)
        return cell;

    w = cellW;
    h = (int)(cellW / aspect + 0.5);
    if (h > cellH)
    {
        h = cellH;
        w = (int)(cellH * aspect + 0.5);
    }

    x = cell.left + (cellW - w) / 2;
    yy = cell.top + (cellH - h) / 2;
    cell.left = x;
    cell.top = yy;
    cell.right = x + w;
    cell.bottom = yy + h;
    return cell;
}

RECT Layout_CloseRect(RECT label)
{
    int size = label.bottom - label.top;
    label.left = label.right - size;
    return label;
}
