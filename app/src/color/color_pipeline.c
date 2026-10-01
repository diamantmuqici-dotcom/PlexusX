/* PlexusX — ColorPipeline implementation: Windows Magnification matrix + GPU
 * gamma ramps + crash recovery.  This is the ONLY module allowed to talk to
 * MagSetFullscreenColorEffect / SetDeviceGammaRamp.
 *
 * Anti-cheat safe: legit OS display APIs only, zero game process touching.
 * The DWM 5x5 matrix affects DWM-COMPOSITED content (desktop, windowed and
 * borderless games).  DirectX exclusive-fullscreen / flip-model surfaces
 * bypass DWM composition — the matrix provably cannot reach them through any
 * legitimate user-mode API; the tone-curve half (the scanout LUT) still does.
 * The color engine reports that split honestly (see PxGameDisplayState).
 *
 * Pipeline split (see color/color_math.h for the shared math kernel):
 *   - DWM 5x5 matrix : every LINEAR adjustment (hue, saturation, vibrance,
 *                      temperature, tint, RGB gain, brightness, contrast,
 *                      black level, white point).
 *   - GPU gamma ramp : only NON-LINEAR curves (gamma, shadows toe, highlights
 *                      shoulder, clarity).  SetDeviceGammaRamp is never called
 *                      for neutral curves, nor when the ramp is unchanged.
 */
#include "common.h"
#include "color_pipeline.h"
#include "color_math.h"

/* Magnification API.  MagColorEffect (color_math.h) is float transform[5][5],
 * exactly the 100-byte Win32 MAGCOLOREFFECT, row-vector convention [R G B A 1]*M:
 * translation in row 4 (transform[4][0..2]), column 4 strictly [0,0,0,0,1].
 * MagGetFullscreenColorEffect exists on the same DLL and lets us VERIFY that
 * the DWM still holds what we sent (ALT+TAB / GPU resets drop it). */
typedef BOOL (WINAPI *fn_MagInitialize)(void);
typedef BOOL (WINAPI *fn_MagUninitialize)(void);
typedef BOOL (WINAPI *fn_MagSetFullscreenColorEffect)(MagColorEffect *);
typedef BOOL (WINAPI *fn_MagGetFullscreenColorEffect)(MagColorEffect *);

static fn_MagInitialize               p_MagInit;
static fn_MagUninitialize             p_MagUninit;
static fn_MagSetFullscreenColorEffect p_MagSetFx;
static fn_MagGetFullscreenColorEffect p_MagGetFx;
static int            g_mag_ok = 0;
static int            g_mag_readback = 0;      /* p_MagGetFx resolved */
static MagColorEffect g_fx_curr;               /* last effect DWM accepted */
static int            g_fx_known = 0;          /* g_fx_curr is trustworthy (cleared by PxPipe_Resync) */

#define MAX_DISP 8
typedef struct DispDC {
    HDC     dc;
    char    name[32];
    wchar_t wname[32];
    WORD    orig[3][256];   /* ramp the display had before PlexusX touched it (validated / identity fallback) */
    WORD    curr[3][256];   /* ramp PlexusX last programmed == what the hardware holds (== orig until first write) */
    int     have_orig;
    int     have_curr;      /* curr is trustworthy (cleared by PxPipe_Resync after external resets) */
} DispDC;

static DispDC  g_disp[MAX_DISP];
static int     g_ndisp = 0;
static int     g_target_disp = -1; /* -1 = all displays */

static wchar_t g_ramps_path[MAX_PATH];
static wchar_t g_dirty_path[MAX_PATH];
static int     g_dirty_start = 0;
static int     g_dirty_marked = 0;     /* dirty.flg currently exists: don't hit the disk on every slider tick */
static int     g_orig_trusted = 0;    /* DispDC.orig really is the pre-PlexusX ramp.  FALSE between capturing the
                                       * ramps and finishing crash recovery on a dirty start: what the GPU holds
                                       * then may still be the previous session's modified ramp. */

/* ---------------- Crash Recovery & Dirty Flag File ---------------- */
void PxPipe_SetPaths(const wchar_t *ramps, const wchar_t *dirty, int was_dirty)
{
    lstrcpynW(g_ramps_path, ramps, MAX_PATH);
    lstrcpynW(g_dirty_path, dirty, MAX_PATH);
    g_dirty_start = was_dirty;
    g_dirty_marked = was_dirty ? 1 : 0;
}

static void mark_dirty(void)
{
    if (g_dirty_marked || !g_dirty_path[0]) return;
    HANDLE h = CreateFileW(g_dirty_path, GENERIC_WRITE, 0, NULL,
                           CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h != INVALID_HANDLE_VALUE) {
        CloseHandle(h);
        g_dirty_marked = 1;
    }
}

static void clear_dirty(void)
{
    if (!g_dirty_marked) return;            /* no flag file: nothing to delete (runs on every bypassed apply) */
    if (g_dirty_path[0]) DeleteFileW(g_dirty_path);
    g_dirty_marked = 0;
}

/* The single place that talks to SetDeviceGammaRamp.  `curr` only changes when
 * the driver accepted the ramp. */
static int write_ramp(DispDC *d, WORD ramp[3][256])
{
    if (!d->dc) return 0;
    if (!SetDeviceGammaRamp(d->dc, ramp)) return 0;
    if ((void *)ramp != (void *)d->curr) memcpy(d->curr, ramp, sizeof d->curr);
    d->have_curr = 1;
    return 1;
}

/* Persist the ORIGINAL ramps.  Written to a temp file and renamed into place so a
 * crash mid-write can never leave a truncated ramps.dat behind. */
static void save_ramps_file(void)
{
    if (!g_ramps_path[0] || !g_ndisp) return;

    CmRampRecord recs[MAX_DISP];
    int n = 0;
    for (int i = 0; i < g_ndisp && n < MAX_DISP; i++) {
        if (!g_disp[i].have_orig) continue;
        memset(&recs[n], 0, sizeof recs[n]);
        lstrcpynA(recs[n].name, g_disp[i].name, CM_RAMP_NAME_LEN);
        memcpy(recs[n].ramp, g_disp[i].orig, sizeof recs[n].ramp);
        n++;
    }

    unsigned char blob[4 + MAX_DISP * CM_RAMP_RECORD_BYTES];
    size_t len = cm_build_ramps_blob(recs, n, blob, sizeof blob);
    if (!len) return;

    wchar_t tmp[MAX_PATH + 8];
    wsprintfW(tmp, L"%s.tmp", g_ramps_path);
    HANDLE h = CreateFileW(tmp, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return;

    DWORD wr = 0;
    BOOL ok = WriteFile(h, blob, (DWORD)len, &wr, NULL) && wr == (DWORD)len && FlushFileBuffers(h);
    CloseHandle(h);
    if (!ok || !MoveFileExW(tmp, g_ramps_path, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
        DeleteFileW(tmp);
}

/* Restores the ramps saved by a previous clean start.  The whole file is parsed and
 * validated (size, device names, monotonic + non-degenerate ramps) BEFORE anything is
 * programmed into hardware.  Returns the number of displays restored; 0 means the
 * file is missing or corrupted and the caller must fall back to an identity ramp. */
static int load_ramps_file(void)
{
    if (!g_ramps_path[0]) return 0;

    HANDLE h = CreateFileW(g_ramps_path, GENERIC_READ, FILE_SHARE_READ, NULL,
                           OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return 0;

    static unsigned char blob[4 + MAX_DISP * CM_RAMP_RECORD_BYTES + 1];
    DWORD rd = 0;
    BOOL read_ok = ReadFile(h, blob, sizeof blob, &rd, NULL);
    CloseHandle(h);
    if (!read_ok) return 0;

    CmRampRecord recs[MAX_DISP];
    int n = cm_parse_ramps_blob(blob, rd, recs, MAX_DISP);
    if (n <= 0) return 0;                          /* corrupted: never apply it */

    int applied = 0;
    for (int r = 0; r < n; r++) {
        for (int d = 0; d < g_ndisp; d++) {
            if (lstrcmpA(g_disp[d].name, recs[r].name) != 0) continue;
            memcpy(g_disp[d].orig, recs[r].ramp, sizeof g_disp[d].orig);
            g_disp[d].have_orig = 1;
            if (write_ramp(&g_disp[d], g_disp[d].orig)) applied++;
        }
    }
    return applied;
}

/* ---------------- Reset / Restore ---------------- */

/* Puts the desktop back to the original state: identity colour matrix and the
 * original gamma ramp of every display.
 *   force = 1  talk to the hardware unconditionally (emergency / crash / explicit reset)
 *   force = 0  only touch what PlexusX actually changed (engine bypassed, shutdown)
 * The dirty flag is cleared only when every restore succeeded, so a failed restore
 * is still retried from ramps.dat on the next start.  While the originals are not yet
 * trusted (crash recovery pending) no ramp is written and the flag is kept. */
static void reset_all(int force)
{
    int ok = 1;

    if (g_mag_ok && p_MagSetFx) {
        MagColorEffect id;
        cm_identity(&id);
        if (force || !g_fx_known || memcmp(&g_fx_curr, &id, sizeof id) != 0) {
            MagColorEffect tmp = id;
            if (p_MagSetFx(&tmp)) {
                g_fx_curr = id;
                g_fx_known = 1;
            } else {
                ok = 0;
            }
        }
    }

    for (int d = 0; d < g_ndisp; d++) {
        DispDC *dd = &g_disp[d];
        if (!dd->have_orig) continue;
        if (!g_orig_trusted) {          /* never "restore" a capture that may be polluted; retry next start */
            ok = 0;
            continue;
        }
        if (force || memcmp(dd->curr, dd->orig, sizeof dd->curr) != 0) {
            if (!write_ramp(dd, dd->orig)) ok = 0;
        }
    }

    /* Only a pass that really covered displays may declare the desktop clean */
    if (ok && g_ndisp > 0) clear_dirty();
}

/* ---------------- Init / Shutdown ---------------- */

int PxPipe_Init(void)
{
    HMODULE h = LoadLibraryW(L"magnification.dll");
    if (h) {
        p_MagInit = (fn_MagInitialize)GetProcAddress(h, "MagInitialize");
        p_MagUninit = (fn_MagUninitialize)GetProcAddress(h, "MagUninitialize");
        p_MagSetFx = (fn_MagSetFullscreenColorEffect)GetProcAddress(h, "MagSetFullscreenColorEffect");
        p_MagGetFx = (fn_MagGetFullscreenColorEffect)GetProcAddress(h, "MagGetFullscreenColorEffect");
        g_mag_readback = p_MagGetFx ? 1 : 0;
        g_mag_ok = p_MagInit && p_MagSetFx && p_MagInit();
    }
    cm_identity(&g_fx_curr);
    g_fx_known = 0;
    g_orig_trusted = g_dirty_start ? 0 : 1;   /* on a dirty start the GPU may still hold the crashed session's ramp */

    /* Enumerate display DCs & store original gamma ramps */
    g_ndisp = 0;
    for (int i = 1; i <= MAX_DISP; i++) {
        char nm[32];
        wsprintfA(nm, "\\\\.\\DISPLAY%d", i);
        HDC dc = CreateDCA(nm, NULL, NULL, NULL);
        if (!dc) continue;
        WORD hw[3][256];
        if (!GetDeviceGammaRamp(dc, hw)) {
            DeleteDC(dc);
            continue;
        }

        DispDC *d = &g_disp[g_ndisp];
        memset(d, 0, sizeof *d);
        d->dc = dc;
        lstrcpynA(d->name, nm, 32);
        MultiByteToWideChar(CP_ACP, 0, nm, -1, d->wname, 32);

        /* A real calibration may carry a few LSBs of dither: repair such dips rather than discard the
         * user's LUT.  Only genuinely degenerate data (all-zero, constant, ...) is replaced by identity. */
        WORD fixed[3][256];
        memcpy(fixed, hw, sizeof fixed);
        cm_ramp_make_monotonic(fixed);
        int garbage = !cm_ramp_is_valid(fixed);
        if (garbage) cm_identity_ramp(d->orig);  /* e.g. a black ramp left behind by an old crash */
        else memcpy(d->orig, fixed, sizeof fixed);
        /* What the hardware holds: only a garbage ramp differs from orig, and the clean-start fix
         * below overwrites it with identity.  (A merely dithered LUT is left alone.) */
        memcpy(d->curr, garbage ? hw : d->orig, sizeof d->curr);
        d->have_orig = 1;
        d->have_curr = 1;
        g_ndisp++;
    }

    if (g_dirty_start) {
        /* The previous session died while it had modified the ramps.  Restore the
         * ramps it saved at its clean start — validated first. */
        if (!load_ramps_file()) {
            /* ramps.dat missing or corrupted: fall back to a clean linear identity ramp */
            for (int i = 0; i < g_ndisp; i++) {
                cm_identity_ramp(g_disp[i].orig);
                g_disp[i].have_orig = 1;
                write_ramp(&g_disp[i], g_disp[i].orig);
            }
        }
        /* From here on `orig` is the true original ramp of every display.  Retry anything that is
         * still off it; the dirty flag is only cleared once every display really is restored. */
        g_orig_trusted = 1;
        reset_all(0);
    } else {
        /* Clean start: if the hardware holds an unusable ramp, fix it before it is saved as "original" */
        for (int i = 0; i < g_ndisp; i++) {
            if (memcmp(g_disp[i].curr, g_disp[i].orig, sizeof g_disp[i].curr) != 0)
                write_ramp(&g_disp[i], g_disp[i].orig);
        }
        save_ramps_file();
    }
    return (g_mag_ok || g_ndisp > 0) ? 1 : 0;
}

void PxPipe_Shutdown(void)
{
    reset_all(0);
    for (int i = 0; i < g_ndisp; i++) {
        if (g_disp[i].dc) {
            DeleteDC(g_disp[i].dc);
            g_disp[i].dc = NULL;
        }
    }
    g_ndisp = 0;
    if (g_mag_ok && p_MagUninit) p_MagUninit();
    g_mag_ok = 0;
    g_orig_trusted = 0;
}

int  PxPipe_MagAvailable(void) { return g_mag_ok; }
int  PxPipe_RampDisplays(void) { return g_ndisp; }

void PxPipe_SetTargetDisplay(int idx) { g_target_disp = idx; }
int  PxPipe_GetTargetDisplay(void)    { return g_target_disp; }

int  PxPipe_BackupOriginals(void)  { save_ramps_file(); return g_ndisp; }
int  PxPipe_RestoreOriginals(void) { return load_ramps_file(); }

/* Forget what we believe the hardware holds.  Called after anything that may
 * have reset the display pipeline behind our back (display mode change, a game
 * taking over, session unlock): the next PxPipe_Run() re-asserts even though
 * the look is unchanged. */
void PxPipe_Resync(void)
{
    g_fx_known = 0;
    for (int d = 0; d < g_ndisp; d++) g_disp[d].have_curr = 0;
}

/* ---------------- Apply ---------------- */

/* Linear half of the look -> DWM.  The effect is sanitised (NaN/Inf -> identity,
 * weights scaled toward identity to stay in [-4, 4], column 4 == [0,0,0,0,1])
 * inside PxPlan_Compute() / cm_build_effect(); the final cm_sanitize() below is
 * the last gate before the API.  Nothing is sent when it equals what DWM
 * already holds; a successful send is READ BACK when the OS exposes
 * MagGetFullscreenColorEffect so "desktop output active" is verified, not
 * assumed. */
static void apply_matrix(const PxTransformPlan *plan, PxPipelineResult *out)
{
    out->matrix_supported = g_mag_ok;
    if (!g_mag_ok) {                       /* no DWM path: treat as success so ramps can still run */
        out->matrix_ok = 1;
        return;
    }

    MagColorEffect fx = plan->effect;
    cm_sanitize(&fx);

    if (g_fx_known && memcmp(&g_fx_curr, &fx, sizeof fx) == 0) {
        out->matrix_ok = 1;                /* already exactly this */
        return;
    }

    MagColorEffect tmp = fx;               /* the API takes a non-const pointer */
    if (!p_MagSetFx(&tmp)) return;         /* matrix_ok stays 0 -> PARTIAL/FAILED outcome */
    g_fx_curr = fx;
    g_fx_known = 1;
    out->matrix_sent = 1;
    out->matrix_ok = 1;

    if (g_mag_readback) {
        MagColorEffect rb;
        cm_identity(&rb);
        if (p_MagGetFx(&rb)) {
            out->matrix_verified = 1;
            out->matrix_held = (memcmp(&rb, &fx, sizeof fx) == 0);
        }
    } else {
        out->matrix_verified = 0;
        out->matrix_held = 1;              /* unverified but acknowledged */
    }
}

/* Non-linear half of the look -> GPU ramps, one display at a time.
 * PxPlan_RampForDisplay() (the transform layer's per-display plan step) decides
 * whether a write is needed at all:
 *   - neutral curves / bypass: no call (the original ramp is restored once if
 *     we had changed it)
 *   - computed ramp == curr: no call, so dragging colour sliders never makes
 *     the driver re-sync the display pipeline. */
static void apply_ramps(const PxTransformPlan *plan, PxPipelineResult *out)
{
    out->ramps_ok = 1;
    out->displays_prepared = g_ndisp;
    if (!g_ndisp) return;

    for (int d = 0; d < g_ndisp; d++) {
        DispDC *dd = &g_disp[d];
        if (!dd->have_orig) continue;

        /* Displays outside the current target simply return to their own ramp */
        PxTransformPlan neutral;
        const PxTransformPlan *dp = plan;
        if (g_target_disp >= 0 && g_target_disp != d) {
            Look n = (Look)LOOK_NEUTRAL_INIT;
            PxPlan_Compute(&n, &neutral);
            dp = &neutral;
        }

        WORD want[3][256];
        if (!PxPlan_RampForDisplay(dp, dd->orig, dd->curr, dd->have_curr, want)) continue;

        /* About to leave the original ramp: crash recovery has to know */
        if (memcmp(want, dd->orig, sizeof want) != 0) mark_dirty();
        if (!write_ramp(dd, want)) out->ramps_ok = 0;
        else out->ramps_called++;
    }

    /* Every display is back on its original ramp: nothing is left for crash recovery to undo */
    for (int d = 0; d < g_ndisp; d++) {
        if (g_disp[d].have_orig && memcmp(g_disp[d].curr, g_disp[d].orig, sizeof g_disp[d].curr) != 0) return;
    }
    clear_dirty();
}

void PxPipe_Run(const PxTransformPlan *plan, PxPipelineResult *out)
{
    memset(out, 0, sizeof *out);
    if (!plan) return;

    if (plan->bypassed) {
        out->restore_only = 1;
        /* bypassed: neutral display, only undo what we changed */
        reset_all(0);
        out->matrix_supported = g_mag_ok;
        out->matrix_ok = 1;
        out->ramps_ok = 1;
        return;
    }

    apply_matrix(plan, out);
    apply_ramps(plan, out);
}

/* Forced, unconditional restore: identity colour matrix + original gamma ramps.
 * Used by the reset buttons, the emergency hotkey and the crash handler. */
void PxPipe_ForceReset(void)
{
    reset_all(1);
}
