#pragma once

#include "montabc.h"

void Tray_Create(HINSTANCE hInst);
void Tray_Destroy(void);

/* Приводит иконку в соответствие состоянию панелей (вызывает host). */
void Tray_SyncIcon(void);
