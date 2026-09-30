/* engine.c \u2014 two-stage display colour engine
 *
 * Stage 1 (whole desktop): the Magnifier colour-matrix layer in
 * magnification.dll \u2014 the same OS facility Windows Magnifier's colour
 * filter uses.  Carries saturation, vibrance (mid-point approximation),
 * contrast, temperature, tint, RGB gains, intensity, hue.
 *
 * Stage 2 (per monitor): 16-bit gamma ramps via SetDeviceGammaRamp on the
 * monitor's DC.  Carries gamma, brightness, shadows, highlights, black
 * level, white point, clarity/dehaze.  This stage CAN target one monitor.
 *
 * Honesty rules baked in:
 *   - if the colour-matrix layer is unavailable (driver), we fall back to
 *     per-channel ramps and clearly report that saturation/temperature are
 *     unavailable on this system \u2014 never silently faked.
 *   - matrix coefficients are clamped to the API's documented ±1.0 range;
 *     values beyond that simply cap there on the live display (the in-app
 *     preview still shows the full curve).
 *   - nothing here loops: applies happen from events only (UI, phone,
 *     game-watch, WM_DISPLAYCHANGE).
 */
#include "common.h"

typedef struct MagColorMatrix {
    float _11, _12, _13, _14;
    float _21, _22, _23, _24;
    float _31, _32, _33, _34;
} MagColorMatrix;

typedef BOOL (WINAPI *FnSetMatrix)(MagColorMatrix *p);
static HMODULE     g_mag;
static FnSetMatrix g_magSet;
static int         g_mag_state = 0;   /* 0 unknown, 1 ok, -1 unavailable */

static CxLook g_last;
static int    g_last_valid;
static wchar_t g_lastPath[MAX_PATH];
static int    g_warned_ramp, g_warned_matrix;

#define LASTAPPLIED_NAME L"lastapplied.json"

static float clampmag(float v) { return v < -1.f ? -1.f : (v > 1.f ? 1.f : v); }

int Eng_MagAvailable(void)
{
    if (g_mag_state) return g_mag_state > 0;
    g_mag = LoadLibraryW(L"magnification.dll");
    if (!g_mag) { g_mag_state = -1; return 0; }
    g_magSet = (FnSetMatrix)(void *)GetProcAddress(g_mag, "MagnificationSetDeviceColorMatrix");
    if (!g_magSet) { g_mag_state = -1; return 0; }
    g_mag_state = 1;
    return 1;
}

int Eng_RampAvailable(void)
{
    static int probed = 0, ok = 1;
    if (probed) return ok;
    probed = 1;
    if (MonCount() > 0) {
        const MonInfo *m = MonGet(0);
        /* CreateDC with the DISPLAY driver targets a specific monitor */
        HDC hdc = CreateDCW(L"DISPLAY", m->device, NULL, NULL);
        int owned = 1;
        if (!hdc) { hdc = GetDC(NULL); owned = 0; }
        if (hdc) {
            WORD ramp[3][256];
            for (int c = 0; c < 3; c++)
                for (int i = 0; i < 256; i++)
                    ramp[c][i] = (WORD)(i * 257);
            ok = SetDeviceGammaRamp(hdc, ramp) ? 1 : 0;
            if (owned) DeleteDC(hdc); else ReleaseDC(NULL, hdc);
        } else ok = 0;
    }
    return ok;
}

static void last_path(void)
{
    if (!g_lastPath[0])
        wsprintfW(g_lastPath, L"%s\\%s", g_appdata, LASTAPPLIED_NAME);
}

static char *path_to_utf8(const wchar_t *wp, char *out, int sz)
{
    WideCharToMultiByte(CP_UTF8, 0, wp, -1, out, sz, NULL, NULL);
    out[sz - 1] = 0;
    return out;
}

void Eng_Init(void)
{
    CxLook_Default(&g_last);
    g_last_valid = 0;
    last_path();
    /* crash recovery: restore last-applied look (last known good) */
    char path[MAX_PATH];
    CxPreset p;
    char err[160] = { 0 };
    if (CxPreset_ImportFile(path_to_utf8(g_lastPath, path, MAX_PATH), &p, err, sizeof err) == 0) {
        CxLook_Clamp(&p.look);
        if (p.look.enabled && !CxLook_IsNeutral(&p.look)) {
            g_last = p.look;
            g_last_valid = 1;
        }
    }
}

void Eng_Shutdown(void)
{
    Eng_ResetAll();
    if (g_mag) FreeLibrary(g_mag);
    g_mag = NULL;
    g_magSet = NULL;
}

static void mag_apply_matrix(const CxLook *look)
{
    if (!Eng_MagAvailable()) return;
    float m[4][4];
    CxLook_BuildMatrix(look, m);
    MagColorMatrix c;
    c._11 = clampmag(m[0][0]); c._12 = clampmag(m[0][1]);
    c._13 = clampmag(m[0][2]); c._14 = clampmag(m[0][3]);
    c._21 = clampmag(m[1][0]); c._22 = clampmag(m[1][1]);
    c._23 = clampmag(m[1][2]); c._24 = clampmag(m[1][3]);
    c._31 = clampmag(m[2][0]); c._32 = clampmag(m[2][1]);
    c._33 = clampmag(m[2][2]); c._34 = clampmag(m[2][3]);
    g_magSet(&c);
}

static void mag_reset(void)
{
    if (!Eng_MagAvailable()) return;
    MagColorMatrix id;
    id._11 = 1; id._12 = 0; id._13 = 0; id._14 = 0;
    id._21 = 0; id._22 = 1; id._23 = 0; id._24 = 0;
    id._31 = 0; id._32 = 0; id._33 = 1; id._34 = 0;
    g_magSet(&id);
}

static int ramp_apply_monitor(int mi, const CxLook *look, int fallback)
{
    const MonInfo *m = MonGet(mi);
    if (!m) return -1;
    HDC hdc = CreateDCW(L"DISPLAY", m->device, NULL, NULL);
    int owned = 1;
    if (!hdc) { hdc = GetDC(NULL); owned = 0; }
    if (!hdc) return -1;
    uint16_t ramp[3][256];
    CxLook_BuildLUT(look, ramp, fallback);
    int ok = SetDeviceGammaRamp(hdc, ramp) ? 1 : 0;
    if (owned) DeleteDC(hdc); else ReleaseDC(NULL, hdc);
    if (!ok && !g_warned_ramp) {
        g_warned_ramp = 1;
        Main_Notify(L"Driver refused gamma-ramp writes on this display \u2014 per-monitor curves are unavailable on it (matrix stage still active).");
    }
    return ok ? 0 : -1;
}

void Eng_ApplyLook(const CxLook *look, int monitor, int allMonitors)
{
    CxLook l = *look;
    CxLook_Clamp(&l);

    if (!l.enabled || CxLook_IsNeutral(&l)) {
        mag_reset();
        for (int i = 0; i < MonCount(); i++) ramp_apply_monitor(i, &l, 0);
        Eng_SaveLast(&l);
        return;
    }

    int mag_ok = Eng_MagAvailable();
    if (mag_ok) {
        mag_apply_matrix(&l);
    } else if (!g_warned_matrix) {
        g_warned_matrix = 1;
        Main_Notify(L"Colour-matrix layer unavailable on this driver \u2014 saturation/temperature are reduced to per-monitor curve approximations.");
    }

    if (allMonitors) {
        for (int i = 0; i < MonCount(); i++) ramp_apply_monitor(i, &l, mag_ok ? 0 : 1);
    } else if (monitor >= 0 && monitor < MonCount()) {
        ramp_apply_monitor(monitor, &l, mag_ok ? 0 : 1);
    }

    Eng_SaveLast(&l);
}

void Eng_ResetAll(void)
{
    CxLook l;
    CxLook_Default(&l);
    l.enabled = 0;
    mag_reset();
    for (int i = 0; i < MonCount(); i++) ramp_apply_monitor(i, &l, 0);
    last_path();
    DeleteFileW(g_lastPath);
    CxLook_Default(&g_last);
    g_last_valid = 0;
}

void Eng_SaveLast(const CxLook *look)
{
    CxLook n = *look;
    CxLook_Clamp(&n);
    g_last = n;
    g_last_valid = n.enabled && !CxLook_IsNeutral(&n);
    last_path();
    DeleteFileW(g_lastPath);
    if (!g_last_valid) return;

    CxPreset p;
    CxPreset_Default(&p, "Last applied");
    p.look = n;
    char path[MAX_PATH];
    if (CxPreset_WriteFile(path_to_utf8(g_lastPath, path, MAX_PATH), &p) != 0)
        Main_Log(L"WARN", L"could not persist last-applied look");
}

int Eng_LoadLast(CxLook *out)
{
    if (!g_last_valid || !out) return 0;
    *out = g_last;
    return 1;
}
