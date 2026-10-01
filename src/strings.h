#pragma once

#include "montabc.h"

/* 1 = русский интерфейс; выбирается один раз по языку системы. */
extern int g_ru;

void Str_Init(void);

#define STR_S(ru, en) ((g_ru) ? (ru) : (en))
