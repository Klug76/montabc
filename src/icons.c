#include "icons.h"

HICON Icon_GetWindow(HWND hwnd, BOOL *owned)
{
    /* 2=ICON_SMALL2, 0=ICON_SMALL, 1=ICON_BIG */
    static const WPARAM kinds[3] = {2, 0, 1};
    DWORD_PTR result;
    DWORD pid;
    HANDLE process;
    int i;

    *owned = FALSE;

    for (i = 0; i < 3; i++)
    {
        result = 0;
        SendMessageTimeoutW(hwnd, WM_GETICON, kinds[i], 0,
                            SMTO_ABORTIFHUNG | SMTO_BLOCK, 200, &result);
        if (result)
            return (HICON)result;
    }

    result = GetClassLongPtrW(hwnd, GCLP_HICONSM);
    if (!result)
        result = GetClassLongPtrW(hwnd, GCLP_HICON);
    if (result)
        return (HICON)result;

    pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (!pid)
        return NULL;

    process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!process)
        return NULL;

    {
        WCHAR path[520];
        DWORD len = 520;
        HICON small = NULL;

        if (QueryFullProcessImageNameW(process, 0, path, &len) && len > 0 &&
            ExtractIconExW(path, 0, NULL, &small, 1) > 0 && small)
        {
            *owned = TRUE;
            CloseHandle(process);
            return small;
        }
    }

    CloseHandle(process);
    return NULL;
}
