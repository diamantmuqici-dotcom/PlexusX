/* cx_color.h \u2014 ChromaX colour model & engine maths (platform independent)
 *
 * The display pipeline has two stages:
 *
 *   1. LINEAR MATRIX  \u2014 hue, saturation/vibrance/intensity, temperature/tint,
 *      RGB gains, contrast, brightness.  On Windows this rides the Magnifier
 *      colour-matrix layer (magnification.dll, MagSetFullscreenColorEffect),
 *      the same OS layer Windows Magnifier's colour filter uses.  This stage
 *      is inherently whole-desktop.
 *
 *   2. CURVE LUT      \u2014 gamma, shadows, highlights, black level, white-point
 *      level, dehaze lift.  On Windows this is written per-monitor as a
 *      16-bit gamma ramp (SetDeviceGammaRamp), so it CAN target one monitor.
 *
 * The in-app live preview (CxColor_ApplyBuffer) runs the exact same maths
 * per pixel \u2014 including the chroma-aware vibrance curve and the spatial
 * sharpness/clarity filters, which a linear matrix / per-channel LUT cannot
 * express on the real display and are therefore preview-only there.
 */
#ifndef CX_COLOR_H
#define CX_COLOR_H

#include <stdint.h>

/* ---------- parameter ranges (100/1.0/6500 = neutral unless noted) ---------- */
#define CX_SAT_MIN     0.f
#define CX_SAT_MAX     300.f     /* 100 = untouched */
#define CX_VIB_MIN     0.f
#define CX_VIB_MAX     300.f     /* 100 = off (no curve) */
#define CX_BRI_MIN     0.f
#define CX_BRI_MAX     200.f
#define CX_CON_MIN     0.f
#define CX_CON_MAX     200.f
#define CX_GAM_MIN     0.50f
#define CX_GAM_MAX     2.50f
#define CX_TEM_MIN     3000.f
#define CX_TEM_MAX     10000.f
#define CX_TINT_MIN   -100.f
#define CX_TINT_MAX    100.f
#define CX_CH_MIN      0.f
#define CX_CH_MAX      200.f     /* red / green / blue */
#define CX_SPX_MIN     0.f
#define CX_SPX_MAX     200.f     /* sharpness / clarity (preview only) */
#define CX_INT_MIN     0.f
#define CX_INT_MAX     300.f
#define CX_HUE_MIN    -60.f
#define CX_HUE_MAX     60.f

typedef struct CxLook {
    float sat;        /* 0..300        100 neutral   */
    float vibrance;   /* 0..300        100 off       */
    float brightness; /* 0..200        100 neutral   */
    float contrast;   /* 0..200        100 neutral   */
    float gamma;      /* 0.50..2.50    1.00 neutral  */
    float temperature;/* 3000..10000 K 6500 neutral  */
    float tint;       /* -100..100     0 neutral     */
    float red, green, blue;  /* 0..200   100 neutral */
    float shadows;    /* 0..200        100 neutral   */
    float highlights; /* 0..200        100 neutral   */
    float blacklevel; /* 0..200        100 neutral   */
    float whitepoint; /* 0..200        100 neutral   */
    float sharpness;  /* 0..200        100 none (preview only) */
    float clarity;    /* 0..200        100 none (preview only) */
    float intensity;  /* 0..300        100 none      */
    float hue;        /* -60..60       0 none        */
    float dehaze;     /* 0..200        100 none      */
    int   enabled;
} CxLook;

void  CxLook_Default(CxLook *l);              /* neutral, enabled = 1 */
void  CxLook_Clamp(CxLook *l);                /* force every field into range */
int   CxLook_Equal(const CxLook *a, const CxLook *b);
int   CxLook_IsNeutral(const CxLook *l);      /* display would be untouched */
const char *CxLook_ParamName(int i);          /* for UI, see CxLook_ParamIndex_* */

/* param index table (order matters \u2014 UI iterates over it) */
enum {
    CXLOOK_SAT = 0, CXLOOK_VIBRANCE, CXLOOK_BRIGHTNESS, CXLOOK_CONTRAST,
    CXLOOK_GAMMA, CXLOOK_TEMPERATURE, CXLOOK_TINT, CXLOOK_RED, CXLOOK_GREEN,
    CXLOOK_BLUE, CXLOOK_SHADOWS, CXLOOK_HIGHLIGHTS, CXLOOK_BLACK,
    CXLOOK_WHITE, CXLOOK_SHARP, CXLOOK_CLARITY, CXLOOK_INTENSITY,
    CXLOOK_HUE, CXLOOK_DEHAZE, CXLOOK_NPARAMS
};
float CxLook_Get(const CxLook *l, int param);
void  CxLook_Set(CxLook *l, int param, float v);

/* stage 1 \u2014 linear 4x4 (state = column vector R G B 1; offset in col 3) */
void  CxLook_BuildMatrix(const CxLook *l, float m[4][4]);
/* stage 2 \u2014 16-bit per-channel curve; fallback_gains=1 folds the linear
 * per-channel gains/offsets into the ramp (used when the magnifier layer is
 * unavailable, e.g. some driver configurations) */
void  CxLook_BuildLUT(const CxLook *l, uint16_t ramp[3][256], int fallback_gains);

/* saturation value actually used by the matrix stage */
float CxLook_EffectiveSat(const CxLook *l);

/* full-fidelity preview: in-place RGBA (8 bpc) buffer.
 * Includes per-pixel vibrance, LUT, and \u2014 when non-neutral \u2014 a real
 * unsharp (sharpness) and soft-contrast (clarity) spatial pass. */
void  CxColor_ApplyBuffer(const CxLook *l, uint8_t *rgba, int w, int h);

/* helpers for tests / diagnostics */
float CxColor_VibranceGain(float r, float g, float b, float vibrance_pct);

#endif /* CX_COLOR_H */
