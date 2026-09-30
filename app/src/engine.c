/* PlexusX — Engine: Windows Magnification API Color Matrix + GPU Gamma Ramps
 * Supports: NVIDIA, AMD, Intel, SDR, HDR, Multi-monitor.
 * Anti-cheat safe: Legit OS display APIs only. Zero game process touching.
 */
#include "common.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* Magnification API types */
#pragma pack(push, 1)
typedef struct { float m[4][5]; } MagColorEffect;
#pragma pack(pop)

typedef BOOL (WINAPI *fn_MagInitialize)(void);
typedef BOOL (WINAPI *fn_MagUninitialize)(void);
typedef BOOL (WINAPI *fn_MagSetFullscreenColorEffect)(MagColorEffect *);

static fn_MagInitialize               p_MagInit;
static fn_MagUninitialize             p_MagUninit;
static fn_MagSetFullscreenColorEffect p_MagSetFx;
static int g_mag_ok = 0;

#define MAX_DISP 8
typedef struct DispDC {
    HDC     dc;
    char    name[32];
    wchar_t wname[32];
    WORD    orig[3][256];
    int     have_orig;
} DispDC;

static DispDC  g_disp[MAX_DISP];
static int     g_ndisp = 0;
static int     g_target_disp = -1; /* -1 = all displays */
static int     g_eng_ready = 0;

static GpuInfo g_gpu_info;

static wchar_t g_ramps_path[MAX_PATH];
static wchar_t g_dirty_path[MAX_PATH];
static int     g_dirty_start = 0;

/* ---------------- Kelvin to RGB Approximation (Planckian Locus) ---------------- */
void Eng_KelvinToRgb(float k, float *r, float *g, float *b)
{
    float temp = k / 100.0f;
    float red, green, blue;

    if (temp <= 66.0f) {
        red = 255.0f;
        green = temp;
        green = 99.4708025861f * logf(green) - 161.1195681661f;
        if (temp <= 19.0f) {
            blue = 0.0f;
        } else {
            blue = temp - 10.0f;
            blue = 138.5177312231f * logf(blue) - 305.0447927307f;
        }
    } else {
        red = temp - 60.0f;
        red = 329.698727446f * powf(red, -0.1332047592f);
        green = temp - 60.0f;
        green = 288.1221695283f * powf(green, -0.0755148492f);
        blue = 255.0f;
    }

    *r = clampf(red / 255.0f, 0.0f, 2.0f);
    *g = clampf(green / 255.0f, 0.0f, 2.0f);
    *b = clampf(blue / 255.0f, 0.0f, 2.0f);
}

/* ---------------- 4x4 Matrix Mathematics ---------------- */
typedef struct { float a[4][4]; } M4;

static void m_ident(M4 *m)
{
    memset(m, 0, sizeof *m);
    for (int i = 0; i < 4; i++) m->a[i][i] = 1.0f;
}

static void m_mul(M4 *o, const M4 *x, const M4 *y)
{
    M4 t;
    for (int r = 0; r < 4; r++) {
        for (int c = 0; c < 4; c++) {
            float s = 0.0f;
            for (int k = 0; k < 4; k++) s += x->a[r][k] * y->a[k][c];
            t.a[r][c] = s;
        }
    }
    *o = t;
}

static void m_sat_vib(M4 *o, float s, float v)
{
    /* Smart saturation / vibrance matrix using Rec.709 coefficients */
    const float lr = 0.2126f, lg = 0.7152f, lb = 0.0722f;
    float eff_sat = s * (0.6f + 0.4f * v);
    m_ident(o);
    for (int i = 0; i < 3; i++) {
        float w = (i == 0) ? lr : (i == 1) ? lg : lb;
        for (int j = 0; j < 3; j++) {
            o->a[i][j] = (j == i ? 1.0f : 0.0f) * eff_sat + w * (1.0f - eff_sat);
        }
    }
}

static void m_hue(M4 *o, float deg)
{
    float c = cosf(deg * (float)M_PI / 180.0f);
    float s = sinf(deg * (float)M_PI / 180.0f);
    m_ident(o);
    o->a[0][0] = 0.213f + 0.787f * c - 0.143f * s;
    o->a[0][1] = 0.715f - 0.715f * c + 0.140f * s;
    o->a[0][2] = 0.072f - 0.072f * c + 0.283f * s;
    o->a[1][0] = 0.213f - 0.213f * c + 0.140f * s;
    o->a[1][1] = 0.715f + 0.285f * c - 0.113f * s;
    o->a[1][2] = 0.072f - 0.072f * c - 0.283f * s;
    o->a[2][0] = 0.213f - 0.213f * c - 0.140f * s;
    o->a[2][1] = 0.715f - 0.715f * c + 0.130f * s;
    o->a[2][2] = 0.072f + 0.928f * c - 0.245f * s;
}

static void m_temp_tint_gain(M4 *o, float temp_k, float tint_pct, float rg, float gg, float bg)
{
    m_ident(o);
    float kr = 1.0f, kg = 1.0f, kb = 1.0f;
    Eng_KelvinToRgb(temp_k, &kr, &kg, &kb);

    /* Normalize relative to standard D65 6500K */
    float norm_r = 1.0f, norm_g = 1.0f, norm_b = 1.0f;
    Eng_KelvinToRgb(6500.0f, &norm_r, &norm_g, &norm_b);
    kr /= norm_r; kg /= norm_g; kb /= norm_b;

    /* Tint adjustment: -100 (green) to +100 (magenta) */
    float tint_norm = tint_pct / 100.0f;
    float tg = 1.0f - tint_norm * 0.20f;
    float tr = 1.0f + tint_norm * 0.15f;
    float tb = 1.0f + tint_norm * 0.15f;

    o->a[0][0] = kr * tr * rg;
    o->a[1][1] = kg * tg * gg;
    o->a[2][2] = kb * tb * bg;
}

static void m_bri_con(M4 *o, float con_pct, float bri_pct, float bl_pct, float wp_pct)
{
    /* Contrast: 0% to 200%, neutral = 100% (c = 1.0)
     * Brightness: 0% to 200%, neutral = 100% (b = 1.0)
     * Black Level: 0% to 200%, neutral = 100%
     * White Point: 0% to 200%, neutral = 100%
     */
    float c = con_pct / 100.0f;
    float b = bri_pct / 100.0f;
    float wp = wp_pct / 100.0f;
    float bl = (bl_pct - 100.0f) / 200.0f; /* offset */

    float slope = c * b * wp;
    float offset = 0.5f * (1.0f - c) + (b - 1.0f) * 0.5f + bl;

    m_ident(o);
    for (int i = 0; i < 3; i++) {
        o->a[i][i] = slope;
        o->a[i][3] = offset;
    }
}

/* ---------------- Hardware GPU Gamma Ramp Calculation ---------------- */
void Eng_CalculateGammaRamp(const Look *lk, WORD ramp[3][256])
{
    float g = clampf(lk->gamma, 0.40f, 2.50f);
    float sh_lift = (lk->shadows - 100.0f) / 100.0f;       /* -1.0 to +1.0 */
    float hl_lift = (lk->highlights - 100.0f) / 100.0f;    /* -1.0 to +1.0 */
    float clarity = (lk->clarity - 100.0f) / 100.0f;       /* -1.0 to +1.0 */

    float rg = lk->r_gain / 100.0f;
    float gg = lk->g_gain / 100.0f;
    float bg = lk->b_gain / 100.0f;

    float bl_offset = (lk->black_level - 100.0f) / 200.0f; /* black floor */
    float wp_scale  = lk->white_point / 100.0f;            /* white ceiling */

    for (int ch = 0; ch < 3; ch++) {
        float gain = (ch == 0 ? rg : (ch == 1 ? gg : bg));
        for (int i = 0; i < 256; i++) {
            float x = i / 255.0f;
            /* Base gamma power curve */
            float y = powf(x, g);

            /* Shadows: toe adjustment */
            if (fabsf(sh_lift) > 0.001f) {
                float toe = (1.0f - x) * (1.0f - x) * sh_lift * 0.35f;
                y = clampf(y + toe, 0.0f, 1.0f);
            }

            /* Highlights: shoulder adjustment */
            if (fabsf(hl_lift) > 0.001f) {
                float shoulder = x * x * hl_lift * 0.30f;
                y = clampf(y + shoulder, 0.0f, 1.0f);
            }

            /* Clarity / Dehaze: midtone S-curve enhancement */
            if (fabsf(clarity) > 0.001f) {
                float scurve = 0.5f * (1.0f - cosf(x * (float)M_PI)) - x;
                y = clampf(y + scurve * clarity * 0.25f, 0.0f, 1.0f);
            }

            /* Black level and white point */
            y = y * wp_scale + bl_offset;
            y *= gain;

            int val = (int)(y * 65535.0f + 0.5f);
            ramp[ch][i] = (WORD)clampi(val, 0, 65535);
        }
    }
}

/* ---------------- Crash Recovery & Dirty Flag File ---------------- */
void Eng_SetPaths(const wchar_t *ramps, const wchar_t *dirty, int was_dirty)
{
    lstrcpynW(g_ramps_path, ramps, MAX_PATH);
    lstrcpynW(g_dirty_path, dirty, MAX_PATH);
    g_dirty_start = was_dirty;
}

static void mark_dirty(void)
{
    if (!g_dirty_path[0]) return;
    HANDLE h = CreateFileW(g_dirty_path, GENERIC_WRITE, 0, NULL,
                           CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h != INVALID_HANDLE_VALUE) CloseHandle(h);
}

static void clear_dirty(void)
{
    if (g_dirty_path[0]) DeleteFileW(g_dirty_path);
}

static void save_ramps_file(void)
{
    if (!g_ramps_path[0] || !g_ndisp) return;
    HANDLE h = CreateFileW(g_ramps_path, GENERIC_WRITE, 0, NULL,
                           CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return;
    DWORD wr = 0, n = (DWORD)g_ndisp;
    WriteFile(h, &n, sizeof n, &wr, NULL);
    for (int i = 0; i < g_ndisp; i++) {
        WriteFile(h, g_disp[i].name, 32, &wr, NULL);
        WriteFile(h, g_disp[i].orig, sizeof g_disp[i].orig, &wr, NULL);
    }
    CloseHandle(h);
}

static int load_ramps_file(void)
{
    if (!g_ramps_path[0]) return 0;
    HANDLE h = CreateFileW(g_ramps_path, GENERIC_READ, FILE_SHARE_READ, NULL,
                           OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return 0;
    DWORD rd = 0, n = 0;
    int ok = 0;
    if (ReadFile(h, &n, sizeof n, &rd, NULL) && rd == 4) {
        for (DWORD i = 0; i < n && i < MAX_DISP; i++) {
            char nm[32];
            WORD ramp[3][256];
            if (!ReadFile(h, nm, 32, &rd, NULL) || rd != 32) break;
            if (!ReadFile(h, ramp, sizeof ramp, &rd, NULL) || rd != sizeof ramp) break;
            for (int d = 0; d < g_ndisp; d++) {
                if (lstrcmpA(g_disp[d].name, nm) == 0) {
                    memcpy(g_disp[d].orig, ramp, sizeof ramp);
                    g_disp[d].have_orig = 1;
                    SetDeviceGammaRamp(g_disp[d].dc, ramp);
                    ok++;
                }
            }
        }
    }
    CloseHandle(h);
    return ok;
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

    /* Enumerate display DCs & store original gamma ramps */
    g_ndisp = 0;
    for (int i = 1; i <= MAX_DISP; i++) {
        char nm[32];
        wsprintfA(nm, "\\\\.\\DISPLAY%d", i);
        HDC dc = CreateDCA(nm, NULL, NULL, NULL);
        if (!dc) continue;
        WORD orig[3][256];
        if (!GetDeviceGammaRamp(dc, orig)) {
            DeleteDC(dc);
            continue;
        }
        g_disp[g_ndisp].dc = dc;
        lstrcpynA(g_disp[g_ndisp].name, nm, 32);
        MultiByteToWideChar(CP_ACP, 0, nm, -1, g_disp[g_ndisp].wname, 32);
        memcpy(g_disp[g_ndisp].orig, orig, sizeof orig);
        g_disp[g_ndisp].have_orig = 1;
        g_ndisp++;
    }

    detect_gpu();

    /* Crash recovery: previous session died unexpectedly */
    if (g_dirty_start) {
        if (!load_ramps_file()) {
            for (int i = 0; i < g_ndisp; i++) {
                if (g_disp[i].have_orig)
                    SetDeviceGammaRamp(g_disp[i].dc, g_disp[i].orig);
            }
        }
    } else {
        save_ramps_file();
    }
    g_eng_ready = 1;
}

void Eng_Shutdown(void)
{
    Eng_Reset();
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

static void apply_matrix(const Look *lk)
{
    if (!g_mag_ok) return;

    M4 m, t;
    m_ident(&m);

    /* 1. Hue rotation */
    if (fabsf(lk->hue) > 0.01f) {
        m_hue(&t, lk->hue);
        m_mul(&m, &m, &t);
    }

    /* 2. Saturation & Vibrance */
    m_sat_vib(&t, lk->sat / 100.0f, lk->vibrance / 100.0f);
    m_mul(&m, &m, &t);

    /* 3. Color Temperature, Tint, and Channel Gain */
    m_temp_tint_gain(&t, lk->temp, lk->tint,
                     lk->r_gain / 100.0f, lk->g_gain / 100.0f, lk->b_gain / 100.0f);
    m_mul(&m, &m, &t);

    /* 4. Brightness, Contrast, Black Level, White Point */
    m_bri_con(&t, lk->con, lk->bri, lk->black_level, lk->white_point);
    m_mul(&m, &m, &t);

    /* Build MagColorEffect */
    MagColorEffect e;
    memset(&e, 0, sizeof e);
    for (int r = 0; r < 3; r++) {
        for (int c = 0; c < 3; c++) e.m[r][c] = m.a[r][c];
        e.m[r][3] = 0.0f;           /* alpha weight = 0 */
        e.m[r][4] = m.a[r][3];       /* translation / bias offset */
    }
    e.m[3][0] = e.m[3][1] = e.m[3][2] = 0.0f;
    e.m[3][3] = 1.0f;               /* alpha passthrough */
    e.m[3][4] = 0.0f;

    p_MagSetFx(&e);
}

static void apply_ramps(const Look *lk)
{
    if (!g_ndisp) return;

    WORD ramp[3][256];
    Eng_CalculateGammaRamp(lk, ramp);

    for (int d = 0; d < g_ndisp; d++) {
        if (g_target_disp >= 0 && g_target_disp != d) continue;
        SetDeviceGammaRamp(g_disp[d].dc, ramp);
    }
}

void Eng_Apply(const Look *lk)
{
    if (!g_eng_ready) return;
    if (!lk->enabled) {
        Eng_Reset();
        return;
    }
    mark_dirty();
    apply_matrix(lk);
    apply_ramps(lk);
}

void Eng_Reset(void)
{
    if (g_mag_ok) {
        MagColorEffect id;
        memset(&id, 0, sizeof id);
        id.m[0][0] = id.m[1][1] = id.m[2][2] = id.m[3][3] = 1.0f;
        p_MagSetFx(&id);
    }
    for (int d = 0; d < g_ndisp; d++) {
        if (g_disp[d].have_orig) {
            SetDeviceGammaRamp(g_disp[d].dc, g_disp[d].orig);
        }
    }
    clear_dirty();
}
