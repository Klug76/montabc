/* deltab — окно для проверки механизмов скрытия кнопки таскбара.
   Запуск: deltab.exe             — CAPTION-окно, при старте DeleteTab;
          deltab.exe add          — CAPTION-окно, API не вызываем (контроль);
          deltab.exe borderless   — стили RoamingWindow (WS_POPUP|WS_SYSMENU +
                                    WS_EX_LAYERED), API не вызываем: даст ли
                                    оболочка кнопку такому окну сама?
   Клик по окну переключает DeleteTab <-> AddTab; состояние — в заголовке.
   Сборка: tests\build-deltab.bat. Смотрим tests\wininfo с фильтром "deltab". */

#include <windows.h>
#include <initguid.h>
#include <shobjidl_core.h>

static ITaskbarList *g_ptbl;
static BOOL g_deleted;
static BOOL g_borderless;
static BOOL g_delayedtool;
static int g_toolPhase;
static const wchar_t *g_caption;

static void UpdateState(HWND hwnd, BOOL deleted)
{
    g_deleted = deleted;
    if (g_ptbl)
    {
        if (deleted)
            g_ptbl->lpVtbl->DeleteTab(g_ptbl, hwnd);
        else
            g_ptbl->lpVtbl->AddTab(g_ptbl, hwnd);
    }
    SetWindowTextW(hwnd, deleted ? L"DeleteTab test [deleted]"
                                 : L"DeleteTab test [on taskbar]");
    InvalidateRect(hwnd, NULL, TRUE);
}

static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    PAINTSTRUCT ps;
    HDC dc;
    static const wchar_t *kDel = L"ITaskbarList::DeleteTab - taskbar button hidden";
    static const wchar_t *kAdd = L"ITaskbarList::AddTab - taskbar button visible";
    static const wchar_t *kBor = L"borderless layered, no API calls - check the taskbar";
    static const wchar_t *kHint = L"click the window to toggle";

    switch (msg)
    {
    case WM_CREATE:
        if (SUCCEEDED(CoCreateInstance(&CLSID_TaskbarList, NULL, CLSCTX_INPROC_SERVER,
                                       &IID_ITaskbarList, (void **)&g_ptbl)))
            g_ptbl->lpVtbl->HrInit(g_ptbl);
        if (g_borderless)
        {
            SetWindowTextW(hwnd, g_caption);
            if (g_delayedtool)
                SetTimer(hwnd, 2, 1000, NULL);
        }
        else
            UpdateState(hwnd, !GetCommandLineW() || !wcsstr(GetCommandLineW(), L" add"));
        return 0;

    case WM_TIMER:
        if (wp == 2)
        {
            LONG_PTR ex = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
            g_toolPhase++;
            if (g_toolPhase == 1)
                SetWindowLongPtrW(hwnd, GWL_EXSTYLE, ex | WS_EX_TOOLWINDOW);
            else
            {
                SetWindowLongPtrW(hwnd, GWL_EXSTYLE, ex & ~WS_EX_TOOLWINDOW);
                KillTimer(hwnd, 2);
            }
            return 0;
        }
        break;

    case WM_LBUTTONDOWN:
        (void)wp; (void)lp;
        UpdateState(hwnd, !g_deleted);
        return 0;

    case WM_PAINT:
        dc = BeginPaint(hwnd, &ps);
        SetBkMode(dc, TRANSPARENT);
        TextOutW(dc, 16, 16,
                 g_borderless && !g_deleted ? kBor : (g_deleted ? kDel : kAdd),
                 lstrlenW(g_borderless && !g_deleted ? kBor : (g_deleted ? kDel : kAdd)));
        TextOutW(dc, 16, 40, kHint, lstrlenW(kHint));
        EndPaint(hwnd, &ps);
        return 0;

    case WM_DESTROY:
        if (g_ptbl)
        {
            g_ptbl->lpVtbl->Release(g_ptbl);
            g_ptbl = NULL;
        }
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE prev, PWSTR cmd, int show)
{
    static const wchar_t kCls[] = L"deltab.test";
    static const wchar_t kHelper[] = L"deltab.helper";
    WNDCLASSW wc;
    HWND hwnd, owner = NULL;
    MSG msg;
    DWORD style, exStyle;
    BOOL toolFirst, ownerFirst;

    (void)prev; (void)cmd;

    if (FAILED(CoInitializeEx(NULL, COINIT_APARTMENTTHREADED)))
        return 1;

    ZeroMemory(&wc, sizeof(wc));
    wc.lpfnWndProc = WndProc;
    wc.hInstance = inst;
    wc.hCursor = LoadCursorW(NULL, (LPCWSTR)IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszClassName = kCls;
    RegisterClassW(&wc);

    ZeroMemory(&wc, sizeof(wc));
    wc.lpfnWndProc = DefWindowProcW;
    wc.hInstance = inst;
    wc.lpszClassName = kHelper;
    RegisterClassW(&wc);

    toolFirst = GetCommandLineW() && wcsstr(GetCommandLineW(), L" toolfirst") != NULL;
    ownerFirst = GetCommandLineW() && wcsstr(GetCommandLineW(), L" ownerfirst") != NULL;
    g_delayedtool = GetCommandLineW() && wcsstr(GetCommandLineW(), L" delayedtool") != NULL;
    g_borderless = toolFirst || ownerFirst || g_delayedtool ||
        (GetCommandLineW() && wcsstr(GetCommandLineW(), L" borderless") != NULL);
    g_caption = toolFirst ? L"DeleteTab test [toolfirst]"
              : ownerFirst ? L"DeleteTab test [ownerfirst]"
              : g_delayedtool ? L"DeleteTab test [delayedtool]"
              : L"DeleteTab test [borderless]";

    style = g_borderless
        ? WS_POPUP | WS_SYSMENU | WS_CLIPSIBLINGS | WS_CLIPCHILDREN | WS_VISIBLE
        : WS_OVERLAPPEDWINDOW;
    exStyle = (g_borderless ? WS_EX_LAYERED : 0) | (toolFirst ? WS_EX_TOOLWINDOW : 0);

    if (ownerFirst)
        owner = CreateWindowExW(0, kHelper, L"", WS_POPUP,
                                0, 0, 0, 0, NULL, NULL, inst, NULL);

    hwnd = CreateWindowExW(exStyle, kCls, L"DeleteTab test", style,
                           CW_USEDEFAULT, CW_USEDEFAULT, 520, 160,
                           owner, NULL, inst, NULL);
    if (g_borderless)
        SetLayeredWindowAttributes(hwnd, 0, 255, LWA_ALPHA);
    ShowWindow(hwnd, show);
    UpdateWindow(hwnd);

    if (toolFirst)
        SetWindowLongPtrW(hwnd, GWL_EXSTYLE,
                          GetWindowLongPtrW(hwnd, GWL_EXSTYLE) & ~WS_EX_TOOLWINDOW);
    if (ownerFirst)
        SetWindowLongPtrW(hwnd, GWLP_HWNDPARENT, (LONG_PTR)NULL);

    while (GetMessageW(&msg, NULL, 0, 0) > 0)
    {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    CoUninitialize();
    return (int)msg.wParam;
}
