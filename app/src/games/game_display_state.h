/* PlexusX — GameDisplayState (header only, platform independent)
 *
 * The actual runtime output state of the foreground game, separate from the
 * user's global ColorState: which game, which window, which profile, what
 * presentation mode, what the display under it reports (HDR / color space),
 * and — derived, never asserted — whether the color engine's output path
 * actually reaches that game.
 *
 * game_output is computed by px_gameout_compute() from real inputs only:
 *   DETECTED       a registered game executable owns the foreground window
 *   ACTIVE         its window is currently the foreground
 *   FULL EFFECT    DWM composition applies the matrix (windowed / borderless)
 *   LIMITED        fullscreen surface: DWM may be bypassed, tone curves still
 *                  reach the scanout via the GPU LUT
 *   UNAVAILABLE    no magnification path on this machine
 *   ENGINE OFF     the user bypassed the engine
 * The UIs render this struct verbatim.  "The game is affected" is only ever
 * claimed when the engine + presentation state support it.
 */
#ifndef PLEXUSX_GAME_DISPLAY_STATE_H
#define PLEXUSX_GAME_DISPLAY_STATE_H

#include <stdio.h>
#include <string.h>
#include <wchar.h>

#include "../display/display_state.h"

#define PX_GAME_EXE_LEN 64

typedef struct PxGameDisplayState {
    wchar_t            exe[PX_GAME_EXE_LEN];  /* base name, e.g. RustClient.exe */
    unsigned long long window_key;            /* foreground HWND value (identity only) */
    unsigned long      pid;                   /* owning process id              */
    int                profile_idx;           /* matched game profile, -1 = none */
    int                sub_idx;               /* active sub-mode of that profile  */
    int                detected;              /* 1 = known game foreground       */
    int                active;                /* 1 = window still foreground     */
    int                presentation;          /* PxPresMode                      */
    int                hdr;                   /* display under the game          */
    int                bpc;
    int                color_space_raw;
    int                game_output;           /* PxGameOut — derived             */
    int                applied;               /* engine confirmed the active look*/
    unsigned           last_change_ms;        /* GetTickCount() of last change   */
} PxGameDisplayState;

static inline void PxGDS_Init(PxGameDisplayState *g)
{
    memset(g, 0, sizeof *g);
    g->profile_idx = -1;
    g->sub_idx = -1;
    g->presentation = PX_PRES_NONE;
    g->color_space_raw = PX_CS_UNKNOWN;
    g->game_output = PX_GAMEOUT_NONE;
}

static inline void PxGDS_ClearForeground(PxGameDisplayState *g)
{
    g->exe[0] = 0;
    g->window_key = 0;
    g->pid = 0;
    g->detected = 0;
    g->active = 0;
    g->presentation = PX_PRES_NONE;
    g->game_output = PX_GAMEOUT_NONE;
    g->applied = 0;
}

/* Recompute the honest game-output verdict from its real inputs. */
static inline void PxGDS_UpdateOutput(PxGameDisplayState *g, int engine_enabled, int mag_ok)
{
    g->game_output = px_gameout_compute(g->detected && g->active, g->presentation,
                                        engine_enabled, mag_ok);
}

/* One-line status for chips / JSON.  ASCII-safe: the wide exe base name is
 * copied byte-wise (base names are ASCII in practice) instead of %ls. */
static inline void PxGDS_StatusLine(const PxGameDisplayState *g, char *buf, size_t cap)
{
    if (!buf || !cap) return;
    if (!g->detected) {
        snprintf(buf, cap, "NO GAME IN FOREGROUND");
        return;
    }
    {
        char exe[PX_GAME_EXE_LEN + 1];
        int n = 0;
        for (; g->exe[n] && n < PX_GAME_EXE_LEN; n++) exe[n] = (char)(g->exe[n] & 0xFF);
        exe[n] = 0;
        snprintf(buf, cap, "GAME: %s  ·  %s", exe, px_gameout_name(g->game_output));
    }
}

#endif /* PLEXUSX_GAME_DISPLAY_STATE_H */
