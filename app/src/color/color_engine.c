/* PlexusX — ColorEngine implementation (see color_engine.h for the contract).
 *
 * Formerly engine.c.  The hardware half now lives in color_pipeline.c; what
 * remains here is the orchestration the two new state layers made explicit:
 * ColorState (requested, user-authoritative) → PxTransformPlan (single color
 * math) → pipeline result → AppliedColorState (hardware-confirmed).
 */
#include "common.h"
#include "color_engine.h"
#include <stdarg.h>
#include "color_transform.h"
#include "color_math.h"
#include "color_runtime_state.h"
#include "../diagnostics/diagnostics.h"

static int            g_eng_ready = 0;
static ColorState     g_cs;                 /* THE authoritative requested state */
static AppliedColorState g_applied;
static const wchar_t *g_invalidate_why = L"startup";
static PxLog          g_log;
static PxEffectiveState g_effective;      /* REQUESTED -> EFFECTIVE -> APPLIED view */
static int            g_apply_pending = 0;
static unsigned long long g_last_log_key = ~0ull;   /* dedupe: same outcome+revision logs once */

static unsigned now_ms(void) { return GetTickCount(); }

void Eng_Log(const char *tag, const char *fmt, ...)
{
    char buf[PXLOG_MSG];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    buf[sizeof buf - 1] = 0;
    PxLog_Add(&g_log, now_ms(), tag, buf);
}

const PxLog *Eng_LogRing(void) { return &g_log; }

/* ---------------- lifecycle ---------------- */
void Eng_SetPaths(const wchar_t *ramps, const wchar_t *dirty, int was_dirty)
{
    PxPipe_SetPaths(ramps, dirty, was_dirty);
}

void Eng_Init(void)
{
    PxCS_Init(&g_cs);
    PxAS_Init(&g_applied);
    PxLog_Init(&g_log);
    g_last_log_key = ~0ull;      /* dedupe must not carry across engine sessions */
    if (!PxPipe_Init()) { /* still usable for the halves that do exist */ }
    g_eng_ready = 1;
    /* DisplayState records WHICH hardware paths are live (single source: the pipeline). */
    Dm_ReportOutputs(PxPipe_MagAvailable(), PxPipe_RampDisplays());
    Eng_Log("eng", "init (mag=%s, %d ramp display(s))",
            PxPipe_MagAvailable() ? "yes" : "NO", PxPipe_RampDisplays());
}

void Eng_Shutdown(void)
{
    Eng_Log("eng", "shutdown: restoring desktop");
    PxPipe_Shutdown();
    g_eng_ready = 0;
}

/* ---------------- state ---------------- */
const ColorState *Eng_State(void) { return &g_cs; }
const Look *Eng_GetRequested(void) { return &g_cs.requested; }
const AppliedColorState *Eng_Applied(void) { return &g_applied; }
const Look *Eng_GetApplied(void) { return g_applied.have ? &g_applied.look : NULL; }

int Eng_RequestedMatchesApplied(void)
{
    return g_applied.have &&
           g_applied.revision == g_cs.revision &&
           g_applied.outcome == PX_APPLY_FULL &&
           cm_looks_equal(&g_cs.requested, &g_applied.look);
}

void Eng_SetLook(const Look *lk) { PxCS_SetLook(&g_cs, lk); }

/* ---------------- requested → effective → applied ---------------- */
void Eng_SetApplyPending(int pending) { g_apply_pending = pending ? 1 : 0; }

/* Gather the facts the EFFECTIVE derivation needs.  Every field is read from
 * its owner (pipeline, display manager, game runtime) — nothing is assumed. */
PxOutputFacts Eng_OutputFacts(void)
{
    PxOutputFacts f;
    PxFacts_Init(&f);
    f.mag_available   = PxPipe_MagAvailable();
    f.ramps_available = PxPipe_RampDisplays() > 0;
    f.displays_total  = Modes_MonitorCount();
    f.hdr_active      = Dm_HdrAny();
    {
        MonitorInfo *mi = Modes_GetMonitor(Modes_CurrentMonitorIndex());
        f.hdr_capable = mi ? (mi->hdr_capable ? 1 : 0) : 0;
    }
    {
        const PxGameDisplayState *gs = Prof_GameState();
        if (gs) {
            f.presentation = gs->presentation;
            f.game_active  = (gs->detected && gs->active) ? 1 : 0;
        }
    }
    f.engine_enabled = g_cs.requested.enabled;
    f.apply_pending  = g_apply_pending;
    return f;
}

const PxEffectiveState *Eng_Effective(void)
{
    PxOutputFacts f = Eng_OutputFacts();
    PxEff_Compute(&g_cs.requested, &g_applied, &f, &g_effective);
    return &g_effective;
}

void Eng_SetMode(PxColorMode mode, int game_idx, int sub_idx)
{
    if (g_cs.mode == mode && g_cs.game_index == game_idx && g_cs.game_sub == sub_idx) return;
    g_cs.mode = mode;
    g_cs.game_index = game_idx;
    g_cs.game_sub = sub_idx;
    g_cs.revision++;
}

static void eng_apply_done(void);

/* ---------------- apply (the core guarantee lives here) ---------------- */
void Eng_Apply(const Look *lk)
{
    if (!g_eng_ready) return;
    if (lk) PxCS_SetLook(&g_cs, lk);

    PxTransformPlan plan;
    PxPlan_Compute(&g_cs.requested, &plan);

    PxPipelineResult res;
    PxPipe_Run(&plan, &res);

    /* Record what the HARDWARE confirmed.  On failure the requested look is
     * untouched — AppliedColorState simply keeps the last FULL look with the
     * revision it reflects, so RequestedMatchesApplied() reports the truth. */
    /* "the API returned TRUE" is not enough: when MagGetFullscreenColorEffect
     * proves the effect did NOT stick (the classic ALT+TAB drop), the honest
     * outcome is PARTIAL, not FULL. */
    int matrix_stuck = res.matrix_ok && (!res.matrix_verified || res.matrix_held);
    const char *note = 0;
    if (!res.ramps_ok || !res.matrix_ok)      note = "hardware rejected part of the pipeline";
    else if (!matrix_stuck)                   note = "readback mismatch: DWM dropped the effect";
    else if (!res.matrix_supported)           note = "no magnification path on this machine";
    eng_apply_done();
    PxAS_Record(&g_applied, &plan.sanitized, g_cs.revision,
                matrix_stuck, res.matrix_verified && res.matrix_held, res.ramps_ok,
                res.ramps_called, !res.matrix_sent, now_ms(), note);

    /* log transitions only: outcome flip or a new revision, never per drag tick */
    unsigned long long key = ((unsigned long long)g_cs.revision << 3) | (unsigned)g_applied.outcome;
    if (key != g_last_log_key) {
        g_last_log_key = key;
        Eng_Log("eng", "apply rev=%u outcome=%s matrix=%s%s ramps=%s(%d)",
                g_cs.revision, PxAS_OutcomeName(g_applied.outcome),
                res.matrix_ok ? (res.matrix_sent ? "sent" : "cached") : "FAIL",
                res.matrix_verified ? (res.matrix_held ? "/held" : "/DROPPED") : "",
                res.ramps_ok ? "ok" : "FAIL", res.ramps_called);
    }
}

void Eng_ApplyNow(void) { Eng_Apply(NULL); }

/* An apply just completed: the "in flight" marker is cleared so the effective
 * state reflects the hardware result, not the edit that triggered it. */
static void eng_apply_done(void) { g_apply_pending = 0; }

/* ---------------- invalidation / reassertion ---------------- */

/* Forget cached GPU/DWM state.  The user's requested look is NEVER overwritten:
 * a focus loss, exclusive-fullscreen game, or device reset only invalidates
 * *applied* hardware state, which Eng_Reassert() / Eng_Apply() then restores. */
void Eng_Invalidate(const wchar_t *reason)
{
    if (reason && reason[0]) g_invalidate_why = reason;
    Eng_Resync();
}

void Eng_Resync(void)
{
    PxPipe_Resync();
}

void Eng_Reassert(void)
{
    if (!g_eng_ready) return;
    Eng_Apply(NULL);
}

const wchar_t *Eng_LastInvalidateReason(void) { return g_invalidate_why; }

/* ---------------- targeting + recovery ---------------- */
int Eng_Available(void) { return PxPipe_MagAvailable(); }
void Eng_SetTargetMonitor(int idx) { PxPipe_SetTargetDisplay(idx); }
int  Eng_GetTargetMonitor(void)    { return PxPipe_GetTargetDisplay(); }
void Eng_BackupCurrentState(void)  { PxPipe_BackupOriginals(); Eng_Log("gpu", "backup of original ramps written"); }
int  Eng_RestoreLastGood(void)     { int n = PxPipe_RestoreOriginals(); Eng_Log("gpu", "restore from ramps.dat: %d display(s)", n); return n; }

void Eng_Reset(void)
{
    PxPipe_ForceReset();
    g_applied.have = 0;
    g_applied.outcome = PX_APPLY_NOT_YET;
    PxAS_Init(&g_applied);
    g_apply_pending = 0;
    Eng_Log("eng", "forced reset (identity matrix + original ramps)");
}

/* ---------------- pure-math wrappers (shared kernel, no duplication) ------ */
void Eng_KelvinToRgb(float k, float *r, float *g, float *b)
{
    cm_kelvin_to_rgb(k, r, g, b);
}

void Eng_CalculateGammaRamp(const Look *lk, WORD ramp[3][256])
{
    cm_calc_ramp(lk, ramp);

    /* Last line of defence at the hardware boundary: WDDM needs non-decreasing ramps
     * (a toe lift on top of a steep gamma can dip), whatever produced the data.
     * Same clamp as cm_ramp_make_monotonic(), kept explicit here on purpose. */
    for (int ch = 0; ch < 3; ch++) {
        for (int i = 1; i < 256; i++) {
            if (ramp[ch][i] < ramp[ch][i - 1]) ramp[ch][i] = ramp[ch][i - 1];
        }
    }
}

/* ---------------- diagnostics snapshot ---------------- */
void Eng_FillSnapshot(PxEngineSnapshot *s)
{
    if (!s) return;
    s->engine_ready = g_eng_ready;
    s->engine_enabled = g_cs.requested.enabled;
    s->requested_revision = g_cs.revision;
    s->requested = g_cs.requested;
    s->applied = g_applied;
    s->mag_available = PxPipe_MagAvailable();
    s->ramp_paths = PxPipe_RampDisplays();
    s->displays_total = PxPipe_RampDisplays();
    s->target_display = PxPipe_GetTargetDisplay();
    s->desktop_output = g_applied.have && g_applied.matrix_ok &&
                            (g_applied.matrix_verified || g_applied.matrix_skipped);
    s->log_count   = (unsigned)g_log.count;
    s->log_dropped = g_log.dropped;
}
