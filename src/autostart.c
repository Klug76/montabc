#include "autostart.h"

#define RUN_KEY L"Software\\Microsoft\\Windows\\CurrentVersion\\Run"

BOOL Autostart_IsEnabled(void)
{
    WCHAR buf[MAX_PATH + 4];
    DWORD size = sizeof(buf);

    return RegGetValueW(HKEY_CURRENT_USER, RUN_KEY, APP_NAME,
                        RRF_RT_REG_SZ, NULL, buf, &size) == ERROR_SUCCESS;
}

void Autostart_Toggle(void)
{
    WCHAR path[MAX_PATH], quoted[MAX_PATH + 4];
    HKEY key;
    LONG r;

    if (Autostart_IsEnabled())
    {
        RegDeleteKeyValueW(HKEY_CURRENT_USER, RUN_KEY, APP_NAME);
        return;
    }

    if (!GetModuleFileNameW(NULL, path, MAX_PATH))
        return;
    wsprintfW(quoted, L"\"%s\"", path);

    r = RegCreateKeyExW(HKEY_CURRENT_USER, RUN_KEY, 0, NULL, 0, KEY_SET_VALUE,
                        NULL, &key, NULL);
    if (r != ERROR_SUCCESS)
        return;
    RegSetValueExW(key, APP_NAME, 0, REG_SZ, (const BYTE *)quoted,
                   (DWORD)((lstrlenW(quoted) + 1) * sizeof(WCHAR)));
    RegCloseKey(key);
}
