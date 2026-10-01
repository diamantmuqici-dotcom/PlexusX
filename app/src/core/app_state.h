/* PlexusX — Central Application State
 *
 * Single authoritative owner for the whole application lifecycle:
 *   - Startup: first-run detection, config load, crash recovery
 *   - Engine: color pipeline, display manager, game detection
 *   - Persistence: atomic saves, dirty tracking, safe shutdown
 *   - Recovery: emergency reset, crash log, ramps.dat restore
 *   - Telemetry: none — this module explicitly asserts zero telemetry
 *
 * No other module may duplicate these responsibilities.  Every state
 * transition is logged to the diagnostics ring with a reason.
 */

#ifndef PLEXUSX_APP_STATE_H
#define PLEXUSX_APP_STATE_H

#include <stddef.h>
#include <string.h>

#include "version.h"
#include "runtime_status.h"
#include "../color/color_state.h"
#include "../display/display_state.h"
#include "../games/game_display_state.h"
#include "../diagnostics/diagnostics.h"

typedef enum PxAppPhase {
    PX_APP_BOOT = 0,         /* process start, before any IO */
    PX_APP_LOADING,          /* reading configs, detecting displays */
    PX_APP_RECOVERING,       /* crash recovery / ramps.dat restore */
    PX_APP_READY,            /* main loop running */
    PX_APP_APPLYING,         /* color apply in flight */
    PX_APP_REASSERTING,      /* ALT+TAB / display change re-assert */
    PX_APP_SAFE_MODE,        /* emergency reset active */
    PX_APP_SHUTDOWN          /* flushing + restoring */
} PxAppPhase;

static inline const char *PxAppPhase_Name(PxAppPhase p)
{
    switch (p) {
    case PX_APP_BOOT:       return "BOOT";
    case PX_APP_LOADING:    return "LOADING";
    case PX_APP_RECOVERING: return "RECOVERING";
    case PX_APP_READY:      return "READY";
    case PX_APP_APPLYING:   return "APPLYING";
    case PX_APP_REASSERTING:return "REASSERTING";
    case PX_APP_SAFE_MODE:  return "SAFE_MODE";
    case PX_APP_SHUTDOWN:   return "SHUTDOWN";
    default:                return "UNKNOWN";
    }
}

typedef enum PxAppFlags {
    PX_APP_FLAG_FIRST_RUN       = 1 << 0,
    PX_APP_FLAG_CONFIG_CORRUPT  = 1 << 1,
    PX_APP_FLAG_PROFILES_CORRUPT= 1 << 2,
    PX_APP_FLAG_PRESETS_CORRUPT = 1 << 3,
    PX_APP_FLAG_CRASH_DETECTED  = 1 << 4,
    PX_APP_FLAG_DIRTY_DISPLAY   = 1 << 5,
    PX_APP_FLAG_SAFE_RESET_ARMED= 1 << 6,
    PX_APP_FLAG_PHONE_ACTIVE    = 1 << 7,
    PX_APP_FLAG_GAME_ACTIVE     = 1 << 8,
    PX_APP_FLAG_HDR_ACTIVE      = 1 << 9
} PxAppFlags;

typedef struct PxAppState {
    PxAppPhase      phase;
    unsigned        flags;
    unsigned        start_ms;
    unsigned        ready_ms;
    unsigned        last_apply_ms;
    unsigned        last_foreground_ms;
    unsigned        reassert_count;
    unsigned        crash_count;
    int             first_run;
    int             portable_mode;
    char            version[16];
    char            build_date[32];
    wchar_t         appdata_path[260];
    wchar_t         exe_path[260];
    PxRuntimeState  runtime;
    PxLog           log;
} PxAppState;

static inline void PxAppState_Init(PxAppState *s)
{
    if (!s) return;
    memset(s, 0, sizeof *s);
    s->phase = PX_APP_BOOT;
    s->first_run = 0;
    s->portable_mode = 0;
    snprintf(s->version, sizeof s->version, "%s", PX_VERSION_STRING);
    snprintf(s->build_date, sizeof s->build_date, "%s", PX_BUILD_DATE_STR);
    PxLog_Init(&s->log);
    s->start_ms = 0;
    s->runtime.status = PX_RT_READY;
}

static inline void PxAppState_SetPhase(PxAppState *s, PxAppPhase p, unsigned now_ms)
{
    if (!s) return;
    s->phase = p;
    if (p == PX_APP_READY && s->ready_ms == 0) s->ready_ms = now_ms;
    PxLog_Fmt(&s->log, now_ms, "app", "phase -> %s", PxAppPhase_Name(p));
}

static inline void PxAppState_SetFlag(PxAppState *s, PxAppFlags f, int on, unsigned now_ms)
{
    if (!s) return;
    unsigned before = s->flags;
    if (on) s->flags |= f;
    else s->flags &= ~f;
    if (before != s->flags) {
        PxLog_Fmt(&s->log, now_ms, "app", "flags %s: 0x%08x -> 0x%08x",
                  on ? "set" : "clear", before, s->flags);
    }
}

static inline int PxAppState_HasFlag(const PxAppState *s, PxAppFlags f)
{
    return s ? (s->flags & f) != 0 : 0;
}

static inline void PxAppState_OnApply(PxAppState *s, unsigned now_ms)
{
    if (!s) return;
    s->last_apply_ms = now_ms;
    s->reassert_count++;
}

static inline void PxAppState_OnForeground(PxAppState *s, unsigned now_ms)
{
    if (!s) return;
    s->last_foreground_ms = now_ms;
}

static inline int PxAppState_IsReady(const PxAppState *s)
{
    return s && s->phase == PX_APP_READY;
}

static inline int PxAppState_IsSafeMode(const PxAppState *s)
{
    return s && (s->phase == PX_APP_SAFE_MODE || PxAppState_HasFlag(s, PX_APP_FLAG_SAFE_RESET_ARMED));
}

/* Telemetry assertion — PlexusX has ZERO telemetry by design */
static inline int PxAppState_HasTelemetry(const PxAppState *s)
{
    (void)s;
    return 0; /* never */
}

static inline const char *PxAppState_TelemetryPolicy(void)
{
    return "PlexusX has zero telemetry: no analytics, no tracking, no network calls "
           "except LAN phone control (port 8777, local network only, opt-in). "
           "All settings stored locally in %APPDATA%\\PlexusX or portable folder.";
}

#endif /* PLEXUSX_APP_STATE_H */
