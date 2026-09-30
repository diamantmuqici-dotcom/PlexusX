/* engine.c — colour pipeline: Windows magnifier colour matrix + GPU gamma ramps */
#include "common.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* ---- magnification.dll (dynamic, no import lib needed) ---- */
#pragma pack(push, 1)
typedef struct { float m[4][5]; } MagColorEffect; /* rows r,g,b,a — cols R,G,B,A,offset */
#pragma pack(pop)

typedef BOOL (WINAPI *fn_MagInitialize)(void);
typedef BOOL (WINAPI *fn_MagUninitialize)(void);
typedef BOOL (WINAPI *fn_MagSetFullscreenColorEffect)(MagColorEffect *);

static fn_MagInitialize               p_MagInit;
static fn_MagUninitialize             p_MagUninit;
static fn_MagSetFullscreenColorEffect p_MagSetFx;
static int g_mag_ok;

/* ---- gamma ramps, one set of originals per display DC ---- */
#define MAX_DISP 4
static struct {
    HDC  dc;
    char name[32];
    WORD orig[3][256];
    int  have_orig;
} g_disp[MAX_DISP];
static int g_ndisp;

static int g_eng_ready;

/* crash-safe gamma state */
static wchar_t g_ramps_path[MAX_PATH];
static wchar_t g_dirty_path[MAX_PATH];
static int     g_dirty_start;

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
                    SetDeviceGammaRamp(g_disp[d].dc, ramp);   /* fix stuck screen */
                    ok++;
                }
            }
        }
    }
    CloseHandle(h);
    return ok;
}

/* ---------------- 4x4 matrix helpers (state = column vector r,g,b,1) ---------------- */
typedef struct { float a[4][4]; } M4;

static void m_ident(M4 *m)
{
    memset(m, 0, sizeof *m);
    for (int i = 0; i < 4; i++) m->a[i][i] = 1.f;
}

static void m_mul(M4 *o, const M4 *x, const M4 *y)   /* o = x * y */
{
    M4 t;
    for (int r = 0; r < 4; r++)
        for (int c = 0; c < 4; c++) {
            float s = 0;
            for (int k = 0; k < 4; k++) s += x->a[r][k] * y->a[k][c];
            t.a[r][c] = s;
        }
    *o = t;
}

static void m_sat(M4 *o, float s)          /* s = 1..3 */
{
    const float lr = 0.2126f, lg = 0.7152f, lb = 0.0722f;
    m_ident(o);
    for (int i = 0; i < 3; i++) {
        float w = (i == 0) ? lr : (i == 1) ? lg : lb;
        for (int j = 0; j < 3; j++)
            o->a[i][j] = (j == i ? 1.f : 0.f) * s + w * (1.f - s);
    }
}

static void m_hue(M4 *o, float deg)         /* SVG feColorMatrix hueRotate */
{
    float c = cosf(deg * (float)M_PI / 180.f), s = sinf(deg * (float)M_PI / 180.f);
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

static void m_temp(M4 *o, float t)          /* t = -1..1, + warm */
{
    m_ident(o);
    o->a[0][0] = 1.f + t * 0.30f;
    o->a[2][2] = 1.f - t * 0.30f;
}

static void m_con_bri(M4 *o, float con, float bri)   /* both -1..1 */
{
    float c = 1.f + con * 0.85f;
    float k = 1.f + bri * 0.80f;
    float off = (bri > 0 ? bri * 0.10f : bri * 0.06f) + 0.5f - 0.5f * c;
    m_ident(o);
    for (int i = 0; i < 3; i++) { o->a[i][i] = c * k; o->a[i][3] = off * k; }
}

/* ---------------- public ---------------- */

void Eng_Init(void)
{
    HMODULE h = LoadLibraryW(L"magnification.dll");
    if (h) {
        p_MagInit = (fn_MagInitialize)GetProcAddress(h, "MagInitialize");
        p_MagUninit = (fn_MagUninitialize)GetProcAddress(h, "MagUninitialize");
        p_MagSetFx = (fn_MagSetFullscreenColorEffect)GetProcAddress(h, "MagSetFullscreenColorEffect");
        g_mag_ok = p_MagInit && p_MagSetFx && p_MagInit();
    }

    /* collect display DCs + save original ramps */
    for (int i = 1; i <= MAX_DISP; i++) {
        char nm[32];
        wsprintfA(nm, "\\\\.\\DISPLAY%d", i);
        HDC dc = CreateDCA(nm, NULL, NULL, NULL);
        if (!dc) continue;
        WORD orig[3][256];
        if (!GetDeviceGammaRamp(dc, orig)) { DeleteDC(dc); continue; }
        g_disp[g_ndisp].dc = dc;
        lstrcpynA(g_disp[g_ndisp].name, nm, 32);
        memcpy(g_disp[g_ndisp].orig, orig, sizeof orig);
        g_disp[g_ndisp].have_orig = 1;
        if (++g_ndisp >= MAX_DISP) break;
    }
    /* crash recovery: previous run died with our ramps active */
    if (g_dirty_start) {
        if (!load_ramps_file())
            for (int i = 0; i < g_ndisp; i++)
                if (g_disp[i].have_orig)
                    SetDeviceGammaRamp(g_disp[i].dc, g_disp[i].orig);
    } else {
        save_ramps_file();
    }
    g_eng_ready = 1;
}

void Eng_Shutdown(void)
{
    Eng_Reset();
    for (int i = 0; i < g_ndisp; i++)
        if (g_disp[i].dc) { DeleteDC(g_disp[i].dc); g_disp[i].dc = NULL; }
    g_ndisp = 0;
    if (g_mag_ok && p_MagUninit) p_MagUninit();
    g_mag_ok = 0;
    g_eng_ready = 0;
}

int Eng_Available(void) { return g_mag_ok; }

static void apply_matrix(const Look *lk)
{
    if (!g_mag_ok) return;
    M4 m, t;
    m_ident(&m);
    m_hue(&t, lk->hue);            m_mul(&m, &m, &t);
    m_sat(&t, lk->sat / 100.f);    m_mul(&m, &m, &t);
    m_temp(&t, lk->temp / 100.f);  m_mul(&m, &m, &t);
    m_con_bri(&t, lk->con / 100.f, lk->bri / 100.f);
    m_mul(&m, &m, &t);

    MagColorEffect e;
    memset(&e, 0, sizeof e);
    for (int r = 0; r < 3; r++) {
        for (int c = 0; c < 4; c++) e.m[r][c] = m.a[r][c]; /* rgb + offset */
        e.m[r][3] = 0.f;                                   /* alpha in = 0 */
        e.m[r][4] = m.a[r][3];                             /* offset */
    }
    e.m[3][0] = e.m[3][1] = e.m[3][2] = 0.f;
    e.m[3][3] = 1.f;   /* alpha passthrough */
    e.m[3][4] = 0.f;
    /* note: layout per row = [R,G,B,A,offset] — rgb rows: put offset in col 4 */
    for (int r = 0; r < 3; r++) {
        e.m[r][3] = 0.f;
        e.m[r][4] = m.a[r][3];
    }
    p_MagSetFx(&e);
}

static void apply_ramps(const Look *lk)
{
    if (!g_ndisp) return;

    /* fallback path without the magnifier: carry temp/brightness on the ramp */
    int fallback = !g_mag_ok;
    float temp = lk->enabled ? lk->temp / 100.f : 0.f;
    float bri  = lk->enabled ? lk->bri / 100.f : 0.f;
    float g    = lk->enabled ? clampf(lk->gamma, 0.40f, 2.40f) : 1.f;

    for (int d = 0; d < g_ndisp; d++) {
        WORD ramp[3][256];
        for (int ch = 0; ch < 3; ch++) {
            float gain = 1.f;
            if (fallback) {
                if (ch == 0) gain = 1.f + temp * 0.30f + bri * 0.35f;
                if (ch == 2) gain = 1.f - temp * 0.30f + bri * 0.35f;
                if (ch == 1) gain = 1.f + bri * 0.35f;
            }
            for (int i = 0; i < 256; i++) {
                float x = i / 255.f;
                float y = powf(x, g) * gain;
                int v = (int)(y * 65535.f + 0.5f);
                ramp[ch][i] = (WORD)clampi(v, 0, 65535);
            }
        }
        SetDeviceGammaRamp(g_disp[d].dc, ramp);
    }
}

void Eng_Apply(const Look *lk)
{
    if (!g_eng_ready) return;
    if (!lk->enabled) { Eng_Reset(); return; }
    mark_dirty();
    apply_matrix(lk);
    apply_ramps(lk);
}

void Eng_Reset(void)
{
    if (g_mag_ok) {
        MagColorEffect id;
        memset(&id, 0, sizeof id);
        id.m[0][0] = id.m[1][1] = id.m[2][2] = id.m[3][3] = 1.f;
        p_MagSetFx(&id);
    }
    for (int d = 0; d < g_ndisp; d++)
        if (g_disp[d].have_orig)
            SetDeviceGammaRamp(g_disp[d].dc, g_disp[d].orig);
    clear_dirty();
}
