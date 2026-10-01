#pragma once

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#include "win7shim.h"

#define APP_NAME L"montabc"
#define APP_MAX_MONITORS 16

typedef enum { DOCK_LEFT = 0, DOCK_RIGHT = 1 } DockEdge;
