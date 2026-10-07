#include "switch.h"

#define VK_MENU_ALT 0x12 /* VK_MENU (Alt) */

static void ActivateCore(Switcher *s, HWND goal)
{
    (void)s;
    if (IsIconic(goal))
        ShowWindow(goal, SW_RESTORE);

    if (!SetForegroundWindow(goal))
    {
        /* The panel is not activatable (WS_EX_NOACTIVATE), so the system may
           hold a foreground lock. Simulating an Alt press releases it. */
        keybd_event(VK_MENU_ALT, 0, 0, 0);
        keybd_event(VK_MENU_ALT, 0, KEYEVENTF_KEYUP, 0);
        SetForegroundWindow(goal);
    }
}

void Sw_OnForegroundChanged(Switcher *s, HWND hwnd)
{
    int i, idx = -1;

    if (!hwnd)
        return;

    for (i = 0; i < s->count; i++)
        if (s->history[i] == hwnd)
        {
            idx = i;
            break;
        }

    if (idx >= 0)
    {
        for (i = idx; i > 0; i--)
            s->history[i] = s->history[i - 1];
    }
    else
    {
        if (s->count < 32)
            s->count++;
        for (i = s->count - 1; i > 0; i--)
            s->history[i] = s->history[i - 1];
    }
    s->history[0] = hwnd;
}

void Sw_Activate(Switcher *s, HWND target)
{
    ActivateCore(s, target);
}

void Sw_ActivateMostRecentExcept(Switcher *s, HWND except)
{
    int i;

    for (i = 0; i < s->count; i++)
    {
        HWND hwnd = s->history[i];
        if (hwnd == except || !IsWindow(hwnd))
            continue;
        if (s->IsEligible && !s->IsEligible(hwnd))
            continue;
        ActivateCore(s, hwnd);
        return;
    }
}
