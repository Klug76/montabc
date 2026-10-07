#include "montabc.h"
#include "app.h"
#include "config.h"
#include "debug.h"
#include "icons.h"
#include "monitors.h"
#include "panel.h"
#include "strings.h"
#include "switch.h"
#include "tracker.h"
#include "tray.h"

#define HOST_CLASS L"montabc.host"
#define SINGLE_INSTANCE_MUTEX L"Local\\montabc.single-instance"

/* AppBar self-healing: after sleep/hibernation the shell may reset the
   work area; the check is delayed — it needs time to settle. */
#define HEAL_TIMER_ID 1
#define HEAL_DELAY_MS 1500
#define HEAL_MAX_ATTEMPTS 3

/* Icon rebuild: events arrive in bursts — debounced by a timer on the host. */
#define ICON_REBUILD_TIMER_ID 2
#define ICON_REBUILD_DELAY_MS 500

typedef struct
{
    WCHAR device[CCHDEVICENAME];
    Panel *panel;
} PanelSlot;

static struct
{
    HINSTANCE hInst;
    HWND hwndHost;
    UINT taskbarCreated;
    int healAttempts;
    DisplayInfo displays[APP_MAX_MONITORS];
    int displayCount;
    PanelSlot panels[APP_MAX_MONITORS];
    int panelCount;
    BOOL exiting;
    BOOL hidden; /* quick "hide all" from the tray: monitor settings kept intact */
} S;

static Switcher g_switch;

/* System small-icon size; a change triggers the icon rebuild. */
static int s_smIconX = -1;
static int s_smIconY = -1;

void App_Activate(HWND hwnd)
{
    Sw_Activate(&g_switch, hwnd);
}

void App_ActivateMostRecentExcept(HWND except)
{
    Sw_ActivateMostRecentExcept(&g_switch, except);
}

/* Automatic history-based activation never targets minimized windows. */
static BOOL App_IsEligible(HWND hwnd)
{
    WindowItem *item = Trk_Find(hwnd);
    return item && !item->minimized;
}

static void App_OnForeground(HWND hwnd)
{
    Sw_OnForegroundChanged(&g_switch, hwnd);
}

static const DisplayInfo *App_FindDisplay(HMONITOR mon)
{
    int i;
    for (i = 0; i < S.displayCount; i++)
        if (S.displays[i].hMon == mon)
            return &S.displays[i];
    return NULL;
}

/* A panel was dropped onto another monitor: if it is free — move there;
   an occupied one already has its own panel, no need to touch it. */
void App_MovePanel(Panel *panel, HMONITOR target, int cursorX)
{
    const DisplayInfo *display = App_FindDisplay(target);
    MonitorCfg *cfg;

    if (!display)
        return;

    cfg = Cfg_For(display->device);
    if (!cfg || cfg->enabled)
        return;

    cfg->enabled = TRUE;
    cfg->edge = cursorX < (display->rc.left + display->rc.right) / 2 ? DOCK_LEFT
                                                                     : DOCK_RIGHT;
    cfg = Cfg_For(Panel_GetDevice(panel));
    if (cfg)
        cfg->enabled = FALSE;
    Cfg_Save();
    App_RefreshDisplays(); /* destroys panel — the caller must not use it afterwards */
}

void App_Exit(void)
{
    int i;

    if (S.exiting)
        return;
    S.exiting = TRUE;

    Trk_Stop();
    Icon_Shutdown();
    Cfg_Save();

    for (i = 0; i < S.panelCount; i++)
        Panel_Destroy(S.panels[i].panel);
    S.panelCount = 0;

    Tray_Destroy();

    if (S.hwndHost)
        DestroyWindow(S.hwndHost);

    PostQuitMessage(0);
}

static Panel *FindPanel(const WCHAR *device)
{
    int i;
    for (i = 0; i < S.panelCount; i++)
        if (lstrcmpW(S.panels[i].device, device) == 0)
            return S.panels[i].panel;
    return NULL;
}

static void App_OnTrackerChanged(void)
{
    int i;
    for (i = 0; i < S.panelCount; i++)
        Panel_Invalidate(S.panels[i].panel);
}

/* Rebuilds the set of panels to match the current monitors and settings. */
void App_RefreshDisplays(void)
{
    int i, j;
    MonitorCfg *cfg;

    Monitors_Enum(S.displays, APP_MAX_MONITORS, &S.displayCount);

    /* Monitor removed, panel disabled, or everything hidden — the window is not needed */
    for (i = 0; i < S.panelCount;)
    {
        BOOL alive = FALSE;
        for (j = 0; j < S.displayCount; j++)
            if (lstrcmpW(S.displays[j].device, S.panels[i].device) == 0)
            {
                alive = TRUE;
                break;
            }

        cfg = Cfg_For(S.panels[i].device);
        if (S.hidden || !alive || !cfg || !cfg->enabled)
        {
            Panel_Destroy(S.panels[i].panel);
            S.panels[i] = S.panels[--S.panelCount];
            continue;
        }
        i++;
    }

    for (j = 0; j < S.displayCount; j++)
    {
        Panel *existing = FindPanel(S.displays[j].device);
        if (existing)
        {
            Panel_SetDisplay(existing, &S.displays[j]);
            continue;
        }

        cfg = Cfg_For(S.displays[j].device);
        if (!S.hidden && cfg && cfg->enabled && S.panelCount < APP_MAX_MONITORS &&
            Panel_Create(S.hInst, &S.displays[j], cfg, &S.panels[S.panelCount].panel))
        {
            lstrcpynW(S.panels[S.panelCount].device, S.displays[j].device, CCHDEVICENAME);
            S.panelCount++;
        }
    }

    Tray_SyncIcon();
}

/* Left click on the tray icon: hide/restore panels on all monitors.
   Monitor settings are untouched — everything is restored as it was. */
void App_ToggleHidden(void)
{
    S.hidden = !S.hidden;
    App_RefreshDisplays();
}

BOOL App_IsHidden(void)
{
    return S.hidden;
}

void App_SetEnabled(const WCHAR *device, BOOL enabled)
{
    MonitorCfg *mon = Cfg_For(device);
    /* Enabling a panel from the menu is an explicit request to show it, clears "hide all" */
    BOOL unhide = enabled && S.hidden;

    if (!mon || (mon->enabled == enabled && !unhide))
        return;

    mon->enabled = enabled;
    if (unhide)
        S.hidden = FALSE;
    Cfg_Save();
    App_RefreshDisplays();
}

void App_SetEdge(const WCHAR *device, DockEdge edge)
{
    MonitorCfg *mon = Cfg_For(device);
    Panel *panel;

    if (!mon || mon->edge == edge)
        return;
    mon->edge = edge;
    Cfg_Save();
    panel = FindPanel(device);
    if (panel)
        Panel_UpdatePosition(panel);
}

int App_DisplayCount(void)
{
    return S.displayCount;
}

const DisplayInfo *App_Display(int index)
{
    return &S.displays[index];
}

void App_ReregisterAppBars(void)
{
    int i;
    for (i = 0; i < S.panelCount; i++)
        Panel_Reregister(S.panels[i].panel);
}

/* Repairs panels whose area was taken over by the work area. */
BOOL App_HealAppBars(void)
{
    BOOL healed = FALSE;
    int i;
    for (i = 0; i < S.panelCount; i++)
        if (Panel_IsWorkAreaBroken(S.panels[i].panel))
        {
            Panel_Reregister(S.panels[i].panel);
            healed = TRUE;
        }
    return healed;
}

static void Host_ScheduleHeal(BOOL resetAttempts)
{
    if (resetAttempts)
        S.healAttempts = 0;
    SetTimer(S.hwndHost, HEAL_TIMER_ID, HEAL_DELAY_MS, NULL);
}

/* Calling SetTimer again with the same ID restarts the countdown — burst debounce. */
void App_ScheduleIconRebuild(void)
{
    LOG0(2, L"rebuild scheduled");
    if (S.hwndHost)
        SetTimer(S.hwndHost, ICON_REBUILD_TIMER_ID, ICON_REBUILD_DELAY_MS, NULL);
}

static LRESULT CALLBACK Host_WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    if (msg == S.taskbarCreated && S.taskbarCreated)
    {
        /* Explorer restarted and took all appbars with it — re-register. */
        LOG0(1, L"TaskbarCreated: explorer restarted");
        App_ReregisterAppBars();
        return 0;
    }

    switch (msg)
    {
    case WM_DISPLAYCHANGE:
        LOG0(1, L"WM_DISPLAYCHANGE");
        App_RefreshDisplays();
        Trk_RefreshMonitors();
        Host_ScheduleHeal(TRUE);
        return 0;

    case WM_POWERBROADCAST:
        /* Resume from sleep/hibernation: the shell may reset the work area. */
        if (wp == PBT_APMRESUMESUSPEND || wp == PBT_APMRESUMEAUTOMATIC)
        {
            LOG0(1, L"WM_POWERBROADCAST: resume");
            Host_ScheduleHeal(TRUE);
            return TRUE;
        }
        break;

    case WM_SETTINGCHANGE:
        /* Work area reset by someone else. Our own ABM_SETPOS also broadcasts
           SPI_SETWORKAREA — do not reset the counter, the check is passive. */
        if (wp == SPI_SETWORKAREA)
        {
            LOG0(1, L"WM_SETTINGCHANGE SPI_SETWORKAREA");
            Host_ScheduleHeal(FALSE);
            return 0;
        }
        /* Applications redraw small icons for the new system
           size — the cache is stale. */
        if (GetSystemMetrics(SM_CXSMICON) != s_smIconX ||
            GetSystemMetrics(SM_CYSMICON) != s_smIconY)
        {
            LOG(1, L"WM_SETTINGCHANGE: smicon %dx%d -> %dx%d",
                s_smIconX, s_smIconY,
                GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON));
            s_smIconX = GetSystemMetrics(SM_CXSMICON);
            s_smIconY = GetSystemMetrics(SM_CYSMICON);
            App_ScheduleIconRebuild();
        }
        break;

    case WM_THEMECHANGED:
        /* Icons of some applications depend on the theme — reloading. */
            LOG0(1, L"WM_THEMECHANGED");
        App_ScheduleIconRebuild();
        break;

    case WM_TIMER:
        if (wp == ICON_REBUILD_TIMER_ID)
        {
            KillTimer(hwnd, ICON_REBUILD_TIMER_ID);
            LOG0(1, L"rebuild timer fired");
            Icon_RebuildAll();
            return 0;
        }
        if (wp == HEAL_TIMER_ID)
        {
            BOOL healed;
            KillTimer(hwnd, HEAL_TIMER_ID);
            /* Our own re-registration changes the work area and lands here again;
               the limit guards against the shell never giving the strip back. */
            if (S.healAttempts < HEAL_MAX_ATTEMPTS)
            {
                healed = App_HealAppBars();
                LOG(1, L"heal: %s (attempt=%d)",
                    healed ? L"fixed" : L"clean", S.healAttempts);
                if (healed)
                    S.healAttempts++;
                else
                    S.healAttempts = 0;
            }
            return 0;
        }
        break;

    case WM_ENDSESSION:
        if (wp)
            Cfg_Save();
        return 0;

    case WM_CLOSE:
        App_Exit();
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

int WINAPI WinMain(HINSTANCE hInst, HINSTANCE hPrev, LPSTR cmdLine, int nShow)
{
    HANDLE mutex;
    WNDCLASSEXW wc;
    MSG msg;

    /* EntryPoint=WinMain without CRT: on x64 the loader puts the thread
       parameter, not hInstance, in RCX — get the module handle ourselves. */
    hInst = GetModuleHandleW(NULL);

    (void)hPrev;
    (void)cmdLine;
    (void)nShow;

    mutex = CreateMutexW(NULL, TRUE, SINGLE_INSTANCE_MUTEX);
    if (mutex && GetLastError() == ERROR_ALREADY_EXISTS)
        return 0;

    Str_Init();
    Log_Init();
    Cfg_Load();
    S.hInst = hInst;

    wc.cbSize = sizeof(wc);
    wc.style = 0;
    wc.lpfnWndProc = Host_WndProc;
    wc.cbClsExtra = 0;
    wc.cbWndExtra = 0;
    wc.hInstance = hInst;
    wc.hIcon = NULL;
    wc.hCursor = NULL;
    wc.hbrBackground = NULL;
    wc.lpszMenuName = NULL;
    wc.lpszClassName = HOST_CLASS;
    wc.hIconSm = NULL;
    RegisterClassExW(&wc);

    /* WndProc starts firing inside CreateWindowExW, so its state must be ready */
    S.taskbarCreated = RegisterWindowMessageW(L"TaskbarCreated");
    s_smIconX = GetSystemMetrics(SM_CXSMICON);
    s_smIconY = GetSystemMetrics(SM_CYSMICON);

    S.hwndHost = CreateWindowExW(WS_EX_TOOLWINDOW, HOST_CLASS, APP_NAME, WS_POPUP,
                                 0, 0, 0, 0, NULL, NULL, hInst, NULL);

    Trk_OnChanged = App_OnTrackerChanged;
    Trk_OnForeground = App_OnForeground;
    g_switch.IsEligible = App_IsEligible;
    Tray_Create(hInst);
    App_RefreshDisplays();
    Trk_Start();
    Sw_OnForegroundChanged(&g_switch, g_foreground);

    while (GetMessageW(&msg, NULL, 0, 0) > 0)
    {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return 0;
}
