/* PlexusX — ColorTransform (header only, platform independent)
 *
 * THE single source of truth for color mathematics.  This header is the named
 * façade over the transform kernel in color_math.h (Rec.709 saturation/vibrance,
 * W3C hue rotation, Planckian temperature, tint/RGB gain, brightness/contrast/
 * black/white as the DWM 5x5 matrix; gamma power, shadow toe, highlight
 * shoulder and clarity S-curve as the monotonic 16-bit GPU ramp).  No second
 * implementation may exist anywhere:
 *
 *   native UI preview (ui.c)   — PxPlan_ApplyPixel()   (same code the engine feeds DWM)
 *   color engine  (color_engine.c) — PxPlan_*()         (drives the pipeline)
 *   host tests    (tests/test_all.c) — color_math.h directly + this façade
 *   web preview   (site/assets/color_engine.js) — exact port, parity-tested
 *
 * The plan is computed BEFORE the hardware is touched so diagnostics can show
 * what WOULD be written (and why), and per-display ramp planning stays honest
 * (px_ramp_plan needs the display's original + current LUT).
 */
#ifndef PLEXUSX_COLOR_TRANSFORM_H
#define PLEXUSX_COLOR_TRANSFORM_H

#include "color_math.h"

typedef struct PxTransformPlan {
    Look           sanitized;      /* the exact values the engine will apply   */
    MagColorEffect effect;         /* linear half -> DWM MagSetFullscreenColorEffect */
    int            sanitize_flags; /* CM_SAN_* observed while building          */
    int            bypassed;       /* engine disabled: restore everything       */
    int            neutral_matrix; /* effect is identity: DWM call can be skipped */
    int            curves_neutral; /* gamma/shadows/highlights/clarity at 1.0/100 */
    int            nonfinite_input;/* a NaN/Inf came in: look was force-neutralised */
} PxTransformPlan;

/* Build the LINEAR half of the plan for a requested look. */
static inline void PxPlan_Compute(const Look *in, PxTransformPlan *p)
{
    Look lk = *in;
    cm_sanitize_look(&lk);
    p->sanitized = lk;
    p->bypassed = lk.enabled ? 0 : 1;
    p->curves_neutral = cm_curves_neutral(&lk);

    Look applied = lk;
    if (p->bypassed) applied = (Look)LOOK_NEUTRAL_INIT;   /* bypass == identity output */
    cm_build_effect(&applied, &p->effect);
    p->neutral_matrix = cm_effect_is_identity(&p->effect);

    /* Diagnostics only: did the incoming request need normalising (clamped
     * range) and were there non-finite inputs (force-neutralised)? */
    p->sanitize_flags = 0;
    p->nonfinite_input = 0;
    {
        const float *raw = (const float *)&in->sat;
        const float *clr = (const float *)&lk.sat;
        for (int i = 0; i < 16; i++) {
            if (!isfinite(raw[i])) { p->nonfinite_input = 1; p->sanitize_flags = 1; break; }
            if (raw[i] != clr[i]) p->sanitize_flags = 1;
        }
        if (in->enabled != (lk.enabled ? 1 : 0)) p->sanitize_flags = 1;
    }
}

/* Decide what one display's hardware LUT should do for this plan.
 *   orig      the ramp the display had before PlexusX touched it
 *   curr      the ramp PlexusX last wrote (== orig until the first write)
 *   have_curr curr is trustworthy (cleared by a resync after external resets)
 * Returns 1 and fills `want` when SetDeviceGammaRamp must run, 0 when the
 * display is already in the wanted state (bypass/neutral: nothing to do). */
static inline int PxPlan_RampForDisplay(const PxTransformPlan *p,
                                        unsigned short orig[3][256],
                                        unsigned short curr[3][256],
                                        int have_curr,
                                        unsigned short want[3][256])
{
    if (p->bypassed) {
        if (memcmp(curr, orig, CM_RAMP_BYTES) == 0) return 0;
        memcpy(want, orig, CM_RAMP_BYTES);
        return 1;
    }
    return cm_ramp_plan(orig, curr, have_curr, &p->sanitized, want);
}

/* The EXACT tone response of a look for one pixel: the same math the engine
 * pushes to the hardware.  Used by every live preview (native UI + parity
 * reference for the web preview). */
static inline void PxPlan_ApplyPixel(const PxTransformPlan *p, float r, float g, float b,
                                     float *or_, float *og_, float *ob_)
{
    cm_apply_pixel(&p->sanitized, r, g, b, or_, og_, ob_);
}

/* Convenience when no plan is at hand (stateless preview). */
static inline void PxTransform_ApplyPixel(const Look *lk, float r, float g, float b,
                                          float *or_, float *og_, float *ob_)
{
    cm_apply_pixel(lk, r, g, b, or_, og_, ob_);
}

/* Compute the non-linear ramp alone (preview curves, hardware fallback paths). */
static inline void PxTransform_CalcRamp(const Look *lk, unsigned short ramp[3][256])
{
    cm_calc_ramp(lk, ramp);
}

#endif /* PLEXUSX_COLOR_TRANSFORM_H */
