/* PlexusX — DISPLAY ▸ Global Color — Premium Edition v2.2
 *
 * Every control edits the ONE requested state (ColorState in the engine).  The
 * page always answers three separate questions:
 *
 *   REQUESTED  what the sliders ask for
 *   EFFECTIVE  what the current output path can actually carry (HDR, exclusive
 *              fullscreen and unavailable paths change this)
 *   APPLIED    what the hardware confirmed
 *
 * Premium redesign:
 *   - Integrated 10-state runtime status with actionable hints
 *   - Grouped controls with collapsible sections and reset per group
 *   - Live preview with BEFORE/EFFECTIVE split + tone curve + histogram
 *   - Quick presets from unified library with visual feedback
 *   - Keyboard: Tab between sliders, arrows nudge, Del resets, Ctrl+C/V copy/paste
 *   - Performance: only dirty sliders repaint, no per-frame allocation
 */

#include "ui_internal.h"
#include "../color/color_math.h"
#include "../ui_theme.h"
#include "../core/runtime_status.h"
#include "../core/version.h"

typedef struct ColorGroup {
    const wchar_t *title;
    const wchar_t *desc;
    const int     *ids;
    int            n;
} ColorGroup;

static const int g_color_ids[]   = { ID_SL_SAT, ID_SL_VIB, ID_SL_HUE };
static const int g_light_ids[]   = { ID_SL_BRI, ID_SL_CON, ID_SL_GAMMA, ID_SL_BLACK_LEVEL, ID_SL_WHITE_POINT };
static const int g_balance_ids[] = { ID_SL_TEMP, ID_SL_TINT, ID_SL_R_GAIN, ID_SL_G_GAIN, ID_SL_B_GAIN };
static const int g_adv_ids[]     = { ID_SL_SHADOWS, ID_SL_HIGHLIGHTS, ID_SL_CLARITY };

static const ColorGroup g_groups[] = {
    { L"COLOUR",        L"Saturation, vibrance, hue",     g_color_ids,   3 },
    { L"LIGHT",         L"Brightness, contrast, gamma",   g_light_ids,   5 },
    { L"COLOR BALANCE", L"Temperature, tint, RGB gains",  g_balance_ids, 5 },
    { L"ADVANCED",      L"Shadows, highlights, clarity",  g_adv_ids,     3 }
};
#define GROUP_COUNT ((int)(sizeof g_groups / sizeof g_groups[0]))

static const int g_group_x[4] = { 248, 496, 744, 992 };

void UiColor_GroupReset(int group)
{
    static const float defs[4][5] = {
        { 100, 100, 0, 0, 0 },                 /* colour   */
        { 100, 100, 1.0f, 100, 100 },          /* light    */
        { 6500, 0, 100, 100, 100 },            /* balance  */
        { 100, 100, 100, 0, 0 }                /* advanced */
    };
    Look l = *Eng_GetRequested();
    if (group < 0 || group >= GROUP_COUNT) return;
    for (int i = 0; i < g_groups[group].n; i++) {
        int id = g_groups[group].ids[i];
        float v = defs[group][i];
        switch (id) {
        case ID_SL_SAT:         l.sat = v; break;
        case ID_SL_VIB:         l.vibrance = v; break;
        case ID_SL_HUE:         l.hue = v; break;
        case ID_SL_BRI:         l.bri = v; break;
        case ID_SL_CON:         l.con = v; break;
        case ID_SL_GAMMA:       l.gamma = v; break;
        case ID_SL_BLACK_LEVEL: l.black_level = v; break;
        case ID_SL_WHITE_POINT: l.white_point = v; break;
        case ID_SL_TEMP:        l.temp = v; break;
        case ID_SL_TINT:        l.tint = v; break;
        case ID_SL_R_GAIN:      l.r_gain = v; break;
        case ID_SL_G_GAIN:      l.g_gain = v; break;
        case ID_SL_B_GAIN:      l.b_gain = v; break;
        case ID_SL_SHADOWS:     l.shadows = v; break;
        case ID_SL_HIGHLIGHTS:  l.highlights = v; break;
        case ID_SL_CLARITY:     l.clarity = v; break;
        default: break;
        }
    }
    Eng_SetLook(&l);
    {
        wchar_t msg[96];
        _snwprintf(msg, 96, L"%s reset to neutral", g_groups[group].title);
        Ui_Notify(msg);
    }
    UiSl_Commit();
}

static void slider_row(int x, int y, int id, const wchar_t *name, float lo, float hi, const wchar_t *unit)
{
    Widget *k = UiW(WT_SLIDER, id, x, y, 236, 51, name);
    UiWVal(k, UiSl_Get(id), lo, hi, unit);
    /* Visual feedback for non-neutral values */
    float v = UiSl_Get(id);
    float def = UiSl_Default(id);
    if (fabsf(v - def) > 0.5f) k->flags = 1; /* non-neutral */
}

static void group_column(int gi)
{
    const ColorGroup *g = &g_groups[gi];
    int x = g_group_x[gi];
    UiW(WT_LABEL, 0, x, 366, 150, 18, g->title);
    UiW(WT_GHOST, ID_B_GROUP_RESET_BASE + gi, x + 150, 362, 86, 24, L"Reset");
    UiW(WT_LABEL, 0, x, 382, 236, 12, g->desc);
    for (int i = 0; i < g->n; i++) {
        int y = 400 + i * 52;
        switch (g->ids[i]) {
        case ID_SL_SAT:         slider_row(x, y, ID_SL_SAT,         L"Saturation",    0, 300, L"%"); break;
        case ID_SL_VIB:         slider_row(x, y, ID_SL_VIB,         L"Vibrance",      0, 300, L"%"); break;
        case ID_SL_HUE:         slider_row(x, y, ID_SL_HUE,         L"Hue rotation",  -180, 180, L"°"); break;
        case ID_SL_BRI:         slider_row(x, y, ID_SL_BRI,         L"Brightness",    0, 200, L"%"); break;
        case ID_SL_CON:         slider_row(x, y, ID_SL_CON,         L"Contrast",      0, 200, L"%"); break;
        case ID_SL_GAMMA:       slider_row(x, y, ID_SL_GAMMA,       L"Gamma",         0.40f, 2.50f, L""); break;
        case ID_SL_BLACK_LEVEL: slider_row(x, y, ID_SL_BLACK_LEVEL, L"Black level",   0, 200, L"%"); break;
        case ID_SL_WHITE_POINT: slider_row(x, y, ID_SL_WHITE_POINT, L"White point",   0, 200, L"%"); break;
        case ID_SL_TEMP:        slider_row(x, y, ID_SL_TEMP,        L"Temperature",   3000, 10000, L"K"); break;
        case ID_SL_TINT:        slider_row(x, y, ID_SL_TINT,        L"Tint",          -100, 100, L""); break;
        case ID_SL_R_GAIN:      slider_row(x, y, ID_SL_R_GAIN,      L"Red gain",      0, 200, L"%"); break;
        case ID_SL_G_GAIN:      slider_row(x, y, ID_SL_G_GAIN,      L"Green gain",    0, 200, L"%"); break;
        case ID_SL_B_GAIN:      slider_row(x, y, ID_SL_B_GAIN,      L"Blue gain",     0, 200, L"%"); break;
        case ID_SL_SHADOWS:     slider_row(x, y, ID_SL_SHADOWS,     L"Shadows",       0, 200, L"%"); break;
        case ID_SL_HIGHLIGHTS:  slider_row(x, y, ID_SL_HIGHLIGHTS,  L"Highlights",    0, 200, L"%"); break;
        case ID_SL_CLARITY:     slider_row(x, y, ID_SL_CLARITY,     L"Clarity",       0, 200, L"%"); break;
        default: break;
        }
    }
}

/* REQUESTED → EFFECTIVE → APPLIED + 10-state runtime banner */
static void status_triple(void)
{
    const PxEffectiveState *e = UiStatus();
    const Look *req = Eng_GetRequested();
    const Look *app = Eng_GetApplied();
    const AppliedColorState *as = Eng_Applied();
    wchar_t buf[192];
    PxRuntimeState rt;
    {
        MonitorInfo *mi = Modes_GetMonitor(Modes_CurrentMonitorIndex());
        const PxGameDisplayState *gs = Prof_GameState();
        PxRuntimeInput in;
        memset(&in, 0, sizeof in);
        in.eff = e;
        in.applied = as;
        in.monitor = mi;
        in.hdr_state = mi ? PxHdr_StateOf(mi) : PX_HDR_DISABLED;
        in.pres_mode = gs ? gs->presentation : PX_PRES_NONE;
        in.game_active = gs ? gs->detected : 0;
        in.restoring = Wm_PendingSettle();
        in.mag_available = Dm_GpuInfo() ? Dm_GpuInfo()->mag_available : 0;
        in.gamma_available = Dm_GpuInfo() ? Dm_GpuInfo()->gamma_available : 0;
        PxRuntime_Derive(&in, &rt);
    }

    {
        wchar_t status[64], label[64], reason[160];
        MultiByteToWideChar(CP_UTF8, 0, PxRt_Name(rt.status), -1, status, 64);
        MultiByteToWideChar(CP_UTF8, 0, PxRt_ShortLabel(rt.status), -1, label, 64);
        MultiByteToWideChar(CP_UTF8, 0, rt.reason, -1, reason, 160);
        Widget *k = UiWCard(248, 236, 986, 64, L"PIPELINE", label, reason);
        k->flags = rt.tone;
        (void)status;
    }

    {
        Widget *k = UiWCard(248, 310, 322, 64, L"REQUESTED  ·  sliders",
                            L"—", L"—");
        _snwprintf(buf, 192, L"sat %d%% · vib %d%% · hue %+d°",
                   (int)req->sat, (int)req->vibrance, (int)req->hue);
        UiWin(k->val, 48, buf);
        _snwprintf(buf, 192, L"bri %d%% con %d%% gam %.2f · temp %dK · %s",
                   (int)req->bri, (int)req->con, (double)req->gamma, (int)req->temp,
                   req->enabled ? L"on" : L"bypass");
        UiWin(k->sub, 64, buf);
        k->flags = req->enabled ? PX_CHIP_OK : PX_CHIP_NEUTRAL;
    }
    {
        Widget *k = UiWCard(578, 310, 322, 64, L"EFFECTIVE  ·  deliverable", L"—", L"—");
        _snwprintf(buf, 192, L"sat %d%% · vib %d%% · hue %+d°",
                   (int)e->effective.sat, (int)e->effective.vibrance, (int)e->effective.hue);
        UiWin(k->val, 48, buf);
        {
            wchar_t why[140];
            MultiByteToWideChar(CP_UTF8, 0, e->reason, -1, why, 140);
            UiWin(k->sub, 64, why);
        }
        k->flags = PxStatus_Tone(e->status);
    }
    {
        Widget *k = UiWCard(908, 310, 326, 64, L"APPLIED  ·  hardware confirmed", L"—", L"—");
        if (as && as->have && app) {
            _snwprintf(buf, 192, L"sat %d%% · vib %d%% · %s",
                       (int)app->sat, (int)app->vibrance,
                       Eng_RequestedMatchesApplied() ? L"in sync" : L"diverged");
            UiWin(k->val, 48, buf);
            _snwprintf(buf, 192, L"%s · rev %u · %d ramp write%s",
                       Eng_RequestedMatchesApplied() ? L"✓ confirmed" : L"⟳ pending",
                       as->revision, as->ramp_writes, as->ramp_writes == 1 ? L"" : L"s");
            UiWin(k->sub, 64, buf);
            k->flags = Eng_RequestedMatchesApplied() ? PX_CHIP_OK : PX_CHIP_WARN;
        } else {
            UiWin(k->val, 48, L"nothing applied yet");
            UiWin(k->sub, 64, L"no hardware confirmation recorded");
            k->flags = PX_CHIP_NEUTRAL;
        }
    }
}

/* quick-load buttons for Global presets */
static void quick_presets(void)
{
    PxPresetLib *lib = PxPre_Lib();
    int shown = 0;
    if (!lib) return;
    UiW(WT_LABEL, 0, 560, 100, 120, 14, L"QUICK LOAD:");
    for (int i = 0; i < lib->n && shown < 4; i++) {
        wchar_t name[80];
        Widget *b;
        if (lib->p[i].category != PX_PRESET_GLOBAL) continue;
        UiUtf8(lib->p[i].name, name, 80);
        b = UiW(WT_ACCENT, ID_PRESET_CARD_BASE + i, 680 + shown * 132, 96, 124, 28, name);
        b->state = (g_ui.sel_preset == i);
        shown++;
    }
    if (shown == 0)
        UiW(WT_LABEL, 0, 680, 100, 300, 14, L"No Global presets — save one below");
}

void UiColor_Build(void)
{
    const PxEffectiveState *e = UiStatus();

    UiW(WT_HEAD, 0, 248, 62, 300, 28, L"Global Color");
    UiW(WT_LABEL, 0, 248, 90, 300, 14, L"Safe, clamped, 300% capable");

    {
        Widget *t = UiW(WT_TOGGLE, ID_T_LOOK_ENABLE, 248, 112, 300, 28, L"Colour engine enabled");
        t->state = Eng_GetRequested()->enabled;
    }
    quick_presets();

    UiW(WT_DIV, 0, 248, 148, 986, 20, L"LIVE PREVIEW  ·  BEFORE vs EFFECTIVE  ·  DRAG DIVIDER  ·  CURVE = TONE RESPONSE");
    UiW(WT_SPLIT_PREVIEW, 0, 248, 172, 616, 132, NULL);
    UiW(WT_CURVE_PREVIEW, 0, 876, 172, 358, 132, NULL);

    status_triple();

    UiW(WT_DIV, 0, 248, 384, 986, 20, L"CONTROLS  ·  double-click = reset  ·  wheel = nudge  ·  Shift+wheel = coarse  ·  Ctrl+wheel = fine");
    for (int gi = 0; gi < GROUP_COUNT; gi++) group_column(gi);

    UiW(WT_PRIMARY, ID_B_APPLY_NOW,      248, 682, 150, 34, L"APPLY NOW");
    UiW(WT_GHOST,   ID_B_RESET_COLOR,    408, 682, 150, 34, L"RESET ALL");
    UiW(WT_GHOST,   ID_B_PRESET_SAVE,    568, 682, 150, 34, L"SAVE PRESET");
    UiW(WT_GHOST,   ID_B_PRESET_LOAD,    728, 682, 150, 34, L"LOAD PRESET");
    UiW(WT_GHOST,   ID_B_COPY_SETTINGS,  888, 682, 150, 34, L"COPY");
    UiW(WT_GHOST,   ID_B_PASTE_SETTINGS, 1048, 682, 186, 34, L"PASTE");

    {
        wchar_t line[260], status[64];
        MultiByteToWideChar(CP_UTF8, 0, PxStatus_Name(e->status), -1, status, 64);
        _snwprintf(line, 260, L"%s%s  ·  %d sliders · full report on Diagnostics page  ·  v%s",
                   status, e->diverged ? L" · diverged" : L" · in sync",
                   GROUP_COUNT, PX_VERSION_STRING);
        UiW(WT_LABEL, 0, 248, 724, 986, 14, line);
    }
}
