#pragma once

#include "montabc.h"

/* 1 = Russian UI; selected once from the system language. */
extern int g_ru;

void Str_Init(void);

#define STR_S(ru, en) ((g_ru) ? (ru) : (en))
