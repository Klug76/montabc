#include "render.h"
#include "util.h"

/* COLORREF = 0x00BBGGRR */
#define COL_BACKGROUND RGB(0x1E, 0x1E, 0x1E)
#define COL_STRIP RGB(0x2D, 0x2D, 0x2D)
#define COL_STRIP_ACTIVE RGB(0x3A, 0x3A, 0x3A)
#define COL_ACCENT RGB(0xD4, 0x78, 0x00)  /* #0078D4 */
#define COL_FRAME RGB(0x40, 0x40, 0x40)
#define COL_TEXT RGB(0xE0, 0xE0, 0xE0)
#define COL_TEXT_DIM RGB(0x90, 0x90, 0x90)
#define COL_CLOSE_HOVER RGB(0x23, 0x11, 0xE8) /* #E81123 */

static HBRUSH s_bg, s_strip, s_stripActive, s_accent, s_frame, s_closeHover;

static HBRUSH EnsureBrush(HBRUSH *b, COLORREF c)
{
    if (!*b)
        *b = CreateSolidBrush(c);
    return *b;
}

static void EnsureBrushes(void)
{
    EnsureBrush(&s_bg, COL_BACKGROUND);
    EnsureBrush(&s_strip, COL_STRIP);
    EnsureBrush(&s_stripActive, COL_STRIP_ACTIVE);
    EnsureBrush(&s_accent, COL_ACCENT);
    EnsureBrush(&s_frame, COL_FRAME);
    EnsureBrush(&s_closeHover, COL_CLOSE_HOVER);
}

static void SetDpi(Renderer *r, UINT dpi)
{
    if (dpi == r->dpi)
        return;
    r->dpi = dpi;

    r->iconSize = Ui_Scale(14, dpi);
    r->pad = Ui_Scale(5, dpi);
    r->gripDot = Ui_Scale(2, dpi);
    if (r->gripDot < 2)
        r->gripDot = 2;
    r->gripGap = Ui_Scale(6, dpi);
    r->header = Ui_Scale(LAY_HEADER_LOGICAL, dpi);
    r->thinBorder = Ui_Scale(1, dpi);
    r->thickBorder = Ui_Scale(2, dpi);

    if (r->font)
        DeleteObject(r->font);
    r->font = CreateFontW(-Ui_Scale(10, dpi), 0, 0, 0,
                          FW_NORMAL, 0, 0, 0,
                          DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                          CLEARTYPE_QUALITY, 0, L"Segoe UI");
}

static void DisposeBackbuffer(Renderer *r)
{
    if (!r->memDc)
        return;
    SelectObject(r->memDc, r->oldBmp);
    DeleteObject(r->memBmp);
    DeleteDC(r->memDc);
    r->memDc = NULL;
}

static void EnsureBackbuffer(Renderer *r, HDC hdc, int width, int height)
{
    if (r->memDc && width == r->bufW && height == r->bufH)
        return;

    DisposeBackbuffer(r);
    r->memDc = CreateCompatibleDC(hdc);
    r->memBmp = CreateCompatibleBitmap(hdc, width, height);
    r->oldBmp = SelectObject(r->memDc, r->memBmp);
    SetBkMode(r->memDc, TRANSPARENT);
    r->bufW = width;
    r->bufH = height;
}

/* Гриппер-«ручка» сверху: за неё панель перетаскивают на другой монитор/край. */
static void DrawHeaderGrip(Renderer *r, const RECT *client)
{
    int centerX = (client->left + client->right) / 2;
    int y = (r->header - r->gripDot) / 2;
    int i;

    for (i = -2; i <= 2; i++)
    {
        int x = centerX + i * r->gripGap - r->gripDot / 2;
        RECT dot = {x, y, x + r->gripDot, y + r->gripDot};
        FillRect(r->memDc, &dot, s_frame);
    }
}

/* Цельная рамка вокруг блока «заголовок + превью». Рисуется НАРУЖУ от
   bounds: внутри рисовать нельзя — DWM компонует превью поверх нашего GDI. */
static void DrawOutline(Renderer *r, RECT bounds, HBRUSH brush, int border)
{
    int i;

    bounds.left -= border;
    bounds.top -= border;
    bounds.right += border;
    bounds.bottom += border;
    for (i = 0; i < border; i++)
    {
        FrameRect(r->memDc, &bounds, brush);
        bounds.left++;
        bounds.top++;
        bounds.right--;
        bounds.bottom--;
    }
}

static void DrawLabel(Renderer *r, const LayoutItem *li, BOOL isActive, BOOL closeHover)
{
    RECT rct = li->label;
    RECT close, textRect;
    int iconX, iconY;
    COLORREF color = li->isStrip ? COL_TEXT_DIM : COL_TEXT;

    FillRect(r->memDc, &rct, isActive ? s_stripActive : s_strip);

    iconX = rct.left + r->pad;
    iconY = rct.top + (rct.bottom - rct.top - r->iconSize) / 2;
    if (li->win->icon)
        DrawIconEx(r->memDc, iconX, iconY, li->win->icon, r->iconSize, r->iconSize,
                   0, NULL, DI_NORMAL);

    close = Layout_CloseRect(rct);

    SetTextColor(r->memDc, color);
    textRect.left = iconX + r->iconSize + r->pad;
    textRect.top = rct.top;
    textRect.right = close.left - r->pad;
    textRect.bottom = rct.bottom;
    DrawTextW(r->memDc, li->win->title, lstrlenW(li->win->title), &textRect,
              DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS | DT_NOPREFIX);

    /* Крестик закрытия приложения */
    if (closeHover)
        FillRect(r->memDc, &close, s_closeHover);
    SetTextColor(r->memDc, closeHover ? RGB(0xFF, 0xFF, 0xFF) : COL_TEXT_DIM);
    DrawTextW(r->memDc, L"\x2715", 1, &close,
              DT_SINGLELINE | DT_VCENTER | DT_CENTER | DT_NOPREFIX);
}

void Rnd_Paint(Renderer *r, HWND hwnd, const Layout *lay, HWND activeWindow,
               UINT dpi, HWND hoverClose)
{
    PAINTSTRUCT ps;
    HDC hdc;
    RECT client;
    int width, height, i;

    SetDpi(r, dpi);
    EnsureBrushes();

    hdc = BeginPaint(hwnd, &ps);
    if (!hdc)
        return;
    GetClientRect(hwnd, &client);
    width = client.right - client.left;
    height = client.bottom - client.top;
    if (width <= 0 || height <= 0)
    {
        EndPaint(hwnd, &ps);
        return;
    }

    EnsureBackbuffer(r, hdc, width, height);
    SelectObject(r->memDc, r->font);

    FillRect(r->memDc, &client, s_bg);
    DrawHeaderGrip(r, &client);

    for (i = 0; i < lay->count; i++)
    {
        const LayoutItem *li = &lay->items[i];
        BOOL isActive;

        if (li->bounds.bottom < client.top || li->bounds.top > client.bottom)
            continue;

        isActive = li->win->hwnd == activeWindow;
        if (!li->isStrip)
        {
            DrawOutline(r, li->bounds, isActive ? s_accent : s_frame,
                        isActive ? r->thickBorder : r->thinBorder);
        }
        DrawLabel(r, li, isActive, li->win->hwnd == hoverClose);
    }

    BitBlt(hdc, 0, 0, width, height, r->memDc, 0, 0, SRCCOPY);
    EndPaint(hwnd, &ps);
}

void Rnd_Destroy(Renderer *r)
{
    DisposeBackbuffer(r);
    if (r->font)
    {
        DeleteObject(r->font);
        r->font = NULL;
    }
}
