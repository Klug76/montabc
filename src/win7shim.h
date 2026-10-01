#pragma once

/* Константы, объявленные в SDK только для WINVER >= 0x0602/0x0605.
   На Win7 безвредны: сообщения/события просто не приходят. */
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
/* GetDpiForWindow появился в Win10 1607: прямой импорт не даст exe
   загрузиться на Win7, поэтому вызов уходит в шим через GetProcAddress. */
UINT Shim_GetWindowDpi(HWND hwnd);
#define GetDpiForWindow Shim_GetWindowDpi
#endif
