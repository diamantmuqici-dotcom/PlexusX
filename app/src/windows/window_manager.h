/* PlexusX — WindowManager interface.
 *
 * Owns every OS-facing focus concern the color pipeline must react to:
 *   - EVENT_SYSTEM_FOREGROUND WinEvent hook (posted to the UI thread)
 *   - WM_ACTIVATEAPP / WM_DISPLAYCHANGE / WM_DEVICECHANGE / session unlock /
 *     power resume forwarding
 *   - ALT+TAB correctness: immediate re-assert of the REQUESTED look plus ONE
 *     coalesced follow-up after the event burst settles (deduplication, not a
 *     sleep-based "wait for the game" hack — see windows/window_state.h)
 *   - driving the GameDetector + preset machine on every focus change
 *
 * The decision logic is the pure PxWinSM in window_state.h (host-tested);
 * this module only wires events, timers and the UI thread together.
 */
#ifndef PLEXUSX_WINDOW_MANAGER_H
#define PLEXUSX_WINDOW_MANAGER_H

#include <windows.h>

enum {
    PX_SETTLE_MS = 120                    /* one coalesced follow-up re-assert */
};

void Wm_Init(HWND notify_hwnd);           /* installs the WinEvent hook          */
void Wm_Shutdown(void);

/* main.c message handlers forward into these: */
void Wm_OnForegroundEvent(void);          /* WM_APP_FOREGROUND                   */
void Wm_OnActivate(int app_active);       /* WM_ACTIVATEAPP                      */
void Wm_OnSystemEvent(int pxwin_evt);     /* DISPLAY/DEVICE/SESSION/RESUME       */
void Wm_Tick(void);                       /* delayed profile applies             */
void Wm_OnSettleTimer(void);              /* WM_TIMER(id == PX_SETTLE_MS)        */
int  Wm_PendingSettle(void);
unsigned Wm_CoalescedEvents(void);              /* diagnostics: folded events        */

/* run the immediate re-assert pipeline: Eng_Invalidate + apply + diagnostics */
void Wm_ReassertNow(const wchar_t *why);

#endif /* PLEXUSX_WINDOW_MANAGER_H */
