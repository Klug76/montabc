#ifndef APP_VERSION_H
#define APP_VERSION_H

/* Единая версия продукта: VERSIONINFO (res/app.rc) и тултип трея (src/tray.c).
   Заголовок включается и rc.exe; он не разворачивает вложенные макросы,
   поэтому строка задана литералом. При подъёме версии менять числа и строку
   здесь И value-строки FileVersion/ProductVersion в res/app.rc. */
#define APP_VERSION_MAJOR 1
#define APP_VERSION_MINOR 2
#define APP_VERSION_PATCH 0
#define APP_VERSION_BUILD 0

/* 1,2,0,0 — для FILEVERSION/PRODUCTVERSION */
#define APP_FILEVERSION APP_VERSION_MAJOR,APP_VERSION_MINOR,APP_VERSION_PATCH,APP_VERSION_BUILD
#define APP_VERSION_W   L"1.2.0.0"

#endif
