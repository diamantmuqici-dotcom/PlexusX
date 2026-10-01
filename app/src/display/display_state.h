/* PlexusX — DisplayState (header only, platform independent)
 *
 * The ACTUAL runtime state of the displays: what the OS and the display pipeline
 * report right now (GPU, monitor, resolution, refresh, HDR, color space, and the
 * presentation mode of the foreground window).  Nothing in here is ever
 * fabricated: PX_CS_UNKNOWN / 0 means "the OS did not tell us" and the UIs must
 * render "—" rather than an invented value.  The collectors that fill these
 * structs are:
 *
 *   modes.c   EnumDisplaySettings / EnumDisplayDevices + DXGI 1.6 (IDXGIOutput6)
 *             -> MonitorInfo (per-display mode, HDR, bits per channel, color space, nits)
 * winman.c   GetForegroundWindow + window geometry + MonitorFromWindow
 *             -> presentation mode of the window that is actually on screen
 *
 * The mode table (ModeInfo) also lives here so the sort / dedup / pick-best
 * logic is pure and unit-tested (tests/test_all.c) instead of buried in the
 * Win32-only module.
 */
#ifndef PLEXUSX_DISPLAY_STATE_H
#define PLEXUSX_DISPLAY_STATE_H

#include <stddef.h>
#include <string.h>
#include <wchar.h>

/* RECT comes from <windows.h> on the Windows build; the host unit tests get a
 * layout-compatible fallback (four LONGs) so the structs below compile anywhere. */
#if !defined(_WINDEF_) && !defined(PLEXUSX_RECT_DEFINED)
#define PLEXUSX_RECT_DEFINED
typedef struct tagRECT { long left, top, right, bottom; } RECT;
#endif

/* ---------------- Monitor mode table (pure) ---------------- */
typedef struct ModeInfo {
    int w, h, hz;
    int aspect;            /* 0 = 16:9, 1 = 4:3 stretched, 2 = 16:10, 3 = Ultrawide, 4 = Other */
    int native;
    int supported;
} ModeInfo;

typedef struct MonitorInfo {
    wchar_t dev_name[32];   /* e.g. \\.\DISPLAY1 */
    wchar_t friendly[64];   /* e.g. "ASUS ROG PG279QM" or "Generic PnP Monitor" */
    wchar_t adapter[128];   /* e.g. "NVIDIA GeForce RTX 4080" */
    RECT    rc;
    int     is_primary;
    int     current_w;
    int     current_h;
    int     current_hz;
    /* DisplayState fields (collected by modes.c; 0 / PX_CS_UNKNOWN = not reported) */
    int     hdr_enabled;    /* 1 = the output is currently running in an HDR transfer function */
    int     hdr_capable;    /* 1 = HDR capable per OS report (current HDR space or >=300 nit full-frame) */
    int     bpc;            /* bits per channel from DXGI (8, 10, 12, 16); 0 = not reported */
    int     color_space_raw;/* DXGI_COLOR_SPACE_TYPE value, or PX_CS_UNKNOWN */
    float   min_nits;       /* from DXGI_OUTPUT_DESC1; 0 = not reported */
    float   max_nits;
    float   max_full_frame_nits;
} MonitorInfo;

/* ---------------- Color space (raw values == DXGI_COLOR_SPACE_TYPE) --------- */
#define PX_CS_UNKNOWN          (-1)

static inline int px_cs_is_hdr(int raw)
{
    /* HDR transfer families: SMPTE ST 2084 (PQ / HDR10) and ARIB STD-B67 (HLG),
     * in the RGB and YCbCR variants DXGI exposes. */
    switch (raw) {
    case 0x0c:   /* DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020   */
    case 0x0d:   /* DXGI_COLOR_SPACE_YCBCR_STUDIO_G2084_LEFT_P2020 */
    case 0x0e:   /* DXGI_COLOR_SPACE_RGB_STUDIO_G2084_NONE_P2020 */
    case 0x10:   /* DXGI_COLOR_SPACE_YCBCR_STUDIO_G2084_TOPLEFT_P2020 */
    case 0x12:   /* DXGI_COLOR_SPACE_YCBCR_STUDIO_GHLG_TOPLEFT_P2020 */
    case 0x13:   /* DXGI_COLOR_SPACE_YCBCR_FULL_GHLG_TOPLEFT_P2020 */
        return 1;
    default:
        return 0;
    }
}

/* Short human names for diagnostics / UI.  Unknown values render honestly. */
static inline const char *px_cs_name(int raw, char *buf, size_t cap)
{
    const char *s = 0;
    switch (raw) {
    case 0x00: s = "sRGB Gamma 2.2 (Rec.709)"; break;
    case 0x01: s = "Linear / scRGB-like (Rec.709)"; break;
    case 0x02: s = "Studio Gamma (Rec.709)"; break;
    case 0x03: s = "Studio Gamma (BT.2020)"; break;
    case 0x0c: s = "HDR10 PQ (BT.2020)"; break;
    case 0x0d: s = "HDR10 PQ YCbCr (BT.2020)"; break;
    case 0x0e: s = "HDR10 PQ Studio (BT.2020)"; break;
    case 0x10: s = "HDR10 PQ YCbCr Studio (BT.2020)"; break;
    case 0x11: s = "SDR BT.2020 (Gamma 2.2)"; break;
    case 0x12: s = "HLG (BT.2020)"; break;
    case 0x13: s = "HLG Full (BT.2020)"; break;
    case 0x14: s = "Studio Gamma 2.4 (Rec.709)"; break;
    case 0x15: s = "Studio Gamma 2.4 (BT.2020)"; break;
    default: break;
    }
    if (!buf || !cap) return (const char *)buf;
    if (s) {
        size_t i = 0;
        for (; s[i] && i + 1 < cap; i++) buf[i] = s[i];
        buf[i] = 0;
    } else {
        /* "Unknown (0xNN)" — never a made-up name */
        const char *u = "Unknown";
        size_t i = 0;
        for (; u[i] && i + 1 < cap; i++) buf[i] = u[i];
        if (raw >= 0 && i + 6 < cap) {
            static const char hexd[] = "0123456789ABCDEF";
            buf[i++] = ' '; buf[i++] = '0'; buf[i++] = 'x';
            buf[i++] = hexd[(raw >> 4) & 0xF];
            buf[i++] = hexd[raw & 0xF];
        }
        buf[i] = 0;
    }
    return buf;
}

/* ---------------- GPU / driver capability (collected by display_manager.c) - */
enum {
    GPU_VENDOR_UNKNOWN = 0,
    GPU_VENDOR_NVIDIA,
    GPU_VENDOR_AMD,
    GPU_VENDOR_INTEL
};

typedef struct GpuInfo {
    int     vendor;         /* GPU_VENDOR_*                                   */
    wchar_t vendor_name[32];/* "NVIDIA", "AMD", "Intel", "Generic"            */
    wchar_t name[128];      /* e.g. "NVIDIA GeForce RTX 4090"                 */
    wchar_t driver_ver[64]; /* Driver version                                 */
    int     mag_available;  /* Windows Magnification API supported            */
    int     gamma_available;/* GDI SetDeviceGammaRamp supported               */
    int     hdr_detected;   /* at least one output reports an HDR color space */
} GpuInfo;

/* ---------------- Presentation mode of the foreground window ----------------
 * What a *legitimate* user-mode display utility can know, and what follows
 * from it for the color engine:
 *
 *   PX_PRES_WINDOWED / PX_PRES_COMPOSITED_COVER
 *      The content goes through DWM composition.  MagSetFullscreenColorEffect
 *      affects it: desktop AND game, full effect.
 *   PX_PRES_FULLSCREEN_SURFACE
 *      A borderless full-cover surface (DirectX exclusive fullscreen or a
 *      flip-model swapchain).  These present straight to the scanout and
 *      BYPASS the DWM composition filter: the linear matrix cannot reach them,
 *      while the GPU gamma-ramp LUT (scanout side) still does.  No legitimate
 *      user-mode API distinguishes this from a "borderless windowed" game, so
 *      the state is reported as such — never silently claimed as ACTIVE.
 */
typedef enum PxPresMode {
    PX_PRES_NONE = 0,
    PX_PRES_WINDOWED,
    PX_PRES_COMPOSITED_COVER,
    PX_PRES_FULLSCREEN_SURFACE
} PxPresMode;

static inline int px_pres_classify(int has_frame, int covers_monitor)
{
    if (!covers_monitor) return PX_PRES_WINDOWED;
    return has_frame ? PX_PRES_COMPOSITED_COVER : PX_PRES_FULLSCREEN_SURFACE;
}

/* 1 = the DWM color matrix provably applies to this content */
static inline int px_pres_dwm_reachable(int pres_mode)
{
    return pres_mode == PX_PRES_WINDOWED || pres_mode == PX_PRES_COMPOSITED_COVER;
}

/* 1 = the GPU gamma-ramp LUT (scanout side) is still the last stage for this content */
static inline int px_pres_ramp_reaches(int pres_mode)
{
    return pres_mode != PX_PRES_NONE;
}

static inline const char *px_pres_name(int pres_mode)
{
    switch (pres_mode) {
    case PX_PRES_WINDOWED:           return "WINDOWED";
    case PX_PRES_COMPOSITED_COVER:   return "BORDERLESS / MAXIMIZED (DWM)";
    case PX_PRES_FULLSCREEN_SURFACE: return "FULLSCREEN SURFACE (DWM MAY BE BYPASSED)";
    default:                         return "DESKTOP";
    }
}

/* ---------------- Game output state ------------------------------------------
 * Derived ONLY from facts: is a game the foreground window, what presentation
 * mode is it using, is the engine enabled, and does the magnification path
 * exist on this machine.  The UIs render this verbatim; it is never optimistic.
 */
typedef enum PxGameOut {
    PX_GAMEOUT_NONE = 0,      /* no game in the foreground */
    PX_GAMEOUT_ACTIVE,        /* game output receives the full effect (DWM-composited) */
    PX_GAMEOUT_LIMITED,       /* fullscreen surface: tone curves via GPU LUT; linear chroma cannot reach it */
    PX_GAMEOUT_ENGINE_OFF,    /* engine bypassed by the user */
    PX_GAMEOUT_UNAVAILABLE    /* no usable output path reported by the OS */
} PxGameOut;

static inline int px_gameout_compute(int game_active, int pres_mode, int engine_enabled, int mag_ok)
{
    if (!game_active) return PX_GAMEOUT_NONE;
    if (!engine_enabled) return PX_GAMEOUT_ENGINE_OFF;
    if (!mag_ok) return PX_GAMEOUT_UNAVAILABLE;
    return px_pres_dwm_reachable(pres_mode) ? PX_GAMEOUT_ACTIVE : PX_GAMEOUT_LIMITED;
}

static inline const char *px_gameout_name(int out)
{
    switch (out) {
    case PX_GAMEOUT_ACTIVE:      return "ACTIVE";
    case PX_GAMEOUT_LIMITED:     return "LIMITED (CURVES ONLY)";
    case PX_GAMEOUT_ENGINE_OFF:  return "ENGINE OFF";
    case PX_GAMEOUT_UNAVAILABLE: return "UNAVAILABLE";
    default:                     return "NO GAME";
    }
}

/* ---------------- Pure mode-table helpers (used by modes.c, tested on host) -- */

/* Aspect classifier shared with the tests. */
static inline int px_aspect_of(int w, int h)
{
    if (w * 9 == h * 16) return 0;                    /* 16:9 */
    if ((long)w * 3 == (long)h * 4) return 1;         /* 4:3 */
    if (w * 10 == h * 16) return 2;                   /* 16:10 */
    if (w * 9 >= h * 21) return 3;                    /* Ultrawide 21:9 or 32:9 */
    return 4;                                         /* Other */
}

static inline int px_mode_less_sortkey(const void *a, const void *b)
{
    const ModeInfo *x = (const ModeInfo *)a, *y = (const ModeInfo *)b;
    long ax = (long)x->w * x->h, ay = (long)y->w * y->h;
    if (ax != ay) return ax > ay ? -1 : 1;             /* larger area first */
    return x->hz == y->hz ? 0 : (x->hz > y->hz ? -1 : 1); /* higher hz first */
}
#define PX_MODE_CMP(a, b) px_mode_less_sortkey((a), (b))

/* Pick the best entry in a mode table: exact (w,h) match with the highest hz.
 * w == 0 -> any size, just the highest refresh ("Max Hz"); hz_hint > 0 prefers
 * that refresh if present (falls back to the highest).  Returns index or -1. */
static inline int px_modes_pick_best(const ModeInfo *m, int n, int w, int h, int hz_hint)
{
    int best = -1, best_hz = -1;
    for (int i = 0; i < n; i++) {
        if (w > 0 && m[i].w != w) continue;
        if (h > 0 && m[i].h != h) continue;
        if (hz_hint > 0 && m[i].hz == hz_hint) return i;          /* exact hit wins */
        if (m[i].hz > best_hz) { best_hz = m[i].hz; best = i; }
    }
    if (hz_hint > 0 && best >= 0 && m[best].hz != hz_hint) return best;  /* highest available */
    return best;
}

#endif /* PLEXUSX_DISPLAY_STATE_H */
