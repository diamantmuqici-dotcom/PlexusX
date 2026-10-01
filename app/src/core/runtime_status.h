/* PlexusX — Enhanced Runtime Status System
 *
 * Requirement: honest status with 10 explicit states
 *   READY, APPLYING, APPLIED, LIMITED, HDR, EXCLUSIVE_FULLSCREEN,
 *   UNSUPPORTED, ERROR, RESTORING, SAFE_MODE
 *
 * This header provides the PRODUCT-LEVEL status model that the UI, diagnostics,
 * tray tooltip and logging all read from ONE place.  It maps from the low-level
 * pipeline facts (PxColorStatus, PxHdrState, PxPresMode, PxApplyOutcome) to a
 * single user-facing status with a reason code and a severity.
 *
 * Design:
 *   - Pure C, no windows.h — testable on host
 *   - No guessing: every branch is a function of OS-reported facts
 *   - Every status has: id, name, short label, explanation, severity, tone,
 *     and actionable hint
 *   - Mapping function PxRuntime_Derive() is total and deterministic
 *
 * Relationship to existing PxColorStatus (color_runtime_state.h):
 *   That file is the ENGINE-LEVEL status (6 states).  This file is the
 *   APP-LEVEL status (10 states) that the user sees.  The engine status
 *   feeds into this one, plus display, game and recovery facts.
 */

#ifndef PLEXUSX_RUNTIME_STATUS_H
#define PLEXUSX_RUNTIME_STATUS_H

#include <stddef.h>
#include <string.h>

#include "../color/color_runtime_state.h"
#include "../display/display_capabilities.h"
#include "../display/display_state.h"
#include "../color/applied_color_state.h"

/* ---- 10-state product model (requirement) ---- */
typedef enum PxRuntimeStatus {
    PX_RT_READY = 0,              /* engine idle, no active look or neutral */
    PX_RT_APPLYING,               /* in-flight apply, spinner */
    PX_RT_APPLIED,                /* full effect confirmed on desktop/composited game */
    PX_RT_LIMITED,                /* curves-only: fullscreen surface bypasses DWM matrix */
    PX_RT_HDR,                    /* HDR output active — engine PASSTHROUGH by design */
    PX_RT_EXCLUSIVE_FULLSCREEN,   /* game in exclusive fullscreen, matrix unreachable */
    PX_RT_UNSUPPORTED,            /* no usable path (no mag, no gamma) */
    PX_RT_ERROR,                  /* last apply failed */
    PX_RT_RESTORING,              /* crash/ALT+TAB/display-change re-assert in progress */
    PX_RT_SAFE_MODE               /* emergency reset armed or active */
} PxRuntimeStatus;

#define PX_RT_COUNT 10

/* Severity for log filtering and UI tone */
typedef enum PxRtSeverity {
    PX_RT_SEV_INFO = 0,
    PX_RT_SEV_OK,
    PX_RT_SEV_WARN,
    PX_RT_SEV_ERROR
} PxRtSeverity;

/* Structured reason codes — machine-readable for diagnostics JSON */
typedef enum PxRtReason {
    PX_RT_REASON_NONE = 0,
    PX_RT_REASON_ENGINE_DISABLED,
    PX_RT_REASON_NEUTRAL,
    PX_RT_REASON_HDR_ACTIVE,
    PX_RT_REASON_EXCLUSIVE_SURFACE,
    PX_RT_REASON_NO_MAG,
    PX_RT_REASON_NO_GAMMA,
    PX_RT_REASON_APPLY_FAILED,
    PX_RT_REASON_VERIFY_FAILED,
    PX_RT_REASON_REASSERT,
    PX_RT_REASON_CRASH_RECOVERY,
    PX_RT_REASON_SAFE_RESET,
    PX_RT_REASON_DISPLAY_CHANGE,
    PX_RT_REASON_SESSION_LOCK,
    PX_RT_REASON_GAME_ACTIVE,
    PX_RT_REASON_GAME_LIMITED
} PxRtReason;

/* Full runtime snapshot — what the UI status bar + diagnostics read */
typedef struct PxRuntimeState {
    PxRuntimeStatus status;
    PxRtSeverity    severity;
    PxRtReason      reason_code;
    const char     *reason;            /* human, always actionable */
    const char     *hint;              /* what user can do, if anything */
    int             tone;              /* 0 neutral, 1 ok, 2 warn, 3 bad, 4 info */

    /* Contributing facts (copied, not pointers) */
    int             engine_enabled;
    int             hdr_active;
    int             exclusive_active;
    int             linear_ok;
    int             curves_ok;
    int             matrix_verified;
    int             ramps_verified;
    int             game_active;
    int             restoring;
    int             safe_mode;

    unsigned        revision;
    unsigned        applied_ms;
} PxRuntimeState;

/* ---- Human names ---- */
static inline const char *PxRt_Name(PxRuntimeStatus s)
{
    switch (s) {
    case PX_RT_READY:                return "READY";
    case PX_RT_APPLYING:             return "APPLYING";
    case PX_RT_APPLIED:              return "APPLIED";
    case PX_RT_LIMITED:              return "LIMITED";
    case PX_RT_HDR:                  return "HDR";
    case PX_RT_EXCLUSIVE_FULLSCREEN: return "EXCLUSIVE_FULLSCREEN";
    case PX_RT_UNSUPPORTED:          return "UNSUPPORTED";
    case PX_RT_ERROR:                return "ERROR";
    case PX_RT_RESTORING:            return "RESTORING";
    case PX_RT_SAFE_MODE:            return "SAFE_MODE";
    default:                         return "UNKNOWN";
    }
}

static inline const char *PxRt_ShortLabel(PxRuntimeStatus s)
{
    switch (s) {
    case PX_RT_READY:                return "Ready";
    case PX_RT_APPLYING:             return "Applying…";
    case PX_RT_APPLIED:              return "Active";
    case PX_RT_LIMITED:              return "Limited — Curves Only";
    case PX_RT_HDR:                  return "HDR Passthrough";
    case PX_RT_EXCLUSIVE_FULLSCREEN: return "Fullscreen — Limited";
    case PX_RT_UNSUPPORTED:          return "Unsupported";
    case PX_RT_ERROR:                return "Error";
    case PX_RT_RESTORING:            return "Restoring…";
    case PX_RT_SAFE_MODE:            return "Safe Mode";
    default:                         return "—";
    }
}

static inline const char *PxRt_Explain(PxRuntimeStatus s)
{
    switch (s) {
    case PX_RT_READY:
        return "Engine idle. No active color look or neutral output.";
    case PX_RT_APPLYING:
        return "Applying requested color transform to display pipeline.";
    case PX_RT_APPLIED:
        return "Full effect active: DWM matrix + GPU gamma ramps confirmed.";
    case PX_RT_LIMITED:
        return "Partial effect: fullscreen surface bypasses DWM matrix; GPU curves still apply.";
    case PX_RT_HDR:
        return "HDR output active — OS tone-mapper owns LUT; PlexusX bypassed by design (PASSTHROUGH).";
    case PX_RT_EXCLUSIVE_FULLSCREEN:
        return "Game in exclusive fullscreen: compositor bypassed, only tone curves reach scanout.";
    case PX_RT_UNSUPPORTED:
        return "No usable color path on this system (Magnification + gamma ramp unavailable).";
    case PX_RT_ERROR:
        return "Last color apply failed — hardware rejected pipeline or verification mismatch.";
    case PX_RT_RESTORING:
        return "Restoring requested look after display change, ALT+TAB, or crash recovery.";
    case PX_RT_SAFE_MODE:
        return "Safe mode: all effects bypassed, original ramps restored. Press Ctrl+Alt+Shift+R to exit.";
    default:
        return "Unknown state.";
    }
}

static inline const char *PxRt_Hint(PxRuntimeStatus s)
{
    switch (s) {
    case PX_RT_READY:
        return "Adjust sliders or load a preset to activate.";
    case PX_RT_APPLYING:
        return "Wait for confirmation — should complete within 100ms.";
    case PX_RT_APPLIED:
        return "All controls live. Changes apply immediately.";
    case PX_RT_LIMITED:
        return "Switch game to borderless windowed for full effect, or use tone controls.";
    case PX_RT_HDR:
        return "Disable HDR in Windows Display Settings to use full pipeline, or use HDR calibration.";
    case PX_RT_EXCLUSIVE_FULLSCREEN:
        return "Use borderless windowed mode in game settings for full color effect.";
    case PX_RT_UNSUPPORTED:
        return "Update GPU driver and ensure Magnification API is available.";
    case PX_RT_ERROR:
        return "Try Ctrl+Alt+0 reset, then re-apply. Check diagnostics for driver rejection.";
    case PX_RT_RESTORING:
        return "Automatic — will complete after display settles.";
    case PX_RT_SAFE_MODE:
        return "Press Ctrl+Alt+Shift+R emergency reset or restart app to exit safe mode.";
    default:
        return "";
    }
}

static inline int PxRt_Tone(PxRuntimeStatus s)
{
    switch (s) {
    case PX_RT_APPLIED:              return 1; /* ok / success */
    case PX_RT_READY:                return 0; /* neutral */
    case PX_RT_APPLYING:             return 4; /* info / spinner */
    case PX_RT_RESTORING:            return 4;
    case PX_RT_LIMITED:              return 2; /* warn */
    case PX_RT_HDR:                  return 2;
    case PX_RT_EXCLUSIVE_FULLSCREEN: return 2;
    case PX_RT_UNSUPPORTED:          return 3; /* bad */
    case PX_RT_ERROR:                return 3;
    case PX_RT_SAFE_MODE:            return 3;
    default:                         return 0;
    }
}

static inline PxRtSeverity PxRt_Severity(PxRuntimeStatus s)
{
    switch (s) {
    case PX_RT_APPLIED:              return PX_RT_SEV_OK;
    case PX_RT_READY:                return PX_RT_SEV_INFO;
    case PX_RT_APPLYING:             return PX_RT_SEV_INFO;
    case PX_RT_RESTORING:            return PX_RT_SEV_INFO;
    case PX_RT_LIMITED:              return PX_RT_SEV_WARN;
    case PX_RT_HDR:                  return PX_RT_SEV_WARN;
    case PX_RT_EXCLUSIVE_FULLSCREEN: return PX_RT_SEV_WARN;
    case PX_RT_UNSUPPORTED:          return PX_RT_SEV_ERROR;
    case PX_RT_ERROR:                return PX_RT_SEV_ERROR;
    case PX_RT_SAFE_MODE:            return PX_RT_SEV_ERROR;
    default:                         return PX_RT_SEV_INFO;
    }
}

/* ---- Derivation: pure function from low-level facts ---- */
typedef struct PxRuntimeInput {
    const PxEffectiveState   *eff;          /* from Eng_Effective() — may be NULL */
    const AppliedColorState  *applied;      /* from Eng_Applied() — may be NULL */
    const MonitorInfo        *monitor;      /* selected monitor — may be NULL */
    int                       hdr_state;    /* PxHdrState */
    int                       pres_mode;    /* PxPresMode */
    int                       game_active;  /* foreground game detected */
    int                       restoring;    /* reassert pending */
    int                       safe_mode;    /* emergency reset active */
    int                       mag_available;
    int                       gamma_available;
} PxRuntimeInput;

static inline void PxRuntime_Derive(const PxRuntimeInput *in, PxRuntimeState *out)
{
    if (!out) return;
    memset(out, 0, sizeof *out);
    out->status = PX_RT_READY;
    out->reason = PxRt_Explain(PX_RT_READY);
    out->hint = PxRt_Hint(PX_RT_READY);
    out->tone = PxRt_Tone(PX_RT_READY);
    out->severity = PxRt_Severity(PX_RT_READY);
    out->reason_code = PX_RT_REASON_NONE;

    if (!in) return;

    /* Snapshot contributing facts */
    if (in->eff) {
        out->engine_enabled = in->eff->engine_enabled;
        out->linear_ok = in->eff->linear_deliverable;
        out->curves_ok = in->eff->curves_deliverable;
        out->matrix_verified = in->eff->linear_live;
        out->ramps_verified = in->eff->curves_live;
        out->revision = in->eff->revision;
        out->applied_ms = in->eff->applied_ms;
        out->hdr_active = (in->eff->status == PX_STATUS_PASSTHROUGH);
    }
    if (in->monitor) {
        out->hdr_active = in->monitor->hdr_enabled ? 1 : out->hdr_active;
    }
    out->exclusive_active = (in->pres_mode == PX_PRES_FULLSCREEN_SURFACE);
    out->game_active = in->game_active ? 1 : 0;
    out->restoring = in->restoring ? 1 : 0;
    out->safe_mode = in->safe_mode ? 1 : 0;

    /* Priority order: safe_mode > restoring > error > hdr > exclusive > limited > unsupported > applying > applied > ready */
    if (in->safe_mode) {
        out->status = PX_RT_SAFE_MODE;
        out->reason_code = PX_RT_REASON_SAFE_RESET;
    } else if (in->restoring) {
        out->status = PX_RT_RESTORING;
        out->reason_code = PX_RT_REASON_REASSERT;
    } else if (in->eff && in->eff->status == PX_STATUS_FAILED) {
        out->status = PX_RT_ERROR;
        out->reason_code = PX_RT_REASON_APPLY_FAILED;
    } else if (in->hdr_state == PX_HDR_ENABLED || (in->eff && in->eff->status == PX_STATUS_PASSTHROUGH)) {
        out->status = PX_RT_HDR;
        out->reason_code = PX_RT_REASON_HDR_ACTIVE;
    } else if (in->pres_mode == PX_PRES_FULLSCREEN_SURFACE && in->game_active) {
        out->status = PX_RT_EXCLUSIVE_FULLSCREEN;
        out->reason_code = PX_RT_REASON_EXCLUSIVE_SURFACE;
    } else if (in->eff && in->eff->status == PX_STATUS_LIMITED) {
        out->status = PX_RT_LIMITED;
        out->reason_code = in->game_active ? PX_RT_REASON_GAME_LIMITED : PX_RT_REASON_EXCLUSIVE_SURFACE;
    } else if (!in->mag_available && !in->gamma_available) {
        out->status = PX_RT_UNSUPPORTED;
        out->reason_code = PX_RT_REASON_NO_MAG;
    } else if (in->eff && in->eff->status == PX_STATUS_APPLYING) {
        out->status = PX_RT_APPLYING;
        out->reason_code = PX_RT_REASON_NONE;
    } else if (in->eff && in->eff->status == PX_STATUS_ACTIVE) {
        out->status = PX_RT_APPLIED;
        out->reason_code = in->game_active ? PX_RT_REASON_GAME_ACTIVE : PX_RT_REASON_NONE;
    } else if (in->eff && !in->eff->engine_enabled) {
        out->status = PX_RT_READY;
        out->reason_code = PX_RT_REASON_ENGINE_DISABLED;
    } else {
        out->status = PX_RT_READY;
        out->reason_code = PX_RT_REASON_NONE;
    }

    out->reason = PxRt_Explain(out->status);
    out->hint = PxRt_Hint(out->status);
    out->tone = PxRt_Tone(out->status);
    out->severity = PxRt_Severity(out->status);
}

/* ---- JSON serialization for diagnostics ---- */
static inline int PxRt_ToJson(const PxRuntimeState *rt, char *buf, size_t cap)
{
    if (!rt || !buf || !cap) return -1;
    int n = snprintf(buf, cap,
        "{\"status\":\"%s\",\"label\":\"%s\",\"severity\":%d,\"reason_code\":%d,"
        "\"reason\":\"%s\",\"hint\":\"%s\",\"tone\":%d,"
        "\"engine_enabled\":%d,\"hdr_active\":%d,\"exclusive\":%d,"
        "\"linear_ok\":%d,\"curves_ok\":%d,\"matrix_verified\":%d,\"ramps_verified\":%d,"
        "\"game_active\":%d,\"restoring\":%d,\"safe_mode\":%d,\"revision\":%u}",
        PxRt_Name(rt->status), PxRt_ShortLabel(rt->status),
        (int)rt->severity, (int)rt->reason_code,
        rt->reason ? rt->reason : "",
        rt->hint ? rt->hint : "",
        rt->tone,
        rt->engine_enabled, rt->hdr_active, rt->exclusive_active,
        rt->linear_ok, rt->curves_ok, rt->matrix_verified, rt->ramps_verified,
        rt->game_active, rt->restoring, rt->safe_mode, rt->revision);
    return n;
}

#endif /* PLEXUSX_RUNTIME_STATUS_H */
