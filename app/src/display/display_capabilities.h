/* PlexusX — DisplayCapabilities: what THIS monitor + output path can do.
 *
 * HDR is the case where a display-control app must be brutally honest.  The
 * OS tone-mapper owns the CRTC gamma LUT while an output runs a PQ/HLG
 * transfer function, and the DWM color-matrix filter is defined for SDR
 * composition — writing either one while HDR is active fights the operating
 * system instead of improving the picture.  So PlexusX reports four states and
 * never claims more:
 *
 *   HDR DISABLED      SDR output: the full pipeline (matrix + ramps) applies
 *   HDR ENABLED       the output is in an HDR transfer function
 *   HDR LIMITED       the panel is HDR-capable but Windows has no HDR mode on
 *                     (capability reported, no HDR space active) — the SDR
 *                     pipeline still works and the UI says the panel is capable
 *   HDR PASSTHROUGH   an HDR output is active, so PlexusX writes nothing and
 *                     the color engine reports PASSTHROUGH, not ACTIVE
 *
 * Everything here is a pure function of OS-reported values (DXGI color space,
 * luminance, bpc, capability) — no guessing, no faking.
 */
#ifndef PLEXUSX_DISPLAY_CAPABILITIES_H
#define PLEXUSX_DISPLAY_CAPABILITIES_H

#include "../display/display_state.h"

typedef enum PxHdrState {
    PX_HDR_DISABLED = 0,
    PX_HDR_ENABLED,
    PX_HDR_LIMITED,
    PX_HDR_PASSTHROUGH
} PxHdrState;

static inline const char *PxHdr_Name(int s)
{
    switch (s) {
    case PX_HDR_ENABLED:     return "HDR ENABLED";
    case PX_HDR_LIMITED:     return "HDR LIMITED";
    case PX_HDR_PASSTHROUGH: return "HDR PASSTHROUGH";
    default:                 return "HDR DISABLED";
    }
}

/* Tone used by the chips: 0 neutral, 1 ok, 2 warn, 3 bad, 4 info. */
static inline int PxHdr_Tone(int s)
{
    switch (s) {
    case PX_HDR_ENABLED:     return 4;
    case PX_HDR_LIMITED:     return 2;
    case PX_HDR_PASSTHROUGH: return 2;
    default:                 return 0;
    }
}

/* One-line explanation, always actionable. */
static inline const char *PxHdr_Explain(int s)
{
    switch (s) {
    case PX_HDR_ENABLED:
        return "This output is HDR: Windows tone-maps SDR content, so PlexusX color controls are bypassed on it (PASSTHROUGH).";
    case PX_HDR_LIMITED:
        return "The panel reports HDR capability but Windows is running it in SDR. Color controls apply normally; enable HDR in Windows to see HDR content.";
    case PX_HDR_PASSTHROUGH:
        return "HDR output is active and owned by the OS tone-mapper. PlexusX writes nothing to this output.";
    default:
        return "SDR output: the full PlexusX pipeline (DWM matrix + GPU gamma ramps) applies.";
    }
}

/* Derive the state from the monitor's OS-reported facts. */
static inline int PxHdr_StateOf(const MonitorInfo *m)
{
    if (!m) return PX_HDR_DISABLED;
    if (m->hdr_enabled)  return PX_HDR_ENABLED;
    if (m->hdr_capable)  return PX_HDR_LIMITED;
    return PX_HDR_DISABLED;
}

/* Colour-pipeline capability summary for the display card / diagnostics.
 * `linear` = the DWM matrix can reach composited content on this machine,
 * `curves` = the GPU ramp path accepted at least one display. */
typedef struct PxCapSummary {
    int monitor_index;
    int is_primary;
    int hdr_state;
    int hdr_capable;
    int bpc;                 /* 0 = not reported */
    int color_space;         /* PX_CS_UNKNOWN = not reported */
    float min_nits, max_nits, max_ff_nits;
    int linear_path;         /* DWM magnification matrix available */
    int curves_path;         /* GPU gamma ramp available          */
    int exclusive_blocked;   /* a fullscreen surface currently bypasses DWM */
} PxCapSummary;

static inline void PxCap_Fill(PxCapSummary *out, const MonitorInfo *m,
                              int linear_path, int curves_path, int exclusive_blocked)
{
    if (!out) return;
    out->monitor_index = -1;
    out->is_primary = 0;
    out->bpc = 0;
    out->color_space = PX_CS_UNKNOWN;
    out->min_nits = out->max_nits = out->max_ff_nits = 0.0f;
    out->linear_path = linear_path ? 1 : 0;
    out->curves_path = curves_path ? 1 : 0;
    out->exclusive_blocked = exclusive_blocked ? 1 : 0;
    out->hdr_state = PxHdr_StateOf(m);
    out->hdr_capable = (m && m->hdr_capable) ? 1 : 0;
    if (m) {
        out->is_primary = m->is_primary ? 1 : 0;
        out->bpc = m->bpc;
        out->color_space = m->color_space_raw;
        out->min_nits = m->min_nits;
        out->max_nits = m->max_nits;
        out->max_ff_nits = m->max_full_frame_nits;
    }
}

#endif /* PLEXUSX_DISPLAY_CAPABILITIES_H */
