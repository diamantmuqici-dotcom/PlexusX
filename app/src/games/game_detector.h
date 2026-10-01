/* PlexusX — GameDetector interface + foreground facts (Win32 side).
 *
 * Collects the raw facts about whatever window owns the screen RIGHT NOW:
 * executable base name, pid, window key and the presentation mode computed
 * from real geometry.  It never decides anything itself — decisions are made
 * by the pure state machine in game_state.h (launch / exit / ALT+TAB), so the
 * semantics are host-testable and this module stays a thin, honest sensor.
 *
 * Detection is EVENT-DRIVEN (EVENT_SYSTEM_FOREGROUND WinEvent hook, posted to
 * the UI thread).  The slow timer in main.c is only a safety net for missed
 * hooks; it never loops, never Sleeps, and never re-applies color on its own.
 * Anti-cheat safe: QueryFullProcessImageNameW on a
 * PROCESS_QUERY_LIMITED_INFORMATION handle — read-only OS metadata, no game
 * memory, no injection, no hooks into game processes.
 */
#ifndef PLEXUSX_GAME_DETECTOR_H
#define PLEXUSX_GAME_DETECTOR_H

#include <windows.h>

#include "../games/game_state.h"
#include "../games/game_display_state.h"

typedef struct PxDetectedForeground {
    wchar_t            exe[PXGAME_EXE_LEN];   /* normalized lowercase base name */
    unsigned long      pid;
    unsigned long long window_key;            /* foreground HWND value            */
    int                presentation;          /* PxPresMode from real geometry    */
    int                our_window;            /* foreground window is PlexusX     */
    int                resolved;              /* 0 = transient OpenProcess failure */
} PxDetectedForeground;

void PxDetect_Scan(PxDetectedForeground *out);   /* always safe to call; sets resolved=0 on failure */

/* presentation classification for an explicit window (winman + tests of the
 * pure predicate in display_state.h both use this) */
int  PxDetect_PresentationFor(HWND wnd);

#endif /* PLEXUSX_GAME_DETECTOR_H */
