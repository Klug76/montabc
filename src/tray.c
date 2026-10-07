#include "tray.h"
#include "app.h"
#include "autostart.h"
#include "config.h"
#include "strings.h"
#include "version.h"

#define TRAY_CLASS L"montabc.tray"
#define TRAY_CALLBACK (WM_APP + 1)
#define TRAY_ICON_ID 1
/* Brightness and opacity of the icon in the "panels hidden" state */
#define TRAY_DIM_PERCENT 60

/* Monitor commands: CMD_MONITOR_BASE + index * MONITOR_STRIDE + action */
#define CMD_AUTOSTART 1
#define CMD_EXIT 2
#define CMD_MONITOR_BASE 100
#define MONITOR_STRIDE 4
#define MONITOR_TOGGLE 0
#define MONITOR_LEFT 1
#define MONITOR_RIGHT 2

static struct
{
    HWND hwnd;
    HICON icon;       /* colored: panels visible on screens */
    HICON iconHidden; /* dimmed: panels hidden */
    BOOL showingHidden;
    UINT taskbarCreated;
} T;

static void Add(void);
static void Remove(void);
static void ShowMenu(void);

static HICON LoadTrayIcon(HINSTANCE hInst, int cx, int cy)
{
    HANDLE handle = LoadImageW(hInst, MAKEINTRESOURCEW(1), IMAGE_ICON, cx, cy,
                               LR_DEFAULTCOLOR);

    /* No resource — fall back to the system placeholder icon */
    return handle ? (HICON)handle : LoadIconW(NULL, (LPCWSTR)IDI_APPLICATION);
}

/* Desaturated, dimmed copy of the icon — the "panels hidden" state.
   No separate resource on purpose: one image, the variant is derived from it. */
static HICON CreateDimmed(HICON source, int cx, int cy)
{
    BITMAPINFO bmi;
    void *bits;
    HBITMAP color, mask;
    HICON dimmed;
    HDC dc;
    HGDIOBJ oldBmp;
    DWORD *px;
    int i;

    ZeroMemory(&bmi, sizeof(bmi));
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = cx;
    bmi.bmiHeader.biHeight = -cy; /* top-down */
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    color = CreateDIBSection(NULL, &bmi, DIB_RGB_COLORS, &bits, NULL, 0);
    if (!color)
        return source;

    dc = CreateCompatibleDC(NULL);
    oldBmp = SelectObject(dc, color);
    DrawIconEx(dc, 0, 0, source, cx, cy, 0, NULL, DI_NORMAL);
    SelectObject(dc, oldBmp);
    DeleteDC(dc);

    /* Pixels are premultiplied: take gray as is, dim color and alpha together */
    px = (DWORD *)bits;
    for (i = 0; i < cx * cy; i++)
    {
        DWORD p = px[i];
        DWORD a = p >> 24;
        DWORD r, g, b, gray;
        if (a == 0)
            continue;
        b = p & 0xFF;
        g = (p >> 8) & 0xFF;
        r = (p >> 16) & 0xFF;
        gray = (r * 77 + g * 151 + b * 28) >> 8;
        gray = gray * TRAY_DIM_PERCENT / 100;
        a = a * TRAY_DIM_PERCENT / 100;
        px[i] = (a << 24) | (gray << 16) | (gray << 8) | gray;
    }

    /* A 32-bit icon does not really need a mask, but CreateIconIndirect requires one */
    mask = CreateBitmap(cx, cy, 1, 1, NULL);
    {
        ICONINFO info;
        info.fIcon = TRUE;
        info.xHotspot = 0;
        info.yHotspot = 0;
        info.hbmMask = mask;
        info.hbmColor = color;
        dimmed = CreateIconIndirect(&info);
    }

    DeleteObject(color);
    DeleteObject(mask);
    return dimmed ? dimmed : source;
}

static void Add(void)
{
    NOTIFYICONDATAW nid;
    const WCHAR *tip;

    ZeroMemory(&nid, sizeof(nid));
    T.showingHidden = App_IsHidden();
    nid.cbSize = sizeof(nid);
    nid.hWnd = T.hwnd;
    nid.uID = TRAY_ICON_ID;
    nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    nid.uCallbackMessage = TRAY_CALLBACK;
    nid.hIcon = T.showingHidden ? T.iconHidden : T.icon;
    tip = STR_S(L"montabc " APP_VERSION_W L" — панель окон",
                L"montabc " APP_VERSION_W L" — window panel");
    lstrcpynW(nid.szTip, tip, 128);
    Shell_NotifyIconW(NIM_ADD, &nid);
}

static void Remove(void)
{
    NOTIFYICONDATAW nid;

    if (!T.hwnd)
        return;
    ZeroMemory(&nid, sizeof(nid));
    nid.cbSize = sizeof(nid);
    nid.hWnd = T.hwnd;
    nid.uID = TRAY_ICON_ID;
    Shell_NotifyIconW(NIM_DELETE, &nid);
}

void Tray_SyncIcon(void)
{
    NOTIFYICONDATAW nid;
    const WCHAR *tip;

    if (!T.hwnd || T.showingHidden == App_IsHidden())
        return;

    ZeroMemory(&nid, sizeof(nid));
    T.showingHidden = App_IsHidden();
    nid.cbSize = sizeof(nid);
    nid.hWnd = T.hwnd;
    nid.uID = TRAY_ICON_ID;
    nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    nid.uCallbackMessage = TRAY_CALLBACK;
    nid.hIcon = T.showingHidden ? T.iconHidden : T.icon;
    tip = STR_S(L"montabc " APP_VERSION_W L" — панель окон",
                L"montabc " APP_VERSION_W L" — window panel");
    lstrcpynW(nid.szTip, tip, 128);
    Shell_NotifyIconW(NIM_MODIFY, &nid);
}

static DWORD Check(DWORD flags, BOOL checked)
{
    return checked ? flags | MF_CHECKED : flags;
}

static void ShowMenu(void)
{
    HMENU menu = CreatePopupMenu();
    POINT pt;
    UINT cmd;
    int i, count = App_DisplayCount();

    if (!menu)
        return;

    for (i = 0; i < count; i++)
    {
        const DisplayInfo *d = App_Display(i);
        const MonitorCfg *mon = Cfg_For(d->device);
        UINT baseCmd = CMD_MONITOR_BASE + (UINT)i * MONITOR_STRIDE;
        HMENU sub;
        WCHAR label[128];

        if (!mon)
            continue;
        sub = CreatePopupMenu();
        if (!sub)
            continue;

        AppendMenuW(sub, Check(MF_STRING, mon->enabled), baseCmd + MONITOR_TOGGLE,
                    STR_S(L"Включено", L"Enabled"));
        AppendMenuW(sub, MF_SEPARATOR, 0, NULL);
        AppendMenuW(sub, Check(MF_STRING, mon->edge == DOCK_LEFT), baseCmd + MONITOR_LEFT,
                    STR_S(L"Слева", L"Dock left"));
        AppendMenuW(sub, Check(MF_STRING, mon->edge == DOCK_RIGHT), baseCmd + MONITOR_RIGHT,
                    STR_S(L"Справа", L"Dock right"));

        /* "Display 1 · 2560×1440 (primary)" */
        wsprintfW(label, STR_S(L"Монитор %d · %d×%d", L"Display %d · %d×%d"),
                  i + 1, d->rc.right - d->rc.left, d->rc.bottom - d->rc.top);
        if (d->primary)
        {
            lstrcatW(label, STR_S(L" (основной)", L" (primary)"));
        }
        AppendMenuW(menu, MF_STRING | MF_POPUP, (UINT_PTR)sub, label);
    }

    AppendMenuW(menu, MF_SEPARATOR, 0, NULL);
    AppendMenuW(menu, Check(MF_STRING, Autostart_IsEnabled()), CMD_AUTOSTART,
                STR_S(L"Автозапуск", L"Start with Windows"));
    AppendMenuW(menu, MF_STRING, CMD_EXIT, STR_S(L"Выход", L"Exit"));

    GetCursorPos(&pt);
    /* Classic pair from MSDN: without it the tray menu does not close on an outside click */
    SetForegroundWindow(T.hwnd);
    cmd = TrackPopupMenu(menu,
                         TPM_RETURNCMD | TPM_RIGHTBUTTON | TPM_RIGHTALIGN | TPM_BOTTOMALIGN,
                         pt.x, pt.y, 0, T.hwnd, NULL);
    PostMessageW(T.hwnd, WM_NULL, 0, 0);
    DestroyMenu(menu); /* along with submenus */

    if (cmd >= CMD_MONITOR_BASE)
    {
        int index = (int)((cmd - CMD_MONITOR_BASE) / MONITOR_STRIDE);
        const DisplayInfo *d;
        if (index >= App_DisplayCount())
            return;
        d = App_Display(index);
        switch ((cmd - CMD_MONITOR_BASE) % MONITOR_STRIDE)
        {
        case MONITOR_TOGGLE:
            App_SetEnabled(d->device, !Cfg_For(d->device)->enabled);
            break;
        case MONITOR_LEFT:
            App_SetEdge(d->device, DOCK_LEFT);
            break;
        case MONITOR_RIGHT:
            App_SetEdge(d->device, DOCK_RIGHT);
            break;
        }
        return;
    }

    switch (cmd)
    {
    case CMD_AUTOSTART:
        Autostart_Toggle();
        break;
    case CMD_EXIT:
        App_Exit();
        break;
    }
}

static LRESULT CALLBACK Tray_WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    if (msg == T.taskbarCreated && T.taskbarCreated)
    {
        /* Explorer restarted — the icon must be re-added */
        Add();
        return 0;
    }

    switch (msg)
    {
    case TRAY_CALLBACK:
    {
        UINT mouse = (UINT)(lp & 0xFFFF);
        if (mouse == WM_LBUTTONUP)
            App_ToggleHidden(); /* quick "hide/restore panels on all screens" */
        else if (mouse == WM_RBUTTONUP)
            ShowMenu();
        return 0;
    }

    case WM_ENDSESSION:
        /* Windows shutdown/reboot: WM_DESTROY may never arrive */
        if (wp)
            Cfg_Save();
        return 0;

    case WM_CLOSE:
        App_Exit();
        return 0;

    case WM_DESTROY:
        Remove();
        T.hwnd = NULL;
        return 0;
    }

    return DefWindowProcW(hwnd, msg, wp, lp);
}

void Tray_Create(HINSTANCE hInst)
{
    WNDCLASSEXW wc;
    int cx, cy;

    ZeroMemory(&wc, sizeof(wc));
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = Tray_WndProc;
    wc.hInstance = hInst;
    wc.lpszClassName = TRAY_CLASS;
    RegisterClassExW(&wc);

    /* The window is never shown, but is a normal (not message-only) one: a popup
       menu requires an owner capable of becoming the foreground window. */
    /* WndProc starts firing inside CreateWindowExW, so its state must be ready */
    T.taskbarCreated = RegisterWindowMessageW(L"TaskbarCreated");
    cx = GetSystemMetrics(SM_CXSMICON);
    cy = GetSystemMetrics(SM_CYSMICON);

    T.hwnd = CreateWindowExW(WS_EX_TOOLWINDOW, TRAY_CLASS, APP_NAME, WS_POPUP,
                             0, 0, 0, 0, NULL, NULL, hInst, NULL);
    T.icon = LoadTrayIcon(hInst, cx, cy);
    T.iconHidden = CreateDimmed(T.icon, cx, cy);
    Add();
}

void Tray_Destroy(void)
{
    Remove();
    if (T.hwnd)
    {
        DestroyWindow(T.hwnd);
        T.hwnd = NULL;
    }
    if (T.iconHidden && T.iconHidden != T.icon)
        DestroyIcon(T.iconHidden);
    if (T.icon)
        DestroyIcon(T.icon);
    T.icon = T.iconHidden = NULL;
}
