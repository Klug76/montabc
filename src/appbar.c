#include "appbar.h"
#include "debug.h"

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
    UINT_PTR res;

    if (ab->registered)
        return;
    FillData(&abd, ab);
    res = SHAppBarMessage(ABM_NEW, &abd);
    ab->registered = TRUE;
    LOG(1, L"appbar ABM_NEW %08x [%s] -> %u",
        DBG_HEX(ab->hwnd), Log_Wnd(ab->hwnd), (unsigned)res);
}

void AppBar_Unregister(AppBar *ab)
{
    APPBARDATA abd;
    UINT_PTR res;

    if (!ab->registered)
        return;
    FillData(&abd, ab);
    res = SHAppBarMessage(ABM_REMOVE, &abd);
    ab->registered = FALSE;
    LOG(1, L"appbar ABM_REMOVE %08x [%s] -> %u",
        DBG_HEX(ab->hwnd), Log_Wnd(ab->hwnd), (unsigned)res);
}

/* A strip of exact width, pinned to the edge. */
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
    ImposeWidth(&abd.rc, edge, width); /* QUERYPOS distorts the width */

    SHAppBarMessage(ABM_SETPOS, &abd);
    LOG(2, L"appbar SETPOS %08x [%s] edge=%s rc=(%d,%d)-(%d,%d)",
        DBG_HEX(ab->hwnd), Log_Wnd(ab->hwnd),
        edge == DOCK_LEFT ? L"left" : L"right",
        abd.rc.left, abd.rc.top, abd.rc.right, abd.rc.bottom);
    return abd.rc;
}
