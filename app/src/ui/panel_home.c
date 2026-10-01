/* PlexusX — HOME dashboard.
 *
 * The dashboard answers, at a glance and from live state only:
 *   what game is running · which profile is active · which display and mode
 *   · is HDR involved · what the colour engine is really doing
 * plus the quick actions a player actually uses, and a live colour preview that
 * runs the same math as the pipeline.
 */
#include "ui_internal.h"
#include "../color/color_math.h"
#include "../ui_theme.h"

static void card_monitor(int x, int y, int w, int h)
{
    MonitorInfo *mi = Modes_GetMonitor(Modes_CurrentMonitorIndex());
    Widget *k = UiWCard(x, y, w, h, L"MONITOR", L"—", L"—");
    if (mi) {
        char csb[64];
        wchar_t cs[64], sub[72];
        px_cs_name(mi->color_space_raw, csb, sizeof csb);
        MultiByteToWideChar(CP_UTF8, 0, csb, -1, cs, 64);
        cs[63] = 0;
        _snwprintf(sub, 72, L"%s  ·  %s", mi->is_primary ? L"primary" : L"secondary", cs);
        UiWin(k->val, 48, mi->friendly);
        UiWin(k->sub, 64, sub);
    }
}

void UiHome_Build(void)
{
    const PxEffectiveState *e = UiStatus();
    const PxGameDisplayState *gs = Prof_GameState();
    Profile *ap = Prof_Get(Prof_ActiveIndex());
    ModeInfo cur;
    MonitorInfo *mi;
    GpuInfo gpu_copy;
    const GpuInfo *gpu = Dm_GpuInfo();
    wchar_t buf[192];

    UiW(WT_HEAD, 0, 248, 66, 620, 32, L"Command Center");
    UiW(WT_LABEL, 0, 248, 98, 900, 18,
        L"Requested colour, effective colour and what the display really applied are three different things — this page shows all three.");

    Modes_Current(&cur);
    mi = Modes_GetMonitor(Modes_CurrentMonitorIndex());
    memset(&gpu_copy, 0, sizeof gpu_copy);
    if (gpu) gpu_copy = *gpu;

    /* ---- status cards (row 1) ---- */
    {
        Widget *k = UiWCard(248, 126, 196, 84, L"CURRENT GAME", L"Desktop", L"no game in the foreground");
        if (gs->detected) {
            UiWin(k->val, 48, gs->exe);
            MultiByteToWideChar(CP_UTF8, 0, px_gameout_name(gs->game_output), -1, buf, 190);
            UiWin(k->sub, 64, buf);
        } else {
            const wchar_t *fg = Prof_CurrentForeground();
            UiWin(k->val, 48, L"Desktop");
            UiWin(k->sub, 64, (fg && fg[0]) ? fg : L"no game in the foreground");
        }
    }
    {
        Widget *k = UiWCard(454, 126, 196, 84, L"ACTIVE PROFILE", L"Global look", L"detection off");
        if (ap) {
            UiWin(k->val, 48, ap->name);
            _snwprintf(buf, 192, L"%s · %s", ap->sub[ap->active_sub].name,
                       ap->auto_apply ? L"auto-apply" : L"manual");
            UiWin(k->sub, 64, buf);
        } else {
            UiWin(k->sub, 64, Prof_Detect() ? L"automatic detection on" : L"detection off");
        }
    }
    card_monitor(660, 126, 196, 84);
    {
        Widget *k = UiWCard(866, 126, 180, 84, L"RESOLUTION", L"—", L"—");
        _snwprintf(buf, 192, L"%d × %d", cur.w, cur.h);
        UiWin(k->val, 48, buf);
        _snwprintf(buf, 192, L"%s mode", cur.native ? L"native" : L"custom");
        UiWin(k->sub, 64, buf);
    }
    {
        Widget *k = UiWCard(1056, 126, 178, 84, L"REFRESH RATE", L"—", L"—");
        _snwprintf(buf, 192, L"%d Hz", cur.hz);
        UiWin(k->val, 48, buf);
        UiWin(k->sub, 64, L"current output");
    }

    /* ---- status cards (row 2) ---- */
    {
        int hdr = mi ? PxHdr_StateOf(mi) : PX_HDR_DISABLED;
        Widget *k = UiWCard(248, 220, 196, 84, L"HDR", L"—", L"—");
        MultiByteToWideChar(CP_UTF8, 0, PxHdr_Name(hdr), -1, buf, 190);
        UiWin(k->val, 48, buf);
        MultiByteToWideChar(CP_UTF8, 0, PxHdr_Explain(hdr), -1, buf, 190);
        UiWin(k->sub, 64, buf);
    }
    {
        Widget *k = UiWCard(454, 220, 196, 84, L"COLOR ENGINE", L"—", L"—");
        MultiByteToWideChar(CP_UTF8, 0, PxStatus_Name(e->status), -1, buf, 190);
        UiWin(k->val, 48, buf);
        MultiByteToWideChar(CP_UTF8, 0, e->reason, -1, buf, 190);
        UiWin(k->sub, 64, buf);
    }
    {
        Widget *k = UiWCard(660, 220, 196, 84, L"APPLICATION STATUS", L"—", L"—");
        _snwprintf(buf, 192, L"%d profile%s · detection %s",
                   Prof_Count(), Prof_Count() == 1 ? L"" : L"s", Prof_Detect() ? L"on" : L"off");
        UiWin(k->val, 48, buf);
        {
            const AppliedColorState *a = Eng_Applied();
            if (a && a->have)
                _snwprintf(buf, 192, L"last apply rev %u · %d ramp write%s", a->revision, a->ramp_writes,
                           a->ramp_writes == 1 ? L"" : L"s");
            else
                UiWin(buf, 192, L"nothing applied yet");
            UiWin(k->sub, 64, buf);
        }
    }
    {
        Widget *k = UiWCard(866, 220, 368, 84, L"GPU", L"—", L"—");
        UiWin(k->val, 48, gpu_copy.name[0] ? gpu_copy.name : (gpu_copy.vendor_name[0] ? gpu_copy.vendor_name : L"not reported"));
        _snwprintf(buf, 192, L"%s driver · magnification matrix %s · gamma ramp %s",
                   gpu_copy.driver_ver[0] ? gpu_copy.driver_ver : L"unknown",
                   gpu_copy.mag_available ? L"available" : L"unavailable",
                   gpu_copy.gamma_available ? L"available" : L"unavailable");
        UiWin(k->sub, 64, buf);
    }

    /* ---- quick actions ---- */
    UiW(WT_DIV, 0, 248, 316, 986, 20, L"QUICK ACTIONS");
    {
        Widget *on = UiW(WT_PRIMARY, ID_B_ENGINE_ON, 248, 340, 150, 36, L"Enable Engine");
        Widget *off = UiW(WT_GHOST, ID_B_ENGINE_OFF, 408, 340, 150, 36, L"Disable Engine");
        on->state = Eng_GetRequested()->enabled ? 1 : 0;
        off->state = on->state;
        (void)on; (void)off;
    }
    UiW(WT_GHOST, ID_B_OPEN_COLOR,   568, 340, 150, 36, L"Global Color");
    UiW(WT_GHOST, ID_B_OPEN_GAME,    728, 340, 150, 36, L"Game Profile");
    UiW(WT_GHOST, ID_B_RESET_DISPLAY,888, 340, 150, 36, L"Reset Display");
    UiW(WT_GHOST, ID_B_OPEN_DIAG,   1048, 340, 186, 36, L"Diagnostics");

    UiW(WT_GHOST, ID_B_HOME_APPLY_GAME,  248, 386, 190, 36, L"Apply Active Profile");
    UiW(WT_GHOST, ID_B_HOME_COMPETITIVE, 448, 386, 160, 36, L"Competitive");
    UiW(WT_GHOST, ID_B_HOME_MAX_VIB,     618, 386, 160, 36, L"Max Vibrance");
    UiW(WT_GHOST, ID_B_HOME_NIGHT_VIS,   788, 386, 150, 36, L"Night Ops");
    UiW(WT_GHOST, ID_B_HOME_CINEMATIC,   948, 386, 140, 36, L"Cinematic");
    UiW(WT_GHOST, ID_B_HOME_NATURAL,    1098, 386, 136, 36, L"Neutral");
    {
        Widget *gm = UiW(WT_BTN, ID_B_HOME_GAMING_MODE, 248, 428, 240, 32,
                         Tools_IsGamingMode() ? L"Gaming mode: ON" : L"Gaming mode: OFF");
        gm->flags = Tools_IsGamingMode() ? 1 : 0;
    }
    UiW(WT_LABEL, 0, 500, 430, 734, 28,
        L"Gaming mode lowers UI timers while the colour pipeline keeps running event-driven; it never pauses colour output.");

    /* ---- live preview ---- */
    UiW(WT_DIV, 0, 248, 466, 986, 20, L"LIVE COLOUR PREVIEW  ·  SAME MATH AS THE PIPELINE  ·  DRAG THE DIVIDER");
    UiW(WT_SPLIT_PREVIEW, 0, 248, 486, 620, 196, NULL);
    UiW(WT_CURVE_PREVIEW, 0, 878, 486, 356, 196, NULL);

    {
        Widget *t = UiW(WT_TOGGLE, ID_T_LOOK_ENABLE, 248, 690, 300, 24, L"Colour engine enabled");
        t->state = Eng_GetRequested()->enabled;
    }
    UiW(WT_LABEL, 0, 560, 730, 674, 26,
        L"Wheel = nudge · Shift+wheel = coarse · Ctrl+wheel = fine · double-click a slider = neutral value");
}
