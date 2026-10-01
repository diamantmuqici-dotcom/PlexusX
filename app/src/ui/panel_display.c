/* PlexusX — DISPLAY ▸ Monitors / Resolution / Refresh Rate.
 *
 * All information comes from the display manager's OS reads (EnumDisplaySettings,
 * EnumDisplayDevices, DXGI).  Mode changes always go through the confirm-or-
 * rollback path in display_manager.c: a change arms a countdown, and the UI
 * shows the countdown bar; if the user does not confirm, the previous DEVMODE
 * is restored and the display never stays in a mode nobody approved.
 */
#include "ui_internal.h"
#include "../ui_theme.h"

/* ---------------- Monitors ---------------- */
void UiMonitors_Build(void)
{
    int n = Modes_MonitorCount();
    int cur = Modes_CurrentMonitorIndex();
    ModeInfo m;
    const GpuInfo *gpu = Dm_GpuInfo();
    wchar_t buf[192];

    UiW(WT_HEAD, 0, 248, 62, 700, 30, L"Monitors");
    UiW(WT_LABEL, 0, 248, 92, 986, 18,
        L"Every connected output, what the OS reports about it, and where the colour engine can reach it.");

    Modes_Current(&m);

    for (int i = 0; i < n && i < 8; i++) {
        MonitorInfo *mi = Modes_GetMonitor(i);
        int y = 122 + i * 84;
        Widget *card;
        if (!mi) continue;
        card = UiW(WT_MONITOR_CARD, ID_MONITOR_ROW_BASE + i, 248, y, 986, 72, mi->friendly);
        card->state = (i == cur);
        _snwprintf(buf, 192, L"%s · %s · %s", mi->dev_name, mi->adapter,
                   mi->is_primary ? L"primary" : L"secondary");
        UiWin(card->sub, 64, buf);
        _snwprintf(buf, 192, L"%d × %d @ %d Hz", mi->current_w, mi->current_h, mi->current_hz);
        UiWin(card->val, 48, buf);
    }

    /* capability readout for the selected monitor (never invented) */
    {
        MonitorInfo *mi = Modes_GetMonitor(cur);
        PxCapSummary cap;
        int hdr = mi ? PxHdr_StateOf(mi) : PX_HDR_DISABLED;
        int y = 122 + (n > 8 ? 8 : n) * 84 + 8;
        char csb[64];
        wchar_t cs[64];

        if (y > 470) y = 470;
        PxCap_Fill(&cap, mi, Eng_Available(), Dm_GpuInfo() && Dm_GpuInfo()->gamma_available, 0);
        if (mi) px_cs_name(mi->color_space_raw, csb, sizeof csb);
        else csb[0] = 0;
        MultiByteToWideChar(CP_UTF8, 0, csb, -1, cs, 64);
        cs[63] = 0;

        UiW(WT_DIV, 0, 248, y, 986, 20, L"SELECTED OUTPUT CAPABILITIES");
        UiW(WT_HEAD, 0, 248, y + 24, 700, 26, L"");
        {
            Widget *k = UiWCard(248, y + 56, 322, 92, L"HDR", L"—", L"—");
            MultiByteToWideChar(CP_UTF8, 0, PxHdr_Name(hdr), -1, buf, 190);
            UiWin(k->val, 48, buf);
            MultiByteToWideChar(CP_UTF8, 0, PxHdr_Explain(hdr), -1, buf, 190);
            UiWin(k->sub, 64, buf);
        }
        {
            Widget *k = UiWCard(578, y + 56, 322, 92, L"OUTPUT", L"—", L"—");
            _snwprintf(buf, 192, L"%s  ·  %d-bit", cs[0] ? cs : L"colour space not reported",
                       cap.bpc ? cap.bpc : 8);
            UiWin(k->val, 48, buf);
            if (cap.max_nits > 0.0f)
                _snwprintf(buf, 192, L"%.0f–%.0f nits (full frame %.0f)", (double)cap.min_nits,
                           (double)cap.max_nits, (double)cap.max_ff_nits);
            else
                UiWin(buf, 192, L"luminance not reported by the driver");
            UiWin(k->sub, 64, buf);
        }
        {
            Widget *k = UiWCard(908, y + 56, 326, 92, L"COLOUR PATHS", L"—", L"—");
            _snwprintf(buf, 192, L"matrix %s  ·  gamma ramps %s",
                       cap.linear_path ? L"available" : L"unavailable",
                       cap.curves_path ? L"available" : L"unavailable");
            UiWin(k->val, 48, buf);
            UiWin(k->sub, 64, cap.linear_path && cap.curves_path
                              ? L"full pipeline: chroma + tone on composited windows"
                              : L"limited pipeline: tone curves only where reported");
        }
    }

    UiW(WT_PRIMARY, ID_B_IDENTIFY_MONITORS, 248, 660, 200, 40, L"Identify Displays");
    UiW(WT_GHOST,   ID_B_HDR_REFRESH,       458, 660, 200, 40, L"Re-read Capabilities");
    UiW(WT_GHOST,   ID_B_MODE_OPENHDR,      668, 660, 200, 40, L"Windows HDR Settings");
    UiW(WT_GHOST,   ID_TEST_PAT_BASE + 0,   878, 660, 170, 40, L"Black pattern");
    UiW(WT_GHOST,   ID_TEST_PAT_BASE + 5,  1058, 660, 176, 40, L"Gradient pattern");
    if (gpu && gpu->name[0]) {
        UiW(WT_LABEL, 0, 248, 704, 986, 18, L"GPU reported by the system:");
        {
            Widget *k = UiW(WT_KV, 0, 380, 704, 854, 18, L"Adapter");
            UiWin(k->val, 48, gpu->name);
        }
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
        _snwprintf(buf, 96, L"%d Hz  ·  %s", mi->hz, asp);
        UiWin(row->sub, 64, buf);
    }
    UiWin(row->val, 48, mi->native ? L"native" : L"");
}

/* The Resolution page lists the DISTINCT resolutions available on this output,
 * the current mode first; selecting one offers the exact refresh rates on the
 * Refresh Rate page, so a resolution change can never silently change Hz. */
void UiResolution_Build(void)
{
    int count = Modes_Count();
    int shown = 0, last_w = -1, last_h = -1;
    ModeInfo cur, best;
    wchar_t buf[192];

    UiW(WT_HEAD, 0, 248, 62, 700, 30, L"Resolution");
    UiW(WT_LABEL, 0, 248, 92, 986, 18,
        L"Only modes the OS reports for the selected output are listed. Every change asks for confirmation and rolls back on its own.");

    Modes_Current(&cur);
    memset(&best, 0, sizeof best);
    _snwprintf(buf, 192, L"%d × %d @ %d Hz  ·  %s", cur.w, cur.h, cur.hz,
               cur.native ? L"native mode" : L"custom mode");
    {
        Widget *k = UiWCard(248, 118, 986, 60, L"CURRENT MODE", buf,
                            L"Changing the mode arms a 15 s confirmation countdown; ignore it and the previous mode comes back.");
        (void)k;
    }

    UiW(WT_DIV, 0, 248, 190, 986, 20, L"AVAILABLE RESOLUTIONS ON THIS OUTPUT");
    for (int i = 0; i < count && shown < 11; i++) {
        ModeInfo *mi = Modes_Get(i);
        if (!mi || mi->w <= 0) continue;
        if (mi->w == last_w && mi->h == last_h) continue;    /* distinct resolutions */
        last_w = mi->w; last_h = mi->h;
        mode_row(shown, i);
        shown++;
    }
    if (shown == 0)
        UiW(WT_LABEL, 0, 248, 152, 986, 20, L"The display reported no usable modes.");

    UiW(WT_DIV, 0, 248, 640, 986, 20, L"SAFE RESOLUTION PRESETS");
    UiW(WT_PRIMARY, ID_B_MODE_APPLY_SEL, 248, 664, 170, 38, L"Apply Selected");
    UiW(WT_GHOST,   ID_B_MODE_NATIVE,    428, 664, 150, 38, L"Native Mode");
    UiW(WT_GHOST,   ID_B_MODE_43_COMP,   588, 664, 140, 38, L"1280 × 960");
    UiW(WT_GHOST,   ID_B_MODE_43_STRETCH,738, 664, 140, 38, L"1440 × 1080");
    UiW(WT_GHOST,   ID_B_MODE_43_CLASSIC,888, 664, 140, 38, L"1600 × 1200");
    UiW(WT_GHOST,   ID_B_MODE_1610,     1038, 664, 130, 38, L"1680 × 1050");
    UiW(WT_GHOST,   ID_TEST_PAT_BASE + 5, 1178, 664, 56, 38, L"Test");

}

/* ---------------- Refresh Rate ---------------- */
void UiRefresh_Build(void)
{
    int count = Modes_Count();
    ModeInfo cur;
    int shown = 0, last_hz = -1;
    wchar_t buf[192];

    UiW(WT_HEAD, 0, 248, 62, 700, 30, L"Refresh Rate");
    UiW(WT_LABEL, 0, 248, 92, 986, 18,
        L"Refresh rates the OS reports for the current resolution. Applying one uses the same confirm-or-rollback path as any mode change.");

    Modes_Current(&cur);
    _snwprintf(buf, 192, L"%d × %d @ %d Hz", cur.w, cur.h, cur.hz);
    {
        Widget *k = UiWCard(248, 118, 986, 60, L"CURRENT MODE", buf,
                            L"Higher refresh rates need a cable/port and panel that support them — if a mode is missing, Windows does not offer it.");
        (void)k;
    }

    UiW(WT_DIV, 0, 248, 190, 986, 20, L"SUPPORTED REFRESH RATES AT THE CURRENT RESOLUTION");
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
            UiWin(row->sub, 64, mi->hz >= 120 ? L"high refresh — verify the panel is actually running it"
                                              : L"standard refresh");
            UiWin(row->val, 48, mi->hz == cur.hz ? L"active" : L"");
        }
        shown++;
    }
    if (shown == 0)
        UiW(WT_LABEL, 0, 248, 152, 986, 20, L"No refresh rate variants were reported for this resolution.");

    UiW(WT_PRIMARY, ID_B_MODE_APPLY_SEL, 248, 640, 220, 40, L"Apply Selected Rate");
    UiW(WT_ACCENT,  ID_B_MODE_MAX_HZ,    478, 640, 220, 40, L"Maximum Refresh Rate");
    UiW(WT_GHOST,   ID_B_MODE_NATIVE,    708, 640, 190, 40, L"Native Mode");
    UiW(WT_GHOST,   ID_B_RESET_DISPLAY,  908, 640, 326, 40, L"Reset Resolution and Refresh");
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
        _snwprintf(msg, 128, L"%d × %d @ %d Hz applied — confirm within 15 s", mi->w, mi->h, mi->hz);
        Ui_Notify(msg);
    } else {
        Ui_Notify(L"The display rejected that mode; nothing changed");
    }
}

void UiDisplay_ApplySafeRes(int w, int h, int hz)
{
    if (Modes_ApplyResHz(w, h, hz) == 0) {
        wchar_t msg[128];
        _snwprintf(msg, 128, L"%d × %d @ %s applied — confirm within 15 s", w, h,
                   hz ? L"the requested rate" : L"the best available rate");
        Ui_Notify(msg);
    } else {
        wchar_t msg[128];
        _snwprintf(msg, 128, L"%d × %d is not offered by this display", w, h);
        Ui_Notify(msg);
    }
}

void UiDisplay_ResetToDefault(void)
{
    if (Modes_ApplyNative() == 0) Ui_Notify(L"Native resolution and refresh restored");
    else Ui_Notify(L"The display did not offer a native mode to restore");
}
