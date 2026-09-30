/* ui.c — hand-drawn dark UI: sidebar, sliders, toggles, lists, swatches */
#include "common.h"

#define ID_SWATCH_O 820

#define WIN_W 1240
#define WIN_H 800
#define SIDE_W 216
#define TOP_H 52

#define C_BG    RGB(12,12,16)
#define C_SIDE  RGB(15,15,21)
#define C_CARD  RGB(23,23,30)
#define C_CARD2 RGB(31,31,41)
#define C_LINE  RGB(42,42,55)
#define C_TXT   RGB(240,240,245)
#define C_SUB   RGB(140,140,153)
#define C_DIM   RGB(96,96,110)
#define C_ACC   RGB(198,255,61)
#define C_DARK  RGB(10,10,14)
#define C_DANG  RGB(255,96,96)
#define C_VIO   RGB(123,92,255)

static HWND    g_ui_hwnd;
static float   g_sc = 1.f;
static Widget  g_w[MAX_WIDGETS];
static int     g_nw;
static int     g_panel = ID_SIDE_DISPLAY;
static Look    g_look = { 150, 0, 0, 0, 0, 1.f, 1 };
static XhCfg   g_xh = { 0, XH_CROSS, 16, 4, 2, 100, 1, RGB(0x7C,0xFF,0x40), RGB(0,0,0) };
static int     g_selProf = -1, g_selMode = -1;
static int     g_active, g_hover;
static int     g_runScroll, g_modeScroll;
static wchar_t g_title[64] = L"Display";
static wchar_t g_toast[128];
static DWORD   g_toastT;
static int     g_startwin, g_phoneon;

static HFONT g_fBig, g_fH1, g_fH2, g_fBody, g_fSmall, g_fMono;
static HBRUSH br_line_dummy(void);

static int S(int v) { return (int)(v * g_sc + 0.5f); }

static void mkfonts(void)
{
    g_fBig   = CreateFontW(-S(48), 0, 0, 0, FW_BOLD, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
    g_fH1    = CreateFontW(-S(28), 0, 0, 0, FW_BOLD, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
    g_fH2    = CreateFontW(-S(17), 0, 0, 0, FW_SEMIBOLD, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
    g_fBody  = CreateFontW(-S(15), 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
    g_fSmall = CreateFontW(-S(13), 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
    g_fMono  = CreateFontW(-S(15), 0, 0, 0, FW_BOLD, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Consolas");
}

/* ---------------- widget helpers ---------------- */

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

static void set_slider_val(Widget *k, float v, const wchar_t *fmtw)
{
    k->val[0] = 0;
    if (fmtw && wcscmp(fmtw, L"g") == 0) _snwprintf(k->val, 48, L"%.2f", v);
    else if (fmtw && wcscmp(fmtw, L"p") == 0) wsprintfW(k->val, L"%d%%", (int)v);
    else if (fmtw && wcscmp(fmtw, L"s") == 0) wsprintfW(k->val, L"%+d%%", (int)v);
    else if (fmtw && wcscmp(fmtw, L"d") == 0) wsprintfW(k->val, L"%+d°", (int)v);
}

static void sl(Widget *k)
{
    float v = 0; const wchar_t *f = L"s";
    switch (k->id) {
    case ID_SAT:   v = g_look.sat;  f = L"p"; k->vmin = 100; k->vmax = 300; break;
    case ID_BRI:   v = g_look.bri;  f = L"s"; k->vmin = -100; k->vmax = 100; break;
    case ID_CON:   v = g_look.con;  f = L"s"; k->vmin = -100; k->vmax = 100; break;
    case ID_TEMP:  v = g_look.temp; f = L"s"; k->vmin = -100; k->vmax = 100; break;
    case ID_HUE:   v = g_look.hue;  f = L"d"; k->vmin = -60; k->vmax = 60; break;
    case ID_GAMMA: v = g_look.gamma; f = L"g"; k->vmin = 0.40f; k->vmax = 2.40f; break;
    case ID_XH_SIZE:  v = (float)g_xh.size;  f = L"p"; k->vmin = 6; k->vmax = 48; break;
    case ID_XH_GAP:   v = (float)g_xh.gap;   f = L"p"; k->vmin = 0; k->vmax = 24; break;
    case ID_XH_THICK: v = (float)g_xh.thick; f = L"p"; k->vmin = 1; k->vmax = 10; break;
    case ID_XH_DOTOP: v = (float)g_xh.dotop; f = L"p"; k->vmin = 0; k->vmax = 100; break;
    }
    set_slider_val(k, v, f);
}

static void build_side(void)
{
    for (int i = 0; i < 6; i++) {
        static const wchar_t *nm[] = { L"Display", L"Game looks", L"Scenes",
                                       L"Crosshair", L"Modes", L"Settings" };
        Widget *k = wadd(WT_SIDE, ID_SIDE_BASE + i, 16, 116 + i * 52, 184, 46, nm[i]);
        k->state = (ID_SIDE_BASE + i == g_panel);
    }
    wadd(WT_LABEL, 0, 24, 690, 180, 20, L"v" CX_VERSION "  ·  MIT source");
    Widget *c = wadd(WT_CARD_INFO, 0, 16, 716, 184, 64, L"FREE FOREVER");
    lstrcpyW(c->val, L"no account · no card");
}

static void build_top(void)
{
    wadd(WT_BTN, ID_CAP_MIN,   WIN_W - 104, 10, 42, 34, L"-");
    wadd(WT_BTN, ID_CAP_CLOSE, WIN_W - 54,  10, 42, 34, L"✕");
}

/* -------- display panel -------- */
static void build_display(void)
{
    wadd(WT_HEAD, 0, 252, 92, 500, 40, L"Display");
    wadd(WT_LABEL, 0, 252, 134, 640, 24, L"Your whole-screen look. Free — no account, no card.");
    wadd(WT_READOUT, 0, 1044, 84, 156, 92, NULL);

    Widget *t = wadd(WT_TOGGLE, ID_T_ENABLE, 252, 176, 340, 36, L"Look enabled");
    t->state = g_look.enabled;

    if (!Eng_Available())
        wadd(WT_LABEL, 0, 252, 216, 700, 22, L"Windows colour filter unavailable on this PC — using GPU gamma.");

    sl(wadd(WT_SLIDER, ID_SAT,   252, 254, 456, 74, L"Saturation"));
    sl(wadd(WT_SLIDER, ID_GAMMA, 744, 254, 456, 74, L"GPU gamma"));
    sl(wadd(WT_SLIDER, ID_BRI,   252, 344, 456, 74, L"Brightness"));
    sl(wadd(WT_SLIDER, ID_CON,   744, 344, 456, 74, L"Contrast"));
    sl(wadd(WT_SLIDER, ID_TEMP,  252, 434, 456, 74, L"Temperature"));
    sl(wadd(WT_SLIDER, ID_HUE,   744, 434, 456, 74, L"Hue shift"));

    Widget *r1 = wadd(WT_GHOST, ID_B_RESET, 252, 544, 170, 44, L"Reset all");
    (void)r1;
    wadd(WT_GHOST, ID_B_DISABLE, 434, 544, 190, 44, L"Switch look off");

    wadd(WT_DIV, 0, 252, 620, 948, 34, L"HOTKEYS");
    wadd(WT_LABEL, 0, 252, 662, 948, 24,
         L"Ctrl+Alt+↑ / ↓ saturation  ·  Ctrl+Alt+0 reset  ·  Ctrl+Alt+X crosshair  ·  Ctrl+Alt+E look on/off");
    wadd(WT_LABEL, 0, 252, 700, 948, 44,
         L"Saturation runs through the Windows colour matrix — the same layer Magnifier uses — so it reaches games, videos and your desktop alike.");
}

/* -------- games panel -------- */
static void build_games(void)
{
    wadd(WT_HEAD, 0, 252, 92, 600, 40, L"Game looks");
    wadd(WT_LABEL, 0, 252, 134, 700, 24, L"One look per game. Switches itself when the game opens.");
    Widget *t = wadd(WT_TOGGLE, ID_T_DETECT, 252, 170, 420, 36, L"Switch automatically");
    t->state = Prof_Detect();

    wadd(WT_DIV, 0, 252, 226, 948, 30, L"SAVED LOOKS");
    int n = Prof_Count();
    int row = 0;
    for (int i = 0; i < n && row < 6; i++, row++) {
        Profile *p = Prof_Get(i);
        wchar_t txt[160];
        wsprintfW(txt, L"%s", p->name);
        if (p->exe[0]) wsprintfW(txt, L"%s   ·   %s", p->name, p->exe);
        else wsprintfW(txt, L"%s   ·   always", p->name);
        Widget *k = wadd(WT_ROW, ID_PROF_BASE + i, 252, 258 + row * 44, 948, 40, txt);
        k->state = (g_selProf == i);
    }
    if (!n) wadd(WT_LABEL, 0, 252, 262, 900, 24, L"No profiles saved yet.");

    wadd(WT_GHOST, ID_B_PROF_APPLY, 252, 534, 250, 44, L"Save current values here");
    wadd(WT_GHOST, ID_B_PROF_DEL,   514, 534, 130, 44, L"Delete");

    Prof_RunningScan();
    wadd(WT_DIV, 0, 252, 600, 948, 30, L"RUNNING NOW — CLICK TO ADD");
    int rn = Prof_RunningCount(), shown = 0;
    int rstart = clampi(g_runScroll, 0, rn > 3 ? rn - 3 : 0);
    for (int i = rstart; i < rn && shown < 3; i++) {
        const wchar_t *exe = Prof_RunningGet(i);
        if (Prof_FindExe(exe) >= 0) continue;
        int slot = shown++;
        Widget *k = wadd(WT_ROW, ID_PROF_RUN + slot, 252, 632 + slot * 40, 948, 36, exe);
        k->flags = i; /* real running index */
    }
    if (!shown) wadd(WT_LABEL, 0, 252, 636, 900, 24, L"Nothing new is running (or every window is already saved).");
}

/* -------- scenes panel -------- */
static void build_scenes(void)
{
    wadd(WT_HEAD, 0, 252, 92, 600, 40, L"Scenes");
    wadd(WT_LABEL, 0, 252, 134, 800, 24,
         L"One-click looks for the situations where colour actually wins.");
    int cnt = 0;
    const SceneDef *sc = Scene_List(&cnt);
    for (int i = 0; i < cnt; i++) {
        int col = i % 4, rw = i / 4;
        int x = 252 + col * 242, y = 210 + rw * 168;
        Widget *k = wadd(WT_SCENE, ID_SCENE_BASE + i, x, y, 222, 144, sc[i].name);
        wsprintfW(k->val, L"sat %d · con %+d", (int)sc[i].look.sat, (int)sc[i].look.con);
        k->state = 0;
    }
    wadd(WT_DIV, 0, 252, 552, 948, 30, L"WHAT A SCENE CHANGES");
    wadd(WT_LABEL, 0, 252, 594, 948, 60,
         L"Saturation, brightness, contrast, temperature and gamma in one click. The values land in your Display sliders, so you can nudge them afterwards — scenes are a starting point, not a cage.");
    wadd(WT_LABEL, 0, 252, 664, 948, 44,
         L"Scene switches with a game too: save a scene into a profile on the Game looks tab and the app applies it when that game opens.");
}

/* -------- crosshair panel -------- */
static void build_cross(void)
{
    wadd(WT_HEAD, 0, 252, 92, 600, 40, L"Crosshair");
    wadd(WT_LABEL, 0, 252, 134, 820, 24,
         L"Drawn on top of your screen, not inside the game. Stays off until you turn it on.");
    Widget *t = wadd(WT_TOGGLE, ID_T_XH, 252, 168, 360, 36, L"Crosshair overlay");
    t->state = g_xh.on;

    wadd(WT_DIV, 0, 252, 222, 948, 30, L"SHAPE");
    static const int shapes[] = { ID_XH_CROSS, ID_XH_DOT, ID_XH_CIRCLE,
                                  ID_XH_CHEVRON, ID_XH_T, ID_XH_TTYPE };
    static const wchar_t *sn[] = { L"Cross", L"Dot", L"Circle", L"Chevron", L"T", L"T-type" };
    for (int i = 0; i < 6; i++) {
        Widget *k = wadd(WT_SHAPE, shapes[i], 252 + i * 158, 256, 142, 84, sn[i]);
        k->state = (g_xh.shape == XH_CROSS + i);
    }

    sl(wadd(WT_SLIDER, ID_XH_SIZE,  252, 366, 456, 74, L"Size"));
    sl(wadd(WT_SLIDER, ID_XH_GAP,   744, 366, 456, 74, L"Gap"));
    sl(wadd(WT_SLIDER, ID_XH_THICK, 252, 456, 456, 74, L"Thickness"));
    sl(wadd(WT_SLIDER, ID_XH_DOTOP, 744, 456, 456, 74, L"Dot opacity (dot shape)"));

    Widget *to = wadd(WT_TOGGLE, ID_T_OUTLINE, 252, 546, 360, 36, L"Outline");
    to->state = g_xh.outline;

    static const COLORREF cols[10] = {
        RGB(255,255,255), RGB(0xC6,0xFF,0x3D), RGB(0x4F,0xE3,0xFF), RGB(0xFF,0x4F,0xE3),
        RGB(0xFF,0x9E,0x3D), RGB(0xFF,0xF2,0x3D), RGB(0xFF,0x4F,0x4F), RGB(0x4F,0x7B,0xFF),
        RGB(0x3D,0xFF,0xB0), RGB(0,0,0) };
    wadd(WT_LABEL, 0, 252, 600, 300, 22, L"SHAPE COLOUR");
    for (int i = 0; i < 10; i++) {
        Widget *k = wadd(WT_SWATCH, ID_SWATCH_BASE + i, 252 + i * 54, 628, 44, 36, NULL);
        k->state = (g_xh.color == cols[i]);
        wsprintfW(k->val, L"%d", (int)cols[i]);
    }
    wadd(WT_LABEL, 0, 252, 678, 300, 22, L"OUTLINE COLOUR");
    for (int i = 0; i < 10; i++) {
        Widget *k = wadd(WT_SWATCH, ID_SWATCH_O + i, 252 + i * 54, 706, 44, 36, NULL);
        k->state = (g_xh.ocolor == cols[i]);
        wsprintfW(k->val, L"%d", (int)cols[i]);
    }
    wadd(WT_LABEL, 0, 812, 628, 390, 44, L"The real crosshair sits at the centre of your screen while this tab is open.");
}

/* -------- modes panel -------- */
static void build_modes(void)
{
    wadd(WT_HEAD, 0, 252, 92, 700, 40, L"Resolution & refresh");
    wadd(WT_LABEL, 0, 252, 134, 820, 24,
         L"Modes, refresh rate and stretched 4:3 — same tab as your colours.");

    ModeInfo cur;
    Modes_Current(&cur);
    wchar_t now[96];
    wsprintfW(now, L"Now: %d×%d @ %d Hz%s", cur.w, cur.h, cur.hz, cur.native ? L"  (native)" : L"");
    Widget *card = wadd(WT_CARD_INFO, 0, 252, 170, 560, 56, now);
    lstrcpyW(card->val, L"Windows asks you to keep a new mode — or it reverts.");

    int n = Modes_Count();
    int vis = 9;
    if (g_modeScroll > n - vis) g_modeScroll = clampi(n - vis, 0, 999);
    for (int r = 0; r < vis; r++) {
        int i = g_modeScroll + r;
        ModeInfo *m = Modes_Get(i);
        if (!m) break;
        wchar_t txt[80];
        const wchar_t *tag = m->native ? L"  ·  native" : L"";
        if (m->w * 9 == m->h * 16)
            wsprintfW(txt, L"%d×%d @ %d Hz%s  ·  16:9", m->w, m->h, m->hz, tag);
        else if ((long)m->w * 3 == (long)m->h * 4)
            wsprintfW(txt, L"%d×%d @ %d Hz%s  ·  4:3 stretched", m->w, m->h, m->hz, tag);
        else
            wsprintfW(txt, L"%d×%d @ %d Hz%s", m->w, m->h, m->hz, tag);
        Widget *k = wadd(WT_ROW, ID_MODE_BASE + r, 252, 244 + r * 44, 948, 40, txt);
        k->state = (g_selMode == i);
        k->flags = i;
    }

    wadd(WT_PRIMARY, ID_B_MODE_APPLY, 252, 660, 220, 46, L"Apply mode");
    wadd(WT_GHOST, ID_B_MODE_NATIVE, 486, 660, 200, 46, L"Back to native");
    wadd(WT_GHOST, ID_B_OPENHDR, 700, 660, 300, 46, L"HDR — open Windows settings");
    wadd(WT_LABEL, 0, 252, 724, 948, 40,
         L"Scroll the list with the wheel. Nothing here patches a game — it asks Windows for a display mode, exactly like Settings does.");
}

/* -------- settings panel -------- */
static void build_set(void)
{
    wadd(WT_HEAD, 0, 252, 92, 600, 40, L"Settings");
    wadd(WT_LABEL, 0, 252, 134, 820, 24, L"Everything is on by default. Nothing is locked.");

    wadd(WT_DIV, 0, 252, 176, 948, 30, L"STARTUP");
    Widget *t1 = wadd(WT_TOGGLE, ID_T_STARTWIN, 252, 214, 460, 36, L"Start with Windows");
    t1->state = g_startwin;

    wadd(WT_DIV, 0, 252, 274, 948, 30, L"PHONE CONTROL");
    Widget *t2 = wadd(WT_TOGGLE, ID_T_PHONE, 252, 312, 460, 36, L"Phone panel on this Wi-Fi");
    t2->state = g_phoneon;
    if (g_phoneon) {
        wchar_t u[160];
        wsprintfW(u, L"%s", Phone_Summary());
        wadd(WT_LABEL, 0, 252, 356, 700, 24, u);
        wadd(WT_GHOST, ID_B_PHONEURL, 640, 350, 190, 40, L"Copy link");
    } else {
        wadd(WT_LABEL, 0, 252, 356, 700, 24, L"Off. Turn on and the link shows here — same Wi-Fi only.");
    }

    wadd(WT_DIV, 0, 252, 412, 948, 30, L"ABOUT");
    wadd(WT_LABEL, 0, 252, 452, 948, 24,
         L"ChromaX v" CX_VERSION " · Windows 10/11 · single portable exe · no installer, no admin.");
    wadd(WT_LABEL, 0, 252, 484, 948, 24,
         L"No account, no telemetry, no phone-home. Settings live in a plain config.ini next to your app data.");
    wadd(WT_LABEL, 0, 252, 516, 948, 24,
         L"What it never does: read game memory · edit game files · inject code · install a driver.");

    wadd(WT_GHOST, ID_B_OPENFOLDER, 252, 566, 240, 44, L"Open settings folder");

    wadd(WT_DIV, 0, 252, 640, 948, 30, L"THE SHORT VERSION");
    wadd(WT_LABEL, 0, 252, 680, 948, 60,
         L"Every feature — saturation to 300%, scenes, per-game switching, crosshair, modes, phone control — is in this one free binary. There is no upgrade screen because there is nothing to upgrade to.");
}

static void build(void)
{
    g_nw = 0;
    build_top();
    build_side();
    switch (g_panel) {
    case ID_SIDE_DISPLAY: build_display(); break;
    case ID_SIDE_GAMES:   build_games();   break;
    case ID_SIDE_SCENES:  build_scenes();  break;
    case ID_SIDE_CROSS:   build_cross();   break;
    case ID_SIDE_MODES:   build_modes();   break;
    case ID_SIDE_SET:     build_set();     break;
    }
}

/* ---------------- painting ---------------- */

static void rrect(HDC dc, RECT rc, int rad, HBRUSH br, HPEN pen)
{
    HGDIOBJ ob = SelectObject(dc, pen ? pen : GetStockObject(NULL_PEN));
    HGDIOBJ obrush = SelectObject(dc, br);
    RoundRect(dc, rc.left, rc.top, rc.right, rc.bottom, rad, rad);
    SelectObject(dc, obrush);
    SelectObject(dc, ob);
}

static void txt(HDC dc, RECT rc, const wchar_t *s, HFONT f, COLORREF c, int align)
{
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, c);
    HGDIOBJ of = SelectObject(dc, f);
    DrawTextW(dc, s, -1, &rc, align | DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS);
    SelectObject(dc, of);
}

static void draw_xh_preview(HDC dc, RECT rc, const XhCfg *c)
{
    int cx = (rc.left + rc.right) / 2, cy = (rc.top + rc.bottom) / 2 - S(8);
    int sz = S(14), gap = S(max(c->gap / 2, 1)), th = max(S(c->thick), 3);
    HPEN pen = CreatePen(PS_SOLID, th, c->color);
    HGDIOBJ op = SelectObject(dc, pen);
    SetBkMode(dc, TRANSPARENT);
    switch (c->shape) {
    case XH_CROSS:
        MoveToEx(dc, cx - gap - sz, cy, NULL); LineTo(dc, cx + gap + sz, cy);
        MoveToEx(dc, cx, cy - gap - sz, NULL); LineTo(dc, cx, cy + gap + sz);
        break;
    case XH_DOT: {
        HBRUSH b = CreateSolidBrush(c->color);
        HGDIOBJ ob = SelectObject(dc, b);
        int r = S(6);
        Ellipse(dc, cx - r, cy - r, cx + r, cy + r);
        SelectObject(dc, ob); DeleteObject(b);
        break; }
    case XH_CIRCLE: {
        int r = sz;
        HGDIOBJ ob = SelectObject(dc, GetStockObject(NULL_BRUSH));
        Ellipse(dc, cx - r, cy - r, cx + r, cy + r);
        SelectObject(dc, ob);
        break; }
    case XH_CHEVRON: {
        POINT p[3] = { { cx - sz, cy - sz }, { cx + gap, cy }, { cx - sz, cy + sz } };
        Polyline(dc, p, 3);
        break; }
    case XH_T:
        MoveToEx(dc, cx - gap - sz, cy - gap, NULL); LineTo(dc, cx + gap + sz, cy - gap);
        MoveToEx(dc, cx, cy - gap, NULL); LineTo(dc, cx, cy + gap + sz);
        break;
    case XH_TTYPE:
        MoveToEx(dc, cx - gap - sz, cy + gap, NULL); LineTo(dc, cx + gap + sz, cy + gap);
        MoveToEx(dc, cx, cy - gap - sz, NULL); LineTo(dc, cx, cy + gap);
        break;
    }
    SelectObject(dc, op);
    DeleteObject(pen);
}

static void draw_widget(HDC dc, Widget *k)
{
    RECT rc = k->rc;
    int hov = (g_hover == k->id);
    HBRUSH br_card = CreateSolidBrush(C_CARD);
    HBRUSH br_card2 = CreateSolidBrush(C_CARD2);
    HBRUSH br_acc = CreateSolidBrush(C_ACC);
    HBRUSH br_bg = CreateSolidBrush(C_BG);
    HBRUSH br_side = CreateSolidBrush(C_SIDE);
    HPEN pen_line = CreatePen(PS_SOLID, 1, C_LINE);
    HPEN pen_acc = CreatePen(PS_SOLID, 1, C_ACC);
    HPEN pen_non = CreatePen(PS_SOLID, 1, RGB(0, 0, 0));

    switch (k->type) {
    case WT_HEAD:
        txt(dc, rc, k->text, g_fH1, C_TXT, DT_LEFT);
        break;
    case WT_LABEL:
        txt(dc, rc, k->text, g_fBody, (k->flags & 1) ? C_DIM : C_SUB, DT_LEFT);
        break;
    case WT_READOUT: {
        RECT r1 = rc, r2 = rc;
        wchar_t big[32];
        if (g_look.enabled) wsprintfW(big, L"%d%%", (int)g_look.sat);
        else lstrcpyW(big, L"OFF");
        r1.bottom = r1.top + S(58);
        r2.top = r1.bottom;
        txt(dc, r1, big, g_fBig, g_look.enabled ? C_ACC : C_DIM, DT_RIGHT);
        txt(dc, r2, L"saturation · free", g_fSmall, C_SUB, DT_RIGHT);
        break; }
    case WT_PRIMARY:
    case WT_GHOST:
    case WT_BTN: {
        HBRUSH fill;
        COLORREF fc = C_TXT;
        if (k->type == WT_PRIMARY) { fill = br_acc; fc = C_DARK; }
        else { fill = hov ? br_card2 : br_card; }
        rrect(dc, rc, S(10), fill, (hov || k->type == WT_PRIMARY) ? pen_acc : pen_line);
        if (k->id == ID_CAP_CLOSE && hov) {
            HBRUSH dang = CreateSolidBrush(C_DANG);
            rrect(dc, rc, S(10), dang, pen_line);
            DeleteObject(dang);
            fc = C_TXT;
        }
        txt(dc, rc, k->text, g_fH2, fc, DT_CENTER);
        break; }
    case WT_SLIDER: {
        /* label + value */
        RECT rl = rc, rv = rc;
        rl.bottom = rl.top + S(26);
        rv.bottom = rv.top + S(26);
        rv.left = rv.right - S(110);
        txt(dc, rl, k->text, g_fBody, C_TXT, DT_LEFT);
        txt(dc, rv, k->val, g_fMono, hov || g_active == k->id ? C_ACC : C_SUB, DT_RIGHT);
        /* track */
        int ins = S(4);
        int tx1 = rc.left + ins, tx2 = rc.right - ins;
        int ty = rc.top + S(50);
        RECT tr = { tx1, ty - S(3), tx2, ty + S(3) };
        rrect(dc, tr, S(3), br_card2, pen_non);
        float cur = 0;
        switch (k->id) {
        case ID_SAT: cur = g_look.sat; break;
        case ID_BRI: cur = g_look.bri; break;
        case ID_CON: cur = g_look.con; break;
        case ID_TEMP: cur = g_look.temp; break;
        case ID_HUE: cur = g_look.hue; break;
        case ID_GAMMA: cur = g_look.gamma; break;
        case ID_XH_SIZE: cur = (float)g_xh.size; break;
        case ID_XH_GAP: cur = (float)g_xh.gap; break;
        case ID_XH_THICK: cur = (float)g_xh.thick; break;
        case ID_XH_DOTOP: cur = (float)g_xh.dotop; break;
        }
        float f = clampf((cur - k->vmin) / (k->vmax - k->vmin), 0, 1);
        int fx = tx1 + (int)((tx2 - tx1) * f);
        if (fx > tx1) {
            RECT fr = { tx1, ty - S(3), fx, ty + S(3) };
            rrect(dc, fr, S(3), br_acc, pen_non);
        }
        int th = (hov || g_active == k->id) ? S(9) : S(7);
        RECT th_ = { fx - th, ty - th, fx + th, ty + th };
        rrect(dc, th_, th, CreateSolidBrush(RGB(255, 255, 255)), pen_non);
        break; }
    case WT_TOGGLE: {
        RECT rl = rc; rl.right -= S(56);
        txt(dc, rl, k->text, g_fBody, C_TXT, DT_LEFT);
        RECT pr = { rc.right - S(48), rc.top + S(5), rc.right, rc.bottom - S(5) };
        HBRUSH pb = CreateSolidBrush(k->state ? C_ACC : C_CARD2);
        rrect(dc, pr, (pr.bottom - pr.top) / 2, pb, pen_non);
        int kr = (pr.bottom - pr.top) / 2 - S(3);
        int kx = k->state ? pr.right - kr - S(4) : pr.left + kr + S(4);
        int ky = (pr.top + pr.bottom) / 2;
        RECT kb = { kx - kr, ky - kr, kx + kr, ky + kr };
        rrect(dc, kb, kr, CreateSolidBrush(k->state ? C_DARK : C_SUB), pen_non);
        DeleteObject(pb);
        break; }
    case WT_SIDE: {
        if (k->state) {
            RECT bg = rc;
            rrect(dc, bg, S(10), br_card2, pen_non);
            RECT bar = { rc.left + S(6), rc.top + S(10), rc.left + S(9), rc.bottom - S(10) };
            RECT b2 = bar; rrect(dc, b2, S(2), br_acc, pen_non);
        }
        RECT rl = rc; rl.left += S(18);
        txt(dc, rl, k->text, g_fH2, k->state ? C_TXT : (hov ? C_TXT : C_SUB), DT_LEFT);
        break; }
    case WT_ROW: {
        HBRUSH fill = k->state ? br_card2 : (hov ? br_card2 : br_card);
        HPEN pen = k->state ? pen_acc : pen_line;
        rrect(dc, rc, S(8), fill, pen);
        if (k->state) {
            RECT dot = { rc.left + S(12), rc.top + S(15), rc.left + S(22), rc.top + S(25) };
            rrect(dc, dot, S(5), br_acc, pen_non);
        }
        RECT rl = rc; rl.left += S(34); rl.right -= S(10);
        txt(dc, rl, k->text, g_fBody, k->state ? C_TXT : (hov ? C_TXT : C_SUB), DT_LEFT);
        break; }
    case WT_CARD_INFO: {
        rrect(dc, rc, S(10), br_card, pen_line);
        RECT r1 = rc, r2 = rc;
        r1.left += S(16); r1.bottom = r1.top + S(30);
        r2.left += S(16); r2.top = r1.bottom - S(2);
        txt(dc, r1, k->text, g_fH2, C_ACC, DT_LEFT);
        txt(dc, r2, k->val[0] ? k->val : L"", g_fSmall, C_SUB, DT_LEFT);
        break; }
    case WT_DIV: {
        RECT rt = rc;
        txt(dc, rt, k->text, g_fSmall, C_DIM, DT_LEFT);
        int y = (rc.top + rc.bottom) / 2;
        RECT ln = { rc.left + S(150), y, rc.right, y + 1 };
        FillRect(dc, &ln, br_line_dummy());
        break; }
    case WT_SCENE: {
        HBRUSH fill = hov ? br_card2 : br_card;
        rrect(dc, rc, S(12), fill, hov ? pen_acc : pen_line);
        RECT r1 = rc, r2 = rc;
        r1.left += S(16); r1.top += S(16); r1.bottom = r1.top + S(28);
        txt(dc, r1, k->text, g_fH2, C_TXT, DT_LEFT);
        r2.left += S(16); r2.top = r1.bottom; r2.bottom = r2.top + S(22);
        txt(dc, r2, k->val, g_fSmall, C_SUB, DT_LEFT);
        /* accent bar */
        RECT a1 = { rc.left + S(16), rc.bottom - S(24), rc.left + S(76), rc.bottom - S(18) };
        RECT a2 = { rc.left + S(82), rc.bottom - S(24), rc.left + S(112), rc.bottom - S(18) };
        rrect(dc, a1, S(3), br_acc, pen_non);
        rrect(dc, a2, S(3), CreateSolidBrush(C_VIO), pen_non);
        break; }
    case WT_SHAPE: {
        HBRUSH fill = hov ? br_card2 : br_card;
        rrect(dc, rc, S(10), fill, k->state ? pen_acc : pen_line);
        XhCfg pv = g_xh;
        pv.shape = k->id - ID_XH_CROSS;
        draw_xh_preview(dc, rc, &pv);
        RECT rl = rc; rl.top = rl.bottom - S(24);
        txt(dc, rl, k->text, g_fSmall, k->state ? C_ACC : C_SUB, DT_CENTER);
        break; }
    case WT_SWATCH: {
        COLORREF col = (COLORREF)_wtoi(k->val);
        HBRUSH b = CreateSolidBrush(col);
        RECT inner = { rc.left + S(3), rc.top + S(3), rc.right - S(3), rc.bottom - S(3) };
        rrect(dc, inner, S(8), b, k->state ? pen_acc : pen_line);
        if (k->state) {
            RECT o = rc;
            rrect(dc, o, S(9), (HBRUSH)GetStockObject(NULL_BRUSH), pen_acc);
        }
        DeleteObject(b);
        break; }
    }

    DeleteObject(br_card); DeleteObject(br_card2); DeleteObject(br_acc);
    DeleteObject(br_bg); DeleteObject(br_side);
    DeleteObject(pen_line); DeleteObject(pen_acc); DeleteObject(pen_non);
}

/* dummy for divider line — real brush */
static HBRUSH br_line_dummy(void)
{
    static HBRUSH b;
    if (!b) b = CreateSolidBrush(C_LINE);
    return b;
}

void Ui_Paint(HDC hdc, const RECT *rc)
{
    HBRUSH bg = CreateSolidBrush(C_BG);
    RECT all = *rc;
    FillRect(hdc, &all, bg);
    DeleteObject(bg);

    HBRUSH side = CreateSolidBrush(C_SIDE);
    RECT sr = { 0, 0, S(SIDE_W), S(WIN_H) };
    FillRect(hdc, &sr, side);
    DeleteObject(side);
    RECT sepline = { S(SIDE_W), 0, S(SIDE_W) + 1, S(WIN_H) };
    HBRUSH lb = CreateSolidBrush(C_LINE);
    FillRect(hdc, &sepline, lb);
    RECT topline = { 0, S(TOP_H), S(WIN_W), S(TOP_H) + 1 };
    FillRect(hdc, &topline, lb);
    DeleteObject(lb);

    /* logo mark */
    {
        RECT box = { S(20), S(14), S(54), S(48) };
        HBRUSH acc = CreateSolidBrush(C_ACC);
        rrect(hdc, box, S(9), acc, NULL);
        DeleteObject(acc);
        HPEN xp = CreatePen(PS_SOLID, S(4), C_DARK);
        HGDIOBJ op = SelectObject(hdc, xp);
        MoveToEx(hdc, S(28), S(22), NULL); LineTo(hdc, S(46), S(40));
        MoveToEx(hdc, S(46), S(22), NULL); LineTo(hdc, S(28), S(40));
        SelectObject(hdc, op);
        DeleteObject(xp);
        RECT lt = { S(64), S(14), S(220), S(36) };
        txt(hdc, lt, L"CHROMAX", g_fH1, C_TXT, DT_LEFT);
        RECT ls = { S(65), S(36), S(220), S(54) };
        txt(hdc, ls, L"monitor colour, unleashed", g_fSmall, C_SUB, DT_LEFT);
    }

    /* title + toast */
    RECT tr = { S(SIDE_W + 36), S(10), S(WIN_W - 240), S(TOP_H) - S(4) };
    txt(hdc, tr, g_title, g_fH2, C_TXT, DT_LEFT);

    if (g_toast[0] && GetTickCount() - g_toastT < 4000) {
        RECT tw = { S(WIN_W - 620), S(12), S(WIN_W - 116), S(TOP_H) - S(8) };
        HBRUSH cb = CreateSolidBrush(C_CARD);
        HPEN cp = CreatePen(PS_SOLID, 1, C_ACC);
        rrect(hdc, tw, S(10), cb, cp);
        RECT td = tw; td.left += S(14);
        wchar_t t2[140]; wsprintfW(t2, L"●  %s", g_toast);
        txt(hdc, td, t2, g_fSmall, C_ACC, DT_LEFT);
        DeleteObject(cb); DeleteObject(cp);
    }

    for (int i = 0; i < g_nw; i++) draw_widget(hdc, &g_w[i]);
}

/* ---------------- interaction ---------------- */

static Widget *hit(int x, int y)
{
    for (int i = g_nw - 1; i >= 0; i--) {
        RECT r = g_w[i].rc;
        if (x >= r.left && x < r.right && y >= r.top && y < r.bottom)
            return &g_w[i];
    }
    return NULL;
}

int Ui_CapHit(int x, int y)
{
    Widget *k = hit(x, y);
    if (k && (k->id == ID_CAP_MIN || k->id == ID_CAP_CLOSE)) return k->id;
    return 0;
}

int Ui_InTop(int x, int y) { return y < S(TOP_H); }

static void slider_from_x(Widget *k, int x)
{
    int ins = S(4);
    int tx1 = k->rc.left + ins + S(9), tx2 = k->rc.right - ins - S(9);
    float f = clampf((float)(x - tx1) / (float)(tx2 - tx1), 0.f, 1.f);
    float v = k->vmin + f * (k->vmax - k->vmin);
    switch (k->id) {
    case ID_SAT:   g_look.sat = floorf(v + .5f); break;
    case ID_BRI:   g_look.bri = floorf(v + .5f); break;
    case ID_CON:   g_look.con = floorf(v + .5f); break;
    case ID_TEMP:  g_look.temp = floorf(v + .5f); break;
    case ID_HUE:   g_look.hue = floorf(v + .5f); break;
    case ID_GAMMA: g_look.gamma = floorf(v * 100.f + .5f) / 100.f; break;
    case ID_XH_SIZE:  g_xh.size = (int)(v + .5f); break;
    case ID_XH_GAP:   g_xh.gap = (int)(v + .5f); break;
    case ID_XH_THICK: g_xh.thick = (int)(v + .5f); break;
    case ID_XH_DOTOP: g_xh.dotop = (int)(v + .5f); break;
    default: return;
    }
    sl(k);
}

static void set_startwin(int on)
{
    const wchar_t *run = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
    HKEY hk;
    if (RegOpenKeyW(HKEY_CURRENT_USER, run, &hk) != ERROR_SUCCESS) return;
    if (on) {
        wchar_t cmd[MAX_PATH + 4];
        wsprintfW(cmd, L"\"%s\"", g_appdir_exe());
        RegSetValueExW(hk, L"ChromaX", 0, REG_SZ, (const BYTE *)cmd,
                       (DWORD)((wcslen(cmd) + 1) * sizeof(wchar_t)));
    } else {
        RegDeleteValueW(hk, L"ChromaX");
    }
    RegCloseKey(hk);
    g_startwin = on;
}

int Ui_Exec(int id)
{
    switch (id) {
    case ID_CAP_MIN:  ShowWindow(g_ui_hwnd, SW_MINIMIZE); return 1;
    case ID_CAP_CLOSE: PostMessageW(g_ui_hwnd, WM_CLOSE, 0, 0); return 1;
    case ID_SIDE_DISPLAY: case ID_SIDE_GAMES: case ID_SIDE_SCENES:
    case ID_SIDE_CROSS: case ID_SIDE_MODES: case ID_SIDE_SET:
        Ui_SetPanel(id); return 1;
    case ID_T_ENABLE:
        g_look.enabled = !g_look.enabled; build(); Main_ApplyAll();
        Ui_Notify(g_look.enabled ? L"Look on" : L"Look off"); return 1;
    case ID_T_DETECT:
        Prof_SetDetect(!Prof_Detect()); build();
        Ui_Notify(Prof_Detect() ? L"Auto-switch on" : L"Auto-switch off"); return 1;
    case ID_T_XH:
        g_xh.on = !g_xh.on; build(); Main_ApplyAll(); return 1;
    case ID_T_OUTLINE:
        g_xh.outline = !g_xh.outline; build(); Main_ApplyAll(); return 1;
    case ID_T_STARTWIN:
        set_startwin(!g_startwin); build();
        Ui_Notify(g_startwin ? L"Starts with Windows" : L"Removed from startup"); return 1;
    case ID_T_PHONE:
        if (!g_phoneon) {
            if (Phone_Start() == 0) { g_phoneon = 1; Phone_SetLook(&g_look);
                Ui_Notify(L"Phone panel on"); }
            else Ui_Notify(L"Port 8777 busy");
        } else { Phone_Stop(); g_phoneon = 0; Ui_Notify(L"Phone panel off"); }
        build(); return 1;
    case ID_B_PHONEURL: {
        if (OpenClipboard(g_ui_hwnd)) {
            EmptyClipboard();
            size_t n = (wcslen(Phone_Summary()) + 1) * sizeof(wchar_t);
            HGLOBAL h = GlobalAlloc(GMEM_MOVEABLE, n);
            if (h) { memcpy(GlobalLock(h), Phone_Summary(), n); GlobalUnlock(h);
                     SetClipboardData(CF_UNICODETEXT, h); }
            CloseClipboard();
            Ui_Notify(L"Link copied");
        }
        return 1; }
    case ID_B_RESET: {
        Look z = { 100, 0, 0, 0, 0, 1.f, g_look.enabled };
        g_look = z; build(); Main_ApplyAll(); Ui_Notify(L"Reset — 100%"); return 1; }
    case ID_B_DISABLE:
        g_look.enabled = 0; build(); Main_ApplyAll();
        Ui_Notify(L"Look off — screen untouched"); return 1;
    /* crosshair shapes */
    case ID_XH_CROSS:  g_xh.shape = XH_CROSS;  build(); Main_ApplyAll(); return 1;
    case ID_XH_DOT:    g_xh.shape = XH_DOT;    build(); Main_ApplyAll(); return 1;
    case ID_XH_CIRCLE: g_xh.shape = XH_CIRCLE; build(); Main_ApplyAll(); return 1;
    case ID_XH_CHEVRON:g_xh.shape = XH_CHEVRON;build(); Main_ApplyAll(); return 1;
    case ID_XH_T:      g_xh.shape = XH_T;      build(); Main_ApplyAll(); return 1;
    case ID_XH_TTYPE:  g_xh.shape = XH_TTYPE;  build(); Main_ApplyAll(); return 1;
    /* profiles */
    case ID_B_PROF_APPLY:
        if (g_selProf >= 0 && g_selProf < Prof_Count()) {
            Profile *p = Prof_Get(g_selProf);
            p->look = g_look;
            Prof_Save();
            Ui_Notify(L"Values saved to profile");
        } else Ui_Notify(L"Pick a profile first");
        return 1;
    case ID_B_PROF_DEL:
        if (g_selProf >= 0) {
            Prof_Del(g_selProf);
            g_selProf = -1; build(); Ui_Notify(L"Profile deleted");
        }
        return 1;
    /* modes */
    case ID_B_MODE_APPLY:
        if (g_selMode >= 0) {
            if (Modes_Apply(g_selMode) == 0) Ui_Notify(L"Mode applied");
            else Ui_Notify(L"Windows refused that mode");
            build();
        } else Ui_Notify(L"Pick a mode first");
        return 1;
    case ID_B_MODE_NATIVE: {
        int n = Modes_Count(), nat = 0;
        for (int i = 0; i < n; i++) if (Modes_Get(i)->native) { nat = i; break; }
        if (Modes_Apply(nat) == 0) Ui_Notify(L"Back to native");
        build(); return 1; }
    case ID_B_OPENHDR:
        Modes_OpenHdr(); return 1;
    case ID_B_OPENFOLDER: {
        wchar_t cmd[8 + MAX_PATH];
        wsprintfW(cmd, L"explorer %s", g_appdata);
        ShellExecuteW(NULL, L"open", g_appdata, NULL, NULL, SW_SHOWNORMAL);
        return 1; }
    }
    return 0;
}

int Ui_MouseDown(int x, int y)
{
    Widget *k = hit(x, y);
    if (!k) return 0;
    switch (k->type) {
    case WT_SLIDER:
        g_active = k->id;
        SetCapture(g_ui_hwnd);
        slider_from_x(k, x);
        build(); Main_ApplyAll();
        return 1;
    case WT_TOGGLE:
        Ui_Exec(k->id);
        return 1;
    case WT_BTN:
    case WT_PRIMARY:
    case WT_GHOST:
        Ui_Exec(k->id);
        return 1;
    case WT_SIDE:
        Ui_Exec(k->id);
        return 1;
    case WT_ROW:
        if (k->id >= ID_PROF_BASE && k->id < ID_PROF_BASE + 16) {
            g_selProf = k->id - ID_PROF_BASE;
            Profile *p = Prof_Get(g_selProf);
            if (p) { Ui_LoadLook(&p->look); Ui_Notify(p->name); Main_ApplyAll(); }
            build();
        } else if (k->id >= ID_PROF_RUN && k->id < ID_PROF_RUN + 25) {
            const wchar_t *exe = Prof_RunningGet(k->flags);
            if (exe[0]) {
                wchar_t nm[64];
                lstrcpynW(nm, exe, 64);
                wchar_t *dot = wcsrchr(nm, L'.');
                if (dot) *dot = 0;
                int idx = Prof_Add(nm, exe, &g_look);
                g_selProf = idx;
                build();
                Ui_Notify(L"Profile added");
            }
        } else if (k->id >= ID_MODE_BASE && k->id < ID_MODE_BASE + 32) {
            g_selMode = k->flags;
            build();
        }
        return 1;
    case WT_SCENE:
        if (k->id >= ID_SCENE_BASE && k->id < ID_SCENE_BASE + 8) {
            int cnt; const SceneDef *sc = Scene_List(&cnt);
            int i = k->id - ID_SCENE_BASE;
            if (i < cnt) {
                g_look = sc[i].look;
                build(); Main_ApplyAll();
                Ui_Notify(sc[i].name);
            }
        }
        return 1;
    case WT_SHAPE:
        Ui_Exec(k->id);
        return 1;
    case WT_SWATCH:
        if (k->id >= ID_SWATCH_BASE && k->id < ID_SWATCH_BASE + 10) {
            g_xh.color = (COLORREF)_wtoi(k->val);
            build(); Main_ApplyAll();
        } else if (k->id >= ID_SWATCH_O && k->id < ID_SWATCH_O + 10) {
            g_xh.ocolor = (COLORREF)_wtoi(k->val);
            build(); Main_ApplyAll();
        }
        return 1;
    }
    return 0;
}

int Ui_MouseMove(int x, int y, int dragging)
{
    Widget *k = hit(x, y);
    int id = k ? k->id : 0;
    if (g_active && dragging) {
        Widget *a = NULL;
        for (int i = 0; i < g_nw; i++) if (g_w[i].id == g_active) a = &g_w[i];
        if (a) {
            float before = a->vmin;
            (void)before;
            slider_from_x(a, x);
            Main_ApplyAll();
            return -1; /* repaint */
        }
    }
    if (id != g_hover) { g_hover = id; return -1; } /* repaint */
    return 0;
}

void Ui_MouseUp(int x, int y)
{
    (void)x; (void)y;
    if (g_active) { g_active = 0; ReleaseCapture(); build(); }
}

int Ui_Hover(int x, int y)
{
    Widget *k = hit(x, y);
    return k ? k->id : 0;
}

int Ui_Wheel(int x, int y, int delta)
{
    (void)x;
    Widget *k = hit(x, y);
    int id = k ? k->id : 0;
    if (g_panel == ID_SIDE_MODES && y > S(240) && y < S(640)) {
        g_modeScroll = clampi(g_modeScroll - (delta > 0 ? 1 : -1), 0,
                              Modes_Count() - 1);
        build();
        return 1;
    }
    if (g_panel == ID_SIDE_GAMES && id == 0 && y > S(600)) {
        g_runScroll = clampi(g_runScroll + (delta > 0 ? -1 : 1), 0, 12);
        build();
        return 1;
    }
    return 0;
}

/* ---------------- api ---------------- */

void Ui_Init(HWND hwnd, HINSTANCE inst)
{
    g_ui_hwnd = hwnd;
    (void)inst;
    HDC dc = GetDC(hwnd);
    g_sc = GetDeviceCaps(dc, LOGPIXELSX) / 96.f;
    ReleaseDC(hwnd, dc);
    if (g_sc < 0.5f) g_sc = 1.f;
    mkfonts();
    /* start-with-windows state */
    HKEY rk;
    if (RegOpenKeyW(HKEY_CURRENT_USER,
                    L"Software\\Microsoft\\Windows\\CurrentVersion\\Run", &rk) == ERROR_SUCCESS) {
        DWORD t = 0;
        g_startwin = (RegQueryValueExW(rk, L"ChromaX", NULL, &t, NULL, NULL) == ERROR_SUCCESS);
        RegCloseKey(rk);
    }
    build();
}

void Ui_Free(void)
{
    DeleteObject(g_fBig); DeleteObject(g_fH1); DeleteObject(g_fH2);
    DeleteObject(g_fBody); DeleteObject(g_fSmall); DeleteObject(g_fMono);
}

void Ui_SetPanel(int side)
{
    g_panel = side;
    static const wchar_t *t[] = { L"Display", L"Game looks", L"Scenes",
                                  L"Crosshair", L"Resolution & refresh", L"Settings" };
    int i = clampi(side - ID_SIDE_BASE, 0, 5);
    lstrcpynW(g_title, t[i], 64);
    build();
}

int Ui_Panel(void) { return g_panel; }
Look *Ui_Look(void) { return &g_look; }

void Ui_LoadLook(const Look *lk)
{
    g_look = *lk;
    build();
}

void Ui_Title(const wchar_t *t) { lstrcpynW(g_title, t, 64); }

void Ui_Notify(const wchar_t *msg)
{
    lstrcpynW(g_toast, msg, 128);
    g_toastT = GetTickCount();
    if (g_ui_hwnd) InvalidateRect(g_ui_hwnd, NULL, FALSE);
}

void Ui_SyncProfiles(void) { build(); }
int  Ui_SelProfile(void) { return g_selProf; }
void Ui_SelProfileSet(int i) { g_selProf = i; }
void Ui_RebuildPanel(void) { build(); }

void Ui_GetXh(XhCfg *out) { *out = g_xh; }
void Ui_SetXh(const XhCfg *in) { g_xh = *in; build(); }
int  Ui_Detect(void) { return Prof_Detect(); }
int  Ui_StartWin(void) { return g_startwin; }
int  Ui_PhoneOn(void) { return g_phoneon; }

void Ui_SyncStartWin(int on) { g_startwin = on; }

int Ui_ScaleLogicalW(void) { return (int)(WIN_W * g_sc); }
int Ui_ScaleLogicalH(void) { return (int)(WIN_H * g_sc); }
int Ui_Scaled(int v) { return S(v); }
