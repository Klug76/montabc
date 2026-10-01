#pragma once

#include "montabc.h"

/* Переключение foreground-окон с MRU-историей активации: клик по уже
   активному окну (и фокус после скрытия) идёт в последнее по истории
   открытое окно; скрытые в полоску исключаются фильтром. */
typedef struct Switcher
{
    HWND history[32]; /* голова — самое свежее foreground-окно */
    int count;

    /* Фильтр целей автоперехода (панель исключает свёрнутые). */
    BOOL (*IsEligible)(HWND hwnd);
} Switcher;

void Sw_OnForegroundChanged(Switcher *s, HWND hwnd);
void Sw_Activate(Switcher *s, HWND target);

/* Активирует последнее по истории открытое окно, пропуская указанное,
   закрытые окна и всё, что не проходит фильтр. */
void Sw_ActivateMostRecentExcept(Switcher *s, HWND except);
