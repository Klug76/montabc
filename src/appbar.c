#include "appbar.h"

static void FillData(APPBARDATA *abd, const AppBar *ab)
{
    abd->cbSize = sizeof(*abd);
    abd->hWnd = ab->hwnd;
    abd->uCallbackMessage = ab->callbackMsg;
}

void AppBar_Init(AppBar *ab, HWND hwnd, UINT callbackMsg)
{
    ab->hwnd = hwnd;
    ab->callbackMsg = callbackMsg;
    ab->registered = FALSE;
}

void AppBar_Register(AppBar *ab)
{
    APPBARDATA abd;

    if (ab->registered)
        return;
    FillData(&abd, ab);
    SHAppBarMessage(ABM_NEW, &abd);
    ab->registered = TRUE;
}

void AppBar_Unregister(AppBar *ab)
{
    APPBARDATA abd;

    if (!ab->registered)
        return;
    FillData(&abd, ab);
    SHAppBarMessage(ABM_REMOVE, &abd);
    ab->registered = FALSE;
}

/* Полоса строго заданной ширины, прижатая к краю. */
static void ImposeWidth(RECT *rc, DockEdge edge, int width)
{
    if (edge == DOCK_LEFT)
        rc->right = rc->left + width;
    else
        rc->left = rc->right - width;
}

RECT AppBar_SetPos(AppBar *ab, DockEdge edge, RECT monitor, int width)
{
    APPBARDATA abd;

    FillData(&abd, ab);
    abd.uEdge = (edge == DOCK_LEFT) ? ABE_LEFT : ABE_RIGHT;
    abd.rc = monitor;
    ImposeWidth(&abd.rc, edge, width);

    SHAppBarMessage(ABM_QUERYPOS, &abd);
    ImposeWidth(&abd.rc, edge, width); /* QUERYPOS искажает ширину */

    SHAppBarMessage(ABM_SETPOS, &abd);
    return abd.rc;
}
