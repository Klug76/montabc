#pragma once

#include "montabc.h"

/* Debug logging, Debug builds only: MONTABC_LOG is defined in the
   vcxproj only for Debug — in Release all the code is cut by the preprocessor.
   montabc.log is created next to the exe, lines are appended one by one.

   Level — the g_debugLevel variable (0 off, 1 key events, 2 verbose);
   overridden before startup by the MONTABC_DEBUG=<0..2> environment variable.
   LOG requires at least one argument after the format; for messages without
   arguments there is LOG0. */
#ifdef MONTABC_LOG

extern int g_debugLevel;

void Log_Init(void);
void Log_Printf(int level, const WCHAR *fmt, ...);

/* Window for the log as "title:class" (<dead>/<null> if none). Rotation
   buffers — no more than 4 calls per single log line. */
const WCHAR *Log_Wnd(HWND hwnd);

#define LOG(level, fmt, ...)                                        \
    do                                                              \
    {                                                               \
        if (g_debugLevel >= (level))                                \
            Log_Printf((level), (fmt), __VA_ARGS__);                \
    } while (0)

/* Variant without arguments (LOG requires at least one argument). */
#define LOG0(level, fmt)                                            \
    do                                                              \
    {                                                               \
        if (g_debugLevel >= (level))                                \
            Log_Printf((level), (fmt));                             \
    } while (0)

#else

#define LOG(level, fmt, ...) ((void)0)
#define LOG0(level, fmt) ((void)0)
#define Log_Init() ((void)0)

#endif

/* Handles for the log: USER/GDI handles are 32-bit — printed as %08x. */
#define DBG_HEX(h) ((unsigned)(UINT_PTR)(h))
