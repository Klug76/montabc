#include "tracker.h"
#include "icons.h"
#include "util.h"
#include <dwmapi.h>

static WindowItem s_items[TRK_MAX_ITEMS];
static int s_count;
static HWINEVENTHOOK s_hooks[6];
static int s_hookCount;
static WCHAR s_titleBuf[TRK_TITLE_MAX];

HWND g_foreground;
void (*Trk_OnChanged)(void);
void (*Trk_OnForeground)(HWND hwnd);

static void Changed(void)
{
    if (Trk_OnChanged)
        Trk_OnChanged();
}

static WindowItem *Find(HWND hwnd)
{
    int i;
    for (i = 0; i < s_count; i++)
        if (s_items[i].hwnd == hwnd)
            return &s_items[i];
    return NULL;
}

/* Индекс первой полоски — граница секций «живые/свёрнутые». */
static int StripBoundary(void)
{
    int i;
    for (i = 0; i < s_count; i++)
        if (s_items[i].minimized)
            return i;
    return s_count;
}

static void InsertAt(int idx, const WindowItem *item)
{
    int i;
    for (i = s_count; i > idx; i--)
        s_items[i] = s_items[i - 1];
    s_items[idx] = *item;
    s_count++;
}

static void RemoveAt(int idx)
{
    for (; idx < s_count - 1; idx++)
        s_items[idx] = s_items[idx + 1];
    s_count--;
}

static void Reposition(int idx)
{
    WindowItem item = s_items[idx];
    RemoveAt(idx);
    InsertAt(StripBoundary(), &item);
}

static void Remove(HWND hwnd)
{
    WindowItem *item = Find(hwnd);
    if (!item)
        return;
    if (item->ownsIcon && item->icon)
        DestroyIcon(item->icon);
    RemoveAt((int)(item - s_items));
    Changed();
}

/* Всплывающее уведомление (тост, OSD), а не рабочее окно; признаки
   нарочно консервативные — потерять настоящее окно хуже лишнего таба. */
static BOOL IsSmallerThanHalfMonitor(HWND hwnd)
{
    RECT rc;
    HMONITOR mon;
    MONITORINFO mi;

    if (!GetWindowRect(hwnd, &rc))
        return FALSE;
    mon = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
    mi.cbSize = sizeof(mi);
    if (!GetMonitorInfoW(mon, &mi))
        return FALSE;
    return (rc.right - rc.left) * 2 < mi.rcMonitor.right - mi.rcMonitor.left &&
           (rc.bottom - rc.top) * 2 < mi.rcMonitor.bottom - mi.rcMonitor.top;
}

static BOOL IsNotification(HWND hwnd, LONG_PTR exStyle)
{
    LONG_PTR style;
    const LONG_PTR frameBits = WS_THICKFRAME | WS_MINIMIZEBOX | WS_MAXIMIZEBOX | WS_SYSMENU;

    if (exStyle & WS_EX_NOACTIVATE)
        return TRUE;

    style = GetWindowLongPtrW(hwnd, GWL_STYLE);
    if ((style & frameBits) != 0 || (style & WS_CAPTION) == WS_CAPTION)
        return FALSE;

    return IsSmallerThanHalfMonitor(hwnd);
}

/* Классический alt-tab-фильтр + отсев cloaked-окон (UWP, виртуальные столы). */
static BOOL IsAppWindow(HWND hwnd)
{
    LONG_PTR exStyle, walk, probe;
    int cloaked = 0;

    if (!IsWindowVisible(hwnd))
        return FALSE;
    if (GetWindowTextLengthW(hwnd) == 0)
        return FALSE;

    exStyle = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
    if ((exStyle & WS_EX_TOOLWINDOW) != 0 && (exStyle & WS_EX_APPWINDOW) == 0)
        return FALSE;

    if (IsNotification(hwnd, exStyle))
        return FALSE;

    /* Правило alt-tab (Raymond Chen): у owned-цепочки показывается корневой владелец. */
    if ((exStyle & WS_EX_APPWINDOW) == 0)
    {
        walk = 0;
        probe = (LONG_PTR)GetAncestor(hwnd, GA_ROOTOWNER);
        while (probe != walk)
        {
            walk = probe;
            probe = (LONG_PTR)GetLastActivePopup((HWND)walk);
            if (IsWindowVisible((HWND)probe))
                break;
        }
        if (walk != (LONG_PTR)hwnd)
            return FALSE;
    }

    DwmGetWindowAttribute(hwnd, DWMWA_CLOAKED, &cloaked, sizeof(cloaked));
    return cloaked == 0;
}

static HMONITOR MonitorOf(HWND hwnd)
{
    WINDOWPLACEMENT placement;

    /* Свёрнутое окно физически уезжает в (-32000,-32000): его монитор —
       тот, где окно развернётся обратно (rcNormalPosition). */
    if (IsIconic(hwnd))
    {
        placement.length = sizeof(placement);
        if (GetWindowPlacement(hwnd, &placement))
            return MonitorFromRect(&placement.rcNormalPosition, MONITOR_DEFAULTTONEAREST);
    }
    return MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
}

static BOOL UpdateMonitor(WindowItem *item)
{
    HMONITOR mon = MonitorOf(item->hwnd);
    if (mon == item->monitor)
        return FALSE;
    item->monitor = mon;
    return TRUE;
}

static BOOL UpdateAspect(WindowItem *item)
{
    RECT rc;
    double aspect;
    int w, h;

    /* У свёрнутого окна клиентская область — «иконик»-полоса ~160×28 */
    if (IsIconic(item->hwnd))
        return FALSE;
    if (!GetClientRect(item->hwnd, &rc))
        return FALSE;
    w = rc.right - rc.left;
    h = rc.bottom - rc.top;
    if (w <= 0 || h <= 0)
        return FALSE;

    aspect = (double)w / (double)h;
    /* Переходная геометрия разворачивания даёт дикие пропорции — игнорируем */
    if (aspect < 0.2 || aspect > 4.5)
        return FALSE;
    if (aspect - item->aspect < 0.01 && item->aspect - aspect < 0.01)
        return FALSE;

    item->aspect = aspect;
    return TRUE;
}

static void CreateItem(HWND hwnd, WindowItem *item)
{
    item->hwnd = hwnd;
    if (GetWindowTextW(hwnd, item->title, TRK_TITLE_MAX) <= 0)
        item->title[0] = L'\0';
    item->icon = Icon_GetWindow(hwnd, &item->ownsIcon);
    item->monitor = NULL;
    item->minimized = IsIconic(hwnd);
    item->aspect = 16.0 / 10.0;
    item->zoom = 1.0;
    item->centerX = 0.5;
    item->centerY = 0.5;
    UpdateAspect(item);
    UpdateMonitor(item);
}

static void AddItem(HWND hwnd)
{
    WindowItem item;
    CreateItem(hwnd, &item);
    InsertAt(s_count, &item);
}

static void TryAdd(HWND hwnd)
{
    WindowItem item;

    if (Find(hwnd) || !IsAppWindow(hwnd))
        return;

    CreateItem(hwnd, &item);
    /* Новые живые — сверху; новые свёрнутые — первыми среди полосок */
    InsertAt(item.minimized ? StripBoundary() : 0, &item);
    Changed();
}

static void SetMinimized(HWND hwnd, BOOL minimized)
{
    WindowItem *item = Find(hwnd);
    if (!item || item->minimized == minimized)
        return;

    item->minimized = minimized;
    if (!minimized)
    {
        UpdateAspect(item); /* к MINIMIZEEND окно уже в финальной геометрии */
        UpdateMonitor(item);
    }
    Reposition((int)(item - s_items));
    Changed();
}

static void CALLBACK WinEventProc(
    HWINEVENTHOOK hook, DWORD ev, HWND hwnd, LONG idObject, LONG idChild,
    DWORD thread, DWORD time)
{
    WindowItem *item;
    BOOL dirty;

    (void)hook;
    (void)thread;
    (void)time;

    /* Только сами окна (OBJID_WINDOW == 0), не дочерние объекты accessibility. */
    if (idObject != 0 || idChild != 0 || !hwnd)
        return;

    switch (ev)
    {
    case EVENT_SYSTEM_FOREGROUND:
        if (g_foreground != hwnd)
        {
            g_foreground = hwnd;
            if (Trk_OnForeground)
                Trk_OnForeground(hwnd);
            Changed();
        }
        break;

    case EVENT_OBJECT_SHOW:
    case EVENT_OBJECT_UNCLOAKED:
        TryAdd(hwnd);
        break;

    case EVENT_OBJECT_NAMECHANGE:
        item = Find(hwnd);
        if (item)
        {
            if (GetWindowTextW(hwnd, s_titleBuf, TRK_TITLE_MAX) > 0 &&
                lstrcmpW(s_titleBuf, item->title) != 0)
            {
                lstrcpynW(item->title, s_titleBuf, TRK_TITLE_MAX);
                Changed();
            }
        }
        else
        {
            /* Многие приложения ставят заголовок уже после показа окна —
               только теперь оно проходит фильтр. */
            TryAdd(hwnd);
        }
        break;

    case EVENT_OBJECT_HIDE:
    case EVENT_OBJECT_DESTROY:
    case EVENT_OBJECT_CLOAKED:
        Remove(hwnd);
        break;

    case EVENT_SYSTEM_MINIMIZESTART:
        SetMinimized(hwnd, TRUE);
        break;

    case EVENT_SYSTEM_MINIMIZEEND:
        SetMinimized(hwnd, FALSE);
        break;

    case EVENT_OBJECT_LOCATIONCHANGE:
        item = Find(hwnd);
        if (item && !item->minimized)
        {
            /* Перетаскивание окна между мониторами переносит тайл в другую панель */
            dirty = UpdateAspect(item);
            dirty |= UpdateMonitor(item);
            if (dirty)
                Changed();
        }
        break;
    }
}

static BOOL CALLBACK EnumProc(HWND hwnd, LPARAM lp)
{
    (void)lp;
    if (IsAppWindow(hwnd))
        AddItem(hwnd);
    return TRUE;
}

void Trk_Start(void)
{
    static const DWORD ranges[6][2] = {
        {EVENT_SYSTEM_FOREGROUND, EVENT_SYSTEM_FOREGROUND},
        {EVENT_SYSTEM_MINIMIZESTART, EVENT_SYSTEM_MINIMIZEEND},
        /* 0x8001..0x8003: DESTROY, SHOW, HIDE одним диапазоном */
        {EVENT_OBJECT_DESTROY, EVENT_OBJECT_HIDE},
        {EVENT_OBJECT_NAMECHANGE, EVENT_OBJECT_NAMECHANGE},
        /* 0x8017..0x8018: CLOAKED, UNCLOAKED (UWP, виртуальные рабочие столы) */
        {EVENT_OBJECT_CLOAKED, EVENT_OBJECT_UNCLOAKED},
        /* Ресайз источника меняет аспект тайла */
        {EVENT_OBJECT_LOCATIONCHANGE, EVENT_OBJECT_LOCATIONCHANGE},
    };
    int i;

    for (i = 0; i < 6; i++)
        s_hooks[s_hookCount++] = SetWinEventHook(
            ranges[i][0], ranges[i][1], NULL, WinEventProc, 0, 0,
            WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS);

    /* EnumWindows идёт сверху z-order; лента двухсекционная: живые вверху,
       свёрнутые полоски внизу. */
    s_count = 0;
    EnumWindows(EnumProc, 0);
    for (i = 0; i < s_count; i++)
        if (s_items[i].minimized)
            Reposition(i);

    g_foreground = GetForegroundWindow();
    Changed();
}

void Trk_Stop(void)
{
    int i;

    for (i = 0; i < s_hookCount; i++)
        UnhookWinEvent(s_hooks[i]);
    s_hookCount = 0;

    for (i = 0; i < s_count; i++)
        if (s_items[i].ownsIcon && s_items[i].icon)
            DestroyIcon(s_items[i].icon);
    s_count = 0;
}

void Trk_RefreshMonitors(void)
{
    BOOL dirty = FALSE;
    int i;

    for (i = 0; i < s_count; i++)
        dirty |= UpdateMonitor(&s_items[i]);
    if (dirty)
        Changed();
}

int Trk_Count(void)
{
    return s_count;
}

WindowItem *Trk_At(int index)
{
    return &s_items[index];
}

WindowItem *Trk_Find(HWND hwnd)
{
    return Find(hwnd);
}

int Trk_IndexOf(const WindowItem *item)
{
    return (int)(item - s_items);
}

void Trk_Move(WindowItem *item, int newIndex)
{
    WindowItem tmp;
    int oldIndex = (int)(item - s_items), i;

    if (oldIndex < 0 || oldIndex >= s_count)
        return;
    newIndex = Util_ClampI(newIndex, 0, s_count - 1);
    if (newIndex == oldIndex)
        return;

    tmp = *item;
    for (i = oldIndex; i < s_count - 1; i++)
        s_items[i] = s_items[i + 1];
    s_count--;
    InsertAt(newIndex, &tmp);
    Changed();
}
