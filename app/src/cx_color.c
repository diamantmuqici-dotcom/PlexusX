/* cx_color.c \u2014 colour engine maths */
#include "cx_color.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

#define CX_CLAMPF(v, a, b) ((v) < (a) ? (a) : ((v) > (b) ? (b) : (v)))

void CxLook_Default(CxLook *l)
{
    memset(l, 0, sizeof *l);
    l->sat = 100; l->vibrance = 100;
    l->brightness = 100; l->contrast = 100;
    l->gamma = 1.f; l->temperature = 6500; l->tint = 0;
    l->red = 100; l->green = 100; l->blue = 100;
    l->shadows = 100; l->highlights = 100;
    l->blacklevel = 100; l->whitepoint = 100;
    l->sharpness = 100; l->clarity = 100;
    l->intensity = 100; l->hue = 0; l->dehaze = 100;
    l->enabled = 1;
}

void CxLook_Clamp(CxLook *l)
{
    l->sat        = CX_CLAMPF(l->sat, CX_SAT_MIN, CX_SAT_MAX);
    l->vibrance   = CX_CLAMPF(l->vibrance, CX_VIB_MIN, CX_VIB_MAX);
    l->brightness = CX_CLAMPF(l->brightness, CX_BRI_MIN, CX_BRI_MAX);
    l->contrast   = CX_CLAMPF(l->contrast, CX_CON_MIN, CX_CON_MAX);
    l->gamma      = CX_CLAMPF(l->gamma, CX_GAM_MIN, CX_GAM_MAX);
    l->temperature= CX_CLAMPF(l->temperature, CX_TEM_MIN, CX_TEM_MAX);
    l->tint       = CX_CLAMPF(l->tint, CX_TINT_MIN, CX_TINT_MAX);
    l->red        = CX_CLAMPF(l->red, CX_CH_MIN, CX_CH_MAX);
    l->green      = CX_CLAMPF(l->green, CX_CH_MIN, CX_CH_MAX);
    l->blue       = CX_CLAMPF(l->blue, CX_CH_MIN, CX_CH_MAX);
    l->shadows    = CX_CLAMPF(l->shadows, CX_CH_MIN, CX_CH_MAX);
    l->highlights = CX_CLAMPF(l->highlights, CX_CH_MIN, CX_CH_MAX);
    l->blacklevel = CX_CLAMPF(l->blacklevel, CX_CH_MIN, CX_CH_MAX);
    l->whitepoint = CX_CLAMPF(l->whitepoint, CX_CH_MIN, CX_CH_MAX);
    l->sharpness  = CX_CLAMPF(l->sharpness, CX_SPX_MIN, CX_SPX_MAX);
    l->clarity    = CX_CLAMPF(l->clarity, CX_SPX_MIN, CX_SPX_MAX);
    l->intensity  = CX_CLAMPF(l->intensity, CX_INT_MIN, CX_INT_MAX);
    l->hue        = CX_CLAMPF(l->hue, CX_HUE_MIN, CX_HUE_MAX);
    l->dehaze     = CX_CLAMPF(l->dehaze, CX_CH_MIN, CX_CH_MAX);
    if (l->enabled != 0) l->enabled = 1;
}

static int fne(float a, float b) { float d = a - b; return d > 0.001f || d < -0.001f; }

int CxLook_Equal(const CxLook *a, const CxLook *b)
{
    if (a->enabled != b->enabled) return 0;
    return !fne(a->sat, b->sat) && !fne(a->vibrance, b->vibrance) &&
           !fne(a->brightness, b->brightness) && !fne(a->contrast, b->contrast) &&
           !fne(a->gamma, b->gamma) && !fne(a->temperature, b->temperature) &&
           !fne(a->tint, b->tint) && !fne(a->red, b->red) &&
           !fne(a->green, b->green) && !fne(a->blue, b->blue) &&
           !fne(a->shadows, b->shadows) && !fne(a->highlights, b->highlights) &&
           !fne(a->blacklevel, b->blacklevel) && !fne(a->whitepoint, b->whitepoint) &&
           !fne(a->sharpness, b->sharpness) && !fne(a->clarity, b->clarity) &&
           !fne(a->intensity, b->intensity) && !fne(a->hue, b->hue) &&
           !fne(a->dehaze, b->dehaze);
}

int CxLook_IsNeutral(const CxLook *l)
{
    CxLook z; CxLook_Default(&z); z.enabled = l->enabled;
    return CxLook_Equal(l, &z);
}

static const float *param_ptr(const CxLook *l, int i)
{
    switch (i) {
    case CXLOOK_SAT:        return &l->sat;
    case CXLOOK_VIBRANCE:   return &l->vibrance;
    case CXLOOK_BRIGHTNESS: return &l->brightness;
    case CXLOOK_CONTRAST:   return &l->contrast;
    case CXLOOK_GAMMA:      return &l->gamma;
    case CXLOOK_TEMPERATURE:return &l->temperature;
    case CXLOOK_TINT:       return &l->tint;
    case CXLOOK_RED:        return &l->red;
    case CXLOOK_GREEN:      return &l->green;
    case CXLOOK_BLUE:       return &l->blue;
    case CXLOOK_SHADOWS:    return &l->shadows;
    case CXLOOK_HIGHLIGHTS: return &l->highlights;
    case CXLOOK_BLACK:      return &l->blacklevel;
    case CXLOOK_WHITE:      return &l->whitepoint;
    case CXLOOK_SHARP:      return &l->sharpness;
    case CXLOOK_CLARITY:    return &l->clarity;
    case CXLOOK_INTENSITY:  return &l->intensity;
    case CXLOOK_HUE:        return &l->hue;
    case CXLOOK_DEHAZE:     return &l->dehaze;
    }
    return &l->sat;
}
float CxLook_Get(const CxLook *l, int i) { return *param_ptr(l, i); }
void  CxLook_Set(CxLook *l, int i, float v) { *(float *)param_ptr(l, i) = v; }

const char *CxLook_ParamName(int i)
{
    switch (i) {
    case CXLOOK_SAT:        return "sat";
    case CXLOOK_VIBRANCE:   return "vibrance";
    case CXLOOK_BRIGHTNESS: return "brightness";
    case CXLOOK_CONTRAST:   return "contrast";
    case CXLOOK_GAMMA:      return "gamma";
    case CXLOOK_TEMPERATURE:return "temperature";
    case CXLOOK_TINT:       return "tint";
    case CXLOOK_RED:        return "red";
    case CXLOOK_GREEN:      return "green";
    case CXLOOK_BLUE:       return "blue";
    case CXLOOK_SHADOWS:    return "shadows";
    case CXLOOK_HIGHLIGHTS: return "highlights";
    case CXLOOK_BLACK:      return "blacklevel";
    case CXLOOK_WHITE:      return "whitepoint";
    case CXLOOK_SHARP:      return "sharpness";
    case CXLOOK_CLARITY:    return "clarity";
    case CXLOOK_INTENSITY:  return "intensity";
    case CXLOOK_HUE:        return "hue";
    case CXLOOK_DEHAZE:     return "dehaze";
    }
    return "?";
}

/* ------------------------------------------------------------------ */

float CxLook_EffectiveSat(const CxLook *l)
{
    /* base saturation × intensity, plus vibrance as an additive curve.
     * Vibrance's true per-pixel chroma-aware curve lives in the preview;
     * on the display pipeline it is rendered as the equivalent gain. */
    float s = (l->sat / 100.f) * (l->intensity / 100.f);
    if (l->vibrance > 100.f) s += (l->vibrance / 100.f - 1.f) * 0.55f;
    return CX_CLAMPF(s, 0.f, 3.f);
}

/* black-body colour temperature → per-channel gain (Tanner Helland style) */
static void temp_gains(float kelvin, float g[3])
{
    float t = CX_CLAMPF((6500.f - kelvin) / 3500.f, -1.f, 1.f); /* + = warm */
    g[0] = 1.f + 0.34f * t;
    g[1] = 1.f + 0.03f * t;
    g[2] = 1.f - 0.34f * t;
}

static void m4_ident(float m[4][4])
{
    for (int i = 0; i < 4; i++)
        for (int j = 0; j < 4; j++)
            m[i][j] = (i == j) ? 1.f : 0.f;
}

/* o = a * b (state = column vector) */
static void m4_mul(float o[4][4], const float a[4][4], const float b[4][4])
{
    float t[4][4];
    for (int i = 0; i < 4; i++)
        for (int j = 0; j < 4; j++) {
            float s = 0;
            for (int k = 0; k < 4; k++) s += a[i][k] * b[k][j];
            t[i][j] = s;
        }
    memcpy(o, t, sizeof t);
}

static void m4_sat(float m[4][4], float s)
{
    /* r' = L + s*(r - L)  with L = Σ w_j * r_j  →  m[i][j] = δ(i,j)*s + w[j]*(1-s)
     * Weights sit on the COLUMN (input channel); with row weights, neutral
     * grey would shift with saturation (a bug the v1 build carried). */
    const float w[3] = { 0.2126f, 0.7152f, 0.0722f };
    m4_ident(m);
    for (int i = 0; i < 3; i++)
        for (int j = 0; j < 3; j++)
            m[i][j] = (i == j ? 1.f : 0.f) * s + w[j] * (1.f - s);
}

static void m4_hue(float m[4][4], float deg)
{
    float c = cosf(deg * 0.0174532925f), s = sinf(deg * 0.0174532925f);
    m4_ident(m);
    m[0][0] =  0.213f + 0.787f * c - 0.143f * s;
    m[0][1] =  0.715f - 0.715f * c + 0.140f * s;
    m[0][2] =  0.072f - 0.072f * c + 0.283f * s;
    m[1][0] =  0.213f - 0.213f * c + 0.140f * s;
    m[1][1] =  0.715f + 0.285f * c - 0.113f * s;
    m[1][2] =  0.072f - 0.072f * c - 0.283f * s;
    m[2][0] =  0.213f - 0.213f * c - 0.140f * s;
    m[2][1] =  0.715f - 0.715f * c + 0.130f * s;
    m[2][2] =  0.072f + 0.928f * c - 0.245f * s;
}

/* per-channel gain + offset (affine, offset in col 3) */
static void m4_affine(float m[4][4], const float g[3], const float off[3])
{
    m4_ident(m);
    for (int i = 0; i < 3; i++) { m[i][i] = g[i]; m[i][3] = off[i]; }
}

void CxLook_BuildMatrix(const CxLook *l, float m[4][4])
{
    if (!l->enabled || CxLook_IsNeutral(l)) { m4_ident(m); return; }

    /* --- white balance / channel gains --- */
    float tg[3]; temp_gains(l->temperature, tg);
    float tint = l->tint / 100.f;
    float gain[3] = {
        (l->red   / 100.f) * tg[0],
        (l->green / 100.f) * tg[1] * (1.f + 0.18f * tint),
        (l->blue  / 100.f) * tg[2]
    };
    float gA[4][4]; m4_affine(gA, gain, (const float[3]){0, 0, 0});

    /* --- hue --- */
    float gH[4][4]; m4_hue(gH, l->hue);

    /* --- saturation (incl. vibrance/intensity) --- */
    float gS[4][4]; m4_sat(gS, CxLook_EffectiveSat(l));

    /* --- contrast + brightness (+ dehaze contrast kick) --- */
    float dehaze_amt = (l->dehaze - 100.f) / 100.f;
    float cc = (l->contrast / 100.f) + dehaze_amt * 0.22f;
    float gain_c = 0.25f + 0.75f * cc;
    gain_c = CX_CLAMPF(gain_c, 0.1f, 2.2f);
    float b = l->brightness / 100.f;
    float gain_b = 0.35f + 0.65f * b;
    float off_b  = (b - 1.f) * 0.16f;
    /* compose: B ∘ contrast :  v → gain_b*(gain_c*v + 0.5(1-gain_c)) + off_b */
    float G = gain_b * gain_c;
    float off = gain_b * 0.5f * (1.f - gain_c) + off_b;
    float gB[4][4];
    m4_affine(gB, (const float[3]){ G, G, G }, (const float[3]){ off, off, off });

    /* M = B \u00b7 S \u00b7 H \u00b7 A   (affine last = applied first) */
    float t[4][4];
    m4_mul(t, gS, gH);   /* t = S\u00b7H */
    m4_mul(t, t, gA);    /* t = S\u00b7H\u00b7A */
    m4_mul(m, gB, t);    /* M = B\u00b7S\u00b7H\u00b7A */
}

/* ------------------------------------------------------------------ */

/* one LUT curve, x in 0..1 */
static float lut_curve(const CxLook *l, float x)
{
    float y = powf(x < 0.f ? 0.f : (x > 1.f ? 1.f : x), l->gamma);
    float s_amt = (l->shadows - 100.f) / 100.f;      /* >0 lifts shadows  */
    float h_amt = (l->highlights - 100.f) / 100.f;   /* >0 compresses top */
    float d_amt = (l->dehaze - 100.f) / 100.f;
    y += s_amt * y * (1.f - y) * 0.50f;
    y -= h_amt * y * y * (1.f - y) * 0.60f;
    y += d_amt * y * (1.f - y) * 0.10f;
    y += (l->blacklevel - 100.f) / 100.f * 0.12f;
    y += (l->whitepoint - 100.f) / 100.f * 0.15f * (y - y * y);
    return y;
}

void CxLook_BuildLUT(const CxLook *l, uint16_t ramp[3][256], int fallback_gains)
{
    float gain[3] = { 1.f, 1.f, 1.f }, off[3] = { 0.f, 0.f, 0.f };
    if (fallback_gains) {
        /* reproduce CxLook_BuildMatrix per-channel affine without the
         * cross-channel part (which the LUT cannot express) */
        float tg[3]; temp_gains(l->temperature, tg);
        float tint = l->tint / 100.f;
        gain[0] = (l->red / 100.f) * tg[0];
        gain[1] = (l->green / 100.f) * tg[1] * (1.f + 0.18f * tint);
        gain[2] = (l->blue / 100.f) * tg[2];
        float dehaze_amt = (l->dehaze - 100.f) / 100.f;
        float cc = (l->contrast / 100.f) + dehaze_amt * 0.22f;
        float gain_c = CX_CLAMPF(0.25f + 0.75f * cc, 0.1f, 2.2f);
        float b = l->brightness / 100.f;
        float gain_b = 0.35f + 0.65f * b;
        float off_b = (b - 1.f) * 0.16f;
        float G = gain_b * gain_c;
        float o = gain_b * 0.5f * (1.f - gain_c) + off_b;
        for (int i = 0; i < 3; i++) { off[i] = o; gain[i] *= G; }
        /* NOTE: saturation is cross-channel and a per-channel LUT cannot
         * express it; in fallback mode saturation is simply unavailable
         * (the UI says so) rather than faked. */
    }

    int neutral = !l->enabled || CxLook_IsNeutral(l);
    for (int c = 0; c < 3; c++)
        for (int i = 0; i < 256; i++) {
            float x = i / 255.f;
            float y;
            if (neutral) {
                y = x;
            } else {
                y = lut_curve(l, x);
                if (fallback_gains) { y = y * gain[c] + off[c]; }
                y = CX_CLAMPF(y, 0.f, 1.f);
            }
            ramp[c][i] = (uint16_t)(y * 65535.f + 0.5f);
        }
}

/* ------------------------------------------------------------------ */

float CxColor_VibranceGain(float r, float g, float b, float vibrance_pct)
{
    if (vibrance_pct <= 100.001f) return 1.f;
    float mx = r > g ? (r > b ? r : b) : (g > b ? g : b);
    float mn = r < g ? (r < b ? r : b) : (g < b ? g : b);
    float chroma = CX_CLAMPF(mx - mn, 0.f, 1.f);
    float v = vibrance_pct / 100.f;
    /* already-saturated pixels get boosted less than washed-out pixels */
    return 1.f + (v - 1.f) * (1.f - 0.55f * chroma);
}

static void apply_3x3_kernel(uint8_t *rgba, int w, int h, const float k9[9],
                             float amt, int channels)
{
    uint8_t *tmp = (uint8_t *)malloc((size_t)w * h * channels);
    if (!tmp) return;
    memcpy(tmp, rgba, (size_t)w * h * channels);
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++)
            for (int c = 0; c < channels; c++) {
                float s = 0; int k = 0;
                for (int dy = -1; dy <= 1; dy++)
                    for (int dx = -1; dx <= 1; dx++) {
                        int xx = x + dx, yy = y + dy;
                        if (xx < 0) xx = 0;
                        if (xx >= w) xx = w - 1;
                        if (yy < 0) yy = 0;
                        if (yy >= h) yy = h - 1;
                        s += ((float)tmp[((size_t)yy * w + xx) * channels + c]) * k9[k++];
                    }
                float cur = rgba[((size_t)y * w + x) * channels + c];
                float v = cur + amt * (cur - s);
                if (v < 0) v = 0;
                if (v > 255) v = 255;
                rgba[((size_t)y * w + x) * channels + c] = (uint8_t)(v + 0.5f);
            }
    free(tmp);
}

void CxColor_ApplyBuffer(const CxLook *l, uint8_t *rgba, int w, int h)
{
    if (!l || !rgba || w <= 0 || h <= 0) return;
    if (!l->enabled || CxLook_IsNeutral(l)) return;

    float m[4][4];      CxLook_BuildMatrix(l, m);
    uint16_t ramp[3][256]; CxLook_BuildLUT(l, ramp, 0);

    const uint8_t *lut8[3];
    static uint8_t l8[3][256];
    for (int c = 0; c < 3; c++) { lut8[c] = l8[c]; for (int i = 0; i < 256; i++) l8[c][i] = ramp[c][i] >> 8; }
    (void)lut8;

    float vib = l->vibrance;
    for (int i = 0; i < w * h; i++) {
        float r = rgba[i * 4] / 255.f, g = rgba[i * 4 + 1] / 255.f, b = rgba[i * 4 + 2] / 255.f;
        float r1 = m[0][0] * r + m[0][1] * g + m[0][2] * b + m[0][3];
        float g1 = m[1][0] * r + m[1][1] * g + m[1][2] * b + m[1][3];
        float b1 = m[2][0] * r + m[2][1] * g + m[2][2] * b + m[2][3];
        if (vib > 100.001f) {
            float vf = CxColor_VibranceGain(r1, g1, b1, vib);
            float lum = 0.2126f * r1 + 0.7152f * g1 + 0.0722f * b1;
            r1 = lum + (r1 - lum) * vf;
            g1 = lum + (g1 - lum) * vf;
            b1 = lum + (b1 - lum) * vf;
        }
        int ri = (int)(CX_CLAMPF(r1, 0.f, 1.f) * 255.f + 0.5f);
        int gi = (int)(CX_CLAMPF(g1, 0.f, 1.f) * 255.f + 0.5f);
        int bi = (int)(CX_CLAMPF(b1, 0.f, 1.f) * 255.f + 0.5f);
        if (ri < 0) ri = 0;
        if (ri > 255) ri = 255;
        if (gi < 0) gi = 0;
        if (gi > 255) gi = 255;
        if (bi < 0) bi = 0;
        if (bi > 255) bi = 255;
        rgba[i * 4]     = l8[0][ri];
        rgba[i * 4 + 1] = l8[1][gi];
        rgba[i * 4 + 2] = l8[2][bi];
    }

    /* spatial passes \u2014 preview only (a display pipeline cannot do these) */
    float sharp_amt = (l->sharpness - 100.f) / 100.f;
    float clar_amt  = (l->clarity - 100.f) / 100.f;
    if (sharp_amt > 0.01f) {
        static const float k[9] = { 0, -1, 0, -1, 5, -1, 0, -1, 0 };
        apply_3x3_kernel(rgba, w, h, k, sharp_amt * 0.6f, 3);
    } else if (sharp_amt < -0.01f) {
        static const float k[9] = { 1, 1, 1, 1, 1, 1, 1, 1, 1 };
        apply_3x3_kernel(rgba, w, h, k, -sharp_amt * 0.5f, 3);
    }
    if (clar_amt > 0.01f) {
        /* soft local contrast: blur by 9-box, re-add */
        static const float k[9] = { 1, 1, 1, 1, 1, 1, 1, 1, 1 };
        apply_3x3_kernel(rgba, w, h, k, clar_amt * 0.45f, 3);
    }
}
