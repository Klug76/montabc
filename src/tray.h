#pragma once

#include "montabc.h"

void Tray_Create(HINSTANCE hInst);
void Tray_Destroy(void);

/* Syncs the icon with the panel state (called by the host). */
void Tray_SyncIcon(void);
