/* PlexusX — UI internals shared by the panel modules.
 *
 * The Win32 shell is split the way the product is laid out:
 *
 *   ui_core.c        engine: DPI, fonts, widget table, painting, input,
 *                    navigation (grouped sidebar), status bar, Ui_Exec
 *   panel_home.c           HOME dashboard
 *   panel_color.c          DISPLAY ▸ Global Color
 *   panel_display.c        DISPLAY ▸ Monitors / Resolution / Refresh Rate
 *   panel_games.c          GAMES ▸ Game Profiles / Active Game / Custom Games
 *   panel_tools.c          TOOLS ▸ Crosshair / Test Patterns / Diagnostics
 *   panel_settings.c       REMOTE ▸ Phone Control · SETTINGS · preset library
 *
 * Every panel builds widgets into the ONE table owned by ui_core.c and reads
 * state from its owner (engine, preset store, display manager, game runtime) —
 * no panel keeps a private copy of anything.  A widget whose value is not real
 * is simply not created: no decorative buttons, no invented readouts.
 */
#ifndef PLEXUSX_UI_INTERNAL_H
#define PLEXUSX_UI_INTERNAL_H

#include "../common.h"

/* ---------------- geometry ---------------- */
#define UI_SIDEBAR_W  220
#define UI_TOPBAR_H   56
#define UI_STATUS_H   104
#define UI_CONTENT_X  248
#define UI_CONTENT_Y  70
#define UI_CONTENT_W  986

/* ---------------- one entry point per panel ---------------- */
void UiHome_Build(void);
void UiColor_Build(void);
void UiMonitors_Build(void);
void UiResolution_Build(void);
void UiRefresh_Build(void);
void UiGames_Build(void);
void UiActiveGame_Build(void);
void UiCustomGames_Build(void);
void UiCrosshair_Build(void);
void UiPatterns_Build(void);
void UiDiagnostics_Build(void);
void UiPhone_Build(void);
void UiSettings_Build(void);

/* panels may ask the core to navigate or notify */
void Ui_Go(int side_id);

/* ---------------- actions implemented by the panels ----------------
 * ui_core.c dispatches by id; the handlers live with the page they belong to. */
void UiPreset_SaveCurrent(void);
void UiPreset_LoadSelected(void);
void UiPreset_DuplicateSelected(void);
void UiPreset_DeleteSelected(void);
void UiPreset_Export(void);
void UiPreset_Import(void);
void UiPreset_ApplyDisplaySelected(void);
void UiPreset_RenameSelected(void);

void UiGames_AddCustom(void);
void UiGames_DuplicateSelected(void);
void UiGames_DeleteSelected(void);
void UiGames_RenameSelected(void);
void UiGames_ExportSelected(void);
void UiGames_ImportFile(void);
void UiGames_AddExeFromEditor(void);
void UiGames_RemoveExeSlot(void);
void UiGames_SetPathFromEditor(void);
void UiGames_ActivateSelected(void);
void UiGames_ClearActive(void);

void UiDisplay_ApplySelectedMode(void);          /* confirm-or-rollback      */
void UiDisplay_ApplySafeRes(int w, int h, int hz);
void UiDisplay_ResetToDefault(void);

void UiColor_GroupReset(int group);

void UiCrosshair_SavePreset(void);
void UiCrosshair_LoadPreset(int idx);

/* ---------------- shared UI state (owned by ui_core.c) ---------------- */
typedef struct UiState {
    HWND    hwnd;
    int     ready;
    float   sc;                       /* DPI scale */
    Widget  w[MAX_WIDGETS];
    int     nw;
    int     panel;

    XhCfg   xh;
    Look    clip_look;                /* COPY SETTINGS buffer (not a truth)   */
    int     have_clip;

    int     active_widget;
    int     hover_widget;
    int     drag_split;
    float   split_pos;

    int     glass, bg_mode, reduce_motion, anim_level, startup;
    HBITMAP bg_bmp;
    COLORREF bg_color;

    wchar_t toast[160];
    DWORD   toast_time;

    /* page-scoped selection (stateless navigation: selection survives a page) */
    int     sel_game;                 /* highlighted library profile          */
    int     applied_game;             /* profile the engine is currently following, -1 */
    int     sel_exe_slot;
    int     sel_sub;
    int     sel_preset;
    int     preset_cat;               /* PxPresetCat tab                      */
    int     sel_monitor;
    int     sel_mode;
    int     sel_hdr_row;
    int     confirm_armed;            /* "reset" two-step confirmation        */

    HFONT   fLogo, fH1, fH2, fBody, fSmall, fMono, fBigVal;
} UiState;

extern UiState g_ui;

/* ---------------- widget construction ---------------- */
Widget *UiW(int type, int id, int x, int y, int w, int h, const wchar_t *text);
void    UiWVal(Widget *k, float val, float lo, float hi, const wchar_t *unit);
Widget *UiWCard(int x, int y, int w, int h, const wchar_t *head, const wchar_t *val, const wchar_t *sub);
Widget *UiWKV(int x, int y, int w, const wchar_t *key, const wchar_t *val);
Widget *UiWChip(int x, int y, int w, const wchar_t *text, int tone);

/* slider <-> value binding (one place; no panel may re-implement it) */
float   UiSl_Get(int id);
int     UiSl_Set(int id, float val);      /* → Eng_SetLook (sanitised)       */
float   UiSl_Default(int id);
int     UiSl_Commit(void);                /* rebuild + apply (engine skips no-op writes) */

/* look mutation shortcut: copy → edit → Eng_SetLook (never a second truth) */
Look   *UiLook_Mut(void);                 /* pointer into ColorState         */
void    UiLook_Commit(void);              /* Eng_SetLook(current) + apply    */

/* drawing helpers (implemented in ui_core.c) */
int      UiS(int v);
void     UiDrawRRect(HDC dc, RECT rc, int rad, HBRUSH br, HPEN pen);
void     UiDrawText(HDC dc, RECT rc, const wchar_t *s, HFONT f, COLORREF c, int align);
COLORREF UiToneColor(int tone);           /* PX_CHIP_* → theme colour        */
void     UiDrawSplitPreview(HDC dc, RECT rc);
const wchar_t *UiUtf8(const char *utf8, wchar_t *buf, size_t cap);
void     UiWin(wchar_t *dst, size_t cap, const wchar_t *src);

/* status helpers — one source: the engine's effective state */
const PxEffectiveState *UiStatus(void);
int  UiToneOfStatus(void);
int  UiEngineStatusIs(const int *states, int n);

/* file dialogs (ui_core.c) */
int  UiOpenFile(wchar_t *out, size_t cap, const wchar_t *filter, const wchar_t *title);
int  UiSaveFile(wchar_t *out, size_t cap, const wchar_t *filter, const wchar_t *title,
                const wchar_t *suggested);
int  UiInputBox(const wchar_t *title, const wchar_t *initial, wchar_t *out, size_t cap);

#endif /* PLEXUSX_UI_INTERNAL_H */
