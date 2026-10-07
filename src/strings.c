#include "strings.h"

int g_ru;

void Str_Init(void)
{
    /* 0x19 — PRIMARYLANGID of Russian */
    g_ru = (GetUserDefaultUILanguage() & 0x3FF) == 0x19;
}
