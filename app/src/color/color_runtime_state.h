/* PlexusX — ColorRuntimeState: REQUESTED → EFFECTIVE → APPLIED.
 *
 * THE problem this header exists for: "the sliders say 200% saturation, the app
 * says ACTIVE, but inside the game nothing changed."  Three different truths
 * have to be kept apart, and the UI must render all three:
 *
 *   REQUESTED   the user's look                      (ColorState.requested)
 *   EFFECTIVE   what the Windows output paths can    (this header, computed
 *               actually deliver to the content       from the facts below)
 *   APPLIED     what the hardware confirmed          (AppliedColorState)
 *
 * The EFFECTIVE half is a pure function of facts the sensors collected — never
 * a guess:
 *
 *   - a true exclusive-fullscreen / flip-presentation surface bypasses DWM
 *     composition, so the magnification color matrix cannot reach it.  The
 *     GPU gamma-ramp LUT is scanout-side and still applies, so the effective
 *     state keeps the tone curves and reports the linear half as undeliverable
 *     (status LIMITED) instead of pretending everything is live;
 *   - an output running an HDR transfer function owns the CRTC LUT: PlexusX
 *     must not fight the OS tone-mapper, so the effective state is pass-through
 *     (status PASSTHROUGH) and nothing is written;
 *   - a machine without the magnification path can still run curve-only;
 *   - a partially failed apply is PARTIAL → LIMITED, never "in sync".
 *
 * Status vocabulary (exactly the six states the product brief asks for):
 *   ACTIVE · APPLYING · LIMITED · FAILED · DISABLED · PASSTHROUGH
 *
 * Pure C, no <windows.h>: unit-tested in tests/test_all.c and shared by the UI,
 * the diagnostics report, the tray tooltip and the phone remote, so no surface
 * can disagree with another.
 */
#ifndef PLEXUSX_COLOR_RUNTIME_STATE_H
#define PLEXUSX_COLOR_RUNTIME_STATE_H

#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include "look.h"
#include "color_math.h"
#include "applied_color_state.h"
#include "../display/display_state.h"
#include "../games/game_display_state.h"

/* ---------------- Pipeline status (the six honest states) ------------------ */
typedef enum PxColorStatus {
    PX_STATUS_DISABLED = 0,   /* engine bypassed by the user: identity output   */
    PX_STATUS_PASSTHROUGH,    /* enabled, but the output is owned by HDR/other:
                                 PlexusX writes nothing and says so              */
    PX_STATUS_APPLYING,       /* an apply is due / in flight / never completed  */
    PX_STATUS_ACTIVE,         /* requested == effective == applied              */
    PX_STATUS_LIMITED,        /* applied, but the path cannot carry every part  */
    PX_STATUS_FAILED          /* the display stack rejected the apply           */
} PxColorStatus;

static inline const char *PxStatus_Name(int s)
{
    switch (s) {
    case PX_STATUS_ACTIVE:      return "ACTIVE";
    case PX_STATUS_APPLYING:    return "APPLYING";
    case PX_STATUS_LIMITED:     return "LIMITED";
    case PX_STATUS_FAILED:      return "FAILED";
    case PX_STATUS_DISABLED:    return "DISABLED";
    case PX_STATUS_PASSTHROUGH: return "PASSTHROUGH";
    default:                    return "UNKNOWN";
    }
}

/* Stable machine id for JSON / phone / logs (never translated). */
static inline const char *PxStatus_Id(int s)
{
    switch (s) {
    case PX_STATUS_ACTIVE:      return "active";
    case PX_STATUS_APPLYING:    return "applying";
    case PX_STATUS_LIMITED:     return "limited";
    case PX_STATUS_FAILED:      return "failed";
    case PX_STATUS_DISABLED:    return "disabled";
    case PX_STATUS_PASSTHROUGH: return "passthrough";
    default:                    return "unknown";
    }
}

/* Chip semantic (common.h PX_CHIP_*): 0 neutral, 1 ok, 2 warn, 3 bad, 4 info.
 * Plain ints so this header stays free of the Win32 common.h. */
static inline int PxStatus_Tone(int s)
{
    switch (s) {
    case PX_STATUS_ACTIVE:      return 1;
    case PX_STATUS_APPLYING:    return 4;
    case PX_STATUS_LIMITED:     return 2;
    case PX_STATUS_FAILED:      return 3;
    case PX_STATUS_PASSTHROUGH: return 4;
    default:                    return 0;
    }
}

/* ---------------- Output facts (collected by the sensors) ------------------ */
typedef struct PxOutputFacts {
    int mag_available;     /* Windows Magnification color path usable          */
    int ramps_available;   /* >= 1 display accepted the GPU gamma LUT          */
    int displays_total;    /* monitors the display manager found               */
    int hdr_active;        /* a targeted output runs an HDR transfer function  */
    int hdr_capable;       /* the OS reports HDR capability for the output     */
    int presentation;      /* PxPresMode of the foreground window              */
    int game_active;       /* a game profile is currently driving the output   */
    int engine_enabled;    /* REQUESTED enabled flag                           */
    int apply_pending;     /* a UI edit is still being applied (drag in flight) */
} PxOutputFacts;

static inline void PxFacts_Init(PxOutputFacts *f)
{
    memset(f, 0, sizeof *f);
    f->presentation = PX_PRES_NONE;
}

/* ---------------- Effective state ----------------------------------------- */
typedef struct PxEffectiveState {
    Look        requested;         /* sanitised user request                    */
    Look        effective;         /* what the paths below can deliver          */
    Look        applied;           /* last hardware-confirmed look              */
    int         have_applied;

    int         status;            /* PxColorStatus                             */

    /* delivery capability, per half of the pipeline */
    int         linear_deliverable;/* DWM matrix reaches the foreground content */
    int         curves_deliverable;/* gamma LUT is scanout-side and applies     */
    int         linear_live;       /* matrix confirmed applied                  */
    int         curves_live;       /* ramps confirmed applied                   */

    int         hdr_active;
    int         hdr_capable;
    int         exclusive_like;    /* presentation bypasses DWM composition     */
    int         engine_enabled;
    int         diverged;          /* requested != applied (or not applied yet) */
    int         outcome;           /* PxApplyOutcome of the last apply          */

    unsigned    revision;          /* ColorState revision this describes        */
    unsigned    applied_ms;

    char        reason[112];       /* ASCII, one line, why the status is what it is */
} PxEffectiveState;

static inline int PxEff_Eq(float a, float b) { return (a > b ? a - b : b - a) <= 0.01f; }

/* Is there anything in the look that only the LINEAR (DWM matrix) half carries? */
static inline int PxEff_LinearPartNonNeutral(const Look *lk)
{
    return !(PxEff_Eq(lk->sat, 100.0f) && PxEff_Eq(lk->vibrance, 100.0f) &&
             PxEff_Eq(lk->bri, 100.0f) && PxEff_Eq(lk->con, 100.0f) &&
             PxEff_Eq(lk->temp, 6500.0f) && PxEff_Eq(lk->tint, 0.0f) &&
             PxEff_Eq(lk->r_gain, 100.0f) && PxEff_Eq(lk->g_gain, 100.0f) &&
             PxEff_Eq(lk->b_gain, 100.0f) && PxEff_Eq(lk->hue, 0.0f) &&
             PxEff_Eq(lk->black_level, 100.0f) && PxEff_Eq(lk->white_point, 100.0f));
}

/* Does the look contain anything the NON-LINEAR (GPU ramp) half carries? */
static inline int PxEff_CurvePartNonNeutral(const Look *lk)
{
    return !cm_curves_neutral(lk);
}

/* Build the look the curve half alone can deliver: linear stages neutral,
 * tone curves exactly as requested. */
static inline Look PxEff_CurvesOnlyLook(const Look *lk)
{
    Look c = *lk;
    c.sat = 100.0f; c.vibrance = 100.0f; c.bri = 100.0f; c.con = 100.0f;
    c.temp = 6500.0f; c.tint = 0.0f;
    c.r_gain = 100.0f; c.g_gain = 100.0f; c.b_gain = 100.0f; c.hue = 0.0f;
    c.black_level = 100.0f; c.white_point = 100.0f;
    cm_sanitize_look(&c);
    return c;
}

static inline void PxEff_SetReason(PxEffectiveState *e, const char *why)
{
    size_t i = 0;
    if (!why) { e->reason[0] = 0; return; }
    for (; why[i] && i + 1 < sizeof e->reason; i++) e->reason[i] = why[i];
    e->reason[i] = 0;
}

/* THE function: derive the EFFECTIVE state + the one honest status.
 *
 * Invariants (unit-tested in tests/test_all.c):
 *   - ACTIVE requires: engine enabled, applied.have, outcome FULL, no divergence,
 *     a usable path, and (when a linear adjustment is requested) a live matrix.
 *   - HDR output while enabled  ⇒ PASSTHROUGH, never ACTIVE.
 *   - exclusive-fullscreen with linear adjustments requested ⇒ LIMITED, and the
 *     EFFECTIVE look provably equals the curves-only look.
 *   - FAILED / PARTIAL apply ⇒ FAILED / LIMITED.  Never silent success. */
static inline void PxEff_Compute(const Look *requested_in,
                                 const AppliedColorState *applied,
                                 const PxOutputFacts *facts,
                                 PxEffectiveState *out)
{
    PxEffectiveState e;
    int linear_req;
    memset(&e, 0, sizeof e);

    Look req = requested_in ? *requested_in : (Look)LOOK_NEUTRAL_INIT;
    cm_sanitize_look(&req);
    e.requested = req;
    e.engine_enabled = req.enabled ? 1 : 0;
    e.hdr_active = facts->hdr_active ? 1 : 0;
    e.hdr_capable = facts->hdr_capable ? 1 : 0;
    e.exclusive_like = (facts->presentation == PX_PRES_FULLSCREEN_SURFACE) ? 1 : 0;
    e.diverged = 1;

    if (applied && applied->have) {
        e.have_applied = 1;
        e.applied = applied->look;
        e.applied_ms = applied->applied_ms;
        e.outcome = applied->outcome;
        e.revision = applied->revision;
        e.diverged = (applied->outcome != PX_APPLY_FULL ||
                      !cm_looks_equal(&applied->look, &req)) ? 1 : 0;
    } else {
        e.outcome = PX_APPLY_NOT_YET;
    }

    /* ---- what each half of the pipeline can reach right now ---- */
    e.curves_deliverable = facts->ramps_available ? 1 : 0;   /* scanout-side LUT */
    e.linear_deliverable = (facts->mag_available && !e.exclusive_like) ? 1 : 0;
    if (e.hdr_active) {                 /* the OS tone-mapper owns the output */
        e.curves_deliverable = 0;
        e.linear_deliverable = 0;
    }

    /* ---- effective look ---- */
    if (!req.enabled || e.hdr_active) {
        e.effective = (Look)LOOK_NEUTRAL_INIT;           /* bypass / pass-through */
    } else if (e.linear_deliverable) {
        e.effective = req;                               /* both halves live      */
    } else if (e.curves_deliverable) {
        e.effective = PxEff_CurvesOnlyLook(&req);        /* curves only           */
    } else {
        e.effective = (Look)LOOK_NEUTRAL_INIT;           /* nothing can carry it  */
    }

    /* ---- confirm which halves the hardware actually accepted ---- */
    if (e.have_applied && !e.hdr_active && req.enabled) {
        e.curves_live = applied->ramps_ok ? 1 : 0;
        e.linear_live = applied->matrix_ok ? 1 : 0;
    }

    linear_req = PxEff_LinearPartNonNeutral(&req);
    {
        int curve_req = PxEff_CurvePartNonNeutral(&req);

        /* ---- the status decision (ordered; no optimism anywhere) ---- */
        if (!req.enabled) {
            e.status = PX_STATUS_DISABLED;
            PxEff_SetReason(&e, facts->ramps_available || !e.have_applied
                                ? "engine bypassed: display output is identity"
                                : "engine bypassed, but the driver rejected the restore of the original ramps");
        } else if (e.hdr_active) {
            e.status = PX_STATUS_PASSTHROUGH;
            PxEff_SetReason(&e, "output is HDR: the OS tone-mapper owns the display LUT, PlexusX writes nothing");
        } else if (e.have_applied && applied->outcome == PX_APPLY_FAILED && (linear_req || curve_req)) {
            e.status = PX_STATUS_FAILED;
            PxEff_SetReason(&e, applied->note[0] ? applied->note
                                                 : "the display stack rejected the transformation");
        } else if (!e.have_applied || facts->apply_pending) {
            e.status = PX_STATUS_APPLYING;
            PxEff_SetReason(&e, e.have_applied ? "requested look changed; an apply is in flight"
                                               : "no confirmed apply yet");
        } else if (e.diverged) {
            /* Applied but different from the request: a re-apply is owed.  When the
             * request needs the matrix and only the ramp half survived, say so. */
            if (e.have_applied && applied->outcome == PX_APPLY_PARTIAL && linear_req) {
                e.status = PX_STATUS_LIMITED;
                PxEff_SetReason(&e, "only the GPU tone-curve half is live; the linear half was not accepted");
            } else {
                e.status = PX_STATUS_APPLYING;
                PxEff_SetReason(&e, "requested look differs from the applied look; a re-apply is due");
            }
        } else if (!linear_req && !curve_req) {
            e.status = PX_STATUS_ACTIVE;                  /* neutral look is neutral */
            PxEff_SetReason(&e, "neutral look confirmed on the display");
        } else if (!e.linear_deliverable && linear_req && e.curves_deliverable) {
            e.status = PX_STATUS_LIMITED;
            PxEff_SetReason(&e, "fullscreen surface bypasses DWM: linear controls cannot reach it, tone curves are live");
        } else if (linear_req && !e.linear_live && e.curves_live) {
            e.status = PX_STATUS_LIMITED;
            PxEff_SetReason(&e, "tone curves applied; the linear matrix was not confirmed on the display");
        } else if (curve_req && !e.curves_deliverable) {
            e.status = PX_STATUS_LIMITED;
            PxEff_SetReason(&e, "linear transformation is live; the GPU gamma ramp path is unavailable");
        } else if (e.have_applied && applied->outcome == PX_APPLY_PARTIAL) {
            e.status = PX_STATUS_LIMITED;
            PxEff_SetReason(&e, applied->note[0] ? applied->note : "part of the pipeline was rejected by the driver");
        } else if (!e.linear_deliverable && !e.curves_deliverable) {
            e.status = PX_STATUS_FAILED;
            PxEff_SetReason(&e, "no usable color path reported by Windows");
        } else {
            e.status = PX_STATUS_ACTIVE;
            PxEff_SetReason(&e, (e.linear_live && e.curves_live)
                                ? "linear matrix and tone curves confirmed on the display"
                                : (e.linear_live ? "linear matrix confirmed; tone curves are neutral"
                                                 : "tone curves confirmed on the display"));
        }
    }

    *out = e;
}

/* One-line summary for the status bar / phone: "ACTIVE | sat 150 vib 120 | matrix+ramps" */
static inline void PxEff_Summary(const PxEffectiveState *e, char *buf, size_t cap)
{
    if (!buf || !cap) return;
    snprintf(buf, cap, "%s | sat %.0f vib %.0f bri %.0f con %.0f gam %.2f | %s + %s%s",
             PxStatus_Name(e->status), e->effective.sat, e->effective.vibrance,
             e->effective.bri, e->effective.con, e->effective.gamma,
             e->linear_deliverable ? "matrix" : "no-matrix",
             e->curves_deliverable ? "ramps" : "no-ramps",
             e->diverged ? " | diverged" : "");
}

#endif /* PLEXUSX_COLOR_RUNTIME_STATE_H */
