#pragma once

#include "montabc.h"

/* Autostart: HKCU\...\Run — only on an explicit user command. */
BOOL Autostart_IsEnabled(void);
void Autostart_Toggle(void);
