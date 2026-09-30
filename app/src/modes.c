/* PlexusX — Display, Refresh Rate, Stretched 4:3 Modes & HDR Manager
 * Legit Windows display APIs only. No driver hacking.
 */
#include "common.h"

#define MAX_MODES    128
#define MAX_MONITORS 8

static ModeInfo    g_modes[MAX_MODES];
static int         g_nmodes = 0;
static ModeInfo    g_cur_mode;

static MonitorInfo g_monitors[MAX_MONITORS];
static int         g_nmonitors = 0;
static int         g_cur_monitor = 0;

static wchar_t     g_active_dev[32];

static int detect_aspect(int w, int h)
{
    if (w * 9 == h * 16) return 0;                     /* 16:9 */
    if ((long)w * 3 == (long)h * 4) return 1;          /* 4:3 */
    if (w * 10 == h * 16) return 2;                    /* 16:10 */
    if (w * 9 >= h * 21) return 3;                     /* Ultrawide 21:9 or 32:9 */
    return 4;                                          /* Other */
}

static int mode_cmp(const void *a, const void *b)
{
    const ModeInfo *x = a, *y = b;
    long ax = (long)x->w * x->h, ay = (long)y->w * y->h;
    if (ax != ay) return (int)(ay - ax);
    return y->hz - x->hz;
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
    m->bpc = 8;

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
        m->bpc = (int)dm.dmBitsPerPel;
    }

    return TRUE;
}

int Modes_Refresh(void)
{
    g_nmonitors = 0;
    EnumDisplayMonitors(NULL, NULL, enum_mon_proc, 0);
    if (!g_nmonitors) {
        /* Fallback */
        lstrcpyW(g_monitors[0].dev_name, L"\\\\.\\DISPLAY1");
        lstrcpyW(g_monitors[0].friendly, L"Primary Display");
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
        g_modes[g_nmodes].aspect = detect_aspect(g_modes[g_nmodes].w, g_modes[g_nmodes].h);
        g_modes[g_nmodes].native = 0;
        g_modes[g_nmodes].supported = 1;
        g_nmodes++;
    }
    qsort(g_modes, g_nmodes, sizeof g_modes[0], mode_cmp);

    /* Identify current & native mode */
    memset(&dm, 0, sizeof dm);
    dm.dmSize = sizeof dm;
    if (EnumDisplaySettingsW(g_active_dev, ENUM_CURRENT_SETTINGS, &dm)) {
        g_cur_mode.w = (int)dm.dmPelsWidth;
        g_cur_mode.h = (int)dm.dmPelsHeight;
        g_cur_mode.hz = (int)dm.dmDisplayFrequency;
        if (g_cur_mode.hz < 24) g_cur_mode.hz = 60;
        g_cur_mode.aspect = detect_aspect(g_cur_mode.w, g_cur_mode.h);
        g_cur_mode.native = 1;

        for (int j = 0; j < g_nmodes; j++) {
            if (g_modes[j].w == g_cur_mode.w &&
                g_modes[j].h == g_cur_mode.h &&
                g_modes[j].hz == g_cur_mode.hz) {
                g_modes[j].native = 1;
            }
        }
    }
    return g_nmodes;
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

    dm.dmPelsWidth = g_modes[idx].w;
    dm.dmPelsHeight = g_modes[idx].h;
    dm.dmDisplayFrequency = g_modes[idx].hz;
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
    /* Find mode with current width & height and highest available Hz */
    int best_idx = -1;
    int max_hz = 0;
    for (int i = 0; i < g_nmodes; i++) {
        if (g_modes[i].w == g_cur_mode.w && g_modes[i].h == g_cur_mode.h) {
            if (g_modes[i].hz > max_hz) {
                max_hz = g_modes[i].hz;
                best_idx = i;
            }
        }
    }
    if (best_idx >= 0) return Modes_Apply(best_idx);
    return -1;
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
    int best_idx = -1;
    int max_hz = 0;
    for (int i = 0; i < g_nmodes; i++) {
        if (g_modes[i].w == target_w && g_modes[i].h == target_h) {
            if (g_modes[i].hz > max_hz) {
                max_hz = g_modes[i].hz;
                best_idx = i;
            }
        }
    }
    if (best_idx >= 0) return Modes_Apply(best_idx);
    return -1;
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
