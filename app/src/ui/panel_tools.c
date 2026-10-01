/* PlexusX — TOOLS ▸ Crosshair / Test Patterns / Diagnostics.
 *
 * Crosshair: a layered, click-through desktop overlay drawn by the OS compositor.
 * It never touches a game process, memory or files — it is a window, like the
 * Windows magnifier is a program.
 *
 * Test Patterns: fullscreen Windows created by tools.c; they exit on any key,
 * click, or after 10 seconds, so they cannot strand the display.
 *
 * Diagnostics: the page renders the SAME builder that the clipboard and the
 * exported file use (diagnostics_report.h), so what is on screen is exactly what
 * a report contains.
 */
#include "ui_internal.h"
#include "../ui_theme.h"

/* ---------------- Crosshair ---------------- */
static const COLORREF g_swatches[10] = {
    RGB(198, 255, 61), RGB(79, 227, 255), RGB(255, 79, 227), RGB(255, 158, 61),
    RGB(255, 242, 61), RGB(255, 79, 79), RGB(0, 255, 124), RGB(79, 123, 255),
    RGB(255, 255, 255), RGB(0, 0, 0)
};

void UiCrosshair_Build(void)
{
    int count = 0;
    const XhPreset *plist = Xh_GetPresets(&count);

    UiW(WT_HEAD, 0, 248, 62, 700, 30, L"Crosshair Overlay");
    UiW(WT_LABEL, 0, 248, 92, 986, 18,
        L"A desktop overlay window: no injection, no game-file edits, no memory access. Desktop mode only — overlays are not drawn over exclusive fullscreen surfaces.");

    {
        Widget *t = UiW(WT_TOGGLE, ID_T_XH, 248, 122, 320, 30, L"Crosshair overlay enabled");
        t->state = Xh_IsActive();
        UiW(WT_GHOST, ID_B_XH_TOGGLE_REMOTE, 578, 122, 180, 30, L"Toggle now (Ctrl+Alt+X)");
        Widget *s = UiW(WT_GHOST, ID_B_XH_SAVE_PRESET, 768, 122, 190, 30, L"Save as preset");
        (void)s;
        UiW(WT_GHOST, ID_B_XH_RESET, 968, 122, 266, 30, L"Reset to default cross");
    }

    UiW(WT_DIV, 0, 248, 166, 986, 20, L"SHAPE");
    {
        static const wchar_t *shapes[XH_SHAPE_COUNT] = { L"Dot", L"Cross", L"Circle", L"Square", L"Plus", L"Chevron", L"T", L"T-Type" };
        for (int i = 0; i < XH_SHAPE_COUNT; i++) {
            Widget *k = UiW(WT_SHAPE, ID_XH_SHAPE_BASE + i, 248 + i * 124, 190, 116, 68, shapes[i]);
            _snwprintf(k->val, 48, L"%d", i);
            k->state = (g_ui.xh.shape == i);
        }
    }

    UiW(WT_DIV, 0, 248, 274, 986, 20, L"GEOMETRY  ·  double-click a slider to reset it");
    {
        static const struct { int id; const wchar_t *name; float lo, hi; const wchar_t *unit; } sl[] = {
            { ID_SL_XH_SIZE,    L"Size",       4, 64,   L"px" },
            { ID_SL_XH_GAP,     L"Center gap", 0, 32,   L"px" },
            { ID_SL_XH_THICK,   L"Thickness",  1, 12,   L"px" },
            { ID_SL_XH_OPACITY, L"Opacity",   10, 100,  L"%"  },
            { ID_SL_XH_DOTSIZE, L"Center dot size", 1, 8, L"px" },
            { ID_SL_XH_ROT,     L"Rotation",    0, 359, L"deg" }
        };
        for (int i = 0; i < 6; i++) {
            int col = i % 3, row = i / 3;
            Widget *k = UiW(WT_SLIDER, sl[i].id, 248 + col * 330, 298 + row * 62, 320, 58, sl[i].name);
            UiWVal(k, UiSl_Get(sl[i].id), sl[i].lo, sl[i].hi, sl[i].unit);
        }
    }
    {
        Widget *t = UiW(WT_TOGGLE, ID_T_XH_OUTLINE, 248, 428, 300, 30, L"Outline");
        t->state = g_ui.xh.outline;
        t = UiW(WT_TOGGLE, ID_T_XH_DOT, 578, 428, 300, 30, L"Center dot");
        t->state = g_ui.xh.center_dot;
    }

    UiW(WT_DIV, 0, 248, 472, 986, 20, L"COLOUR");
    UiW(WT_LABEL, 0, 248, 496, 200, 18, L"Primary");
    for (int i = 0; i < 10; i++) {
        Widget *sw = UiW(WT_SWATCH, ID_SWATCH_BASE + i, 248 + i * 52, 518, 44, 34, NULL);
        _snwprintf(sw->val, 48, L"%d", (int)g_swatches[i]);
        sw->state = (g_ui.xh.color == g_swatches[i]);
    }
    UiW(WT_LABEL, 0, 248, 562, 200, 18, L"Outline");
    for (int i = 0; i < 10; i++) {
        Widget *sw = UiW(WT_SWATCH, ID_SWATCH_O_BASE + i, 248 + i * 52, 584, 44, 34, NULL);
        _snwprintf(sw->val, 48, L"%d", (int)g_swatches[i]);
        sw->state = (g_ui.xh.ocolor == g_swatches[i]);
    }
    UiW(WT_LABEL, 0, 248, 630, 986, 18, L"Presets");

    for (int i = 0; i < count && i < 10; i++) {
        Widget *b = UiW(WT_GHOST, ID_XH_PRESET_BASE + i, 248 + i * 100, 654, 92, 32, plist[i].name);
        (void)b;
    }
    UiW(WT_LABEL, 0, 248, 698, 986, 18,
        L"The overlay is per-monitor (primary by default) and click-through, so it never steals input from a game.");
}

void UiCrosshair_SavePreset(void)
{
    wchar_t name[64];
    PxPreset ps;
    char utf8[PX_PRESET_NAME];
    if (!UiInputBox(L"Save crosshair preset", L"My Crosshair", name, 64)) return;
    PxPreset_Neutral(&ps);
    WideCharToMultiByte(CP_UTF8, 0, name, -1, utf8, PX_PRESET_NAME, NULL, NULL);
    snprintf(ps.name, PX_PRESET_NAME, "%s", utf8);
    ps.category = PX_PRESET_CROSSHAIR;
    ps.xh_shape = g_ui.xh.shape;
    ps.xh_size = g_ui.xh.size;
    ps.xh_gap = g_ui.xh.gap;
    ps.xh_thick = g_ui.xh.thick;
    ps.xh_opacity = g_ui.xh.opacity;
    ps.xh_dot = g_ui.xh.center_dot;
    ps.xh_dot_size = g_ui.xh.dot_size;
    ps.xh_outline = g_ui.xh.outline;
    ps.xh_outline_th = g_ui.xh.outline_th;
    ps.xh_rot = g_ui.xh.rotation;
    ps.xh_color = (unsigned)g_ui.xh.color;
    ps.xh_ocolor = (unsigned)g_ui.xh.ocolor;
    if (PxPre_Add(PxPre_Lib(), &ps) < 0) { Ui_Notify(L"Preset library is full"); return; }
    PxPre_Save();
    Ui_Notify(L"Crosshair preset saved to the unified library");
}

void UiCrosshair_LoadPreset(int idx)
{
    int count = 0;
    const XhPreset *plist = Xh_GetPresets(&count);
    if (idx < 0 || idx >= count) return;
    g_ui.xh.shape = plist[idx].cfg.shape;
    g_ui.xh.size = plist[idx].cfg.size;
    g_ui.xh.gap = plist[idx].cfg.gap;
    g_ui.xh.thick = plist[idx].cfg.thick;
    g_ui.xh.opacity = plist[idx].cfg.opacity;
    g_ui.xh.center_dot = plist[idx].cfg.center_dot;
    g_ui.xh.dot_size = plist[idx].cfg.dot_size;
    g_ui.xh.outline = plist[idx].cfg.outline;
    g_ui.xh.outline_th = plist[idx].cfg.outline_th;
    g_ui.xh.rotation = plist[idx].cfg.rotation;
    g_ui.xh.color = plist[idx].cfg.color;
    g_ui.xh.ocolor = plist[idx].cfg.ocolor;
    Xh_Update(&g_ui.xh);
    {
        wchar_t msg[96];
        _snwprintf(msg, 96, L"Crosshair preset: %s", plist[idx].name);
        Ui_Notify(msg);
    }
}

/* ---------------- Test Patterns ---------------- */
void UiPatterns_Build(void)
{
    static const wchar_t *pats[] = {
        L"Pure Black", L"Pure White", L"Pure Red", L"Pure Green", L"Pure Blue",
        L"16-Step Gradient", L"Gamma 2.2 Wedge", L"Contrast Steps",
        L"Sharpness Grid", L"Banding Test", L"HDR Peak Luminance"
    };

    UiW(WT_HEAD, 0, 248, 62, 700, 30, L"Test Patterns");
    UiW(WT_LABEL, 0, 248, 92, 986, 18,
        L"Fullscreen reference patterns for verifying the panel, the cable and the colour pipeline. Any key, click, or 10 s auto-close exits.");

    for (int i = 0; i < (int)(sizeof pats / sizeof pats[0]); i++) {
        int col = i % 4, row = i / 4;
        Widget *b = UiW(WT_BTN, ID_TEST_PAT_BASE + i, 248 + col * 248, 126 + row * 58, 234, 46, pats[i]);
        (void)b;
    }

    UiW(WT_DIV, 0, 248, 314, 986, 20, L"WHAT EACH GROUP PROVES");
    UiW(WT_LABEL, 0, 248, 338, 986, 20,
        L"Flat colours and gradient steps: panel uniformity and banding. Contrast and gamma wedges: black crush and highlight clipping. Grid: sharpness and scaling.");
    UiW(WT_LABEL, 0, 248, 360, 986, 20,
        L"HDR peak: what the current output really shows compresses to — run it with HDR on and off to compare honestly.");

    UiW(WT_DIV, 0, 248, 398, 986, 20, L"VERIFICATION");
    UiW(WT_GHOST, ID_B_DIAG_DISP_TEST, 248, 422, 220, 40, L"Display pattern");
    UiW(WT_GHOST, ID_B_DIAG_COLOR_TEST, 478, 422, 220, 40, L"Gamma 2.2 wedge");
    UiW(WT_GHOST, ID_B_DIAG_HDR_TEST, 708, 422, 220, 40, L"HDR peak pattern");
    UiW(WT_GHOST, ID_B_DIAG_RUN_TEST, 938, 422, 296, 40, L"Pattern with the current look");

    UiW(WT_DIV, 0, 248, 478, 986, 20, L"SAFETY AND RECOVERY");
    UiW(WT_GHOST, ID_B_BACKUP_NOW, 248, 502, 220, 40, L"Back up ramps");
    UiW(WT_GHOST, ID_B_RESTORE_BACKUP, 478, 502, 220, 40, L"Restore last good ramps");
    UiW(WT_ACCENT, ID_B_RESET_DISPLAY, 708, 502, 220, 40, L"Reset display mode");
    UiW(WT_ACCENT, ID_B_EMERGENCY_RESET, 938, 502, 296, 40, L"Emergency reset (Ctrl+Alt+Shift+R)");

    UiW(WT_DIV, 0, 248, 558, 986, 20, L"GAMING MODE");
    {
        Widget *gm = UiW(WT_BTN, ID_B_HOME_GAMING_MODE, 248, 582, 300, 40,
                         Tools_IsGamingMode() ? L"Gaming mode: ON" : L"Gaming mode: OFF");
        gm->flags = Tools_IsGamingMode() ? 1 : 0;
    }
    UiW(WT_LABEL, 0, 560, 590, 674, 34,
        L"Gaming mode lowers UI work while a game is in the foreground. The colour pipeline itself is event-driven and is never throttled.");
}

/* ---------------- Diagnostics ---------------- */
void UiDiagnostics_Build(void)
{
    char text[8192];
    wchar_t wline[400];
    int scan = 0, row = 0, y = 96;
    const int max_rows = 30;

    UiW(WT_HEAD, 0, 248, 62, 700, 30, L"Diagnostics");
    UiW(WT_LABEL, 0, 248, 92, 986, 18,
        L"Every value on this page comes from the same report the clipboard and the exported file contain — nothing here is estimated.");

    UiW(WT_PRIMARY, ID_B_DIAG_REFRESH,   248,  122, 170, 34, L"REFRESH");
    UiW(WT_GHOST,   ID_B_DIAG_COPY,      428,  122, 200, 34, L"COPY DIAGNOSTICS");
    UiW(WT_GHOST,   ID_B_DIAG_EXPORT,    638,  122, 210, 34, L"EXPORT DIAGNOSTICS");
    UiW(WT_GHOST,   ID_B_DIAG_COLOR_TEST,858,  122, 180, 34, L"TEST COLOUR");
    UiW(WT_GHOST,   ID_B_DIAG_RESET_COLOR,1048,122, 186, 34, L"RESET COLOUR");

    if (Tools_DiagnosticsText(text, (int)sizeof text) <= 0) {
        UiW(WT_LABEL, 0, 248, 176, 986, 20, L"The diagnostics report could not be built.");
        return;
    }

    /* Render the report verbatim, newest-relevant sections first; the text is
     * ASCII/UTF-8 from the builder, so it is widened without reinterpretation. */
    {
        const char *p = text;
        while (*p && row < max_rows) {
            const char *e = strchr(p, '\n');
            int len = e ? (int)(e - p) : (int)strlen(p);
            int is_head = (len > 3 && p[0] != ' ' && p[1] != ' ' && strchr(p, ':') == NULL);
            int is_kv = 0;
            if (len > 0) {
                int i;
                for (i = 0; i < len; i++) if (p[i] == ':' ) { is_kv = 1; break; }
            }
            if (len > 0) {
                if (len > 390) len = 390;
                MultiByteToWideChar(CP_UTF8, 0, p, len, wline, 400);
                wline[len < 399 ? len : 399] = 0;
                if (is_head && !is_kv) {
                    UiW(WT_DIV, 0, 248, y, 986, 20, wline);
                    y += 26;
                } else if (is_kv) {
                    char key[128];
                    int colon = 0, i;
                    for (i = 0; i < len && i < 120; i++) if (p[i] == ':') { colon = i; break; }
                    if (colon > 0) {
                        wchar_t wk[128], wv[280];
                        MultiByteToWideChar(CP_UTF8, 0, p, colon, wk, 128);
                        wk[colon < 127 ? colon : 127] = 0;
                        MultiByteToWideChar(CP_UTF8, 0, p + colon + 1, len - colon - 1, wv, 280);
                        wv[len - colon - 1 < 279 ? len - colon - 1 : 279] = 0;
                        (void)key;
                        (void)UiWKV(248, y, 986, wk, wv);
                    }
                    y += 22;
                } else {
                    UiW(WT_LABEL, 0, 248, y, 986, 18, wline);
                    y += 20;
                }
                row++;
            }
            if (!e) break;
            p = e + 1;
            scan++;
            (void)scan;
            if (y > 700) break;
        }
    }

    {
        wchar_t buf[128];
        _snwprintf(buf, 128, L"%d lines shown  ·  the full report is written by EXPORT DIAGNOSTICS", row);
        UiW(WT_LABEL, 0, 248, 700, 986, 18, buf);
    }
}
