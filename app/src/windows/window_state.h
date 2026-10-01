/* PlexusX — WindowManager focus state (header only, platform independent)
 *
 * The decision core of windows/window_manager.c: which OS events force a
 * re-assert of the REQUESTED color state and how bursts are coalesced.
 * There is no timer-based "wait for the game to settle" delay anywhere: the
 * re-assert is immediate on the event, plus one coalesced follow-up once the
 * message burst of a focus switch has drained (that is what a pending flag
 * does — it is deduplication, not an arbitrary sleep).
 *
 *   GAME ACTIVE → ALT+TAB → DESKTOP      : desktop must be restored  → REASSERT
 *   DESKTOP     → ALT+TAB → GAME ACTIVE  : game must get the state   → REASSERT + GAME_REATTACH
 *   WM_DISPLAYCHANGE / lock / resume      : pipeline was recreated    → REFRESH + REASSERT
 */
#ifndef PLEXUSX_WINDOW_STATE_H
#define PLEXUSX_WINDOW_STATE_H

#include <stddef.h>

typedef enum PxWinEvt {
    PXWIN_EV_FOREGROUND = 1,   /* EVENT_SYSTEM_FOREGROUND / WM_APP_FOREGROUND */
    PXWIN_EV_ACTIVATE,         /* WM_ACTIVATEAPP (in or out)                   */
    PXWIN_EV_MINIMIZE,         /* WM_SYSCOMMAND SC_MINIMIZE / show-state       */
    PXWIN_EV_RESTORE,          /* WM_SYSCOMMAND SC_RESTORE                     */
    PXWIN_EV_DISPLAYCHANGE,    /* WM_DISPLAYCHANGE                             */
    PXWIN_EV_DEVICECHANGE,     /* WM_DEVICECHANGE (GPU / monitor)              */
    PXWIN_EV_SESSION,          /* session unlock / logon / console connect     */
    PXWIN_EV_RESUME            /* power resume                                 */
} PxWinEvt;

typedef struct PxWinAction {
    int reassert;              /* Eng_Invalidate + apply the REQUESTED look    */
    int refresh_displays;      /* Modes/DisplayManager re-enumeration needed   */
    int game_reattach;         /* tell the GameDetector the foreground moved   */
    int repaint;               /* status chips / previews must be redrawn      */
} PxWinAction;

typedef struct PxWinSM {
    int      app_foreground;   /* 1 while our own window has focus            */
    int      reassert_pending; /* coalesced follow-up armed, not yet fired    */
    unsigned events_coalesced; /* diagnostics: how many events the flag ate   */
} PxWinSM;

static inline void PxWinSM_Init(PxWinSM *s) { s->app_foreground = 1; s->reassert_pending = 0; s->events_coalesced = 0; }

static inline PxWinAction PxWinSM_OnEvent(PxWinSM *s, int evt, int game_foreground)
{
    PxWinAction a;
    a.reassert = 1; a.refresh_displays = 0; a.game_reattach = 0; a.repaint = 1;

    switch (evt) {
    case PXWIN_EV_ACTIVATE:
    case PXWIN_EV_FOREGROUND:
        a.game_reattach = 1;              /* detector must re-evaluate the fg window */
        break;
    case PXWIN_EV_DISPLAYCHANGE:
    case PXWIN_EV_DEVICECHANGE:
        a.refresh_displays = 1;           /* monitor list / modes / color state all moved */
        break;
    case PXWIN_EV_MINIMIZE:
        a.repaint = 0;                    /* nothing on screen changed; the look is untouched */
        break;
    default:
        break;                            /* RESTORE / SESSION / RESUME: plain re-assert */
    }
    (void)game_foreground;                /* the caller uses it for its own bookkeeping */

    /* One-shot pending flag: while a follow-up re-assert is already armed,
     * further events fold into it instead of hammering the DWM. */
    if (a.reassert) {
        if (s->reassert_pending) { s->events_coalesced++; a.reassert = 0; }
        else s->reassert_pending = 1;
    }
    return a;
}

/* Fire the pending follow-up (message loop drained). Returns 1 when the caller
 * should run one final re-assert; 0 when nothing is outstanding. */
static inline int PxWinSM_FirePending(PxWinSM *s)
{
    if (!s->reassert_pending) return 0;
    s->reassert_pending = 0;
    return 1;
}

#endif /* PLEXUSX_WINDOW_STATE_H */
