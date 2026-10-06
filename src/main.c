#include "montabc.h"
#include "app.h"
#include "config.h"
#include "monitors.h"
#include "panel.h"
#include "strings.h"
#include "switch.h"
#include "tracker.h"
#include "tray.h"

#define HOST_CLASS L"montabc.host"
#define SINGLE_INSTANCE_MUTEX L"Local\\montabc.single-instance"

/* Самолечение appbar'ов: shell после сна/гибернации может сбросить
   рабочую область; проверка откладывается — ему нужно время устаканиться. */
#define HEAL_TIMER_ID 1
#define HEAL_DELAY_MS 1500
#define HEAL_MAX_ATTEMPTS 3

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
    BOOL hidden; /* быстрое «скрыть всё» из трея: настройки мониторов целы */
} S;

static Switcher g_switch;

void App_Activate(HWND hwnd)
{
    Sw_Activate(&g_switch, hwnd);
}

void App_ActivateMostRecentExcept(HWND except)
{
    Sw_ActivateMostRecentExcept(&g_switch, except);
}

/* Автопереходы по истории не идут в свёрнутые окна. */
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

/* Панель бросили на другой монитор: если он свободен — переезжаем туда,
   у занятого своя панель уже есть, и трогать её незачем. */
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
    App_RefreshDisplays(); /* уничтожит panel — вызывающий уже не обращается к ней */
}

void App_Exit(void)
{
    int i;

    if (S.exiting)
        return;
    S.exiting = TRUE;

    Trk_Stop();
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

/* Пересобирает набор панелей под текущие мониторы и настройки. */
void App_RefreshDisplays(void)
{
    int i, j;
    MonitorCfg *cfg;

    Monitors_Enum(S.displays, APP_MAX_MONITORS, &S.displayCount);

    /* Монитор отключили, панель выключили или скрыли всё — окно не нужно */
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

/* Левый клик по иконке в трее: убрать/вернуть панели на всех мониторах.
   Настройки мониторов не трогаются — при возврате всё как было. */
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
    /* Включение панели из меню — явная просьба её показать, снимает «скрыть всё» */
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

/* Починка панелей, под которые залезла рабочая область. */
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

static LRESULT CALLBACK Host_WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    if (msg == S.taskbarCreated && S.taskbarCreated)
    {
        /* Explorer перезапустился и забыл все appbar'ы — регистрируемся заново. */
        App_ReregisterAppBars();
        return 0;
    }

    switch (msg)
    {
    case WM_DISPLAYCHANGE:
        App_RefreshDisplays();
        Trk_RefreshMonitors();
        Host_ScheduleHeal(TRUE);
        return 0;

    case WM_POWERBROADCAST:
        /* Выход из сна/гибернации: shell может сбросить work area. */
        if (wp == PBT_APMRESUMESUSPEND || wp == PBT_APMRESUMEAUTOMATIC)
        {
            Host_ScheduleHeal(TRUE);
            return TRUE;
        }
        break;

    case WM_SETTINGCHANGE:
        /* Чужой сброс рабочей области. Свой ABM_SETPOS тоже рассылает
           SPI_SETWORKAREA — счётчик не сбрасываем, проверка пассивна. */
        if (wp == SPI_SETWORKAREA)
        {
            Host_ScheduleHeal(FALSE);
            return 0;
        }
        break;

    case WM_TIMER:
        if (wp == HEAL_TIMER_ID)
        {
            KillTimer(hwnd, HEAL_TIMER_ID);
            /* Своя перерегистрация меняет work area и снова приходит сюда;
               лимит — на случай, если shell полосу так и не отдаёт. */
            if (S.healAttempts < HEAL_MAX_ATTEMPTS && App_HealAppBars())
                S.healAttempts++;
            else if (S.healAttempts < HEAL_MAX_ATTEMPTS)
                S.healAttempts = 0;
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

    /* EntryPoint=WinMain без CRT: на x64 загрузчик кладёт в RCX параметр
       потока, а не hInstance — модуль берём сами. */
    hInst = GetModuleHandleW(NULL);

    (void)hPrev;
    (void)cmdLine;
    (void)nShow;

    mutex = CreateMutexW(NULL, TRUE, SINGLE_INSTANCE_MUTEX);
    if (mutex && GetLastError() == ERROR_ALREADY_EXISTS)
        return 0;

    Str_Init();
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

    S.hwndHost = CreateWindowExW(WS_EX_TOOLWINDOW, HOST_CLASS, APP_NAME, WS_POPUP,
                                 0, 0, 0, 0, NULL, NULL, hInst, NULL);
    S.taskbarCreated = RegisterWindowMessageW(L"TaskbarCreated");

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
