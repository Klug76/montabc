#pragma once

#include "montabc.h"
#include "tracker.h"

/* Element without an icon: "not set" priority (lower is better). */
#define ICON_PRIO_NONE 0xFF

/* Paint the item's icon. The first call sends the asynchronous WM_GETICON
   cascade (SendMessageCallback — the reply arrives on the UI thread) and the
   synchronous class/exe fallbacks; until an icon exists it draws nothing
   (the icon "blinks" into place). A low-priority reply does not overwrite the
   already shown icon. listIdx — from Icon_ListFor for the taskbar icon size. */
void Icon_Paint(WindowItem *it, HDC hdc, int x, int y, int listIdx);

/* ImageList index for a size (creates and backfills with copies when needed). */
int Icon_ListFor(int size);

/* Slot of a removed item — reused via ImageList_ReplaceIcon. */
void Icon_ReleaseSlot(int slot);

/* Full rebuild: lists are destroyed, items are reset to
   I_IMAGECALLBACK — the cascade is re-requested on the next paint.
   Triggered by system events (WM_SETTINGCHANGE with a change of
   SM_CXSMICON/SM_CYSMICON, WM_THEMECHANGED, WM_DPICHANGED) with debouncing
   from main.c. */
void Icon_RebuildAll(void);

/* Shutdown: destroy the lists. */
void Icon_Shutdown(void);
