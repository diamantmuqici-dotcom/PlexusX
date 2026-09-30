/* modes.c \u2014 per-monitor resolution/refresh management
 *
 * Applies work through the same OS path Windows Settings uses
 * (ChangeDisplaySettingsEx with CDS_UPDATEREGISTRY): the OS shows its own
 * "Keep these display settings?" safety prompt and reverts automatically if
 * nobody answers.  We additionally pre-test with CDS_TEST before applying,
 * and roll back ourselves if the apply call fails.
 */
#include "common.h"

#define MAX_MODES_PER 192

static ModeInfo g_modes[8][MAX_MODES_PER];
static int      g_n[8];
static int      g_ok;   /* refresh done */

static int mode_cmp(const void *a, const void *b)
{
    const ModeInfo *x = a, *y = b;
    long ax = (long)x->w * x->h, ay = (long)y->w * y->h;
    if (ax != ay) return ay - ax;
    return y->hz - x->hz;
}

int Modes_Refresh(void)
{
    int nmon = MonCount();
    for (int m = 0; m < nmon && m < 8; m++) {
        const MonInfo *mi = MonGet(m);
        DEVMODEW cur;
        memset(&cur, 0, sizeof cur);
        cur.dmSize = sizeof cur;
        int have_cur = EnumDisplaySettingsW(mi->device, ENUM_CURRENT_SETTINGS, &cur);

        g_n[m] = 0;
        DEVMODEW dm;
        for (int i = 0; i < 4096 && g_n[m] < MAX_MODES_PER; i++) {
            memset(&dm, 0, sizeof dm);
            dm.dmSize = sizeof dm;
            if (!EnumDisplaySettingsExW(mi->device, i, &dm, EDS_RAWMODE)) break;
            int w = (int)dm.dmPelsWidth, h = (int)dm.dmPelsHeight;
            if (w < 640 || h < 480) continue;
            int hz = (int)dm.dmDisplayFrequency;
            if (hz < 24 || hz > 1000) hz = 60;
            int dup = 0;
            for (int j = 0; j < g_n[m]; j++)
                if (g_modes[m][j].w == w && g_modes[m][j].h == h && g_modes[m][j].hz == hz) {
                    dup = 1; break;
                }
            if (dup) continue;
            g_modes[m][g_n[m]].w = w;
            g_modes[m][g_n[m]].h = h;
            g_modes[m][g_n[m]].hz = hz;
            g_modes[m][g_n[m]].native =
                have_cur && w == (int)cur.dmPelsWidth && h == (int)cur.dmPelsHeight &&
                hz == (int)cur.dmDisplayFrequency;
            g_modes[m][g_n[m]].preferred = 0;
            g_n[m]++;
        }
        qsort(g_modes[m], g_n[m], sizeof g_modes[m][0], mode_cmp);

        /* update monitor live state */
        if (have_cur) {
            MonInfo *wmi = (MonInfo *)mi;   /* const-cast: live state is mutable */
            wmi->res_w = (int)cur.dmPelsWidth;
            wmi->res_h = (int)cur.dmPelsHeight;
            wmi->hz = (int)cur.dmDisplayFrequency;
            wmi->bpc = (int)cur.dmBitsPerPel;
        }
    }
    g_ok = 1;
    return g_ok;
}

int Modes_CountFor(int monitor)
{
    return (monitor >= 0 && monitor < 8) ? g_n[monitor] : 0;
}

ModeInfo *Modes_GetFor(int monitor, int i)
{
    if (monitor < 0 || monitor >= 8 || i < 0 || i >= g_n[monitor]) return NULL;
    return &g_modes[monitor][i];
}

int Modes_CurrentFor(int monitor, ModeInfo *out)
{
    if (!out || monitor < 0 || monitor >= 8) return -1;
    const MonInfo *mi = MonGet(monitor);
    if (!mi) return -1;
    out->w = mi->res_w; out->h = mi->res_h; out->hz = mi->hz;
    out->native = 1; out->preferred = 0;
    return 0;
}

int Modes_FindMode(int monitor, int w, int h, int hz)
{
    if (monitor < 0 || monitor >= 8) return -1;
    if (hz == 0 || hz < 0) {
        /* best (max refresh) mode for w×h */
        int best = -1, bestHz = -1;
        for (int i = 0; i < g_n[monitor]; i++)
            if (g_modes[monitor][i].w == w && g_modes[monitor][i].h == h &&
                g_modes[monitor][i].hz > bestHz) {
                best = i; bestHz = g_modes[monitor][i].hz;
            }
        return best;
    }
    for (int i = 0; i < g_n[monitor]; i++)
        if (g_modes[monitor][i].w == w && g_modes[monitor][i].h == h &&
            g_modes[monitor][i].hz == hz)
            return i;
    return -1;
}

int Modes_ApplySpec(int monitor, int w, int h, int hz)
{
    int idx = Modes_FindMode(monitor, w, h, hz);
    if (idx < 0) return -1;
    return Modes_Apply(monitor, idx);
}

int Modes_Apply(int monitor, int modeIdx)
{
    if (monitor < 0 || monitor >= 8 || modeIdx < 0 || modeIdx >= g_n[monitor])
        return -1;
    const MonInfo *mi = MonGet(monitor);
    if (!mi) return -1;

    ModeInfo saved;
    Modes_CurrentFor(monitor, &saved);

    DEVMODEW dm;
    memset(&dm, 0, sizeof dm);
    dm.dmSize = sizeof dm;
    if (!EnumDisplaySettingsW(mi->device, ENUM_CURRENT_SETTINGS, &dm)) return -1;
    dm.dmPelsWidth = g_modes[monitor][modeIdx].w;
    dm.dmPelsHeight = g_modes[monitor][modeIdx].h;
    dm.dmDisplayFrequency = g_modes[monitor][modeIdx].hz;
    dm.dmFields = DM_PELSWIDTH | DM_PELSHEIGHT | DM_DISPLAYFREQUENCY;

    /* no-op guard: already in this mode */
    if (saved.w == (int)dm.dmPelsWidth && saved.h == (int)dm.dmPelsHeight && saved.hz == (int)dm.dmDisplayFrequency)
        return 0;

    /* pre-test \u2014 never force a mode the driver rejects */
    LONG t = ChangeDisplaySettingsExW(mi->device, &dm, NULL, CDS_TEST, 0);
    if (t != DISP_CHANGE_SUCCESSFUL) return -1;

    LONG r = ChangeDisplaySettingsExW(mi->device, &dm, NULL, CDS_UPDATEREGISTRY, NULL);
    if (r != DISP_CHANGE_SUCCESSFUL && r != DISP_CHANGE_BADFLAGS) {
        /* roll back to the previous mode */
        DEVMODEW rb = dm;
        rb.dmPelsWidth = saved.w;
        rb.dmPelsHeight = saved.h;
        rb.dmDisplayFrequency = saved.hz;
        (void)ChangeDisplaySettingsExW(mi->device, &rb, NULL, CDS_UPDATEREGISTRY, NULL);
        return -1;
    }
    Modes_Refresh();
    return 0;
}
