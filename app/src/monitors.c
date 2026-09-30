/* monitors.c \u2014 enumerate connected displays with stable EDID identity
 *
 * Identity is built from EDID vendor/model/serial (GetMonitorID +
 * PHYSICAL_MONITOR), never from the unstable \\.\DISPLAYn index.
 */
#include "common.h"

/* Physical-monitor EDID APIs (Win8+).  Not present in every bundled header
 * set, so the ABI-stable fragments are vendored here (MS SDK layout). */
typedef void *HPHYSICALMONITOR;

/* exact MS SDK PHYSICAL_MONITOR layout (Win8+, ABI-stable) */
typedef struct _CX_PHYSICAL_MONITOR {
    HPHYSICALMONITOR hPhysicalMonitor;
    UINT             dwFlags;
    WORD             wWidth;
    WORD             wHeight;
    WCHAR            szMonitor[128];
    WCHAR            szManufactureName[128];
} CX_PHYSICAL_MONITOR;

typedef struct _CX_MONITORIDW {
    DWORD dwAssembledId;
    DWORD idVendorId;
    DWORD idProductCode;
    DWORD idSerialNumber;
    DWORD dwManufactureYear;
} CX_MONITORIDW;

/* Resolved at runtime: the bundled import libraries predate these exports,
 * and older systems (Win7) do not have them at all. */
typedef int  (WINAPI *FnGetMonitorID)(HMONITOR hMonitor, CX_MONITORIDW *lpid);
typedef LONG (WINAPI *FnGetPhysMonitors)(HMONITOR hMonitor,
               DWORD dwPhysicalMonitorArraySize, HPHYSICALMONITOR *lpaPhysicalMonitors,
               DWORD *pdwPhysicalMonitorCount);
typedef BOOL (WINAPI *FnClosePhysMonitors)(DWORD dwPhysicalMonitorArraySize,
               HPHYSICALMONITOR *lpaPhysicalMonitors);
typedef LONG (WINAPI *FnGetPhysMonitorInfo)(CX_PHYSICAL_MONITOR *pMonitor);

static FnGetMonitorID     fnGetMonitorID;
static FnGetPhysMonitors  fnGetPhysMonitors;
static FnClosePhysMonitors fnClosePhysMonitors;
static FnGetPhysMonitorInfo fnGetPhysMonitorInfo;

static void mon_phys_init(void)
{
    static int done = 0;
    if (done) return;
    done = 1;
    HMODULE u32 = GetModuleHandleW(L"user32.dll");
    if (!u32) return;
    fnGetMonitorID      = (FnGetMonitorID)(void *)GetProcAddress(u32, "GetMonitorIDW");
    fnGetPhysMonitors   = (FnGetPhysMonitors)(void *)GetProcAddress(u32, "GetPhysicalMonitorsFromHMONITORW");
    fnClosePhysMonitors = (FnClosePhysMonitors)(void *)GetProcAddress(u32, "ClosePhysicalMonitors");
    fnGetPhysMonitorInfo = (FnGetPhysMonitorInfo)(void *)GetProcAddress(u32, "GetPhysicalMonitorInfo");
}

#define MAX_MON 8

static MonInfo g_mon[MAX_MON];
static int     g_nmon;
static char g_gpu[128];

static void mon_fill_phys(MonInfo *m, HMONITOR h)
{
    m->manufacturer[0] = m->model[0] = m->serial[0] = 0;
    m->width_mm = m->height_mm = 0;
    mon_phys_init();

    HPHYSICALMONITOR hpm[2] = { 0, 0 };
    DWORD n = 0;
    if (fnGetPhysMonitors &&
        fnGetPhysMonitors(h, 0, NULL, &n) == DISP_CHANGE_SUCCESSFUL &&
        n > 0 && n <= 2) {
        if (fnGetPhysMonitors(h, n, hpm, &n) == DISP_CHANGE_SUCCESSFUL) {
            CX_PHYSICAL_MONITOR pm;
            memset(&pm, 0, sizeof pm);
            pm.hPhysicalMonitor = hpm[0];
            if (fnGetPhysMonitorInfo &&
                fnGetPhysMonitorInfo(&pm) == DISP_CHANGE_SUCCESSFUL) {
                /* szManufactureName/szMonitor are already wide (WCHAR[128]) */
                lstrcpynW(m->manufacturer, pm.szManufactureName, 64);
                lstrcpynW(m->model, pm.szMonitor, 48);
                m->width_mm  = (int)pm.wWidth;
                m->height_mm = (int)pm.wHeight;
            }
            if (fnClosePhysMonitors) fnClosePhysMonitors(1, hpm);
        }
    }

    CX_MONITORIDW mid;
    if (fnGetMonitorID && fnGetMonitorID(h, &mid) == 0 && mid.dwManufactureYear != 0) {
        wchar_t s[32];
        wsprintfW(s, L"%04x%04x-%08x", mid.idVendorId, mid.idProductCode,
                  mid.idSerialNumber);
        lstrcpynW(m->serial, s + 8, 32);
        if (!m->manufacturer[0]) wsprintfW(m->manufacturer, L"Vendor %04X", mid.idVendorId);
        if (!m->model[0])        wsprintfW(m->model,        L"Product %04X", mid.idProductCode);
    }
    if (!m->manufacturer[0]) lstrcpyW(m->manufacturer, L"Unknown");
    if (!m->model[0])        lstrcpyW(m->model, L"Monitor");
}

typedef struct { MonInfo *arr; int n, cap; } MonEnumCtx;

static BOOL CALLBACK mon_enum_cb(HMONITOR h, HDC hdc, LPRECT rc, LPARAM lp)
{
    MonEnumCtx *c = (MonEnumCtx *)lp;
    if (c->n >= c->cap) return TRUE;
    MonInfo *m = &c->arr[c->n];

    MONITORINFOEXW mi;
    mi.cbSize = sizeof mi;
    if (!GetMonitorInfoW(h, (MONITORINFO *)&mi)) return TRUE;

    memset(m, 0, sizeof *m);
    lstrcpynW(m->device, mi.szDevice, 32);
    m->x = rc->left; m->y = rc->top;
    m->w = rc->right - rc->left; m->h = rc->bottom - rc->top;
    m->primary = (mi.dwFlags & MONITORINFOF_PRIMARY) ? 1 : 0;
    m->portrait = (m->h > m->w) ? 1 : 0;

    HDC dc = hdc ? hdc : GetDC(NULL);
    m->dpi = GetDeviceCaps(dc, LOGPIXELSX);
    if (!hdc) ReleaseDC(NULL, dc);

    mon_fill_phys(m, h);

    /* stable identity: manufacture|model|serial \u2014 never the display index */
    if (m->serial[0])
        wsprintfW(m->id, L"%s|%s|%s", m->manufacturer, m->model, m->serial);
    else
        wsprintfW(m->id, L"%s|%s", m->manufacturer, m->model);
    for (wchar_t *p = m->id; *p; p++) *p = (wchar_t)towlower(*p);

    wsprintfW(m->name, L"%s %s", m->manufacturer, m->model);

    /* live mode */
    DEVMODEW dm;
    memset(&dm, 0, sizeof dm);
    dm.dmSize = sizeof dm;
    if (EnumDisplaySettingsW(m->device, ENUM_CURRENT_SETTINGS, &dm)) {
        m->res_w = (int)dm.dmPelsWidth;
        m->res_h = (int)dm.dmPelsHeight;
        m->hz    = (int)dm.dmDisplayFrequency;
        m->bpc   = (int)dm.dmBitsPerPel;
    }
    c->n++;
    return TRUE;
}

int MonRefresh(void)
{
    MonEnumCtx c = { g_mon, 0, MAX_MON };
    g_nmon = 0;
    EnumDisplayMonitors(NULL, NULL, mon_enum_cb, (LPARAM)&c);
    g_nmon = c.n;
    return g_nmon;
}

int MonCount(void) { return g_nmon; }
const MonInfo *MonGet(int i) { return (i >= 0 && i < g_nmon) ? &g_mon[i] : NULL; }

int MonFindDevice(const wchar_t *device)
{
    for (int i = 0; i < g_nmon; i++)
        if (_wcsicmp(g_mon[i].device, device) == 0) return i;
    return -1;
}

const char *MonGpuName(void)
{
    if (!g_gpu[0]) {
        g_gpu[0] = 0;
        DISPLAY_DEVICEW dd;
        memset(&dd, 0, sizeof dd);
        dd.cb = sizeof dd;
        if (EnumDisplayDevicesW(NULL, 0, &dd, 0) && dd.DeviceString[0]) {
            int n = WideCharToMultiByte(CP_UTF8, 0, dd.DeviceString, -1,
                                        g_gpu, 127, NULL, NULL);
            if (n > 0) g_gpu[n - 1] = 0;
        }
        if (!g_gpu[0]) strcpy(g_gpu, "Unknown GPU");
    }
    return g_gpu;
}

void MonSetHdr(int i, int capable, int on, int sdrWhite)
{
    if (i >= 0 && i < g_nmon) {
        g_mon[i].hdr_capable = capable;
        g_mon[i].hdr_on      = on;
        g_mon[i].sdr_white   = sdrWhite;
    }
}
