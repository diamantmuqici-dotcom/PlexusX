/* PlexusX — Colour math (header only, platform independent)
 *
 * Everything in here is pure C11 with no Win32 dependency.  engine.c uses it to
 * drive the Windows Magnification API and the GPU gamma ramps, and
 * tests/test_all.c compiles the very same code on the host, so the unit tests
 * cover what actually ships (not a hand-copied duplicate).
 *
 * ── Display pipeline split ──────────────────────────────────────────────────
 *   DWM 5x5 matrix (MagSetFullscreenColorEffect) — every LINEAR adjustment:
 *       hue, saturation, vibrance, temperature, tint, R/G/B gain,
 *       brightness, contrast, black level, white point.
 *   GPU gamma ramp (SetDeviceGammaRamp)           — only NON-LINEAR tone curves:
 *       gamma power curve, shadows toe, highlights shoulder, clarity S-curve.
 *   Keeping the two apart means dragging a colour slider never touches the GPU
 *   LUT (no driver re-sync / blackout) and nothing is applied twice.
 *
 * ── MAGCOLOREFFECT layout (row-vector convention) ───────────────────────────
 *       [R' G' B' A' W'] = [R G B A 1] * transform          transform is 5x5
 *   rows 0..3  weights of the R, G, B, A inputs
 *   row  4     translation (offset) row — brightness / contrast / black level
 *              live ONLY in transform[4][0..2]
 *   column 4   the homogeneous W' divisor column: it MUST be exactly
 *              [0 0 0 0 1]^T.  Offsets in column 4 make W' <= 0 and the DWM
 *              shader blanks the screen black/green.
 */
#ifndef PLEXUSX_COLOR_MATH_H
#define PLEXUSX_COLOR_MATH_H

#include <math.h>
#include <stddef.h>
#include <string.h>

#include "look.h"

/* The sanitisers below exist to catch NaN / Inf.  -ffast-math (-ffinite-math-only) lets the compiler
 * assume they cannot occur and silently removes those checks, so refuse to build that way. */
#if defined(__FINITE_MATH_ONLY__) && __FINITE_MATH_ONLY__
#error "color_math.h relies on NaN/Inf detection: build without -ffast-math / -ffinite-math-only"
#endif

#define CM_PI 3.14159265358979323846f

/* Rec.709 / W3C luminance coefficients */
#define CM_LUM_R 0.2126f
#define CM_LUM_G 0.7152f
#define CM_LUM_B 0.0722f

/* Matrix weights outside this range are never sent to the DWM */
#define CM_WEIGHT_LIMIT 4.0f

/* ============================ 5x5 matrix core ============================= */

/* Win32 MAGCOLOREFFECT: float transform[5][5] = 100 bytes.  The previous build
 * declared float m[4][5] (80 bytes) and the API read 20 bytes of stack garbage
 * as row 4 (the translation row). */
typedef struct MagColorEffect {
    float transform[5][5];       /* 25 floats, naturally 4-aligned, no padding */
} MagColorEffect;

_Static_assert(sizeof(MagColorEffect) == 100, "MagColorEffect must be the 100-byte Win32 MAGCOLOREFFECT");

static inline float cm_clampf(float v, float lo, float hi)
{
    if (!(v >= lo)) return lo;   /* NaN falls into this branch as well */
    if (v > hi) return hi;
    return v;
}

static inline void cm_identity(MagColorEffect *e)
{
    memset(e, 0, sizeof *e);
    for (int i = 0; i < 5; i++) e->transform[i][i] = 1.0f;
}

/* out = a * b.  Row-vector convention: `a` is applied first, then `b`.
 * `out` may alias `a` or `b`. */
static inline void cm_mul(MagColorEffect *out, const MagColorEffect *a, const MagColorEffect *b)
{
    MagColorEffect t;
    for (int r = 0; r < 5; r++) {
        for (int c = 0; c < 5; c++) {
            float s = 0.0f;
            for (int k = 0; k < 5; k++) s += a->transform[r][k] * b->transform[k][c];
            t.transform[r][c] = s;
        }
    }
    *out = t;
}

static inline int cm_effect_is_identity(const MagColorEffect *e)
{
    for (int r = 0; r < 5; r++)
        for (int c = 0; c < 5; c++)
            if (e->transform[r][c] != (r == c ? 1.0f : 0.0f)) return 0;
    return 1;
}

/* Result flags of cm_sanitize() */
#define CM_SAN_NONFINITE 1   /* a NaN/Inf was found: whole matrix replaced by identity */
#define CM_SAN_CLAMPED   2   /* at least one weight was outside [-4, 4] */

/* Final safety net, applied right before MagSetFullscreenColorEffect():
 *   - any NaN/Inf anywhere           -> the whole matrix becomes the identity
 *   - weights clamped to [-4, 4]
 *   - structure enforced: column 4 == [0 0 0 0 1], alpha passes straight through
 * Returns a bit mask of CM_SAN_* describing what had to be fixed. */
static inline int cm_sanitize(MagColorEffect *e)
{
    int flags = 0;

    for (int r = 0; r < 5; r++) {
        for (int c = 0; c < 5; c++) {
            if (!isfinite(e->transform[r][c])) {
                cm_identity(e);
                return CM_SAN_NONFINITE;
            }
        }
    }

    for (int r = 0; r < 5; r++) {
        for (int c = 0; c < 5; c++) {
            float v = e->transform[r][c];
            if (v < -CM_WEIGHT_LIMIT || v > CM_WEIGHT_LIMIT) {
                e->transform[r][c] = cm_clampf(v, -CM_WEIGHT_LIMIT, CM_WEIGHT_LIMIT);
                flags |= CM_SAN_CLAMPED;
            }
        }
    }

    /* Column 4 (homogeneous W' divisor): strictly [0, 0, 0, 0, 1] */
    for (int r = 0; r < 4; r++) e->transform[r][4] = 0.0f;
    e->transform[4][4] = 1.0f;

    /* Alpha: untouched by colour, no colour contribution, no offset */
    for (int r = 0; r < 3; r++) e->transform[r][3] = 0.0f;
    for (int c = 0; c < 3; c++) e->transform[3][c] = 0.0f;
    e->transform[3][3] = 1.0f;
    e->transform[4][3] = 0.0f;

    return flags;
}

/* ===================== Individual linear colour stages ===================== */

/* Saturation / vibrance: luminance-preserving (Rec.709), grays are fixed points */
static inline void cm_saturation(MagColorEffect *o, float sat_pct, float vib_pct)
{
    const float w[3] = { CM_LUM_R, CM_LUM_G, CM_LUM_B };
    float eff = (sat_pct / 100.0f) * (0.6f + 0.4f * (vib_pct / 100.0f));
    if (fabsf(eff - 1.0f) < 1e-6f) eff = 1.0f;   /* exact identity when neutral */

    cm_identity(o);
    /* Row-vector form: out_j = eff * in_j + (1 - eff) * Y,  Y = sum_i w_i * in_i */
    for (int i = 0; i < 3; i++)
        for (int j = 0; j < 3; j++)
            o->transform[i][j] = (i == j ? eff : 0.0f) + w[i] * (1.0f - eff);
}

/* Hue rotation about the neutral axis.
 *
 * This is the W3C Filter Effects `hueRotate` matrix with exact Rec.709
 * luminance coefficients.  By construction each row of the sine and cosine
 * parts sums to zero, so pure white (1,1,1) and every neutral gray map to
 * themselves at ALL angles, and Rec.709 luminance is preserved.  It is also an
 * exact rotation group: hue(a) * hue(b) == hue(a + b).
 * (The previous matrix mixed coefficients from two different derivations: white
 *  became (1.14, 0.87, 0.87) at 30 degrees and went out of range beyond that.)
 *
 * The W3C formula is written for column vectors; MAGCOLOREFFECT uses row
 * vectors, so the matrix is stored transposed. */
static inline void cm_hue(MagColorEffect *o, float degrees)
{
    const float lr = CM_LUM_R, lg = CM_LUM_G, lb = CM_LUM_B;
    const float rad = degrees * (CM_PI / 180.0f);
    const float c = cosf(rad), s = sinf(rad);

    /* Skew terms for the middle row.  The W3C text prints 0.143 / 0.140 / -0.283
     * for the older .213/.715/.072 weights; these are the exact closed forms. */
    const float k10 = (lr * lr + lb * (1.0f - lr)) / lg;
    const float k11 = lr - lb;
    const float k12 = -(lr * (1.0f - lb) + lb * lb) / lg;

    const float h[3][3] = {   /* column-vector form: out = h * in */
        { lr + c * (1.0f - lr) - s * lr,  lg - c * lg - s * lg,            lb - c * lb + s * (1.0f - lb) },
        { lr - c * lr + s * k10,          lg + c * (1.0f - lg) + s * k11,  lb - c * lb + s * k12         },
        { lr - c * lr - s * (1.0f - lr),  lg - c * lg + s * lg,            lb + c * (1.0f - lb) + s * lb },
    };

    cm_identity(o);
    for (int i = 0; i < 3; i++)
        for (int j = 0; j < 3; j++)
            o->transform[i][j] = h[j][i];          /* transpose for row vectors */
}

/* Kelvin -> RGB multiplier (Planckian locus approximation).  Total function:
 * NaN/Inf and out-of-range inputs can never produce a NaN. */
static inline void cm_kelvin_to_rgb(float k, float *r, float *g, float *b)
{
    if (!isfinite(k)) k = 6500.0f;
    k = cm_clampf(k, 1000.0f, 40000.0f);

    float temp = k / 100.0f;
    float red, green, blue;

    if (temp <= 66.0f) {
        red = 255.0f;
        green = 99.4708025861f * logf(temp) - 161.1195681661f;
        if (temp <= 19.0f) {
            blue = 0.0f;
        } else {
            blue = 138.5177312231f * logf(temp - 10.0f) - 305.0447927307f;
        }
    } else {
        red   = 329.698727446f  * powf(temp - 60.0f, -0.1332047592f);
        green = 288.1221695283f * powf(temp - 60.0f, -0.0755148492f);
        blue  = 255.0f;
    }

    *r = cm_clampf(red   / 255.0f, 0.0f, 2.0f);
    *g = cm_clampf(green / 255.0f, 0.0f, 2.0f);
    *b = cm_clampf(blue  / 255.0f, 0.0f, 2.0f);
}

/* Temperature (relative to D65), tint and per-channel gain: a diagonal scale */
static inline void cm_temp_tint_gain(MagColorEffect *o, float temp_k, float tint_pct,
                                     float rg, float gg, float bg)
{
    float kr, kg, kb, nr, ng, nb;
    cm_kelvin_to_rgb(temp_k, &kr, &kg, &kb);
    cm_kelvin_to_rgb(6500.0f, &nr, &ng, &nb);   /* normalise: 6500 K is neutral */
    kr /= nr; kg /= ng; kb /= nb;

    /* Tint: -100 (green) .. +100 (magenta) */
    float tn = tint_pct / 100.0f;
    float tg = 1.0f - tn * 0.20f;
    float tr = 1.0f + tn * 0.15f;
    float tb = 1.0f + tn * 0.15f;

    cm_identity(o);
    o->transform[0][0] = kr * tr * rg;
    o->transform[1][1] = kg * tg * gg;
    o->transform[2][2] = kb * tb * bg;
}

/* Brightness, contrast, black level and white point.
 *   out = slope * in + offset
 * The slope is the matrix diagonal; the offset is a TRANSLATION and therefore
 * lives in row 4 (transform[4][0..2]) — never in column 4. */
static inline void cm_bri_con(MagColorEffect *o, float con_pct, float bri_pct,
                              float bl_pct, float wp_pct)
{
    float c  = con_pct / 100.0f;
    float b  = bri_pct / 100.0f;
    float wp = wp_pct  / 100.0f;
    float bl = (bl_pct - 100.0f) / 200.0f;              /* black floor offset */

    float slope  = c * b * wp;
    float offset = 0.5f * (1.0f - c) + (b - 1.0f) * 0.5f + bl;

    cm_identity(o);
    for (int i = 0; i < 3; i++) {
        o->transform[i][i] = slope;
        o->transform[4][i] = offset;
    }
}

/* ======================= Look sanitising & full matrix ===================== */

static inline float cm_finite_clamp(float v, float lo, float hi, float fallback)
{
    if (!isfinite(v)) return fallback;
    return cm_clampf(v, lo, hi);
}

/* Force every parameter into its documented range; NaN/Inf fall back to the
 * neutral value.  Protects the math from corrupt config files, bad phone input
 * or any other source. */
static inline void cm_sanitize_look(Look *lk)
{
    lk->enabled     = lk->enabled ? 1 : 0;
    lk->sat         = cm_finite_clamp(lk->sat,         0.0f,   300.0f,   100.0f);
    lk->vibrance    = cm_finite_clamp(lk->vibrance,    0.0f,   300.0f,   100.0f);
    lk->bri         = cm_finite_clamp(lk->bri,         0.0f,   200.0f,   100.0f);
    lk->con         = cm_finite_clamp(lk->con,         0.0f,   200.0f,   100.0f);
    lk->gamma       = cm_finite_clamp(lk->gamma,       0.40f,  2.50f,    1.0f);
    lk->temp        = cm_finite_clamp(lk->temp,        1000.0f, 40000.0f, 6500.0f);
    lk->tint        = cm_finite_clamp(lk->tint,        -100.0f, 100.0f,  0.0f);
    lk->r_gain      = cm_finite_clamp(lk->r_gain,      0.0f,   200.0f,   100.0f);
    lk->g_gain      = cm_finite_clamp(lk->g_gain,      0.0f,   200.0f,   100.0f);
    lk->b_gain      = cm_finite_clamp(lk->b_gain,      0.0f,   200.0f,   100.0f);
    lk->shadows     = cm_finite_clamp(lk->shadows,     0.0f,   200.0f,   100.0f);
    lk->highlights  = cm_finite_clamp(lk->highlights,  0.0f,   200.0f,   100.0f);
    lk->black_level = cm_finite_clamp(lk->black_level, 0.0f,   200.0f,   100.0f);
    lk->white_point = cm_finite_clamp(lk->white_point, 0.0f,   200.0f,   100.0f);
    lk->clarity     = cm_finite_clamp(lk->clarity,     0.0f,   200.0f,   100.0f);
    lk->hue         = cm_finite_clamp(lk->hue,         -180.0f, 180.0f,  0.0f);
}

/* Builds the complete LINEAR transform for a look (hue -> saturation/vibrance ->
 * temperature/tint/gain -> brightness/contrast/black/white; each stage is
 * applied after the previous one) and sanitises it.  The result is safe to hand
 * to MagSetFullscreenColorEffect() as is. */
static inline void cm_build_effect(const Look *in, MagColorEffect *out)
{
    Look lk = *in;
    cm_sanitize_look(&lk);

    MagColorEffect m, t;
    cm_identity(&m);

    if (fabsf(lk.hue) > 0.01f) {
        cm_hue(&t, lk.hue);
        cm_mul(&m, &m, &t);
    }

    cm_saturation(&t, lk.sat, lk.vibrance);
    cm_mul(&m, &m, &t);

    cm_temp_tint_gain(&t, lk.temp, lk.tint,
                      lk.r_gain / 100.0f, lk.g_gain / 100.0f, lk.b_gain / 100.0f);
    cm_mul(&m, &m, &t);

    cm_bri_con(&t, lk.con, lk.bri, lk.black_level, lk.white_point);
    cm_mul(&m, &m, &t);

    cm_sanitize(&m);
    *out = m;
}

/* ======================= Non-linear GPU gamma ramp ========================= */

#define CM_RAMP_BYTES      (3 * 256 * sizeof(unsigned short))
#define CM_RAMP_MIN_SPAN   0x4000    /* a usable ramp must span at least 25% of the range */

/* "Neutral curves" tolerances: what the UI displays as 1.00 / 100% */
#define CM_NEUTRAL_EPS_GAMMA 0.005f
#define CM_NEUTRAL_EPS_PCT   0.5f

/* True when gamma == 1.0, shadows == 100, highlights == 100 and clarity == 100
 * (within display precision): there is nothing for the GPU ramp to do, and
 * SetDeviceGammaRamp must not be called. */
static inline int cm_curves_neutral(const Look *lk)
{
    return fabsf(lk->gamma      - 1.0f)   <= CM_NEUTRAL_EPS_GAMMA &&
           fabsf(lk->shadows    - 100.0f) <= CM_NEUTRAL_EPS_PCT &&
           fabsf(lk->highlights - 100.0f) <= CM_NEUTRAL_EPS_PCT &&
           fabsf(lk->clarity    - 100.0f) <= CM_NEUTRAL_EPS_PCT;
}

static inline void cm_identity_ramp(unsigned short ramp[3][256])
{
    for (int ch = 0; ch < 3; ch++)
        for (int i = 0; i < 256; i++)
            ramp[ch][i] = (unsigned short)(i * 257);          /* i * 65535 / 255 */
}

/* Forces every channel to be non-decreasing (WDDM requires monotonic ramps). */
static inline void cm_ramp_make_monotonic(unsigned short ramp[3][256])
{
    for (int ch = 0; ch < 3; ch++) {
        for (int i = 1; i < 256; i++) {
            if (ramp[ch][i] < ramp[ch][i - 1]) ramp[ch][i] = ramp[ch][i - 1];
        }
    }
}

/* Computes the NON-LINEAR part of a look only: gamma power curve, shadows toe,
 * highlights shoulder and midtone clarity S-curve.  RGB gain, brightness,
 * contrast, black level and white point are linear and live in the DWM matrix;
 * applying them here as well would count them twice.
 * The result is guaranteed non-decreasing (WDDM requires monotonic ramps). */
static inline void cm_calc_ramp(const Look *in, unsigned short ramp[3][256])
{
    Look lk = *in;
    cm_sanitize_look(&lk);

    const float g       = cm_clampf(lk.gamma, 0.40f, 2.50f);
    const float sh_lift = (lk.shadows    - 100.0f) / 100.0f;     /* -1.0 .. +1.0 */
    const float hl_lift = (lk.highlights - 100.0f) / 100.0f;     /* -1.0 .. +1.0 */
    const float clarity = (lk.clarity    - 100.0f) / 100.0f;     /* -1.0 .. +1.0 */

    for (int i = 0; i < 256; i++) {
        float x = i / 255.0f;
        float y = powf(x, g);                                    /* base gamma curve */

        if (fabsf(sh_lift) > 0.001f) {                           /* shadows: toe */
            float toe = (1.0f - x) * (1.0f - x) * sh_lift * 0.35f;
            y = cm_clampf(y + toe, 0.0f, 1.0f);
        }
        if (fabsf(hl_lift) > 0.001f) {                           /* highlights: shoulder */
            float shoulder = x * x * hl_lift * 0.30f;
            y = cm_clampf(y + shoulder, 0.0f, 1.0f);
        }
        if (fabsf(clarity) > 0.001f) {                           /* clarity: midtone S-curve */
            float scurve = 0.5f * (1.0f - cosf(x * CM_PI)) - x;
            y = cm_clampf(y + scurve * clarity * 0.25f, 0.0f, 1.0f);
        }

        int val = (int)(y * 65535.0f + 0.5f);
        if (val < 0) val = 0;
        if (val > 65535) val = 65535;
        ramp[0][i] = (unsigned short)val;
        ramp[1][i] = ramp[0][i];
        ramp[2][i] = ramp[0][i];
    }

    /* A toe lift combined with a steep gamma can make the raw curve dip.
     * Enforce non-decreasing output. */
    cm_ramp_make_monotonic(ramp);
}

/* A ramp worth sending to (or restoring on) real hardware: every channel is
 * non-decreasing and spans a meaningful part of the range (rejects all-zero /
 * constant data, which would black the screen out). */
static inline int cm_ramp_is_valid(unsigned short ramp[3][256])
{
    for (int ch = 0; ch < 3; ch++) {
        for (int i = 1; i < 256; i++) {
            if (ramp[ch][i] < ramp[ch][i - 1]) return 0;
        }
        if ((int)ramp[ch][255] - (int)ramp[ch][0] < CM_RAMP_MIN_SPAN) return 0;
    }
    return 1;
}

/* Decides what to do with one display's hardware ramp for a given look.
 *   orig      the ramp the display had before PlexusX touched it
 *   curr      the ramp PlexusX last wrote (== orig until the first write)
 *   have_curr curr is trusted (cleared by Eng_Resync() after external resets)
 * Returns 1 and fills `want` when SetDeviceGammaRamp must be called, or 0 when
 * the call can be skipped:
 *   - neutral curves: nothing to apply; only if a previous write left the display
 *     on a non-original ramp is the original restored (once)
 *   - otherwise: skip when the computed ramp equals what is already programmed */
static inline int cm_ramp_plan(unsigned short orig[3][256], unsigned short curr[3][256],
                               int have_curr, const Look *in, unsigned short want[3][256])
{
    Look lk = *in;
    cm_sanitize_look(&lk);

    if (cm_curves_neutral(&lk)) {
        if (memcmp(curr, orig, CM_RAMP_BYTES) == 0) return 0;
        memcpy(want, orig, CM_RAMP_BYTES);
        return 1;
    }

    cm_calc_ramp(&lk, want);
    if (have_curr && memcmp(curr, want, CM_RAMP_BYTES) == 0) return 0;
    return 1;
}

/* ====================== ramps.dat (crash-recovery file) ==================== */
/* Layout (little endian):  u32 count, then count records of
 *     char name[32];  u16 ramp[3][256]
 * parse() validates the whole image before anything is applied to hardware. */

#define CM_RAMP_NAME_LEN     32
#define CM_RAMP_RECORD_BYTES (CM_RAMP_NAME_LEN + (int)CM_RAMP_BYTES)
#define CM_RAMPS_MAX_RECORDS 8

typedef struct CmRampRecord {
    char           name[CM_RAMP_NAME_LEN];
    unsigned short ramp[3][256];
} CmRampRecord;

/* Returns the number of records (>= 1) when the image is well formed, or 0 if
 * anything is wrong: short/odd size, absurd count, unterminated or non-printable
 * device name, non-monotonic or degenerate ramp. */
static inline int cm_parse_ramps_blob(const unsigned char *buf, size_t len,
                                      CmRampRecord *out, int max_out)
{
    if (!buf || !out || max_out <= 0 || len < 4) return 0;

    unsigned long n = (unsigned long)buf[0] | ((unsigned long)buf[1] << 8) |
                      ((unsigned long)buf[2] << 16) | ((unsigned long)buf[3] << 24);
    if (n == 0 || n > (unsigned long)max_out || n > CM_RAMPS_MAX_RECORDS) return 0;
    if (len < 4 + (size_t)n * CM_RAMP_RECORD_BYTES) return 0;

    for (unsigned long r = 0; r < n; r++) {
        const unsigned char *p = buf + 4 + (size_t)r * CM_RAMP_RECORD_BYTES;

        int k = 0;
        while (k < CM_RAMP_NAME_LEN && p[k] != 0) {
            if (p[k] < 0x20 || p[k] > 0x7E) return 0;
            k++;
        }
        if (k == 0 || k == CM_RAMP_NAME_LEN) return 0;           /* empty or not terminated */
        memset(out[r].name, 0, CM_RAMP_NAME_LEN);
        memcpy(out[r].name, p, (size_t)k);

        const unsigned char *w = p + CM_RAMP_NAME_LEN;
        for (int ch = 0; ch < 3; ch++)
            for (int i = 0; i < 256; i++) {
                size_t o = (size_t)(ch * 256 + i) * 2;
                out[r].ramp[ch][i] = (unsigned short)(w[o] | (w[o + 1] << 8));
            }

        if (!cm_ramp_is_valid(out[r].ramp)) return 0;
    }
    return (int)n;
}

/* Serialises records into the ramps.dat layout.  Returns the byte count, or 0
 * if `cap` is too small / arguments are invalid. */
static inline size_t cm_build_ramps_blob(const CmRampRecord *recs, int n,
                                         unsigned char *buf, size_t cap)
{
    size_t need = 4 + (size_t)(n > 0 ? n : 0) * CM_RAMP_RECORD_BYTES;
    if (!recs || !buf || n <= 0 || n > CM_RAMPS_MAX_RECORDS || cap < need) return 0;

    buf[0] = (unsigned char)(n & 0xFF);
    buf[1] = (unsigned char)((n >> 8) & 0xFF);
    buf[2] = (unsigned char)((n >> 16) & 0xFF);
    buf[3] = (unsigned char)((n >> 24) & 0xFF);

    for (int r = 0; r < n; r++) {
        unsigned char *p = buf + 4 + (size_t)r * CM_RAMP_RECORD_BYTES;
        memcpy(p, recs[r].name, CM_RAMP_NAME_LEN);
        unsigned char *w = p + CM_RAMP_NAME_LEN;
        for (int ch = 0; ch < 3; ch++)
            for (int i = 0; i < 256; i++) {
                size_t o = (size_t)(ch * 256 + i) * 2;
                w[o]     = (unsigned char)(recs[r].ramp[ch][i] & 0xFF);
                w[o + 1] = (unsigned char)(recs[r].ramp[ch][i] >> 8);
            }
    }
    return need;
}

#endif /* PLEXUSX_COLOR_MATH_H */
