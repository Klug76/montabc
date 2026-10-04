/* probe — зонд для shell hook: окно проходит сценарии показа/стилей,
   каждый шаг — в заголовок окна и в probe.log (сек от старта).
   Наблюдатель — tests\hooklist (shell: WCREATED/WDESTROYED/REDRAW;
   winevent: raw-лог удалений). Шаги идут по WM_TIMER: окно обязано качать
   сообщения, иначе win32k посчитает его зависшим и подменит Ghost-дублем
   (проверено — портит тест). Шаги 15-21 — child-эксперимент P8.4:
   видимый child (hide/show/destroy), teardown родителя и ГЛАВНОЕ —
   завершение процесса с живыми окнами без DestroyWindow (шаг 21):
   их убивает win32k — модель закрытия ZCode пользователем.
   Запуск: probe [сек-на-шаг] (по умолчанию 2).
   Сборка: tests\build-probe.bat. */

#include <windows.h>
#include <stdio.h>

static const wchar_t kCls[] = L"probe.test";
static const wchar_t kHelper[] = L"probe.helper";
static FILE *g_log;
static ULONGLONG g_t0;
static HWND g_hwnd, g_helper, g_child;
static int g_step;
static int g_stepMs;
static BOOL g_done;

static void Log(const wchar_t *fmt, ...)
{
    va_list ap;
    if (!g_log)
        return;
    fwprintf(g_log, L"[%6.1fs] ", (double)(GetTickCount64() - g_t0) / 1000.0);
    va_start(ap, fmt);
    vfwprintf(g_log, fmt, ap);
    va_end(ap);
    fputwc(L'\n', g_log);
    fflush(g_log);
}

static void Step(int n, const wchar_t *name)
{
    wchar_t ttl[96];
    _snwprintf(ttl, 96, L"probe[%d] %ls", n, name);
    SetWindowTextW(g_hwnd, ttl);
    Log(L"step %d: %ls (vis=%d ex=%08lx own=%d)",
        n, name, IsWindowVisible(g_hwnd) ? 1 : 0,
        (unsigned long)GetWindowLongW(g_hwnd, GWL_EXSTYLE),
        GetWindow(g_hwnd, GW_OWNER) ? 1 : 0);
}

static void DoStep(void)
{
    switch (g_step)
    {
    case 1:
        ShowWindow(g_hwnd, SW_SHOW);
        break; /* unowned, обычный показ */
    case 2:
        ShowWindow(g_hwnd, SW_HIDE);
        break;
    case 3:
        SetWindowPos(g_hwnd, NULL, 0, 0, 0, 0,
                     SWP_SHOWWINDOW | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER);
        break; /* показ без hide-пары */
    case 4:
        SetWindowLongPtrW(g_hwnd, GWLP_HWNDPARENT, (LONG_PTR)g_helper);
        break; /* назначить владельца (окно видно) */
    case 5:
        ShowWindow(g_hwnd, SW_HIDE);
        ShowWindow(g_hwnd, SW_SHOW);
        break; /* owned: hide+show */
    case 6:
        SetWindowLongPtrW(g_hwnd, GWLP_HWNDPARENT, 0);
        ShowWindow(g_hwnd, SW_HIDE);
        ShowWindow(g_hwnd, SW_SHOW);
        break; /* owner снят: hide+show */
    case 7:
        SetWindowLongW(g_hwnd, GWL_EXSTYLE,
                       GetWindowLongW(g_hwnd, GWL_EXSTYLE) | WS_EX_LAYERED);
        SetLayeredWindowAttributes(g_hwnd, 0, 255, LWA_ALPHA);
        ShowWindow(g_hwnd, SW_HIDE);
        ShowWindow(g_hwnd, SW_SHOW);
        break; /* layered: hide+show */
    case 8:
        SetWindowLongW(g_hwnd, GWL_EXSTYLE,
                       GetWindowLongW(g_hwnd, GWL_EXSTYLE) | WS_EX_TOOLWINDOW);
        ShowWindow(g_hwnd, SW_HIDE);
        ShowWindow(g_hwnd, SW_SHOW);
        break; /* +toolwindow: hide+show */
    case 9:
        SetWindowLongW(g_hwnd, GWL_EXSTYLE,
                       GetWindowLongW(g_hwnd, GWL_EXSTYLE) | WS_EX_APPWINDOW);
        ShowWindow(g_hwnd, SW_HIDE);
        ShowWindow(g_hwnd, SW_SHOW);
        break; /* +appwindow: hide+show */
    case 10:
        ShowWindow(g_hwnd, SW_HIDE);
        break; /* финальный hide */
    case 11:
        DestroyWindow(g_hwnd);
        g_hwnd = CreateWindowExW(0, kCls, L"", WS_OVERLAPPEDWINDOW | WS_VISIBLE,
                                 60, 80, 360, 120, NULL, NULL, GetModuleHandleW(NULL), NULL);
        SetTimer(g_hwnd, 1, g_stepMs, NULL);
        break; /* создано сразу видимым, unowned */
    case 12:
        DestroyWindow(g_hwnd);
        g_hwnd = CreateWindowExW(0, kCls, L"", WS_OVERLAPPEDWINDOW | WS_VISIBLE,
                                 60, 100, 360, 120, g_helper, NULL, GetModuleHandleW(NULL), NULL);
        SetTimer(g_hwnd, 1, g_stepMs, NULL);
        break; /* создано видимым + owned */
    case 13:
        DestroyWindow(g_hwnd);
        g_hwnd = CreateWindowExW(WS_EX_APPWINDOW, kCls, L"", WS_OVERLAPPEDWINDOW | WS_VISIBLE,
                                 60, 120, 360, 120, g_helper, NULL, GetModuleHandleW(NULL), NULL);
        SetTimer(g_hwnd, 1, g_stepMs, NULL);
        break; /* создано видимым + owned + appwindow */
    case 14:
        SetForegroundWindow(g_hwnd);
        break; /* активация окна шага 13 */
    case 15:
        g_child = CreateWindowExW(0, kHelper, L"probe child", WS_CHILD | WS_VISIBLE,
                                  10, 10, 200, 40, g_hwnd, NULL, GetModuleHandleW(NULL), NULL);
        break; /* видимый child (модель Chrome_RenderWidgetHostHWND) */
    case 16:
        ShowWindow(g_child, SW_HIDE);
        break; /* скрыть child */
    case 17:
        ShowWindow(g_child, SW_SHOW);
        break; /* вернуть child */
    case 18:
        DestroyWindow(g_child);
        g_child = NULL;
        break; /* уничтожить child при живом родителе */
    case 19:
        g_child = CreateWindowExW(0, kHelper, L"probe child 2", WS_CHILD | WS_VISIBLE,
                                  10, 60, 200, 40, g_hwnd, NULL, GetModuleHandleW(NULL), NULL);
        break; /* пересоздать child: следующий шаг убьёт родителя с живым child */
    case 20:
        DestroyWindow(g_hwnd);
        g_hwnd = CreateWindowExW(0, kCls, L"probe exit-win", WS_OVERLAPPEDWINDOW | WS_VISIBLE,
                                 80, 140, 360, 120, NULL, NULL, GetModuleHandleW(NULL), NULL);
        g_child = CreateWindowExW(0, kHelper, L"probe exit-child", WS_CHILD | WS_VISIBLE,
                                  10, 10, 200, 40, g_hwnd, NULL, GetModuleHandleW(NULL), NULL);
        SetTimer(g_hwnd, 1, g_stepMs, NULL);
        break; /* окна для смерти процесса: следующий шаг завершает процесс
                  БЕЗ DestroyWindow — их убьёт win32k (модель закрытия ZCode) */
    default:
        Log(L"done");
        PostQuitMessage(0);
        return;
    }
    switch (g_step)
    {
    case 1: Step(1, L"show-unowned"); break;
    case 2: Step(2, L"hide"); break;
    case 3: Step(3, L"show-SetWindowPos"); break;
    case 4: Step(4, L"owner-set"); break;
    case 5: Step(5, L"owned-hide-show"); break;
    case 6: Step(6, L"unowned-hide-show"); break;
    case 7: Step(7, L"layered-hide-show"); break;
    case 8: Step(8, L"toolwindow-hide-show"); break;
    case 9: Step(9, L"appwindow-hide-show"); break;
    case 10: Step(10, L"final-hide"); break;
    case 11: Step(11, L"created-visible"); break;
    case 12: Step(12, L"created-visible-owned"); break;
    case 13: Step(13, L"created-visible-owned-appw"); break;
    case 14: Step(14, L"activate"); break;
    case 15: Log(L"step 15: child-create vis=%d", IsWindowVisible(g_child) ? 1 : 0); break;
    case 16: Log(L"step 16: child-hide vis=%d", IsWindowVisible(g_child) ? 1 : 0); break;
    case 17: Log(L"step 17: child-show vis=%d", IsWindowVisible(g_child) ? 1 : 0); break;
    case 18: Log(L"step 18: child-destroy"); break;
    case 19: Log(L"step 19: child-recreate vis=%d", IsWindowVisible(g_child) ? 1 : 0); break;
    case 20: Log(L"step 20: exit-windows created (child vis=%d)",
                  IsWindowVisible(g_child) ? 1 : 0); break;
    case 21:
        Log(L"step 21: process teardown (windows alive, NO DestroyWindow)");
        g_done = TRUE;
        PostQuitMessage(0);
        break;
    }
    g_step++;
}

static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    (void)lp;
    if (msg == WM_TIMER && wp == 1)
    {
        DoStep();
        return 0;
    }
    if (msg == WM_DESTROY && g_done)
        PostQuitMessage(0);
    return DefWindowProcW(hwnd, msg, wp, lp);
}

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE prev, PWSTR cmd, int show)
{
    WNDCLASSW wc;
    MSG msg;

    (void)prev; (void)show;
    g_stepMs = cmd && *cmd >= L'0' && *cmd <= L'9' ? _wtoi(cmd) * 1000 : 2000;
    g_log = _wfopen(L"probe.log", L"wb");
    if (g_log)
        fputwc(L'\xfeff', g_log);
    g_t0 = GetTickCount64();

    ZeroMemory(&wc, sizeof(wc));
    wc.lpfnWndProc = WndProc;
    wc.hInstance = inst;
    wc.hCursor = LoadCursorW(NULL, (LPCWSTR)IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);

    wc.lpszClassName = kCls;
    RegisterClassW(&wc);
    wc.lpfnWndProc = DefWindowProcW;
    wc.lpszClassName = kHelper;
    RegisterClassW(&wc);

    g_helper = CreateWindowExW(0, kHelper, L"", WS_POPUP,
                               0, 0, 0, 0, NULL, NULL, inst, NULL);
    /* создаётся СКРЫТЫМ: первый показ — шаг 1 */
    g_hwnd = CreateWindowExW(0, kCls, L"probe[0] created-hidden",
                             WS_OVERLAPPEDWINDOW,
                             40, 40, 360, 120, NULL, NULL, inst, NULL);
    Log(L"created hidden hwnd=%p helper=%p", g_hwnd, g_helper);
    g_step = 1;
    SetTimer(g_hwnd, 1, g_stepMs, NULL);

    while (GetMessageW(&msg, NULL, 0, 0) > 0)
    {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    if (g_log)
        fclose(g_log);
    return 0;
}
