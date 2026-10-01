#pragma once

#include "montabc.h"

/* Автозапуск: HKCU\...\Run — только по явной команде пользователя. */
BOOL Autostart_IsEnabled(void);
void Autostart_Toggle(void);
