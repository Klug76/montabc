#include "panel.h"
#include "app.h"
#include "appbar.h"
#include "autostart.h"
#include "layout.h"
#include "render.h"
#include "scrollbar.h"
#include "strings.h"
#include "thumbs.h"
#include "tracker.h"
#include "util.h"

#define PANEL_CLASS L"montabc.panel"
#define GRIP_LOGICAL 6

#define CMD_DOCK_LEFT 1
#define CMD_DOCK_RIGHT 2
#define CMD_EXIT 3
#define CMD_HIDE 4
#define CMD_AUTOSTART 5

/* таймеры */
#define ACTIVATE_TIMER_ID 1   /* окно ожидания второго клика по живому тайлу */
#define HOVERZOOM_TIMER_ID 2  /* окно ожидания hover-лупы */
#define ACTIVATE_DELAY_MS 150
#define HOVERZOOM_DELAY_MS 700
#define HOVERZOOM_FACTOR 5.0

/* машина мыши */
#define PS_NONE 0
#define PS_PRESSED 1
#define PS_DRAGGING 2
#define PS_PANEL_DRAG 3

struct Panel
{
    HWND hwnd;
    MonitorCfg *cfg;
    DisplayInfo display;
    AppBar appbar;
    UINT dpi;
    int gripPx;
    int headerPx;
    BOOL updatingPos;
    BOOL resizing;

    /* лента окон своего монитора */
    Renderer rnd;
    Layout layout;
    Thumbs thumbs;
    ScrollBar sbar;
    WindowItem *mine[TRK_MAX_ITEMS];
    int mineCount;
    int scrollOffset;
    BOOL pointerNearby;

    /* DPI-кеш */
    int wheelStepPx;
    int repeatRadiusPx;
    int dragThresholdPx;

    /* hover крестика закрытия (HWND окна, а не указатель на item —
       массив трекера сдвигается, указатель может устареть) */
    HWND hoverClose;
    DWORD closeClickTick;
    int closeClickX, closeClickY;

    /* машина мыши */
    int press;
    HWND pressHwnd;
    int pressX, pressY;
    BOOL swallowNextUp;

    /* отложенная активация по клику (150 мс на распознавание двойного) */
    HWND pendingHwnd;

    /* hover-лупа: наведение на превью без нажатий включает временный zoom */
    HWND hoverZoomHwnd;
    HWND hoverCandidateHwnd;
    double savedZoom, savedCenterX, savedCenterY;
};

static UINT s_callbackMsg;
static BOOL s_classRegistered;

static BOOL Panel_InGrip(Panel *p, int clientX);
static int Panel_CalcWidth(const Panel *p, double pct);
static void Panel_ResizeToScreenX(Panel *p, int screenX);
static LRESULT CALLBACK Panel_WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);
static LRESULT Panel_Handle(Panel *p, HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);
static void Panel_SetDpi(Panel *p, UINT dpi);
static void Panel_CollectMine(Panel *p);
static void Panel_Menu(Panel *p);
static void Panel_SetEdge(Panel *p, DockEdge edge);
static void Panel_OnPress(Panel *p, int x, int y);
static void Panel_OnMove(Panel *p, int x, int y, BOOL ctrl);
static void Panel_OnClick(Panel *p, int x, int y, BOOL ctrl);
static void Panel_EndPress(Panel *p);
static void Panel_CancelPendingActivation(Panel *p, HWND unlessHwnd);
static void Panel_DragTo(Panel *p, int x, int y);
static void Panel_DropPanel(Panel *p);
static void Panel_Minimize(Panel *p, WindowItem *item);
static void Panel_SendToBottom(Panel *p, WindowItem *item);
static HWND Panel_TopWindowExcept(Panel *p, const WindowItem *item);
static BOOL Panel_HasOtherLive(Panel *p, const WindowItem *item);
static void Panel_CtrlZoom(Panel *p, int wheelDelta);
static void Panel_CtrlPan(Panel *p, LayoutItem *over, int x, int y);
static void Panel_HoverZoomMove(Panel *p, LayoutItem *over, int x, int y);
static void Panel_TryBeginHoverZoom(Panel *p);
static void Panel_CancelHoverZoom(Panel *p);
static void Panel_ClearHoverCandidate(Panel *p);
static void Panel_SetCenterFromPoint(Panel *p, WindowItem *item, int x, int y);
static void Panel_ResetZoom(WindowItem *item);
static void Panel_GetCursorClient(Panel *p, POINT *pt);
/* Окна этого монитора в порядке общей ленты. */
static void Panel_CollectMine(Panel *p)
{
    int i, total = Trk_Count();

    p->mineCount = 0;
    for (i = 0; i < total && p->mineCount < TRK_MAX_ITEMS; i++)
    {
        WindowItem *item = Trk_At(i);
        if (item->monitor == p->display.hMon)
            p->mine[p->mineCount++] = item;
    }
}

static BOOL PtIn(int x, int y, const RECT *r)
{
    return x >= r->left && x < r->right && y >= r->top && y < r->bottom;
}

static LayoutItem *Panel_HitTest(Panel *p, int x, int y)
{
    int i;
    for (i = 0; i < p->layout.count; i++)
    {
        LayoutItem *li = &p->layout.items[i];
        if (PtIn(x, y, &li->bounds))
            return li;
    }
    return NULL;
}

static void Panel_UpdateCloseHover(Panel *p, int x, int y)
{
    HWND hover = NULL;
    LayoutItem *li = Panel_HitTest(p, x, y);

    if (li)
    {
        RECT close = Layout_CloseRect(li->label);
        if (PtIn(x, y, &close))
            hover = li->win->hwnd;
    }

    if (hover != p->hoverClose)
    {
        p->hoverClose = hover;
        InvalidateRect(p->hwnd, NULL, FALSE);
    }

    /* Всегда: иначе WM_MOUSELEAVE не придёт, и подсветка крестика
       «залипнет», когда мышь уйдёт с панели. */
    {
        TRACKMOUSEEVENT tme;
        tme.cbSize = sizeof(tme);
        tme.dwFlags = TME_LEAVE;
        tme.hwndTrack = p->hwnd;
        tme.dwHoverTime = 0;
        TrackMouseEvent(&tme);
    }
}

/* Абсолютный скролл ленты; max берётся из раскладки прошлого кадра,
   как в оригинале. */
void Panel_SetScrollOffset(Panel *p, int offset)
{
    RECT client;
    int maxScroll;

    GetClientRect(p->hwnd, &client);
    maxScroll = p->layout.totalHeight - (client.bottom - client.top);
    if (maxScroll < 0)
        maxScroll = 0;
    offset = Util_ClampI(offset, 0, maxScroll);

    if (offset == p->scrollOffset)
        return;
    Panel_CancelHoverZoom(p); /* лента уезжает из-под курсора */
    p->scrollOffset = offset;
    InvalidateRect(p->hwnd, NULL, FALSE);
}

void Panel_PointerSeen(Panel *p)
{
    if (p->pointerNearby)
        return;
    p->pointerNearby = TRUE;
    ScrollBar_UpdateVisibility(&p->sbar, TRUE);
}

/* Похоже, курсор ушёл; скрываем скроллбар, только если он вне окна панели. */
void Panel_PointerMaybeGone(Panel *p)
{
    POINT pt;
    RECT wnd;

    if (!p->pointerNearby)
        return;
    GetCursorPos(&pt);
    GetWindowRect(p->hwnd, &wnd);
    if (pt.x >= wnd.left && pt.x < wnd.right && pt.y >= wnd.top && pt.y < wnd.bottom)
        return;
    p->pointerNearby = FALSE;
    ScrollBar_UpdateVisibility(&p->sbar, FALSE);
}

BOOL Panel_IsOverClose(Panel *p, int x, int y)
{
    LayoutItem *li = Panel_HitTest(p, x, y);
    RECT close;

    if (!li)
        return FALSE;
    close = Layout_CloseRect(li->label);
    return PtIn(x, y, &close);
}

/* Второй клик того же жеста: в пределах double-click-времени (с запасом)
   и рядом — после закрытия лента сдвигается, под курсором может оказаться
   крестик чужого окна, его закрывать нельзя. */
static BOOL Panel_IsRepeatClick(Panel *p, int x, int y)
{
    int dx, dy;
    DWORD elapsed = GetTickCount() - p->closeClickTick;

    if (elapsed > GetDoubleClickTime() + 150)
        return FALSE;
    dx = x - p->closeClickX;
    dy = y - p->closeClickY;
    if (dx < 0)
        dx = -dx;
    if (dy < 0)
        dy = -dy;
    return dx <= p->repeatRadiusPx && dy <= p->repeatRadiusPx;
}

static void Panel_GetCursorClient(Panel *p, POINT *pt)
{
    GetCursorPos(pt);
    ScreenToClient(p->hwnd, pt);
}

static void Panel_ResetZoom(WindowItem *item)
{
    item->zoom = 1.0;
    item->centerX = 0.5;
    item->centerY = 0.5;
}

static void Panel_CancelPendingActivation(Panel *p, HWND unlessHwnd)
{
    if (!p->pendingHwnd || p->pendingHwnd == unlessHwnd)
        return;
    KillTimer(p->hwnd, ACTIVATE_TIMER_ID);
    p->pendingHwnd = NULL;
}

static void Panel_ClearHoverCandidate(Panel *p)
{
    if (p->hoverCandidateHwnd)
    {
        p->hoverCandidateHwnd = NULL;
        KillTimer(p->hwnd, HOVERZOOM_TIMER_ID);
    }
}

static void Panel_CancelHoverZoom(Panel *p)
{
    WindowItem *item;

    Panel_ClearHoverCandidate(p);
    if (!p->hoverZoomHwnd)
        return;
    item = Trk_Find(p->hoverZoomHwnd);
    p->hoverZoomHwnd = NULL;
    if (item)
    {
        item->zoom = p->savedZoom;
        item->centerX = p->savedCenterX;
        item->centerY = p->savedCenterY;
    }
    InvalidateRect(p->hwnd, NULL, FALSE);
}

/* Центр видимой области по позиции курсора над превью данного окна. */
static void Panel_SetCenterFromPoint(Panel *p, WindowItem *item, int x, int y)
{
    int i;

    for (i = 0; i < p->layout.count; i++)
    {
        LayoutItem *li = &p->layout.items[i];
        if (li->isStrip || li->win->hwnd != item->hwnd)
            continue;
        {
            RECT fit = Layout_FitRect(li->preview, item->aspect);
            int w = fit.right - fit.left, h = fit.bottom - fit.top;
            if (w <= 0 || h <= 0)
                return;
            item->centerX = Util_ClampD((x - fit.left) / (double)w, 0.0, 1.0);
            item->centerY = Util_ClampD((y - fit.top) / (double)h, 0.0, 1.0);
            InvalidateRect(p->hwnd, NULL, FALSE);
            return;
        }
    }
}

/* Hover-лупа: задержка над превью включает временный zoom, движение
   панорамирует, уход с превью — восстановление. */
static void Panel_HoverZoomMove(Panel *p, LayoutItem *over, int x, int y)
{
    BOOL overPreview = over && !over->isStrip && PtIn(x, y, &over->preview);
    WindowItem *item;

    if (p->hoverZoomHwnd)
    {
        if (overPreview && over->win->hwnd == p->hoverZoomHwnd)
        {
            SetCursor(LoadCursorW(NULL, (LPCWSTR)IDC_SIZEALL));
            Panel_SetCenterFromPoint(p, over->win, x, y);
        }
        else
        {
            Panel_CancelHoverZoom(p);
        }
        return;
    }

    if (overPreview)
    {
        item = over->win;
        if (p->hoverCandidateHwnd != item->hwnd)
        {
            p->hoverCandidateHwnd = item->hwnd;
            SetTimer(p->hwnd, HOVERZOOM_TIMER_ID, HOVERZOOM_DELAY_MS, NULL);
        }
    }
    else
    {
        Panel_ClearHoverCandidate(p);
    }
}

static void Panel_TryBeginHoverZoom(Panel *p)
{
    WindowItem *item;
    POINT pt;
    LayoutItem *li;

    item = Trk_Find(p->hoverCandidateHwnd);
    p->hoverCandidateHwnd = NULL;
    if (!item || p->press != PS_NONE || p->hoverZoomHwnd)
        return;

    /* Мышь всё ещё над этим же превью? */
    Panel_GetCursorClient(p, &pt);
    li = Panel_HitTest(p, pt.x, pt.y);
    if (!li || li->isStrip || li->win != item || !PtIn(pt.x, pt.y, &li->preview))
        return;

    p->savedZoom = item->zoom;
    p->savedCenterX = item->centerX;
    p->savedCenterY = item->centerY;
    p->hoverZoomHwnd = item->hwnd;

    /* Лупа всегда ровно ×5, независимо от постоянного Ctrl-zoom */
    item->zoom = HOVERZOOM_FACTOR;
    SetCursor(LoadCursorW(NULL, (LPCWSTR)IDC_SIZEALL));
    Panel_SetCenterFromPoint(p, item, pt.x, pt.y);
    InvalidateRect(p->hwnd, NULL, FALSE);
}

/* Ctrl+колесо над превью: постоянный zoom ×1..×5. */
static void Panel_CtrlZoom(Panel *p, int wheelDelta)
{
    WindowItem *item;
    LayoutItem *li;
    POINT pt;
    double zoom;

    Panel_CancelHoverZoom(p); /* ctrl управляет постоянным zoom, лупа не мешает */
    Panel_GetCursorClient(p, &pt);
    li = Panel_HitTest(p, pt.x, pt.y);
    if (!li || li->isStrip || !PtIn(pt.x, pt.y, &li->preview))
        return;

    item = li->win;
    zoom = item->zoom * (wheelDelta > 0 ? 1.25 : 0.8);
    zoom = Util_ClampD(zoom, 1.0, 5.0);
    if (zoom < 1.05)
        Panel_ResetZoom(item);
    else
        item->zoom = zoom;
    InvalidateRect(p->hwnd, NULL, FALSE);
}

/* Ctrl+движение мыши над увеличенным превью: центр видимой области. */
static void Panel_CtrlPan(Panel *p, LayoutItem *over, int x, int y)
{
    if (!over || over->isStrip || over->win->zoom <= 1.001)
        return;
    SetCursor(LoadCursorW(NULL, (LPCWSTR)IDC_SIZEALL));
    Panel_SetCenterFromPoint(p, over->win, x, y);
}

static void Panel_EndPress(Panel *p)
{
    BOOL wasDragging = p->press == PS_DRAGGING;

    p->press = PS_NONE;
    p->pressHwnd = NULL;
    ReleaseCapture();
    if (wasDragging)
        InvalidateRect(p->hwnd, NULL, FALSE); /* снять подсветку таскаемого */
}

static void Panel_OnPress(Panel *p, int x, int y)
{
    LayoutItem *li;

    /* «Ручка» сверху или пустая зона — перетаскивание всей панели */
    if (y < p->headerPx || (li = Panel_HitTest(p, x, y)) == NULL)
    {
        Panel_CancelPendingActivation(p, NULL);
        p->press = PS_PANEL_DRAG;
        SetCapture(p->hwnd);
        SetCursor(LoadCursorW(NULL, (LPCWSTR)IDC_SIZEALL));
        return;
    }

    /* Второй клик двойного детектируем по НАЖАТИЮ: сворачиваем окно
       системно прямо здесь, а его отпускание глотаем. */
    if (p->pendingHwnd && p->pendingHwnd == li->win->hwnd && !li->isStrip)
    {
        Panel_CancelPendingActivation(p, NULL);
        p->swallowNextUp = TRUE;
        Panel_Minimize(p, li->win);
        return;
    }

    /* Нажатие по другому элементу отменяет ожидающую активацию */
    Panel_CancelPendingActivation(p, li->win->hwnd);
    /* Клик не должен внезапно включать hover-лупу */
    Panel_ClearHoverCandidate(p);

    p->press = PS_PRESSED;
    p->pressHwnd = li->win->hwnd;
    p->pressX = x;
    p->pressY = y;
    SetCapture(p->hwnd);
}

static void Panel_DragTo(Panel *p, int x, int y)
{
    WindowItem *pressItem = Trk_Find(p->pressHwnd);
    LayoutItem *over;

    if (!pressItem)
        return;
    over = Panel_HitTest(p, x, y);
    if (!over || over->win->hwnd == p->pressHwnd)
        return;
    /* Перетаскивание только внутри своей секции (живые / свёрнутые) */
    if (over->win->minimized != pressItem->minimized)
        return;
    Trk_Move(pressItem, Trk_IndexOf(over->win));
}

static void Panel_OnMove(Panel *p, int x, int y, BOOL ctrl)
{
    LayoutItem *over;
    int dx, dy;

    switch (p->press)
    {
    case PS_PRESSED:
        dx = x - p->pressX;
        dy = y - p->pressY;
        if (dx < 0)
            dx = -dx;
        if (dy < 0)
            dy = -dy;
        if (dx > p->dragThresholdPx || dy > p->dragThresholdPx)
        {
            Panel_CancelHoverZoom(p);
            Panel_CancelPendingActivation(p, NULL);
            p->press = PS_DRAGGING;
            SetCursor(LoadCursorW(NULL, (LPCWSTR)IDC_SIZENS));
            InvalidateRect(p->hwnd, NULL, FALSE); /* подсветка таскаемого */
            Panel_DragTo(p, x, y);
        }
        break;

    case PS_DRAGGING:
        SetCursor(LoadCursorW(NULL, (LPCWSTR)IDC_SIZENS));
        Panel_DragTo(p, x, y);
        break;

    case PS_PANEL_DRAG:
        SetCursor(LoadCursorW(NULL, (LPCWSTR)IDC_SIZEALL));
        break;

    case PS_NONE:
        /* Один hit-test на движение — общий для лупы, pan и крестика */
        over = Panel_HitTest(p, x, y);
        if (ctrl)
        {
            Panel_ClearHoverCandidate(p);
            Panel_CtrlPan(p, over, x, y);
        }
        else
        {
            Panel_HoverZoomMove(p, over, x, y);
        }
        Panel_UpdateCloseHover(p, x, y);
        break;
    }
}

/* Бросок панели: свой монитор — смена края по половине экрана,
   свободный чужой — переезд туда (у занятого своя панель уже есть). */
static void Panel_DropPanel(Panel *p)
{
    POINT pt;
    HMONITOR target;

    GetCursorPos(&pt);
    target = MonitorFromPoint(pt, MONITOR_DEFAULTTONEAREST);

    if (target == p->display.hMon)
    {
        Panel_SetEdge(p, pt.x < (p->display.rc.left + p->display.rc.right) / 2
                             ? DOCK_LEFT
                             : DOCK_RIGHT);
        return;
    }

    /* Может уничтожить эту панель — дальше по стеку к её полям не обращаться */
    App_MovePanel(p, target, pt.x);
}

static void Panel_OnClick(Panel *p, int x, int y, BOOL ctrl)
{
    LayoutItem *li = Panel_HitTest(p, x, y);
    WindowItem *item;

    if (!li)
        return;
    item = li->win;

    {
        RECT close = Layout_CloseRect(li->label);
        if (PtIn(x, y, &close))
        {
            /* Повторный клик в зоне крестика: после закрытия лента сдвинулась,
               под курсором крестик чужого окна — не закрываем его случайно. */
            if (!Panel_IsRepeatClick(p, x, y))
            {
                p->closeClickTick = GetTickCount();
                p->closeClickX = x;
                p->closeClickY = y;
                PostMessageW(item->hwnd, WM_CLOSE, 0, 0);
            }
            return;
        }
    }

    if (ctrl)
    {
        /* Ctrl+клик — сброс zoom&pan этого превью */
        Panel_ResetZoom(item);
        InvalidateRect(p->hwnd, NULL, FALSE);
        return;
    }

    if (!li->isStrip)
    {
        /* Живой тайл: переключение (или уход вниз z-order, если окно уже
           активно) после короткого окна ожидания второго клика. Сам второй
           клик (двойной = свернуть) перехватывается в OnPress. */
        p->pendingHwnd = item->hwnd;
        SetTimer(p->hwnd, ACTIVATE_TIMER_ID, ACTIVATE_DELAY_MS, NULL);
        return;
    }

    /* Полоска: мгновенный restore + переключение (Activate сам делает
       SW_RESTORE). Второй клик двойного попадёт сюда же и ничего не
       изменит — одинарный и двойной эквивалентны. */
    App_Activate(item->hwnd);
}

/* Системное сворачивание с передачей фокуса следующему по истории окну. */
static void Panel_Minimize(Panel *p, WindowItem *item)
{
    BOOL wasForeground;
    (void)p;

    if (!IsWindow(item->hwnd))
        return;
    wasForeground = item->hwnd == g_foreground;
    ShowWindow(item->hwnd, SW_SHOWMINNOACTIVE);
    if (wasForeground)
        App_ActivateMostRecentExcept(item->hwnd);
}

/* На этом мониторе есть ещё несвёрнутые окна, кроме данного? */
static BOOL Panel_HasOtherLive(Panel *p, const WindowItem *item)
{
    int i, total = Trk_Count();

    for (i = 0; i < total; i++)
    {
        WindowItem *other = Trk_At(i);
        if (other != item && !other->minimized && other->monitor == p->display.hMon)
            return TRUE;
    }
    return FALSE;
}

/* Уводит окно под все остальные, не сворачивая (повторный клик по
   активному тайлу). Единственное живое окно монитора остаётся как есть. */
static void Panel_SendToBottom(Panel *p, WindowItem *item)
{
    HWND next;

    if (!IsWindow(item->hwnd) || !Panel_HasOtherLive(p, item))
        return;

    SetWindowPos(item->hwnd, HWND_BOTTOM, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);

    /* Фокус — тому, что теперь оказалось сверху, а не последнему по истории:
       иначе повторение жеста чередует два окна вместо обхода всей стопки. */
    if (item->hwnd != g_foreground)
        return;
    next = Panel_TopWindowExcept(p, item);
    if (next)
        App_Activate(next);
}

/* Верхнее по z-order живое окно этого монитора, кроме указанного. */
static HWND Panel_TopWindowExcept(Panel *p, const WindowItem *item)
{
    HWND probe = GetTopWindow(NULL);
    while (probe)
    {
        WindowItem *cand;
        if (probe != item->hwnd)
        {
            cand = Trk_Find(probe);
            if (cand && !cand->minimized && cand->monitor == p->display.hMon)
                return probe;
        }
        probe = GetWindow(probe, GW_HWNDNEXT);
    }
    return NULL;
}

static void EnsureClass(HINSTANCE hInst)
{
    WNDCLASSEXW wc;

    if (s_classRegistered)
        return;

    s_callbackMsg = RegisterWindowMessageW(L"montabc.appbar");

    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = Panel_WndProc;
    wc.cbClsExtra = 0;
    wc.cbWndExtra = 0;
    wc.hInstance = hInst;
    wc.hIcon = LoadIconW(hInst, MAKEINTRESOURCEW(1));
    wc.hCursor = LoadCursorW(NULL, (LPCWSTR)IDC_ARROW);
    wc.hbrBackground = NULL; /* весь фон рисуем сами в WM_PAINT */
    wc.lpszMenuName = NULL;
    wc.lpszClassName = PANEL_CLASS;
    wc.hIconSm = NULL;
    RegisterClassExW(&wc);

    s_classRegistered = TRUE;
}

BOOL Panel_Create(HINSTANCE hInst, const DisplayInfo *display, MonitorCfg *cfg, Panel **out)
{
    Panel *p = (Panel *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(Panel));

    if (!p)
        return FALSE;

    p->cfg = cfg;
    p->display = *display;

    EnsureClass(hInst);

    p->hwnd = CreateWindowExW(
        WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_TOPMOST,
        PANEL_CLASS, APP_NAME, WS_POPUP,
        0, 0, 200, 200,
        NULL, NULL, hInst, p);
    if (!p->hwnd)
    {
        HeapFree(GetProcessHeap(), 0, p);
        return FALSE;
    }

    Panel_SetDpi(p, GetDpiForWindow(p->hwnd));
    ScrollBar_Create(&p->sbar, p, p->hwnd, hInst);
    AppBar_Init(&p->appbar, p->hwnd, s_callbackMsg);
    AppBar_Register(&p->appbar);
    Panel_UpdatePosition(p);
    ShowWindow(p->hwnd, SW_SHOWNOACTIVATE);

    Thumbs_Init(&p->thumbs, p->hwnd);

    *out = p;
    return TRUE;
}

void Panel_Destroy(Panel *p)
{
    if (p->hwnd)
        DestroyWindow(p->hwnd);
    Rnd_Destroy(&p->rnd);
    HeapFree(GetProcessHeap(), 0, p);
}

void Panel_Invalidate(Panel *p)
{
    if (p->hwnd)
        InvalidateRect(p->hwnd, NULL, FALSE);
}

void Panel_SetDisplay(Panel *p, const DisplayInfo *display)
{
    p->display = *display;
    Panel_UpdatePosition(p);
}

const WCHAR *Panel_GetDevice(const Panel *p)
{
    return p->display.device;
}

/* Рабочая область залезла под панель: shell потерял нашу полосу
   (выход из сна/гибернации, сброс work area). При тяге и
   пересогласовании позиции перекрытие штатно. */
BOOL Panel_IsWorkAreaBroken(const Panel *p)
{
    MONITORINFO mi;
    RECT wnd;

    if (!p->hwnd || p->resizing || p->updatingPos)
        return FALSE;
    if (!GetWindowRect(p->hwnd, &wnd))
        return FALSE;
    mi.cbSize = sizeof(mi);
    if (!GetMonitorInfoW(p->display.hMon, &mi))
        return FALSE;
    /* Панели докованы по X во всю высоту — значимо пересечение по X. */
    return min(wnd.right, mi.rcWork.right) > max(wnd.left, mi.rcWork.left);
}

/* Регистрация appbar'а с нуля: после сна и рестарта explorer.
   AppBar_Register но-опит по флагу registered, поэтому сначала ABM_REMOVE. */
void Panel_Reregister(Panel *p)
{
    AppBar_Unregister(&p->appbar);
    AppBar_Register(&p->appbar);
    Panel_UpdatePosition(p);
}

void Panel_UpdatePosition(Panel *p)
{
    RECT rc;
    int width;
    double pct;

    if (p->updatingPos || !p->hwnd)
        return;
    p->updatingPos = TRUE;

    pct = Util_ClampD(p->cfg->widthPct, CFG_MIN_WIDTH, CFG_MAX_WIDTH);
    width = Panel_CalcWidth(p, pct);

    rc = AppBar_SetPos(&p->appbar, p->cfg->edge, p->display.rc, width);
    /* Бит WS_EX_TOPMOST рассинхронизируется с фактическим z-order —
       переутверждаем topmost при каждом размещении. */
    SetWindowPos(p->hwnd, HWND_TOPMOST,
                 rc.left, rc.top, rc.right - rc.left, rc.bottom - rc.top,
                 SWP_NOACTIVATE);
    Panel_SetDpi(p, GetDpiForWindow(p->hwnd));
    ScrollBar_Layout(&p->sbar, rc, p->dpi, p->cfg->edge == DOCK_RIGHT, p->headerPx);

    p->updatingPos = FALSE;
}

static void Panel_SetDpi(Panel *p, UINT dpi)
{
    if (dpi == p->dpi && p->gripPx != 0)
        return;
    p->dpi = dpi;
    p->gripPx = Ui_Scale(GRIP_LOGICAL, dpi);
    p->headerPx = Ui_Scale(LAY_HEADER_LOGICAL, dpi);
    p->wheelStepPx = Ui_Scale(60, dpi); /* px за один щелчок колеса */
    p->repeatRadiusPx = Ui_Scale(16, dpi);
    p->dragThresholdPx = Ui_Scale(8, dpi);
}

/* Зона захвата ресайза — узкая полоса вдоль внутреннего края панели. */
static BOOL Panel_InGrip(Panel *p, int clientX)
{
    RECT rc;
    GetClientRect(p->hwnd, &rc);
    return p->cfg->edge == DOCK_LEFT
               ? clientX >= rc.right - p->gripPx
               : clientX < p->gripPx;
}

/* Ширина панели в пикселях: процент от монитора, минимум 40px. */
static int Panel_CalcWidth(const Panel *p, double pct)
{
    int monW = p->display.rc.right - p->display.rc.left;
    int width = (int)(monW * pct / 100.0 + 0.5);
    if (width < 40)
        width = 40;
    return width;
}

/* Лёгкая установка размера БЕЗ переговоров с shell — только в процессе drag,
   чтобы максимизированные окна не переезжали на каждом шаге. */
static void Panel_DragSetSize(Panel *p)
{
    double pct = Util_ClampD(p->cfg->widthPct, CFG_MIN_WIDTH, CFG_MAX_WIDTH);
    int width = Panel_CalcWidth(p, pct);
    RECT rc = p->display.rc;

    if (p->cfg->edge == DOCK_LEFT)
        rc.right = rc.left + width;
    else
        rc.left = rc.right - width;

    SetWindowPos(p->hwnd, HWND_TOPMOST, rc.left, rc.top,
                 rc.right - rc.left, rc.bottom - rc.top, SWP_NOACTIVATE);
    /* WM_PAINT низкоприоритетен (генерируется лишь при пустой очереди) и при
       потоке WM_MOUSEMOVE запаздывает — форсируем перерисовку немедленно. */
    UpdateWindow(p->hwnd);
}

/* Ширина по экранной X курсора, clamp 3–50%; дрожание меньше 0.05% глушим. */
static void Panel_ResizeToScreenX(Panel *p, int screenX)
{
    int monW = p->display.rc.right - p->display.rc.left;
    int widthPx;
    double pct;

    if (monW <= 0)
        return;

    widthPx = (p->cfg->edge == DOCK_LEFT)
                  ? screenX - p->display.rc.left
                  : p->display.rc.right - screenX;
    pct = Util_ClampD(widthPx * 100.0 / monW, CFG_MIN_WIDTH, CFG_MAX_WIDTH);
    if (pct - p->cfg->widthPct < 0.05 && p->cfg->widthPct - pct < 0.05)
        return;

    p->cfg->widthPct = pct;
    Panel_DragSetSize(p);
}

static LRESULT CALLBACK Panel_WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    Panel *p;

    if (msg == WM_NCCREATE)
        SetWindowLongPtrW(hwnd, GWLP_USERDATA,
                          (LONG_PTR)((LPCREATESTRUCTW)lp)->lpCreateParams);

    p = (Panel *)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    if (p)
        return Panel_Handle(p, hwnd, msg, wp, lp);

    return DefWindowProcW(hwnd, msg, wp, lp);
}

static LRESULT Panel_Handle(Panel *p, HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    if (msg == s_callbackMsg)
    {
        switch ((UINT)wp)
        {
        case ABN_POSCHANGED:
            Panel_UpdatePosition(p);
            break;
        case ABN_FULLSCREENAPP:
            /* Полноэкранное приложение: уходим вниз z-order, потом возвращаемся. */
            SetWindowPos(p->hwnd, lp ? HWND_BOTTOM : HWND_TOPMOST,
                         0, 0, 0, 0,
                         SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
            break;
        }
        return 0;
    }

    switch (msg)
    {
    case WM_PAINT:
    {
        RECT client;
        GetClientRect(hwnd, &client);
        Panel_CollectMine(p);
        Layout_Compute(&p->layout, p->mine, p->mineCount, client, p->dpi, p->scrollOffset);
        Thumbs_Sync(&p->thumbs, &p->layout, client, g_foreground);
        Rnd_Paint(&p->rnd, hwnd, &p->layout, g_foreground, p->dpi, p->hoverClose);
        ScrollBar_Update(&p->sbar, p->layout.totalHeight, client.bottom - client.top,
                         p->scrollOffset, p->pointerNearby);
        return 0;
    }

    case WM_MOUSEWHEEL:
    {
        int delta = (short)HIWORD(wp);
        if (wp & MK_CONTROL)
            Panel_CtrlZoom(p, delta);
        else
            Panel_SetScrollOffset(p, p->scrollOffset - delta * p->wheelStepPx / 120);
        return 0;
    }

    case WM_MOUSELEAVE:
        Panel_CancelHoverZoom(p);
        if (p->hoverClose)
        {
            p->hoverClose = NULL;
            InvalidateRect(hwnd, NULL, FALSE);
        }
        return 0;

    case WM_TIMER:
        if (wp == ACTIVATE_TIMER_ID)
        {
            WindowItem *item;
            KillTimer(hwnd, ACTIVATE_TIMER_ID);
            item = Trk_Find(p->pendingHwnd);
            p->pendingHwnd = NULL;
            if (item && IsWindow(item->hwnd))
            {
                /* Клик по уже активному окну убирает его назад, а не переключает */
                if (item->hwnd == g_foreground)
                    Panel_SendToBottom(p, item);
                else
                    App_Activate(item->hwnd);
            }
        }
        else if (wp == HOVERZOOM_TIMER_ID)
        {
            KillTimer(hwnd, HOVERZOOM_TIMER_ID);
            Panel_TryBeginHoverZoom(p);
        }
        return 0;

    case WM_CAPTURECHANGED:
        p->resizing = FALSE;
        p->press = PS_NONE;
        p->pressHwnd = NULL;
        return 0;

    case WM_ERASEBKGND:
        return 1; /* всё рисуется в WM_PAINT */

    case WM_RBUTTONDOWN:
        return 0; /* жест целиком обрабатывается на отпускании */

    case WM_RBUTTONUP:
    {
        int x = (short)LOWORD(lp), y = (short)HIWORD(lp);
        LayoutItem *li;

        /* Меню осталось за «ручкой» вверху и пустой частью ленты */
        if (y < p->headerPx || (li = Panel_HitTest(p, x, y)) == NULL)
        {
            Panel_Menu(p);
            return 0;
        }
        if (li->isStrip)
            return 0; /* свёрнутое окно сворачивать некуда */
        Panel_CancelPendingActivation(p, NULL);
        Panel_Minimize(p, li->win);
        return 0;
    }

    case WM_LBUTTONDOWN:
        if (Panel_InGrip(p, (short)LOWORD(lp)))
        {
            p->resizing = TRUE;
            /* Панель могла быть утоплена fullscreen-приложением — вернуть наверх. */
            SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0,
                         SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
            SetCapture(hwnd);
            return 0;
        }
        Panel_OnPress(p, (short)LOWORD(lp), (short)HIWORD(lp));
        return 0;

    case WM_MOUSEMOVE:
        Panel_PointerSeen(p);
        if (p->resizing)
        {
            POINT pt;
            GetCursorPos(&pt);
            Panel_ResizeToScreenX(p, pt.x);
            return 0;
        }
        Panel_OnMove(p, (short)LOWORD(lp), (short)HIWORD(lp), (wp & MK_CONTROL) != 0);
        break;

    case WM_LBUTTONUP:
        if (p->resizing)
        {
            p->resizing = FALSE;
            ReleaseCapture();
            /* Переговоры с shell один раз в конце drag: work area и
               максимизированные окна перестраиваются по итоговой ширине. */
            Panel_UpdatePosition(p);
            Cfg_Save();
            return 0;
        }
        if (p->swallowNextUp)
        {
            /* Отпускание второго клика двойного — жест обработан на DOWN */
            p->swallowNextUp = FALSE;
            Panel_EndPress(p);
            return 0;
        }
        if (p->press == PS_DRAGGING)
        {
            Panel_EndPress(p);
        }
        else if (p->press == PS_PANEL_DRAG)
        {
            Panel_EndPress(p);
            Panel_DropPanel(p); /* может уничтожить панель — выходим */
            return 0;
        }
        else
        {
            Panel_EndPress(p);
            Panel_OnClick(p, (short)LOWORD(lp), (short)HIWORD(lp), (wp & MK_CONTROL) != 0);
        }
        return 0;

    case WM_SETCURSOR:
        if (LOWORD(lp) == HTCLIENT)
        {
            POINT pt;
            GetCursorPos(&pt);
            ScreenToClient(hwnd, &pt);
            if (Panel_InGrip(p, pt.x))
            {
                SetCursor(LoadCursorW(NULL, (LPCWSTR)IDC_SIZEWE));
                return 1;
            }
            if (pt.y < p->headerPx)
            {
                SetCursor(LoadCursorW(NULL, (LPCWSTR)IDC_SIZEALL));
                return 1;
            }
            /* Активная hover-лупа или Ctrl над увеличенным превью — режим pan */
            {
                LayoutItem *li = Panel_HitTest(p, pt.x, pt.y);
                BOOL ctrlHeld = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
                if (li && !li->isStrip && PtIn(pt.x, pt.y, &li->preview) &&
                    (li->win->hwnd == p->hoverZoomHwnd ||
                     (ctrlHeld && li->win->zoom > 1.001)))
                {
                    SetCursor(LoadCursorW(NULL, (LPCWSTR)IDC_SIZEALL));
                    return 1;
                }
            }
        }
        break;

    case WM_DPICHANGED:
        Panel_SetDpi(p, (UINT)LOWORD(wp));
        Panel_UpdatePosition(p);
        return 0;

    case WM_CLOSE:
        /* Внешний сигнал завершения — выходит всё приложение. */
        App_Exit();
        return 0;

    case WM_DESTROY:
        ScrollBar_Destroy(&p->sbar);
        Thumbs_Dispose(&p->thumbs);
        AppBar_Unregister(&p->appbar);
        p->hwnd = NULL;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
        return 0;
    }

    return DefWindowProcW(hwnd, msg, wp, lp);
}

static void Panel_Menu(Panel *p)
{
    HMENU menu = CreatePopupMenu();
    POINT pt;
    UINT cmd;

    if (!menu)
        return;

    AppendMenuW(menu, MF_STRING | (p->cfg->edge == DOCK_LEFT ? MF_CHECKED : 0),
                CMD_DOCK_LEFT, STR_S(L"Слева", L"Dock left"));
    AppendMenuW(menu, MF_STRING | (p->cfg->edge == DOCK_RIGHT ? MF_CHECKED : 0),
                CMD_DOCK_RIGHT, STR_S(L"Справа", L"Dock right"));
    AppendMenuW(menu, MF_SEPARATOR, 0, NULL);
    AppendMenuW(menu, MF_STRING, CMD_HIDE,
                STR_S(L"Скрыть на этом мониторе", L"Hide on this display"));
    AppendMenuW(menu, MF_STRING | (Autostart_IsEnabled() ? MF_CHECKED : 0), CMD_AUTOSTART,
                STR_S(L"Автозапуск", L"Start with Windows"));
    AppendMenuW(menu, MF_SEPARATOR, 0, NULL);
    AppendMenuW(menu, MF_STRING, CMD_EXIT, STR_S(L"Выход", L"Exit"));

    GetCursorPos(&pt);
    /* Без этого меню у неактивируемого окна не закрывается кликом мимо. */
    SetForegroundWindow(p->hwnd);
    cmd = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON,
                         pt.x, pt.y, 0, p->hwnd, NULL);
    DestroyMenu(menu);
    PostMessageW(p->hwnd, WM_NULL, 0, 0);

    switch (cmd)
    {
    case CMD_DOCK_LEFT:
        Panel_SetEdge(p, DOCK_LEFT);
        break;
    case CMD_DOCK_RIGHT:
        Panel_SetEdge(p, DOCK_RIGHT);
        break;
    case CMD_HIDE:
        /* Уничтожит эту панель — дальше по стеку к её полям не обращаемся */
        App_SetEnabled(Panel_GetDevice(p), FALSE);
        break;
    case CMD_AUTOSTART:
        Autostart_Toggle();
        break;
    case CMD_EXIT:
        App_Exit();
        break;
    }
}

static void Panel_SetEdge(Panel *p, DockEdge edge)
{
    if (p->cfg->edge == edge)
        return;
    p->cfg->edge = edge;
    Panel_UpdatePosition(p);
    Cfg_Save();
}
