#ifndef APP_VERSION_H
#define APP_VERSION_H

/* Single product version: VERSIONINFO (res/app.rc) and the tray tooltip
   (src/tray.c). The header is included by rc.exe too; it does not expand
   nested macros, so the string is a literal. When bumping the version, change
   the numbers and string here AND the FileVersion/ProductVersion values in res/app.rc. */
#define APP_VERSION_MAJOR 1
#define APP_VERSION_MINOR 3
#define APP_VERSION_PATCH 0
#define APP_VERSION_BUILD 0

/* 1,2,0,0 — for FILEVERSION/PRODUCTVERSION */
#define APP_FILEVERSION APP_VERSION_MAJOR,APP_VERSION_MINOR,APP_VERSION_PATCH,APP_VERSION_BUILD
#define APP_VERSION_W   L"1.3.0.0"

#endif
