/* PlexusX — GameDetector state machine (header only, platform independent)
 *
 * The DECISION half of game detection: given "the foreground process base
 * name just became X" and "X matched profile #i (or none)", decide what the
 * preset system must do — launch, switch, return from ALT+TAB, or restore on
 * exit.  The Win32 side (games/game_detector.c) only feeds it facts collected
 * from GetForegroundWindow / QueryFullProcessImageName and executes the
 * decision; the tests drive this machine directly (tests/test_all.c), so the
 * ALT+TAB and no-stacking guarantees are verified, not hoped for.
 *
 * Core rule: a look switch ALWAYS loads a complete absolute look snapshot
 * (global <-> game profile); looks are never stacked or delta-edited, so
 * Global → Rust → CS2 → Valorant → Global provably returns the first look.
 */
#ifndef PLEXUSX_GAME_STATE_H
#define PLEXUSX_GAME_STATE_H

#include <stddef.h>
#include <string.h>
#include <wchar.h>

#include "../color/look.h"

#define PXGAME_EXE_LEN 64

typedef enum PxGameEv {
    PXGAME_EV_NONE = 0,        /* nothing changed, act on nothing           */
    PXGAME_EV_GAME_LAUNCH,     /* non-game or desktop -> game               */
    PXGAME_EV_GAME_SWITCH,     /* game A -> game B                          */
    PXGAME_EV_GAME_RETURN,     /* ALT+TAB back into the same/known game     */
    PXGAME_EV_DESKTOP,         /* game -> desktop/other app (restore hook)  */
    PXGAME_EV_DESKTOP_IDLE     /* desktop -> other desktop app              */
} PxGameEv;

typedef struct PxGameDecision {
    int ev;                    /* PxGameEv                                    */
    int apply_idx;             /* >= 0: load profile[i].sub[active_sub] (ABSOLUTE replace) */
    int snapshot;              /* 1: save the current global look FIRST       */
    int restore;               /* 1: drop back to the saved global snapshot   */
} PxGameDecision;

typedef struct PxGameSM {
    wchar_t last_exe[PXGAME_EXE_LEN];   /* "" until the first successful read  */
    int     last_idx;                   /* profile matched for last_exe (-1)   */
    int     active_idx;                 /* profile currently loaded (-1 = global) */
    Look    snapshot;                   /* the user's global look while gaming */
    int     have_snapshot;
} PxGameSM;

static inline void PxGameSM_Init(PxGameSM *m)
{
    m->last_exe[0] = 0;
    m->last_idx = -1;
    m->active_idx = -1;
    m->have_snapshot = 0;
}

/* Lowercase ASCII copy with only the file name kept — paths never reach the matcher. */
static inline size_t px_game_base_name(const wchar_t *path, wchar_t *out, size_t cap)
{
    const wchar_t *base = path;
    size_t n = 0;
    if (!path || !path[0] || !out || !cap) { if (out && cap) out[0] = 0; return 0; }
    for (const wchar_t *p = path; *p; p++)
        if (*p == L'\\' || *p == L'/') base = p + 1;
    for (; base[n] && n + 1 < cap; n++) {
        wchar_t c = base[n];
        if (c >= L'A' && c <= L'Z') c += 32;
        out[n] = c;
    }
    out[n] = 0;
    return n;
}

/* Foreground transition.  fg_exe = normalized base name (empty on lookup
 * failure: never mutates state, so a denied OpenProcess cannot "exit" a game).
 * fg_idx = profile match, -1 none.  auto_restore = the user's restore
 * preference.  current = the live requested look (for the snapshot). */
static inline PxGameDecision PxGameSM_OnForeground(PxGameSM *m,
                                                    const wchar_t *fg_exe,
                                                    int fg_idx,
                                                    int auto_restore,
                                                    const Look *current)
{
    PxGameDecision d;
    d.ev = PXGAME_EV_NONE; d.apply_idx = -1; d.snapshot = 0; d.restore = 0;
    if (!fg_exe || !fg_exe[0]) return d;                  /* lookup failed */

    if (wcscmp(m->last_exe, fg_exe) == 0) {
        /* Same process as before: the ALT+TAB case where the focus event fired
         * while the exe never changed is already handled here (no-op keeps the
         * requested look authoritative; the engine reasserts separately). */
        return d;
    }

    int was_game = m->last_idx >= 0;

    if (fg_idx >= 0) {
        d.snapshot = (fg_idx != m->active_idx && !m->have_snapshot && current) ? 1 : 0;
        if (d.snapshot) { m->snapshot = *current; m->have_snapshot = 1; }
        if (was_game && fg_idx != m->last_idx) d.ev = PXGAME_EV_GAME_SWITCH;
        else if (m->have_snapshot && fg_idx == m->active_idx) d.ev = PXGAME_EV_GAME_RETURN;
        else d.ev = PXGAME_EV_GAME_LAUNCH;
        d.apply_idx = fg_idx;
        m->active_idx = fg_idx;
    } else {
        d.ev = was_game ? PXGAME_EV_DESKTOP : PXGAME_EV_DESKTOP_IDLE;
        if (was_game && auto_restore && m->have_snapshot) {
            d.restore = 1;
            m->have_snapshot = 0;
            m->active_idx = -1;
        } else if (was_game) {
            m->active_idx = -1;      /* game left; user kept the game look on purpose */
            m->have_snapshot = 0;
        }
    }

    {
        size_t i = 0;
        for (; fg_exe[i] && i + 1 < PXGAME_EXE_LEN; i++) m->last_exe[i] = fg_exe[i];
        m->last_exe[i] = 0;
    }
    m->last_idx = fg_idx;
    return d;
}

#endif /* PLEXUSX_GAME_STATE_H */
