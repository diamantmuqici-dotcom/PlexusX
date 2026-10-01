/* PlexusX — ColorState (header only, platform independent)
 *
 * THE authoritative global color configuration: what the user asked for.
 * Deliberately free of any GPU / runtime state — the hardware result of an
 * apply lives in AppliedColorState (applied_color_state.h), never here.  The
 * engine re-applies this block whenever the display path is disturbed
 * (ALT+TAB, display mode change, device reset, session unlock); the requested
 * values themselves are never overwritten from a GPU readout or by a failed
 * apply.
 *
 * The Look parameter block (look.h) holds the actual adjustment values:
 *   requestedVibrance = requested.vibrance, requestedSaturation = requested.sat,
 *   requestedBrightness = requested.bri, requestedContrast = requested.con,
 *   requestedGamma = requested.gamma, requestedHue = requested.hue,
 *   enabled = requested.enabled, mode = mode.
 *
 * Written by the UI (sliders), phone remote and the game preset manager; read
 * by the color engine and every status/diagnostics surface.  A single process-
 * wide instance lives inside the color engine (color_engine.c) — nothing else
 * may own requested color state.
 */
#ifndef PLEXUSX_COLOR_STATE_H
#define PLEXUSX_COLOR_STATE_H

#include <string.h>

#include "look.h"
#include "color_math.h"

typedef enum PxColorMode {
    PX_CSMODE_GLOBAL = 0,   /* the user's own global look is in control   */
    PX_CSMODE_GAME   = 1    /* a game profile look is loaded as requested  */
} PxColorMode;

typedef struct ColorState {
    Look        requested;    /* always sanitised (no NaN/Inf, ranges enforced) */
    PxColorMode mode;
    int         game_index;   /* profile slot when GAME mode, -1 for GLOBAL */
    int         game_sub;     /* active sub-mode index when GAME mode */
    unsigned    revision;     /* bumped on every user-visible change */
} ColorState;

static inline void PxCS_Init(ColorState *cs)
{
    cs->requested = (Look)LOOK_NEUTRAL_INIT;
    cs->mode = PX_CSMODE_GLOBAL;
    cs->game_index = -1;
    cs->game_sub = 0;
    cs->revision = 1;
}

/* Replace the requested look.  Sanitised on entry (corrupt/NaN input can never
 * reach the engine); the revision only moves when something visible changed. */
static inline void PxCS_SetLook(ColorState *cs, const Look *lk)
{
    if (!lk) return;
    Look next = *lk;
    cm_sanitize_look(&next);
    if (cm_looks_equal(&cs->requested, &next)) return;
    cs->requested = next;
    cs->revision++;
}

/* Same look, but also claims a mode/game attribution atomically (used by the
 * preset manager when it loads a game profile and when it restores global). */
static inline void PxCS_SetLookEx(ColorState *cs, const Look *lk,
                                  PxColorMode mode, int game_idx, int sub_idx)
{
    PxCS_SetLook(cs, lk);
    if (cs->mode != mode || cs->game_index != game_idx || cs->game_sub != sub_idx) {
        cs->mode = mode;
        cs->game_index = game_idx;
        cs->game_sub = sub_idx;
        cs->revision++;
    }
}

static inline void PxCS_ResetNeutral(ColorState *cs)
{
    Look n = (Look)LOOK_NEUTRAL_INIT;
    PxCS_SetLook(cs, &n);
}

static inline void PxCS_SetEnabled(ColorState *cs, int on)
{
    if (cs->requested.enabled == (on ? 1 : 0)) return;
    cs->requested.enabled = on ? 1 : 0;
    cs->revision++;
}

/* Pattern for UI/phone/hotkey edits: copy, tweak, commit — the commit runs
 * cm_sanitize_look(), so no caller can ever push an out-of-range or non-finite
 * value into the engine.
 *
 *   Look l = *PxCS_Look(cs); l.sat += 10; PxCS_SetLook(cs, &l);
 */
static inline const Look *PxCS_Look(const ColorState *cs) { return &cs->requested; }

#endif /* PLEXUSX_COLOR_STATE_H */
