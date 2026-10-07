#include <windows.h>
#include "win7shim.h"

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

typedef HRESULT (WINAPI *PFN_GetWindowBand)(HWND, DWORD *);

HRESULT Shim_GetWindowBand(HWND hwnd, DWORD *band)
{
    static PFN_GetWindowBand pfn;
    static BOOL init;

    if (!init)
    {
        init = TRUE;
        pfn = (PFN_GetWindowBand)(void *)GetProcAddress(
            GetModuleHandleW(L"user32.dll"), "GetWindowBand");
    }
    /* API missing (Win7/8) or the call failed: the window is treated as
       the desktop band — not lost (BandAllowed semantics from tests/hooklist.c). */
    if (pfn && SUCCEEDED(pfn(hwnd, band)))
        return S_OK;
    *band = ZBID_DESKTOP;
    return S_OK;
}
