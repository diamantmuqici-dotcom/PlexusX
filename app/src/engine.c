/* PlexusX — Engine: Windows Magnification API Color Matrix + GPU Gamma Ramps
 * Supports: NVIDIA, AMD, Intel, SDR, HDR, Multi-monitor.
 * Anti-cheat safe: Legit OS display APIs only. Zero game process touching.
 *
 * Pipeline split (see color_math.h for the math, which is shared with the tests):
 *   - DWM 5x5 matrix  : every LINEAR adjustment (hue, saturation, vibrance,
 *                       temperature, tint, RGB gain, brightness, contrast,
 *                       black level, white point).
 *   - GPU gamma ramp  : only NON-LINEAR curves (gamma, shadows toe, highlights
 *                       shoulder, clarity).  SetDeviceGammaRamp is never called
 *                       for neutral curves, nor when the ramp is unchanged.
 */
#include "common.h"
#include "color_math.h"

/* Magnification API.  MagColorEffect (color_math.h) is float transform[5][5],
 * exactly the 100-byte Win32 MAGCOLOREFFECT, row-vector convention [R G B A 1]*M:
 * translation in row 4 (transform[4][0..2]), column 4 strictly [0,0,0,0,1]. */
typedef BOOL (WINAPI *fn_MagInitialize)(void);
typedef BOOL (WINAPI *fn_MagUninitialize)(void);
typedef BOOL (WINAPI *fn_MagSetFullscreenColorEffect)(MagColorEffect *);

static fn_MagInitialize               p_MagInit;
static fn_MagUninitialize             p_MagUninit;
static fn_MagSetFullscreenColorEffect p_MagSetFx;
static int            g_mag_ok = 0;
static MagColorEffect g_fx_curr;          /* last effect DWM accepted */
static int            g_fx_known = 0;     /* g_fx_curr is trustworthy (cleared by Eng_Resync) */

_Static_assert(sizeof(WORD) == sizeof(unsigned short), "WORD must match the ramp type used by color_math.h");

#define MAX_DISP 8
typedef struct DispDC {
    HDC     dc;
    char    name[32];
    wchar_t wname[32];
    WORD    orig[3][256];   /* ramp the display had before PlexusX touched it (validated / identity fallback) */
    WORD    curr[3][256];   /* ramp PlexusX last programmed == what the hardware holds (== orig until first write) */
    int     have_orig;
    int     have_curr;      /* curr is trustworthy (cleared by Eng_Resync after external resets) */
} DispDC;

static DispDC  g_disp[MAX_DISP];
static int     g_ndisp = 0;
static int     g_target_disp = -1; /* -1 = all displays */
static int     g_eng_ready = 0;

static GpuInfo g_gpu_info;

static wchar_t g_ramps_path[MAX_PATH];
static wchar_t g_dirty_path[MAX_PATH];
static int     g_dirty_start = 0;
static int     g_dirty_marked = 0;     /* dirty.flg currently exists: don't hit the disk on every slider tick */
static int     g_orig_trusted = 0;     /* DispDC.orig really is the pre-PlexusX ramp.  FALSE between capturing the
                                        * ramps and finishing crash recovery on a dirty start: what the GPU holds
                                        * then may still be the previous session's modified ramp. */

/* ---------------- Kelvin to RGB Approximation (Planckian Locus) ---------------- */
void Eng_KelvinToRgb(float k, float *r, float *g, float *b)
{
    cm_kelvin_to_rgb(k, r, g, b);
}

/* ---------------- Hardware GPU Gamma Ramp Calculation ---------------- */
/* Non-linear curves ONLY (gamma, shadows toe, highlights shoulder, clarity).
 * Linear adjustments (RGB gain, black level, white point, brightness, contrast)
 * live in the DWM matrix; applying them here too would count them twice. */
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

/* ---------------- Crash Recovery & Dirty Flag File ---------------- */
void Eng_SetPaths(const wchar_t *ramps, const wchar_t *dirty, int was_dirty)
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

/* ---------------- GPU Detection ---------------- */
static void detect_gpu(void)
{
    memset(&g_gpu_info, 0, sizeof g_gpu_info);
    g_gpu_info.vendor = GPU_VENDOR_UNKNOWN;
    lstrcpyW(g_gpu_info.vendor_name, L"Generic Display");
    lstrcpyW(g_gpu_info.name, L"Display Adapter");

    DISPLAY_DEVICEW dd;
    memset(&dd, 0, sizeof dd);
    dd.cb = sizeof dd;

    for (DWORD i = 0; EnumDisplayDevicesW(NULL, i, &dd, 0); i++) {
        if (dd.StateFlags & DISPLAY_DEVICE_ATTACHED_TO_DESKTOP) {
            lstrcpynW(g_gpu_info.name, dd.DeviceString, 128);

            /* Check vendor name from device string or device ID */
            if (wcsstr(dd.DeviceString, L"NVIDIA") || wcsstr(dd.DeviceID, L"VEN_10DE")) {
                g_gpu_info.vendor = GPU_VENDOR_NVIDIA;
                lstrcpyW(g_gpu_info.vendor_name, L"NVIDIA");
            } else if (wcsstr(dd.DeviceString, L"AMD") || wcsstr(dd.DeviceString, L"Radeon") ||
                       wcsstr(dd.DeviceID, L"VEN_1002")) {
                g_gpu_info.vendor = GPU_VENDOR_AMD;
                lstrcpyW(g_gpu_info.vendor_name, L"AMD");
            } else if (wcsstr(dd.DeviceString, L"Intel") || wcsstr(dd.DeviceID, L"VEN_8086")) {
                g_gpu_info.vendor = GPU_VENDOR_INTEL;
                lstrcpyW(g_gpu_info.vendor_name, L"Intel");
            }
            break;
        }
    }

    g_gpu_info.mag_available = g_mag_ok;
    g_gpu_info.gamma_available = (g_ndisp > 0);
    g_gpu_info.hdr_detected = 0; /* Will be updated via display enumeration */
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

/* ---------------- Public Functions ---------------- */

void Eng_Init(void)
{
    HMODULE h = LoadLibraryW(L"magnification.dll");
    if (h) {
        p_MagInit = (fn_MagInitialize)GetProcAddress(h, "MagInitialize");
        p_MagUninit = (fn_MagUninitialize)GetProcAddress(h, "MagUninitialize");
        p_MagSetFx = (fn_MagSetFullscreenColorEffect)GetProcAddress(h, "MagSetFullscreenColorEffect");
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

    detect_gpu();

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
    g_eng_ready = 1;
}

void Eng_Shutdown(void)
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
    g_eng_ready = 0;
    g_orig_trusted = 0;
}

int  Eng_Available(void) { return g_mag_ok; }

void Eng_SetTargetMonitor(int idx)
{
    g_target_disp = idx;
}

int  Eng_GetTargetMonitor(void)
{
    return g_target_disp;
}

const GpuInfo *Eng_GetGpuInfo(void)
{
    return &g_gpu_info;
}

void Eng_BackupCurrentState(void)
{
    save_ramps_file();
}

int  Eng_RestoreLastGood(void)
{
    return load_ramps_file();
}

/* Forget what we believe the hardware holds.  Call after anything that may have
 * reset the display pipeline behind our back (display mode change, a game taking
 * over, the periodic refresh): the next Eng_Apply() re-asserts the look even though
 * it is unchanged. */
void Eng_Resync(void)
{
    g_fx_known = 0;
    for (int d = 0; d < g_ndisp; d++) g_disp[d].have_curr = 0;
}

/* Linear half of the look -> DWM.  The effect is sanitised (NaN/Inf -> identity,
 * weights clamped to [-4, 4], column 4 == [0,0,0,0,1]) inside cm_build_effect(),
 * and nothing is sent when it equals what DWM already has. */
static void apply_matrix(const Look *lk)
{
    if (!g_mag_ok) return;

    MagColorEffect fx;
    cm_build_effect(lk, &fx);
    cm_sanitize(&fx);                 /* last gate before DWM: NaN/Inf -> identity, weights in [-4, 4], W' column [0,0,0,0,1] */

    if (g_fx_known && memcmp(&g_fx_curr, &fx, sizeof fx) == 0) return;

    MagColorEffect tmp = fx;          /* the API takes a non-const pointer */
    if (p_MagSetFx(&tmp)) {
        g_fx_curr = fx;
        g_fx_known = 1;
    }
}

/* Non-linear half of the look -> GPU ramps, one display at a time.
 * cm_ramp_plan() decides whether a write is needed at all:
 *   - gamma==1.0 && shadows==100 && highlights==100 && clarity==100: no call
 *     (the original ramp is restored once if we had changed it)
 *   - computed ramp == curr: no call, so dragging colour sliders never makes the
 *     driver re-sync the display pipeline. */
static void apply_ramps(const Look *lk)
{
    if (!g_ndisp) return;

    static const Look neutral = LOOK_NEUTRAL_INIT;

    for (int d = 0; d < g_ndisp; d++) {
        DispDC *dd = &g_disp[d];
        if (!dd->have_orig) continue;

        /* Displays outside the current target simply return to their own ramp */
        const Look *dl = (g_target_disp >= 0 && g_target_disp != d) ? &neutral : lk;

        WORD want[3][256];
        if (!cm_ramp_plan(dd->orig, dd->curr, dd->have_curr, dl, want)) continue;

        /* About to leave the original ramp: crash recovery has to know */
        if (memcmp(want, dd->orig, sizeof want) != 0) mark_dirty();
        write_ramp(dd, want);
    }

    /* Every display is back on its original ramp: nothing is left for crash recovery to undo */
    for (int d = 0; d < g_ndisp; d++) {
        if (g_disp[d].have_orig && memcmp(g_disp[d].curr, g_disp[d].orig, sizeof g_disp[d].curr) != 0) return;
    }
    clear_dirty();
}

void Eng_Apply(const Look *lk)
{
    if (!g_eng_ready || !lk) return;

    Look safe = *lk;
    cm_sanitize_look(&safe);

    if (!safe.enabled) {
        reset_all(0);          /* bypassed: neutral display, only undo what we changed */
        return;
    }

    apply_matrix(&safe);
    apply_ramps(&safe);
}

/* Forced, unconditional restore: identity colour matrix + original gamma ramps.
 * Used by the reset buttons, the emergency hotkey and the crash handler. */
void Eng_Reset(void)
{
    reset_all(1);
}
