#include "scrollbar.h"
#include "util.h"

#define SB_CLASS L"montabc.scrollbar"
#define SB_WIDTH_LOGICAL 14
#define SB_MIN_THUMB_LOGICAL 24

/* Classic semi-transparent gray thumb; the track is nearly transparent,
   the thumb body has moderate alpha, and a dark rim holds the outline. */
static DWORD s_track, s_body, s_bodyDrag, s_border;
static BOOL s_pixelsReady;
static BOOL s_classRegistered;

/* Premultiplied gray pixel: value × alpha in each channel. */
static DWORD Premultiply(BYTE alpha, BYTE value)
{
    DWORD c = (DWORD)value * alpha / 255;
    return ((DWORD)alpha << 24) | (c << 16) | (c << 8) | c;
}

static void InitPixels(void)
{
    if (s_pixelsReady)
        return;
    s_track = Premultiply(36, 30);
    s_body = Premultiply(150, 200);
    s_bodyDrag = Premultiply(200, 220);
    s_border = Premultiply(90, 55);
    s_pixelsReady = TRUE;
}

static ScrollBar *Get(HWND hwnd)
{
    return (ScrollBar *)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
}

static void ThumbMetrics(const ScrollBar *sb, int *top, int *height)
{
    int h;

    if (sb->totalHeight <= sb->viewportHeight || sb->height <= 0)
    {
        *top = 0;
        *height = sb->height;
        return;
    }

    h = sb->height * sb->viewportHeight / sb->totalHeight;
    if (h < sb->minThumbPx)
        h = sb->minThumbPx;
    if (h > sb->height)
        h = sb->height;
    *top = (sb->height - h) * sb->scrollOffset / (sb->totalHeight - sb->viewportHeight);
    *height = h;
}

static void DragTo(ScrollBar *sb, int y, int thumbHeight)
{
    int span = sb->height - thumbHeight;
    double frac;

    if (span <= 0)
        return;
    frac = (double)(y - sb->dragAnchor) / (double)span;
    if (frac < 0.0)
        frac = 0.0;
    if (frac > 1.0)
        frac = 1.0;
    Panel_SetScrollOffset(sb->panel, (int)(frac * (sb->totalHeight - sb->viewportHeight) + 0.5));
}

static void DestroySurface(ScrollBar *sb)
{
    if (!sb->dibDc)
        return;
    SelectObject(sb->dibDc, sb->dibOld);
    DeleteObject(sb->dib);
    DeleteDC(sb->dibDc);
    sb->dibDc = NULL;
    sb->bits = NULL;
}

static void EnsureSurface(ScrollBar *sb)
{
    BITMAPINFO bmi;

    if (sb->dibDc && sb->surfaceW == sb->width && sb->surfaceH == sb->height)
        return;

    DestroySurface(sb);

    ZeroMemory(&bmi, sizeof(bmi));
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = sb->width;
    bmi.bmiHeader.biHeight = -sb->height; /* top-down */
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    sb->dib = CreateDIBSection(NULL, &bmi, DIB_RGB_COLORS, (void **)&sb->bits, NULL, 0);
    sb->dibDc = CreateCompatibleDC(NULL);
    sb->dibOld = SelectObject(sb->dibDc, sb->dib);
    sb->surfaceW = sb->width;
    sb->surfaceH = sb->height;
}

/* Redraws the per-pixel-alpha surface and feeds it to the compositor. */
static void Redraw(ScrollBar *sb)
{
    int thumbTop, thumbHeight, border, thumbBottom, x, y;
    DWORD bodyPixel;
    SIZE size = {0, 0};
    POINT src = {0, 0};
    BLENDFUNCTION blend;

    if (sb->width <= 0 || sb->height <= 0)
        return;
    EnsureSurface(sb);
    InitPixels();

    bodyPixel = sb->dragging ? s_bodyDrag : s_body;
    ThumbMetrics(sb, &thumbTop, &thumbHeight);
    border = sb->width / 12;
    if (border < 1)
        border = 1;
    thumbBottom = thumbTop + thumbHeight;
    if (thumbBottom > sb->height)
        thumbBottom = sb->height;

    {
        int i, total = sb->width * sb->height;
        for (i = 0; i < total; i++)
            sb->bits[i] = s_track;
    }
    for (y = thumbTop; y < thumbBottom; y++)
    {
        BOOL edgeRow = y < thumbTop + border || y >= thumbBottom - border;
        DWORD *row = sb->bits + (long)y * sb->width;
        for (x = 0; x < sb->width; x++)
        {
            BOOL edgeCol = x < border || x >= sb->width - border;
            row[x] = (edgeRow || edgeCol) ? s_border : bodyPixel;
        }
    }

    size.cx = sb->width;
    size.cy = sb->height;
    blend.BlendOp = AC_SRC_OVER;
    blend.BlendFlags = 0;
    blend.SourceConstantAlpha = 255;
    blend.AlphaFormat = AC_SRC_ALPHA;
    UpdateLayeredWindow(sb->hwnd, NULL, NULL, &size, sb->dibDc, &src,
                        0, &blend, ULW_ALPHA);
}

void ScrollBar_UpdateVisibility(ScrollBar *sb, BOOL pointerNearby)
{
    BOOL want = (pointerNearby || sb->dragging) && sb->totalHeight > sb->viewportHeight;

    if (want == sb->visible)
        return;
    sb->visible = want;
    if (want)
        Redraw(sb);
    ShowWindow(sb->hwnd, want ? SW_SHOWNOACTIVATE : SW_HIDE);
}

void ScrollBar_Update(ScrollBar *sb, int totalHeight, int viewportHeight,
                      int scrollOffset, BOOL pointerNearby)
{
    BOOL changed = totalHeight != sb->totalHeight || viewportHeight != sb->viewportHeight ||
                   scrollOffset != sb->scrollOffset;

    sb->totalHeight = totalHeight;
    sb->viewportHeight = viewportHeight;
    sb->scrollOffset = scrollOffset;

    ScrollBar_UpdateVisibility(sb, pointerNearby);
    if (changed && sb->visible)
        Redraw(sb);
}

void ScrollBar_Layout(ScrollBar *sb, RECT panelScreen, UINT dpi, BOOL dockRight, int topOffset)
{
    int x, y;

    sb->width = Ui_Scale(SB_WIDTH_LOGICAL, dpi);
    sb->minThumbPx = Ui_Scale(SB_MIN_THUMB_LOGICAL, dpi);
    x = dockRight ? panelScreen.right - sb->width : panelScreen.left;
    y = panelScreen.top + topOffset;
    sb->height = panelScreen.bottom - y;
    SetWindowPos(sb->hwnd, NULL, x, y, sb->width, sb->height,
                 SWP_NOACTIVATE | SWP_NOZORDER);
    if (sb->visible)
        Redraw(sb);
}

static LRESULT CALLBACK ScrollBar_WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    ScrollBar *sb;

    if (msg == WM_NCCREATE)
        SetWindowLongPtrW(hwnd, GWLP_USERDATA,
                          (LONG_PTR)((LPCREATESTRUCTW)lp)->lpCreateParams);

    sb = Get(hwnd);
    if (!sb)
        return DefWindowProcW(hwnd, msg, wp, lp);

    switch (msg)
    {
    case WM_DESTROY:
        DestroySurface(sb);
        sb->hwnd = NULL;
        return 0;

    case WM_NCHITTEST:
    {
        POINT pt;
        /* A click on the close button passes through the scrollbar into the panel */
        pt.x = (short)LOWORD(lp);
        pt.y = (short)HIWORD(lp);
        ScreenToClient(sb->panelHwnd, &pt);
        if (Panel_IsOverClose(sb->panel, pt.x, pt.y))
            return HTTRANSPARENT;
        return HTCLIENT;
    }

    case WM_MOUSEACTIVATE:
        return MA_NOACTIVATE;

    case WM_LBUTTONDOWN:
    {
        int y = (short)HIWORD(lp), thumbTop, thumbHeight;
        ThumbMetrics(sb, &thumbTop, &thumbHeight);
        /* On the thumb — drag from the grab point; on the track — teleport centered */
        sb->dragAnchor = (y >= thumbTop && y < thumbTop + thumbHeight)
                             ? y - thumbTop
                             : thumbHeight / 2;
        sb->dragging = TRUE;
        SetCapture(hwnd);
        DragTo(sb, y, thumbHeight);
        Redraw(sb);
        return 0;
    }

    case WM_MOUSEMOVE:
    {
        if (sb->dragging)
        {
            int y = (short)HIWORD(lp), thumbTop, thumbHeight;
            ThumbMetrics(sb, &thumbTop, &thumbHeight);
            DragTo(sb, y, thumbHeight);
        }
        Panel_PointerSeen(sb->panel);
        /* Otherwise we won't know the mouse left the scrollbar outside the panel */
        {
            TRACKMOUSEEVENT tme;
            tme.cbSize = sizeof(tme);
            tme.dwFlags = TME_LEAVE;
            tme.hwndTrack = hwnd;
            tme.dwHoverTime = 0;
            TrackMouseEvent(&tme);
        }
        return 0;
    }

    case WM_MOUSELEAVE:
        Panel_PointerMaybeGone(sb->panel);
        return 0;

    case WM_LBUTTONUP:
    case WM_CAPTURECHANGED:
        if (sb->dragging)
        {
            sb->dragging = FALSE;
            if (msg == WM_LBUTTONUP)
                ReleaseCapture();
            Redraw(sb);
            Panel_PointerMaybeGone(sb->panel); /* the cursor may have left the panel during the drag */
        }
        return 0;
    }

    return DefWindowProcW(hwnd, msg, wp, lp);
}

void ScrollBar_Create(ScrollBar *sb, Panel *panel, HWND owner, HINSTANCE hInst)
{
    WNDCLASSEXW wc;

    sb->panel = panel;
    sb->panelHwnd = owner;

    if (!s_classRegistered)
    {
        ZeroMemory(&wc, sizeof(wc));
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = ScrollBar_WndProc;
        wc.hInstance = hInst;
        wc.lpszClassName = SB_CLASS;
        RegisterClassExW(&wc);
        s_classRegistered = TRUE;
    }

    /* Owner (not parent): an owned window is always above its owner in z-order */
    sb->hwnd = CreateWindowExW(
        WS_EX_LAYERED | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW,
        SB_CLASS, NULL, WS_POPUP,
        0, 0, 0, 0, owner, NULL, hInst, sb);
}

void ScrollBar_Destroy(ScrollBar *sb)
{
    if (sb->hwnd)
        DestroyWindow(sb->hwnd);
}
