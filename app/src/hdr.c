/* hdr.c — HDR discovery + toggling via the DisplayConfig advanced-colour APIs.
 *
 * Windows exposes HDR ("advanced colour") as a per-target property queried and
 * set through user32's DisplayConfig* functions.  Everything here is real,
 * driver-backed state: if the driver/OS does not expose the property we report
 * "not capable" and refuse to fake a toggle.
 *
 *   - QueryDisplayConfig(QDC_ONLY_ACTIVE_PATHS)  -> active targets
 *   - DISPLAYCONFIG_DEVICE_INFO_GET_TARGET_NAME            (edid + friendly name)
 *   - DISPLAYCONFIG_DEVICE_INFO_GET_ADVANCED_COLOR_INFO    (capable / enabled)
 *   - DISPLAYCONFIG_DEVICE_INFO_GET_SDR_WHITE_LEVEL        (SDR white, cd/m)
 *   - DISPLAYCONFIG_DEVICE_INFO_SET_ADVANCED_COLOR_STATE   (HDR on/off)
 *
 * The bundled headers carry the modern (Win10 1903+) DisplayConfig layouts
 * (single-id device-info header), which is what current Windows expects.
 */
#include "common.h"

#define CX_MAX_HDR 8

typedef struct HdrTarget {
    LUID    adapterId;
    UINT32  id;
    wchar_t name[64];          /* EDID friendly name */
    int     avail;             /* target physically present */
    int     capable;           /* driver exposes advanced colour */
    int     enabled;           /* HDR currently on */
    int     sdr_sup;           /* SDR white level readable */
    UINT32  sdr;               /* SDR white level (cd/m) */
    int     monIdx;            /* mapped PlexusX monitor, -1 if none */
} HdrTarget;

static HdrTarget g_t[CX_MAX_HDR];
static int       g_nt;
static int       g_api_ok;    /* QueryDisplayConfig returned an answer */

static void hdr_scan(void);

/* Win10 1607+ is the floor for the advanced-colour API.  RtlGetVersion is
 * not version-spoofed, unlike GetVersionEx. */
static int os_supports(void)
{
    static int r = -1;
    if (r < 0) {
        typedef BOOL (WINAPI *RtlGetVersionFn)(OSVERSIONINFOW *);
        HMODULE nt = GetModuleHandleW(L"ntdll.dll");
        RtlGetVersionFn fn = nt ? (RtlGetVersionFn)GetProcAddress(nt, "RtlGetVersion") : NULL;
        OSVERSIONINFOW v;
        memset(&v, 0, sizeof v);
        v.dwOSVersionInfoSize = sizeof v;
        r = (fn && fn(&v) == 0 &&
             (v.dwMajorVersion > 10 || (v.dwMajorVersion == 10 && v.dwBuildNumber >= 1607)))
                ? 1 : 0;
    }
    return r;
}

static void hdr_scan(void)
{
    g_nt = 0;
    g_api_ok = 0;
    if (!os_supports()) return;

    UINT32 np = 0, nm = 0;
    if (GetDisplayConfigBufferSizes(QDC_ONLY_ACTIVE_PATHS, &np, &nm) != 0 || np == 0)
        return;

    DISPLAYCONFIG_PATH_INFO *paths =
        (DISPLAYCONFIG_PATH_INFO *)malloc(sizeof(DISPLAYCONFIG_PATH_INFO) * np);
    if (!paths) return;
    UINT32 np2 = np, nm2 = nm;
    LONG rc = QueryDisplayConfig(QDC_ONLY_ACTIVE_PATHS, &np2, paths, &nm2, NULL, NULL);
    if (rc != 0 || np2 == 0) { free(paths); return; }
    g_api_ok = 1;

    int nmon = MonCount();
    for (UINT32 i = 0; i < np2 && g_nt < CX_MAX_HDR; i++) {
        const DISPLAYCONFIG_PATH_TARGET_INFO *ti = &paths[i].targetInfo;
        HdrTarget *t = &g_t[g_nt];
        memset(t, 0, sizeof *t);
        t->adapterId = ti->adapterId;
        t->id        = ti->id;
        t->avail     = ti->targetAvailable ? 1 : 0;
        t->monIdx    = -1;
        g_nt++;
        if (!t->avail) continue;

        /* friendly name + EDID identity */
        DISPLAYCONFIG_TARGET_DEVICE_NAME nm1;
        memset(&nm1, 0, sizeof nm1);
        nm1.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_TARGET_NAME;
        nm1.header.size = sizeof nm1;
        nm1.header.adapterId = t->adapterId;
        nm1.header.id = t->id;
        if (DisplayConfigGetDeviceInfo((DISPLAYCONFIG_DEVICE_INFO_HEADER *)&nm1) == 0)
            lstrcpynW(t->name, nm1.monitorFriendlyDeviceName, 64);

        /* advanced colour (HDR) capability + state */
        DISPLAYCONFIG_GET_ADVANCED_COLOR_INFO ac;
        memset(&ac, 0, sizeof ac);
        ac.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_ADVANCED_COLOR_INFO;
        ac.header.size = sizeof ac;
        ac.header.adapterId = t->adapterId;
        ac.header.id = t->id;
        if (DisplayConfigGetDeviceInfo((DISPLAYCONFIG_DEVICE_INFO_HEADER *)&ac) == 0) {
            t->capable = ac.advancedColorSupported ? 1 : 0;
            t->enabled = ac.advancedColorEnabled ? 1 : 0;
        } else {
            /* driver does not expose the property — report honestly */
            t->capable = 0;
            t->enabled = 0;
        }

        /* SDR white level (read-only; setters land in Win11 24H2) */
        DISPLAYCONFIG_SDR_WHITE_LEVEL sw;
        memset(&sw, 0, sizeof sw);
        sw.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_SDR_WHITE_LEVEL;
        sw.header.size = sizeof sw;
        sw.header.adapterId = t->adapterId;
        sw.header.id = t->id;
        if (DisplayConfigGetDeviceInfo((DISPLAYCONFIG_DEVICE_INFO_HEADER *)&sw) == 0) {
            t->sdr_sup = 1;
            t->sdr = sw.SDRWhiteLevel;
        }

        /* map back to a PlexusX monitor: EDID friendly name first, ordinal second.
         * MonInfo.model carries the EDID PnP name (szMonitor), so compare it. */
        for (int m = 0; m < nmon; m++) {
            const MonInfo *mi = MonGet(m);
            if (!mi || !mi->model[0] || !t->name[0]) continue;
            if (_wcsicmp(mi->model, t->name) == 0) { t->monIdx = m; break; }
        }
        if (t->monIdx < 0) {
            for (int m = 0; m < nmon; m++) {
                int taken = 0;
                for (int k = 0; k < g_nt; k++) if (g_t[k].monIdx == m) taken = 1;
                if (!taken) { t->monIdx = m; break; }
            }
        }
    }
    free(paths);

    /* push state into MonInfo for diagnostics */
    for (int i = 0; i < g_nt; i++)
        if (g_t[i].monIdx >= 0)
            MonSetHdr(g_t[i].monIdx, g_t[i].capable, g_t[i].enabled,
                      g_t[i].sdr_sup ? (int)g_t[i].sdr : 0);
}

static HdrTarget *target_for_monitor(int monitor)
{
    if (monitor < 0) monitor = 0;
    for (int i = 0; i < g_nt; i++)
        if (g_t[i].monIdx == monitor && g_t[i].avail) return &g_t[i];
    if (monitor == 0) {
        for (int i = 0; i < g_nt; i++)
            if (g_t[i].avail) return &g_t[i];
    }
    return NULL;
}

static void fill_state(const HdrTarget *t, HdrState *out)
{
    out->supported = os_supports();
    out->found     = t ? 1 : 0;
    out->capable   = t ? t->capable : 0;
    out->enabled   = t ? t->enabled : 0;
    out->sdr_white_supported = t ? t->sdr_sup : 0;
    out->sdr_white           = t && t->sdr_sup ? (float)t->sdr : 0.0f;
}

void Hdr_Scan(HdrState *out)
{
    hdr_scan();
    /* prefer the primary monitor's target, else the first capable one */
    HdrTarget *pick = NULL;
    for (int i = 0; i < g_nt; i++)
        if (g_t[i].avail && g_t[i].monIdx == 0 && g_t[i].capable) { pick = &g_t[i]; break; }
    if (!pick)
        for (int i = 0; i < g_nt; i++)
            if (g_t[i].avail && g_t[i].capable) { pick = &g_t[i]; break; }
    fill_state(pick, out);
}

void Hdr_ScanFor(int monitor, HdrState *out)
{
    hdr_scan();
    fill_state(target_for_monitor(monitor), out);
}

int Hdr_SetEnabled(int monitor, int on)
{
    hdr_scan();
    HdrTarget *t = target_for_monitor(monitor);
    if (!t || !t->capable) return -1;   /* not supported by this driver/display */
    if (t->enabled == on) return 0;

    DISPLAYCONFIG_SET_ADVANCED_COLOR_STATE s;
    memset(&s, 0, sizeof s);
    s.header.type = DISPLAYCONFIG_DEVICE_INFO_SET_ADVANCED_COLOR_STATE;
    s.header.size = sizeof s;
    s.header.adapterId = t->adapterId;
    s.header.id = t->id;
    s.value = on ? 1 : 0;

    LONG rc = DisplayConfigSetDeviceInfo((DISPLAYCONFIG_DEVICE_INFO_HEADER *)&s);
    if (rc != 0) return -1;

    t->enabled = on;
    if (t->monIdx >= 0) MonSetHdr(t->monIdx, t->capable, on, t->sdr_sup ? (int)t->sdr : 0);
    return 0;
}

int Hdr_SetSdrWhite(float nits)
{
    (void)nits;
    /* A public SDR-white setter only exists from Windows 11 24H2
     * (DISPLAYCONFIG_SET_HDR_STATE); it is not part of this SDK surface,
     * so we do not fake the control. */
    return -1;
}

void Hdr_OpenSettings(void)
{
    ShellExecuteW(NULL, L"open", L"ms-settings:display", NULL, NULL, SW_SHOWNORMAL);
}
