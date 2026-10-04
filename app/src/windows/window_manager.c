/* PlexusX — WindowManager implementation.
 *
 * ALT+TAB and display-event handling for the color engine.  Rules enforced
 * here (contract: windows/window_state.h):
 *   1. Every focus/display/session event re-asserts the REQUESTED ColorState
 *      — never a GPU readout, so a game taking over the GPU can never rewrite
 *      the user's sliders (the old inconsistency bug).
 *   2. Events that arrive in a burst (ALT+TAB fires several) fold into ONE
 *      settle re-assert ~120ms later: DWM recomposes asynchronously after a
 *      fullscreen swap, and the follow-up catches exactly that.  It is a
 *      single one-shot coalesced pass, not a polling delay: while a pending
 *      pass is armed, further events do NOT hit the DWM again.
 *   3. The GameDetector is consulted on every foreground change; a failed
 *      OpenProcess never mutates game state (PxDetect_Scan resolved=0).
 */
#include "common.h"
#include "window_manager.h"
#include "window_state.h"
#include "../color/color_engine.h"
#include "../games/game_detector.h"
#include "../games/game_state.h"
#include "../settings/settings_store.h"
#include "../diagnostics/diagnostics.h"

static HWND             g_notify = NULL;
static HWINEVENTHOOK    g_fg_hook = NULL;
static PxWinSM          g_wsm;
static int              g_settle_timer = 0;
static unsigned long long g_last_window_key = 0;

/* Immediate + authoritative: forget cached hardware, apply the REQUESTED look. */
void Wm_ReassertNow(const wchar_t *why)
{
    Eng_Invalidate(why);
    Main_ApplyAll();
    Prof_SyncApplied(why);
    if (g_notify && Ui_IsReady()) InvalidateRect(g_notify, NULL, FALSE);
}

static void CALLBACK fg_event(HWINEVENTHOOK hook, DWORD event, HWND hwnd,
                              LONG id_obj, LONG id_child, DWORD tid, DWORD time)
{
    (void)hook; (void)event; (void)hwnd; (void)id_obj; (void)id_child; (void)tid; (void)time;
    if (g_notify) PostMessageW(g_notify, WM_APP_FOREGROUND, 0, 0);
}

void Wm_Init(HWND notify_hwnd)
{
    g_notify = notify_hwnd;
    PxWinSM_Init(&g_wsm);
    g_settle_timer = 0;
    g_last_window_key = 0;
    g_fg_hook = SetWinEventHook(EVENT_SYSTEM_FOREGROUND, EVENT_SYSTEM_FOREGROUND,
                                NULL, fg_event, 0, 0, WINEVENT_OUTOFCONTEXT);
    if (!g_fg_hook)
        Eng_Log("ui", "foreground WinEvent hook unavailable - slow fallback active");
}

void Wm_Shutdown(void)
{
    if (g_fg_hook) { UnhookWinEvent(g_fg_hook); g_fg_hook = NULL; }
    if (g_settle_timer && g_notify) { KillTimer(g_notify, PX_SETTLE_MS); g_settle_timer = 0; }
}

int Wm_PendingSettle(void) { return g_settle_timer ? 1 : 0; }
unsigned Wm_CoalescedEvents(void) { return g_wsm.events_coalesced; }

static void arm_settle(void)
{
    if (g_settle_timer || !g_notify) return;
    g_settle_timer = 1;
    SetTimer(g_notify, PX_SETTLE_MS, PX_SETTLE_MS, NULL);   /* one-shot: fired once, then cleared */
}

void Wm_OnForegroundEvent(void)
{
    PxDetectedForeground f;
    PxDetect_Scan(&f);
    g_last_window_key = f.window_key;

    PxWinAction a = PxWinSM_OnEvent(&g_wsm, PXWIN_EV_FOREGROUND, !f.our_window);
    if (a.refresh_displays) Modes_Refresh();
    /* Always forwarded: Prof_NotifyForeground refreshes active/presentation
     * from the facts and only feeds the machine when the exe truly resolved,
     * so a denied OpenProcess can never end a game session (no flicker). */
    if (a.game_reattach) Prof_NotifyForeground(&f);

    if (a.reassert) {
        Wm_ReassertNow(L"foreground");
        arm_settle();
    } else if (g_wsm.reassert_pending) {
        arm_settle();      /* folded event: make sure the pending pass still runs */
    }
}

void Wm_OnActivate(int app_active)
{
    g_wsm.app_foreground = app_active ? 1 : 0;
    PxWinAction a = PxWinSM_OnEvent(&g_wsm, PXWIN_EV_ACTIVATE, 0);
    if (a.reassert) {
        Wm_ReassertNow(L"activate-app");
        arm_settle();
    }
}

void Wm_OnSystemEvent(int pxwin_evt)
{
    PxWinAction a = PxWinSM_OnEvent(&g_wsm, pxwin_evt, 0);
    if (a.refresh_displays) Modes_Refresh();
    if (a.reassert) {
        const wchar_t *why = L"pipeline";
        if (pxwin_evt == PXWIN_EV_DISPLAYCHANGE) why = L"display-change";
        else if (pxwin_evt == PXWIN_EV_DEVICECHANGE) why = L"device-change";
        else if (pxwin_evt == PXWIN_EV_SESSION) why = L"session";
        else if (pxwin_evt == PXWIN_EV_RESUME) why = L"resume";
        Wm_ReassertNow(why);
        arm_settle();
    } else if (g_wsm.reassert_pending) {
        arm_settle();
    }
}

/* WM_TIMER(wp == PX_SETTLE_MS): the single coalesced follow-up re-assert. */
void Wm_OnSettleTimer(void)
{
    if (!g_settle_timer) return;            /* spurious / already consumed */
    g_settle_timer = 0;
    if (g_notify) KillTimer(g_notify, PX_SETTLE_MS);
    if (PxWinSM_FirePending(&g_wsm))
        Wm_ReassertNow(L"settle");
}

/* WM_TIMER(wp == TIMER_POLL): slow safety net + delayed game applies. */
void Wm_Tick(void)
{
    Prof_TickPending();

    /* Hardware state can disappear without a matching Win32 event.  Let the
     * pipeline verify its own live state and only reapply when something was
     * actually lost; this is not a periodic colour rewrite loop. */
    if (PxPipe_HealthCheck())
        Wm_ReassertNow(L"hardware-health");
}
