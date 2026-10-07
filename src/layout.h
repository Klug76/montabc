#pragma once

#include "montabc.h"
#include "tracker.h"

#define LAY_HEADER_LOGICAL 14 /* panel drag grip at the top */

typedef struct LayoutItem
{
    WindowItem *win;
    RECT bounds;  /* the whole tile: label + preview */
    RECT preview; /* preview area (live tiles only) */
    RECT label;   /* label bar: icon + title */
    BOOL isStrip; /* minimized window strip */
} LayoutItem;

typedef struct Layout
{
    LayoutItem *items; /* capacity in cap, grows as needed */
    int count;
    int cap;
    int totalHeight;

    /* precomputed sizes for the current DPI */
    UINT dpi;
    int header, strip, label, gap, padding, minPreview;
} Layout;

/* Lay out the ribbon of mon's windows in the tracker's global order. */
void Layout_Compute(Layout *L, HMONITOR mon, RECT client,
                    UINT dpi, int scrollOffset);

/* Fits a rectangle of the given aspect inside the cell, centered
   (DWM preserves the aspect itself but anchors it to the top-left corner). */
RECT Layout_FitRect(RECT cell, double aspect);

/* Square close X area at the right edge of the label. */
RECT Layout_CloseRect(RECT label);
