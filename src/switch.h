#pragma once

#include "montabc.h"

/* Foreground window switching with an MRU activation history: a click on the
   already active window (and focus after hiding) goes to the most recently
   opened window in the history; windows hidden to a strip are excluded by the filter. */
typedef struct Switcher
{
    HWND history[32]; /* head — the most recent foreground window */
    int count;

    /* Filter for auto-switch targets (the panel excludes minimized ones). */
    BOOL (*IsEligible)(HWND hwnd);
} Switcher;

void Sw_OnForegroundChanged(Switcher *s, HWND hwnd);
void Sw_Activate(Switcher *s, HWND target);

/* Activates the most recently opened window in the history, skipping the given
   one, closed windows, and anything that fails the filter. */
void Sw_ActivateMostRecentExcept(Switcher *s, HWND except);
