#include "tracker.h"
#include "icons.h"
#include "debug.h"
#include "util.h"
#include <dwmapi.h>

static WindowItem *s_items;
static int s_count;
static int s_cap;
static HWINEVENTHOOK s_hooks[6];
static int s_hookCount;
static WCHAR s_titleBuf[TRK_TITLE_MAX];

HWND g_foreground;
void (*Trk_OnChanged)(void);
void (*Trk_OnForeground)(HWND hwnd);

void Trk_NotifyChanged(void)
{
    if (Trk_OnChanged)
        Trk_OnChanged();
}

static void Changed(void)
{
    Trk_NotifyChanged();
}

static WindowItem *Find(HWND hwnd)
{
    int i;
    for (i = 0; i < s_count; i++)
        if (s_items[i].hwnd == hwnd)
            return &s_items[i];
    return NULL;
}

/* Index of the first strip — boundary between the "live/minimized" sections. */
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
    if (!Util_Grow((void **)&s_items, &s_cap, s_count + 1, TRK_MAX_ITEMS,
                   sizeof(WindowItem), L"trk"))
    {
        LOG(1, L"trk: item dropped, cap=%d", TRK_MAX_ITEMS);
        return;
    }
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
    LOG(2, L"remove %08x: slot=%d", DBG_HEX(hwnd), item->iconSlot);
    Icon_ReleaseSlot(item->iconSlot);
    RemoveAt((int)(item - s_items));
    Changed();
}

/* A popup notification (toast, OSD), not a working window; the signs
   are deliberately conservative — losing a real window is worse than an extra tab. */
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

/* Active filter — live Win11 22621.6199 predicate, ReconAddsToTray2 from
   tests\hooklist.c (see experimental\riddle-solved.md): the shell-hook sender's
   classic checks + RecoverWindowsProc + XAML taskbar band table; APPWINDOW
   overrides TOOLWINDOW/NOACTIVATE and owner. On top — local rejections
   P7 (ITaskList_Deleted, CoreWindow) and cloaked: the filmstrip shows the
   current virtual desktop. */
static BOOL ReconAddsToTray2(HWND hwnd)
{
    wchar_t cls[64];
    LONG_PTR ex;
    DWORD band;
    BOOL haveCls;
    int cloaked = 0;

    if (!IsWindowVisible(hwnd))
        return FALSE;

    /* Top-level only: winevent sends SHOW/NAME for child windows too, while the
       Win11 predicates (shell hook/EnumWindows) never see them at all. */
    if (GetAncestor(hwnd, GA_PARENT) != GetDesktopWindow())
        return FALSE;
    if (GetWindowTextLengthW(hwnd) == 0)
        return FALSE;

    /* Button removed via ITaskbarList::DeleteTab — the property is set by the
       shell itself (verified in tests\deltab). */
    if (GetPropW(hwnd, L"ITaskList_Deleted"))
        return FALSE;

    haveCls = GetClassNameW(hwnd, cls, 64) != 0;
    /* A "bare" CoreWindow is shell infrastructure (Start, search, TextInputHost):
       the live Win11 path filters it outside the predicate — v2 blind spot. */
    if (haveCls && !lstrcmpW(cls, L"Windows.UI.Core.CoreWindow"))
        return FALSE;
    /* Ghost duplicate of a hung window — IsGhostWindowClass rejects it in the
       WCREATED branch of Taskbar.dll (0x13C470). */
    if (haveCls && !lstrcmpW(cls, L"Ghost"))
        return FALSE;

    ex = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
    /* Hook sender: TOOLWINDOW and NOACTIVATE reject, APPWINDOW overrides both. */
    if (!(ex & WS_EX_APPWINDOW) && (ex & (WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE)))
        return FALSE;
    /* IsOwnedWindow/sender: owned without APPWINDOW is not announced. */
    if (!(ex & WS_EX_APPWINDOW) && GetWindow(hwnd, GW_OWNER))
        return FALSE;

    /* IsWindowNotDesktopOrTray */
    if (hwnd == GetShellWindow())
        return FALSE;
    if (haveCls && (!lstrcmpW(cls, L"Progman") || !lstrcmpW(cls, L"WorkerW") ||
                    !lstrcmpW(cls, L"Shell_TrayWnd")))
        return FALSE;

    /* IsValidDesktopZOrderBand; the shim yields ZBID_DESKTOP when the API is absent. */
    GetWindowBand(hwnd, &band);
    if (band != ZBID_DESKTOP && band != ZBID_UIACCESS)
        return FALSE;

    DwmGetWindowAttribute(hwnd, DWMWA_CLOAKED, &cloaked, sizeof(cloaked));
    return cloaked == 0;
}

/* Previous filter (classic alt-tab + cloaked-window rejection): replaced by
   ReconAddsToTray2, not called — kept for comparison and rollback. */
static BOOL IsAppWindow(HWND hwnd)
{
    wchar_t cls[64];
    LONG_PTR exStyle, walk, probe;
    int cloaked = 0;

    if (!IsWindowVisible(hwnd))
        return FALSE;
    if (GetWindowTextLengthW(hwnd) == 0)
        return FALSE;

    /* Button removed via ITaskbarList::DeleteTab — the property is set by the
       shell itself (verified in tests\deltab). */
    if (GetPropW(hwnd, L"ITaskList_Deleted"))
        return FALSE;

    /* A "bare" CoreWindow is shell infrastructure (Start, search): it enters the
       filmstrip via UNCLOAKED the moment the menu opens. UWP apps are represented
       by the ApplicationFrameWindow host, so the class can be safely rejected whole. */
    if (GetClassNameW(hwnd, cls, 64) && !lstrcmpW(cls, L"Windows.UI.Core.CoreWindow"))
        return FALSE;

    exStyle = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
    if ((exStyle & WS_EX_TOOLWINDOW) != 0 && (exStyle & WS_EX_APPWINDOW) == 0)
        return FALSE;

    if (IsNotification(hwnd, exStyle))
        return FALSE;

    /* Alt-tab rule (Raymond Chen): in an owned chain the root owner is shown. */
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

    /* A minimized window physically moves to (-32000,-32000): its monitor is
       the one where the window will be restored (rcNormalPosition). */
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

    /* A minimized window's client area is the iconic bar ~160×28 */
    if (IsIconic(item->hwnd))
        return FALSE;
    if (!GetClientRect(item->hwnd, &rc))
        return FALSE;
    w = rc.right - rc.left;
    h = rc.bottom - rc.top;
    if (w <= 0 || h <= 0)
        return FALSE;

    aspect = (double)w / (double)h;
    /* Transitional restore geometry gives wild aspect ratios — ignore */
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
    /* The icon loads asynchronously at first draw (icons.c). */
    item->iconSlot = -1;
    item->iconPrio = ICON_PRIO_NONE;
    item->iconReq = 0;
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

    if (Find(hwnd) || !ReconAddsToTray2(hwnd))
        return;

    CreateItem(hwnd, &item);
    /* New live windows go on top; new minimized ones first among the strips */
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
        UpdateAspect(item); /* by MINIMIZEEND the window is in its final geometry */
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

    /* Only the windows themselves (OBJID_WINDOW == 0), not child accessibility objects. */
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
            /* Many apps set the title only after the window is shown —
               only then does it pass the filter. */
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
            /* Dragging a window between monitors moves its tile to another panel */
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
    if (ReconAddsToTray2(hwnd))
        AddItem(hwnd);
    return TRUE;
}

void Trk_Start(void)
{
    static const DWORD ranges[6][2] = {
        {EVENT_SYSTEM_FOREGROUND, EVENT_SYSTEM_FOREGROUND},
        {EVENT_SYSTEM_MINIMIZESTART, EVENT_SYSTEM_MINIMIZEEND},
        /* 0x8001..0x8003: DESTROY, SHOW, HIDE as a single range */
        {EVENT_OBJECT_DESTROY, EVENT_OBJECT_HIDE},
        {EVENT_OBJECT_NAMECHANGE, EVENT_OBJECT_NAMECHANGE},
        /* 0x8017..0x8018: CLOAKED, UNCLOAKED (UWP, virtual desktops) */
        {EVENT_OBJECT_CLOAKED, EVENT_OBJECT_UNCLOAKED},
        /* Resizing the source changes the tile aspect */
        {EVENT_OBJECT_LOCATIONCHANGE, EVENT_OBJECT_LOCATIONCHANGE},
    };
    int i;

    for (i = 0; i < 6; i++)
        s_hooks[s_hookCount++] = SetWinEventHook(
            ranges[i][0], ranges[i][1], NULL, WinEventProc, 0, 0,
            WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS);

    /* EnumWindows walks the z-order top-down; the filmstrip has two sections:
       live windows on top, minimized strips at the bottom. */
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
    s_count = 0;
    Util_Free(s_items);
    s_items = NULL;
    s_cap = 0;
}

void Trk_ResetIcons(void)
{
    int i;

    for (i = 0; i < s_count; i++)
    {
        s_items[i].iconSlot = -1;
        s_items[i].iconPrio = ICON_PRIO_NONE;
        s_items[i].iconReq = 0;
    }
    Changed();
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
