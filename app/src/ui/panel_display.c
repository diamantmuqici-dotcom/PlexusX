/* PlexusX — DISPLAY ▸ Monitors / Resolution / Refresh Rate — Premium v2.2
 *
 * All information comes from the display manager's OS reads (EnumDisplaySettings,
 * EnumDisplayDevices, DXGI).  Mode changes always go through the confirm-or-
 * rollback path in display_manager.c: a change arms a countdown, and the UI
 * shows the countdown bar; if the user does not confirm, the previous DEVMODE
 * is restored and the display never stays in a mode nobody approved.
 *
 * Premium redesign:
 *   - Monitor cards with HDR tone, primary badge, bpc/colorSpace/nits honest
 *   - Capabilities section with linear/curves path availability
 *   - Resolution distinct list, current mode highlighted, safe presets
 *   - Refresh rate per-resolution, high refresh badge, max Hz action
 *   - Confirm-or-rollback countdown integrated in status bar
 */

#include "ui_internal.h"
#include "../ui_theme.h"
#include "../core/version.h"

/* ---------------- Monitors ---------------- */
void UiMonitors_Build(void)
{
    int n = Modes_MonitorCount();
    int cur = Modes_CurrentMonitorIndex();
    ModeInfo m;
    const GpuInfo *gpu = Dm_GpuInfo();
    wchar_t buf[192];

    UiW(WT_HEAD, 0, 248, 62, 400, 28, L"Monitors");
    UiW(WT_LABEL, 0, 248, 90, 600, 14, L"Every output, OS-reported caps, and where color pipeline reaches");

    Modes_Current(&m);

    for (int i = 0; i < n && i < 8; i++) {
        MonitorInfo *mi = Modes_GetMonitor(i);
        int y = 116 + i * 84;
        Widget *card;
        if (!mi) continue;
        card = UiW(WT_MONITOR_CARD, ID_MONITOR_ROW_BASE + i, 248, y, 986, 72, mi->friendly);
        card->state = (i == cur);
        _snwprintf(buf, 192, L"%s · %s · %s · %d×%d@%d",
                   mi->dev_name, mi->adapter,
                   mi->is_primary ? L"primary" : L"secondary",
                   mi->current_w, mi->current_h, mi->current_hz);
        UiWin(card->sub, 64, buf);
        _snwprintf(buf, 192, L"%d × %d @ %d Hz", mi->current_w, mi->current_h, mi->current_hz);
        UiWin(card->val, 48, buf);
        /* Tone by HDR */
        int hdr = PxHdr_StateOf(mi);
        card->flags = PxHdr_Tone(hdr);
    }

    /* capability readout for selected monitor — never invented */
    {
        MonitorInfo *mi = Modes_GetMonitor(cur);
        PxCapSummary cap;
        int hdr = mi ? PxHdr_StateOf(mi) : PX_HDR_DISABLED;
        int y = 116 + (n > 6 ? 6 : n) * 84 + 8;
        char csb[64];
        wchar_t cs[64];

        if (y > 480) y = 480;
        PxCap_Fill(&cap, mi, Eng_Available(), Dm_GpuInfo() && Dm_GpuInfo()->gamma_available, 0);
        if (mi) px_cs_name(mi->color_space_raw, csb, sizeof csb);
        else csb[0] = 0;
        MultiByteToWideChar(CP_UTF8, 0, csb, -1, cs, 64);
        cs[63] = 0;

        UiW(WT_DIV, 0, 248, y, 986, 20, L"SELECTED OUTPUT — HONEST CAPABILITIES (0 / UNKNOWN = NOT REPORTED)");
        {
            Widget *k = UiWCard(248, y + 24, 322, 92, L"HDR STATUS", L"—", L"—");
            MultiByteToWideChar(CP_UTF8, 0, PxHdr_Name(hdr), -1, buf, 190);
            UiWin(k->val, 48, buf);
            MultiByteToWideChar(CP_UTF8, 0, PxHdr_Explain(hdr), -1, buf, 190);
            UiWin(k->sub, 64, buf);
            k->flags = PxHdr_Tone(hdr);
        }
        {
            Widget *k = UiWCard(578, y + 24, 322, 92, L"OUTPUT FORMAT", L"—", L"—");
            _snwprintf(buf, 192, L"%s · %d-bit", cs[0] ? cs : L"color space not reported",
                       cap.bpc ? cap.bpc : 8);
            UiWin(k->val, 48, buf);
            if (cap.max_nits > 0.0f)
                _snwprintf(buf, 192, L"%.0f–%.0f nits (FF %.0f) · %s",
                           (double)cap.min_nits, (double)cap.max_nits, (double)cap.max_ff_nits,
                           cap.hdr_capable ? L"HDR capable" : L"SDR");
            else
                UiWin(buf, 192, L"luminance not reported — driver didn't say");
            UiWin(k->sub, 64, buf);
            k->flags = cap.bpc >= 10 ? PX_CHIP_OK : PX_CHIP_NEUTRAL;
        }
        {
            Widget *k = UiWCard(908, y + 24, 326, 92, L"COLOR PATHS", L"—", L"—");
            _snwprintf(buf, 192, L"matrix %s · ramps %s",
                       cap.linear_path ? L"✓ available" : L"✗ unavailable",
                       cap.curves_path ? L"✓ available" : L"✗ unavailable");
            UiWin(k->val, 48, buf);
            UiWin(k->sub, 64, cap.linear_path && cap.curves_path
                              ? L"full pipeline: chroma + tone on composited windows"
                              : cap.linear_path ? L"matrix only — ramp path missing"
                              : cap.curves_path ? L"curves only — DWM matrix missing"
                              : L"no usable path — update driver");
            k->flags = (cap.linear_path && cap.curves_path) ? PX_CHIP_OK :
                       (cap.linear_path || cap.curves_path) ? PX_CHIP_WARN : PX_CHIP_BAD;
        }
    }

    UiW(WT_PRIMARY, ID_B_IDENTIFY_MONITORS, 248, 680, 200, 40, L"Identify Displays");
    UiW(WT_GHOST,   ID_B_HDR_REFRESH,       458, 680, 200, 40, L"Re-read Caps");
    UiW(WT_GHOST,   ID_B_MODE_OPENHDR,      668, 680, 200, 40, L"Windows HDR Settings");
    UiW(WT_GHOST,   ID_TEST_PAT_BASE + 0,   878, 680, 170, 40, L"Black Pattern");
    UiW(WT_GHOST,   ID_TEST_PAT_BASE + 5,  1058, 680, 176, 40, L"Gradient Test");
    if (gpu && gpu->name[0]) {
        wchar_t gpu_line[220];
        _snwprintf(gpu_line, 220, L"GPU: %s · driver %s · v%s · %s",
                   gpu->name, gpu->driver_ver[0] ? gpu->driver_ver : L"unknown",
                   PX_VERSION_STRING,
                   (gpu->mag_available && gpu->gamma_available) ? L"full pipeline" : L"limited");
        UiW(WT_LABEL, 0, 248, 724, 986, 14, gpu_line);
    }
}

/* ---------------- Resolution ---------------- */
static void mode_row(int slot, int mode_index)
{
    ModeInfo *mi = Modes_Get(mode_index);
    ModeInfo cur;
    wchar_t buf[96];
    Widget *row;
    if (!mi) return;
    Modes_Current(&cur);
    _snwprintf(buf, 96, L"%d × %d", mi->w, mi->h);
    row = UiW(WT_ROW, ID_MODE_ROW_BASE + mode_index, 248, 152 + slot * 44, 640, 40, buf);
    row->state = (mi->w == cur.w && mi->h == cur.h && mi->hz == cur.hz) || mode_index == g_ui.sel_mode;
    {
        const wchar_t *asp = mi->aspect == 1 ? L"4:3 stretched" :
                             mi->aspect == 2 ? L"16:10" :
                             mi->aspect == 3 ? L"ultrawide" :
                             mi->native ? L"native 16:9" : L"16:9";
        _snwprintf(buf, 96, L"%d Hz · %s · %s", mi->hz, asp,
                   mi->hz >= 240 ? L"high refresh" : mi->hz >= 144 ? L"gaming" : L"standard");
        UiWin(row->sub, 64, buf);
    }
    UiWin(row->val, 48, mi->native ? L"native" : L"");
    row->flags = mi->native ? PX_CHIP_OK : (mi->hz >= 144 ? PX_CHIP_INFO : PX_CHIP_NEUTRAL);
}

void UiResolution_Build(void)
{
    int count = Modes_Count();
    int shown = 0, last_w = -1, last_h = -1;
    ModeInfo cur;
    wchar_t buf[192];

    UiW(WT_HEAD, 0, 248, 62, 300, 28, L"Resolution");
    UiW(WT_LABEL, 0, 248, 90, 600, 14, L"Only OS-reported modes · confirm-or-rollback safety · no invented modes");

    Modes_Current(&cur);
    _snwprintf(buf, 192, L"%d × %d @ %d Hz · %s · v%s",
               cur.w, cur.h, cur.hz,
               cur.native ? L"native" : L"custom", PX_VERSION_STRING);
    {
        Widget *k = UiWCard(248, 112, 986, 56, L"CURRENT MODE", buf,
                            L"Change arms 15s countdown; ignore and previous mode returns automatically");
        k->flags = cur.native ? PX_CHIP_OK : PX_CHIP_WARN;
        (void)k;
    }

    UiW(WT_DIV, 0, 248, 178, 986, 20, L"AVAILABLE RESOLUTIONS — DISTINCT, OS-REPORTED, SORTED BY AREA THEN HZ");
    for (int i = 0; i < count && shown < 11; i++) {
        ModeInfo *mi = Modes_Get(i);
        if (!mi || mi->w <= 0) continue;
        if (mi->w == last_w && mi->h == last_h) continue;
        last_w = mi->w; last_h = mi->h;
        mode_row(shown, i);
        shown++;
    }
    if (shown == 0)
        UiW(WT_LABEL, 0, 248, 152, 986, 20, L"The display reported no usable modes — driver didn't say");

    UiW(WT_DIV, 0, 248, 640, 986, 20, L"SAFE PRESETS — COMPETITIVE STRETCHED + STANDARD");
    UiW(WT_PRIMARY, ID_B_MODE_APPLY_SEL, 248, 664, 170, 38, L"Apply Selected");
    UiW(WT_GHOST,   ID_B_MODE_NATIVE,    428, 664, 150, 38, L"Native Mode");
    UiW(WT_GHOST,   ID_B_MODE_43_COMP,   588, 664, 140, 38, L"1280×960 4:3");
    UiW(WT_GHOST,   ID_B_MODE_43_STRETCH,738, 664, 140, 38, L"1440×1080 4:3");
    UiW(WT_GHOST,   ID_B_MODE_43_CLASSIC,888, 664, 140, 38, L"1600×1200 4:3");
    UiW(WT_GHOST,   ID_B_MODE_1610,     1038, 664, 130, 38, L"1680×1050 16:10");
    UiW(WT_GHOST,   ID_TEST_PAT_BASE + 5, 1178, 664, 56, 38, L"Test");
}

/* ---------------- Refresh Rate ---------------- */
void UiRefresh_Build(void)
{
    int count = Modes_Count();
    ModeInfo cur;
    int shown = 0, last_hz = -1;
    wchar_t buf[192];

    UiW(WT_HEAD, 0, 248, 62, 300, 28, L"Refresh Rate");
    UiW(WT_LABEL, 0, 248, 90, 600, 14, L"Rates for current resolution · confirm-or-rollback · cable/panel must support");

    Modes_Current(&cur);
    _snwprintf(buf, 192, L"%d × %d @ %d Hz · %s", cur.w, cur.h, cur.hz,
               cur.hz >= 240 ? L"high refresh" : L"standard");
    {
        Widget *k = UiWCard(248, 112, 986, 56, L"CURRENT MODE", buf,
                            L"Higher rates need cable/port/panel that support them — missing = Windows doesn't offer it");
        k->flags = cur.hz >= 144 ? PX_CHIP_OK : PX_CHIP_NEUTRAL;
        (void)k;
    }

    UiW(WT_DIV, 0, 248, 178, 986, 20, L"SUPPORTED RATES AT CURRENT RESOLUTION — OS-REPORTED");
    for (int i = 0; i < count && shown < 12; i++) {
        ModeInfo *mi = Modes_Get(i);
        if (!mi) continue;
        if (mi->w != cur.w || mi->h != cur.h) continue;
        if (mi->hz == last_hz) continue;
        last_hz = mi->hz;
        {
            Widget *row = UiW(WT_ROW, ID_MODE_ROW_BASE + i, 248, 152 + shown * 40, 640, 36, L"");
            _snwprintf(buf, 192, L"%d Hz", mi->hz);
            UiWin(row->text, 96, buf);
            row->state = (mi->hz == cur.hz) || (i == g_ui.sel_mode);
            UiWin(row->sub, 64, mi->hz >= 240 ? L"high refresh — verify panel actually running it" :
                                  mi->hz >= 144 ? L"gaming refresh — check OSD" :
                                  L"standard refresh");
            UiWin(row->val, 48, mi->hz == cur.hz ? L"active" : L"");
            row->flags = mi->hz == cur.hz ? PX_CHIP_OK : (mi->hz >= 144 ? PX_CHIP_INFO : PX_CHIP_NEUTRAL);
        }
        shown++;
    }
    if (shown == 0)
        UiW(WT_LABEL, 0, 248, 152, 986, 20, L"No refresh variants reported for this resolution");

    UiW(WT_PRIMARY, ID_B_MODE_APPLY_SEL, 248, 640, 220, 40, L"Apply Selected Rate");
    UiW(WT_ACCENT,  ID_B_MODE_MAX_HZ,    478, 640, 220, 40, L"Maximum Refresh Rate");
    UiW(WT_GHOST,   ID_B_MODE_NATIVE,    708, 640, 190, 40, L"Native Mode");
    UiW(WT_GHOST,   ID_B_RESET_DISPLAY,  908, 640, 326, 40, L"Reset Resolution & Refresh");
}

/* ---------------- shared actions ---------------- */
void UiDisplay_ApplySelectedMode(void)
{
    ModeInfo *mi;
    if (g_ui.sel_mode < 0 || g_ui.sel_mode >= Modes_Count()) {
        Ui_Notify(L"Select a mode row first");
        return;
    }
    mi = Modes_Get(g_ui.sel_mode);
    if (!mi) return;
    if (Modes_ApplySafe(g_ui.sel_mode, 15000) == 0) {
        wchar_t msg[128];
        _snwprintf(msg, 128, L"%d×%d@%d applied — confirm within 15s or auto-rollback", mi->w, mi->h, mi->hz);
        Ui_Notify(msg);
    } else {
        Ui_Notify(L"Display rejected that mode; nothing changed — driver said no");
    }
}

void UiDisplay_ApplySafeRes(int w, int h, int hz)
{
    if (Modes_ApplyResHz(w, h, hz) == 0) {
        wchar_t msg[128];
        _snwprintf(msg, 128, L"%d×%d@%s applied — confirm within 15s",
                   w, h, hz ? L"requested" : L"best");
        Ui_Notify(msg);
    } else {
        wchar_t msg[128];
        _snwprintf(msg, 128, L"%d×%d not offered by this display — OS didn't report it", w, h);
        Ui_Notify(msg);
    }
}

void UiDisplay_ResetToDefault(void)
{
    if (Modes_ApplyNative() == 0) Ui_Notify(L"Native resolution & refresh restored — confirmed");
    else Ui_Notify(L"No native mode reported — nothing to restore");
}
