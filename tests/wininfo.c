/* wininfo — дамп свойств top-level окон; тест-инструмент для problems.md (P3).
   Сборка (VS Native Tools x64, из каталога tests или tests\build.bat):
     cl /nologo /W4 /utf-8 wininfo.c user32.lib dwmapi.lib shell32.lib ole32.lib
   Запуск: wininfo [подстрока] [сек] — только окна с подстрокой в классе/
   заголовке; сек — пауза перед снапшотом (по умолчанию 5, «открой Пуск»).
   Если первый аргумент — число, оно задаёт паузу: wininfo 10 [подстрока].

   Колонки-вердикты:
     cur  — текущий фильтр montabc (порт 1:1 из src/tracker.c, с правками P7:
            ITaskList_Deleted и CoreWindow; keep = попадёт в ленту)
     bl   — эвристика P3 «безрамочный layered» (DROP = была бы отсечена)
     gg   — фильтр из Google-сниппета (keep = сниппет бы показал)
     rcn  — реконструкция ShouldAddWindowToTray из Taskbar.dll 22621.6199
            (PDB+дизассембл, experimental/findings.md): vis && !TOOLWINDOW &&
            (!owner || LAYERED) && !desktop/tray && band in {1,2};
            band=NN — GetWindowBand, band?/band- — API нет/отказ
            (проп 0xA920 и IsShellManagedWindow недоступны стороннему) */

#include <windows.h>
#include <dwmapi.h>
#include <shellapi.h>
#include <propsys.h>
#include <propkey.h>
#include <stdio.h>
#include <wchar.h>
#include <wctype.h>

/* ===== Текущий фильтр montabc P8 — порт 1:1 из src/tracker.c ===== */

typedef HRESULT(WINAPI *GetWindowBand_t)(HWND, DWORD *);
static GetWindowBand_t g_GetWindowBand;

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

/* Текущий фильтр montabc P8 — порт 1:1 из src/tracker.c (ReconAddsToTray2 +
   отсечения P7 и cloak + C1 top-level/C3 Ghost/C4 заголовок); band — через
   g_GetWindowBand с семантикой шима. */
static BOOL CurrentIsAppWindow(HWND hwnd)
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

/* ===== Эвристика P3 «безрамочный layered» (problems.md), TRUE = отсечь ===== */

static BOOL BlHeuristicDrops(HWND hwnd)
{
    LONG_PTR style = GetWindowLongPtrW(hwnd, GWL_STYLE);
    LONG_PTR ex = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
    return (style & WS_CAPTION) != WS_CAPTION &&
           (ex & WS_EX_APPWINDOW) == 0 &&
           (ex & WS_EX_LAYERED) != 0;
}

/* ===== Фильтр из Google-сниппета — порт 1:1 (GetClassName* -> *W) ===== */

static BOOL GoogleIsAltTab(HWND hwnd)
{
    HWND hwndWalk, hwndPopup;
    LONG exStyle;
    BOOL isAppWindow;
    int cloaked = 0;
    wchar_t className[256];

    if (!IsWindowVisible(hwnd))
        return FALSE;

    hwndWalk = GetAncestor(hwnd, GA_ROOTOWNER);
    hwndPopup = GetLastActivePopup(hwndWalk);
    if (IsWindowVisible(hwndPopup) && hwndPopup != hwnd)
        return FALSE;

    exStyle = GetWindowLongW(hwnd, GWL_EXSTYLE);
    if (exStyle & WS_EX_TOOLWINDOW)
        return FALSE;
    isAppWindow = (exStyle & WS_EX_APPWINDOW) != 0;

    if (SUCCEEDED(DwmGetWindowAttribute(hwnd, DWMWA_CLOAKED, &cloaked, sizeof(cloaked))) && cloaked != 0)
        return FALSE;

    if (GetClassNameW(hwnd, className, 256))
    {
        if (wcscmp(className, L"ApplicationFrameWindow") == 0)
        {
            if (GetWindowTextLengthW(hwnd) == 0)
                return FALSE;
        }
        if (wcscmp(className, L"Windows.UI.Core.CoreWindow") == 0 ||
            wcscmp(className, L"EdgeUiInputTopWndClass") == 0 ||
            wcscmp(className, L"Shell_TrayWnd") == 0)
            return FALSE;
    }

    if (!isAppWindow)
    {
        if (GetWindowTextLengthW(hwnd) == 0)
            return FALSE;
    }
    return TRUE;
}

/* ===== Реконструкция ShouldAddWindowToTray (Taskbar.dll 22621.6199, по PDB) ===== */

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

/* ===== Сбор и печать ===== */

static wchar_t g_filter[128];
static int g_total, g_vis, g_curKeep, g_blDrop, g_ggKeep, g_rcnKeep;

struct BitName { LONG_PTR bit; const wchar_t *name; };

static const struct BitName kStyle[] = {
    {WS_CAPTION, L"CAP"}, {WS_SYSMENU, L"SYS"}, {WS_THICKFRAME, L"THICK"},
    {WS_MINIMIZEBOX, L"MINB"}, {WS_MAXIMIZEBOX, L"MAXB"}, {WS_POPUP, L"POPUP"},
    {WS_CHILD, L"CHILD"}, {WS_VISIBLE, L"VIS"}, {WS_MINIMIZE, L"MINI"},
};
static const struct BitName kEx[] = {
    {WS_EX_LAYERED, L"LAY"}, {WS_EX_TOOLWINDOW, L"TOOL"}, {WS_EX_APPWINDOW, L"APPW"},
    {WS_EX_NOACTIVATE, L"NOACT"}, {WS_EX_TOPMOST, L"TOPM"},
    {WS_EX_WINDOWEDGE, L"WEDGE"}, {WS_EX_PALETTEWINDOW, L"PALET"},
};

static void BuildBits(wchar_t *out, int cch, LONG_PTR v, const struct BitName *tab, int n)
{
    int i, len = 0;
    out[0] = 0;
    for (i = 0; i < n; i++)
        if ((v & tab[i].bit) == tab[i].bit)
        {
            int w = _snwprintf(out + len, cch - len, len ? L"|%ls" : L"%ls", tab[i].name);
            if (w > 0)
                len += w;
        }
    if (!out[0])
        lstrcpynW(out, L"-", cch);
}

static const wchar_t *WcsIstr(const wchar_t *hay, const wchar_t *needle)
{
    if (!*needle)
        return hay;
    for (; *hay; hay++)
    {
        int i;
        for (i = 0; ; i++)
        {
            wchar_t a = hay[i], b = needle[i];
            if (!b)
                return hay;
            if (!a || towlower(a) != towlower(b))
                break;
        }
    }
    return NULL;
}

static void PrintW(const wchar_t *s)
{
    char buf[8192];
    if (WideCharToMultiByte(CP_UTF8, 0, s, -1, buf, sizeof(buf), NULL, NULL) > 0)
        fputs(buf, stdout);
}

static void GetExeName(DWORD pid, wchar_t *out, int cch)
{
    HANDLE h;
    wchar_t path[MAX_PATH];
    DWORD c = MAX_PATH;
    wchar_t *p;

    out[0] = 0;
    h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!h)
        return;
    if (QueryFullProcessImageNameW(h, 0, path, &c))
    {
        p = wcsrchr(path, L'\\');
        lstrcpynW(out, p ? p + 1 : path, cch);
    }
    CloseHandle(h);
}

/* dual: -1 нет свойства, 0 FALSE, 1 TRUE */
static void GetAumid(HWND hwnd, wchar_t *id, int cchId, int *dual)
{
    IPropertyStore *ps;
    PROPVARIANT pv;

    id[0] = 0;
    *dual = -1;
    if (FAILED(SHGetPropertyStoreForWindow(hwnd, &IID_IPropertyStore, (void **)&ps)) || !ps)
        return;
    PropVariantInit(&pv);
    if (SUCCEEDED(ps->lpVtbl->GetValue(ps, &PKEY_AppUserModel_ID, &pv)) &&
        pv.vt == VT_LPWSTR && pv.pwszVal)
        lstrcpynW(id, pv.pwszVal, cchId);
    PropVariantClear(&pv);
    PropVariantInit(&pv);
    if (SUCCEEDED(ps->lpVtbl->GetValue(ps, &PKEY_AppUserModel_IsDualMode, &pv)))
    {
        if (pv.vt == VT_BOOL)
            *dual = pv.boolVal ? 1 : 0;
        PropVariantClear(&pv);
    }
    ps->lpVtbl->Release(ps);
}

/* Перечисление всех свойств окна (EnumPropsW): имя — строка или целочисленный атом. */
static wchar_t g_props[4096];

static void AppendPropName(LPWSTR name)
{
    wchar_t item[256];
    int len, room;

    if (((ULONG_PTR)name >> 16) != 0)
        lstrcpynW(item, name, 256);
    else
    {
        item[0] = L'#';
        if (!GlobalGetAtomNameW((ATOM)(ULONG_PTR)name, item + 1, 254))
            _snwprintf(item, 256, L"#atom%u", (unsigned)(ULONG_PTR)name);
    }
    len = lstrlenW(g_props);
    room = (int)(sizeof(g_props) / sizeof(g_props[0])) - len - lstrlenW(item) - 2;
    if (room <= 0)
        return;
    if (len)
        wcscat(g_props, L";");
    wcscat(g_props, item);
}

static BOOL CALLBACK PropEnumProc(HWND hwnd, LPWSTR name, HANDLE data)
{
    (void)hwnd; (void)data;
    AppendPropName(name);
    return TRUE;
}

static void BuildClk(wchar_t *out, int cch, int cloaked)
{
    if (cloaked == 0)
    {
        lstrcpynW(out, L"0", cch);
        return;
    }
    out[0] = 0;
    if (cloaked & DWM_CLOAKED_APP) wcscat(out, L"A");
    if (cloaked & DWM_CLOAKED_SHELL) wcscat(out, L"S");
    if (cloaked & DWM_CLOAKED_INHERITED) wcscat(out, L"I");
}

static BOOL CALLBACK EnumProc(HWND hwnd, LPARAM lp)
{
    DWORD pid = 0;
    wchar_t exe[64], cls[256], ttl[128], aumid[512];
    wchar_t styleBuf[80], exBuf[80], szBuf[32], ownBuf[32], parBuf[32], clkBuf[16];
    wchar_t bandBuf[16];
    wchar_t line[3072];
    int cloaked = 0, dual = -1;
    HANDLE itld;
    wchar_t itldBuf[24];
    BOOL vis, cur, gg, bl, rcn;
    LONG_PTR style, ex, rootOwner;
    RECT rc;
    MONITORINFO mi;

    (void)lp;
    GetWindowThreadProcessId(hwnd, &pid);
    GetExeName(pid, exe, 64);
    GetClassNameW(hwnd, cls, 256);
    GetWindowTextW(hwnd, ttl, 128);

    if (g_filter[0] && !WcsIstr(cls, g_filter) && !WcsIstr(ttl, g_filter))
        return TRUE;

    vis = IsWindowVisible(hwnd);
    DwmGetWindowAttribute(hwnd, DWMWA_CLOAKED, &cloaked, sizeof(cloaked));
    style = GetWindowLongPtrW(hwnd, GWL_STYLE);
    ex = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
    GetAumid(hwnd, aumid, 512, &dual);

    itld = GetPropW(hwnd, L"ITaskList_Deleted");
    if (itld)
        _snwprintf(itldBuf, 24, L"%llx", (unsigned long long)(ULONG_PTR)itld);
    else
        lstrcpynW(itldBuf, L"-", 24);

    g_props[0] = 0;
    EnumPropsW(hwnd, (PROPENUMPROCW)PropEnumProc);

    cur = CurrentIsAppWindow(hwnd);
    gg = GoogleIsAltTab(hwnd);
    bl = BlHeuristicDrops(hwnd);
    bandBuf[0] = 0;
    rcn = ReconAddsToTray(hwnd, bandBuf, 16);
    if (!bandBuf[0])
        lstrcpynW(bandBuf, L"-", 16); /* дроп до band-проверки — заметки нет */

    BuildBits(styleBuf, 80, style, kStyle, 9);
    BuildBits(exBuf, 80, ex, kEx, 7);

    BuildClk(clkBuf, 16, cloaked);
    rootOwner = (LONG_PTR)GetAncestor(hwnd, GA_ROOTOWNER);
    if (rootOwner == (LONG_PTR)hwnd)
        lstrcpynW(ownBuf, L"self", 32);
    else
        _snwprintf(ownBuf, 32, L"%llx", (unsigned long long)rootOwner);

    mi.cbSize = sizeof(mi);
    if (IsIconic(hwnd))
        lstrcpynW(szBuf, L"min", 32);
    else if (GetWindowRect(hwnd, &rc) &&
             GetMonitorInfoW(MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST), &mi) &&
             mi.rcMonitor.right > mi.rcMonitor.left && mi.rcMonitor.bottom > mi.rcMonitor.top)
        _snwprintf(szBuf, 32, L"%dx%d",
                   (int)((rc.right - rc.left) * 100 / (mi.rcMonitor.right - mi.rcMonitor.left)),
                   (int)((rc.bottom - rc.top) * 100 / (mi.rcMonitor.bottom - mi.rcMonitor.top)));
    else
        lstrcpynW(szBuf, L"?", 32);

    swprintf(line, 3072,
             L"%016llx pid=%lu vis=%c clk=%d(%ls) itld=%ls cur=%ls bl=%ls gg=%ls rcn=%ls %ls style=%08llx:%ls ex=%08llx:%ls\n",
             (unsigned long long)(ULONG_PTR)hwnd, pid, vis ? L'Y' : L'N',
             cloaked, clkBuf, itldBuf,
             cur ? L"keep" : L"DROP", bl ? L"DROP" : L"keep", gg ? L"keep" : L"DROP",
             rcn ? L"keep" : L"DROP", bandBuf,
             (unsigned long long)style, styleBuf, (unsigned long long)ex, exBuf);
    PrintW(line);
    {
        HWND par = GetParent(hwnd);
        if (!par)
            lstrcpynW(parBuf, L"-", 32);
        else if (par == hwnd)
            lstrcpynW(parBuf, L"self", 32);
        else
            _snwprintf(parBuf, 32, L"%llx", (unsigned long long)(ULONG_PTR)par);
    }
    swprintf(line, 3072,
             L"    exe=%.20ls cls=%.48ls ttl=\"%-.60ls\" sz=%ls own=%ls par=%ls aumid=%.40ls\n",
             exe, cls, ttl, szBuf, ownBuf, parBuf, aumid[0] ? aumid : L"-");
    PrintW(line);
    if (aumid[0])
    {
        swprintf(line, 3072, L"    aumid.full=\"%ls\" dual=%c\n", aumid,
                 dual < 0 ? L'-' : (dual ? L'Y' : L'N'));
        PrintW(line);
    }
    if (g_props[0])
    {
        swprintf(line, 3072, L"    props=\"%ls\"\n", g_props);
        PrintW(line);
    }

    g_total++;
    if (vis) g_vis++;
    if (cur) g_curKeep++;
    if (bl) g_blDrop++;
    if (gg) g_ggKeep++;
    if (rcn) g_rcnKeep++;
    return TRUE;
}

/* ===== Watch-режим: лог WinEvent-событий (диапазоны как в src/tracker.c) ===== */

static ULONGLONG g_watchT0;

static const wchar_t *EventName(DWORD ev)
{
    switch (ev)
    {
    case EVENT_SYSTEM_FOREGROUND: return L"FG";
    case EVENT_SYSTEM_MINIMIZESTART: return L"MINSTART";
    case EVENT_SYSTEM_MINIMIZEEND: return L"MINEND";
    case EVENT_OBJECT_DESTROY: return L"DESTROY";
    case EVENT_OBJECT_SHOW: return L"SHOW";
    case EVENT_OBJECT_HIDE: return L"HIDE";
    case EVENT_OBJECT_NAMECHANGE: return L"NAME";
    case EVENT_OBJECT_CLOAKED: return L"CLOAKED";
    case EVENT_OBJECT_UNCLOAKED: return L"UNCLOAKED";
    }
    return L"?";
}

static void CALLBACK WatchProc(HWINEVENTHOOK hook, DWORD ev, HWND hwnd,
                               LONG idObject, LONG idChild, DWORD thread, DWORD time)
{
    DWORD pid = 0;
    wchar_t exe[64], cls[256], ttl[128], styleBuf[80], exBuf[80];
    wchar_t clkBuf[16], itldBuf[24], bandBuf[16], line[2048];
    HANDLE itld;
    int cloaked = 0;
    BOOL vis, cur, rcn;
    LONG_PTR style, ex;

    (void)hook; (void)thread; (void)time;

    if (idObject != 0 || idChild != 0 || !hwnd)
        return;

    GetWindowThreadProcessId(hwnd, &pid);
    GetExeName(pid, exe, 64);
    GetClassNameW(hwnd, cls, 256);
    GetWindowTextW(hwnd, ttl, 128);

    if (g_filter[0] && !WcsIstr(cls, g_filter) && !WcsIstr(ttl, g_filter))
        return;

    vis = IsWindowVisible(hwnd);
    DwmGetWindowAttribute(hwnd, DWMWA_CLOAKED, &cloaked, sizeof(cloaked));
    style = GetWindowLongPtrW(hwnd, GWL_STYLE);
    ex = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
    cur = CurrentIsAppWindow(hwnd);
    bandBuf[0] = 0;
    rcn = ReconAddsToTray(hwnd, bandBuf, 16);
    if (!bandBuf[0])
        lstrcpynW(bandBuf, L"-", 16); /* дроп до band-проверки — заметки нет */
    BuildClk(clkBuf, 16, cloaked);
    BuildBits(styleBuf, 80, style, kStyle, 9);
    BuildBits(exBuf, 80, ex, kEx, 7);
    itld = GetPropW(hwnd, L"ITaskList_Deleted");
    if (itld)
        _snwprintf(itldBuf, 24, L"%llx", (unsigned long long)(ULONG_PTR)itld);
    else
        lstrcpynW(itldBuf, L"-", 24);

    swprintf(line, 2048,
             L"[%6.1fs] %-8ls %016llx pid=%lu vis=%c clk=%d(%ls) itld=%ls cur=%ls rcn=%ls %ls style=%ls ex=%ls\n"
             L"    exe=%ls cls=%ls ttl=\"%ls\"\n",
             (double)(GetTickCount64() - g_watchT0) / 1000.0, EventName(ev),
             (unsigned long long)(ULONG_PTR)hwnd, pid, vis ? L'Y' : L'N',
             cloaked, clkBuf, itldBuf, cur ? L"keep" : L"DROP",
             rcn ? L"keep" : L"DROP", bandBuf,
             styleBuf, exBuf, exe, cls, ttl);
    PrintW(line);
    fflush(stdout);
}

static void RunWatch(int seconds)
{
    static const DWORD ranges[5][2] = {
        {EVENT_SYSTEM_FOREGROUND, EVENT_SYSTEM_FOREGROUND},
        {EVENT_SYSTEM_MINIMIZESTART, EVENT_SYSTEM_MINIMIZEEND},
        {EVENT_OBJECT_DESTROY, EVENT_OBJECT_HIDE},
        {EVENT_OBJECT_NAMECHANGE, EVENT_OBJECT_NAMECHANGE},
        {EVENT_OBJECT_CLOAKED, EVENT_OBJECT_UNCLOAKED},
    };
    HWINEVENTHOOK hooks[5] = {0};
    ULONGLONG deadline;
    MSG msg;
    int i;

    for (i = 0; i < 5; i++)
        hooks[i] = SetWinEventHook(ranges[i][0], ranges[i][1], NULL, WatchProc,
                                   0, 0, WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS);
    g_watchT0 = GetTickCount64();
    deadline = g_watchT0 + (ULONGLONG)seconds * 1000;

    while (GetTickCount64() < deadline)
    {
        while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE))
        {
        }
        Sleep(50);
    }

    for (i = 0; i < 5; i++)
        if (hooks[i])
            UnhookWinEvent(hooks[i]);
}

int wmain(int argc, wchar_t **argv)
{
    wchar_t line[256];
    int i, argSec = 1, argFilter = 2;

    setvbuf(stdout, NULL, _IONBF, 0);
    SetConsoleOutputCP(CP_UTF8);

    g_GetWindowBand = (GetWindowBand_t)(void *)GetProcAddress(
        GetModuleHandleW(L"user32.dll"), "GetWindowBand");

    /* wininfo 10 [фильтр] или wininfo [фильтр] [сек] */
    if (argc > 1 && argv[1][0] >= L'0' && argv[1][0] <= L'9' &&
        argv[1][1] == 0)
    {
        argSec = 1;
        argFilter = 2;
    }
    else
    {
        argSec = 2;
        argFilter = 1;
    }

    if (argc > 1 && lstrcmpW(argv[1], L"-w") == 0)
    {
        int sec = argc > 2 ? _wtoi(argv[2]) : 20;
        if (sec <= 0)
            sec = 20;
        if (argc > 3)
            lstrcpynW(g_filter, argv[3], 128);
        RunWatch(sec);
        return 0;
    }

    if (argc > argFilter && argv[argFilter][0])
        lstrcpynW(g_filter, argv[argFilter], 128);

    PrintW(L"waiting - open the Start menu now\n");
    i = argc > argSec ? _wtoi(argv[argSec]) : 5;
    if (i < 0) i = 0;
    if (i > 120) i = 120;
    Sleep(i * 1000);

    if (FAILED(CoInitializeEx(NULL, COINIT_APARTMENTTHREADED)))
    {
        PrintW(L"CoInitializeEx failed\n");
        return 1;
    }
    EnumWindows(EnumProc, 0);
    CoUninitialize();

    swprintf(line, 256,
             L"total=%d visible=%d | cur keep=%d | bl DROP=%d | gg keep=%d | rcn keep=%d\n",
             g_total, g_vis, g_curKeep, g_blDrop, g_ggKeep, g_rcnKeep);
    PrintW(line);
    return 0;
}
