#include "debug.h"
#include "version.h"
#include <stdarg.h>

#ifdef MONTABC_LOG

/* TEMPORARY: verbose log by default; set back to 1 before release. */
int g_debugLevel = 2;

static WCHAR s_path[MAX_PATH];
static WCHAR s_wndBuf[4][128];
static int s_wndRot;

const WCHAR *Log_Wnd(HWND hwnd)
{
    WCHAR *buf = s_wndBuf[s_wndRot++ & 3];
    WCHAR title[56], cls[56];

    if (!hwnd)
    {
        lstrcpynW(buf, L"<null>", 128);
        return buf;
    }
    title[0] = 0;
    cls[0] = 0;
    /* No messages are sent to foreign windows — no blocking. */
    GetWindowTextW(hwnd, title, 56);
    GetClassNameW(hwnd, cls, 56);
    if (!title[0] && !cls[0])
        lstrcpynW(buf, L"<dead>", 128);
    else
        wsprintfW(buf, L"%s:%s", title, cls);
    return buf;
}

void Log_Init(void)
{
    WCHAR buf[4];
    DWORD len;

    len = GetModuleFileNameW(NULL, s_path, MAX_PATH);
    while (len > 0 && s_path[len - 1] != L'\\')
        len--;
    lstrcpyW(s_path + len, L"montabc.log");

    if (GetEnvironmentVariableW(L"MONTABC_DEBUG", buf, 4) > 0 &&
        buf[0] >= L'0' && buf[0] <= L'2')
        g_debugLevel = buf[0] - L'0';

    if (g_debugLevel >= 1)
        Log_Printf(1, L"=== montabc v%s, %S %S, level=%d ===",
                   APP_VERSION_W, __DATE__, __TIME__, g_debugLevel);
}

void Log_Printf(int level, const WCHAR *fmt, ...)
{
    WCHAR line[1024];
    WCHAR out[1120];
    char utf8[3500];
    SYSTEMTIME st;
    va_list args;
    HANDLE file;
    int head, body, n;
    DWORD len, written;

    (void)level;
    if (g_debugLevel < 1)
        return;

    GetLocalTime(&st);
    head = wsprintfW(out, L"[%02u:%02u:%02u.%03u] ",
                     st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);
    va_start(args, fmt);
    body = wvsprintfW(line, fmt, args);
    va_end(args);
    if (head <= 0 || body <= 0)
        return;

    lstrcpynW(out + head, line, 1024);
    len = (DWORD)(head + body);
    out[len++] = L'\r';
    out[len++] = L'\n';

    /* UTF-8 — so the log is readable both in editors and in grep. */
    n = WideCharToMultiByte(CP_UTF8, 0, out, (int)len,
                            utf8, sizeof(utf8), NULL, NULL);
    if (n <= 0)
        return;

    /* FILE_APPEND_DATA: WriteFile appends to the end atomically. */
    file = CreateFileW(s_path, FILE_APPEND_DATA, FILE_SHARE_READ, NULL,
                       OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file != INVALID_HANDLE_VALUE)
    {
        WriteFile(file, utf8, (DWORD)n, &written, NULL);
        CloseHandle(file);
    }
}

#endif /* MONTABC_LOG */
