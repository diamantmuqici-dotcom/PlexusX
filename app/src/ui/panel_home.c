/* PlexusX — HOME dashboard — Premium Edition v2.2
 *
 * The dashboard answers, at a glance and from live state only:
 *   what game is running · which profile is active · which display and mode
 *   · is HDR involved · what the colour engine is really doing
 * plus the quick actions a player actually uses, and a live colour preview that
 * runs the same math as the pipeline.
 *
 * Premium redesign:
 *   - Enhanced status cards with tone indicators and live values
 *   - Integrated 10-state runtime status (READY, APPLYING, APPLIED, LIMITED,
 *     HDR, EXCLUSIVE_FULLSCREEN, UNSUPPORTED, ERROR, RESTORING, SAFE_MODE)
 *   - Quick actions grouped by intent (engine, looks, system)
 *   - Live preview with split + curve + histogram
 *   - Performance: no per-frame allocation, DPI-aware, keyboard accessible
 */

#include "ui_internal.h"
#include "../color/color_math.h"
#include "../ui_theme.h"
#include "../core/runtime_status.h"
#include "../core/version.h"

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
        /* Tone based on HDR state */
        if (mi->hdr_enabled) k->flags = PX_CHIP_WARN;
        else if (mi->hdr_capable) k->flags = PX_CHIP_INFO;
        else k->flags = PX_CHIP_OK;
    }
}

static void derive_runtime_state(PxRuntimeState *rt)
{
    const PxEffectiveState *e = UiStatus();
    const AppliedColorState *a = Eng_Applied();
    MonitorInfo *mi = Modes_GetMonitor(Modes_CurrentMonitorIndex());
    const PxGameDisplayState *gs = Prof_GameState();
    int hdr = mi ? PxHdr_StateOf(mi) : PX_HDR_DISABLED;
    int pres = gs ? gs->presentation : PX_PRES_NONE;
    int game_active = gs ? gs->detected : 0;

    PxRuntimeInput in;
    memset(&in, 0, sizeof in);
    in.eff = e;
    in.applied = a;
    in.monitor = mi;
    in.hdr_state = hdr;
    in.pres_mode = pres;
    in.game_active = game_active;
    in.restoring = Wm_PendingSettle();
    in.safe_mode = 0; /* TODO: wire safe mode flag */
    in.mag_available = Dm_GpuInfo() ? Dm_GpuInfo()->mag_available : 0;
    in.gamma_available = Dm_GpuInfo() ? Dm_GpuInfo()->gamma_available : 0;

    PxRuntime_Derive(&in, rt);
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
    PxRuntimeState rt;

    derive_runtime_state(&rt);

    UiW(WT_HEAD, 0, 248, 62, 620, 30, L"Command Center");
    UiW(WT_LABEL, 0, 248, 92, 900, 18,
        L"Requested → Effective → Applied: three truths, one honest status. Everything below is live.");

    /* ---- runtime status banner (10-state) ---- */
    {
        wchar_t status_line[160];
        wchar_t reason_line[160];
        MultiByteToWideChar(CP_UTF8, 0, PxRt_ShortLabel(rt.status), -1, status_line, 160);
        MultiByteToWideChar(CP_UTF8, 0, rt.reason, -1, reason_line, 160);
        Widget *k = UiWCard(248, 116, 986, 72, L"PIPELINE STATUS", status_line, reason_line);
        k->flags = rt.tone; /* 0 neutral, 1 ok, 2 warn, 3 bad, 4 info */
        _snwprintf(buf, 192, L"%hs", PxRt_Name(rt.status));
        UiWin(k->text, 96, L"PIPELINE STATUS");
        /* Sub includes hint */
        {
            wchar_t hint[160];
            MultiByteToWideChar(CP_UTF8, 0, rt.hint, -1, hint, 160);
            UiWin(k->sub, 64, hint);
        }
    }

    Modes_Current(&cur);
    mi = Modes_GetMonitor(Modes_CurrentMonitorIndex());
    memset(&gpu_copy, 0, sizeof gpu_copy);
    if (gpu) gpu_copy = *gpu;

    /* ---- status cards (row 1) ---- */
    {
        Widget *k = UiWCard(248, 198, 196, 84, L"CURRENT GAME", L"Desktop", L"no game in the foreground");
        if (gs->detected) {
            UiWin(k->val, 48, gs->exe);
            MultiByteToWideChar(CP_UTF8, 0, px_gameout_name(gs->game_output), -1, buf, 190);
            UiWin(k->sub, 64, buf);
            k->flags = (gs->game_output == PX_GAMEOUT_ACTIVE) ? PX_CHIP_OK :
                       (gs->game_output == PX_GAMEOUT_LIMITED) ? PX_CHIP_WARN : PX_CHIP_INFO;
        } else {
            const wchar_t *fg = Prof_CurrentForeground();
            UiWin(k->val, 48, L"Desktop");
            UiWin(k->sub, 64, (fg && fg[0]) ? fg : L"no game in the foreground");
            k->flags = PX_CHIP_NEUTRAL;
        }
    }
    {
        Widget *k = UiWCard(454, 198, 196, 84, L"ACTIVE PROFILE", L"Global look", L"detection off");
        if (ap) {
            UiWin(k->val, 48, ap->name);
            _snwprintf(buf, 192, L"%s · %s", ap->sub[ap->active_sub].name,
                       ap->auto_apply ? L"auto-apply" : L"manual");
            UiWin(k->sub, 64, buf);
            k->flags = PX_CHIP_OK;
        } else {
            UiWin(k->sub, 64, Prof_Detect() ? L"automatic detection on" : L"detection off");
            k->flags = Prof_Detect() ? PX_CHIP_INFO : PX_CHIP_NEUTRAL;
        }
    }
    card_monitor(660, 198, 196, 84);
    {
        Widget *k = UiWCard(866, 198, 180, 84, L"RESOLUTION", L"—", L"—");
        _snwprintf(buf, 192, L"%d × %d", cur.w, cur.h);
        UiWin(k->val, 48, buf);
        _snwprintf(buf, 192, L"%s mode · %d Hz", cur.native ? L"native" : L"custom", cur.hz);
        UiWin(k->sub, 64, buf);
        k->flags = cur.native ? PX_CHIP_OK : PX_CHIP_WARN;
    }
    {
        Widget *k = UiWCard(1056, 198, 178, 84, L"REFRESH RATE", L"—", L"—");
        _snwprintf(buf, 192, L"%d Hz", cur.hz);
        UiWin(k->val, 48, buf);
        _snwprintf(buf, 192, L"%s", cur.hz >= 240 ? L"high refresh" : cur.hz >= 144 ? L"standard gaming" : L"current output");
        UiWin(k->sub, 64, buf);
        k->flags = cur.hz >= 144 ? PX_CHIP_OK : PX_CHIP_NEUTRAL;
    }

    /* ---- status cards (row 2) ---- */
    {
        int hdr = mi ? PxHdr_StateOf(mi) : PX_HDR_DISABLED;
        Widget *k = UiWCard(248, 292, 196, 84, L"HDR", L"—", L"—");
        MultiByteToWideChar(CP_UTF8, 0, PxHdr_Name(hdr), -1, buf, 190);
        UiWin(k->val, 48, buf);
        MultiByteToWideChar(CP_UTF8, 0, PxHdr_Explain(hdr), -1, buf, 190);
        UiWin(k->sub, 64, buf);
        k->flags = PxHdr_Tone(hdr);
    }
    {
        Widget *k = UiWCard(454, 292, 196, 84, L"COLOR ENGINE", L"—", L"—");
        MultiByteToWideChar(CP_UTF8, 0, PxStatus_Name(e->status), -1, buf, 190);
        UiWin(k->val, 48, buf);
        MultiByteToWideChar(CP_UTF8, 0, e->reason, -1, buf, 190);
        UiWin(k->sub, 64, buf);
        k->flags = PxStatus_Tone(e->status);
    }
    {
        Widget *k = UiWCard(660, 292, 196, 84, L"APPLICATION STATUS", L"—", L"—");
        _snwprintf(buf, 192, L"v%s · %d profile%s · %s",
                   PX_VERSION_STRING,
                   Prof_Count(), Prof_Count() == 1 ? L"" : L"s", Prof_Detect() ? L"auto" : L"manual");
        UiWin(k->val, 48, buf);
        {
            const AppliedColorState *a = Eng_Applied();
            if (a && a->have)
                _snwprintf(buf, 192, L"rev %u · %d ramp write%s · %s",
                           a->revision, a->ramp_writes,
                           a->ramp_writes == 1 ? L"" : L"s",
                           PxAS_OutcomeName(a->outcome));
            else
                UiWin(buf, 192, L"nothing applied yet");
            UiWin(k->sub, 64, buf);
        }
        k->flags = PX_CHIP_OK;
    }
    {
        Widget *k = UiWCard(866, 292, 368, 84, L"GPU", L"—", L"—");
        UiWin(k->val, 48, gpu_copy.name[0] ? gpu_copy.name : (gpu_copy.vendor_name[0] ? gpu_copy.vendor_name : L"not reported"));
        _snwprintf(buf, 192, L"%s driver · matrix %s · ramp %s",
                   gpu_copy.driver_ver[0] ? gpu_copy.driver_ver : L"unknown",
                   gpu_copy.mag_available ? L"✓" : L"✗",
                   gpu_copy.gamma_available ? L"✓" : L"✗");
        UiWin(k->sub, 64, buf);
        k->flags = (gpu_copy.mag_available && gpu_copy.gamma_available) ? PX_CHIP_OK :
                   (gpu_copy.mag_available || gpu_copy.gamma_available) ? PX_CHIP_WARN : PX_CHIP_BAD;
    }

    /* ---- quick actions — grouped by intent ---- */
    UiW(WT_DIV, 0, 248, 386, 986, 20, L"ENGINE CONTROLS");
    {
        Widget *on = UiW(WT_PRIMARY, ID_B_ENGINE_ON, 248, 410, 150, 36, L"Enable Engine");
        Widget *off = UiW(WT_GHOST, ID_B_ENGINE_OFF, 408, 410, 150, 36, L"Disable Engine");
        on->state = Eng_GetRequested()->enabled ? 1 : 0;
        off->state = on->state ? 0 : 1;
        (void)on; (void)off;
    }
    UiW(WT_GHOST, ID_B_OPEN_COLOR,   568, 410, 150, 36, L"Global Color");
    UiW(WT_GHOST, ID_B_OPEN_GAME,    728, 410, 150, 36, L"Game Profile");
    UiW(WT_GHOST, ID_B_RESET_DISPLAY,888, 410, 150, 36, L"Reset Display");
    UiW(WT_GHOST, ID_B_OPEN_DIAG,   1048, 410, 186, 36, L"Diagnostics");

    UiW(WT_DIV, 0, 248, 456, 986, 20, L"LOOK PRESETS — ONE CLICK");
    UiW(WT_GHOST, ID_B_HOME_APPLY_GAME,  248, 480, 190, 36, L"Apply Active Profile");
    UiW(WT_GHOST, ID_B_HOME_COMPETITIVE, 448, 480, 160, 36, L"Competitive");
    UiW(WT_GHOST, ID_B_HOME_MAX_VIB,     618, 480, 160, 36, L"Max Vibrance");
    UiW(WT_GHOST, ID_B_HOME_NIGHT_VIS,   788, 480, 150, 36, L"Night Ops");
    UiW(WT_GHOST, ID_B_HOME_CINEMATIC,   948, 480, 140, 36, L"Cinematic");
    UiW(WT_GHOST, ID_B_HOME_NATURAL,    1098, 480, 136, 36, L"Neutral");

    UiW(WT_DIV, 0, 248, 526, 986, 20, L"SYSTEM");
    {
        Widget *gm = UiW(WT_BTN, ID_B_HOME_GAMING_MODE, 248, 550, 240, 32,
                         Tools_IsGamingMode() ? L"Gaming mode: ON — lightweight" : L"Gaming mode: OFF");
        gm->flags = Tools_IsGamingMode() ? 1 : 0;
    }
    UiW(WT_LABEL, 0, 500, 552, 734, 26,
        L"Gaming mode lowers UI timers while pipeline stays event-driven; never pauses color output.");

    /* ---- live preview — premium ---- */
    UiW(WT_DIV, 0, 248, 586, 986, 20, L"LIVE PREVIEW  ·  SAME MATH AS PIPELINE  ·  DRAG DIVIDER  ·  WHEEL = NUDGE");
    UiW(WT_SPLIT_PREVIEW, 0, 248, 606, 620, 196, NULL);
    UiW(WT_CURVE_PREVIEW, 0, 878, 606, 356, 196, NULL);

    {
        Widget *t = UiW(WT_TOGGLE, ID_T_LOOK_ENABLE, 248, 810, 300, 24, L"Colour engine enabled");
        t->state = Eng_GetRequested()->enabled;
    }
    UiW(WT_LABEL, 0, 560, 810, 674, 18,
        L"Wheel = nudge · Shift+wheel = coarse · Ctrl+wheel = fine · Dbl-click = neutral · Ctrl+Alt+0 = reset all");
}
