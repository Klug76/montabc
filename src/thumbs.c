#include "thumbs.h"
#include "debug.h"
#include "util.h"
#include <dwmapi.h>

/* Dimming of the active window's preview (255 = opaque). */
#define ACTIVE_OPACITY 110

void Thumbs_Init(Thumbs *T, HWND panel)
{
    T->panel = panel;
    T->count = 0;
}

static int Find(const Thumbs *T, HWND source)
{
    int i;
    for (i = 0; i < T->count; i++)
        if (T->entries[i].source == source)
            return i;
    return -1;
}

static void RemoveAt(Thumbs *T, int idx)
{
    T->entries[idx] = T->entries[T->count - 1];
    T->count--;
}

/* Visible source area: the whole window at zoom=1, otherwise a 1/zoom
   fragment around the normalized center. */
static void ComputeSourceRect(const WindowItem *item, RECT *out)
{
    RECT rc;
    double sw, sh, zoom = item->zoom, visW, visH, cx, cy;

    GetClientRect(item->hwnd, &rc);
    sw = rc.right - rc.left;
    sh = rc.bottom - rc.top;
    if (sw <= 0 || sh <= 0 || zoom <= 1.001)
    {
        out->left = 0;
        out->top = 0;
        out->right = (int)sw > 0 ? (int)sw : 1;
        out->bottom = (int)sh > 0 ? (int)sh : 1;
        return;
    }

    visW = sw / zoom;
    visH = sh / zoom;
    cx = Util_ClampD(item->centerX * sw, visW / 2, sw - visW / 2);
    cy = Util_ClampD(item->centerY * sh, visH / 2, sh - visH / 2);

    out->left = (int)(cx - visW / 2);
    out->top = (int)(cy - visH / 2);
    out->right = (int)(cx + visW / 2);
    out->bottom = (int)(cy + visH / 2);
}

void Thumbs_Sync(Thumbs *T, const Layout *lay, RECT client, HWND activeWindow)
{
    int i;

    for (i = 0; i < T->count; i++)
        T->entries[i].wanted = FALSE;

    for (i = 0; i < lay->count; i++)
    {
        const LayoutItem *li = &lay->items[i];
        HWND source;
        BOOL wantCustomSource;
        int idx;
        HANDLE thumb;
        RECT dest, srcRect;
        DWM_THUMBNAIL_PROPERTIES props;

        if (li->isStrip)
            continue;
        if (li->preview.bottom <= client.top || li->preview.top >= client.bottom)
            continue; /* outside the viewport — no thumbnail stream needed */

        source = li->win->hwnd;
        wantCustomSource = li->win->zoom > 1.001;

        /* rcSource cannot be reset (DWM flags are additive only) —
           recreate the thumbnail so DWM tracks the source itself again. */
        idx = Find(T, source);
        if (idx >= 0 && T->entries[idx].customSource && !wantCustomSource)
        {
            DwmUnregisterThumbnail(T->entries[idx].thumb);
            RemoveAt(T, idx);
            idx = -1;
        }

        if (idx < 0)
        {
            if (FAILED(DwmRegisterThumbnail(T->panel, source, &thumb)))
                continue;
            if (!Util_Grow((void **)&T->entries, &T->cap, T->count + 1,
                           TRK_MAX_ITEMS, sizeof(ThumbEntry), L"thumbs"))
            {
                LOG(1, L"thumbs: grow failed, cap=%d", T->cap);
                DwmUnregisterThumbnail(thumb);
                continue;
            }
            idx = T->count++;
            T->entries[idx].source = source;
            T->entries[idx].thumb = thumb;
            T->entries[idx].customSource = FALSE;
        }

        dest = Layout_FitRect(li->preview, li->win->aspect);

        /* Set rcSource only at zoom>1: for a window in transitional geometry
           (restoring from minimized) GetClientRect returns the iconic bar,
           and a pinned rcSource would keep showing it until the next event. */
        if (wantCustomSource)
            ComputeSourceRect(li->win, &srcRect);
        else
        {
            srcRect.left = srcRect.top = srcRect.right = srcRect.bottom = 0;
        }

        props.dwFlags = DWM_TNP_RECTDESTINATION | DWM_TNP_VISIBLE |
                        DWM_TNP_OPACITY | DWM_TNP_SOURCECLIENTAREAONLY |
                        (wantCustomSource ? DWM_TNP_RECTSOURCE : 0);
        props.rcDestination = dest;
        props.rcSource = srcRect;
        props.opacity = (source == activeWindow) ? ACTIVE_OPACITY : 255;
        props.fVisible = TRUE;
        props.fSourceClientAreaOnly = TRUE;
        DwmUpdateThumbnailProperties(T->entries[idx].thumb, &props);

        T->entries[idx].customSource = wantCustomSource;
        T->entries[idx].wanted = TRUE;
    }

    /* Anything no longer needed (strip, off-screen, window closed) — unregister. */
    for (i = T->count - 1; i >= 0; i--)
    {
        if (!T->entries[i].wanted)
        {
            DwmUnregisterThumbnail(T->entries[i].thumb);
            RemoveAt(T, i);
        }
    }
}

void Thumbs_Dispose(Thumbs *T)
{
    int i;
    for (i = 0; i < T->count; i++)
        DwmUnregisterThumbnail(T->entries[i].thumb);
    T->count = 0;
    Util_Free(T->entries);
    T->entries = NULL;
    T->cap = 0;
}
