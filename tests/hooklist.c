/* hooklist — окна, приходящие от hook, таблицей с колонками-вердиктами фильтров.
   Сборка (VS Native Tools x64): tests\build-hooklist.bat, т.е.
     cl /nologo /W4 /utf-8 hooklist.c user32.lib dwmapi.lib
   Запуск: hooklist [-shell | -winevent] [-enum | -noenum] [logfile]
     -winevent (по умолчанию) — SetWinEventHook (диапазоны как в src/tracker.c,
       плюс отдельно EVENT_OBJECT_CREATE); первичный фильтр шума: только
       top-level окна (родитель — рабочий стол), child-контролы и message-only
       отсекаются сразу; объём отсева — счётчик «шума» в шапке;
     -shell — RegisterShellHookWindow + коды HSHELL_*;
     -enum — стартовый EnumWindows (z-order, как в montabc); по умолчанию нет;
     logfile — файл лога (по умолчанию hooklist.log): появления окон (первое
       попадание в список, с вердиктами фильтров и стилями) и удаления
       (DESTROY/HIDE/CLOAKED из hook-событий; снятие по IsWindow в
       SweepDead — REMOVED-dead).
   Из списка окно убирается по DESTROY/HIDE/CLOAKED («закрыто», как в
   WinEventProc tracker.c); CREATE/NAMECHANGE добавляют окно только если оно
   уже видно — иначе таблицу бесконечно заселяют невидимые хелперы (IME, OLE).
   Колонки таблицы: все окна от hook | recon v2 — живой фильтр Win11
   (отправитель shell hook + RecoverWindowsProc + band-таблица;
   experimental/riddle-solved.md, ReconAddsToTray2; v1 оставлена в коде для
   истории) | старый фильтр montabc P7 (в src уже заменён на P8 — колонка
   оставлена для сравнения шума P7→P8). Вердикт текущего P8 — в логе
   (p8=keep/DROP), там же стили: vis/ex/own/clk/itld/sz.
   Ячейка двустрочная:
   заголовок / [класс], каждая строка усекается с «...», выравнивание —
   фиксированными колонками (как tab stop); пустые ячейки у окон, которые
   фильтр не пропустил. Свои окна (процесс hooklist, скрытый host, консоль
   с таблицей) в список не попадают. Перерисовка только при изменениях
   (события hook, размер консоли) — в покое вывода нет, текст можно выделять
   и копировать. Выход — Ctrl+C. */

#include <windows.h>
#include <dwmapi.h>
#include <stdio.h>
#include <wchar.h>

/* ===== Реконструкция ShouldAddWindowToTray (Taskbar.dll 22621.6199, по PDB;
   порт из tests/wininfo.c) ===== */

typedef HRESULT(WINAPI *GetWindowBand_t)(HWND, DWORD *);
static GetWindowBand_t g_GetWindowBand;

/* IsValidDesktopZOrderBand: таблица разрешённых ZBID из rdata Taskbar.dll
   (0x27DB54, байт-флаг с шагом 8): band 1 = ZBID_DESKTOP и 2 = ZBID_UIACCESS
   разрешены, 0 и 3+ (immersive/system/lock) — нет. Проп-атом 0xA920 и
   IsShellManagedWindow у стороннего процесса недоступны — не моделируются. */
static BOOL BandAllowed(HWND hwnd, wchar_t *note, int cch)
{
    DWORD band;

    if (!g_GetWindowBand)
    {
        lstrcpynW(note, L"band?", cch);
        return TRUE;
    }
    if (FAILED(g_GetWindowBand(hwnd, &band)))
    {
        lstrcpynW(note, L"band-", cch);
        return TRUE;
    }
    _snwprintf(note, cch, L"band%lu", band);
    return band == 1 || band == 2;
}

/* IsWindowNotDesktopOrTray: hwnd != tray != desktop; атом класса Progman у
   explorer кэширован — здесь заменён сравнением имён классов. */
static BOOL NotDesktopOrTray(HWND hwnd)
{
    wchar_t cls[64];

    if (hwnd == GetShellWindow())
        return FALSE;
    if (GetClassNameW(hwnd, cls, 64) &&
        (!lstrcmpW(cls, L"Progman") || !lstrcmpW(cls, L"WorkerW") ||
         !lstrcmpW(cls, L"Shell_TrayWnd")))
        return FALSE;
    return TRUE;
}

/* v1 (для истории): реконструкция ShouldAddWindowToTray из Taskbar.dll —
   оказалась предикатом МЁРТВОГО пути (enum-инициализация CTaskBand не
   вызывается); см. experimental/riddle-solved.md. */
static BOOL ReconAddsToTray(HWND hwnd, wchar_t *bandNote, int cch)
{
    LONG_PTR ex = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);

    if (!IsWindowVisible(hwnd))
        return FALSE;
    if (ex & WS_EX_TOOLWINDOW)
        return FALSE;
    if (GetWindow(hwnd, GW_OWNER) && !(ex & WS_EX_LAYERED))
        return FALSE;
    if (!NotDesktopOrTray(hwnd))
        return FALSE;
    return BandAllowed(hwnd, bandNote, cch);
}

/* v2: живые уровни Win11 22621.6199 — классика отправителя shell hook
   (xxxEndDeferWindowPosEx) + RecoverWindowsProc (twinui.pcshell), плюс
   band-таблица XAML-таскбара: APPWINDOW перекрывает TOOLWINDOW/NOACTIVATE
   и owner; owned без APPWINDOW не анонсируется; band ∈ {1=DESKTOP,2=UIACCESS}. */
static BOOL ReconAddsToTray2(HWND hwnd, wchar_t *bandNote, int cch)
{
    LONG_PTR ex = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);

    if (!IsWindowVisible(hwnd))
        return FALSE;
    if (!(ex & WS_EX_APPWINDOW) && (ex & (WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE)))
        return FALSE;
    if (!(ex & WS_EX_APPWINDOW) && GetWindow(hwnd, GW_OWNER))
        return FALSE;
    if (!NotDesktopOrTray(hwnd))
        return FALSE;
    return BandAllowed(hwnd, bandNote, cch);
}

/* ===== Фильтр montabc — P8 (порт 1:1 из src/tracker.c) ===== */

/* P8 = ReconAddsToTray2 + локальные отсечения P7 (ITaskList_Deleted,
   CoreWindow) и cloak + C1 top-level/C3 Ghost/C4 заголовок. Проверки
   сгруппированы как в src: стилевой предикат v2 в начале, локальные
   надстройки — после. */
static BOOL IsP8Filter(HWND hwnd)
{
    wchar_t cls[64];
    LONG_PTR ex;
    DWORD band;
    BOOL haveCls;
    int cloaked = 0;

    if (!IsWindowVisible(hwnd))
        return FALSE;
    if (GetAncestor(hwnd, GA_PARENT) != GetDesktopWindow())
        return FALSE;
    if (GetWindowTextLengthW(hwnd) == 0)
        return FALSE;
    if (GetPropW(hwnd, L"ITaskList_Deleted"))
        return FALSE;
    haveCls = GetClassNameW(hwnd, cls, 64) != 0;
    if (haveCls && !lstrcmpW(cls, L"Windows.UI.Core.CoreWindow"))
        return FALSE;
    if (haveCls && !lstrcmpW(cls, L"Ghost"))
        return FALSE;

    ex = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
    if (!(ex & WS_EX_APPWINDOW) && (ex & (WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE)))
        return FALSE;
    if (!(ex & WS_EX_APPWINDOW) && GetWindow(hwnd, GW_OWNER))
        return FALSE;

    if (hwnd == GetShellWindow())
        return FALSE;
    if (haveCls && (!lstrcmpW(cls, L"Progman") || !lstrcmpW(cls, L"WorkerW") ||
                    !lstrcmpW(cls, L"Shell_TrayWnd")))
        return FALSE;

    band = 1; /* нет API/отказ — desktop, как Shim_GetWindowBand в src */
    if (g_GetWindowBand && SUCCEEDED(g_GetWindowBand(hwnd, &band)) &&
        band != 1 && band != 2)
        return FALSE;

    DwmGetWindowAttribute(hwnd, DWMWA_CLOAKED, &cloaked, sizeof(cloaked));
    return cloaked == 0;
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
    wchar_t cls[64];
    LONG_PTR exStyle, walk, probe;
    int cloaked = 0;

    if (!IsWindowVisible(hwnd))
        return FALSE;
    if (GetWindowTextLengthW(hwnd) == 0)
        return FALSE;

    /* Кнопку убрали через ITaskbarList::DeleteTab — свойство ставит сама оболочка
       (проверено tests\deltab). */
    if (GetPropW(hwnd, L"ITaskList_Deleted"))
        return FALSE;

    /* «Голый» CoreWindow — shell-инфраструктура (Пуск, поиск): в ленту попадает
       по UNCLOAKED в момент открытия меню. UWP-приложения представлены хостом
       ApplicationFrameWindow, поэтому класс безопасно отсекать целиком. */
    if (GetClassNameW(hwnd, cls, 64) && !lstrcmpW(cls, L"Windows.UI.Core.CoreWindow"))
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

/* ===== Список окон, пришедших от hook, и лог ===== */

enum { MODE_WINEVENT, MODE_SHELL };

#define HOOK_MAX 512

typedef struct HookItem
{
    HWND hwnd;
    ULONGLONG tick;
} HookItem;

static HookItem s_items[HOOK_MAX];
static int s_count;
static int g_mode, g_enumOn;
static HWND g_host; /* скрытое host-окно shell-режима; NULL в winevent-режиме */
static wchar_t g_logPath[MAX_PATH];
static ULONGLONG g_t0;
static DWORD g_events, g_noise, g_adds, g_removed, g_full;
static FILE *g_log;
static volatile BOOL g_dirty = TRUE; /* кадр изменился — перерисовать */

static void PrintW(const wchar_t *s)
{
    char buf[8192];
    if (WideCharToMultiByte(CP_UTF8, 0, s, -1, buf, sizeof(buf), NULL, NULL) > 0)
        fputs(buf, stdout);
}

static void PrintFW(FILE *f, const wchar_t *s)
{
    char buf[4096];
    if (WideCharToMultiByte(CP_UTF8, 0, s, -1, buf, sizeof(buf), NULL, NULL) > 0)
        fputs(buf, f);
}

static int FindIndex(HWND hwnd)
{
    int i;
    for (i = 0; i < s_count; i++)
        if (s_items[i].hwnd == hwnd)
            return i;
    return -1;
}

/* Свои окна: что угодно нашего процесса (shell hook не умеет SKIPOWNPROCESS),
   скрытый host и консоль с таблицей — в списке им не место. Хитро с Windows
   Terminal: GetConsoleWindow отдаёт скрытое псевдоокно, а видимое окно
   терминала принадлежит процессу WindowsTerminal.exe — узнаём его по
   owner-цепочке псевдоокна и по классу + заголовку, совпадающему с
   заголовком нашей консоли (GetConsoleTitleW). */
static BOOL IsSelfWindow(HWND hwnd)
{
    wchar_t cls[64], conTtl[512], wndTtl[512];
    DWORD pid = 0;
    HWND con = GetConsoleWindow();

    if (hwnd == g_host)
        return TRUE;
    if (con)
    {
        if (hwnd == con || hwnd == GetAncestor(con, GA_ROOTOWNER))
            return TRUE;
        GetConsoleTitleW(conTtl, 512);
        if (conTtl[0])
        {
            GetClassNameW(hwnd, cls, 64);
            if ((!lstrcmpW(cls, L"CASCADIA_HOSTING_WINDOW_CLASS") ||
                 !lstrcmpW(cls, L"ConsoleWindowClass")) &&
                GetWindowTextW(hwnd, wndTtl, 512) && !lstrcmpW(wndTtl, conTtl))
                return TRUE;
        }
    }
    GetWindowThreadProcessId(hwnd, &pid);
    return pid == GetCurrentProcessId();
}

/* Первое попадание окна в список: единственное место, пишущее в лог. */
static void AddHwnd(HWND hwnd, const wchar_t *src)
{
    wchar_t cls[256], ttl[128], band[16], bandShow[16], line[1024];
    BOOL rcn, mtb, p8;

    if (!hwnd || !IsWindow(hwnd) || IsSelfWindow(hwnd) || FindIndex(hwnd) >= 0)
        return;
    if (s_count >= HOOK_MAX)
    {
        g_full++;
        return;
    }
    s_items[s_count].hwnd = hwnd;
    s_items[s_count].tick = GetTickCount64();
    s_count++;
    g_adds++;
    g_dirty = TRUE;

    if (!g_log)
        return;
    cls[0] = 0;
    ttl[0] = 0;
    GetClassNameW(hwnd, cls, 256);
    GetWindowTextW(hwnd, ttl, 128);
    /* recon может дропнуть окно до band-проверки — тогда заметки не будет */
    band[0] = 0;
    rcn = ReconAddsToTray2(hwnd, band, 16);
    mtb = IsAppWindow(hwnd);
    p8 = IsP8Filter(hwnd);
    lstrcpynW(bandShow, band[0] ? band : L"-", 16);
    {
        LONG_PTR ex = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
        HWND own = GetWindow(hwnd, GW_OWNER);
        int cloaked = 0, w = 0, h = 0;
        RECT rc;
        BOOL haveRc;

        DwmGetWindowAttribute(hwnd, DWMWA_CLOAKED, &cloaked, sizeof(cloaked));
        haveRc = GetWindowRect(hwnd, &rc);
        if (haveRc)
        {
            w = rc.right - rc.left;
            h = rc.bottom - rc.top;
        }
        _snwprintf(line, 1024,
                   L"[%8.1fs] %-9ls %016llx recon=%ls %ls montabc=%ls p8=%ls vis=%c ex=%08llx own=%ls clk=%d itld=%d sz=%dx%d cls=\"%ls\" ttl=\"%ls\"\n",
                   (double)(GetTickCount64() - g_t0) / 1000.0, src,
                   (unsigned long long)(ULONG_PTR)hwnd,
                   rcn ? L"keep" : L"DROP", bandShow, mtb ? L"keep" : L"DROP",
                   p8 ? L"keep" : L"DROP", IsWindowVisible(hwnd) ? L'Y' : L'N',
                   (unsigned long long)ex,
                   own ? L"Y" : L"-", cloaked,
                   GetPropW(hwnd, L"ITaskList_Deleted") ? 1 : 0,
                   w, h, cls, ttl);
    }
    PrintFW(g_log, line);
    fflush(g_log);
}

static void RemoveHwnd(HWND hwnd, const wchar_t *src)
{
    wchar_t cls[256], ttl[128], line[512];
    int idx = FindIndex(hwnd), i;
    if (idx < 0)
        return;
    for (i = idx; i < s_count - 1; i++)
        s_items[i] = s_items[i + 1];
    s_count--;
    g_removed++;
    g_dirty = TRUE;

    if (!g_log)
        return;
    cls[0] = 0;
    ttl[0] = 0;
    GetClassNameW(hwnd, cls, 256);
    GetWindowTextW(hwnd, ttl, 128);
    _snwprintf(line, 512,
               L"[%8.1fs] REMOVED-%-7ls %016llx cls=\"%ls\" ttl=\"%ls\"\n",
               (double)(GetTickCount64() - g_t0) / 1000.0, src,
               (unsigned long long)(ULONG_PTR)hwnd, cls, ttl);
    PrintFW(g_log, line);
    fflush(g_log);
}

/* Страховка: окна, чей DESTROY не дошёл (или переиспользованный hwnd). */
static void SweepDead(void)
{
    int i;
    for (i = s_count - 1; i >= 0; i--)
        if (!IsWindow(s_items[i].hwnd))
            RemoveHwnd(s_items[i].hwnd, L"dead");
}

/* ===== Режим SetWinEventHook ===== */

static HWINEVENTHOOK g_winevents[6];

/* Первичный фильтр шума winevent (только для этого режима; shell hook не
   фильтруется — там shell сам присылает только кандидатов): настоящее
   top-level окно — родитель рабочий стол. Заодно отсекает child-контролы
   (ScrollBar, CtrlNotifySink и т.п. тоже шлют OBJID_WINDOW-события) и
   message-only окна. Хелперы уровня top-level (Default IME, OLE) остаются —
   их отсев семантический, это работа колонок-фильтров, не первичного. */
static BOOL IsTopLevelWindow(HWND hwnd)
{
    return GetAncestor(hwnd, GA_PARENT) == GetDesktopWindow();
}

static const wchar_t *EventName(DWORD ev)
{
    switch (ev)
    {
    case EVENT_OBJECT_CREATE: return L"CREATE";
    case EVENT_SYSTEM_FOREGROUND: return L"FG";
    case EVENT_OBJECT_SHOW: return L"SHOW";
    case EVENT_OBJECT_NAMECHANGE: return L"NAME";
    case EVENT_OBJECT_UNCLOAKED: return L"UNCLOAKED";
    case EVENT_OBJECT_DESTROY: return L"DESTROY";
    case EVENT_OBJECT_HIDE: return L"HIDE";
    case EVENT_OBJECT_CLOAKED: return L"CLOAKED";
    }
    return L"?";
}

/* Raw-лог событий удаления ДО любых фильтров (эксперимент P8.4: доставка
   DESTROY/HIDE — приходит ли событие, жив ли hwnd, top-level ли). */
static void LogRawRemove(DWORD ev, HWND hwnd)
{
    wchar_t cls[64], ttl[64], line[512];
    DWORD pid = 0;

    if (!g_log)
        return;
    cls[0] = 0;
    ttl[0] = 0;
    GetClassNameW(hwnd, cls, 64);
    GetWindowTextW(hwnd, ttl, 64);
    GetWindowThreadProcessId(hwnd, &pid);
    _snwprintf(line, 512,
               L"[%8.1fs] RAW-%-8ls %016llx win=%d pid=%lu par=top?%d vis=%d cls=\"%ls\" ttl=\"%ls\"\n",
               (double)(GetTickCount64() - g_t0) / 1000.0, EventName(ev),
               (unsigned long long)(ULONG_PTR)hwnd, IsWindow(hwnd) ? 1 : 0, pid,
               GetAncestor(hwnd, GA_PARENT) == GetDesktopWindow() ? 1 : 0,
               IsWindowVisible(hwnd) ? 1 : 0, cls, ttl);
    PrintFW(g_log, line);
    fflush(g_log);
}

static void CALLBACK WineventProc(HWINEVENTHOOK hook, DWORD ev, HWND hwnd,
                                  LONG idObject, LONG idChild, DWORD thread,
                                  DWORD time)
{
    (void)hook;
    (void)thread;
    (void)time;

    /* Только сами окна (OBJID_WINDOW == 0), не дочерние объекты accessibility. */
    if (idObject != 0 || idChild != 0 || !hwnd)
        return;
    if (ev == EVENT_OBJECT_DESTROY || ev == EVENT_OBJECT_HIDE ||
        ev == EVENT_OBJECT_CLOAKED)
        LogRawRemove(ev, hwnd); /* raw-лог ДО фильтров (эксперимент P8.4) */
    if (!IsTopLevelWindow(hwnd))
    {
        g_noise++;
        return;
    }
    if (IsSelfWindow(hwnd))
        return;
    g_events++;
    /* Любое событие по top-level окну может менять вердикты (HIDE/CLOAKED/
       MINSTART не трогают список) — кадр надо обновить. */
    g_dirty = TRUE;

    switch (ev)
    {
    case EVENT_OBJECT_CREATE:
        /* Хелперы (IME, OLE, ...) создаются скрытыми и не получают HIDE —
           без проверки видимости таблица растёт безгранично. */
        if (IsWindowVisible(hwnd))
            AddHwnd(hwnd, EventName(ev));
        break;
    case EVENT_OBJECT_SHOW:
    case EVENT_SYSTEM_FOREGROUND:
    case EVENT_OBJECT_NAMECHANGE:
        if (IsWindowVisible(hwnd))
            AddHwnd(hwnd, EventName(ev));
        break;
    case EVENT_OBJECT_UNCLOAKED:
        AddHwnd(hwnd, EventName(ev));
        break;
    case EVENT_OBJECT_DESTROY:
    case EVENT_OBJECT_HIDE:
    case EVENT_OBJECT_CLOAKED:
        /* HIDE/CLOAKED = «закрыто», как в WinEventProc tracker.c: убираем;
           вернётся — придёт снова по SHOW/UNCLOAKED. */
        RemoveHwnd(hwnd, EventName(ev));
        break;
    }
}

static void StartWinevent(void)
{
    static const DWORD ranges[6][2] = {
        {EVENT_OBJECT_CREATE, EVENT_OBJECT_CREATE},
        {EVENT_SYSTEM_FOREGROUND, EVENT_SYSTEM_FOREGROUND},
        {EVENT_SYSTEM_MINIMIZESTART, EVENT_SYSTEM_MINIMIZEEND},
        /* 0x8001..0x8003: DESTROY, SHOW, HIDE одним диапазоном */
        {EVENT_OBJECT_DESTROY, EVENT_OBJECT_HIDE},
        {EVENT_OBJECT_NAMECHANGE, EVENT_OBJECT_NAMECHANGE},
        /* 0x8017..0x8018: CLOAKED, UNCLOAKED (UWP, виртуальные рабочие столы) */
        {EVENT_OBJECT_CLOAKED, EVENT_OBJECT_UNCLOAKED},
    };
    int i;

    for (i = 0; i < 6; i++)
        g_winevents[i] = SetWinEventHook(ranges[i][0], ranges[i][1], NULL,
                                         WineventProc, 0, 0,
                                         WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS);
}

static void StopWinevent(void)
{
    int i;
    for (i = 0; i < 6; i++)
        if (g_winevents[i])
            UnhookWinEvent(g_winevents[i]);
}

/* ===== Режим RegisterShellHookWindow ===== */

static UINT g_msgShell;

static LRESULT CALLBACK HostProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    if (g_msgShell && msg == g_msgShell)
    {
        g_dirty = TRUE;
        switch ((int)wp)
        {
        case HSHELL_WINDOWCREATED:
            AddHwnd((HWND)lp, L"WCREATED");
            break;
        case HSHELL_WINDOWACTIVATED:
            AddHwnd((HWND)lp, L"WACT");
            break;
        case HSHELL_REDRAW:
            AddHwnd((HWND)lp, L"REDRAW");
            break;
        case HSHELL_RUDEAPPACTIVATED: /* REDRAW | 0x8000 */
            AddHwnd((HWND)lp, L"RUDEACT");
            break;
        case HSHELL_WINDOWDESTROYED:
            RemoveHwnd((HWND)lp, L"WDESTROY");
            break;
        case HSHELL_WINDOWREPLACED:
            /* Параметры кода разночтимы (wParam — то ли сам код, то ли
               заменяемое окно): добавляем оба hwnd, лишнее вычистит SweepDead. */
            AddHwnd((HWND)wp, L"WREPLACED");
            AddHwnd((HWND)lp, L"WREPLACED");
            break;
        }
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

static BOOL StartShell(void)
{
    WNDCLASSW wc;

    g_msgShell = RegisterWindowMessageW(L"SHELLHOOK");
    if (!g_msgShell)
        return FALSE;

    ZeroMemory(&wc, sizeof(wc));
    wc.lpfnWndProc = HostProc;
    wc.hInstance = GetModuleHandleW(NULL);
    wc.lpszClassName = L"hooklist.host";
    if (!RegisterClassW(&wc))
        return FALSE;

    /* Скрытое top-level окно: RegisterShellHookWindow шлёт сообщения ему. */
    g_host = CreateWindowExW(WS_EX_TOOLWINDOW, wc.lpszClassName, L"hooklist",
                             WS_OVERLAPPED, 0, 0, 0, 0, NULL, NULL,
                             wc.hInstance, NULL);
    if (!g_host)
        return FALSE;
    return RegisterShellHookWindow(g_host);
}

static void StopShell(void)
{
    if (g_host)
    {
        DeregisterShellHookWindow(g_host);
        DestroyWindow(g_host);
        g_host = NULL;
    }
}

/* ===== Стартовый EnumWindows ===== */

static BOOL CALLBACK EnumAddProc(HWND hwnd, LPARAM lp)
{
    (void)lp;
    AddHwnd(hwnd, L"ENUM");
    return TRUE;
}

/* ===== Консоль ===== */

static HANDLE g_con;
static CONSOLE_CURSOR_INFO g_savedCursor;
static int g_frameLines;
static int g_lastW, g_lastH;
static volatile BOOL g_quit;
static int g_exitSec; /* -exit N: само-закрытие через N секунд */

static BOOL WINAPI CtrlHandler(DWORD type)
{
    (void)type;
    g_quit = TRUE;
    return TRUE;
}

/* Строка кадра: усечь/добить пробелами до width-1 (без заворота); '\n' —
   только если withNl: '\n' на последней видимой строке скроллит консоль. */
static void FrameLine(const wchar_t *s, int width, int withNl)
{
    wchar_t buf[512];
    DWORD written;
    int i, n = 0, len = lstrlenW(s);

    if (len > width - 1)
        len = width - 1;
    for (i = 0; i < len; i++)
        buf[n++] = s[i];
    for (; n < width - 1; n++)
        buf[n] = L' ';
    if (withNl)
        buf[n++] = L'\n';
    WriteConsoleW(g_con, buf, (DWORD)n, &written, NULL);
}

/* Усечение с «...» на конце, если длиннее width (ширина в символах). */
static void Ellipsize(wchar_t *out, int cch, const wchar_t *s, int width)
{
    int i, len = lstrlenW(s);

    if (len <= width)
    {
        for (i = 0; i <= len; i++)
            out[i] = s[i];
        return;
    }
    if (width > cch - 4)
        width = cch - 4;
    if (width < 1)
    {
        out[0] = 0;
        return;
    }
    for (i = 0; i < width - 3; i++)
        out[i] = s[i];
    out[i++] = L'.';
    out[i++] = L'.';
    out[i++] = L'.';
    out[i] = 0;
}

/* Ячейка колонки: content, добитый пробелами до colw; content==NULL — пусто. */
static void PadCell(wchar_t *out, int colw, const wchar_t *content)
{
    int i = 0;
    if (content)
        for (; content[i] && i < colw; i++)
            out[i] = content[i];
    for (; i < colw; i++)
        out[i] = L' ';
    out[i] = 0;
}

/* Смена размера окна консоли — повод перерисовать (колонки/строки меняются). */
static BOOL ConsoleResized(void)
{
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    int w, h;

    if (!GetConsoleScreenBufferInfo(g_con, &csbi))
        return FALSE;
    w = csbi.srWindow.Right - csbi.srWindow.Left + 1;
    h = csbi.srWindow.Bottom - csbi.srWindow.Top + 1;
    return w != g_lastW || h != g_lastH;
}

static void Render(void)
{
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    wchar_t line[2048], cellA[200], cellB[200], cellC[200];
    int width = 100, height = 30, colw, rows, maxW, i, n, total, lineIdx = 0;
    BOOL haveCsbi;
    COORD home;

    haveCsbi = GetConsoleScreenBufferInfo(g_con, &csbi) != 0;
    if (haveCsbi)
    {
        width = csbi.srWindow.Right - csbi.srWindow.Left + 1;
        height = csbi.srWindow.Bottom - csbi.srWindow.Top + 1;
    }
    if (width < 60)
        width = 60;
    if (width > 250)
        width = 250;
    if (height < 10)
        height = 10;
    g_lastW = width;
    g_lastH = height;

    colw = (width - 13) / 3;
    if (colw > 120)
        colw = 120;
    rows = height - 3; /* 2 строки шапки + строка заголовков колонок */
    maxW = (rows - 1) / 2; /* окно занимает 2 строки кадра */

    /* Всего строк кадра: последняя пишется без '\n'; хвост более длинного
       предыдущего кадра затирается пустыми строками. */
    n = s_count < maxW ? s_count : maxW;
    total = 3 + 2 * n + (s_count > maxW ? 1 : 0);
    if (total < g_frameLines)
        total = g_frameLines;
    if (total > height)
        total = height;

    if (haveCsbi)
    {
        home.X = csbi.srWindow.Left;
        home.Y = csbi.srWindow.Top;
        SetConsoleCursorPosition(g_con, home);
    }

    /* шапка */
    _snwprintf(line, 2048, L"hooklist режим=%ls enum=%ls лог=%ls",
               g_mode == MODE_SHELL ? L"shell" : L"winevent",
               g_enumOn ? L"да" : L"нет", g_logPath);
    FrameLine(line, width, lineIdx++ < total - 1);
    _snwprintf(line, 2048,
               L"окон=%d появлений=%lu удалений=%lu событий=%lu шума=%lu%ls",
               s_count, g_adds, g_removed, g_events, g_noise,
               g_full ? L" (переполнение списка!)" : L"");
    FrameLine(line, width, lineIdx++ < total - 1);

    PadCell(cellA, colw, L"все (hook)");
    PadCell(cellB, colw, L"recon (explorer)");
    PadCell(cellC, colw, L"montabc P8");
    _snwprintf(line, 2048, L"%9ls %ls|%ls|%ls", L"", cellA, cellB, cellC);
    FrameLine(line, width, lineIdx++ < total - 1);

    n = 0;
    for (i = 0; i < s_count && n < maxW; i++)
    {
        HWND h = s_items[i].hwnd;
        wchar_t ttl[128], cls[256], band[16];
        wchar_t titleCell[260], clsCell[300], clsTag[304];
        BOOL rcn, mtb;

        ttl[0] = 0;
        cls[0] = 0;
        GetWindowTextW(h, ttl, 128);
        GetClassNameW(h, cls, 256);
        Ellipsize(titleCell, 260, ttl, colw);
        Ellipsize(clsCell, 300, cls, colw - 2);
        _snwprintf(clsTag, 304, L"[%ls]", clsCell);

        /* строка 1: заголовок; строка 2: [класс] — по одной позиции колонки */
        PadCell(cellA, colw, titleCell);
        rcn = ReconAddsToTray2(h, band, 16);
        PadCell(cellB, colw, rcn ? titleCell : NULL);
        mtb = IsP8Filter(h);
        PadCell(cellC, colw, mtb ? titleCell : NULL);
        _snwprintf(line, 2048, L"%9llx %ls|%ls|%ls",
                   (unsigned long long)(ULONG_PTR)h, cellA, cellB, cellC);
        FrameLine(line, width, lineIdx++ < total - 1);

        PadCell(cellA, colw, clsTag);
        PadCell(cellB, colw, rcn ? clsTag : NULL);
        PadCell(cellC, colw, mtb ? clsTag : NULL);
        _snwprintf(line, 2048, L"%9ls %ls|%ls|%ls", L"", cellA, cellB, cellC);
        FrameLine(line, width, lineIdx++ < total - 1);
        n++;
    }
    if (i < s_count)
    {
        _snwprintf(line, 2048, L"%9ls +%d окон ниже по списку", L"", s_count - n);
        FrameLine(line, width, lineIdx++ < total - 1);
    }
    while (lineIdx < total) /* затереть хвост предыдущего кадра */
        FrameLine(L"", width, lineIdx++ < total - 1);
    g_frameLines = total;
}

int wmain(int argc, wchar_t **argv)
{
    wchar_t line[512];
    MSG msg;
    CONSOLE_CURSOR_INFO ci;
    int i;

    lstrcpynW(g_logPath, L"hooklist.log", MAX_PATH);
    for (i = 1; i < argc; i++)
    {
        if (!lstrcmpW(argv[i], L"-shell"))
            g_mode = MODE_SHELL;
        else if (!lstrcmpW(argv[i], L"-winevent"))
            g_mode = MODE_WINEVENT;
        else if (!lstrcmpW(argv[i], L"-enum"))
            g_enumOn = 1;
        else if (!lstrcmpW(argv[i], L"-noenum"))
            g_enumOn = 0;
        else if (!lstrcmpW(argv[i], L"-exit") && i + 1 < argc)
            g_exitSec = _wtoi(argv[++i]);
        else if (argv[i][0] == L'-')
        {
            PrintW(L"hooklist [-shell | -winevent] [-enum | -noenum] [-exit N] [logfile]\n");
            return 2;
        }
        else
            lstrcpynW(g_logPath, argv[i], MAX_PATH);
    }

    SetConsoleOutputCP(CP_UTF8);
    g_con = GetStdHandle(STD_OUTPUT_HANDLE);
    if (GetConsoleCursorInfo(g_con, &g_savedCursor))
    {
        ci.dwSize = 25;
        ci.bVisible = FALSE;
        SetConsoleCursorInfo(g_con, &ci);
    }
    SetConsoleCtrlHandler(CtrlHandler, TRUE);

    g_GetWindowBand = (GetWindowBand_t)(void *)GetProcAddress(
        GetModuleHandleW(L"user32.dll"), "GetWindowBand");

    g_log = _wfopen(g_logPath, L"ab");
    if (g_log)
    {
        if (ftell(g_log) == 0)
            fputs("\xef\xbb\xbf", g_log);
        _snwprintf(line, 512, L"=== hooklist -%ls enum=%ls ===\n",
                   g_mode == MODE_SHELL ? L"shell" : L"winevent",
                   g_enumOn ? L"1" : L"0");
        PrintFW(g_log, line);
        fflush(g_log);
    }

    g_t0 = GetTickCount64();
    if (g_mode == MODE_SHELL)
    {
        if (!StartShell())
        {
            PrintW(L"RegisterShellHookWindow failed\n");
            if (g_log)
                fclose(g_log);
            return 1;
        }
    }
    else
    {
        StartWinevent();
    }

    if (g_enumOn)
        EnumWindows(EnumAddProc, 0);

    while (!g_quit)
    {
        while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE))
        {
            if (msg.message == WM_QUIT)
            {
                g_quit = TRUE;
                break;
            }
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        if (g_quit)
            break;
        SweepDead();
        if (ConsoleResized())
            g_dirty = TRUE;
        if (g_dirty)
        {
            g_dirty = FALSE;
            Render();
        }
        if (g_exitSec && GetTickCount64() - g_t0 >= (ULONGLONG)g_exitSec * 1000)
            g_quit = TRUE;
        Sleep(500);
    }

    if (g_mode == MODE_SHELL)
        StopShell();
    else
        StopWinevent();

    if (GetConsoleCursorInfo(g_con, &g_savedCursor))
        SetConsoleCursorInfo(g_con, &g_savedCursor);
    {
        COORD below;
        below.X = 0;
        below.Y = (SHORT)g_frameLines;
        SetConsoleCursorPosition(g_con, below);
    }
    PrintW(L"остановлено\n");
    if (g_log)
        fclose(g_log);
    return 0;
}
