#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <math.h>
#include "host_mock.h"

MockDisplay g_mock_disp[MOCK_MAX_DISP];
int g_mock_set_total, g_mock_mag_calls, g_mock_mag_bad, g_mock_mag_init, g_mock_mag_uninit, g_mock_dirty_creates;
MagColorEffect g_mock_mag_last;
void (*g_mock_hook_get_ramp)(int display_index);
int g_mock_mag_set_fail = 0;          /* force MagSetFullscreenColorEffect to fail */
int g_mock_mag_noreadback = 0;        /* pretend the OS lacks MagGetFullscreenColorEffect */
int g_mock_mag_readback_drift = 0;    /* readback returns identity: DWM dropped the effect */
int g_mock_dm_report_mag, g_mock_dm_report_ramps, g_mock_dm_report_calls;

static DWORD g_tick = 1000;
DWORD GetTickCount(void) { g_tick += 13; return g_tick; }

/* ---- Magnification API ---- */
static BOOL mock_MagInitialize(void) { g_mock_mag_init++; return TRUE; }
static BOOL mock_MagUninitialize(void) { g_mock_mag_uninit++; return TRUE; }
static BOOL mock_MagSetFullscreenColorEffect(MagColorEffect *e)
{
    g_mock_mag_calls++;
    if (g_mock_mag_set_fail) return FALSE;
    g_mock_mag_last = *e;
    /* the DWM contract: finite, bounded weights, column 4 == [0 0 0 0 1] */
    for (int r = 0; r < 5; r++) for (int c = 0; c < 5; c++) {
        float v = e->transform[r][c];
        if (!isfinite(v) || v < -4.0f || v > 4.0f) g_mock_mag_bad++;
    }
    for (int r = 0; r < 4; r++) if (e->transform[r][4] != 0.0f) g_mock_mag_bad++;
    if (e->transform[4][4] != 1.0f) g_mock_mag_bad++;
    return TRUE;
}
static BOOL mock_MagGetFullscreenColorEffect(MagColorEffect *e)
{
    if (g_mock_mag_readback_drift) { cm_identity(e); return TRUE; }   /* "DWM forgot" */
    *e = g_mock_mag_last;
    return TRUE;
}
HMODULE LoadLibraryW(const wchar_t *n) { (void)n; return (HMODULE)0x1000; }
FARPROC GetProcAddress(HMODULE m, const char *name)
{
    (void)m;
    if (!strcmp(name, "MagInitialize")) return (FARPROC)mock_MagInitialize;
    if (!strcmp(name, "MagUninitialize")) return (FARPROC)mock_MagUninitialize;
    if (!strcmp(name, "MagSetFullscreenColorEffect")) return (FARPROC)mock_MagSetFullscreenColorEffect;
    if (!strcmp(name, "MagGetFullscreenColorEffect"))
        return g_mock_mag_noreadback ? NULL : (FARPROC)mock_MagGetFullscreenColorEffect;
    return NULL;
}

/* ---- display-manager seam: the engine reports pipeline capability here ---- */
void Dm_ReportOutputs(int mag_available, int ramp_displays)
{
    g_mock_dm_report_calls++;
    g_mock_dm_report_mag = mag_available;
    g_mock_dm_report_ramps = ramp_displays;
}

/* ---- displays / gamma ramps ---- */
HDC CreateDCA(const char *name, const char *a, const char *b, const void *c)
{
    (void)a; (void)b; (void)c;
    int n = 0;
    if (sscanf(name, "\\\\.\\DISPLAY%d", &n) != 1 || n < 1 || n > MOCK_MAX_DISP) return NULL;
    return g_mock_disp[n - 1].present ? (HDC)&g_mock_disp[n - 1] : NULL;
}
BOOL DeleteDC(HDC dc) { (void)dc; return TRUE; }
BOOL GetDeviceGammaRamp(HDC dc, LPVOID ramp)
{
    MockDisplay *d = (MockDisplay *)dc;
    if (g_mock_hook_get_ramp) g_mock_hook_get_ramp((int)(d - g_mock_disp));
    memcpy(ramp, d->hw, sizeof d->hw);
    return TRUE;
}
BOOL SetDeviceGammaRamp(HDC dc, LPVOID ramp)
{
    MockDisplay *d = (MockDisplay *)dc;
    d->set_calls++; g_mock_set_total++;
    if (!d->set_ok) return FALSE;
    memcpy(d->hw, ramp, sizeof d->hw);
    return TRUE;
}
BOOL EnumDisplayDevicesW(const wchar_t *a, DWORD i, DISPLAY_DEVICEW *dd, DWORD f) { (void)a; (void)i; (void)dd; (void)f; return FALSE; }

/* ---- strings ---- */
int wsprintfA(char *d, const char *fmt, ...) { va_list ap; va_start(ap, fmt); int n = vsnprintf(d, 1024, fmt, ap); va_end(ap); return n; }
int wsprintfW(wchar_t *d, const wchar_t *fmt, ...)
{
    wchar_t f2[256]; size_t j = 0;                       /* Windows %s in wsprintfW is a wide string */
    for (size_t i = 0; fmt[i] && j < 250; i++) {
        if (fmt[i] == L'%' && fmt[i + 1] == L's') { f2[j++] = L'%'; f2[j++] = L'l'; f2[j++] = L's'; i++; }
        else f2[j++] = fmt[i];
    }
    f2[j] = 0;
    va_list ap; va_start(ap, fmt); int n = vswprintf(d, 1024, f2, ap); va_end(ap); return n;
}
wchar_t *lstrcpyW(wchar_t *d, const wchar_t *s) { return wcscpy(d, s); }
wchar_t *lstrcpynW(wchar_t *d, const wchar_t *s, int n) { if (n <= 0) return d; wcsncpy(d, s, (size_t)n - 1); d[n - 1] = 0; return d; }
char *lstrcpynA(char *d, const char *s, int n) { if (n <= 0) return d; strncpy(d, s, (size_t)n - 1); d[n - 1] = 0; return d; }
int lstrcmpA(const char *a, const char *b) { return strcmp(a, b); }
int MultiByteToWideChar(UINT cp, DWORD fl, const char *s, int n, wchar_t *w, int wn)
{ (void)cp; (void)fl; (void)n; size_t i = 0; for (; s[i] && (int)i < wn - 1; i++) w[i] = (unsigned char)s[i]; if (wn > 0) w[i] = 0; return (int)i + 1; }

/* ---- files (POSIX-backed) ---- */
static void narrow(const wchar_t *w, char *out, size_t cap) { size_t n = wcstombs(out, w, cap - 1); out[n == (size_t)-1 ? 0 : n] = 0; }
HANDLE CreateFileW(const wchar_t *path, DWORD access, DWORD share, void *sec, DWORD disp, DWORD attrs, HANDLE t)
{
    (void)share; (void)sec; (void)attrs; (void)t;
    char p[1024]; narrow(path, p, sizeof p);
    int flags = 0;
    if (disp == OPEN_EXISTING) flags = (access & GENERIC_WRITE) ? O_RDWR : O_RDONLY;
    else if (disp == CREATE_ALWAYS) {
        flags = O_WRONLY | O_CREAT | O_TRUNC;
        size_t L = strlen(p);
        if (L >= 9 && !strcmp(p + L - 9, "dirty.flg")) g_mock_dirty_creates++;
    } else return INVALID_HANDLE_VALUE;
    int fd = open(p, flags, 0644);
    return fd < 0 ? INVALID_HANDLE_VALUE : (HANDLE)(intptr_t)fd;
}
BOOL ReadFile(HANDLE h, LPVOID b, DWORD n, DWORD *rd, void *o) { (void)o; ssize_t r = read((int)(intptr_t)h, b, n); if (r < 0) return FALSE; *rd = (DWORD)r; return TRUE; }
BOOL WriteFile(HANDLE h, LPCVOID b, DWORD n, DWORD *wr, void *o) { (void)o; ssize_t r = write((int)(intptr_t)h, b, n); if (r < 0) return FALSE; *wr = (DWORD)r; return TRUE; }
BOOL CloseHandle(HANDLE h) { return close((int)(intptr_t)h) == 0; }
BOOL DeleteFileW(const wchar_t *path) { char p[1024]; narrow(path, p, sizeof p); return unlink(p) == 0; }
BOOL FlushFileBuffers(HANDLE h) { return fsync((int)(intptr_t)h) == 0; }
BOOL MoveFileExW(const wchar_t *a, const wchar_t *b, DWORD f) { (void)f; char pa[1024], pb[1024]; narrow(a, pa, sizeof pa); narrow(b, pb, sizeof pb); return rename(pa, pb) == 0; }

/* ---- DisplayState / GameDisplayState facts ---------------------------------
 * The engine reads these through its normal interface; the host harness makes
 * them settable so HDR / exclusive-fullscreen / desktop cases can be driven. */
MockFacts g_mock_facts;

void MockFacts_Reset(void)
{
    memset(&g_mock_facts, 0, sizeof g_mock_facts);
    g_mock_facts.monitor_count = 1;
    g_mock_facts.current_monitor = 0;
    {
        const wchar_t *f = L"Mock Panel";
        const wchar_t *d = L"\\\\DISPLAY1";
        size_t i;
        for (i = 0; f[i] && i < 63; i++) g_mock_facts.mon[0].friendly[i] = f[i];
        g_mock_facts.mon[0].friendly[i] = 0;
        for (i = 0; d[i] && i < 31; i++) g_mock_facts.mon[0].dev_name[i] = d[i];
        g_mock_facts.mon[0].dev_name[i] = 0;
    }
    g_mock_facts.mon[0].is_primary = 1;
    g_mock_facts.mon[0].current_w = 1920;
    g_mock_facts.mon[0].current_h = 1080;
    g_mock_facts.mon[0].current_hz = 144;
    g_mock_facts.mon[0].bpc = 8;
    g_mock_facts.mon[0].color_space_raw = 0x00;   /* sRGB */
    PxGDS_Init(&g_mock_facts.game);
}

int Modes_MonitorCount(void) { return g_mock_facts.monitor_count; }
MonitorInfo *Modes_GetMonitor(int i)
{
    if (i < 0 || i >= g_mock_facts.monitor_count) return NULL;
    return &g_mock_facts.mon[i];
}
int Modes_CurrentMonitorIndex(void) { return g_mock_facts.current_monitor; }
int Dm_HdrAny(void) { return g_mock_facts.hdr_any; }
const PxGameDisplayState *Prof_GameState(void) { return &g_mock_facts.game; }
