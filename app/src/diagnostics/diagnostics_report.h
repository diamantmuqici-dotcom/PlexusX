/* PlexusX — Diagnostics report: ONE text/JSON builder for every surface.
 *
 * The Diagnostics page, "Copy diagnostics", the exported file and the tray
 * "Save report" action all render through this file, so what the user reads on
 * screen is byte-for-byte what they paste into a bug report.  Every field is
 * read from its owner (engine, display manager, game runtime, log ring) — a
 * value that the OS did not report is printed as "—", never invented.
 *
 * Pure C (no <windows.h>): the host test suite compiles it and asserts the
 * report contains the request/effective/applied triple and the honest status.
 */
#ifndef PLEXUSX_DIAGNOSTICS_REPORT_H
#define PLEXUSX_DIAGNOSTICS_REPORT_H

#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include "diagnostics.h"
#include "../color/color_runtime_state.h"
#include "../display/display_capabilities.h"
#include "../games/game_profile.h"      /* PxW_ToUtf8 (pure) */

typedef struct PxDiagInput {
    const char             *app_name;
    const char             *version;
    const char             *build_date;
    const PxEngineSnapshot *snap;        /* engine: requested/applied/paths/log size */
    const PxEffectiveState *eff;         /* requested → effective → applied          */
    const GpuInfo          *gpu;
    const MonitorInfo      *mon;         /* selected monitor (may be NULL)           */
    int                     monitor_count;
    int                     monitor_index;
    const ModeInfo         *mode;        /* current mode (may be NULL)               */
    int                     hdr_state;   /* PxHdrState                               */
    const PxGameDisplayState *game;
    const PxLog             *log;
    int                     profiles_total;
    int                     presets_total;
    int                     config_corrupt;
    int                     profiles_corrupt;
    int                     presets_corrupt;
} PxDiagInput;

/* wide → UTF-8 for the report (never printed raw) */
static inline const char *PxDg_U8(const wchar_t *w, char *buf, size_t cap)
{
    if (!buf || !cap) return "";
    PxW_ToUtf8(w, buf, cap);
    return buf[0] ? buf : "-";
}

typedef struct PxDiagWriter {
    char  *buf;
    size_t cap;
    size_t len;
} PxDiagWriter;

static inline void PxDg_Init(PxDiagWriter *w, char *buf, size_t cap)
{
    w->buf = buf;
    w->cap = cap;
    w->len = 0;
    if (cap) buf[0] = 0;
}

static inline void PxDg_Raw(PxDiagWriter *w, const char *s)
{
    if (!w->buf || !w->cap || !s) return;
    while (*s && w->len + 1 < w->cap) w->buf[w->len++] = *s++;
    w->buf[w->len] = 0;
}

static inline void PxDg_Fmt(PxDiagWriter *w, const char *fmt, ...)
{
    char tmp[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(tmp, sizeof tmp, fmt, ap);
    va_end(ap);
    tmp[sizeof tmp - 1] = 0;
    PxDg_Raw(w, tmp);
}

/* key: value line, value "—" when unknown */
static inline void PxDg_KV(PxDiagWriter *w, const char *key, const char *val)
{
    PxDg_Fmt(w, "%-22s %s\n", key, (val && val[0]) ? val : "-");
}

static inline void PxDg_KVF(PxDiagWriter *w, const char *key, double v)
{
    char b[64];
    if (v != v) snprintf(b, sizeof b, "-");
    else snprintf(b, sizeof b, "%.2f", v);
    PxDg_KV(w, key, b);
}

static inline void PxDg_KVI(PxDiagWriter *w, const char *key, int v)
{
    char b[32];
    snprintf(b, sizeof b, "%d", v);
    PxDg_KV(w, key, b);
}

static inline void PxDg_KVPct(PxDiagWriter *w, const char *key, double v)
{
    char b[32];
    if (v != v) snprintf(b, sizeof b, "-");
    else snprintf(b, sizeof b, "%.1f%%", v);
    PxDg_KV(w, key, b);
}

/* The human report.  Sections mirror the Diagnostics page top-to-bottom. */
static inline size_t PxDiag_BuildText(const PxDiagInput *in, char *buf, size_t cap)
{
    PxDiagWriter w;
    char tmp[192];
    PxDg_Init(&w, buf, cap);
    if (!in) return 0;

    PxDg_Fmt(&w, "PlexusX diagnostics report\n");
    PxDg_Fmt(&w, "=========================\n");
    PxDg_KV(&w, "Application", in->app_name ? in->app_name : "PlexusX");
    PxDg_KV(&w, "Version", in->version ? in->version : "-");
    PxDg_KV(&w, "Build date", in->build_date ? in->build_date : "-");

    /* ---- COLOR ENGINE (the triple) ---- */
    PxDg_Fmt(&w, "\n[COLOR ENGINE]\n");
    if (in->eff) {
        const PxEffectiveState *e = in->eff;
        PxDg_KV(&w, "Pipeline status", PxStatus_Name(e->status));
        PxDg_KV(&w, "Why", e->reason);
        PxDg_KV(&w, "Engine enabled", e->engine_enabled ? "yes" : "no");
        PxDg_KV(&w, "DWM matrix path", e->linear_deliverable ? "usable" : "not delivered");
        PxDg_KV(&w, "GPU gamma path", e->curves_deliverable ? "usable" : "not delivered");
        PxDg_KVF(&w, "Matrix confirmed", e->linear_live ? 1 : 0);
        PxDg_KVF(&w, "Curves confirmed", e->curves_live ? 1 : 0);
        PxDg_Fmt(&w, "%-22s rev %u%s\n", "Requested state", e->revision,
                 e->diverged ? " (differs from applied)" : " (in sync)");
        PxDg_Fmt(&w, "%-22s sat %.0f%%  vib %.0f%%  hue %.0f deg  bri %.0f%%  con %.0f%%\n",
                 "REQUESTED", e->requested.sat, e->requested.vibrance, e->requested.hue,
                 e->requested.bri, e->requested.con);
        PxDg_Fmt(&w, "%-22s sat %.0f%%  vib %.0f%%  hue %.0f deg  bri %.0f%%  con %.0f%%\n",
                 "EFFECTIVE", e->effective.sat, e->effective.vibrance, e->effective.hue,
                 e->effective.bri, e->effective.con);
        PxDg_Fmt(&w, "%-22s sat %.0f%%  vib %.0f%%  hue %.0f deg  bri %.0f%%  con %.0f%%\n",
                 "APPLIED", e->applied.sat, e->applied.vibrance, e->applied.hue,
                 e->applied.bri, e->applied.con);
        PxDg_KVF(&w, "REQUESTED gamma", e->requested.gamma);
        PxDg_KVF(&w, "EFFECTIVE gamma", e->effective.gamma);
        PxDg_KVF(&w, "APPLIED gamma", e->applied.gamma);
        PxDg_Fmt(&w, "%-22s R %.0f%% G %.0f%% B %.0f%%  temp %.0fK tint %+.0f%%\n", "REQUESTED balance",
                 e->requested.r_gain, e->requested.g_gain, e->requested.b_gain,
                 e->requested.temp, e->requested.tint);
        PxDg_Fmt(&w, "%-22s s %.0f%% h %.0f%% blk %.0f%% wht %.0f%% clarity %.0f%%\n", "REQUESTED tone",
                 e->requested.shadows, e->requested.highlights, e->requested.black_level,
                 e->requested.white_point, e->requested.clarity);
        PxDg_KV(&w, "Last apply outcome",
                in->snap ? PxAS_OutcomeName(in->snap->applied.outcome) : "-");
        if (in->snap) {
            PxDg_KVI(&w, "Ramp writes", in->snap->applied.ramp_writes);
            PxDg_KV(&w, "Matrix skipped", in->snap->applied.matrix_skipped ? "yes (unchanged)" : "no");
            if (in->snap->applied.note[0]) PxDg_KV(&w, "Last note", in->snap->applied.note);
        }
        if (e->applied_ms) { snprintf(tmp, sizeof tmp, "%u ms after start", e->applied_ms); PxDg_KV(&w, "Last apply time", tmp); }
    } else {
        PxDg_KV(&w, "Pipeline status", "unavailable");
    }

    /* ---- DISPLAY ---- */
    PxDg_Fmt(&w, "\n[DISPLAY]\n");
    PxDg_Fmt(&w, "%-22s %d of %d\n", "Monitor index", in->monitor_index + 1, in->monitor_count);
    if (in->mon) {
        char csbuf[64];
        px_cs_name(in->mon->color_space_raw, csbuf, sizeof csbuf);
        PxDg_KV(&w, "Monitor", PxDg_U8(in->mon->friendly, tmp, sizeof tmp));
        PxDg_KV(&w, "Device", PxDg_U8(in->mon->dev_name, tmp, sizeof tmp));
        PxDg_KV(&w, "Adapter", PxDg_U8(in->mon->adapter, tmp, sizeof tmp));
        PxDg_KV(&w, "Primary", in->mon->is_primary ? "yes" : "no");
        PxDg_KVF(&w, "bpc", in->mon->bpc);
        PxDg_KV(&w, "Color space", csbuf);
        if (in->mon->max_nits > 0.0f) {
            snprintf(tmp, sizeof tmp, "min %.1f / max %.1f / full-frame %.1f",
                     in->mon->min_nits, in->mon->max_nits, in->mon->max_full_frame_nits);
            PxDg_KV(&w, "Luminance (nits)", tmp);
        }
    } else {
        PxDg_KV(&w, "Monitor", "-");
    }
    {
        char hb[64];
        snprintf(hb, sizeof hb, "%s", PxHdr_Name(in->hdr_state));
        PxDg_KV(&w, "HDR", hb);
        PxDg_KV(&w, "HDR note", PxHdr_Explain(in->hdr_state));
    }
    if (in->mode) {
        snprintf(tmp, sizeof tmp, "%d x %d @ %d Hz", in->mode->w, in->mode->h, in->mode->hz);
        PxDg_KV(&w, "Resolution", tmp);
        PxDg_KVI(&w, "Refresh rate", in->mode->hz);
        PxDg_KV(&w, "Mode kind", in->mode->native ? "native" : "custom");
    } else {
        PxDg_KV(&w, "Resolution", "-");
        PxDg_KV(&w, "Refresh rate", "-");
    }

    /* ---- GPU ---- */
    PxDg_Fmt(&w, "\n[GPU]\n");
    if (in->gpu) {
        PxDg_KV(&w, "GPU", PxDg_U8(in->gpu->name[0] ? in->gpu->name : in->gpu->vendor_name, tmp, sizeof tmp));
        PxDg_KV(&w, "Driver", PxDg_U8(in->gpu->driver_ver, tmp, sizeof tmp));
        PxDg_KV(&w, "Magnification API", in->gpu->mag_available ? "available" : "unavailable");
        PxDg_KV(&w, "GPU gamma ramp", in->gpu->gamma_available ? "available" : "unavailable");
    } else {
        PxDg_KV(&w, "GPU", "-");
    }

    /* ---- GAMES ---- */
    PxDg_Fmt(&w, "\n[GAME / FOREGROUND]\n");
    if (in->game) {
        const PxGameDisplayState *g = in->game;
        char gexe[PX_GAME_EXE_LEN * 2];
        const char *gexe_s = PxDg_U8(g->exe, gexe, sizeof gexe);
        PxDg_KV(&w, "Foreground process", g->exe[0] ? gexe_s : "(desktop)");
        PxDg_KV(&w, "Detected game", g->detected ? (g->exe[0] ? gexe_s : "yes") : "none");
        snprintf(tmp, sizeof tmp, "%d", g->profile_idx);
        PxDg_KV(&w, "Active profile index", tmp);
        PxDg_KV(&w, "Presentation", px_pres_name(g->presentation));
        PxDg_KV(&w, "Game output", px_gameout_name(g->game_output));
        PxDg_KV(&w, "Profile applied", g->applied ? "yes" : "not confirmed");
    } else {
        PxDg_KV(&w, "Foreground process", "-");
    }
    PxDg_KVI(&w, "Profiles", in->profiles_total);
    PxDg_KVI(&w, "Presets", in->presets_total);

    /* ---- CONFIG ---- */
    PxDg_Fmt(&w, "\n[CONFIGURATION]\n");
    PxDg_KV(&w, "config.ini", in->config_corrupt ? "corrupt (moved aside)" : "ok");
    PxDg_KV(&w, "profiles.ini", in->profiles_corrupt ? "corrupt (moved aside)" : "ok");
    PxDg_KV(&w, "presets.ini", in->presets_corrupt ? "corrupt (moved aside)" : "ok");

    /* ---- LOG ---- */
    PxDg_Fmt(&w, "\n[EVENT LOG]\n");
    if (in->log && in->log->count > 0) {
        int start = (in->log->head - in->log->count + PXLOG_CAP) % PXLOG_CAP;
        for (int i = 0; i < in->log->count; i++) {
            const PxLogEntry *en = &in->log->e[(start + i) % PXLOG_CAP];
            PxDg_Fmt(&w, "%8u ms  %-5s  %s\n", en->ms, en->tag, en->msg);
        }
        if (in->log->dropped) PxDg_Fmt(&w, "(%u older entries dropped)\n", in->log->dropped);
    } else {
        PxDg_Raw(&w, "(no events recorded yet)\n");
    }
    return w.len;
}

/* JSON variant for export / programmatic use. */
static inline size_t PxDiag_BuildJson(const PxDiagInput *in, char *buf, size_t cap)
{
    PxDiagWriter w;
    char csbuf[64] = "-";
    PxDg_Init(&w, buf, cap);
    if (!in) return 0;
    if (in->mon) px_cs_name(in->mon->color_space_raw, csbuf, sizeof csbuf);
    PxDg_Fmt(&w, "{\n  \"app\": \"PlexusX\",\n  \"version\": \"%s\",\n", in->version ? in->version : "-");
    if (in->eff) {
        const PxEffectiveState *e = in->eff;
        PxDg_Fmt(&w, "  \"status\": \"%s\",\n  \"status_reason\": \"%s\",\n",
                 PxStatus_Id(e->status), e->reason);
        PxDg_Fmt(&w, "  \"requested\": {\"sat\":%.1f,\"vibrance\":%.1f,\"hue\":%.1f,\"brightness\":%.1f,\"contrast\":%.1f,\"gamma\":%.2f,\"temperature\":%.0f,\"tint\":%.1f,\"r\":%.1f,\"g\":%.1f,\"b\":%.1f,\"shadows\":%.1f,\"highlights\":%.1f,\"blackLevel\":%.1f,\"whitePoint\":%.1f,\"clarity\":%.1f,\"enabled\":%d},\n",
                 e->requested.sat, e->requested.vibrance, e->requested.hue, e->requested.bri,
                 e->requested.con, e->requested.gamma, e->requested.temp, e->requested.tint,
                 e->requested.r_gain, e->requested.g_gain, e->requested.b_gain,
                 e->requested.shadows, e->requested.highlights, e->requested.black_level,
                 e->requested.white_point, e->requested.clarity, e->requested.enabled);
        PxDg_Fmt(&w, "  \"effective\": {\"sat\":%.1f,\"vibrance\":%.1f,\"brightness\":%.1f,\"contrast\":%.1f,\"gamma\":%.2f,\"linearPath\":%d,\"curvePath\":%d},\n",
                 e->effective.sat, e->effective.vibrance, e->effective.bri, e->effective.con,
                 e->effective.gamma, e->linear_deliverable, e->curves_deliverable);
        PxDg_Fmt(&w, "  \"applied\": {\"sat\":%.1f,\"vibrance\":%.1f,\"brightness\":%.1f,\"contrast\":%.1f,\"gamma\":%.2f,\"revision\":%u,\"matrixOk\":%d,\"rampsOk\":%d,\"outcome\":\"%s\"},\n",
                 e->applied.sat, e->applied.vibrance, e->applied.bri, e->applied.con,
                 e->applied.gamma, e->revision, e->linear_live, e->curves_live,
                 in->snap ? PxAS_OutcomeName(in->snap->applied.outcome) : "-");
    }
    PxDg_Fmt(&w, "  \"hdr\": \"%s\",\n", PxHdr_Name(in->hdr_state));
    if (in->mon) {
        {
            char mname[96], mdev[48];
            PxDg_Fmt(&w, "  \"monitor\": {\"name\":\"%s\",\"device\":\"%s\",\"primary\":%d,\"bpc\":%d,\"colorSpace\":\"%s\",\"maxNits\":%.1f},\n",
                     PxDg_U8(in->mon->friendly, mname, sizeof mname),
                     PxDg_U8(in->mon->dev_name, mdev, sizeof mdev),
                     in->mon->is_primary, in->mon->bpc,
                     csbuf, (double)in->mon->max_full_frame_nits);
        }
    }
    if (in->mode)
        PxDg_Fmt(&w, "  \"mode\": {\"width\":%d,\"height\":%d,\"hz\":%d,\"native\":%d},\n",
                 in->mode->w, in->mode->h, in->mode->hz, in->mode->native);
    if (in->gpu) {
        char gname[192], gdrv[96];
        PxDg_Fmt(&w, "  \"gpu\": {\"name\":\"%s\",\"driver\":\"%s\",\"magAvailable\":%d,\"gammaAvailable\":%d},\n",
                 PxDg_U8(in->gpu->name, gname, sizeof gname),
                 PxDg_U8(in->gpu->driver_ver, gdrv, sizeof gdrv),
                 in->gpu->mag_available, in->gpu->gamma_available);
    }
    if (in->game) {
        char gexe2[PX_GAME_EXE_LEN * 2];
        PxDg_Fmt(&w, "  \"foreground\": {\"exe\":\"%s\",\"detected\":%d,\"profile\":%d,\"presentation\":\"%s\",\"output\":\"%s\"},\n",
                 PxDg_U8(in->game->exe, gexe2, sizeof gexe2), in->game->detected, in->game->profile_idx,
                 px_pres_name(in->game->presentation), px_gameout_name(in->game->game_output));
    }
    PxDg_Fmt(&w, "  \"config\": {\"configCorrupt\":%d,\"profilesCorrupt\":%d,\"presetsCorrupt\":%d},\n",
             in->config_corrupt, in->profiles_corrupt, in->presets_corrupt);
    PxDg_Fmt(&w, "  \"log\": [");
    if (in->log) {
        int start = (in->log->head - in->log->count + PXLOG_CAP) % PXLOG_CAP;
        for (int i = 0; i < in->log->count; i++) {
            const PxLogEntry *en = &in->log->e[(start + i) % PXLOG_CAP];
            char esc[PXLOG_MSG * 2];
            size_t o = 0;
            for (size_t k = 0; en->msg[k] && o + 2 < sizeof esc; k++) {
                char c = en->msg[k];
                if (c == '"' || c == '\\') esc[o++] = '\\';
                if ((unsigned char)c < 0x20) c = ' ';
                esc[o++] = c;
            }
            esc[o] = 0;
            PxDg_Fmt(&w, "%s{\"ms\":%u,\"tag\":\"%s\",\"msg\":\"%s\"}",
                     i ? "," : "", en->ms, en->tag, esc);
        }
    }
    PxDg_Fmt(&w, "]\n}\n");
    return w.len;
}

#endif /* PLEXUSX_DIAGNOSTICS_REPORT_H */
