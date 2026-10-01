/* PlexusX — UI core: DPI, fonts, the widget table, navigation, painting, input,
 * the modal helpers and the single action dispatcher.
 *
 * Panels (panel_*.c) only build widgets and read authoritative state; every
 * interaction rule and every drawing rule lives here.  The bottom status area
 * always shows the same five readouts — COLOR ENGINE · CURRENT GAME · CURRENT
 * PROFILE · DISPLAY · VERSION — each read from its owner at paint time, never
 * cached into a second copy of the truth.
 */
#include "ui_internal.h"
#include "../color/color_math.h"
#include "../ui_theme.h"
#include <commdlg.h>

#define C_BG       PX_T_BG
#define C_SIDE     PX_T_SIDE
#define C_CARD     PX_T_SURFACE
#define C_CARD2    PX_T_SURFACE_HOVER
#define C_CARD_ACT PX_T_SURFACE_ACTIVE
#define C_INSET    PX_T_INSET
#define C_LINE     PX_T_BORDER
#define C_GRID     PX_T_GRID
#define C_TXT      PX_T_TEXT
#define C_SUB      PX_T_TEXT_SUB
#define C_DIM      PX_T_TEXT_DIM
#define C_ACC      PX_T_ACCENT
#define C_ACC2     PX_T_CYAN
#define C_VIOLET   PX_T_VIOLET
#define C_DARK     PX_T_ON_ACCENT
#define C_DANG     PX_T_DANGER
#define C_OK       PX_T_SUCCESS
#define C_WARN     PX_T_WARNING

UiState g_ui;

#define g_w        g_ui.w
#define g_nw       g_ui.nw
#define g_panel    g_ui.panel
#define g_xh       g_ui.xh

int UiS(int v) { return (int)(v * g_ui.sc + 0.5f); }
#define S(v) UiS(v)

/* ---------------- fonts ---------------- */
static int F(int pt) { return (int)(pt * g_ui.sc + 0.5f); }

static void make_fonts(void)
{
    g_ui.fLogo   = CreateFontW(-F(PX_T_F_LOGO), 0, 0, 0, FW_BOLD, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
    g_ui.fH1     = CreateFontW(-F(PX_T_F_H1), 0, 0, 0, FW_BOLD, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
    g_ui.fH2     = CreateFontW(-F(PX_T_F_H2), 0, 0, 0, FW_SEMIBOLD, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
    g_ui.fBody   = CreateFontW(-F(PX_T_F_BODY), 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
    g_ui.fSmall  = CreateFontW(-F(PX_T_F_SMALL), 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
    g_ui.fMono   = CreateFontW(-F(PX_T_F_MONO), 0, 0, 0, FW_BOLD, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Consolas");
    g_ui.fBigVal = CreateFontW(-F(PX_T_F_BIG), 0, 0, 0, FW_BOLD, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
}

/* ---------------- string helpers ---------------- */
const wchar_t *UiUtf8(const char *utf8, wchar_t *buf, size_t cap)
{
    if (!buf || !cap) return L"";
    if (!utf8 || !utf8[0]) { buf[0] = 0; return buf; }
    if (!MultiByteToWideChar(CP_UTF8, 0, utf8, -1, buf, (int)cap)) buf[0] = 0;
    buf[cap - 1] = 0;
    return buf;
}

void UiWin(wchar_t *dst, size_t cap, const wchar_t *src)
{
    size_t i = 0;
    if (!dst || !cap) return;
    if (src) for (; src[i] && i + 1 < cap; i++) dst[i] = src[i];
    dst[i] = 0;
}

/* ---------------- widget construction ---------------- */
Widget *UiW(int type, int id, int x, int y, int w, int h, const wchar_t *text)
{
    Widget *k;
    if (g_nw >= MAX_WIDGETS) return &g_w[0];
    k = &g_w[g_nw++];
    memset(k, 0, sizeof *k);
    k->type = type;
    k->id = id;
    k->rc = (RECT){ S(x), S(y), S(x + w), S(y + h) };
    if (text) UiWin(k->text, 96, text);
    return k;
}

void UiWVal(Widget *k, float val, float lo, float hi, const wchar_t *unit)
{
    if (!k) return;
    k->vmin = lo;
    k->vmax = hi;
    if (!unit || !unit[0]) { _snwprintf(k->val, 48, L"%d%%", (int)(val + 0.5f)); return; }
    if (wcscmp(unit, L"K") == 0)               _snwprintf(k->val, 48, L"%dK", (int)(val + 0.5f));
    else if (wcscmp(unit, L"gamma") == 0)      _snwprintf(k->val, 48, L"%.2f", val);
    else if (wcscmp(unit, L"deg") == 0)        _snwprintf(k->val, 48, L"%+d°", (int)(val + 0.5f));
    else if (wcscmp(unit, L"tint") == 0)       _snwprintf(k->val, 48, L"%+d%%", (int)(val + 0.5f));
    else if (wcscmp(unit, L"px") == 0)         _snwprintf(k->val, 48, L"%d px", (int)(val + 0.5f));
    else if (wcscmp(unit, L"ms") == 0)         _snwprintf(k->val, 48, L"%d ms", (int)(val + 0.5f));
    else                                       _snwprintf(k->val, 48, L"%d%%", (int)(val + 0.5f));
    k->val[47] = 0;
}

Widget *UiWCard(int x, int y, int w, int h, const wchar_t *head, const wchar_t *val, const wchar_t *sub)
{
    Widget *k = UiW(WT_CARD, 0, x, y, w, h, head);
    if (val) UiWin(k->val, 48, val);
    if (sub) UiWin(k->sub, 64, sub);
    return k;
}

Widget *UiWKV(int x, int y, int w, const wchar_t *key, const wchar_t *val)
{
    Widget *k = UiW(WT_KV, 0, x, y, w, 22, key);
    if (val) UiWin(k->val, 48, val);
    return k;
}

Widget *UiWChip(int x, int y, int w, const wchar_t *text, int tone)
{
    Widget *k = UiW(WT_CHIP, 0, x, y, w, 24, text);
    k->flags = tone;
    return k;
}

/* ---------------- slider binding (the ONLY place ids meet values) ---------------- */
static float slider_get(int id, const Look *l, const XhCfg *x)
{
    switch (id) {
    case ID_SL_SAT:         return l->sat;
    case ID_SL_VIB:         return l->vibrance;
    case ID_SL_BRI:         return l->bri;
    case ID_SL_CON:         return l->con;
    case ID_SL_GAMMA:       return l->gamma;
    case ID_SL_TEMP:        return l->temp;
    case ID_SL_TINT:        return l->tint;
    case ID_SL_R_GAIN:      return l->r_gain;
    case ID_SL_G_GAIN:      return l->g_gain;
    case ID_SL_B_GAIN:      return l->b_gain;
    case ID_SL_SHADOWS:     return l->shadows;
    case ID_SL_HIGHLIGHTS:  return l->highlights;
    case ID_SL_BLACK_LEVEL: return l->black_level;
    case ID_SL_WHITE_POINT: return l->white_point;
    case ID_SL_CLARITY:     return l->clarity;
    case ID_SL_HUE:         return l->hue;
    case ID_SL_XH_SIZE:     return (float)x->size;
    case ID_SL_XH_GAP:      return (float)x->gap;
    case ID_SL_XH_THICK:    return (float)x->thick;
    case ID_SL_XH_OPACITY:  return (float)x->opacity;
    case ID_SL_XH_DOTSIZE:  return (float)x->dot_size;
    case ID_SL_XH_ROT:      return (float)x->rotation;
    default:                return 0.0f;
    }
}

float UiSl_Get(int id) { return slider_get(id, Eng_GetRequested(), &g_xh); }

Look *UiLook_Mut(void) { return (Look *)Eng_GetRequested(); }

int UiSl_Set(int id, float val)
{
    Look l;
    if (id >= ID_SL_XH_SIZE) {
        switch (id) {
        case ID_SL_XH_SIZE:    g_xh.size = clampi((int)(val + 0.5f), 4, 64); break;
        case ID_SL_XH_GAP:     g_xh.gap = clampi((int)(val + 0.5f), 0, 32); break;
        case ID_SL_XH_THICK:   g_xh.thick = clampi((int)(val + 0.5f), 1, 12); break;
        case ID_SL_XH_OPACITY: g_xh.opacity = clampi((int)(val + 0.5f), 10, 100); break;
        case ID_SL_XH_DOTSIZE: g_xh.dot_size = clampi((int)(val + 0.5f), 1, 8); break;
        case ID_SL_XH_ROT:     g_xh.rotation = clampi((int)(val + 0.5f), 0, 359); break;
        default: return 0;
        }
        Xh_Update(&g_xh);
        return 1;
    }
    l = *Eng_GetRequested();
    switch (id) {
    case ID_SL_SAT:         l.sat = clampf(val, 0, 300); break;
    case ID_SL_VIB:         l.vibrance = clampf(val, 0, 300); break;
    case ID_SL_BRI:         l.bri = clampf(val, 0, 200); break;
    case ID_SL_CON:         l.con = clampf(val, 0, 200); break;
    case ID_SL_GAMMA:       l.gamma = clampf(val, 0.40f, 2.50f); break;
    case ID_SL_TEMP:        l.temp = clampf(val, 3000, 10000); break;
    case ID_SL_TINT:        l.tint = clampf(val, -100, 100); break;
    case ID_SL_R_GAIN:      l.r_gain = clampf(val, 0, 200); break;
    case ID_SL_G_GAIN:      l.g_gain = clampf(val, 0, 200); break;
    case ID_SL_B_GAIN:      l.b_gain = clampf(val, 0, 200); break;
    case ID_SL_SHADOWS:     l.shadows = clampf(val, 0, 200); break;
    case ID_SL_HIGHLIGHTS:  l.highlights = clampf(val, 0, 200); break;
    case ID_SL_BLACK_LEVEL: l.black_level = clampf(val, 0, 200); break;
    case ID_SL_WHITE_POINT: l.white_point = clampf(val, 0, 200); break;
    case ID_SL_CLARITY:     l.clarity = clampf(val, 0, 200); break;
    case ID_SL_HUE:         l.hue = clampf(val, -180, 180); break;
    default: return 0;
    }
    Eng_SetLook(&l);           /* sanitises + bumps the revision */
    return 1;
}

float UiSl_Default(int id)
{
    switch (id) {
    case ID_SL_GAMMA: return 1.00f;
    case ID_SL_TEMP:  return 6500.0f;
    case ID_SL_TINT:
    case ID_SL_HUE:   return 0.0f;
    case ID_SL_XH_SIZE: return 16.0f;
    case ID_SL_XH_GAP:  return 4.0f;
    case ID_SL_XH_THICK: return 2.0f;
    case ID_SL_XH_OPACITY: return 100.0f;
    case ID_SL_XH_DOTSIZE: return 2.0f;
    case ID_SL_XH_ROT: return 0.0f;
    default: return 100.0f;
    }
}

int UiSl_Commit(void)
{
    Ui_RebuildPanel();
    Main_ApplyAll();
    return 1;
}

void UiLook_Commit(void)
{
    Look l = *Eng_GetRequested();
    Eng_SetLook(&l);            /* sanitise + bump revision */
    UiSl_Commit();
}

/* ---------------- status helpers ---------------- */
const PxEffectiveState *UiStatus(void) { return Eng_Effective(); }

int UiToneOfStatus(void) { return PxStatus_Tone(UiStatus()->status); }

int UiEngineStatusIs(const int *states, int n)
{
    int st = UiStatus()->status;
    for (int i = 0; i < n; i++) if (states[i] == st) return 1;
    return 0;
}

COLORREF UiToneColor(int tone)
{
    switch (tone) {
    case PX_CHIP_OK:   return C_OK;
    case PX_CHIP_WARN: return C_WARN;
    case PX_CHIP_BAD:  return C_DANG;
    case PX_CHIP_INFO: return C_ACC2;
    default:           return C_SUB;
    }
}

/* ---------------- drawing helpers ---------------- */
static COLORREF px_to_color(float r, float g, float b)
{
    int ir = clampi((int)(cm_clampf(r, 0.0f, 1.0f) * 255.0f + 0.5f), 0, 255);
    int ig = clampi((int)(cm_clampf(g, 0.0f, 1.0f) * 255.0f + 0.5f), 0, 255);
    int ib = clampi((int)(cm_clampf(b, 0.0f, 1.0f) * 255.0f + 0.5f), 0, 255);
    return RGB(ir, ig, ib);
}

void UiDrawRRect(HDC dc, RECT rc, int rad, HBRUSH br, HPEN pen)
{
    HGDIOBJ ob = SelectObject(dc, pen ? pen : GetStockObject(NULL_PEN));
    HGDIOBJ obr = SelectObject(dc, br);
    RoundRect(dc, rc.left, rc.top, rc.right, rc.bottom, rad, rad);
    SelectObject(dc, obr);
    SelectObject(dc, ob);
}

void UiDrawText(HDC dc, RECT rc, const wchar_t *s, HFONT f, COLORREF c, int align)
{
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, c);
    if (!s) s = L"";
    {
        HGDIOBJ of = SelectObject(dc, f);
        DrawTextW(dc, s, -1, &rc, align | DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS);
        SelectObject(dc, of);
    }
}

/* Live preview: the SAME math the pipeline pushes (cm_apply_pixel) — left half
 * identity, right half what the engine will ACTUALLY output.  When the engine
 * is bypassed or the output is HDR passthrough, the right half is identity too:
 * the preview never promises something the display cannot show. */
void UiDrawSplitPreview(HDC dc, RECT rc)
{
    static const float chips[][3] = {
        { 0.00f, 0.00f, 0.00f }, { 0.20f, 0.20f, 0.20f }, { 0.50f, 0.50f, 0.50f }, { 0.80f, 0.80f, 0.80f },
        { 1.00f, 1.00f, 1.00f }, { 1.00f, 0.00f, 0.00f }, { 0.00f, 1.00f, 0.00f }, { 0.00f, 0.00f, 1.00f },
        { 0.00f, 1.00f, 1.00f }, { 1.00f, 0.00f, 1.00f }, { 1.00f, 1.00f, 0.00f }, { 0.76f, 0.57f, 0.46f },
        { 0.40f, 0.70f, 0.30f }, { 0.20f, 0.35f, 0.70f }, { 0.95f, 0.55f, 0.20f }, { 0.55f, 0.20f, 0.15f }
    };
    const PxEffectiveState *e = UiStatus();
    const int nchips = (int)(sizeof chips / sizeof chips[0]);
    int w = rc.right - rc.left, h = rc.bottom - rc.top;
    int split_x = rc.left + (int)(w * g_ui.split_pos);
    int cols = 8, rows = 2, pad = S(12), gap = S(6), cw, ch;
    Look identity = LOOK_NEUTRAL_INIT;
    Look tuned = e->effective;      /* authoritative: what will be output */
    HBRUSH bg;

    if (w < 64 || h < 64) return;
    cw = (w - pad * 2 - gap * (cols - 1)) / cols;
    ch = (h - pad * 2 - S(30) - gap * (rows - 1)) / rows;
    if (cw < 8) cw = 8;
    if (ch < 8) ch = 8;

    bg = CreateSolidBrush(C_INSET);
    FillRect(dc, &rc, bg);
    DeleteObject(bg);

    for (int i = 0; i < nchips; i++) {
        int col = i % cols, row = i / cols;
        RECT cr = { rc.left + pad + col * (cw + gap), rc.top + pad + S(26) + row * (ch + gap), 0, 0 };
        float lr, lg, lb, rr, rg, rb;
        cr.right = cr.left + cw;
        cr.bottom = cr.top + ch;

        cm_apply_pixel(&identity, chips[i][0], chips[i][1], chips[i][2], &lr, &lg, &lb);
        cm_apply_pixel(&tuned, chips[i][0], chips[i][1], chips[i][2], &rr, &rg, &rb);

        {
            HRGN clip = CreateRectRgn(rc.left, rc.top, split_x, rc.bottom);
            HBRUSH br;
            SelectClipRgn(dc, clip);
            br = CreateSolidBrush(px_to_color(lr, lg, lb));
            FillRect(dc, &cr, br);
            DeleteObject(br);
            SelectClipRgn(dc, NULL);
            DeleteObject(clip);
        }
        {
            HRGN clip = CreateRectRgn(split_x, rc.top, rc.right, rc.bottom);
            HBRUSH br;
            SelectClipRgn(dc, clip);
            br = CreateSolidBrush(px_to_color(rr, rg, rb));
            FillRect(dc, &cr, br);
            DeleteObject(br);
            SelectClipRgn(dc, NULL);
            DeleteObject(clip);
        }
    }

    {
        RECT ll = { rc.left + S(14), rc.top + S(5), rc.left + S(320), rc.top + S(24) };
        RECT rl = { rc.right - S(430), rc.top + S(5), rc.right - S(14), rc.top + S(24) };
        const wchar_t *after;
        switch (e->status) {
        case PX_STATUS_DISABLED:    after = L"AFTER  ·  engine disabled — identity output"; break;
        case PX_STATUS_PASSTHROUGH: after = L"AFTER  ·  HDR pass-through — identity output"; break;
        case PX_STATUS_LIMITED:     after = L"AFTER  ·  LIMITED path — tone curves only"; break;
        case PX_STATUS_FAILED:      after = L"AFTER  ·  apply FAILED — identity output"; break;
        case PX_STATUS_APPLYING:    after = L"AFTER  ·  applying…"; break;
        default:                    after = L"AFTER  ·  cm_apply_pixel (live)"; break;
        }
        UiDrawText(dc, ll, L"BEFORE  ·  identity", g_ui.fSmall, C_SUB, DT_LEFT);
        UiDrawText(dc, rl, after, g_ui.fSmall, C_ACC, DT_RIGHT);
    }

    {
        HPEN pen_div = CreatePen(PS_SOLID, S(2), C_ACC);
        HGDIOBJ op = SelectObject(dc, pen_div);
        HBRUSH handle = CreateSolidBrush(C_ACC);
        int hy = (rc.top + rc.bottom) / 2;
        MoveToEx(dc, split_x, rc.top, NULL);
        LineTo(dc, split_x, rc.bottom);
        SelectObject(dc, op);
        DeleteObject(pen_div);
        op = SelectObject(dc, handle);
        SelectObject(dc, GetStockObject(NULL_PEN));
        Ellipse(dc, split_x - S(11), hy - S(11), split_x + S(11), hy + S(11));
        SelectObject(dc, op);
        DeleteObject(handle);
    }
    {
        HPEN pen_b = CreatePen(PS_SOLID, 1, C_LINE);
        HGDIOBJ op = SelectObject(dc, pen_b);
        SelectObject(dc, GetStockObject(NULL_BRUSH));
        RoundRect(dc, rc.left, rc.top, rc.right, rc.bottom, S(10), S(10));
        SelectObject(dc, op);
        DeleteObject(pen_b);
    }
}

/* Effective tone response: DWM matrix slope + the GPU ramp the engine calculated. */
static void draw_curve_preview(HDC dc, RECT rc)
{
    HBRUSH bg = CreateSolidBrush(C_INSET);
    HPEN pen_grid, pen_c;
    HGDIOBJ op;
    WORD ramp[3][256];
    MagColorEffect fx;
    float slope, offset;
    int w = rc.right - rc.left, h = rc.bottom - rc.top;
    const Look *lk = Eng_GetRequested();

    FillRect(dc, &rc, bg);
    DeleteObject(bg);

    pen_grid = CreatePen(PS_SOLID, 1, C_GRID);
    op = SelectObject(dc, pen_grid);
    for (int i = 1; i <= 3; i++) {
        int x = rc.left + (i * w) / 4;
        int y = rc.top + (i * h) / 4;
        MoveToEx(dc, x, rc.top, NULL); LineTo(dc, x, rc.bottom);
        MoveToEx(dc, rc.left, y, NULL); LineTo(dc, rc.right, y);
    }
    SelectObject(dc, op);
    DeleteObject(pen_grid);

    cm_calc_ramp(lk, ramp);
    cm_build_effect(lk, &fx);
    slope  = fx.transform[0][1] + fx.transform[1][1] + fx.transform[2][1];
    offset = fx.transform[4][1];

    pen_c = CreatePen(PS_SOLID, S(2), C_ACC);
    op = SelectObject(dc, pen_c);
    for (int i = 0; i < 256; i++) {
        int x = rc.left + (i * w) / 255;
        float lin = cm_clampf((i / 255.0f) * slope + offset, 0.0f, 1.0f);
        int idx = (int)(lin * 255.0f + 0.5f);
        float y_norm = ramp[1][clampi(idx, 0, 255)] / 65535.0f;
        int y = rc.bottom - (int)(y_norm * (h - 10)) - 5;
        if (i == 0) MoveToEx(dc, x, y, NULL);
        else LineTo(dc, x, y);
    }
    SelectObject(dc, op);
    DeleteObject(pen_c);

    {
        RECT tr = { rc.left + S(8), rc.top + S(5), rc.right - S(8), rc.top + S(23) };
        UiDrawText(dc, tr, L"Effective tone response  ·  colour matrix + GPU ramp", g_ui.fSmall, C_SUB, DT_LEFT);
    }
}

/* ---------------- sidebar (grouped navigation) ---------------- */
typedef struct NavItem { const wchar_t *group; const wchar_t *label; int id; } NavItem;

static const NavItem g_nav[] = {
    { L"",        L"HOME",            ID_SIDE_HOME },
    { L"DISPLAY", L"Global Color",    ID_SIDE_COLOR },
    { L"DISPLAY", L"Monitors",        ID_SIDE_MONITORS },
    { L"DISPLAY", L"Resolution",      ID_SIDE_RESOLUTION },
    { L"DISPLAY", L"Refresh Rate",    ID_SIDE_RATE },
    { L"GAMES",   L"Game Profiles",   ID_SIDE_GAMES },
    { L"GAMES",   L"Active Game",     ID_SIDE_ACTIVE_GAME },
    { L"GAMES",   L"Custom Games",    ID_SIDE_CUSTOM_GAMES },
    { L"TOOLS",   L"Crosshair",       ID_SIDE_CROSS },
    { L"TOOLS",   L"Test Patterns",   ID_SIDE_PATTERNS },
    { L"TOOLS",   L"Diagnostics",     ID_SIDE_DIAG },
    { L"REMOTE",  L"Phone Control",   ID_SIDE_PHONE },
    { L"",        L"Settings",        ID_SIDE_SETTINGS }
};
#define NAV_COUNT ((int)(sizeof g_nav / sizeof g_nav[0]))

static void build_sidebar(void)
{
    int y = 76;
    const wchar_t *last_group = L"\1";
    for (int i = 0; i < NAV_COUNT; i++) {
        if (g_nav[i].group[0] && wcscmp(g_nav[i].group, last_group) != 0) {
            if (wcscmp(last_group, L"\1") != 0) y += 10;
            UiW(WT_NAVHEAD, 0, 26, y, UI_SIDEBAR_W - 40, 16, g_nav[i].group);
            y += 20;
            last_group = g_nav[i].group;
        } else if (!g_nav[i].group[0]) {
            y += 8;
            last_group = L"";
        }
        {
            Widget *k = UiW(WT_SIDE, g_nav[i].id, 14, y, UI_SIDEBAR_W - 28, 32, g_nav[i].label);
            k->state = (g_nav[i].id == g_panel);
        }
        y += 36;
    }
}

static void build_topbar(void)
{
    UiW(WT_BTN, ID_CAP_MIN,   PX_WIN_W - 96, 12, 38, 30, L"—");
    UiW(WT_BTN, ID_CAP_CLOSE, PX_WIN_W - 50, 12, 38, 30, L"✕");
}

void Ui_RebuildPanel(void)
{
    g_nw = 0;
    build_topbar();
    build_sidebar();
    switch (g_panel) {
    case ID_SIDE_HOME:          UiHome_Build();        break;
    case ID_SIDE_COLOR:         UiColor_Build();       break;
    case ID_SIDE_MONITORS:      UiMonitors_Build();    break;
    case ID_SIDE_RESOLUTION:    UiResolution_Build();  break;
    case ID_SIDE_RATE:          UiRefresh_Build();     break;
    case ID_SIDE_GAMES:         UiGames_Build();       break;
    case ID_SIDE_ACTIVE_GAME:   UiActiveGame_Build();  break;
    case ID_SIDE_CUSTOM_GAMES:  UiCustomGames_Build(); break;
    case ID_SIDE_CROSS:         UiCrosshair_Build();   break;
    case ID_SIDE_PATTERNS:      UiPatterns_Build();    break;
    case ID_SIDE_DIAG:          UiDiagnostics_Build(); break;
    case ID_SIDE_PHONE:         UiPhone_Build();       break;
    case ID_SIDE_SETTINGS:      UiSettings_Build();    break;
    default:                    UiHome_Build();        break;
    }
}

/* ---------------- widget drawing ---------------- */
static void draw_widget(HDC dc, Widget *k)
{
    RECT rc = k->rc;
    int hov = (g_ui.hover_widget == k->id && k->id != 0);
    HBRUSH br_card = CreateSolidBrush(C_CARD);
    HBRUSH br_card2 = CreateSolidBrush(C_CARD2);
    HBRUSH br_card_act = CreateSolidBrush(C_CARD_ACT);
    HBRUSH br_acc = CreateSolidBrush(C_ACC);
    HPEN pen_line = CreatePen(PS_SOLID, 1, C_LINE);
    HPEN pen_acc = CreatePen(PS_SOLID, 1, C_ACC);
    HPEN pen_non = CreatePen(PS_SOLID, 1, C_CARD);

    switch (k->type) {
    case WT_HEAD:
        UiDrawText(dc, rc, k->text, g_ui.fH1, C_TXT, DT_LEFT);
        break;
    case WT_LABEL:
        UiDrawText(dc, rc, k->text, g_ui.fBody, C_SUB, DT_LEFT);
        break;
    case WT_NAVHEAD:
        UiDrawText(dc, rc, k->text, g_ui.fSmall, C_DIM, DT_LEFT);
        break;
    case WT_DIV: {
        int y = (rc.top + rc.bottom) / 2;
        RECT ln = { rc.left + S(200), y, rc.right, y + 1 };
        FillRect(dc, &ln, br_card2);
        UiDrawText(dc, rc, k->text, g_ui.fSmall, C_DIM, DT_LEFT);
        break;
    }
    case WT_PRIMARY:
    case WT_GHOST:
    case WT_ACCENT:
    case WT_BTN:
    case WT_QUICK_SAT: {
        HBRUSH fill;
        COLORREF fc = C_TXT;
        int armed = (k->flags == 1);
        if (k->type == WT_PRIMARY || armed) { fill = br_acc; fc = C_DARK; }
        else if (hov) { fill = br_card2; fc = C_TXT; }
        else fill = br_card;
        UiDrawRRect(dc, rc, S(8), fill, (hov || k->type == WT_PRIMARY || armed) ? pen_acc : pen_line);
        if (k->type == WT_QUICK_SAT && !hov && !armed) fc = C_SUB;
        UiDrawText(dc, rc, k->text, (k->type == WT_QUICK_SAT ? g_ui.fSmall : g_ui.fH2), fc, DT_CENTER);
        break;
    }
    case WT_SIDE: {
        if (k->state) {
            UiDrawRRect(dc, rc, S(8), br_card2, pen_non);
            {
                RECT bar = { rc.left + S(4), rc.top + S(7), rc.left + S(8), rc.bottom - S(7) };
                UiDrawRRect(dc, bar, S(2), br_acc, pen_non);
            }
        } else if (hov) {
            UiDrawRRect(dc, rc, S(8), br_card, pen_non);
        }
        {
            RECT rl = rc;
            rl.left += S(18);
            UiDrawText(dc, rl, k->text, g_ui.fBody, k->state ? C_ACC : (hov ? C_TXT : C_SUB), DT_LEFT);
        }
        break;
    }
    case WT_CARD: {
        UiDrawRRect(dc, rc, S(10), br_card, pen_line);
        {
            RECT r_head = { rc.left + S(16), rc.top + S(8), rc.right - S(16), rc.top + S(24) };
            RECT r_val  = { rc.left + S(16), rc.top + S(26), rc.right - S(16), rc.top + S(52) };
            RECT r_sub  = { rc.left + S(16), rc.top + S(52), rc.right - S(16), rc.bottom - S(6) };
            if (!k->val[0]) { r_val.top = r_val.bottom; r_sub.top = rc.top + S(28); }
            UiDrawText(dc, r_head, k->text, g_ui.fSmall, C_DIM, DT_LEFT);
            UiDrawText(dc, r_val, k->val, g_ui.fH2, C_TXT, DT_LEFT);
            UiDrawText(dc, r_sub, k->sub, g_ui.fSmall, C_SUB, DT_LEFT);
        }
        break;
    }
    case WT_KV: {
        RECT rk = { rc.left, rc.top, rc.left + (rc.right - rc.left) * 52 / 100, rc.bottom };
        RECT rv = { rk.right, rc.top, rc.right, rc.bottom };
        UiDrawText(dc, rk, k->text, g_ui.fSmall, C_SUB, DT_LEFT);
        UiDrawText(dc, rv, k->val, g_ui.fSmall, C_TXT, DT_LEFT);
        break;
    }
    case WT_CHIP: {
        COLORREF tone = UiToneColor(k->flags);
        HBRUSH fill = CreateSolidBrush(RGB(GetRValue(tone) / 6, GetGValue(tone) / 6, GetBValue(tone) / 6));
        HPEN pen = CreatePen(PS_SOLID, 1, RGB(GetRValue(tone) / 2, GetGValue(tone) / 2, GetBValue(tone) / 2));
        UiDrawRRect(dc, rc, (rc.bottom - rc.top) / 2, fill, pen);
        DeleteObject(fill);
        DeleteObject(pen);
        UiDrawText(dc, rc, k->text, g_ui.fSmall, tone, DT_CENTER);
        break;
    }
    case WT_GAME_CARD:
    case WT_LOOK_CARD:
    case WT_MONITOR_CARD:
    case WT_ROW: {
        HBRUSH fill = k->state ? br_card_act : (hov ? br_card2 : br_card);
        UiDrawRRect(dc, rc, S(8), fill, k->state ? pen_acc : pen_line);
        {
            RECT r_title = { rc.left + S(14), rc.top + S(4), rc.right - S(14), rc.top + S(24) };
            RECT r_sub   = { rc.left + S(14), rc.top + S(24), rc.right - S(14), rc.bottom - S(3) };
            RECT r_val   = { rc.right - S(250), rc.top + S(4), rc.right - S(14), rc.top + S(24) };
            UiDrawText(dc, r_title, k->text, g_ui.fH2, k->state ? C_ACC : C_TXT, DT_LEFT);
            UiDrawText(dc, r_sub, k->sub, g_ui.fSmall, C_SUB, DT_LEFT);
            UiDrawText(dc, r_val, k->val, g_ui.fSmall, C_ACC2, DT_RIGHT);
        }
        break;
    }
    case WT_SLIDER: {
        float cur = UiSl_Get(k->id);
        float f;
        int ins = S(4);
        int tx1 = rc.left + ins, tx2 = rc.right - ins;
        int ty = rc.top + S(46);
        int fx, th;
        {
            RECT rl = rc;   rl.bottom = rl.top + S(22);
            RECT rv = rc;   rv.bottom = rv.top + S(22); rv.left = rv.right - S(130);
            RECT rr = rc;   rr.top += S(20); rr.bottom = rr.top + S(18); rr.left += S(2);
            wchar_t range[48];
            UiDrawText(dc, rl, k->text, g_ui.fBody, C_TXT, DT_LEFT);
            UiDrawText(dc, rv, k->val, g_ui.fMono, (hov || g_ui.active_widget == k->id) ? C_ACC : C_SUB, DT_RIGHT);
            if (k->vmax > k->vmin) {
                if (k->id == ID_SL_GAMMA)
                    _snwprintf(range, 48, L"%.2f – %.2f", (double)k->vmin, (double)k->vmax);
                else
                    _snwprintf(range, 48, L"%d – %d", (int)k->vmin, (int)k->vmax);
                UiDrawText(dc, rr, range, g_ui.fSmall, C_DIM, DT_LEFT);
            }
        }
        {
            RECT tr = { tx1, ty - S(3), tx2, ty + S(3) };
            UiDrawRRect(dc, tr, S(3), br_card2, pen_non);
        }
        f = clampf((cur - k->vmin) / (k->vmax - k->vmin), 0.0f, 1.0f);
        fx = tx1 + (int)((tx2 - tx1) * f);
        if (fx > tx1) {
            RECT fr = { tx1, ty - S(3), fx, ty + S(3) };
            UiDrawRRect(dc, fr, S(3), br_acc, pen_non);
        }
        th = (hov || g_ui.active_widget == k->id) ? S(8) : S(6);
        {
            RECT thr = { fx - th, ty - th, fx + th, ty + th };
            HBRUSH tb = CreateSolidBrush(RGB(255, 255, 255));
            UiDrawRRect(dc, thr, th, tb, pen_non);
            DeleteObject(tb);
        }
        break;
    }
    case WT_TOGGLE: {
        RECT rl = rc; rl.right -= S(56);
        RECT pr = { rc.right - S(48), rc.top + S(4), rc.right, rc.bottom - S(4) };
        HBRUSH pb = CreateSolidBrush(k->state ? C_ACC : C_CARD2);
        int kr = (pr.bottom - pr.top) / 2 - S(3);
        int ky = (pr.top + pr.bottom) / 2;
        int kx = k->state ? pr.right - kr - S(4) : pr.left + kr + S(4);
        RECT kb = { kx - kr, ky - kr, kx + kr, ky + kr };
        HBRUSH kb_br = CreateSolidBrush(k->state ? C_DARK : C_SUB);
        UiDrawText(dc, rl, k->text, g_ui.fBody, C_TXT, DT_LEFT);
        UiDrawRRect(dc, pr, (pr.bottom - pr.top) / 2, pb, pen_non);
        UiDrawRRect(dc, kb, kr, kb_br, pen_non);
        DeleteObject(pb);
        DeleteObject(kb_br);
        break;
    }
    case WT_SHAPE: {
        int shape = (int)_wtoi(k->val);
        UiDrawRRect(dc, rc, S(8), hov ? br_card2 : br_card, k->state ? pen_acc : pen_line);
        {
            RECT rl = rc; rl.top = rl.bottom - S(24);
            RECT ic = { (rc.left + rc.right) / 2 - S(9), rc.top + S(8), (rc.left + rc.right) / 2 + S(9), rc.top + S(26) };
            HPEN ip = CreatePen(PS_SOLID, S(2), k->state ? C_ACC : C_SUB);
            HGDIOBJ op;
            SelectObject(dc, GetStockObject(NULL_BRUSH));
            op = SelectObject(dc, ip);
            switch (shape) {
            case XH_DOT: {
                HBRUSH b = CreateSolidBrush(k->state ? C_ACC : C_SUB);
                HGDIOBJ ob = SelectObject(dc, b);
                Ellipse(dc, ic.left + S(6), ic.top + S(6), ic.right - S(6), ic.bottom - S(6));
                SelectObject(dc, ob);
                DeleteObject(b);
                break;
            }
            case XH_CIRCLE: Ellipse(dc, ic.left, ic.top, ic.right, ic.bottom); break;
            case XH_SQUARE: Rectangle(dc, ic.left, ic.top, ic.right, ic.bottom); break;
            case XH_PLUS:
                MoveToEx(dc, (ic.left + ic.right) / 2, ic.top, NULL);
                LineTo(dc, (ic.left + ic.right) / 2, ic.bottom);
                /* fall through into the cross arms */
            default:
                MoveToEx(dc, (ic.left + ic.right) / 2, ic.top, NULL);
                LineTo(dc, (ic.left + ic.right) / 2, ic.bottom);
                MoveToEx(dc, ic.left, (ic.top + ic.bottom) / 2, NULL);
                LineTo(dc, ic.right, (ic.top + ic.bottom) / 2);
                break;
            }
            SelectObject(dc, op);
            DeleteObject(ip);
            UiDrawText(dc, rl, k->text, g_ui.fSmall, k->state ? C_ACC : C_SUB, DT_CENTER);
        }
        break;
    }
    case WT_SWATCH: {
        COLORREF col = (COLORREF)_wtoi(k->val);
        HBRUSH b = CreateSolidBrush(col);
        RECT inner = { rc.left + S(3), rc.top + S(3), rc.right - S(3), rc.bottom - S(3) };
        UiDrawRRect(dc, inner, S(6), b, (k->state || hov) ? pen_acc : pen_line);
        DeleteObject(b);
        break;
    }
    case WT_SPLIT_PREVIEW:
        UiDrawSplitPreview(dc, rc);
        break;
    case WT_CURVE_PREVIEW:
        draw_curve_preview(dc, rc);
        break;
    default:
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

/* ---------------- status bar ---------------- */
static void rows_as_wide(const char *ascii, wchar_t *buf, size_t cap)
{
    size_t i = 0;
    if (!buf || !cap) return;
    MultiByteToWideChar(CP_UTF8, 0, ascii ? ascii : "", -1, buf, (int)cap);
    buf[cap - 1] = 0;
    for (; buf[i]; i++) if ((unsigned char)buf[i] > 126) buf[i] = L'?';
}

static void draw_status_bar(HDC dc, const RECT *win)
{
    const PxEffectiveState *e = UiStatus();
    const PxGameDisplayState *gs = Prof_GameState();
    Profile *ap = Prof_Get(Prof_ActiveIndex());
    ModeInfo mode;
    MonitorInfo *mi;
    int y = PX_WIN_H - UI_STATUS_H;
    RECT bar = { 0, S(y), win->right, win->bottom };
    RECT line = { 0, S(y), win->right, S(y) + 1 };
    HBRUSH bb = CreateSolidBrush(C_SIDE);
    HBRUSH lb = CreateSolidBrush(C_LINE);
    wchar_t wreason[140];
    int colw = (win->right - S(UI_SIDEBAR_W) - S(24)) / 5;
    int x0 = S(UI_SIDEBAR_W) + S(16);
    wchar_t buf[200];

    FillRect(dc, &bar, bb);
    FillRect(dc, &line, lb);
    DeleteObject(bb);
    DeleteObject(lb);

    Modes_Current(&mode);
    mi = Modes_GetMonitor(Modes_CurrentMonitorIndex());
    rows_as_wide(e->reason, wreason, 140);

    /* COLOR ENGINE */
    UiDrawText(dc, (RECT){ x0, S(y + 8), x0 + colw, S(y + 26) }, L"COLOR ENGINE", g_ui.fSmall, C_DIM, DT_LEFT);
    {
        wchar_t st[64];
        rows_as_wide(PxStatus_Name(e->status), st, 64);
        _snwprintf(buf, 200, L"%s%s", st, e->diverged ? L"  ·  re-applying" : L"");
    }
    UiDrawText(dc, (RECT){ x0, S(y + 26), x0 + colw - S(12), S(y + 50) }, buf, g_ui.fH2,
               UiToneColor(PxStatus_Tone(e->status)), DT_LEFT);
    UiDrawText(dc, (RECT){ x0, S(y + 52), x0 + colw - S(12), S(y + 74) }, wreason, g_ui.fSmall, C_SUB, DT_LEFT);

    /* CURRENT APPLICATION / GAME */
    UiDrawText(dc, (RECT){ x0 + colw, S(y + 8), x0 + colw * 2, S(y + 26) }, L"CURRENT GAME", g_ui.fSmall, C_DIM, DT_LEFT);
    if (gs->detected) {
        wchar_t sub[128];
        rows_as_wide(px_gameout_name(gs->game_output), sub, 128);
        UiDrawText(dc, (RECT){ x0 + colw, S(y + 26), x0 + colw * 2 - S(12), S(y + 50) }, gs->exe, g_ui.fH2, C_TXT, DT_LEFT);
        UiDrawText(dc, (RECT){ x0 + colw, S(y + 52), x0 + colw * 2 - S(12), S(y + 74) }, sub, g_ui.fSmall, C_SUB, DT_LEFT);
    } else {
        wchar_t fgw[128];
        UiWin(fgw, 128, Prof_CurrentForeground());
        UiDrawText(dc, (RECT){ x0 + colw, S(y + 26), x0 + colw * 2 - S(12), S(y + 50) }, L"Desktop", g_ui.fH2, C_TXT, DT_LEFT);
        UiDrawText(dc, (RECT){ x0 + colw, S(y + 52), x0 + colw * 2 - S(12), S(y + 74) },
                   fgw[0] ? fgw : L"no game in the foreground", g_ui.fSmall, C_SUB, DT_LEFT);
    }

    /* CURRENT PROFILE */
    UiDrawText(dc, (RECT){ x0 + colw * 2, S(y + 8), x0 + colw * 3, S(y + 26) }, L"CURRENT PROFILE", g_ui.fSmall, C_DIM, DT_LEFT);
    if (ap) {
        wchar_t sub[128];
        _snwprintf(sub, 128, L"%s  ·  %s", ap->sub[ap->active_sub].name,
                   ap->enabled ? (ap->auto_apply ? L"auto-apply" : L"manual") : L"disabled — skipped");
        UiDrawText(dc, (RECT){ x0 + colw * 2, S(y + 26), x0 + colw * 3 - S(12), S(y + 50) }, ap->name, g_ui.fH2, C_TXT, DT_LEFT);
        UiDrawText(dc, (RECT){ x0 + colw * 2, S(y + 52), x0 + colw * 3 - S(12), S(y + 74) }, sub, g_ui.fSmall, C_SUB, DT_LEFT);
    } else {
        UiDrawText(dc, (RECT){ x0 + colw * 2, S(y + 26), x0 + colw * 3 - S(12), S(y + 50) }, L"Global look", g_ui.fH2, C_TXT, DT_LEFT);
        UiDrawText(dc, (RECT){ x0 + colw * 2, S(y + 52), x0 + colw * 3 - S(12), S(y + 74) },
                   Prof_Detect() ? L"automatic detection on" : L"detection off", g_ui.fSmall, C_SUB, DT_LEFT);
    }

    /* DISPLAY */
    UiDrawText(dc, (RECT){ x0 + colw * 3, S(y + 8), x0 + colw * 4, S(y + 26) }, L"DISPLAY", g_ui.fSmall, C_DIM, DT_LEFT);
    _snwprintf(buf, 200, L"%d × %d @ %d Hz", mode.w, mode.h, mode.hz);
    UiDrawText(dc, (RECT){ x0 + colw * 3, S(y + 26), x0 + colw * 4 - S(12), S(y + 50) }, buf, g_ui.fH2, C_TXT, DT_LEFT);
    {
        wchar_t sub[128];
        wchar_t cs[64];
        int hdr = mi ? PxHdr_StateOf(mi) : PX_HDR_DISABLED;
        char csb[64];
        if (mi) px_cs_name(mi->color_space_raw, csb, sizeof csb);
        else csb[0] = 0;
        rows_as_wide(csb, cs, 64);
        _snwprintf(sub, 128, L"%s  ·  %s", PxHdr_Name(hdr), cs);
        UiDrawText(dc, (RECT){ x0 + colw * 3, S(y + 52), x0 + colw * 4 - S(12), S(y + 74) }, sub, g_ui.fSmall,
                   PxHdr_Tone(hdr) >= 1 ? C_WARN : C_SUB, DT_LEFT);
    }

    /* VERSION */
    UiDrawText(dc, (RECT){ x0 + colw * 4, S(y + 8), win->right - S(16), S(y + 26) }, L"PLEXUSX", g_ui.fSmall, C_DIM, DT_LEFT);
    _snwprintf(buf, 200, L"v%s", PX_VERSION);
    UiDrawText(dc, (RECT){ x0 + colw * 4, S(y + 26), win->right - S(16), S(y + 50) }, buf, g_ui.fH2, C_ACC, DT_LEFT);
    _snwprintf(buf, 200, L"%s  ·  mag %s  ·  %d profiles", PX_BUILD_DATE,
               Eng_Available() ? L"available" : L"unavailable", Prof_Count());
    UiDrawText(dc, (RECT){ x0 + colw * 4, S(y + 52), win->right - S(16), S(y + 74) }, buf, g_ui.fSmall, C_SUB, DT_LEFT);

    /* pending display change → confirmation bar above the status strip */
    if (Modes_PendingChange()) {
        RECT wr = { S(UI_SIDEBAR_W) + S(16), S(y - 38), win->right - S(16), S(y - 8) };
        HBRUSH wb = CreateSolidBrush(RGB(58, 46, 12));
        HPEN wp = CreatePen(PS_SOLID, 1, C_WARN);
        char label[64];
        wchar_t txt[200], wlabel[80];
        UiDrawRRect(dc, wr, S(8), wb, wp);
        DeleteObject(wb);
        DeleteObject(wp);
        Modes_PendingMode(label, sizeof label);
        rows_as_wide(label, wlabel, 80);
        _snwprintf(txt, 200, L"Display mode %s applied — keep it?  Auto-revert in %d s", wlabel, Modes_PendingSecondsLeft());
        {
            RECT t = wr;
            t.right -= S(320);
            UiDrawText(dc, t, txt, g_ui.fBody, C_WARN, DT_LEFT);
        }
        {
            RECT keep = { wr.right - S(310), wr.top + S(6), wr.right - S(170), wr.bottom - S(6) };
            RECT rev  = { wr.right - S(160), wr.top + S(6), wr.right - S(20), wr.bottom - S(6) };
            HBRUSH kb = CreateSolidBrush(C_ACC);
            HBRUSH rb = CreateSolidBrush(C_CARD2);
            HPEN kp = CreatePen(PS_SOLID, 1, C_ACC);
            HPEN rp = CreatePen(PS_SOLID, 1, C_LINE);
            UiDrawRRect(dc, keep, S(6), kb, kp);
            UiDrawText(dc, keep, L"KEEP MODE", g_ui.fSmall, C_DARK, DT_CENTER);
            UiDrawRRect(dc, rev, S(6), rb, rp);
            UiDrawText(dc, rev, L"REVERT NOW", g_ui.fSmall, C_TXT, DT_CENTER);
            DeleteObject(kb); DeleteObject(rb); DeleteObject(kp); DeleteObject(rp);
        }
    }
}

/* click targets of the pending-change bar (kept in one place) */
static int pending_hit(int x, int y, int *is_revert)
{
    int sy;
    RECT keep, rev;
    if (!Modes_PendingChange()) return 0;
    sy = S(PX_WIN_H - UI_STATUS_H);
    keep = (RECT){ S(PX_WIN_W) - S(310), S(sy - 38 + 6), S(PX_WIN_W) - S(170), S(sy - 8 - 6) };
    rev  = (RECT){ S(PX_WIN_W) - S(160), S(sy - 38 + 6), S(PX_WIN_W) - S(20), S(sy - 8 - 6) };
    if (x >= keep.left && x <= keep.right && y >= keep.top && y <= keep.bottom) { *is_revert = 0; return 1; }
    if (x >= rev.left && x <= rev.right && y >= rev.top && y <= rev.bottom) { *is_revert = 1; return 1; }
    return 0;
}

void Ui_Paint(HDC hdc, const RECT *rc)
{
    HBRUSH bg;
    if (!g_ui.ready) {
        bg = CreateSolidBrush(g_ui.bg_color);
        FillRect(hdc, rc, bg);
        DeleteObject(bg);
        return;
    }

    bg = CreateSolidBrush(g_ui.bg_color);
    FillRect(hdc, rc, bg);
    DeleteObject(bg);

    if (g_ui.bg_mode == 2 && g_ui.bg_bmp) {
        HDC mdc = CreateCompatibleDC(hdc);
        HGDIOBJ ob = SelectObject(mdc, g_ui.bg_bmp);
        BITMAP bm;
        GetObject(g_ui.bg_bmp, sizeof bm, &bm);
        StretchBlt(hdc, S(UI_SIDEBAR_W), S(UI_TOPBAR_H),
                   rc->right - S(UI_SIDEBAR_W), rc->bottom - S(UI_TOPBAR_H),
                   mdc, 0, 0, bm.bmWidth, bm.bmHeight, SRCCOPY);
        SelectObject(mdc, ob);
        DeleteDC(mdc);
    } else if (g_ui.bg_mode == 1) {
        HPEN mesh = CreatePen(PS_SOLID, 1, C_GRID);
        HGDIOBJ op = SelectObject(hdc, mesh);
        int step = S(28);
        for (int x = S(UI_SIDEBAR_W); x < rc->right; x += step) {
            MoveToEx(hdc, x, S(UI_TOPBAR_H), NULL);
            LineTo(hdc, x, S(PX_WIN_H - UI_STATUS_H));
        }
        for (int y = S(UI_TOPBAR_H); y < S(PX_WIN_H - UI_STATUS_H); y += step) {
            MoveToEx(hdc, S(UI_SIDEBAR_W), y, NULL);
            LineTo(hdc, rc->right, y);
        }
        SelectObject(hdc, op);
        DeleteObject(mesh);
    }

    {
        RECT sr = { 0, 0, S(UI_SIDEBAR_W), rc->bottom };
        RECT sepline = { S(UI_SIDEBAR_W), 0, S(UI_SIDEBAR_W) + 1, rc->bottom };
        RECT topline = { 0, S(UI_TOPBAR_H), rc->right, S(UI_TOPBAR_H) + 1 };
        HBRUSH side_br = CreateSolidBrush(C_SIDE);
        HBRUSH lb = CreateSolidBrush(C_LINE);
        FillRect(hdc, &sr, side_br);
        FillRect(hdc, &sepline, lb);
        FillRect(hdc, &topline, lb);
        DeleteObject(side_br);
        DeleteObject(lb);
    }

    /* brand */
    {
        RECT box = { S(18), S(14), S(48), S(44) };
        HBRUSH acc = CreateSolidBrush(C_ACC);
        HPEN xp;
        HGDIOBJ op;
        UiDrawRRect(hdc, box, S(8), acc, NULL);
        DeleteObject(acc);
        xp = CreatePen(PS_SOLID, S(3), C_DARK);
        op = SelectObject(hdc, xp);
        MoveToEx(hdc, S(25), S(21), NULL); LineTo(hdc, S(41), S(37));
        MoveToEx(hdc, S(41), S(21), NULL); LineTo(hdc, S(25), S(37));
        SelectObject(hdc, op);
        DeleteObject(xp);
        UiDrawText(hdc, (RECT){ S(58), S(10), S(205), S(32) }, L"PLEXUSX", g_ui.fLogo, C_TXT, DT_LEFT);
        UiDrawText(hdc, (RECT){ S(58), S(32), S(214), S(48) }, L"DISPLAY CONTROL CENTER", g_ui.fSmall, C_SUB, DT_LEFT);
    }

    for (int i = 0; i < g_nw; i++) draw_widget(hdc, &g_w[i]);

    /* chips are painted directly (they carry live state, not widget state) */
    {
        const PxEffectiveState *e = UiStatus();
        const PxGameDisplayState *gs = Prof_GameState();
        wchar_t st[64], buf[128];
        int cx = S(PX_WIN_W) - S(452);
        rows_as_wide(PxStatus_Name(e->status), st, 64);
        _snwprintf(buf, 128, L"COLOR ENGINE · %s", st);
        {
            RECT r = { cx, S(14), cx + S(214), S(38) };
            COLORREF tone = UiToneColor(PxStatus_Tone(e->status));
            HBRUSH fill = CreateSolidBrush(RGB(GetRValue(tone) / 6, GetGValue(tone) / 6, GetBValue(tone) / 6));
            HPEN pen = CreatePen(PS_SOLID, 1, RGB(GetRValue(tone) / 2, GetGValue(tone) / 2, GetBValue(tone) / 2));
            UiDrawRRect(hdc, r, S(12), fill, pen);
            UiDrawText(hdc, r, buf, g_ui.fSmall, tone, DT_CENTER);
            DeleteObject(fill);
            DeleteObject(pen);
        }
        cx += S(224);
        _snwprintf(buf, 128, L"GAME · %s", gs->detected ? gs->exe : L"desktop");
        {
            RECT r = { cx, S(14), cx + S(214), S(38) };
            COLORREF tone = gs->detected ? (gs->game_output == PX_GAMEOUT_ACTIVE ? C_OK :
                                            gs->game_output == PX_GAMEOUT_LIMITED ? C_WARN : C_SUB) : C_SUB;
            HBRUSH fill = CreateSolidBrush(RGB(GetRValue(tone) / 6, GetGValue(tone) / 6, GetBValue(tone) / 6));
            HPEN pen = CreatePen(PS_SOLID, 1, RGB(GetRValue(tone) / 2, GetGValue(tone) / 2, GetBValue(tone) / 2));
            UiDrawRRect(hdc, r, S(12), fill, pen);
            UiDrawText(hdc, r, buf, g_ui.fSmall, tone, DT_CENTER);
            DeleteObject(fill);
            DeleteObject(pen);
        }
    }

    /* notification toast */
    if (g_ui.toast[0] && GetTickCount() - g_ui.toast_time < 3500) {
        RECT tw = { S(PX_WIN_W) - S(660), S(UI_TOPBAR_H + 8), S(PX_WIN_W) - S(20), S(UI_TOPBAR_H + 46) };
        HBRUSH cb = CreateSolidBrush(C_CARD2);
        HPEN cp = CreatePen(PS_SOLID, 1, C_ACC);
        wchar_t t2[200];
        RECT td = tw;
        td.left += S(14);
        UiDrawRRect(hdc, tw, S(8), cb, cp);
        _snwprintf(t2, 200, L"●  %s", g_ui.toast);
        UiDrawText(hdc, td, t2, g_ui.fSmall, C_ACC, DT_LEFT);
        DeleteObject(cb);
        DeleteObject(cp);
    }

    draw_status_bar(hdc, rc);
}

/* ---------------- settings glue (defined below; used by the toggles) -------- */
static int  cfg_get(const char *sec, const char *key, int dflt);
static void cfg_set(const char *sec, const char *key, int val);

/* ---------------- input ---------------- */
int Ui_MouseDown(int x, int y)
{
    int revert = 0;
    if (!g_ui.ready) return 0;

    if (pending_hit(x, y, &revert)) {
        if (revert) {
            if (Modes_RollbackPending()) Ui_Notify(L"Previous display mode restored");
        } else {
            if (Modes_ConfirmPending()) Ui_Notify(L"Display mode kept");
        }
        Ui_RebuildPanel();
        if (g_ui.hwnd) InvalidateRect(g_ui.hwnd, NULL, FALSE);
        return 1;
    }

    for (int i = 0; i < g_nw; i++) {
        if (g_w[i].type != WT_SPLIT_PREVIEW) continue;
        {
            RECT rc = g_w[i].rc;
            if (x >= rc.left && x <= rc.right && y >= rc.top && y <= rc.bottom) {
                g_ui.drag_split = 1;
                g_ui.split_pos = clampf((float)(x - rc.left) / (float)(rc.right - rc.left), 0.05f, 0.95f);
                return 1;
            }
        }
    }

    for (int i = 0; i < g_nw; i++) {
        Widget *k = &g_w[i];
        if (x < k->rc.left || x > k->rc.right || y < k->rc.top || y > k->rc.bottom) continue;
        g_ui.active_widget = k->id;

        if (k->type == WT_SIDE) {
            Ui_Go(k->id);
            return 1;
        }
        if (k->type == WT_SLIDER) {
            int ins = S(4);
            int tx1 = k->rc.left + ins, tx2 = k->rc.right - ins;
            float f = clampf((float)(x - tx1) / (float)(tx2 - tx1), 0.0f, 1.0f);
            UiSl_Set(k->id, k->vmin + f * (k->vmax - k->vmin));
            UiSl_Commit();
            return 1;
        }
        if (k->type == WT_QUICK_SAT) {
            float v = 100.0f;
            if (k->id == ID_B_SAT_150) v = 150.0f;
            else if (k->id == ID_B_SAT_200) v = 200.0f;
            else if (k->id == ID_B_SAT_250) v = 250.0f;
            else if (k->id == ID_B_SAT_300) v = 300.0f;
            UiSl_Set(ID_SL_SAT, v);
            UiSl_Commit();
            return 1;
        }
        if (k->type == WT_TOGGLE) {
            switch (k->id) {
            case ID_T_LOOK_ENABLE: {
                Look l = *Eng_GetRequested();
                l.enabled = !l.enabled;
                Eng_SetLook(&l);
                break;
            }
            case ID_T_DETECT:       Prof_SetDetect(!Prof_Detect()); break;
            case ID_T_AUTO_RESTORE: Prof_SetAutoRestore(!Prof_GetAutoRestore()); break;
            case ID_T_XH:           Xh_Toggle(); g_ui.xh.on = Xh_IsActive(); break;
            case ID_T_XH_OUTLINE:   g_ui.xh.outline = !g_ui.xh.outline; Xh_Update(&g_ui.xh); break;
            case ID_T_XH_DOT:       g_ui.xh.center_dot = !g_ui.xh.center_dot; Xh_Update(&g_ui.xh); break;
            case ID_T_PHONE:
                if (Phone_IsRunning()) Phone_Stop(); else Phone_Start();
                break;
            case ID_T_REDUCE_MOTION:
                g_ui.reduce_motion = !g_ui.reduce_motion;
                if (g_ui.reduce_motion) g_ui.anim_level = 0;
                cfg_set("ui", "reduce_motion", g_ui.reduce_motion);
                Main_ApplyChrome();
                break;
            case ID_T_GLASS:
                g_ui.glass = !g_ui.glass;
                cfg_set("ui", "glass", g_ui.glass);
                Main_ApplyChrome();
                break;
            case ID_T_STARTWIN:
                g_ui.startup = !g_ui.startup;
                Main_SetStartup(g_ui.startup);
                break;
            default:
                break;
            }
            Ui_RebuildPanel();
            if (g_ui.hwnd) InvalidateRect(g_ui.hwnd, NULL, FALSE);
            Main_ApplyAll();
            return 1;
        }
        if (k->type == WT_SWATCH) {
            COLORREF col = (COLORREF)_wtoi(k->val);
            if (k->id < ID_SWATCH_O_BASE) g_ui.xh.color = col;
            else g_ui.xh.ocolor = col;
            Xh_Update(&g_ui.xh);
            Ui_RebuildPanel();
            if (g_ui.hwnd) InvalidateRect(g_ui.hwnd, NULL, FALSE);
            return 1;
        }
        if (k->type == WT_SHAPE) {
            g_ui.xh.shape = k->id - ID_XH_SHAPE_BASE;
            Xh_Update(&g_ui.xh);
            Ui_RebuildPanel();
            if (g_ui.hwnd) InvalidateRect(g_ui.hwnd, NULL, FALSE);
            return 1;
        }
        if (Ui_Exec(k->id)) {
            Ui_RebuildPanel();
            if (g_ui.hwnd) InvalidateRect(g_ui.hwnd, NULL, FALSE);
            return 1;
        }
        return 1;
    }
    return 0;
}

int Ui_MouseMove(int x, int y, int dragging)
{
    int old_hover;
    if (!g_ui.ready) return 0;

    if (dragging && g_ui.drag_split) {
        for (int i = 0; i < g_nw; i++) {
            if (g_w[i].type != WT_SPLIT_PREVIEW) continue;
            {
                RECT rc = g_w[i].rc;
                g_ui.split_pos = clampf((float)(x - rc.left) / (float)(rc.right - rc.left), 0.05f, 0.95f);
                return -1;
            }
        }
    }

    if (dragging && g_ui.active_widget) {
        for (int i = 0; i < g_nw; i++) {
            Widget *k = &g_w[i];
            if (k->id != g_ui.active_widget || k->type != WT_SLIDER) continue;
            {
                int ins = S(4);
                int tx1 = k->rc.left + ins, tx2 = k->rc.right - ins;
                float f = clampf((float)(x - tx1) / (float)(tx2 - tx1), 0.0f, 1.0f);
                UiSl_Set(k->id, k->vmin + f * (k->vmax - k->vmin));
                UiSl_Commit();
                return -1;
            }
        }
    }

    old_hover = g_ui.hover_widget;
    g_ui.hover_widget = 0;
    for (int i = 0; i < g_nw; i++) {
        if (x >= g_w[i].rc.left && x <= g_w[i].rc.right && y >= g_w[i].rc.top && y <= g_w[i].rc.bottom) {
            g_ui.hover_widget = g_w[i].id;
            break;
        }
    }
    return old_hover != g_ui.hover_widget ? -1 : 0;
}

void Ui_MouseUp(int x, int y)
{
    (void)x; (void)y;
    g_ui.drag_split = 0;
    g_ui.active_widget = 0;
    if (g_ui.hwnd) InvalidateRect(g_ui.hwnd, NULL, FALSE);
}

int Ui_Hover(int x, int y)
{
    if (!g_ui.ready) return 0;
    for (int i = 0; i < g_nw; i++) {
        if (x >= g_w[i].rc.left && x <= g_w[i].rc.right && y >= g_w[i].rc.top && y <= g_w[i].rc.bottom)
            return g_w[i].id;
    }
    return 0;
}

int Ui_Wheel(int x, int y, int delta)
{
    int ctrl, shift;
    if (!g_ui.ready || delta == 0) return 0;
    ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
    shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
    for (int i = 0; i < g_nw; i++) {
        Widget *k = &g_w[i];
        float span, step, cur, dir, next;
        if (k->type != WT_SLIDER) continue;
        if (x < k->rc.left || x > k->rc.right || y < k->rc.top || y > k->rc.bottom) continue;
        span = k->vmax - k->vmin;
        step = span / 100.0f;
        if (shift) step = span / 20.0f;
        if (ctrl) step = span / 500.0f;
        dir = delta > 0 ? 1.0f : -1.0f;
        cur = UiSl_Get(k->id);
        next = clampf(cur + dir * step, k->vmin, k->vmax);
        if (k->id != ID_SL_GAMMA && k->id != ID_SL_TEMP)
            next = (float)(int)(next + (dir > 0 ? 0.5f : -0.5f));
        UiSl_Set(k->id, next);
        g_ui.active_widget = k->id;
        UiSl_Commit();
        return 1;
    }
    return 0;
}

int Ui_Key(int vk, int ctrl, int shift)
{
    int id;
    Widget *k = NULL;
    (void)ctrl; (void)shift;
    if (!g_ui.ready) return 0;
    id = g_ui.active_widget ? g_ui.active_widget : g_ui.hover_widget;
    for (int i = 0; i < g_nw; i++)
        if (g_w[i].id == id && g_w[i].type == WT_SLIDER) { k = &g_w[i]; break; }
    if (!k) return 0;
    if (vk == VK_HOME) { UiSl_Set(k->id, k->vmin); UiSl_Commit(); return 1; }
    if (vk == VK_END)  { UiSl_Set(k->id, k->vmax); UiSl_Commit(); return 1; }
    if (vk == VK_RIGHT || vk == VK_UP)
        return Ui_Wheel((k->rc.left + k->rc.right) / 2, (k->rc.top + k->rc.bottom) / 2, 120);
    if (vk == VK_LEFT || vk == VK_DOWN)
        return Ui_Wheel((k->rc.left + k->rc.right) / 2, (k->rc.top + k->rc.bottom) / 2, -120);
    return 0;
}

int Ui_DoubleClick(int x, int y)
{
    if (!g_ui.ready) return 0;
    for (int i = 0; i < g_nw; i++) {
        Widget *k = &g_w[i];
        if (k->type != WT_SLIDER) continue;
        if (x < k->rc.left || x > k->rc.right || y < k->rc.top || y > k->rc.bottom) continue;
        UiSl_Set(k->id, UiSl_Default(k->id));
        UiSl_Commit();
        Ui_Notify(L"Slider reset to its neutral value");
        return 1;
    }
    return 0;
}

/* ---------------- navigation ---------------- */
void Ui_Go(int side_id)
{
    if (side_id < ID_SIDE_BASE || side_id >= ID_SIDE_BASE + ID_SIDE_COUNT) return;
    g_panel = side_id;
    Ui_RebuildPanel();
    if (g_ui.hwnd) InvalidateRect(g_ui.hwnd, NULL, FALSE);
}

/* ---------------- settings glue ---------------- */
static int cfg_get(const char *sec, const char *key, int dflt)
{
    return PxSetGetInt(sec, key, dflt);
}

static void cfg_set(const char *sec, const char *key, int val)
{
    PxSetSetInt(sec, key, val);
    PxSet_Flush();
}

static void load_bg_image(void)
{
    if (g_ui.bg_bmp) { DeleteObject(g_ui.bg_bmp); g_ui.bg_bmp = NULL; }
    if (!g_appdata[0]) return;
    {
        wchar_t p[MAX_PATH];
        _snwprintf(p, MAX_PATH, L"%s\\background.bmp", g_appdata);
        g_ui.bg_bmp = (HBITMAP)LoadImageW(NULL, p, IMAGE_BITMAP, 0, 0, LR_LOADFROMFILE);
    }
}

int UiOpenFile(wchar_t *out, size_t cap, const wchar_t *filter, const wchar_t *title)
{
    OPENFILENAMEW ofn;
    out[0] = 0;
    memset(&ofn, 0, sizeof ofn);
    ofn.lStructSize = sizeof ofn;
    ofn.hwndOwner = g_ui.hwnd;
    ofn.lpstrFilter = filter;
    ofn.lpstrFile = out;
    ofn.nMaxFile = (DWORD)cap;
    ofn.lpstrTitle = title;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    return GetOpenFileNameW(&ofn) ? 1 : 0;
}

int UiSaveFile(wchar_t *out, size_t cap, const wchar_t *filter, const wchar_t *title,
               const wchar_t *suggested)
{
    OPENFILENAMEW ofn;
    memset(&ofn, 0, sizeof ofn);
    if (suggested) UiWin(out, cap, suggested);
    else out[0] = 0;
    ofn.lStructSize = sizeof ofn;
    ofn.hwndOwner = g_ui.hwnd;
    ofn.lpstrFilter = filter;
    ofn.lpstrFile = out;
    ofn.nMaxFile = (DWORD)cap;
    ofn.lpstrTitle = title;
    ofn.lpstrDefExt = L"json";
    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    return GetSaveFileNameW(&ofn) ? 1 : 0;
}

/* ---------------- modal input box (real window, real edit control) ---------- */
static wchar_t g_ib_text[192];
static int     g_ib_done;
static HFONT   g_ib_font;
#define ID_IB_EDIT 1
#define ID_IB_OK   2
#define ID_IB_CANCEL 3

static LRESULT CALLBACK inputbox_proc(HWND wnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_CREATE: {
        HWND ed = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", g_ib_text,
                                  WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
                                  16, 18, 400, 26, wnd, (HMENU)ID_IB_EDIT, NULL, NULL);
        CreateWindowExW(0, L"BUTTON", L"OK", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON,
                        230, 62, 88, 30, wnd, (HMENU)ID_IB_OK, NULL, NULL);
        CreateWindowExW(0, L"BUTTON", L"Cancel", WS_CHILD | WS_VISIBLE | WS_TABSTOP,
                        326, 62, 90, 30, wnd, (HMENU)ID_IB_CANCEL, NULL, NULL);
        SendMessageW(ed, WM_SETFONT, (WPARAM)g_ib_font, TRUE);
        SendMessageW(GetDlgItem(wnd, ID_IB_OK), WM_SETFONT, (WPARAM)g_ib_font, TRUE);
        SendMessageW(GetDlgItem(wnd, ID_IB_CANCEL), WM_SETFONT, (WPARAM)g_ib_font, TRUE);
        SendMessageW(ed, EM_SETSEL, 0, -1);
        SetFocus(ed);
        return 0;
    }
    case WM_COMMAND:
        if (LOWORD(wp) == ID_IB_OK) {
            GetDlgItemTextW(wnd, ID_IB_EDIT, g_ib_text, 192);
            g_ib_done = 1;
            DestroyWindow(wnd);
            return 0;
        }
        if (LOWORD(wp) == ID_IB_CANCEL) {
            g_ib_done = 0;
            DestroyWindow(wnd);
            return 0;
        }
        break;
    case WM_CLOSE:
        g_ib_done = 0;
        DestroyWindow(wnd);
        return 0;
    }
    return DefWindowProcW(wnd, msg, wp, lp);
}

/* Modal text prompt. Returns 1 + the edited string, 0 when cancelled. */
int UiInputBox(const wchar_t *title, const wchar_t *initial, wchar_t *out, size_t cap)
{
    static int registered = 0;
    WNDCLASSW wc;
    RECT parent;
    HWND wnd;
    MSG msg;
    int ox, oy;

    g_ib_font = g_ui.fBody;
    if (!registered) {
        memset(&wc, 0, sizeof wc);
        wc.lpfnWndProc = inputbox_proc;
        wc.hInstance = g_inst;
        wc.hCursor = LoadCursor(NULL, IDC_ARROW);
        wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
        wc.lpszClassName = L"PlexusXInputBox";
        RegisterClassW(&wc);
        registered = 1;
    }
    UiWin(g_ib_text, 192, initial ? initial : L"");
    g_ib_done = 0;
    GetWindowRect(g_ui.hwnd ? g_ui.hwnd : GetDesktopWindow(), &parent);
    ox = parent.left + ((parent.right - parent.left) - 448) / 2;
    oy = parent.top + ((parent.bottom - parent.top) - 116) / 2;

    wnd = CreateWindowExW(WS_EX_DLGMODALFRAME | WS_EX_TOPMOST, L"PlexusXInputBox", title ? title : L"PlexusX",
                          WS_POPUP | WS_CAPTION | WS_SYSMENU, ox, oy, 448, 140,
                          g_ui.hwnd, NULL, g_inst, NULL);
    if (!wnd) return 0;
    ShowWindow(wnd, SW_SHOW);
    SetWindowPos(wnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
    while (IsWindow(wnd) && GetMessageW(&msg, NULL, 0, 0)) {
        if (msg.message == WM_KEYDOWN && msg.wParam == VK_ESCAPE) {
            g_ib_done = 0;
            DestroyWindow(wnd);
            break;
        }
        if (!IsDialogMessageW(wnd, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
    if (!g_ib_done) return 0;
    UiWin(out, cap, g_ib_text);
    return out[0] != 0;
}

/* ---------------- action dispatch ---------------- */
int Ui_Exec(int id)
{
    switch (id) {
    case ID_CAP_MIN:
        ShowWindow(g_ui.hwnd, SW_MINIMIZE);
        return 1;
    case ID_CAP_CLOSE:
        PostMessageW(g_ui.hwnd, WM_CLOSE, 0, 0);
        return 1;

    /* ---------- dashboard quick actions ---------- */
    case ID_B_ENGINE_ON: {
        Look l = *Eng_GetRequested();
        l.enabled = 1;
        Eng_SetLook(&l);
        Ui_Notify(L"Colour engine enabled");
        UiSl_Commit();
        return 1;
    }
    case ID_B_ENGINE_OFF: {
        Look l = *Eng_GetRequested();
        l.enabled = 0;
        Eng_SetLook(&l);
        Ui_Notify(L"Colour engine bypassed — display output is untouched");
        UiSl_Commit();
        return 1;
    }
    case ID_B_OPEN_COLOR:    Ui_Go(ID_SIDE_COLOR);     return 1;
    case ID_B_OPEN_GAME:     Ui_Go(ID_SIDE_GAMES);     return 1;
    case ID_B_OPEN_DIAG:     Ui_Go(ID_SIDE_DIAG);      return 1;
    case ID_B_OPEN_MONITORS: Ui_Go(ID_SIDE_MONITORS);  return 1;
    case ID_B_OPEN_RATE:     Ui_Go(ID_SIDE_RATE);      return 1;
    case ID_B_OPEN_PHONE:    Ui_Go(ID_SIDE_PHONE);     return 1;

    case ID_B_HOME_APPLY_GAME: {
        Profile *gp = Prof_Get(Prof_ActiveIndex());
        if (gp) {
            Ui_LoadLook(&gp->sub[gp->active_sub].look);
            Prof_MarkActivated(Prof_ActiveIndex());
            Phone_PushProfileChange(gp->name);
            Eng_SetMode(PX_CSMODE_GAME, Prof_ActiveIndex(), gp->active_sub);
            {
                wchar_t msg[128];
                _snwprintf(msg, 128, L"%s · %s applied", gp->name, gp->sub[gp->active_sub].name);
                Ui_Notify(msg);
            }
            Wm_ReassertNow(L"profile-apply");
            UiSl_Commit();
        } else {
            Ui_Notify(L"No profile selected");
        }
        return 1;
    }
    case ID_B_HOME_COMPETITIVE: {
        int cnt = 0;
        const SceneDef *sc = Scene_GetList(&cnt);
        if (cnt > 0) Ui_LoadLook(&sc[0].look);
        Ui_Notify(L"Competitive look applied");
        UiSl_Commit();
        return 1;
    }
    case ID_B_HOME_MAX_VIB: {
        Look l = *Eng_GetRequested();
        l.sat = 300.0f; l.vibrance = 240.0f; l.enabled = 1;
        Eng_SetLook(&l);
        Ui_Notify(L"Maximum vibrance (sat 300% · vib 240%) applied");
        UiSl_Commit();
        return 1;
    }
    case ID_B_HOME_NIGHT_VIS: {
        Look l = *Eng_GetRequested();
        l.sat = 230.0f; l.gamma = 0.78f; l.shadows = 160.0f; l.bri = 120.0f; l.enabled = 1;
        Eng_SetLook(&l);
        Ui_Notify(L"Night visibility look applied");
        UiSl_Commit();
        return 1;
    }
    case ID_B_HOME_CINEMATIC: {
        Look l = *Eng_GetRequested();
        l.sat = 135.0f; l.temp = 5800.0f; l.con = 110.0f; l.enabled = 1;
        Eng_SetLook(&l);
        Ui_Notify(L"Cinematic warm look applied");
        UiSl_Commit();
        return 1;
    }
    case ID_B_HOME_NATURAL: {
        Look l = LOOK_NEUTRAL_INIT;
        l.enabled = 1;
        Eng_SetLook(&l);
        Ui_Notify(L"Neutral look restored");
        UiSl_Commit();
        return 1;
    }
    case ID_B_HOME_GAMING_MODE:
        Tools_ToggleGamingMode();
        Ui_Notify(Tools_IsGamingMode() ? L"Gaming mode on (overlay + engine priority)"
                                       : L"Gaming mode off");
        return 1;

    /* ---------- global colour ---------- */
    case ID_B_RESET_COLOR:
        Eng_Reset();
        Ui_Notify(L"Colour settings reset to neutral and original ramps restored");
        UiSl_Commit();
        return 1;
    case ID_B_RESET_EFFECT: {
        Look l = *Eng_GetRequested();
        l.sat = 100; l.vibrance = 100; l.bri = 100; l.con = 100; l.hue = 0;
        Eng_SetLook(&l);
        Ui_Notify(L"Current effect (saturation / vibrance / brightness / contrast / hue) reset");
        UiSl_Commit();
        return 1;
    }
    case ID_B_APPLY_NOW:
        Eng_ApplyNow();
        Wm_ReassertNow(L"manual-apply");
        Ui_Notify(Eng_RequestedMatchesApplied() ? L"Colour state applied and confirmed"
                                                : L"Apply requested — see the diagnostics page if it stays diverged");
        UiSl_Commit();
        return 1;
    case ID_B_COPY_SETTINGS:
        g_ui.clip_look = *Eng_GetRequested();
        g_ui.have_clip = 1;
        Ui_Notify(L"Colour settings copied");
        return 1;
    case ID_B_PASTE_SETTINGS:
        if (g_ui.have_clip) {
            Look l = g_ui.clip_look;
            l.enabled = Eng_GetRequested()->enabled;
            Eng_SetLook(&l);
            Ui_Notify(L"Colour settings pasted");
            UiSl_Commit();
        } else {
            Ui_Notify(L"Nothing copied yet — use COPY SETTINGS first");
        }
        return 1;

    /* ---------- unified preset library ---------- */
    case ID_B_PRESET_SAVE:           UiPreset_SaveCurrent();     return 1;
    case ID_B_PRESET_LOAD:           UiPreset_LoadSelected();    return 1;
    case ID_B_PRESET_DUPLICATE:      UiPreset_DuplicateSelected(); return 1;
    case ID_B_PRESET_DELETE:         UiPreset_DeleteSelected();  return 1;
    case ID_B_PRESET_EXPORT:         UiPreset_Export();          return 1;
    case ID_B_PRESET_IMPORT:         UiPreset_Import();          return 1;
    case ID_B_PRESET_APPLY_DISPLAY:  UiPreset_ApplyDisplaySelected(); return 1;
    case ID_B_PRESET_RENAME:         UiPreset_RenameSelected();      return 1;
    case ID_B_PRESET_CAT_GLOBAL:     g_ui.preset_cat = PX_PRESET_GLOBAL;    g_ui.sel_preset = 0; return 1;
    case ID_B_PRESET_CAT_GAME:       g_ui.preset_cat = PX_PRESET_GAME;      g_ui.sel_preset = 0; return 1;
    case ID_B_PRESET_CAT_DISPLAY:    g_ui.preset_cat = PX_PRESET_DISPLAY;   g_ui.sel_preset = 0; return 1;
    case ID_B_PRESET_CAT_CROSS:      g_ui.preset_cat = PX_PRESET_CROSSHAIR; g_ui.sel_preset = 0; return 1;

    /* ---------- game profiles ---------- */
    case ID_B_GAME_ADD:              UiGames_AddCustom();            return 1;
    case ID_B_GAME_DUPLICATE:        UiGames_DuplicateSelected();    return 1;
    case ID_B_GAME_DELETE:           UiGames_DeleteSelected();       return 1;
    case ID_B_GAME_RENAME:           UiGames_RenameSelected();       return 1;
    case ID_B_GAME_ACTIVATE:         UiGames_ActivateSelected();     return 1;
    case ID_B_GAME_CLEAR:            UiGames_ClearActive();          return 1;
    case ID_B_GAME_EXPORT:           UiGames_ExportSelected();       return 1;
    case ID_B_GAME_IMPORT:           UiGames_ImportFile();           return 1;
    case ID_B_GAME_EXE_ADD:          UiGames_AddExeFromEditor();     return 1;
    case ID_B_GAME_EXE_DEL:          UiGames_RemoveExeSlot();        return 1;
    case ID_B_GAME_PATH_SET:         UiGames_SetPathFromEditor();    return 1;
    case ID_B_GAME_SAVE_LOOK:
        if (Prof_Get(g_ui.sel_game)) {
            Prof_SetSubLook(g_ui.sel_game, Prof_Get(g_ui.sel_game)->active_sub, Eng_GetRequested());
            Ui_Notify(L"Current colour settings stored into the selected profile");
        }
        return 1;
    case ID_B_GAME_ENABLE:
        if (Prof_Get(g_ui.sel_game)) {
            int on = Prof_SetEnabled(g_ui.sel_game, !Prof_Get(g_ui.sel_game)->enabled);
            Ui_Notify(on ? L"Profile enabled" : L"Profile disabled — detection will skip it");
        }
        return 1;
    case ID_B_GAME_AUTOAPPLY:
        if (Prof_Get(g_ui.sel_game)) {
            int on = Prof_SetAutoApply(g_ui.sel_game, !Prof_Get(g_ui.sel_game)->auto_apply);
            Ui_Notify(on ? L"Auto-apply on launch enabled" : L"Manual activation only");
        }
        return 1;
    case ID_B_GAME_AUTORESTORE:
        if (Prof_Get(g_ui.sel_game)) {
            int on = Prof_SetAutoRestoreProfile(g_ui.sel_game, !Prof_Get(g_ui.sel_game)->auto_restore);
            Ui_Notify(on ? L"Desktop look restored when this game exits" : L"Desktop look will be kept");
        }
        return 1;
    case ID_B_GAME_APPLY_DISPLAY:
        if (Prof_Get(g_ui.sel_game)) {
            int on = Prof_SetApplyDisplay(g_ui.sel_game, !Prof_Get(g_ui.sel_game)->apply_display);
            Ui_Notify(on ? L"Profile will apply its display preference on activation"
                         : L"Profile will leave the display mode alone");
        }
        return 1;
    case ID_B_GAME_MONITOR_DEFAULT:
        if (Prof_Get(g_ui.sel_game)) Prof_SetMonitorTarget(g_ui.sel_game, -1);
        return 1;
    case ID_B_GAME_FAV:
        Prof_ToggleFavorite(g_ui.sel_game);
        return 1;
    case ID_B_GAME_RESET_BUILTINS:
        Prof_ResetBuiltins();
        Ui_Notify(L"Built-in game profiles restored (custom profiles kept)");
        return 1;

    /* ---------- display ---------- */
    case ID_B_MODE_APPLY_SEL:   UiDisplay_ApplySelectedMode(); return 1;
    case ID_B_MODE_CONFIRM:
        if (Modes_ConfirmPending()) Ui_Notify(L"Display mode kept — saved to Windows");
        return 1;
    case ID_B_MODE_REVERT:
        if (Modes_RollbackPending()) Ui_Notify(L"Previous display mode restored");
        return 1;
    case ID_B_RESET_DISPLAY:    UiDisplay_ResetToDefault(); return 1;
    case ID_B_MODE_MAX_HZ: {
        int before = Modes_Count();
        (void)before;
        if (Modes_ApplyMaxHz() == 0) Ui_Notify(L"Highest supported refresh rate applied");
        else Ui_Notify(L"The display reported no higher refresh mode");
        return 1;
    }
    case ID_B_MODE_NATIVE:
        if (Modes_ApplyNative() == 0) Ui_Notify(L"Native resolution and refresh restored");
        else Ui_Notify(L"Could not restore the native mode");
        return 1;
    case ID_B_MODE_43_COMP:     UiDisplay_ApplySafeRes(1280, 960, 0);  return 1;
    case ID_B_MODE_43_STRETCH:  UiDisplay_ApplySafeRes(1440, 1080, 0); return 1;
    case ID_B_MODE_43_CLASSIC:  UiDisplay_ApplySafeRes(1600, 1200, 0); return 1;
    case ID_B_MODE_1610:        UiDisplay_ApplySafeRes(1680, 1050, 0); return 1;
    case ID_B_MODE_ULTRAWIDE:   UiDisplay_ApplySafeRes(2560, 1080, 0); return 1;
    case ID_B_MODE_OPENHDR:     Modes_OpenHdrSettings();               return 1;
    case ID_B_HDR_REFRESH:
        Modes_Refresh();
        Ui_Notify(L"Display capabilities re-read from the OS");
        return 1;
    case ID_B_IDENTIFY_MONITORS: Modes_IdentifyMonitors(); return 1;
    case ID_B_BACKUP_NOW:
        Eng_BackupCurrentState();
        Ui_Notify(L"Current GPU ramps backed up");
        return 1;
    case ID_B_RESTORE_BACKUP:
        Ui_Notify(Eng_RestoreLastGood() ? L"Backed-up GPU ramps restored"
                                        : L"No usable ramp backup was found");
        return 1;

    /* ---------- crosshair ---------- */
    case ID_B_XH_TOGGLE_REMOTE:
        Xh_Toggle();
        g_ui.xh.on = Xh_IsActive();
        return 1;
    case ID_B_XH_SAVE_PRESET:   UiCrosshair_SavePreset(); return 1;
    case ID_B_XH_RESET: {
        XhCfg def = g_ui.xh;
        def.size = 16; def.gap = 4; def.thick = 2; def.opacity = 100;
        def.center_dot = 1; def.dot_size = 2; def.outline = 1; def.outline_th = 1;
        def.rotation = 0; def.shape = XH_CROSS;
        def.color = PX_T_ACCENT;
        def.ocolor = RGB(0, 0, 0);
        g_ui.xh = def;
        Xh_Update(&g_ui.xh);
        Ui_Notify(L"Crosshair reset to the default cross");
        return 1;
    }

    /* ---------- diagnostics ---------- */
    case ID_B_DIAG_COLOR_TEST:
        Tools_LaunchPattern(2);
        return 1;
    case ID_B_DIAG_DISP_TEST:
        Tools_LaunchPattern(7);
        return 1;
    case ID_B_DIAG_HDR_TEST:
        Tools_LaunchPattern(10);
        return 1;
    case ID_B_DIAG_RESET_COLOR:
        Eng_Reset();
        Ui_Notify(L"Colour transformation reset to identity and original ramps restored");
        UiSl_Commit();
        return 1;
    case ID_B_DIAG_REFRESH:
        Modes_Refresh();
        Prof_Poll();
        Eng_Invalidate(L"diagnostics refresh");
        Wm_ReassertNow(L"diagnostics-refresh");
        Ui_Notify(L"Diagnostics refreshed and the requested colour state re-asserted");
        return 1;
    case ID_B_DIAG_COPY:
        Ui_Notify(Tools_CopyDiagnostics() ? L"Diagnostics copied to the clipboard"
                                          : L"Clipboard unavailable");
        return 1;
    case ID_B_DIAG_EXPORT: {
        wchar_t path[MAX_PATH];
        int ok;
        _snwprintf(path, MAX_PATH, L"%s\\PlexusX_Diagnostics.json", g_appdata);
        ok = Tools_ExportDiagnostics(path);
        _snwprintf(path, MAX_PATH, L"%s\\PlexusX_Diagnostics.txt", g_appdata);
        ok = Tools_ExportDiagnosticsText(path) && ok;
        Ui_Notify(ok ? L"Diagnostics exported to the PlexusX AppData folder"
                     : L"Could not write the diagnostics files");
        return 1;
    }
    case ID_B_DIAG_RUN_TEST:
        Tools_LaunchPattern(6);
        return 1;

    /* ---------- phone remote ---------- */
    case ID_B_PHONE_TOGGLE:
        if (Phone_IsRunning()) {
            Phone_Stop();
            Ui_Notify(L"LAN phone remote stopped");
        } else {
            Ui_Notify(Phone_Start() == 0 ? L"LAN phone remote started on port 8777"
                                         : L"Could not bind port 8777 — another instance may be using it");
        }
        cfg_set("phone", "enabled", Phone_IsRunning());
        return 1;
    case ID_B_PHONE_NEW_PIN:
        Phone_RegeneratePin();
        Ui_Notify(L"New pairing PIN generated — paired devices were signed out");
        return 1;

    /* ---------- game switching delay ---------- */
    case ID_B_DELAY_0:    Prof_SetDelayMs(0);    Ui_Notify(L"Profiles apply instantly on detection"); return 1;
    case ID_B_DELAY_500:  Prof_SetDelayMs(500);  Ui_Notify(L"Profiles apply 500 ms after detection"); return 1;
    case ID_B_DELAY_1000: Prof_SetDelayMs(1000); Ui_Notify(L"Profiles apply 1 s after detection"); return 1;
    case ID_B_DELAY_2000: Prof_SetDelayMs(2000); Ui_Notify(L"Profiles apply 2 s after detection"); return 1;

    /* ---------- settings ---------- */
    case ID_B_SET_TRAY_MIN:
        cfg_set("ui", "minimize_tray", !cfg_get("ui", "minimize_tray", 0));
        return 1;
    case ID_B_SET_STARTWIN:
        g_ui.startup = !g_ui.startup;
        Main_SetStartup(g_ui.startup);
        Ui_Notify(g_ui.startup ? L"PlexusX will start with Windows" : L"Windows start-up entry removed");
        return 1;
    case ID_B_SET_AUTODETECT:
        Prof_SetDetect(!Prof_Detect());
        return 1;
    case ID_B_SET_AUTOSWITCH:
        Prof_SetAutoRestore(!Prof_GetAutoRestore());
        return 1;
    case ID_B_SET_ENGINE_START: {
        int v = (cfg_get("engine", "startup_state", 2) + 1) % 3;   /* off · on · last */
        cfg_set("engine", "startup_state", v);
        return 1;
    }
    case ID_B_SET_NOTIFY:
        cfg_set("ui", "notify", !cfg_get("ui", "notify", 1));
        return 1;
    case ID_B_SET_LOGGING: {
        int v = (cfg_get("diag", "log_level", 1) + 1) % 3;
        cfg_set("diag", "log_level", v);
        return 1;
    }
    case ID_B_SET_THEME: {
        int v = cfg_get("ui", "theme", 0) ? 0 : 1;
        cfg_set("ui", "theme", v);
        g_ui.bg_color = v ? RGB(0, 0, 0) : PX_T_BG;
        return 1;
    }
    case ID_B_SET_DEFAULT_MONITOR:
        cfg_set("engine", "default_monitor", Modes_CurrentMonitorIndex());
        Ui_Notify(L"Default monitor saved (colour engine targets every display)");
        return 1;
    case ID_B_SET_RESET:
        Eng_Reset();
        Prof_SetDetect(1);
        Prof_SetAutoRestore(1);
        Prof_SetDelayMs(0);
        cfg_set("ui", "theme", 0);
        cfg_set("ui", "notify", 1);
        cfg_set("ui", "minimize_tray", 0);
        cfg_set("ui", "glass", 1);
        cfg_set("ui", "anim", 2);
        cfg_set("ui", "reduce_motion", 0);
        cfg_set("ui", "bg", 1);
        cfg_set("diag", "log_level", 1);
        cfg_set("engine", "startup_state", 2);
        cfg_set("phone", "enabled", Phone_IsRunning());
        g_ui.glass = 1; g_ui.anim_level = 2; g_ui.reduce_motion = 0; g_ui.bg_mode = 1;
        g_ui.bg_color = PX_T_BG;
        Main_ApplyChrome();
        Ui_Notify(L"Settings reset to defaults — profiles, presets and history kept");
        return 1;
    case ID_B_OPEN_SETTINGS_DIR:
        ShellExecuteW(NULL, L"open", g_appdata, NULL, NULL, SW_SHOWNORMAL);
        return 1;

    /* ---------- appearance ---------- */
    case ID_B_BG_NONE:      g_ui.bg_mode = 0; cfg_set("ui", "bg", 0); return 1;
    case ID_B_BG_ABSTRACT:  g_ui.bg_mode = 1; cfg_set("ui", "bg", 1); return 1;
    case ID_B_BG_IMAGE:
        g_ui.bg_mode = 2;
        cfg_set("ui", "bg", 2);
        load_bg_image();
        if (!g_ui.bg_bmp) Ui_Notify(L"Put background.bmp in the PlexusX AppData folder, then pick it again");
        return 1;
    case ID_B_ANIM_ON:      g_ui.anim_level = 2; g_ui.reduce_motion = 0; cfg_set("ui", "anim", 2); cfg_set("ui", "reduce_motion", 0); return 1;
    case ID_B_ANIM_REDUCED: g_ui.anim_level = 1; cfg_set("ui", "anim", 1); return 1;
    case ID_B_ANIM_OFF:     g_ui.anim_level = 0; g_ui.reduce_motion = 1; cfg_set("ui", "anim", 0); cfg_set("ui", "reduce_motion", 1); Main_ApplyChrome(); return 1;

    /* ---------- safety ---------- */
    case ID_B_RESET_ALL:
        Modes_ApplyNative();
        Eng_Reset();
        Ui_Notify(L"Colour and display state returned to defaults");
        return 1;
    case ID_B_EMERGENCY_RESET:
        Main_EmergencyReset();
        Ui_Notify(L"Emergency reset: patterns closed, engine bypassed, original ramps restored");
        UiSl_Commit();
        return 1;

    default:
        if (id >= ID_B_GROUP_RESET_BASE && id < ID_B_GROUP_RESET_BASE + 8) {
            UiColor_GroupReset(id - ID_B_GROUP_RESET_BASE);
            return 1;
        }
        if (id >= ID_TEST_PAT_BASE && id < ID_TEST_PAT_BASE + 12) {
            Tools_LaunchPattern(id - ID_TEST_PAT_BASE);
            return 1;
        }
        if (id >= ID_LOOK_CARD_BASE && id < ID_LOOK_CARD_BASE + 32) {
            int cnt = 0, idx = id - ID_LOOK_CARD_BASE;
            const SceneDef *sc = Scene_GetList(&cnt);
            if (idx >= 0 && idx < cnt) {
                Ui_LoadLook(&sc[idx].look);
                Ui_Notify(sc[idx].name);
                UiSl_Commit();
            }
            return 1;
        }
        if (id >= ID_XH_PRESET_BASE && id < ID_XH_PRESET_BASE + 16) {
            UiCrosshair_LoadPreset(id - ID_XH_PRESET_BASE);
            return 1;
        }
        if (id >= ID_MONITOR_CARD_BASE && id < ID_MONITOR_CARD_BASE + 8) {
            Modes_SetCurrentMonitor(id - ID_MONITOR_CARD_BASE);
            Ui_Notify(L"Monitor selected");
            return 1;
        }
        if (id >= ID_MONITOR_ROW_BASE && id < ID_MONITOR_ROW_BASE + 8) {
            g_ui.sel_monitor = id - ID_MONITOR_ROW_BASE;
            Modes_SetCurrentMonitor(g_ui.sel_monitor);
            return 1;
        }
        if (id >= ID_MODE_ROW_BASE && id < ID_MODE_ROW_BASE + 64) {
            g_ui.sel_mode = id - ID_MODE_ROW_BASE;
            return 1;
        }
        if (id >= ID_GAME_CARD_BASE && id < ID_GAME_CARD_BASE + 40) {
            g_ui.sel_game = id - ID_GAME_CARD_BASE;
            Prof_SetActiveIndex(g_ui.sel_game);
            return 1;
        }
        if (id >= ID_GAME_SUB_BASE && id < ID_GAME_SUB_BASE + 16) {
            if (Prof_Get(g_ui.sel_game)) Prof_SelectSubMode(g_ui.sel_game, id - ID_GAME_SUB_BASE);
            return 1;
        }
        if (id >= ID_GAME_EXE_SLOT_BASE && id < ID_GAME_EXE_SLOT_BASE + 8) {
            g_ui.sel_exe_slot = id - ID_GAME_EXE_SLOT_BASE;
            return 1;
        }
        if (id >= ID_MONITOR_TARGET_BASE && id < ID_MONITOR_TARGET_BASE + 8) {
            int slot = id - ID_MONITOR_TARGET_BASE;
            if (Prof_Get(g_ui.sel_game))
                Prof_SetMonitorTarget(g_ui.sel_game, slot == 0 ? -1 : slot - 1);
            return 1;
        }
        if (id >= ID_PRESET_CARD_BASE && id < ID_PRESET_CARD_BASE + PX_PRESET_MAX) {
            g_ui.sel_preset = id - ID_PRESET_CARD_BASE;
            return 1;
        }
        if (id >= ID_SIDE_BASE && id < ID_SIDE_BASE + ID_SIDE_COUNT) {
            Ui_Go(id);
            return 1;
        }
        return 0;
    }
}

/* ---------------- lifecycle ---------------- */
void Ui_Init(HWND hwnd, HINSTANCE inst)
{
    (void)inst;
    memset(&g_ui, 0, sizeof g_ui);
    g_ui.hwnd = hwnd;
    g_ui.ready = 0;
    g_ui.panel = ID_SIDE_HOME;
    g_ui.split_pos = 0.50f;
    g_ui.sel_mode = -1;
    g_ui.sel_exe_slot = -1;
    g_ui.sel_hdr_row = -1;
    g_ui.sel_monitor = 0;
    g_ui.applied_game = -1;
    g_ui.preset_cat = PX_PRESET_GLOBAL;
    g_ui.bg_color = PX_T_BG;
    g_ui.clip_look = (Look)LOOK_NEUTRAL_INIT;

    {
        HDC sdc = GetDC(NULL);
        g_ui.sc = GetDeviceCaps(sdc, LOGPIXELSX) / 96.0f;
        ReleaseDC(NULL, sdc);
        if (g_ui.sc < 0.75f || g_ui.sc > 4.0f) g_ui.sc = 1.0f;
    }

    memset(&g_ui.xh, 0, sizeof g_ui.xh);
    g_ui.xh.on = 0;
    g_ui.xh.shape = XH_CROSS;
    g_ui.xh.size = 16; g_ui.xh.gap = 4; g_ui.xh.thick = 2; g_ui.xh.opacity = 90;
    g_ui.xh.center_dot = 1; g_ui.xh.dot_size = 2; g_ui.xh.outline = 1; g_ui.xh.outline_th = 1;
    g_ui.xh.color = PX_T_ACCENT;
    g_ui.xh.ocolor = RGB(0, 0, 0);
    g_ui.xh.monitor_idx = 0;

    make_fonts();
    Modes_Refresh();
    Prof_Init();
    Tools_Init();
    PxPre_Init(g_appdata);
    Ui_RebuildPanel();
    g_ui.ready = 1;
}

void Ui_AttachWindow(HWND hwnd) { g_ui.hwnd = hwnd; }
int  Ui_IsReady(void) { return g_ui.ready; }

void Ui_Free(void)
{
    g_ui.ready = 0;
    DeleteObject(g_ui.fLogo);
    DeleteObject(g_ui.fH1);
    DeleteObject(g_ui.fH2);
    DeleteObject(g_ui.fBody);
    DeleteObject(g_ui.fSmall);
    DeleteObject(g_ui.fMono);
    DeleteObject(g_ui.fBigVal);
    if (g_ui.bg_bmp) { DeleteObject(g_ui.bg_bmp); g_ui.bg_bmp = NULL; }
    Tools_Shutdown();
}

void Ui_SetPanel(int side_id) { Ui_Go(side_id); }
int  Ui_Panel(void) { return g_panel; }
Look *Ui_Look(void) { return (Look *)Eng_GetRequested(); }
void Ui_LoadLook(const Look *lk) { if (lk) Eng_SetLook(lk); }

void Ui_Notify(const wchar_t *msg)
{
    if (!msg) return;
    if (!PxSetGetInt("ui", "notify", 1)) return;
    UiWin(g_ui.toast, 160, msg);
    g_ui.toast_time = GetTickCount();
}

void Ui_GetXh(XhCfg *out) { if (out) *out = g_ui.xh; }
void Ui_SetXh(const XhCfg *in) { if (in) g_ui.xh = *in; }

int Ui_CapHit(int x, int y) { return (x >= S(PX_WIN_W - 100) && y <= S(UI_TOPBAR_H)); }
int Ui_InTop(int x, int y) { return (y <= S(UI_TOPBAR_H) && x < S(PX_WIN_W - 100)); }

void Ui_FilterGames(const wchar_t *filter) { (void)filter; Ui_RebuildPanel(); }
const wchar_t *Ui_GetFilter(void) { return L""; }

int  Ui_GlassEnabled(void) { return g_ui.glass; }
int  Ui_BgMode(void) { return g_ui.bg_mode; }
int  Ui_ReduceMotion(void) { return g_ui.reduce_motion; }
int  Ui_AnimLevel(void) { return g_ui.anim_level; }
int  Ui_StartupEnabled(void) { return g_ui.startup; }
void Ui_SetStartupEnabled(int on) { g_ui.startup = on ? 1 : 0; }

void Ui_LoadAppearance(int glass, int bg, int reduce, int anim, int startup)
{
    g_ui.glass = glass ? 1 : 0;
    g_ui.bg_mode = clampi(bg, 0, 2);
    g_ui.reduce_motion = reduce ? 1 : 0;
    g_ui.anim_level = clampi(anim, 0, 2);
    g_ui.startup = startup ? 1 : 0;
    g_ui.bg_color = PxSetGetInt("ui", "theme", 0) ? RGB(0, 0, 0) : PX_T_BG;
    if (g_ui.bg_mode == 2) load_bg_image();
}
