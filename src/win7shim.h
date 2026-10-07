#pragma once

/* Constants declared in the SDK only for WINVER >= 0x0602/0x0605.
   Harmless on Win7: the messages/events simply never arrive. */
#ifndef WM_DPICHANGED
#define WM_DPICHANGED 0x02E0
#endif

#ifndef EVENT_OBJECT_CLOAKED
#define EVENT_OBJECT_CLOAKED 0x8017
#endif

#ifndef EVENT_OBJECT_UNCLOAKED
#define EVENT_OBJECT_UNCLOAKED 0x8018
#endif

#ifdef MONTABC_WIN7
/* GetDpiForWindow appeared in Win10 1607: a direct import would prevent the
   exe from loading on Win7, so the call goes through a shim via GetProcAddress. */
UINT Shim_GetWindowDpi(HWND hwnd);
#define GetDpiForWindow Shim_GetWindowDpi
#endif

/* GetWindowBand is not declared in SDK headers and not importable from
   user32.lib — available only via GetProcAddress, and not only on Win7, so
   the shim is shared by all configurations. */
HRESULT Shim_GetWindowBand(HWND hwnd, DWORD *band);
#define GetWindowBand Shim_GetWindowBand

/* ZBID is not in the SDK either. Only these two bands are allowed — the rest
   (immersive/system/lock) is cut off by the XAML taskbar ZBID table
   (Taskbar.dll rdata 0x27DB54; see experimental/riddle-solved.md). */
#define ZBID_DESKTOP 1
#define ZBID_UIACCESS 2
