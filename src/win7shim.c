#include <windows.h>

#ifdef MONTABC_WIN7

typedef UINT (WINAPI *PFN_GetDpiForWindow)(HWND);

UINT Shim_GetWindowDpi(HWND hwnd)
{
    static PFN_GetDpiForWindow pfn;
    static BOOL init;
    HDC hdc;
    UINT dpi;

    if (!init)
    {
        init = TRUE;
        pfn = (PFN_GetDpiForWindow)GetProcAddress(
            GetModuleHandleW(L"user32.dll"), "GetDpiForWindow");
    }
    if (pfn)
        return pfn(hwnd);

    hdc = GetDC(hwnd);
    dpi = hdc ? (UINT)GetDeviceCaps(hdc, LOGPIXELSX) : 96;
    if (hdc)
        ReleaseDC(hwnd, hdc);
    return dpi;
}

#endif
