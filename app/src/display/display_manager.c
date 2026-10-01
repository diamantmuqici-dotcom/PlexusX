/* PlexusX — DisplayManager (formerly modes.c): monitors, resolution & refresh
 * modes, stretched 4:3 presets, and the REAL DisplayState (HDR, bits per
 * channel, color space, luminance) collected from the OS.
 *
 * Legit Windows display APIs only.  No driver hacking:
 *   - EnumDisplaySettingsW / EnumDisplayDevicesW for modes + monitor names
 *   - ChangeDisplaySettingsExW with CDS_TEST rollback protection
 *   - DXGI 1.6 (IDXGIOutput6::GetDesc1) for color space / bpc / luminance —
 *     values the OS reports, never invented.  If dxgi.dll is unavailable the
 *     fields stay 0 / PX_CS_UNKNOWN and the UIs render "—".
 *
 * The mode tables and classification helpers live in display_state.h (pure)
 * so the sort / pick / HDR predicates are host unit-tested.
 */
#include "common.h"
#include <dxgi.h>
#include <dxgi1_6.h>

#define MAX_MODES    128
#define MAX_MONITORS 8

static ModeInfo    g_modes[MAX_MODES];
static int         g_nmodes = 0;
static ModeInfo    g_cur_mode;

static MonitorInfo g_monitors[MAX_MONITORS];
static int         g_nmonitors = 0;
static int         g_cur_monitor = 0;

static wchar_t     g_active_dev[32];

static GpuInfo     g_gpu;            /* collected here now (was inside engine.c) */
static int         g_pipe_mag = 0;   /* reported by the color pipeline at init  */
static int         g_pipe_ramps = 0;

static int mode_cmp(const void *a, const void *b)
{
    return PX_MODE_CMP(a, b);
}

/* ---------------- GPU + per-output color state via DXGI 1.6 ----------------
 * One pass over factory→adapters→outputs.  Each output matched to a
 * MonitorInfo entry (by \\.\DISPLAYn name) gets BitsPerColor / ColorSpace /
 * luminance written in; the first desktop-scoped adapter becomes GpuInfo.
 * GUIDs are local literals: no initguid/libuuid link-order games. */
static const GUID g_iid_dxgi_factory1 =
    { 0x770aae78, 0xf26f, 0x4dba, { 0xa8, 0x29, 0x25, 0x3c, 0x83, 0xd1, 0xb3, 0x87 } };
static const GUID g_iid_dxgi_output6 =
    { 0x068346e8, 0xaaec, 0x4b84, { 0xad, 0xd7, 0x13, 0x7f, 0x51, 0x3f, 0x77, 0xa1 } };

typedef HRESULT (WINAPI *fn_CreateDXGIFactory1)(REFIID riid, void **factory);

static void collect_dxgi_state(void)
{
    HMODULE dxgi = LoadLibraryW(L"dxgi.dll");
    if (!dxgi) return;
    fn_CreateDXGIFactory1 pCreate =
        (fn_CreateDXGIFactory1)(void (*)(void))GetProcAddress(dxgi, "CreateDXGIFactory1");
    if (!pCreate) { FreeLibrary(dxgi); return; }

    IDXGIFactory1 *factory = NULL;
    if (FAILED(pCreate(&g_iid_dxgi_factory1, (void **)&factory)) || !factory) {
        FreeLibrary(dxgi);
        return;
    }

    for (UINT ai = 0; ; ai++) {
        IDXGIAdapter1 *adapter = NULL;
        if (FAILED(factory->lpVtbl->EnumAdapters1(factory, ai, &adapter)) || !adapter) break;

        DXGI_ADAPTER_DESC1 ad;
        memset(&ad, 0, sizeof ad);
        if (SUCCEEDED(adapter->lpVtbl->GetDesc1(adapter, &ad)) &&
            !(ad.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) &&
            (g_gpu.vendor == GPU_VENDOR_UNKNOWN || !g_gpu.name[0])) {
            lstrcpynW(g_gpu.name, ad.Description, 128);
            if (ad.VendorId == 0x10DE)      { g_gpu.vendor = GPU_VENDOR_NVIDIA; lstrcpyW(g_gpu.vendor_name, L"NVIDIA"); }
            else if (ad.VendorId == 0x1002) { g_gpu.vendor = GPU_VENDOR_AMD;   lstrcpyW(g_gpu.vendor_name, L"AMD"); }
            else if (ad.VendorId == 0x8086) { g_gpu.vendor = GPU_VENDOR_INTEL; lstrcpyW(g_gpu.vendor_name, L"Intel"); }
            else                            { g_gpu.vendor = GPU_VENDOR_UNKNOWN; lstrcpyW(g_gpu.vendor_name, L"Display adapter"); }
        }

        for (UINT oi = 0; ; oi++) {
            IDXGIOutput *out = NULL;
            if (FAILED(adapter->lpVtbl->EnumOutputs(adapter, oi, &out)) || !out) break;

            IDXGIOutput6 *o6 = NULL;
            if (SUCCEEDED(out->lpVtbl->QueryInterface(out, &g_iid_dxgi_output6, (void **)&o6)) && o6) {
                DXGI_OUTPUT_DESC1 d1;
                if (SUCCEEDED(o6->lpVtbl->GetDesc1(o6, &d1))) {
                    for (int mi = 0; mi < g_nmonitors; mi++) {
                        if (lstrcmpW(g_monitors[mi].dev_name, d1.DeviceName) != 0) continue;
                        MonitorInfo *m = &g_monitors[mi];
                        m->bpc = (int)d1.BitsPerColor;
                        m->color_space_raw = (int)d1.ColorSpace;
                        m->hdr_enabled = px_cs_is_hdr(m->color_space_raw);
                        m->min_nits = d1.MinLuminance;
                        m->max_nits = d1.MaxLuminance;
                        m->max_full_frame_nits = d1.MaxFullFrameLuminance;
                        /* Capable == currently HDR (OS says so) or the panel reports
                         * HDR10-class full-frame luminance.  Nothing more is claimed. */
                        m->hdr_capable = m->hdr_enabled || d1.MaxFullFrameLuminance >= 300.0f;
                    }
                }
                o6->lpVtbl->Release(o6);
            }
            out->lpVtbl->Release(out);
        }
        adapter->lpVtbl->Release(adapter);
    }
    factory->lpVtbl->Release(factory);
    FreeLibrary(dxgi);
}

/* ---------------- Enumerate All Attached Monitors ---------------- */
static BOOL CALLBACK enum_mon_proc(HMONITOR hm, HDC hdc, LPRECT rc, LPARAM lp)
{
    (void)hdc; (void)lp;
    if (g_nmonitors >= MAX_MONITORS) return FALSE;

    MONITORINFOEXW mi;
    memset(&mi, 0, sizeof mi);
    mi.cbSize = sizeof mi;
    if (!GetMonitorInfoW(hm, (LPMONITORINFO)&mi)) return TRUE;

    MonitorInfo *m = &g_monitors[g_nmonitors++];
    memset(m, 0, sizeof *m);
    lstrcpynW(m->dev_name, mi.szDevice, 32);
    m->rc = mi.rcMonitor;
    m->is_primary = (mi.dwFlags & MONITORINFOF_PRIMARY) ? 1 : 0;
    m->bpc = 0;
    m->color_space_raw = PX_CS_UNKNOWN;
    m->min_nits = m->max_nits = m->max_full_frame_nits = 0.0f;

    /* Get friendly name from secondary EnumDisplayDevicesW call */
    DISPLAY_DEVICEW dd;
    memset(&dd, 0, sizeof dd);
    dd.cb = sizeof dd;
    if (EnumDisplayDevicesW(mi.szDevice, 0, &dd, 0)) {
        lstrcpynW(m->friendly, dd.DeviceString, 64);
    } else {
        lstrcpyW(m->friendly, L"Generic PnP Monitor");
    }

    /* Adapter name */
    DISPLAY_DEVICEW da;
    memset(&da, 0, sizeof da);
    da.cb = sizeof da;
    if (EnumDisplayDevicesW(NULL, 0, &da, 0)) {
        lstrcpynW(m->adapter, da.DeviceString, 128);
    }

    DEVMODEW dm;
    memset(&dm, 0, sizeof dm);
    dm.dmSize = sizeof dm;
    if (EnumDisplaySettingsW(mi.szDevice, ENUM_CURRENT_SETTINGS, &dm)) {
        m->current_w = (int)dm.dmPelsWidth;
        m->current_h = (int)dm.dmPelsHeight;
        m->current_hz = (int)dm.dmDisplayFrequency;
        m->bpc = 0;   /* refresh replaces this with the DXGI report (or leaves unknown) */
    }

    return TRUE;
}

/* Driver version: DISPLAY_DEVICEW carries no version on current Windows, so read
 * the class key the PnP installer writes for the primary display adapter (always
 * present; read-only; no admin).  Failure stays "" — the UI prints "unknown". */
static void collect_driver_version(void)
{
    wchar_t buf[64];
    DWORD sz = sizeof buf, type = 0;
    g_gpu.driver_ver[0] = 0;
    if (RegGetValueW(HKEY_LOCAL_MACHINE,
                     L"SYSTEM\\CurrentControlSet\\Control\\Class\\{4d36e968-e325-11ce-bfc1-08002be10318}\\0000",
                     L"DriverVersion", RRF_RT_REG_SZ, &type, buf, &sz) == ERROR_SUCCESS)
        lstrcpynW(g_gpu.driver_ver, buf, 64);
}

int Modes_Refresh(void)
{
    g_nmonitors = 0;
    EnumDisplayMonitors(NULL, NULL, enum_mon_proc, 0);
    if (!g_nmonitors) {
        /* Fallback */
        lstrcpyW(g_monitors[0].dev_name, L"\\\\.\\DISPLAY1");
        lstrcpyW(g_monitors[0].friendly, L"Primary Display");
        g_monitors[0].color_space_raw = PX_CS_UNKNOWN;
        g_monitors[0].is_primary = 1;
        g_nmonitors = 1;
    }

    if (g_cur_monitor >= g_nmonitors) g_cur_monitor = 0;
    lstrcpynW(g_active_dev, g_monitors[g_cur_monitor].dev_name, 32);

    /* Enumerate supported modes for active monitor */
    DEVMODEW dm;
    g_nmodes = 0;
    for (int i = 0; i < 4096 && g_nmodes < MAX_MODES; i++) {
        memset(&dm, 0, sizeof dm);
        dm.dmSize = sizeof dm;
        if (!EnumDisplaySettingsW(g_active_dev, i, &dm)) break;
        if (dm.dmPelsWidth < 640 || dm.dmPelsHeight < 480) continue;
        int hz = (int)dm.dmDisplayFrequency;
        if (hz < 24 || hz > 1000) hz = 60;

        int dup = 0;
        for (int j = 0; j < g_nmodes; j++) {
            if (g_modes[j].w == (int)dm.dmPelsWidth &&
                g_modes[j].h == (int)dm.dmPelsHeight &&
                g_modes[j].hz == hz) {
                dup = 1;
                break;
            }
        }
        if (dup) continue;

        g_modes[g_nmodes].w = (int)dm.dmPelsWidth;
        g_modes[g_nmodes].h = (int)dm.dmPelsHeight;
        g_modes[g_nmodes].hz = hz;
        g_modes[g_nmodes].aspect = px_aspect_of(g_modes[g_nmodes].w, g_modes[g_nmodes].h);
        g_modes[g_nmodes].native = 0;
        g_modes[g_nmodes].supported = 1;
        g_nmodes++;
    }
    qsort(g_modes, (size_t)g_nmodes, sizeof g_modes[0], mode_cmp);

    /* Identify current & native mode */
    memset(&dm, 0, sizeof dm);
    dm.dmSize = sizeof dm;
    if (EnumDisplaySettingsW(g_active_dev, ENUM_CURRENT_SETTINGS, &dm)) {
        g_cur_mode.w = (int)dm.dmPelsWidth;
        g_cur_mode.h = (int)dm.dmPelsHeight;
        g_cur_mode.hz = (int)dm.dmDisplayFrequency;
        if (g_cur_mode.hz < 24) g_cur_mode.hz = 60;
        g_cur_mode.aspect = px_aspect_of(g_cur_mode.w, g_cur_mode.h);
        g_cur_mode.native = 1;

        for (int j = 0; j < g_nmodes; j++) {
            if (g_modes[j].w == g_cur_mode.w &&
                g_modes[j].h == g_cur_mode.h &&
                g_modes[j].hz == g_cur_mode.hz) {
                g_modes[j].native = 1;
            }
        }
    }

    /* DisplayState refresh: GPU name + per-output HDR / color space (real APIs) */
    memset(&g_gpu, 0, sizeof g_gpu);
    g_gpu.mag_available = g_pipe_mag;
    g_gpu.gamma_available = (g_pipe_ramps > 0);
    collect_driver_version();
    collect_dxgi_state();
    g_gpu.hdr_detected = 0;
    for (int i = 0; i < g_nmonitors; i++)
        if (g_monitors[i].hdr_enabled) { g_gpu.hdr_detected = 1; break; }

    return g_nmodes;
}

/* The color pipeline announces what output paths it actually got running. */
void Dm_ReportOutputs(int mag_available, int ramp_displays)
{
    g_pipe_mag = mag_available;
    g_pipe_ramps = ramp_displays;
    g_gpu.mag_available = mag_available;
    g_gpu.gamma_available = (ramp_displays > 0);
}

const GpuInfo *Dm_GpuInfo(void) { return &g_gpu; }

int Dm_HdrAny(void)
{
    for (int i = 0; i < g_nmonitors; i++)
        if (g_monitors[i].hdr_enabled) return 1;
    return 0;
}

/* Index of the monitor whose rect contains (or is nearest to) r; -1 = none. */
int Modes_FindMonitorForRect(const RECT *r)
{
    if (!r) return -1;
    long best = -1;
    int best_i = -1;
    for (int i = 0; i < g_nmonitors; i++) {
        RECT m = g_monitors[i].rc;
        long ix = (r->right < m.right ? r->right : m.right) - (r->left > m.left ? r->left : m.left);
        long iy = (r->bottom < m.bottom ? r->bottom : m.bottom) - (r->top > m.top ? r->top : m.top);
        long area = (ix > 0 && iy > 0) ? ix * iy : 0;
        if (area > best) { best = area; best_i = i; }
    }
    return best_i >= 0 ? best_i : 0;
}

int Modes_Count(void) { return g_nmodes; }
ModeInfo *Modes_Get(int i) { return (i >= 0 && i < g_nmodes) ? &g_modes[i] : NULL; }
int Modes_Current(ModeInfo *out) { *out = g_cur_mode; return 0; }

int Modes_MonitorCount(void) { return g_nmonitors; }
MonitorInfo *Modes_GetMonitor(int i) { return (i >= 0 && i < g_nmonitors) ? &g_monitors[i] : NULL; }
int Modes_CurrentMonitorIndex(void) { return g_cur_monitor; }
void Modes_SetCurrentMonitor(int idx)
{
    if (idx >= 0 && idx < g_nmonitors) {
        g_cur_monitor = idx;
        Modes_Refresh();
    }
}

/* ---------------- Safe Display Mode Application ---------------- */
int Modes_Apply(int idx)
{
    if (idx < 0 || idx >= g_nmodes) return -1;

    DEVMODEW dm;
    memset(&dm, 0, sizeof dm);
    dm.dmSize = sizeof dm;
    if (!EnumDisplaySettingsW(g_active_dev, ENUM_CURRENT_SETTINGS, &dm)) return -1;

    dm.dmPelsWidth = (DWORD)g_modes[idx].w;
    dm.dmPelsHeight = (DWORD)g_modes[idx].h;
    dm.dmDisplayFrequency = (DWORD)g_modes[idx].hz;
    dm.dmFields = DM_PELSWIDTH | DM_PELSHEIGHT | DM_DISPLAYFREQUENCY;

    /* Rollback protection: verify mode with CDS_TEST first */
    LONG test_res = ChangeDisplaySettingsExW(g_active_dev, &dm, NULL, CDS_TEST, NULL);
    if (test_res != DISP_CHANGE_SUCCESSFUL) {
        return -2; /* Mode not supported or rejected by driver */
    }

    /* Apply with registry update */
    LONG r = ChangeDisplaySettingsExW(g_active_dev, &dm, NULL, CDS_UPDATEREGISTRY, NULL);
    if (r != DISP_CHANGE_SUCCESSFUL && r != DISP_CHANGE_BADFLAGS) return -1;

    Modes_Refresh();
    return 0;
}

int Modes_ApplyMaxHz(void)
{
    int idx = px_modes_pick_best(g_modes, g_nmodes, g_cur_mode.w, g_cur_mode.h, 0);
    return idx >= 0 ? Modes_Apply(idx) : -1;
}

int Modes_ApplyNative(void)
{
    for (int i = 0; i < g_nmodes; i++) {
        if (g_modes[i].native) return Modes_Apply(i);
    }
    return -1;
}

int Modes_ApplyRes(int target_w, int target_h)
{
    int idx = px_modes_pick_best(g_modes, g_nmodes, target_w, target_h, 0);
    return idx >= 0 ? Modes_Apply(idx) : -1;
}

/* Resolution + refresh together (used by per-game display preferences).
 * A 0 target means "match the current value". */
int Modes_ApplyResHz(int target_w, int target_h, int target_hz)
{
    int i, best = -1;
    int w = target_w > 0 ? target_w : g_cur_mode.w;
    int h = target_h > 0 ? target_h : g_cur_mode.h;
    int hz = target_hz > 0 ? target_hz : 0;

    for (i = 0; i < g_nmodes; i++) {
        if (g_modes[i].w != w || g_modes[i].h != h) continue;
        if (hz > 0 && g_modes[i].hz != hz) continue;
        if (best < 0 || g_modes[i].hz > g_modes[best].hz) best = i;
    }
    if (best < 0 && target_hz > 0) return Modes_ApplyRes(w, h);  /* exact Hz absent */
    return best >= 0 ? Modes_Apply(best) : -1;
}

/* ---------------- Confirm-or-rollback display change ------------------------
 * A resolution/refresh change can leave an unfamiliar or even unusable picture
 * (unsupported scaling, black screen).  Windows gives user-mode code no timer
 * that survives a dead desktop, so PlexusX does the next best thing:
 *
 *   1. remember the exact current DEVMODE;
 *   2. apply the requested mode with CDS_UPDATEREGISTRY;
 *   3. arm a countdown (the UI paints a "Keep this mode / Revert" bar);
 *   4. if the user does not confirm in time — or the app cannot repaint — the
 *      previous DEVMODE is written back with ChangeDisplaySettingsExW.
 *
 * The rollback also fires from the tray "Restore display" action and from
 * Main_EmergencyReset(), so a stuck mode always has an exit. */
static DEVMODEW g_prev_dm;
static int      g_have_prev = 0;
static int      g_pending_idx = -1;
static DWORD    g_pending_deadline = 0;
static int      g_pending_timeout = 0;

/* ---- crash-safe confirm-or-rollback ------------------------------------
 * The DEVMODE we are leaving is also written to <state>\display.pending before
 * the change reaches the driver, and removed again when the user confirms or
 * the rollback fires.  A display mode survives a dead process (it lives in the
 * driver / registry), so without the marker a crash, a task-manager kill or a
 * power cut during the countdown would strand the user in a mode they never
 * confirmed.  On the next start Modes_RecoverPendingFromDisk() finds the marker
 * and puts the recorded mode back. */
static wchar_t g_state_dir[MAX_PATH];

void Modes_SetStateDir(const wchar_t *dir)
{
    if (!dir) { g_state_dir[0] = 0; return; }
    lstrcpynW(g_state_dir, dir, MAX_PATH);
}

static int pending_path(wchar_t *out, int cap)
{
    if (!g_state_dir[0] || cap < 40) return 0;
    wsprintfW(out, L"%s\\display.pending", g_state_dir);
    return 1;
}

/* {magic, device, DEVMODE} — small, fixed size, versioned by the magic. */
#define PX_PEND_MAGIC 0x50584450u   /* "PXDP" */
typedef struct {
    DWORD    magic;
    DWORD    size;
    wchar_t  dev[32];
    DEVMODEW dm;
} PxPendingRec;

static void pending_write(void)
{
    PxPendingRec rec;
    wchar_t path[MAX_PATH];
    HANDLE h;
    DWORD wr = 0;

    if (!g_have_prev || !pending_path(path, MAX_PATH)) return;
    memset(&rec, 0, sizeof rec);
    rec.magic = PX_PEND_MAGIC;
    rec.size = (DWORD)sizeof rec;
    lstrcpynW(rec.dev, g_active_dev, 32);
    rec.dm = g_prev_dm;
    h = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return;
    WriteFile(h, &rec, (DWORD)sizeof rec, &wr, NULL);
    FlushFileBuffers(h);
    CloseHandle(h);
}

static void pending_clear(void)
{
    wchar_t path[MAX_PATH];
    if (pending_path(path, MAX_PATH)) DeleteFileW(path);
}

int Modes_RecoverPendingFromDisk(void)
{
    PxPendingRec rec;
    wchar_t path[MAX_PATH];
    HANDLE h;
    DWORD rd = 0;
    LONG r;

    if (!pending_path(path, MAX_PATH)) return 0;
    h = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return 0;
    memset(&rec, 0, sizeof rec);
    if (!ReadFile(h, &rec, (DWORD)sizeof rec, &rd, NULL) || rd != sizeof rec ||
        rec.magic != PX_PEND_MAGIC || rec.size != (DWORD)sizeof rec) {
        CloseHandle(h);
        DeleteFileW(path);            /* unreadable marker: nothing safe to restore */
        return 0;
    }
    CloseHandle(h);

    rec.dm.dmFields = DM_PELSWIDTH | DM_PELSHEIGHT | DM_DISPLAYFREQUENCY;
    r = ChangeDisplaySettingsExW(rec.dev[0] ? rec.dev : g_active_dev, &rec.dm, NULL,
                                 CDS_UPDATEREGISTRY, NULL);
    Modes_Refresh();
    DeleteFileW(path);
    Eng_Log("disp", "recovered unconfirmed display mode from a previous session: %lux%lu@%lu (%s)",
            (unsigned long)rec.dm.dmPelsWidth, (unsigned long)rec.dm.dmPelsHeight,
            (unsigned long)rec.dm.dmDisplayFrequency,
            r == DISP_CHANGE_SUCCESSFUL ? "restored" : "rejected");
    return 1;
}

int Modes_ApplySafe(int idx, int timeout_ms)
{
    if (idx < 0 || idx >= g_nmodes) return -1;

    /* remember the mode we are leaving */
    memset(&g_prev_dm, 0, sizeof g_prev_dm);
    g_prev_dm.dmSize = sizeof g_prev_dm;
    if (EnumDisplaySettingsW(g_active_dev, ENUM_CURRENT_SETTINGS, &g_prev_dm))
        g_have_prev = 1;
    pending_write();               /* arm the crash-safe marker BEFORE the change */

    if (Modes_Apply(idx) != 0) { pending_clear(); return -1; }

    g_pending_idx = idx;
    g_pending_timeout = clampi(timeout_ms, 3000, 60000);
    g_pending_deadline = GetTickCount() + (DWORD)g_pending_timeout;
    Eng_Log("disp", "mode change pending confirmation: %dx%d@%d (%d ms)",
            g_modes[idx].w, g_modes[idx].h, g_modes[idx].hz, g_pending_timeout);
    return 0;
}

int Modes_PendingChange(void) { return g_pending_idx >= 0; }

int Modes_PendingSecondsLeft(void)
{
    LONG left;
    if (g_pending_idx < 0) return 0;
    left = (LONG)(g_pending_deadline - GetTickCount());
    return left > 0 ? (int)(left / 1000) + 1 : 0;
}

int Modes_PendingMode(char *label, int cap)
{
    if (!label || cap <= 0) return 0;
    label[0] = 0;
    if (g_pending_idx < 0 || g_pending_idx >= g_nmodes) return 0;
    snprintf(label, (size_t)cap, "%dx%d @ %d Hz", g_modes[g_pending_idx].w,
             g_modes[g_pending_idx].h, g_modes[g_pending_idx].hz);
    return 1;
}

/* Restore the remembered mode.  Returns 1 when a rollback really happened. */
int Modes_RollbackPending(void)
{
    LONG r;
    if (g_pending_idx < 0) return 0;
    g_pending_idx = -1;
    pending_clear();               /* the user is out of the danger zone either way */
    if (!g_have_prev) return 0;
    g_prev_dm.dmFields = DM_PELSWIDTH | DM_PELSHEIGHT | DM_DISPLAYFREQUENCY;
    r = ChangeDisplaySettingsExW(g_active_dev, &g_prev_dm, NULL, CDS_UPDATEREGISTRY, NULL);
    Modes_Refresh();
    Eng_Log("disp", "display mode rolled back to %lux%lu@%lu: %s",
            (unsigned long)g_prev_dm.dmPelsWidth, (unsigned long)g_prev_dm.dmPelsHeight,
            (unsigned long)g_prev_dm.dmDisplayFrequency, r == DISP_CHANGE_SUCCESSFUL ? "ok" : "rejected");
    return 1;
}

int Modes_ConfirmPending(void)
{
    if (g_pending_idx < 0) return 0;
    g_pending_idx = -1;
    pending_clear();               /* kept: the marker must not undo it next start */
    Eng_Log("disp", "display mode change confirmed by the user");
    return 1;
}

/* Called from the UI timer: fires the rollback when the deadline passes and
 * the user never confirmed. */
int Modes_RollbackTick(void)
{
    if (g_pending_idx < 0) return 0;
    if ((LONG)(GetTickCount() - g_pending_deadline) < 0) return 0;
    return Modes_RollbackPending();
}

void Modes_OpenHdrSettings(void)
{
    ShellExecuteW(NULL, L"open", L"ms-settings:display", NULL, NULL, SW_SHOWNORMAL);
}

/* ---------------- Monitor Identification Overlay ---------------- */
static LRESULT CALLBACK id_overlay_proc(HWND wnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(wnd, &ps);
        RECT rc;
        GetClientRect(wnd, &rc);
        HBRUSH bg = CreateSolidBrush(RGB(15, 15, 20));
        FillRect(dc, &rc, bg);
        DeleteObject(bg);

        HPEN pen = CreatePen(PS_SOLID, 4, RGB(198, 255, 61));
        HGDIOBJ op = SelectObject(dc, pen);
        SelectObject(dc, GetStockObject(NULL_BRUSH));
        Rectangle(dc, rc.left + 2, rc.top + 2, rc.right - 2, rc.bottom - 2);
        SelectObject(dc, op);
        DeleteObject(pen);

        HFONT font = CreateFontW(-72, 0, 0, 0, FW_BOLD, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
        HGDIOBJ of = SelectObject(dc, font);
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, RGB(255, 255, 255));

        int id = (int)GetWindowLongPtrW(wnd, GWLP_USERDATA);
        wchar_t buf[32];
        wsprintfW(buf, L"DISPLAY %d", id + 1);
        DrawTextW(dc, buf, -1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

        SelectObject(dc, of);
        DeleteObject(font);
        EndPaint(wnd, &ps);
        return 0;
    }
    case WM_TIMER:
        DestroyWindow(wnd);
        return 0;
    }
    return DefWindowProcW(wnd, msg, wp, lp);
}

void Modes_IdentifyMonitors(void)
{
    static int registered = 0;
    if (!registered) {
        WNDCLASSW wc;
        memset(&wc, 0, sizeof wc);
        wc.lpfnWndProc = id_overlay_proc;
        wc.hInstance = g_inst;
        wc.lpszClassName = L"PlexusXIdentifyWnd";
        RegisterClassW(&wc);
        registered = 1;
    }

    for (int i = 0; i < g_nmonitors; i++) {
        MonitorInfo *m = &g_monitors[i];
        int w = 320, h = 180;
        int x = (m->rc.left + m->rc.right) / 2 - w / 2;
        int y = (m->rc.top + m->rc.bottom) / 2 - h / 2;
        HWND wnd = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
                                   L"PlexusXIdentifyWnd", L"", WS_POPUP | WS_VISIBLE,
                                   x, y, w, h, NULL, NULL, g_inst, NULL);
        if (wnd) {
            SetWindowLongPtrW(wnd, GWLP_USERDATA, (LONG_PTR)i);
            SetTimer(wnd, 1, 2500, NULL);
        }
    }
}
