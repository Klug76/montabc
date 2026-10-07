#include "icons.h"
#include "tracker.h"
#include "debug.h"
#include "util.h"
#include <commctrl.h>

/* Icon source priorities: lower is better. A shown icon is overwritten
   only by a reply with a better priority (lower number). */
#define PRIO_SMALL2   0 /* SendMessageCallback WM_GETICON ICON_SMALL2 */
#define PRIO_SMALL    1 /* SendMessageCallback WM_GETICON ICON_SMALL */
#define PRIO_BIG      2 /* SendMessageCallback WM_GETICON ICON_BIG */
#define PRIO_CLASS_SM 3 /* GetClassLongPtrW GCLP_HICONSM */
#define PRIO_CLASS_BG 4 /* GetClassLongPtrW GCLP_HICON */
#define PRIO_EXE      5 /* ExtractIconExW, as in the original */

/* Lists for different sizes (taskbars on monitors with different DPI) stay
   in sync: slot N holds the same icon in every list. */
#define ICON_MAX_LISTS 8

/* dwData for SendAsyncProc: priority in the high 32 bits, truncated hwnd
   in the low bits (USER/GDI handles are 32-bit sign-extended —
   documented; the project is x64 only). */
#define REQ_DATA(prio, hwnd) \
    ((((ULONG_PTR)(prio)) << 32) | ((ULONG_PTR)(DWORD)(UINT_PTR)(hwnd)))

static struct
{
    int size;
    HIMAGELIST il;
} s_lists[ICON_MAX_LISTS];
static int s_listCount;
static int s_slotCount;
static int *s_freeSlots;
static int s_freeCap;
static int s_freeCount;

/* Copy the icon into all lists; a freed slot is reused. */
static int AcquireSlot(HICON h)
{
    int i, slot;

    if (!s_listCount)
        return -1;
    if (s_freeCount)
    {
        slot = s_freeSlots[--s_freeCount];
        LOG(2, L"slot reuse=%d (lists=%d)", slot, s_listCount);
        for (i = 0; i < s_listCount; i++)
            ImageList_ReplaceIcon(s_lists[i].il, slot, h);
        return slot;
    }
    for (i = 0; i < s_listCount; i++)
        ImageList_AddIcon(s_lists[i].il, h);
    slot = s_slotCount++;
    LOG(2, L"slot append=%d (lists=%d)", slot, s_listCount);
    return slot;
}

static void ApplyIcon(WindowItem *it, HICON h, BYTE prio)
{
    ICONINFO ii;
    int slot;

    if (!h)
        return;
    /* A low-priority reply does not overwrite the already shown icon. */
    if (it->iconPrio != ICON_PRIO_NONE && prio >= it->iconPrio)
    {
        LOG(2, L"skip %08x [%s]: new prio=%d >= cur=%d",
            DBG_HEX(it->hwnd), Log_Wnd(it->hwnd), prio, (int)it->iconPrio);
        return;
    }
    /* Icon liveness check: GetIconInfo fails on a garbage handle. */
    if (!GetIconInfo(h, &ii))
    {
        LOG(2, L"skip %08x [%s]: invalid hicon %08x",
            DBG_HEX(it->hwnd), Log_Wnd(it->hwnd), DBG_HEX(h));
        return;
    }
    if (ii.hbmColor)
        DeleteObject(ii.hbmColor);
    DeleteObject(ii.hbmMask);

    slot = AcquireSlot(h);
    if (slot >= 0)
    {
        it->iconSlot = slot;
        it->iconPrio = prio;
        LOG(1, L"set %08x [%s]: prio=%d slot=%d",
            DBG_HEX(it->hwnd), Log_Wnd(it->hwnd), prio, slot);
        Trk_NotifyChanged();
    }
}

static void CALLBACK SendAsyncProc(HWND hwnd, UINT uMsg, ULONG_PTR dwData,
                                   LRESULT lResult)
{
    WindowItem *it;
    BYTE prio;
    HWND h;

    /* The docs leave the callback's hwnd parameter semantics ambiguous —
       take hwnd from dwData. */
    (void)hwnd;
    (void)uMsg;
    prio = (BYTE)(dwData >> 32);
    h = (HWND)(ULONG_PTR)(LONG)(DWORD)dwData;
    LOG(2, L"resp prio=%d hwnd=%08x [%s] hicon=%08x",
        prio, DBG_HEX(h), Log_Wnd(h), DBG_HEX(lResult));

    /* The window may die (or be removed from the tracker) before the reply. */
    if (!lResult || !IsWindow(h))
    {
        LOG(2, L"resp drop %08x [%s]: empty or dead", DBG_HEX(h), Log_Wnd(h));
        return;
    }
    it = Trk_Find(h);
    if (!it)
    {
        LOG(2, L"resp drop %08x [%s]: no item", DBG_HEX(h), Log_Wnd(h));
        return;
    }
    ApplyIcon(it, (HICON)lResult, prio);
}

/* Asynchronous WM_GETICON cascade: all three requests at once, replies FIFO.
   SendMessageCallback does not block and has no timeout. */
static void RequestAsync(HWND hwnd)
{
    static const WPARAM kinds[3] = {ICON_SMALL2, ICON_SMALL, ICON_BIG};
    static const BYTE prios[3] = {PRIO_SMALL2, PRIO_SMALL, PRIO_BIG};
    int i;

    for (i = 0; i < 3; i++)
        SendMessageCallbackW(hwnd, WM_GETICON, kinds[i], 0, SendAsyncProc,
                             REQ_DATA(prios[i], hwnd));
}

/* Synchronous fallbacks run immediately: a hung window will never answer
   WM_GETICON, and without this it would get no icon at all. Higher priority
   WM_GETICON replies will overwrite them when they arrive. */
static void SyncFallback(WindowItem *it)
{
    ULONG_PTR hsm, hbg;
    DWORD pid;
    HANDLE process;

    hsm = GetClassLongPtrW(it->hwnd, GCLP_HICONSM);
    if (hsm)
        ApplyIcon(it, (HICON)hsm, PRIO_CLASS_SM);
    hbg = GetClassLongPtrW(it->hwnd, GCLP_HICON);
    if (hbg)
        ApplyIcon(it, (HICON)hbg, PRIO_CLASS_BG);
    LOG(2, L"class %08x [%s]: sm=%08x bg=%08x",
        DBG_HEX(it->hwnd), Log_Wnd(it->hwnd), DBG_HEX(hsm), DBG_HEX(hbg));
    if (it->iconSlot >= 0)
        return;

    pid = 0;
    GetWindowThreadProcessId(it->hwnd, &pid);
    if (!pid)
        return;
    process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!process)
        return;

    {
        WCHAR path[520];
        DWORD len = 520;
        HICON small = NULL;
        BOOL ok = QueryFullProcessImageNameW(process, 0, path, &len) &&
                  len > 0 && ExtractIconExW(path, 0, NULL, &small, 1) > 0 && small;

        if (ok)
        {
            LOG(2, L"exe %08x [%s]: pid=%u path=%s icon=%08x",
                DBG_HEX(it->hwnd), Log_Wnd(it->hwnd), pid, path, DBG_HEX(small));
            ApplyIcon(it, small, PRIO_EXE);
            DestroyIcon(small); /* ImageList keeps its own copy */
        }
        else
        {
            LOG(2, L"exe %08x [%s]: no icon (pid=%u)",
                DBG_HEX(it->hwnd), Log_Wnd(it->hwnd), pid);
        }
    }

    CloseHandle(process);
}

void Icon_Paint(WindowItem *it, HDC hdc, int x, int y, int listIdx)
{
    if (!it->iconReq)
    {
        it->iconReq = 1;
        LOG(1, L"req %08x [%s]: cascade(3) + fallback",
            DBG_HEX(it->hwnd), Log_Wnd(it->hwnd));
        RequestAsync(it->hwnd);
        SyncFallback(it);
    }
    if (it->iconSlot >= 0 && listIdx >= 0)
        ImageList_Draw(s_lists[listIdx].il, it->iconSlot, hdc, x, y, ILD_NORMAL);
}

int Icon_ListFor(int size)
{
    HIMAGELIST il;
    int i, j, copied = 0;

    if (size <= 0)
        return -1;
    for (i = 0; i < s_listCount; i++)
        if (s_lists[i].size == size)
            return i;
    if (s_listCount == ICON_MAX_LISTS)
    {
        LOG(1, L"list size=%d: cache full (%d), reuse #0", size, s_listCount);
        return 0;
    }

    il = ImageList_Create(size, size, ILC_COLOR32, 32, 32);
    if (!il)
    {
        LOG(1, L"list size=%d: ImageList_Create failed", size);
        return -1;
    }
    /* Fill the new size with copies of already loaded icons from the first
       list (all lists are synchronized by slots). */
    for (j = 0; j < s_slotCount; j++)
    {
        HICON copy = ImageList_GetIcon(s_lists[0].il, j, ILD_NORMAL);
        if (copy)
        {
            ImageList_AddIcon(il, copy);
            DestroyIcon(copy);
            copied++;
        }
    }
    s_lists[s_listCount].size = size;
    s_lists[s_listCount].il = il;
    LOG(1, L"list #%d size=%d backfill=%d", s_listCount, size, copied);
    return s_listCount++;
}

void Icon_ReleaseSlot(int slot)
{
    if (slot < 0)
        return;
    if (!Util_Grow((void **)&s_freeSlots, &s_freeCap, s_freeCount + 1,
                   TRK_MAX_ITEMS, sizeof(int), L"icons"))
    {
        LOG(1, L"slot freed=%d: free-list full", slot);
        return;
    }
    s_freeSlots[s_freeCount++] = slot;
    LOG(2, L"slot freed=%d", slot);
}

void Icon_RebuildAll(void)
{
    int i;

    LOG(1, L"rebuild: lists=%d slots=%d", s_listCount, s_slotCount);
    for (i = 0; i < s_listCount; i++)
        ImageList_Destroy(s_lists[i].il);
    s_listCount = 0;
    s_slotCount = 0;
    s_freeCount = 0;
    Trk_ResetIcons();
}

void Icon_Shutdown(void)
{
    int i;

    LOG(1, L"shutdown: lists=%d slots=%d", s_listCount, s_slotCount);
    for (i = 0; i < s_listCount; i++)
        ImageList_Destroy(s_lists[i].il);
    s_listCount = 0;
    s_slotCount = 0;
    s_freeCount = 0;
    Util_Free(s_freeSlots);
    s_freeSlots = NULL;
    s_freeCap = 0;
}
