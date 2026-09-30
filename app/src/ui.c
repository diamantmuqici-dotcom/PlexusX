/* PlexusX — Complete Redesigned Desktop UI
 * 10 Panels: Home, Games, Display, Color, Presets, Crosshair, Monitors, Automation, Tools, Settings.
 * Retained/Immediate GDI UI with High DPI scaling and double-buffered rendering.
 */
#include "common.h"

/* Theme Colors */
#define C_BG       RGB(12, 12, 16)
#define C_SIDE     RGB(16, 16, 22)
#define C_CARD     RGB(22, 22, 30)
#define C_CARD2    RGB(30, 30, 42)
#define C_CARD_ACT RGB(36, 36, 52)
#define C_LINE     RGB(42, 42, 58)
#define C_TXT      RGB(242, 242, 248)
#define C_SUB      RGB(145, 145, 160)
#define C_DIM      RGB(95, 95, 110)
#define C_ACC      RGB(198, 255, 61)     /* Vibrant Lime */
#define C_ACC2     RGB(79, 227, 255)     /* Cyan */
#define C_PURPLE   RGB(139, 92, 246)
#define C_DARK     RGB(10, 10, 14)
#define C_DANG     RGB(255, 80, 80)

static HWND    g_ui_hwnd;
static float   g_sc = 1.0f;
static Widget  g_w[MAX_WIDGETS];
static int     g_nw = 0;
static int     g_panel = ID_SIDE_HOME;
static Look    g_look = { 1, 150, 120, 100, 100, 1.00f, 6500, 0, 100, 100, 100, 100, 100, 100, 100, 100, 0 };
static XhCfg   g_xh;
static int     g_active_widget = 0;
static int     g_hover_widget = 0;
static int     g_drag_split = 0;
static float   g_split_pos = 0.50f; /* 0.0 to 1.0 for Before/After preview */

static wchar_t g_toast[128];
static DWORD   g_toast_time = 0;
static wchar_t g_game_filter[64] = { 0 };

static HFONT   g_fLogo, g_fH1, g_fH2, g_fBody, g_fSmall, g_fMono, g_fBigVal;

static int S(int v) { return (int)(v * g_sc + 0.5f); }

static void make_fonts(void)
{
    g_fLogo   = CreateFontW(-S(20), 0, 0, 0, FW_BOLD, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
    g_fH1     = CreateFontW(-S(26), 0, 0, 0, FW_BOLD, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
    g_fH2     = CreateFontW(-S(16), 0, 0, 0, FW_SEMIBOLD, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
    g_fBody   = CreateFontW(-S(14), 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
    g_fSmall  = CreateFontW(-S(12), 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
    g_fMono   = CreateFontW(-S(14), 0, 0, 0, FW_BOLD, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Consolas");
    g_fBigVal = CreateFontW(-S(42), 0, 0, 0, FW_BOLD, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
}

/* ---------------- Widget Builder Helpers ---------------- */

static Widget *wadd(int type, int id, int x, int y, int w, int h, const wchar_t *text)
{
    if (g_nw >= MAX_WIDGETS) return &g_w[0];
    Widget *k = &g_w[g_nw++];
    memset(k, 0, sizeof *k);
    k->type = type;
    k->id = id;
    k->rc = (RECT){ S(x), S(y), S(x + w), S(y + h) };
    if (text) lstrcpynW(k->text, text, 96);
    return k;
}

static void set_slider(Widget *k, float val, float min_v, float max_v, const wchar_t *unit)
{
    k->vmin = min_v;
    k->vmax = max_v;
    if (wcscmp(unit, L"K") == 0) {
        wsprintfW(k->val, L"%dK", (int)val);
    } else if (wcscmp(unit, L"gamma") == 0) {
        _snwprintf(k->val, 48, L"%.2f", val);
    } else if (wcscmp(unit, L"deg") == 0) {
        wsprintfW(k->val, L"%+d°", (int)val);
    } else if (wcscmp(unit, L"tint") == 0) {
        wsprintfW(k->val, L"%+d%%", (int)val);
    } else {
        wsprintfW(k->val, L"%d%%", (int)val);
    }
}

/* ---------------- Sidebar Navigation ---------------- */
static void build_sidebar(void)
{
    static const wchar_t *nav_names[] = {
        L"Dashboard", L"Games", L"Display", L"Color Engine", L"Presets",
        L"Crosshair", L"Monitors", L"Automation", L"Tools", L"Settings"
    };

    for (int i = 0; i < ID_SIDE_COUNT; i++) {
        Widget *k = wadd(WT_SIDE, ID_SIDE_BASE + i, 14, 84 + i * 46, 192, 40, nav_names[i]);
        k->state = (ID_SIDE_BASE + i == g_panel);
    }
}

/* ---------------- Window Caption & Top Bar ---------------- */
static void build_topbar(void)
{
    wadd(WT_BTN, ID_CAP_MIN,   PX_WIN_W - 96, 12, 38, 30, L"-");
    wadd(WT_BTN, ID_CAP_CLOSE, PX_WIN_W - 50, 12, 38, 30, L"✕");
}

/* ---------------- 1. HOME DASHBOARD ---------------- */
static void build_panel_home(void)
{
    wadd(WT_HEAD, 0, 248, 80, 500, 36, L"Gaming Display Dashboard");
    wadd(WT_LABEL, 0, 248, 118, 600, 20, L"Active foreground status, display parameters & instant game tuning");

    /* Active Game Card */
    Widget *gcard = wadd(WT_CARD, 0, 248, 150, 480, 100, L"CURRENT FOREGROUND");
    Profile *p = Prof_Get(Prof_ActiveIndex());
    if (p) {
        wsprintfW(gcard->val, L"%s", p->name);
        wsprintfW(gcard->sub, L"Active Profile: %s  ·  %s", p->sub[p->active_sub].name, p->tag);
    } else {
        lstrcpyW(gcard->val, L"Desktop Environment");
        lstrcpyW(gcard->sub, L"Default Display Profile Active");
    }

    /* Display Hardware Card */
    Widget *dcard = wadd(WT_CARD, 0, 744, 150, 490, 100, L"ACTIVE DISPLAY HARDWARE");
    ModeInfo cur;
    Modes_Current(&cur);
    const GpuInfo *gpu = Eng_GetGpuInfo();
    wsprintfW(dcard->val, L"%d × %d @ %d Hz", cur.w, cur.h, cur.hz);
    wsprintfW(dcard->sub, L"%s  ·  %s", gpu->vendor_name, cur.native ? L"Native Resolution" : L"Custom Stretched");

    /* Quick Color Stats Card */
    Widget *scard = wadd(WT_CARD, 0, 248, 266, 986, 76, L"ACTIVE COLOR PROFILE ENGINE");
    wsprintfW(scard->val, L"Sat: %d%%   Vib: %d%%   Bri: %d%%   Con: %d%%   Gamma: %.2f   Temp: %dK",
              (int)g_look.sat, (int)g_look.vibrance, (int)g_look.bri, (int)g_look.con, g_look.gamma, (int)g_look.temp);
    lstrcpyW(scard->sub, g_look.enabled ? L"● Engine Active · Magnification API + GPU Gamma Ramps" : L"○ Engine Bypassed (Neutral Display)");

    /* Quick Actions */
    wadd(WT_DIV, 0, 248, 356, 986, 24, L"ONE-CLICK QUICK MODES");
    wadd(WT_PRIMARY, ID_B_HOME_COMPETITIVE, 248, 386, 150, 40, L"Competitive");
    wadd(WT_GHOST,   ID_B_HOME_MAX_VIB,     408, 386, 150, 40, L"300% Vibrance");
    wadd(WT_GHOST,   ID_B_HOME_NIGHT_VIS,   568, 386, 150, 40, L"Night Visibility");
    wadd(WT_GHOST,   ID_B_HOME_CINEMATIC,   728, 386, 140, 40, L"Cinematic");
    wadd(WT_GHOST,   ID_B_HOME_NATURAL,     878, 386, 130, 40, L"Natural");
    wadd(WT_ACCENT,  ID_B_RESET_COLOR,     1018, 386, 116, 40, L"Reset All");

    /* Live Visual Preview (Before / After Split) */
    wadd(WT_DIV, 0, 248, 442, 986, 24, L"LIVE VISUAL PREVIEW  (DRAG DIVIDER TO COMPARE)");
    Widget *prev = wadd(WT_SPLIT_PREVIEW, 0, 248, 472, 986, 260, NULL);
    (void)prev;

    /* Bottom quick toggles */
    Widget *gm_btn = wadd(WT_BTN, ID_B_HOME_GAMING_MODE, 248, 746, 220, 36,
                          Tools_IsGamingMode() ? L"★ Gaming Mode: ACTIVE" : L"☆ Gaming Mode: OFF");
    gm_btn->flags = Tools_IsGamingMode() ? 1 : 0;
    wadd(WT_GHOST, ID_B_BACKUP_NOW, 480, 746, 200, 36, L"Backup Display State");
    wadd(WT_GHOST, ID_B_HOME_APPLY_GAME, 692, 746, 220, 36, L"Apply Current Game Look");
}

/* ---------------- 2. GAMES PANEL ---------------- */
static void build_panel_games(void)
{
    wadd(WT_HEAD, 0, 248, 80, 600, 36, L"Game Profiles Library");
    wadd(WT_LABEL, 0, 248, 118, 700, 20, L"Dedicated starting looks per game. Automatically switches when game launches.");

    wadd(WT_DIV, 0, 248, 150, 986, 24, L"AVAILABLE PROFILES");

    int count = Prof_Count();
    int row = 0;
    for (int i = 0; i < count && row < 8; i++) {
        Profile *p = Prof_Get(i);
        if (g_game_filter[0] && !wcsstr(p->name, g_game_filter)) continue;

        int y = 184 + row * 66;
        Widget *card = wadd(WT_GAME_CARD, ID_GAME_CARD_BASE + i, 248, y, 986, 58, p->name);
        card->state = (i == Prof_ActiveIndex());
        wsprintfW(card->sub, L"%s  ·  %s", p->exe, p->tag);
        wsprintfW(card->val, L"Mode: %s (%d modes)", p->sub[p->active_sub].name, p->sub_count);

        row++;
    }

    wadd(WT_GHOST, ID_B_CUSTOM_GAME_ADD, 248, 730, 220, 42, L"+ Add Custom Game");
    wadd(WT_PRIMARY, ID_B_HOME_APPLY_GAME, 480, 730, 220, 42, L"Apply Selected Profile");
}

/* ---------------- 3. DISPLAY PANEL ---------------- */
static void build_panel_display(void)
{
    wadd(WT_HEAD, 0, 248, 80, 600, 36, L"Display & Refresh Rate");
    wadd(WT_LABEL, 0, 248, 118, 700, 20, L"Resolution, refresh rates, stretched 4:3 modes & HDR management.");

    ModeInfo cur;
    Modes_Current(&cur);
    Widget *cur_card = wadd(WT_CARD, 0, 248, 150, 986, 74, L"CURRENT ACTIVE DISPLAY MODE");
    wsprintfW(cur_card->val, L"%d × %d @ %d Hz  (%s)", cur.w, cur.h, cur.hz,
              cur.aspect == 1 ? L"4:3 Stretched" : cur.aspect == 2 ? L"16:10" : cur.aspect == 3 ? L"Ultrawide" : L"16:9 Native");
    lstrcpyW(cur_card->sub, L"Legitimate Windows display settings change with automatic rollback safety.");

    wadd(WT_DIV, 0, 248, 238, 986, 24, L"SUPPORTED RESOLUTION & REFRESH PRESETS");

    int mode_cnt = Modes_Count();
    int shown = 0;
    for (int i = 0; i < mode_cnt && shown < 8; i++) {
        ModeInfo *m = Modes_Get(i);
        int y = 270 + shown * 46;
        wchar_t buf[80];
        wsprintfW(buf, L"%d × %d @ %d Hz", m->w, m->h, m->hz);
        Widget *row = wadd(WT_ROW, ID_MODE_ROW_BASE + i, 248, y, 986, 40, buf);
        row->state = (m->w == cur.w && m->h == cur.h && m->hz == cur.hz);
        if (m->aspect == 1) lstrcpyW(row->sub, L"4:3 Stretched");
        else if (m->aspect == 2) lstrcpyW(row->sub, L"16:10");
        else if (m->aspect == 3) lstrcpyW(row->sub, L"Ultrawide");
        else lstrcpyW(row->sub, m->native ? L"Native 16:9" : L"16:9");
        shown++;
    }

    wadd(WT_PRIMARY, ID_B_MODE_APPLY,   248, 660, 200, 44, L"Apply Selected Mode");
    wadd(WT_GHOST,   ID_B_MODE_MAX_HZ,  460, 660, 220, 44, L"Max Available Hz");
    wadd(WT_GHOST,   ID_B_MODE_NATIVE,  692, 660, 200, 44, L"Reset to Native");
    wadd(WT_GHOST,   ID_B_MODE_OPENHDR, 904, 660, 230, 44, L"Windows HDR Settings");

    wadd(WT_DIV, 0, 248, 718, 986, 20, L"HDR GUIDANCE");
    wadd(WT_LABEL, 0, 248, 744, 986, 40,
         L"Why does HDR look washed out on some games? Windows maps SDR game buffers to sRGB space in HDR mode. Use PlexusX Saturation (150-220%) to restore vibrant dynamic colors in HDR!");
}

/* ---------------- 4. COLOR PANEL ---------------- */
static void build_panel_color(void)
{
    wadd(WT_HEAD, 0, 248, 70, 500, 36, L"Global Color Engine");
    wadd(WT_LABEL, 0, 248, 106, 700, 20, L"Hardware-accelerated saturation, vibrance, gamma curve, shadows and tone controls.");

    Widget *t = wadd(WT_TOGGLE, ID_T_LOOK_ENABLE, 248, 136, 280, 34, L"Engine Active");
    t->state = g_look.enabled;

    /* Quick Saturation Buttons */
    wadd(WT_LABEL, 0, 560, 142, 140, 20, L"Quick Saturation:");
    wadd(WT_QUICK_SAT, ID_B_SAT_100, 710, 136, 64, 32, L"100%");
    wadd(WT_QUICK_SAT, ID_B_SAT_150, 782, 136, 64, 32, L"150%");
    wadd(WT_QUICK_SAT, ID_B_SAT_200, 854, 136, 64, 32, L"200%");
    wadd(WT_QUICK_SAT, ID_B_SAT_250, 926, 136, 64, 32, L"250%");
    wadd(WT_QUICK_SAT, ID_B_SAT_300, 998, 136, 64, 32, L"300%");

    /* Column 1 Sliders */
    Widget *s1 = wadd(WT_SLIDER, ID_SL_SAT, 248, 186, 470, 68, L"Saturation (0–300%)");
    set_slider(s1, g_look.sat, 0, 300, L"%");

    Widget *s2 = wadd(WT_SLIDER, ID_SL_VIB, 248, 260, 470, 68, L"Vibrance (Smart Saturation)");
    set_slider(s2, g_look.vibrance, 0, 300, L"%");

    Widget *s3 = wadd(WT_SLIDER, ID_SL_BRI, 248, 334, 470, 68, L"Brightness");
    set_slider(s3, g_look.bri, 0, 200, L"%");

    Widget *s4 = wadd(WT_SLIDER, ID_SL_CON, 248, 408, 470, 68, L"Contrast");
    set_slider(s4, g_look.con, 0, 200, L"%");

    Widget *s5 = wadd(WT_SLIDER, ID_SL_SHADOWS, 248, 482, 470, 68, L"Shadows (Toe Lift / Crush)");
    set_slider(s5, g_look.shadows, 0, 200, L"%");

    Widget *s6 = wadd(WT_SLIDER, ID_SL_HIGHLIGHTS, 248, 556, 470, 68, L"Highlights (Shoulder Compress)");
    set_slider(s6, g_look.highlights, 0, 200, L"%");

    Widget *s7 = wadd(WT_SLIDER, ID_SL_CLARITY, 248, 630, 470, 68, L"Clarity / Dehaze S-Curve");
    set_slider(s7, g_look.clarity, 0, 200, L"%");

    /* Column 2 Sliders */
    Widget *s8 = wadd(WT_SLIDER, ID_SL_GAMMA, 740, 186, 470, 68, L"GPU Gamma Ramp");
    set_slider(s8, g_look.gamma, 0.40f, 2.50f, L"gamma");

    Widget *s9 = wadd(WT_SLIDER, ID_SL_TEMP, 740, 260, 470, 68, L"Color Temperature (Kelvin)");
    set_slider(s9, g_look.temp, 3000, 10000, L"K");

    Widget *s10 = wadd(WT_SLIDER, ID_SL_TINT, 740, 334, 470, 68, L"Tint (Green / Magenta)");
    set_slider(s10, g_look.tint, -100, 100, L"tint");

    Widget *s11 = wadd(WT_SLIDER, ID_SL_R_GAIN, 740, 408, 150, 68, L"Red Channel");
    set_slider(s11, g_look.r_gain, 0, 200, L"%");

    Widget *s12 = wadd(WT_SLIDER, ID_SL_G_GAIN, 900, 408, 150, 68, L"Green Channel");
    set_slider(s12, g_look.g_gain, 0, 200, L"%");

    Widget *s13 = wadd(WT_SLIDER, ID_SL_B_GAIN, 1060, 408, 150, 68, L"Blue Channel");
    set_slider(s13, g_look.b_gain, 0, 200, L"%");

    Widget *s14 = wadd(WT_SLIDER, ID_SL_BLACK_LEVEL, 740, 482, 230, 68, L"Black Level Floor");
    set_slider(s14, g_look.black_level, 0, 200, L"%");

    Widget *s15 = wadd(WT_SLIDER, ID_SL_WHITE_POINT, 980, 482, 230, 68, L"White Point Ceiling");
    set_slider(s15, g_look.white_point, 0, 200, L"%");

    /* Response Curve Preview */
    Widget *curve = wadd(WT_CURVE_PREVIEW, 0, 740, 560, 470, 138, NULL);
    (void)curve;

    /* Bottom Action Buttons */
    wadd(WT_PRIMARY, ID_B_RESET_COLOR, 248, 720, 160, 44, L"Reset All");
    wadd(WT_GHOST,   ID_B_SAVE_PRESET, 420, 720, 180, 44, L"Save as Preset");
    wadd(WT_GHOST,   ID_B_COPY_PRESET, 612, 720, 160, 44, L"Copy Preset");
    wadd(WT_GHOST,   ID_B_EXPORT_PRESET, 784, 720, 180, 44, L"Export JSON");
    wadd(WT_GHOST,   ID_B_IMPORT_PRESET, 976, 720, 180, 44, L"Import JSON");
}

/* ---------------- 5. PRESETS (LOOKS) PANEL ---------------- */
static void build_panel_presets(void)
{
    wadd(WT_HEAD, 0, 248, 80, 600, 36, L"Looks & Presets Library");
    wadd(WT_LABEL, 0, 248, 118, 700, 20, L"Curated parameters for competitive visibility, environments, and panel calibration.");

    int count = 0;
    const SceneDef *scenes = Scene_GetList(&count);

    for (int i = 0; i < count && i < 16; i++) {
        int col = i % 4;
        int row = i / 4;
        int x = 248 + col * 248;
        int y = 160 + row * 128;

        Widget *k = wadd(WT_LOOK_CARD, ID_LOOK_CARD_BASE + i, x, y, 234, 114, scenes[i].name);
        k->flags = i;
        wsprintfW(k->sub, L"Category: %s", scenes[i].category);
        wsprintfW(k->val, L"Sat: %d%%  Gamma: %.2f", (int)scenes[i].look.sat, scenes[i].look.gamma);
    }

    wadd(WT_DIV, 0, 248, 696, 986, 20, L"CUSTOM LOOK MANAGEMENT");
    wadd(WT_GHOST, ID_B_SAVE_PRESET, 248, 726, 200, 42, L"Create New Look");
    wadd(WT_GHOST, ID_B_EXPORT_PRESET, 460, 726, 200, 42, L"Export Preset JSON");
    wadd(WT_GHOST, ID_B_IMPORT_PRESET, 672, 726, 200, 42, L"Import Preset JSON");
}

/* ---------------- 6. CROSSHAIR PANEL ---------------- */
static void build_panel_crosshair(void)
{
    wadd(WT_HEAD, 0, 248, 80, 600, 36, L"Desktop Overlay Crosshair");
    wadd(WT_LABEL, 0, 248, 118, 700, 20, L"Layered click-through overlay. Zero game file modifications or injection.");

    Widget *t = wadd(WT_TOGGLE, ID_T_XH, 248, 150, 320, 36, L"Crosshair Overlay Enabled");
    t->state = g_xh.on;

    wadd(WT_DIV, 0, 248, 200, 986, 24, L"SHAPE SELECTION");

    static const wchar_t *shapes[] = { L"Dot", L"Cross", L"Circle", L"Square", L"Plus", L"Chevron", L"T", L"T-Type" };
    for (int i = 0; i < 8; i++) {
        Widget *k = wadd(WT_SHAPE, ID_XH_SHAPE_BASE + i, 248 + i * 122, 230, 114, 76, shapes[i]);
        k->state = (g_xh.shape == i);
    }

    /* Sliders */
    Widget *s1 = wadd(WT_SLIDER, ID_SL_XH_SIZE, 248, 326, 470, 68, L"Crosshair Size");
    set_slider(s1, (float)g_xh.size, 4, 64, L"%");

    Widget *s2 = wadd(WT_SLIDER, ID_SL_XH_GAP, 740, 326, 470, 68, L"Center Gap");
    set_slider(s2, (float)g_xh.gap, 0, 32, L"%");

    Widget *s3 = wadd(WT_SLIDER, ID_SL_XH_THICK, 248, 400, 470, 68, L"Thickness");
    set_slider(s3, (float)g_xh.thick, 1, 12, L"%");

    Widget *s4 = wadd(WT_SLIDER, ID_SL_XH_OPACITY, 740, 400, 470, 68, L"Opacity");
    set_slider(s4, (float)g_xh.opacity, 10, 100, L"%");

    /* Toggles */
    Widget *to = wadd(WT_TOGGLE, ID_T_XH_OUTLINE, 248, 484, 280, 36, L"Black Outline");
    to->state = g_xh.outline;

    Widget *td = wadd(WT_TOGGLE, ID_T_XH_DOT, 540, 484, 280, 36, L"Center Dot");
    td->state = g_xh.center_dot;

    /* Color Swatches */
    static const COLORREF cols[10] = {
        RGB(198, 255, 61), RGB(79, 227, 255), RGB(255, 79, 227), RGB(255, 158, 61),
        RGB(255, 242, 61), RGB(255, 79, 79), RGB(0, 255, 124), RGB(79, 123, 255),
        RGB(255, 255, 255), RGB(0, 0, 0)
    };

    wadd(WT_LABEL, 0, 248, 540, 300, 20, L"PRIMARY COLOR");
    for (int i = 0; i < 10; i++) {
        Widget *sw = wadd(WT_SWATCH, ID_SWATCH_BASE + i, 248 + i * 50, 568, 42, 34, NULL);
        sw->state = (g_xh.color == cols[i]);
        wsprintfW(sw->val, L"%d", (int)cols[i]);
    }

    wadd(WT_LABEL, 0, 248, 620, 300, 20, L"OUTLINE COLOR");
    for (int i = 0; i < 10; i++) {
        Widget *sw = wadd(WT_SWATCH, ID_SWATCH_O_BASE + i, 248 + i * 50, 648, 42, 34, NULL);
        sw->state = (g_xh.ocolor == cols[i]);
        wsprintfW(sw->val, L"%d", (int)cols[i]);
    }

    wadd(WT_DIV, 0, 248, 706, 986, 20, L"CROSSHAIR SHORTCUT");
    wadd(WT_LABEL, 0, 248, 736, 986, 24, L"Global Hotkey: Ctrl+Alt+X toggles the crosshair instantly in any game.");
}

/* ---------------- 7. MONITORS PANEL ---------------- */
static void build_panel_monitors(void)
{
    wadd(WT_HEAD, 0, 248, 80, 600, 36, L"Multi-Monitor Manager");
    wadd(WT_LABEL, 0, 248, 118, 700, 20, L"Target individual monitors or synchronize adjustments across all screens.");

    int mon_cnt = Modes_MonitorCount();
    for (int i = 0; i < mon_cnt; i++) {
        MonitorInfo *m = Modes_GetMonitor(i);
        int y = 160 + i * 110;
        Widget *card = wadd(WT_MONITOR_CARD, ID_MONITOR_CARD_BASE + i, 248, y, 986, 96, m->friendly);
        card->state = (i == Modes_CurrentMonitorIndex());
        wsprintfW(card->sub, L"Device: %s  ·  %s", m->dev_name, m->adapter);
        wsprintfW(card->val, L"%d × %d @ %d Hz  (%s)", m->current_w, m->current_h, m->current_hz,
                  m->is_primary ? L"Primary Monitor" : L"Secondary Monitor");
    }

    wadd(WT_PRIMARY, ID_B_IDENTIFY_MONITORS, 248, 640, 240, 44, L"Identify Displays");
    wadd(WT_GHOST,   ID_B_BACKUP_NOW,        500, 640, 240, 44, L"Backup All Ramps");
}

/* ---------------- 8. AUTOMATION PANEL ---------------- */
static void build_panel_automation(void)
{
    wadd(WT_HEAD, 0, 248, 80, 600, 36, L"Profile Automation Rules");
    wadd(WT_LABEL, 0, 248, 118, 700, 20, L"Event-driven profile switching based on foreground application.");

    Widget *t1 = wadd(WT_TOGGLE, ID_T_DETECT, 248, 160, 420, 36, L"Auto Detect Games on Launch");
    t1->state = Prof_Detect();

    Widget *t2 = wadd(WT_TOGGLE, ID_T_AUTO_RESTORE, 248, 210, 420, 36, L"Auto Restore Desktop Look on Exit");
    t2->state = Prof_GetAutoRestore();

    wadd(WT_DIV, 0, 248, 270, 986, 24, L"CURRENT FOREGROUND PROCESS MONITOR");
    Widget *fg_card = wadd(WT_CARD, 0, 248, 304, 986, 76, L"DETECTED PROCESS");
    const wchar_t *fg = Prof_CurrentForeground();
    wsprintfW(fg_card->val, L"Executable: %s", fg && fg[0] ? fg : L"None / Desktop");
    int match_idx = Prof_FindExe(fg);
    if (match_idx >= 0) {
        Profile *mp = Prof_Get(match_idx);
        wsprintfW(fg_card->sub, L"Matched Game: %s  (Profile: %s)", mp->name, mp->sub[mp->active_sub].name);
    } else {
        lstrcpyW(fg_card->sub, L"No profile associated. Desktop look remains active.");
    }

    wadd(WT_DIV, 0, 248, 410, 986, 24, L"SWITCHING DELAY");
    wadd(WT_LABEL, 0, 248, 440, 986, 40,
         L"Some games switch display modes or launch video intros. A small delay prevents unnecessary reapplication.");
}

/* ---------------- 9. TOOLS PANEL ---------------- */
static void build_panel_tools(void)
{
    wadd(WT_HEAD, 0, 248, 80, 600, 36, L"Diagnostic Tools & Test Patterns");
    wadd(WT_LABEL, 0, 248, 118, 700, 20, L"Screen uniformity, gamma 2.2 calibration, dead pixels, and crash recovery.");

    wadd(WT_DIV, 0, 248, 150, 986, 24, L"FULLSCREEN MONITOR TEST PATTERNS  (CLICK TO LAUNCH)");

    static const wchar_t *pats[] = {
        L"Pure Black", L"Pure White", L"Pure Red", L"Pure Green", L"Pure Blue",
        L"16-Step Gradient", L"Gamma 2.2 Wedge", L"Contrast Steps",
        L"Sharpness Grid", L"Banding Test", L"HDR Peak Luminance"
    };

    for (int i = 0; i < 11; i++) {
        int col = i % 4;
        int row = i / 4;
        int x = 248 + col * 248;
        int y = 184 + row * 62;
        wadd(WT_BTN, ID_TEST_PAT_BASE + i, x, y, 234, 50, pats[i]);
    }

    wadd(WT_DIV, 0, 248, 390, 986, 24, L"CRASH RESTORATION & BACKUP");
    wadd(WT_PRIMARY, ID_B_BACKUP_NOW,      248, 424, 230, 44, L"Backup Display State");
    wadd(WT_GHOST,   ID_B_RESTORE_BACKUP, 490, 424, 230, 44, L"Restore Last Good");
    wadd(WT_ACCENT,  ID_B_RESET_ALL,      732, 424, 230, 44, L"Reset All Changes");

    wadd(WT_DIV, 0, 248, 498, 986, 24, L"SYSTEM DIAGNOSTICS");
    wadd(WT_GHOST,   ID_B_DIAG_EXPORT,    248, 532, 230, 44, L"Export Diagnostics JSON");
}

/* ---------------- 10. SETTINGS PANEL ---------------- */
static void build_panel_settings(void)
{
    wadd(WT_HEAD, 0, 248, 80, 600, 36, L"Application Settings");
    wadd(WT_LABEL, 0, 248, 118, 700, 20, L"Startup, hotkeys, LAN phone remote control, and privacy.");

    wadd(WT_DIV, 0, 248, 150, 986, 24, L"STARTUP");
    Widget *t1 = wadd(WT_TOGGLE, ID_T_STARTWIN, 248, 184, 400, 36, L"Start PlexusX with Windows");
    (void)t1;

    Widget *t2 = wadd(WT_TOGGLE, ID_T_REDUCE_MOTION, 248, 230, 400, 36, L"Reduce Motion / Animations");
    (void)t2;

    wadd(WT_DIV, 0, 248, 286, 986, 24, L"LAN PHONE REMOTE CONTROL");
    Widget *tp = wadd(WT_TOGGLE, ID_T_PHONE, 248, 320, 400, 36, L"LAN Phone Remote Server");
    tp->state = Phone_IsRunning();

    if (Phone_IsRunning()) {
        wchar_t u[160];
        wsprintfW(u, L"URL: %s   ·   Pairing PIN: %04d   ·   Connected: %d",
                  Phone_SummaryUrl(), Phone_GetPin(), Phone_GetClientCount());
        wadd(WT_LABEL, 0, 248, 366, 600, 24, u);
        wadd(WT_GHOST, ID_B_PHONE_NEW_PIN, 860, 360, 180, 36, L"New PIN");
    } else {
        wadd(WT_LABEL, 0, 248, 366, 700, 24, L"Server is OFF. Enable to control display colors from your smartphone on Wi-Fi.");
    }

    wadd(WT_DIV, 0, 248, 410, 986, 24, L"GLOBAL HOTKEYS");
    wadd(WT_LABEL, 0, 248, 440, 986, 24,
         L"Ctrl+Alt+↑: Saturation +10%   ·   Ctrl+Alt+↓: Saturation −10%   ·   Ctrl+Alt+0: Reset All");
    wadd(WT_LABEL, 0, 248, 468, 986, 24,
         L"Ctrl+Alt+X: Toggle Crosshair   ·   Ctrl+Alt+E: Toggle Color Engine On/Off");

    wadd(WT_DIV, 0, 248, 514, 986, 24, L"PRIVACY & LOCAL CONFIGURATION");
    wadd(WT_LABEL, 0, 248, 544, 986, 44,
         L"100% Zero Telemetry Guarantee. No analytics, no accounts, no cloud calls. Configuration is saved locally.");

    wadd(WT_GHOST, ID_B_OPEN_SETTINGS_DIR, 248, 598, 220, 40, L"Open App Data Folder");

    wadd(WT_DIV, 0, 248, 658, 986, 24, L"ABOUT PLEXUSX");
    wadd(WT_LABEL, 0, 248, 688, 986, 40,
         L"PlexusX v" PX_VERSION L" · Built " PX_BUILD_DATE L" · Free & Open Source for Windows 10/11 x64\n"
         L"Legitimate Windows Magnification & Display APIs. Zero anti-cheat triggers.");
}

/* ---------------- Master UI Builder ---------------- */
void Ui_RebuildPanel(void)
{
    g_nw = 0;
    build_topbar();
    build_sidebar();

    switch (g_panel) {
    case ID_SIDE_HOME:       build_panel_home();       break;
    case ID_SIDE_GAMES:      build_panel_games();      break;
    case ID_SIDE_DISPLAY:    build_panel_display();    break;
    case ID_SIDE_COLOR:      build_panel_color();      break;
    case ID_SIDE_PRESETS:    build_panel_presets();    break;
    case ID_SIDE_CROSS:      build_panel_crosshair();  break;
    case ID_SIDE_MONITORS:   build_panel_monitors();   break;
    case ID_SIDE_AUTOMATION: build_panel_automation(); break;
    case ID_SIDE_TOOLS:      build_panel_tools();      break;
    case ID_SIDE_SETTINGS:   build_panel_settings();   break;
    }
}

/* ---------------- Painting & Rendering ---------------- */

static void draw_rrect(HDC dc, RECT rc, int rad, HBRUSH br, HPEN pen)
{
    HGDIOBJ ob = SelectObject(dc, pen ? pen : GetStockObject(NULL_PEN));
    HGDIOBJ obrush = SelectObject(dc, br);
    RoundRect(dc, rc.left, rc.top, rc.right, rc.bottom, rad, rad);
    SelectObject(dc, obrush);
    SelectObject(dc, ob);
}

static void draw_text(HDC dc, RECT rc, const wchar_t *s, HFONT f, COLORREF c, int align)
{
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, c);
    HGDIOBJ of = SelectObject(dc, f);
    DrawTextW(dc, s, -1, &rc, align | DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS);
    SelectObject(dc, of);
}

/* Procedural Live Preview with Split Slider */
static void draw_split_preview(HDC dc, RECT rc)
{
    int w = rc.right - rc.left;
    int h = rc.bottom - rc.top;
    (void)h;
    int split_x = rc.left + (int)(w * g_split_pos);

    /* Left Side: SDR / Neutral Scene
     * Right Side: Tuned with Active Look Parameters
     */

    /* Clip and draw Left Side */
    HRGN rgn_left = CreateRectRgn(rc.left, rc.top, split_x, rc.bottom);
    SelectClipRgn(dc, rgn_left);

    /* Neutral Sky */
    HBRUSH sky_neutral = CreateSolidBrush(RGB(50, 70, 110));
    FillRect(dc, &rc, sky_neutral);
    DeleteObject(sky_neutral);

    /* Neutral Sun */
    HBRUSH sun_neutral = CreateSolidBrush(RGB(220, 200, 150));
    HGDIOBJ osun = SelectObject(dc, sun_neutral);
    Ellipse(dc, rc.left + w / 4 - S(25), rc.top + S(30), rc.left + w / 4 + S(25), rc.top + S(80));
    SelectObject(dc, osun);
    DeleteObject(sun_neutral);

    /* Neutral Mountains */
    POINT pts_m1[3] = { { rc.left, rc.bottom - S(50) }, { rc.left + w / 3, rc.top + S(60) }, { rc.left + (2 * w) / 3, rc.bottom - S(50) } };
    HBRUSH m_neutral = CreateSolidBrush(RGB(40, 50, 65));
    HGDIOBJ om = SelectObject(dc, m_neutral);
    Polygon(dc, pts_m1, 3);
    SelectObject(dc, om);
    DeleteObject(m_neutral);

    /* Neutral Treeline */
    HBRUSH tree_neutral = CreateSolidBrush(RGB(35, 60, 45));
    RECT tr_rc = { rc.left, rc.bottom - S(70), rc.right, rc.bottom };
    FillRect(dc, &tr_rc, tree_neutral);
    DeleteObject(tree_neutral);

    /* Left Label: BEFORE / NEUTRAL */
    RECT lbl_left = { rc.left + S(16), rc.top + S(16), rc.left + S(200), rc.top + S(40) };
    draw_text(dc, lbl_left, L"BEFORE (NEUTRAL)", g_fSmall, RGB(200, 200, 210), DT_LEFT);

    /* Clip and draw Right Side (Tuned with active Look) */
    HRGN rgn_right = CreateRectRgn(split_x, rc.top, rc.right, rc.bottom);
    SelectClipRgn(dc, rgn_right);

    /* Color modulation according to saturation, brightness, contrast, temp */
    float sat_mult = g_look.sat / 100.0f;
    float bri_mult = g_look.bri / 100.0f;

    int sky_r = (int)clampi((int)(40 * bri_mult), 0, 255);
    int sky_g = (int)clampi((int)(80 * bri_mult * sat_mult), 0, 255);
    int sky_b = (int)clampi((int)(160 * bri_mult * sat_mult), 0, 255);
    HBRUSH sky_tuned = CreateSolidBrush(RGB(sky_r, sky_g, sky_b));
    FillRect(dc, &rc, sky_tuned);
    DeleteObject(sky_tuned);

    /* Tuned Sun */
    HBRUSH sun_tuned = CreateSolidBrush(RGB(255, 230, 120));
    osun = SelectObject(dc, sun_tuned);
    Ellipse(dc, rc.left + w / 4 - S(25), rc.top + S(30), rc.left + w / 4 + S(25), rc.top + S(80));
    SelectObject(dc, osun);
    DeleteObject(sun_tuned);

    /* Tuned Mountains */
    POINT pts_m2[3] = { { rc.left, rc.bottom - S(50) }, { rc.left + w / 3, rc.top + S(60) }, { rc.left + (2 * w) / 3, rc.bottom - S(50) } };
    HBRUSH m_tuned = CreateSolidBrush(RGB((int)(30 * bri_mult), (int)(55 * bri_mult), (int)(90 * bri_mult)));
    om = SelectObject(dc, m_tuned);
    Polygon(dc, pts_m2, 3);
    SelectObject(dc, om);
    DeleteObject(m_tuned);

    /* Tuned Vibrant Treeline */
    int tree_g = (int)clampi((int)(90 * bri_mult * sat_mult), 0, 255);
    HBRUSH tree_tuned = CreateSolidBrush(RGB(20, tree_g, 40));
    FillRect(dc, &tr_rc, tree_tuned);
    DeleteObject(tree_tuned);

    /* Right Label: AFTER / TUNED */
    RECT lbl_right = { rc.right - S(180), rc.top + S(16), rc.right - S(16), rc.top + S(40) };
    draw_text(dc, lbl_right, L"AFTER (PLEXUSX)", g_fSmall, C_ACC, DT_RIGHT);

    /* Reset Clipping */
    SelectClipRgn(dc, NULL);
    DeleteObject(rgn_left);
    DeleteObject(rgn_right);

    /* Draggable Split Divider Line */
    HPEN pen_div = CreatePen(PS_SOLID, S(2), C_ACC);
    HGDIOBJ op = SelectObject(dc, pen_div);
    MoveToEx(dc, split_x, rc.top, NULL);
    LineTo(dc, split_x, rc.bottom);
    SelectObject(dc, op);
    DeleteObject(pen_div);

    /* Center Split Handle Circle */
    int hy = (rc.top + rc.bottom) / 2;
    HBRUSH br_handle = CreateSolidBrush(C_ACC);
    HGDIOBJ oh = SelectObject(dc, br_handle);
    Ellipse(dc, split_x - S(12), hy - S(12), split_x + S(12), hy + S(12));
    SelectObject(dc, oh);
    DeleteObject(br_handle);

    /* Outer Border */
    HPEN pen_b = CreatePen(PS_SOLID, 1, C_LINE);
    op = SelectObject(dc, pen_b);
    SelectObject(dc, GetStockObject(NULL_BRUSH));
    Rectangle(dc, rc.left, rc.top, rc.right, rc.bottom);
    SelectObject(dc, op);
    DeleteObject(pen_b);
}

/* Gamma & RGB Response Curve Preview */
static void draw_curve_preview(HDC dc, RECT rc)
{
    HBRUSH bg = CreateSolidBrush(C_CARD);
    FillRect(dc, &rc, bg);
    DeleteObject(bg);

    HPEN pen_grid = CreatePen(PS_SOLID, 1, RGB(35, 35, 48));
    HGDIOBJ op = SelectObject(dc, pen_grid);
    for (int i = 1; i <= 3; i++) {
        int x = rc.left + (i * (rc.right - rc.left)) / 4;
        int y = rc.top + (i * (rc.bottom - rc.top)) / 4;
        MoveToEx(dc, x, rc.top, NULL); LineTo(dc, x, rc.bottom);
        MoveToEx(dc, rc.left, y, NULL); LineTo(dc, rc.right, y);
    }
    SelectObject(dc, op);
    DeleteObject(pen_grid);

    /* Draw Transfer Curve */
    WORD ramp[3][256];
    Eng_CalculateGammaRamp(&g_look, ramp);

    HPEN pen_c = CreatePen(PS_SOLID, S(2), C_ACC);
    op = SelectObject(dc, pen_c);
    int w = rc.right - rc.left;
    int h = rc.bottom - rc.top;

    for (int i = 0; i < 256; i++) {
        int x = rc.left + (i * w) / 255;
        float y_norm = ramp[1][i] / 65535.0f;
        int y = rc.bottom - (int)(y_norm * (h - 8)) - 4;
        if (i == 0) MoveToEx(dc, x, y, NULL);
        else LineTo(dc, x, y);
    }
    SelectObject(dc, op);
    DeleteObject(pen_c);

    RECT tr = { rc.left + S(8), rc.top + S(6), rc.right - S(8), rc.top + S(24) };
    draw_text(dc, tr, L"Hardware Gamma Ramp Curve Preview", g_fSmall, C_SUB, DT_LEFT);
}

static void draw_widget(HDC dc, Widget *k)
{
    RECT rc = k->rc;
    int hov = (g_hover_widget == k->id);

    HBRUSH br_card = CreateSolidBrush(C_CARD);
    HBRUSH br_card2 = CreateSolidBrush(C_CARD2);
    HBRUSH br_card_act = CreateSolidBrush(C_CARD_ACT);
    HBRUSH br_acc = CreateSolidBrush(C_ACC);
    HPEN pen_line = CreatePen(PS_SOLID, 1, C_LINE);
    HPEN pen_acc = CreatePen(PS_SOLID, 1, C_ACC);
    HPEN pen_non = CreatePen(PS_SOLID, 1, RGB(0, 0, 0));

    switch (k->type) {
    case WT_HEAD:
        draw_text(dc, rc, k->text, g_fH1, C_TXT, DT_LEFT);
        break;
    case WT_LABEL:
        draw_text(dc, rc, k->text, g_fBody, C_SUB, DT_LEFT);
        break;
    case WT_DIV: {
        RECT rt = rc;
        draw_text(dc, rt, k->text, g_fSmall, C_DIM, DT_LEFT);
        int y = (rc.top + rc.bottom) / 2;
        RECT ln = { rc.left + S(180), y, rc.right, y + 1 };
        FillRect(dc, &ln, br_card2);
        break;
    }
    case WT_PRIMARY:
    case WT_GHOST:
    case WT_ACCENT:
    case WT_BTN: {
        HBRUSH fill;
        COLORREF fc = C_TXT;
        if (k->type == WT_PRIMARY) { fill = br_acc; fc = C_DARK; }
        else if (k->type == WT_ACCENT) { fill = hov ? br_acc : br_card2; fc = hov ? C_DARK : C_TXT; }
        else { fill = hov ? br_card2 : br_card; }
        draw_rrect(dc, rc, S(8), fill, (hov || k->type == WT_PRIMARY) ? pen_acc : pen_line);
        draw_text(dc, rc, k->text, g_fH2, fc, DT_CENTER);
        break;
    }
    case WT_QUICK_SAT: {
        draw_rrect(dc, rc, S(6), hov ? br_card2 : br_card, hov ? pen_acc : pen_line);
        draw_text(dc, rc, k->text, g_fSmall, hov ? C_ACC : C_TXT, DT_CENTER);
        break;
    }
    case WT_SIDE: {
        if (k->state) {
            draw_rrect(dc, rc, S(8), br_card2, pen_non);
            RECT bar = { rc.left + S(4), rc.top + S(8), rc.left + S(8), rc.bottom - S(8) };
            draw_rrect(dc, bar, S(2), br_acc, pen_non);
        }
        RECT rl = rc; rl.left += S(18);
        draw_text(dc, rl, k->text, g_fH2, k->state ? C_ACC : (hov ? C_TXT : C_SUB), DT_LEFT);
        break;
    }
    case WT_CARD: {
        draw_rrect(dc, rc, S(10), br_card, pen_line);
        RECT r_head = { rc.left + S(16), rc.top + S(12), rc.right - S(16), rc.top + S(28) };
        RECT r_val  = { rc.left + S(16), rc.top + S(30), rc.right - S(16), rc.top + S(64) };
        RECT r_sub  = { rc.left + S(16), rc.bottom - S(26), rc.right - S(16), rc.bottom - S(8) };
        draw_text(dc, r_head, k->text, g_fSmall, C_DIM, DT_LEFT);
        draw_text(dc, r_val, k->val, g_fH1, C_TXT, DT_LEFT);
        draw_text(dc, r_sub, k->sub, g_fSmall, C_SUB, DT_LEFT);
        break;
    }
    case WT_GAME_CARD:
    case WT_LOOK_CARD:
    case WT_MONITOR_CARD: {
        HBRUSH fill = k->state ? br_card_act : (hov ? br_card2 : br_card);
        draw_rrect(dc, rc, S(10), fill, k->state ? pen_acc : pen_line);
        RECT r_title = { rc.left + S(16), rc.top + S(10), rc.right - S(16), rc.top + S(32) };
        RECT r_sub   = { rc.left + S(16), rc.top + S(32), rc.right - S(16), rc.top + S(50) };
        RECT r_val   = { rc.right - S(260), rc.top + S(10), rc.right - S(16), rc.top + S(32) };
        draw_text(dc, r_title, k->text, g_fH2, k->state ? C_ACC : C_TXT, DT_LEFT);
        draw_text(dc, r_sub, k->sub, g_fSmall, C_SUB, DT_LEFT);
        draw_text(dc, r_val, k->val, g_fSmall, C_ACC2, DT_RIGHT);
        break;
    }
    case WT_ROW: {
        HBRUSH fill = k->state ? br_card_act : (hov ? br_card2 : br_card);
        draw_rrect(dc, rc, S(8), fill, k->state ? pen_acc : pen_line);
        RECT r1 = rc; r1.left += S(16); r1.right = rc.left + S(300);
        RECT r2 = rc; r2.left = rc.right - S(200); r2.right -= S(16);
        draw_text(dc, r1, k->text, g_fBody, k->state ? C_ACC : C_TXT, DT_LEFT);
        draw_text(dc, r2, k->sub, g_fSmall, C_SUB, DT_RIGHT);
        break;
    }
    case WT_SLIDER: {
        RECT rl = rc; rl.bottom = rl.top + S(22);
        RECT rv = rc; rv.bottom = rv.top + S(22); rv.left = rv.right - S(100);
        draw_text(dc, rl, k->text, g_fBody, C_TXT, DT_LEFT);
        draw_text(dc, rv, k->val, g_fMono, (hov || g_active_widget == k->id) ? C_ACC : C_SUB, DT_RIGHT);

        int ins = S(4);
        int tx1 = rc.left + ins, tx2 = rc.right - ins;
        int ty = rc.top + S(46);
        RECT tr = { tx1, ty - S(3), tx2, ty + S(3) };
        draw_rrect(dc, tr, S(3), br_card2, pen_non);

        float cur = 0;
        switch (k->id) {
        case ID_SL_SAT: cur = g_look.sat; break;
        case ID_SL_VIB: cur = g_look.vibrance; break;
        case ID_SL_BRI: cur = g_look.bri; break;
        case ID_SL_CON: cur = g_look.con; break;
        case ID_SL_GAMMA: cur = g_look.gamma; break;
        case ID_SL_TEMP: cur = g_look.temp; break;
        case ID_SL_TINT: cur = g_look.tint; break;
        case ID_SL_R_GAIN: cur = g_look.r_gain; break;
        case ID_SL_G_GAIN: cur = g_look.g_gain; break;
        case ID_SL_B_GAIN: cur = g_look.b_gain; break;
        case ID_SL_SHADOWS: cur = g_look.shadows; break;
        case ID_SL_HIGHLIGHTS: cur = g_look.highlights; break;
        case ID_SL_BLACK_LEVEL: cur = g_look.black_level; break;
        case ID_SL_WHITE_POINT: cur = g_look.white_point; break;
        case ID_SL_CLARITY: cur = g_look.clarity; break;
        case ID_SL_XH_SIZE: cur = (float)g_xh.size; break;
        case ID_SL_XH_GAP: cur = (float)g_xh.gap; break;
        case ID_SL_XH_THICK: cur = (float)g_xh.thick; break;
        case ID_SL_XH_OPACITY: cur = (float)g_xh.opacity; break;
        }

        float f = clampf((cur - k->vmin) / (k->vmax - k->vmin), 0.0f, 1.0f);
        int fx = tx1 + (int)((tx2 - tx1) * f);
        if (fx > tx1) {
            RECT fr = { tx1, ty - S(3), fx, ty + S(3) };
            draw_rrect(dc, fr, S(3), br_acc, pen_non);
        }
        int th = (hov || g_active_widget == k->id) ? S(8) : S(6);
        RECT th_rc = { fx - th, ty - th, fx + th, ty + th };
        HBRUSH br_thumb = CreateSolidBrush(RGB(255, 255, 255));
        draw_rrect(dc, th_rc, th, br_thumb, pen_non);
        DeleteObject(br_thumb);
        break;
    }
    case WT_TOGGLE: {
        RECT rl = rc; rl.right -= S(56);
        draw_text(dc, rl, k->text, g_fBody, C_TXT, DT_LEFT);
        RECT pr = { rc.right - S(48), rc.top + S(4), rc.right, rc.bottom - S(4) };
        HBRUSH pb = CreateSolidBrush(k->state ? C_ACC : C_CARD2);
        draw_rrect(dc, pr, (pr.bottom - pr.top) / 2, pb, pen_non);
        DeleteObject(pb);

        int kr = (pr.bottom - pr.top) / 2 - S(3);
        int kx = k->state ? pr.right - kr - S(4) : pr.left + kr + S(4);
        int ky = (pr.top + pr.bottom) / 2;
        RECT kb = { kx - kr, ky - kr, kx + kr, ky + kr };
        HBRUSH kb_br = CreateSolidBrush(k->state ? C_DARK : C_SUB);
        draw_rrect(dc, kb, kr, kb_br, pen_non);
        DeleteObject(kb_br);
        break;
    }
    case WT_SHAPE: {
        draw_rrect(dc, rc, S(8), hov ? br_card2 : br_card, k->state ? pen_acc : pen_line);
        RECT rl = rc; rl.top = rl.bottom - S(22);
        draw_text(dc, rl, k->text, g_fSmall, k->state ? C_ACC : C_SUB, DT_CENTER);
        break;
    }
    case WT_SWATCH: {
        COLORREF col = (COLORREF)_wtoi(k->val);
        HBRUSH b = CreateSolidBrush(col);
        RECT inner = { rc.left + S(3), rc.top + S(3), rc.right - S(3), rc.bottom - S(3) };
        draw_rrect(dc, inner, S(6), b, k->state ? pen_acc : pen_line);
        DeleteObject(b);
        break;
    }
    case WT_SPLIT_PREVIEW:
        draw_split_preview(dc, rc);
        break;
    case WT_CURVE_PREVIEW:
        draw_curve_preview(dc, rc);
        break;
    }

    DeleteObject(br_card);
    DeleteObject(br_card2);
    DeleteObject(br_card_act);
    DeleteObject(br_acc);
    DeleteObject(pen_line);
    DeleteObject(pen_acc);
    DeleteObject(pen_non);
}

void Ui_Paint(HDC hdc, const RECT *rc)
{
    /* Background */
    HBRUSH bg = CreateSolidBrush(C_BG);
    FillRect(hdc, rc, bg);
    DeleteObject(bg);

    /* Sidebar Background */
    RECT sr = { 0, 0, S(PX_SIDE_W), S(PX_WIN_H) };
    HBRUSH side_br = CreateSolidBrush(C_SIDE);
    FillRect(hdc, &sr, side_br);
    DeleteObject(side_br);

    /* Separator Lines */
    RECT sepline = { S(PX_SIDE_W), 0, S(PX_SIDE_W) + 1, S(PX_WIN_H) };
    RECT topline = { 0, S(PX_TOP_H), S(PX_WIN_W), S(PX_TOP_H) + 1 };
    HBRUSH lb = CreateSolidBrush(C_LINE);
    FillRect(hdc, &sepline, lb);
    FillRect(hdc, &topline, lb);
    DeleteObject(lb);

    /* Brand Logo Mark */
    {
        RECT box = { S(18), S(14), S(48), S(44) };
        HBRUSH acc = CreateSolidBrush(C_ACC);
        draw_rrect(hdc, box, S(8), acc, NULL);
        DeleteObject(acc);

        /* Draw Geometric X mark */
        HPEN xp = CreatePen(PS_SOLID, S(3), C_DARK);
        HGDIOBJ op = SelectObject(hdc, xp);
        MoveToEx(hdc, S(25), S(21), NULL); LineTo(hdc, S(41), S(37));
        MoveToEx(hdc, S(41), S(21), NULL); LineTo(hdc, S(25), S(37));
        SelectObject(hdc, op);
        DeleteObject(xp);

        RECT lt = { S(56), S(12), S(200), S(34) };
        RECT ls = { S(56), S(34), S(200), S(50) };
        draw_text(hdc, lt, L"PLEXUSX", g_fLogo, C_TXT, DT_LEFT);
        draw_text(hdc, ls, L"DISPLAY OPTIMIZER", g_fSmall, C_SUB, DT_LEFT);
    }

    /* Bottom Sidebar Hardware Status */
    {
        RECT hr = { S(16), S(PX_WIN_H - 90), S(PX_SIDE_W - 16), S(PX_WIN_H - 16) };
        HBRUSH hb = CreateSolidBrush(C_CARD);
        HPEN hp = CreatePen(PS_SOLID, 1, C_LINE);
        draw_rrect(hdc, hr, S(8), hb, hp);
        DeleteObject(hb);
        DeleteObject(hp);

        const GpuInfo *gpu = Eng_GetGpuInfo();
        ModeInfo cur;
        Modes_Current(&cur);

        RECT h1 = { hr.left + S(10), hr.top + S(8), hr.right - S(10), hr.top + S(24) };
        RECT h2 = { hr.left + S(10), hr.top + S(26), hr.right - S(10), hr.top + S(42) };
        RECT h3 = { hr.left + S(10), hr.top + S(46), hr.right - S(10), hr.top + S(62) };

        draw_text(hdc, h1, gpu->vendor_name, g_fSmall, C_ACC, DT_LEFT);
        wchar_t dstr[48];
        wsprintfW(dstr, L"%dx%d @ %dHz", cur.w, cur.h, cur.hz);
        draw_text(hdc, h2, dstr, g_fSmall, C_TXT, DT_LEFT);
        draw_text(hdc, h3, Tools_IsGamingMode() ? L"● Gaming Mode ACTIVE" : L"● Active · No Hooking", g_fSmall, C_SUB, DT_LEFT);
    }

    /* Notification Toast */
    if (g_toast[0] && GetTickCount() - g_toast_time < 3500) {
        RECT tw = { S(PX_WIN_W - 560), S(12), S(PX_WIN_W - 110), S(PX_TOP_H - 10) };
        HBRUSH cb = CreateSolidBrush(C_CARD2);
        HPEN cp = CreatePen(PS_SOLID, 1, C_ACC);
        draw_rrect(hdc, tw, S(8), cb, cp);
        RECT td = tw; td.left += S(14);
        wchar_t t2[160];
        wsprintfW(t2, L"●  %s", g_toast);
        draw_text(hdc, td, t2, g_fSmall, C_ACC, DT_LEFT);
        DeleteObject(cb);
        DeleteObject(cp);
    }

    for (int i = 0; i < g_nw; i++) {
        draw_widget(hdc, &g_w[i]);
    }
}

/* ---------------- Event Handling & Interactivity ---------------- */

int Ui_MouseDown(int x, int y)
{
    /* Check split preview drag */
    for (int i = 0; i < g_nw; i++) {
        if (g_w[i].type == WT_SPLIT_PREVIEW) {
            RECT rc = g_w[i].rc;
            if (x >= rc.left && x <= rc.right && y >= rc.top && y <= rc.bottom) {
                g_drag_split = 1;
                g_split_pos = clampf((float)(x - rc.left) / (float)(rc.right - rc.left), 0.05f, 0.95f);
                return 1;
            }
        }
    }

    for (int i = 0; i < g_nw; i++) {
        Widget *k = &g_w[i];
        if (x >= k->rc.left && x <= k->rc.right && y >= k->rc.top && y <= k->rc.bottom) {
            g_active_widget = k->id;

            if (k->type == WT_SIDE) {
                g_panel = k->id;
                Ui_RebuildPanel();
                return 1;
            }
            if (k->type == WT_SLIDER) {
                int ins = S(4);
                int tx1 = k->rc.left + ins, tx2 = k->rc.right - ins;
                float f = clampf((float)(x - tx1) / (float)(tx2 - tx1), 0.0f, 1.0f);
                float val = k->vmin + f * (k->vmax - k->vmin);

                switch (k->id) {
                case ID_SL_SAT: g_look.sat = val; break;
                case ID_SL_VIB: g_look.vibrance = val; break;
                case ID_SL_BRI: g_look.bri = val; break;
                case ID_SL_CON: g_look.con = val; break;
                case ID_SL_GAMMA: g_look.gamma = val; break;
                case ID_SL_TEMP: g_look.temp = val; break;
                case ID_SL_TINT: g_look.tint = val; break;
                case ID_SL_R_GAIN: g_look.r_gain = val; break;
                case ID_SL_G_GAIN: g_look.g_gain = val; break;
                case ID_SL_B_GAIN: g_look.b_gain = val; break;
                case ID_SL_SHADOWS: g_look.shadows = val; break;
                case ID_SL_HIGHLIGHTS: g_look.highlights = val; break;
                case ID_SL_BLACK_LEVEL: g_look.black_level = val; break;
                case ID_SL_WHITE_POINT: g_look.white_point = val; break;
                case ID_SL_CLARITY: g_look.clarity = val; break;
                case ID_SL_XH_SIZE: g_xh.size = (int)val; break;
                case ID_SL_XH_GAP: g_xh.gap = (int)val; break;
                case ID_SL_XH_THICK: g_xh.thick = (int)val; break;
                case ID_SL_XH_OPACITY: g_xh.opacity = (int)val; break;
                }
                Ui_RebuildPanel();
                Main_ApplyAll();
                return 1;
            }
            if (k->type == WT_TOGGLE) {
                switch (k->id) {
                case ID_T_LOOK_ENABLE:
                    g_look.enabled = !g_look.enabled;
                    break;
                case ID_T_DETECT:
                    Prof_SetDetect(!Prof_Detect());
                    break;
                case ID_T_AUTO_RESTORE:
                    Prof_SetAutoRestore(!Prof_GetAutoRestore());
                    break;
                case ID_T_XH:
                    Xh_Toggle();
                    break;
                case ID_T_XH_OUTLINE:
                    g_xh.outline = !g_xh.outline;
                    Xh_Update(&g_xh);
                    break;
                case ID_T_XH_DOT:
                    g_xh.center_dot = !g_xh.center_dot;
                    Xh_Update(&g_xh);
                    break;
                case ID_T_PHONE:
                    if (Phone_IsRunning()) Phone_Stop();
                    else Phone_Start();
                    break;
                }
                Ui_RebuildPanel();
                Main_ApplyAll();
                return 1;
            }
            if (k->type == WT_SHAPE) {
                g_xh.shape = k->id - ID_XH_SHAPE_BASE;
                Xh_Update(&g_xh);
                Ui_RebuildPanel();
                return 1;
            }
            if (k->type == WT_SWATCH) {
                COLORREF c = (COLORREF)_wtoi(k->val);
                if (k->id < ID_SWATCH_O_BASE) g_xh.color = c;
                else g_xh.ocolor = c;
                Xh_Update(&g_xh);
                Ui_RebuildPanel();
                return 1;
            }
            if (k->type == WT_GAME_CARD) {
                int idx = k->id - ID_GAME_CARD_BASE;
                Prof_SetActiveIndex(idx);
                Profile *gp = Prof_Get(idx);
                if (gp) Ui_LoadLook(&gp->sub[gp->active_sub].look);
                Ui_RebuildPanel();
                Main_ApplyAll();
                return 1;
            }
            if (k->type == WT_LOOK_CARD) {
                int count = 0;
                const SceneDef *sc = Scene_GetList(&count);
                if (k->flags >= 0 && k->flags < count) {
                    Ui_LoadLook(&sc[k->flags].look);
                    Ui_Notify(sc[k->flags].name);
                    Ui_RebuildPanel();
                    Main_ApplyAll();
                }
                return 1;
            }
            if (k->type == WT_ROW && k->id >= ID_MODE_ROW_BASE) {
                int m_idx = k->id - ID_MODE_ROW_BASE;
                Modes_Apply(m_idx);
                Ui_Notify(L"Display Mode Applied");
                Ui_RebuildPanel();
                return 1;
            }
            if (k->type == WT_QUICK_SAT) {
                switch (k->id) {
                case ID_B_SAT_100: g_look.sat = 100; break;
                case ID_B_SAT_150: g_look.sat = 150; break;
                case ID_B_SAT_200: g_look.sat = 200; break;
                case ID_B_SAT_250: g_look.sat = 250; break;
                case ID_B_SAT_300: g_look.sat = 300; break;
                }
                Ui_RebuildPanel();
                Main_ApplyAll();
                return 1;
            }
            if (k->type == WT_PRIMARY || k->type == WT_GHOST || k->type == WT_ACCENT || k->type == WT_BTN) {
                Ui_Exec(k->id);
                return 1;
            }
        }
    }
    return 0;
}

int Ui_MouseMove(int x, int y, int dragging)
{
    if (dragging && g_drag_split) {
        for (int i = 0; i < g_nw; i++) {
            if (g_w[i].type == WT_SPLIT_PREVIEW) {
                RECT rc = g_w[i].rc;
                g_split_pos = clampf((float)(x - rc.left) / (float)(rc.right - rc.left), 0.05f, 0.95f);
                return -1;
            }
        }
    }

    if (dragging && g_active_widget >= ID_SL_SAT) {
        for (int i = 0; i < g_nw; i++) {
            Widget *k = &g_w[i];
            if (k->id == g_active_widget && k->type == WT_SLIDER) {
                int ins = S(4);
                int tx1 = k->rc.left + ins, tx2 = k->rc.right - ins;
                float f = clampf((float)(x - tx1) / (float)(tx2 - tx1), 0.0f, 1.0f);
                float val = k->vmin + f * (k->vmax - k->vmin);

                switch (k->id) {
                case ID_SL_SAT: g_look.sat = val; break;
                case ID_SL_VIB: g_look.vibrance = val; break;
                case ID_SL_BRI: g_look.bri = val; break;
                case ID_SL_CON: g_look.con = val; break;
                case ID_SL_GAMMA: g_look.gamma = val; break;
                case ID_SL_TEMP: g_look.temp = val; break;
                case ID_SL_TINT: g_look.tint = val; break;
                case ID_SL_R_GAIN: g_look.r_gain = val; break;
                case ID_SL_G_GAIN: g_look.g_gain = val; break;
                case ID_SL_B_GAIN: g_look.b_gain = val; break;
                case ID_SL_SHADOWS: g_look.shadows = val; break;
                case ID_SL_HIGHLIGHTS: g_look.highlights = val; break;
                case ID_SL_BLACK_LEVEL: g_look.black_level = val; break;
                case ID_SL_WHITE_POINT: g_look.white_point = val; break;
                case ID_SL_CLARITY: g_look.clarity = val; break;
                case ID_SL_XH_SIZE: g_xh.size = (int)val; break;
                case ID_SL_XH_GAP: g_xh.gap = (int)val; break;
                case ID_SL_XH_THICK: g_xh.thick = (int)val; break;
                case ID_SL_XH_OPACITY: g_xh.opacity = (int)val; break;
                }
                Ui_RebuildPanel();
                Main_ApplyAll();
                return -1;
            }
        }
    }

    int old_hover = g_hover_widget;
    g_hover_widget = Ui_Hover(x, y);
    if (old_hover != g_hover_widget) return -1;
    return 0;
}

void Ui_MouseUp(int x, int y)
{
    (void)x; (void)y;
    g_active_widget = 0;
    g_drag_split = 0;
}

int Ui_Hover(int x, int y)
{
    for (int i = 0; i < g_nw; i++) {
        if (x >= g_w[i].rc.left && x <= g_w[i].rc.right &&
            y >= g_w[i].rc.top && y <= g_w[i].rc.bottom) {
            return g_w[i].id;
        }
    }
    return 0;
}

int Ui_Wheel(int x, int y, int delta)
{
    (void)x; (void)y; (void)delta;
    return 0;
}

int Ui_Exec(int id)
{
    switch (id) {
    case ID_CAP_MIN:
        ShowWindow(g_ui_hwnd, SW_MINIMIZE);
        return 1;
    case ID_CAP_CLOSE:
        PostMessageW(g_ui_hwnd, WM_CLOSE, 0, 0);
        return 1;
    case ID_B_HOME_COMPETITIVE: {
        int cnt = 0;
        const SceneDef *sc = Scene_GetList(&cnt);
        if (cnt > 0) Ui_LoadLook(&sc[0].look);
        Ui_Notify(L"Competitive Look Applied");
        Ui_RebuildPanel();
        Main_ApplyAll();
        return 1;
    }
    case ID_B_HOME_MAX_VIB:
        g_look.sat = 300.0f;
        g_look.vibrance = 240.0f;
        g_look.enabled = 1;
        Ui_Notify(L"300% Maximum Vibrance Applied");
        Ui_RebuildPanel();
        Main_ApplyAll();
        return 1;
    case ID_B_HOME_NIGHT_VIS: {
        g_look.sat = 230.0f;
        g_look.gamma = 0.78f;
        g_look.shadows = 160.0f;
        g_look.bri = 120.0f;
        g_look.enabled = 1;
        Ui_Notify(L"Night Visibility Mode Applied");
        Ui_RebuildPanel();
        Main_ApplyAll();
        return 1;
    }
    case ID_B_HOME_CINEMATIC: {
        g_look.sat = 135.0f;
        g_look.temp = 5800.0f;
        g_look.con = 110.0f;
        g_look.enabled = 1;
        Ui_Notify(L"Cinematic Warm Mode Applied");
        Ui_RebuildPanel();
        Main_ApplyAll();
        return 1;
    }
    case ID_B_HOME_NATURAL:
        g_look = (Look){ 1, 100, 100, 100, 100, 1.00f, 6500, 0, 100, 100, 100, 100, 100, 100, 100, 100, 0 };
        Ui_Notify(L"Natural Display Mode Applied");
        Ui_RebuildPanel();
        Main_ApplyAll();
        return 1;
    case ID_B_RESET_COLOR:
        g_look = (Look){ 1, 100, 100, 100, 100, 1.00f, 6500, 0, 100, 100, 100, 100, 100, 100, 100, 100, 0 };
        Eng_Reset();
        Ui_Notify(L"All Color Settings Reset to Neutral");
        Ui_RebuildPanel();
        Main_ApplyAll();
        return 1;
    case ID_B_HOME_GAMING_MODE:
        Tools_ToggleGamingMode();
        Ui_RebuildPanel();
        return 1;
    case ID_B_MODE_APPLY:
        Modes_Apply(0);
        Ui_Notify(L"Display Mode Applied");
        Ui_RebuildPanel();
        return 1;
    case ID_B_MODE_MAX_HZ:
        Modes_ApplyMaxHz();
        Ui_Notify(L"Maximum Refresh Rate Applied");
        Ui_RebuildPanel();
        return 1;
    case ID_B_MODE_NATIVE:
        Modes_ApplyNative();
        Ui_Notify(L"Reset to Native Mode");
        Ui_RebuildPanel();
        return 1;
    case ID_B_MODE_OPENHDR:
        Modes_OpenHdrSettings();
        return 1;
    case ID_B_BACKUP_NOW:
        Eng_BackupCurrentState();
        Ui_Notify(L"Display State & Ramps Backed Up");
        return 1;
    case ID_B_RESTORE_BACKUP:
        if (Eng_RestoreLastGood()) Ui_Notify(L"Restored Last Known Good Configuration");
        else Ui_Notify(L"No Previous Backup Found");
        return 1;
    case ID_B_RESET_ALL:
        Eng_Reset();
        Modes_ApplyNative();
        Ui_Notify(L"All Changes Reverted to Hardware Defaults");
        Ui_RebuildPanel();
        return 1;
    case ID_B_DIAG_EXPORT: {
        wchar_t path[MAX_PATH];
        wsprintfW(path, L"%s\\PlexusX_Diagnostics.json", g_appdata);
        Tools_ExportDiagnostics(path);
        Ui_Notify(L"Diagnostics exported to app data folder");
        return 1;
    }
    case ID_B_IDENTIFY_MONITORS:
        Modes_IdentifyMonitors();
        return 1;
    case ID_B_PHONE_NEW_PIN:
        Phone_RegeneratePin();
        Ui_Notify(L"New Phone Pairing PIN Generated");
        Ui_RebuildPanel();
        return 1;
    case ID_B_OPEN_SETTINGS_DIR:
        ShellExecuteW(NULL, L"open", g_appdata, NULL, NULL, SW_SHOWNORMAL);
        return 1;
    default:
        if (id >= ID_TEST_PAT_BASE && id < ID_TEST_PAT_BASE + 12) {
            Tools_LaunchPattern(id - ID_TEST_PAT_BASE);
            return 1;
        }
        break;
    }
    return 0;
}

void Ui_Init(HWND hwnd, HINSTANCE inst)
{
    g_ui_hwnd = hwnd;
    HDC sdc = GetDC(NULL);
    g_sc = GetDeviceCaps(sdc, LOGPIXELSX) / 96.0f;
    ReleaseDC(NULL, sdc);
    if (g_sc < 0.75f) g_sc = 1.0f;

    make_fonts();
    Modes_Refresh();
    Prof_Init();
    Tools_Init();
    Ui_RebuildPanel();
}

void Ui_Free(void)
{
    DeleteObject(g_fLogo);
    DeleteObject(g_fH1);
    DeleteObject(g_fH2);
    DeleteObject(g_fBody);
    DeleteObject(g_fSmall);
    DeleteObject(g_fMono);
    DeleteObject(g_fBigVal);
    Tools_Shutdown();
}

void Ui_SetPanel(int side_id)
{
    g_panel = side_id;
    Ui_RebuildPanel();
    InvalidateRect(g_ui_hwnd, NULL, FALSE);
}

int  Ui_Panel(void) { return g_panel; }
Look *Ui_Look(void) { return &g_look; }

void Ui_LoadLook(const Look *lk)
{
    if (lk) g_look = *lk;
}

void Ui_Notify(const wchar_t *msg)
{
    if (msg) {
        lstrcpynW(g_toast, msg, 128);
        g_toast_time = GetTickCount();
    }
}

void Ui_GetXh(XhCfg *out) { if (out) *out = g_xh; }
void Ui_SetXh(const XhCfg *in) { if (in) g_xh = *in; }

int Ui_CapHit(int x, int y)
{
    return (x >= S(PX_WIN_W - 100) && y <= S(PX_TOP_H));
}

int Ui_InTop(int x, int y)
{
    return (y <= S(PX_TOP_H) && x < S(PX_WIN_W - 100));
}

void Ui_FilterGames(const wchar_t *filter)
{
    if (filter) lstrcpynW(g_game_filter, filter, 64);
    else g_game_filter[0] = 0;
    Ui_RebuildPanel();
}

const wchar_t *Ui_GetFilter(void)
{
    return g_game_filter;
}
