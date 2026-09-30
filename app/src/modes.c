/* modes.c — resolution / refresh switching + jump to Windows HDR settings */
#include "common.h"

#define MAX_MODES 96

static ModeInfo g_modes[MAX_MODES];
static int      g_nm;
static wchar_t  g_dev[32];
static ModeInfo g_cur;

static int mode_cmp(const void *a, const void *b)
{
    const ModeInfo *x = a, *y = b;
    long ax = (long)x->w * x->h, ay = (long)y->w * y->h;
    if (ax != ay) return ay - ax;
    return y->hz - x->hz;
}

int Modes_Refresh(void)
{
    DISPLAY_DEVICEW dd;
    memset(&dd, 0, sizeof dd);
    dd.cb = sizeof dd;
    if (!EnumDisplayDevicesW(NULL, 0, &dd, 0)) return 0;
    lstrcpynW(g_dev, dd.DeviceName, 32);

    DEVMODEW dm;
    g_nm = 0;
    for (int i = 0; i < 4096 && g_nm < MAX_MODES; i++) {
        memset(&dm, 0, sizeof dm);
        dm.dmSize = sizeof dm;
        if (!EnumDisplaySettingsW(g_dev, i, &dm)) break;
        if (dm.dmPelsWidth < 640 || dm.dmPelsHeight < 480) continue;
        int hz = (int)dm.dmDisplayFrequency;
        if (hz < 24 || hz > 1000) hz = 60;
        int dup = 0;
        for (int j = 0; j < g_nm; j++)
            if (g_modes[j].w == (int)dm.dmPelsWidth &&
                g_modes[j].h == (int)dm.dmPelsHeight &&
                g_modes[j].hz == hz) { dup = 1; break; }
        if (dup) continue;
        g_modes[g_nm].w = (int)dm.dmPelsWidth;
        g_modes[g_nm].h = (int)dm.dmPelsHeight;
        g_modes[g_nm].hz = hz;
        g_modes[g_nm].native = 0;
        g_nm++;
    }
    qsort(g_modes, g_nm, sizeof g_modes[0], mode_cmp);

    memset(&dm, 0, sizeof dm);
    dm.dmSize = sizeof dm;
    if (EnumDisplaySettingsW(g_dev, ENUM_CURRENT_SETTINGS, &dm)) {
        g_cur.w = (int)dm.dmPelsWidth;
        g_cur.h = (int)dm.dmPelsHeight;
        g_cur.hz = (int)dm.dmDisplayFrequency;
        if (g_cur.hz < 24) g_cur.hz = 60;
        g_cur.native = 1;
        for (int j = 0; j < g_nm; j++)
            if (g_modes[j].w == g_cur.w && g_modes[j].h == g_cur.h &&
                g_modes[j].hz == g_cur.hz)
                g_modes[j].native = 1;
    }
    return g_nm;
}

int Modes_Count(void) { return g_nm; }
ModeInfo *Modes_Get(int i) { return (i >= 0 && i < g_nm) ? &g_modes[i] : NULL; }

int Modes_Current(ModeInfo *out)
{
    *out = g_cur;
    return 0;
}

int Modes_Apply(int idx)
{
    if (idx < 0 || idx >= g_nm) return -1;
    DEVMODEW dm;
    memset(&dm, 0, sizeof dm);
    dm.dmSize = sizeof dm;
    if (!EnumDisplaySettingsW(g_dev, ENUM_CURRENT_SETTINGS, &dm)) return -1;
    dm.dmPelsWidth = g_modes[idx].w;
    dm.dmPelsHeight = g_modes[idx].h;
    dm.dmDisplayFrequency = g_modes[idx].hz;
    dm.dmFields = DM_PELSWIDTH | DM_PELSHEIGHT | DM_DISPLAYFREQUENCY;
    /* CDS_UPDATEREGISTRY = user-style change: Windows shows "Keep these
       display settings?" and reverts automatically if nobody answers. */
    LONG r = ChangeDisplaySettingsExW(g_dev, &dm, NULL, CDS_UPDATEREGISTRY, NULL);
    if (r != DISP_CHANGE_SUCCESSFUL && r != DISP_CHANGE_BADFLAGS) return -1;
    Modes_Refresh();
    return 0;
}

void Modes_OpenHdr(void)
{
    ShellExecuteW(NULL, L"open", L"ms-settings:display", NULL, NULL, SW_SHOWNORMAL);
}
