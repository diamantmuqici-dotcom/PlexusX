/* PlexusX — REMOTE ▸ Phone Control · SETTINGS · unified preset library.
 *
 * Phone Control: the pairing server lives in phone.c; this page only shows what
 * that server really answers (URL, PIN, connected clients) and lets the user turn
 * it on or off and rotate the PIN.
 *
 * The preset library is the unified versioned store (presets.ini): Global, Game,
 * Display and Crosshair payloads in one format, with save / load / rename /
 * duplicate / delete / import / export and migration handled by preset_store.h.
 */
#include "ui_internal.h"
#include "../ui_theme.h"

static const wchar_t *cat_name(int cat)
{
    switch (cat) {
    case PX_PRESET_GLOBAL:    return L"Global";
    case PX_PRESET_GAME:      return L"Game";
    case PX_PRESET_DISPLAY:   return L"Display";
    case PX_PRESET_CROSSHAIR: return L"Crosshair";
    default:                  return L"—";
    }
}

/* ---------------- preset library actions ---------------- */
static int preset_index(void)
{
    PxPresetLib *lib = PxPre_Lib();
    if (!lib || g_ui.sel_preset < 0 || g_ui.sel_preset >= lib->n) return -1;
    return g_ui.sel_preset;
}

void UiPreset_SaveCurrent(void)
{
    PxPreset ps;
    wchar_t name[64];
    char utf8[PX_PRESET_NAME];
    PxPresetLib *lib = PxPre_Lib();
    ModeInfo m;
    MonitorInfo *mi;
    const Look *lk = Eng_GetRequested();

    if (!lib) return;
    if (!UiInputBox(L"Save preset — name", L"", name, 64) || !name[0]) return;
    PxPreset_Neutral(&ps);
    WideCharToMultiByte(CP_UTF8, 0, name, -1, utf8, PX_PRESET_NAME, NULL, NULL);
    snprintf(ps.name, PX_PRESET_NAME, "%s", utf8);
    ps.category = g_ui.preset_cat;
    ps.look = *lk;

    ps.xh_shape = g_ui.xh.shape;   ps.xh_size = g_ui.xh.size;
    ps.xh_gap = g_ui.xh.gap;       ps.xh_thick = g_ui.xh.thick;
    ps.xh_opacity = g_ui.xh.opacity;
    ps.xh_dot = g_ui.xh.center_dot; ps.xh_dot_size = g_ui.xh.dot_size;
    ps.xh_outline = g_ui.xh.outline; ps.xh_outline_th = g_ui.xh.outline_th;
    ps.xh_rot = g_ui.xh.rotation;
    ps.xh_color = (unsigned)g_ui.xh.color;
    ps.xh_ocolor = (unsigned)g_ui.xh.ocolor;

    Modes_Current(&m);
    ps.disp_w = m.w; ps.disp_h = m.h; ps.disp_hz = m.hz;
    mi = Modes_GetMonitor(Modes_CurrentMonitorIndex());
    ps.disp_hdr = mi ? (PxHdr_StateOf(mi) == PX_HDR_ENABLED) : 0;
    ps.monitor_idx = -1;
    ps.created = 0;

    if (PxPre_Add(lib, &ps) < 0) { Ui_Notify(L"Preset library is full (64 presets)"); return; }
    PxPre_Save();
    {
        wchar_t msg[128];
        _snwprintf(msg, 128, L"Preset saved: %s (%s)", name, cat_name(ps.category));
        Ui_Notify(msg);
    }
    Ui_RebuildPanel();
}

void UiPreset_LoadSelected(void)
{
    PxPresetLib *lib = PxPre_Lib();
    int i = preset_index();
    PxPreset *ps;
    if (!lib || i < 0) { Ui_Notify(L"Select a preset first"); return; }
    ps = &lib->p[i];

    switch (ps->category) {
    case PX_PRESET_CROSSHAIR:
        g_ui.xh.shape = clampi(ps->xh_shape, 0, XH_SHAPE_COUNT - 1);
        g_ui.xh.size = clampi(ps->xh_size, 4, 64);
        g_ui.xh.gap = clampi(ps->xh_gap, 0, 32);
        g_ui.xh.thick = clampi(ps->xh_thick, 1, 12);
        g_ui.xh.opacity = clampi(ps->xh_opacity, 10, 100);
        g_ui.xh.center_dot = ps->xh_dot ? 1 : 0;
        g_ui.xh.dot_size = clampi(ps->xh_dot_size, 1, 8);
        g_ui.xh.outline = ps->xh_outline ? 1 : 0;
        g_ui.xh.outline_th = clampi(ps->xh_outline_th, 1, 4);
        g_ui.xh.rotation = clampi(ps->xh_rot, 0, 359);
        g_ui.xh.color = (COLORREF)ps->xh_color;
        g_ui.xh.ocolor = (COLORREF)ps->xh_ocolor;
        Xh_Update(&g_ui.xh);
        Ui_Notify(L"Crosshair preset applied");
        break;
    case PX_PRESET_DISPLAY:
        UiPreset_ApplyDisplaySelected();
        break;
    default:
        Eng_SetLook(&ps->look);          /* sanitises the stored look */
        Ui_Notify(L"Preset loaded as the requested colour state");
        UiSl_Commit();
        break;
    }
    Ui_RebuildPanel();
}

void UiPreset_ApplyDisplaySelected(void)
{
    PxPresetLib *lib = PxPre_Lib();
    int i = preset_index();
    PxPreset *ps;
    if (!lib || i < 0) { Ui_Notify(L"Select a preset first"); return; }
    ps = &lib->p[i];
    if (ps->disp_w <= 0 || ps->disp_h <= 0) {
        Ui_Notify(L"That preset has no display payload");
        return;
    }
    if (ps->monitor_idx >= 0 && ps->monitor_idx < Modes_MonitorCount())
        Modes_SetCurrentMonitor(ps->monitor_idx);
    UiDisplay_ApplySafeRes(ps->disp_w, ps->disp_h, ps->disp_hz);
}

void UiPreset_DuplicateSelected(void)
{
    PxPresetLib *lib = PxPre_Lib();
    int i = preset_index();
    if (!lib || i < 0) { Ui_Notify(L"Select a preset first"); return; }
    if (PxPre_Duplicate(lib, i, NULL) < 0) { Ui_Notify(L"Could not duplicate (library full)"); return; }
    PxPre_Save();
    Ui_Notify(L"Preset duplicated");
    Ui_RebuildPanel();
}

void UiPreset_RenameSelected(void)
{
    PxPresetLib *lib = PxPre_Lib();
    int i = preset_index();
    wchar_t name[80];
    char utf8[PX_PRESET_NAME];
    if (!lib || i < 0) { Ui_Notify(L"Select a preset first"); return; }
    UiUtf8(lib->p[i].name, name, 80);
    if (!UiInputBox(L"Rename preset", name, name, 80) || !name[0]) return;
    WideCharToMultiByte(CP_UTF8, 0, name, -1, utf8, PX_PRESET_NAME, NULL, NULL);
    if (PxPre_Rename(lib, i, utf8) < 0) { Ui_Notify(L"Could not rename the preset"); return; }
    PxPre_Save();
    Ui_Notify(L"Preset renamed");
    Ui_RebuildPanel();
}

void UiPreset_DeleteSelected(void)
{
    PxPresetLib *lib = PxPre_Lib();
    int i = preset_index();
    if (!lib || i < 0) { Ui_Notify(L"Select a preset first"); return; }
    if (lib->p[i].builtin) { Ui_Notify(L"Built-in presets cannot be deleted — duplicate it first"); return; }
    PxPre_Delete(lib, i);
    PxPre_Save();
    if (g_ui.sel_preset >= PxPre_Lib()->n) g_ui.sel_preset = PxPre_Lib()->n - 1;
    Ui_Notify(L"Preset deleted");
    Ui_RebuildPanel();
}

void UiPreset_Export(void)
{
    wchar_t path[MAX_PATH];
    if (!UiSaveFile(path, MAX_PATH, L"PlexusX preset library (*.json)\0*.json\0All files\0*.*\0",
                    L"Export preset library", L"PlexusX_Presets.json")) return;
    if (PxPre_ExportFile(path) == 0) Ui_Notify(L"Preset library exported");
    else Ui_Notify(L"Export failed — check the path");
}

void UiPreset_Import(void)
{
    wchar_t path[MAX_PATH];
    if (!UiOpenFile(path, MAX_PATH, L"PlexusX preset library (*.json)\0*.json\0All files\0*.*\0",
                    L"Import preset library")) return;
    if (PxPre_ImportFile(path) == 0) { Ui_Notify(L"Preset library imported"); Ui_RebuildPanel(); }
    else Ui_Notify(L"Import failed — not a PlexusX preset file");
}

/* ---------------- preset library UI ---------------- */
static void preset_library(void)
{
    PxPresetLib *lib = PxPre_Lib();
    int shown = 0;

    UiW(WT_DIV, 0, 248, 380, 986, 20, L"UNIFIED PRESET LIBRARY  ·  GLOBAL / GAME / DISPLAY / CROSSHAIR");
    UiW(WT_GHOST, ID_B_PRESET_CAT_GLOBAL,  248,  404, 118, 30, L"Global");
    UiW(WT_GHOST, ID_B_PRESET_CAT_GAME,    374,  404, 118, 30, L"Game");
    UiW(WT_GHOST, ID_B_PRESET_CAT_DISPLAY, 500,  404, 118, 30, L"Display");
    UiW(WT_GHOST, ID_B_PRESET_CAT_CROSS,   626,  404, 118, 30, L"Crosshair");
    UiW(WT_GHOST, ID_B_PRESET_SAVE,        760,  404, 140, 30, L"Save current…");
    UiW(WT_GHOST, ID_B_PRESET_LOAD,        908,  404, 100, 30, L"Load");
    UiW(WT_GHOST, ID_B_PRESET_DUPLICATE,  1016,  404, 100, 30, L"Duplicate");
    UiW(WT_GHOST, ID_B_PRESET_DELETE,     1124,  404, 110, 30, L"Delete");
    UiW(WT_GHOST, ID_B_PRESET_EXPORT,      760,  440, 130, 28, L"Export library…");
    UiW(WT_GHOST, ID_B_PRESET_IMPORT,      898,  440, 130, 28, L"Import library…");
    UiW(WT_GHOST, ID_B_PRESET_RENAME,     1036,  440, 100, 28, L"Rename");
    UiW(WT_GHOST, ID_B_PRESET_APPLY_DISPLAY, 1144, 440, 90, 28, L"Apply");

    if (!lib) return;
    for (int i = 0; i < lib->n && shown < 8; i++) {
        PxPreset *ps = &lib->p[i];
        Widget *card;
        wchar_t name[80], sub[96];
        if (ps->category != g_ui.preset_cat) continue;
        UiUtf8(ps->name, name, 80);
        card = UiW(WT_LOOK_CARD, ID_PRESET_CARD_BASE + i, 248 + (shown % 4) * 248, 480 + (shown / 4) * 78, 234, 70, name);
        card->state = (g_ui.sel_preset == i);
        if (ps->category == PX_PRESET_DISPLAY)
            _snwprintf(sub, 96, L"%d × %d @ %d Hz", ps->disp_w, ps->disp_h, ps->disp_hz);
        else if (ps->category == PX_PRESET_CROSSHAIR)
            _snwprintf(sub, 96, L"shape %d · %d px · %d%% opacity", ps->xh_shape, ps->xh_size, ps->xh_opacity);
        else
            _snwprintf(sub, 96, L"sat %d%% · vib %d%% · gamma %.2f", (int)ps->look.sat, (int)ps->look.vibrance,
                       (double)ps->look.gamma);
        UiWin(card->sub, 64, sub);
        UiWin(card->val, 48, ps->builtin ? L"built-in" : L"user");
        shown++;
    }
    if (shown == 0)
        UiW(WT_LABEL, 0, 248, 480, 986, 20, L"No presets in this category yet — SAVE CURRENT adds one.");

    {
        wchar_t buf[160];
        _snwprintf(buf, 160, L"%d preset%s stored · schema %d · %s", lib->n, lib->n == 1 ? L"" : L"s",
                   lib->schema, PxPre_Corrupt() ? L"the previous file was corrupt and has been set aside"
                                                : L"file loaded cleanly");
        UiW(WT_LABEL, 0, 248, 646, 986, 18, buf);
    }
}

/* ---------------- Phone Control ---------------- */
void UiPhone_Build(void)
{
    int running = Phone_IsRunning();
    int pin = Phone_GetPin();
    int clients = Phone_GetClientCount();
    wchar_t *url = Phone_SummaryUrl();
    wchar_t buf[192];

    UiW(WT_HEAD, 0, 248, 62, 700, 30, L"Phone Control");
    UiW(WT_LABEL, 0, 248, 92, 986, 18,
        L"Control saturation, vibrance, brightness, contrast, the game profile and the engine on/off from a phone on the same network.");

    {
        Widget *t = UiW(WT_TOGGLE, ID_T_PHONE, 248, 124, 340, 30, L"LAN server enabled");
        t->state = running;
        UiW(WT_GHOST, ID_B_PHONE_TOGGLE, 600, 124, 180, 30, running ? L"Stop server" : L"Start server");
        UiW(WT_GHOST, ID_B_PHONE_NEW_PIN, 790, 124, 180, 30, L"New PIN");
    }

    if (running) {
        UiW(WT_DIV, 0, 248, 170, 986, 20, L"PAIRING");
        {
            Widget *k = UiWCard(248, 194, 322, 96, L"OPEN ON YOUR PHONE",
                                url && url[0] ? url : L"http://<this-pc>:8777/", L"same Wi-Fi / LAN only");
            (void)k;
        }
        {
            Widget *k = UiWCard(578, 194, 322, 96, L"PAIRING PIN", L"—", L"single use · rotates on every pairing");
            _snwprintf(buf, 192, L"%04d", pin);
            UiWin(k->val, 48, buf);
        }
        {
            Widget *k = UiWCard(908, 194, 326, 96, L"CONNECTED DEVICES", L"—", L"—");
            _snwprintf(buf, 192, L"%d of 8", clients);
            UiWin(k->val, 48, buf);
            UiWin(k->sub, 64, clients ? L"a device is controlling this PC" : L"no device paired right now");
        }
        UiW(WT_DIV, 0, 248, 306, 986, 20, L"WHAT A PAIRED PHONE CAN CHANGE");
        UiW(WT_LABEL, 0, 248, 330, 986, 18,
            L"Saturation · vibrance · brightness · contrast · gamma · temperature · game profile selection · engine on/off · crosshair toggle.");
        UiW(WT_LABEL, 0, 248, 352, 986, 18,
            L"Nothing else: no shell, no file access, no display-mode changes, and every command is clamped to the same safe ranges as the desktop UI.");
    } else {
        UiW(WT_DIV, 0, 248, 170, 986, 20, L"SERVER OFF");
        UiW(WT_LABEL, 0, 248, 194, 986, 20,
            L"Start the server to show a pairing URL and PIN. The server binds the LAN interface only and never talks to the internet.");
    }

    UiW(WT_DIV, 0, 248, 690, 986, 20, L"SECURITY");
    UiW(WT_LABEL, 0, 248, 714, 986, 18,
        L"4-digit PIN is single use and trades for a random 128-bit session token. Five wrong attempts lock pairing for 30 seconds; sessions expire after 8 idle hours.");
}

/* ---------------- Settings ---------------- */
void UiSettings_Build(void)
{
    const GpuInfo *gpu = Dm_GpuInfo();
    wchar_t buf[192];
    int tray = PxSetGetInt("ui", "minimize_tray", 0);
    int notify = PxSetGetInt("ui", "notify", 1);
    int startup_state = PxSetGetInt("engine", "startup_state", 2);
    int log_level = PxSetGetInt("diag", "log_level", 1);
    int theme = PxSetGetInt("ui", "theme", 0);

    UiW(WT_HEAD, 0, 248, 62, 700, 30, L"Settings");
    UiW(WT_LABEL, 0, 248, 92, 986, 18,
        L"Startup, detection, appearance and diagnostics. Every change is written to the AppData configuration immediately.");

    /* ---- startup & behaviour ---- */
    UiW(WT_DIV, 0, 248, 126, 486, 20, L"STARTUP & BEHAVIOUR");
    {
        Widget *t;
        t = UiW(WT_TOGGLE, ID_B_SET_STARTWIN, 248, 150, 486, 30, L"Start PlexusX with Windows");
        t->state = Ui_StartupEnabled();
        t = UiW(WT_TOGGLE, ID_B_SET_TRAY_MIN, 248, 186, 486, 30, L"Minimize to the notification area");
        t->state = tray;
        t = UiW(WT_TOGGLE, ID_B_SET_AUTODETECT, 248, 222, 486, 30, L"Automatic game detection");
        t->state = Prof_Detect();
        t = UiW(WT_TOGGLE, ID_B_SET_AUTOSWITCH, 248, 258, 486, 30, L"Automatic profile switching");
        t->state = Prof_GetAutoRestore();
        t = UiW(WT_TOGGLE, ID_B_SET_NOTIFY, 248, 294, 486, 30, L"Show notifications");
        t->state = notify;
    }
    {
        const wchar_t *states[3] = { L"Off", L"On", L"Restore last state" };
        Widget *b = UiW(WT_GHOST, ID_B_SET_ENGINE_START, 248, 330, 320, 30, L"Colour engine at startup");
        _snwprintf(buf, 192, L"%s", states[clampi(startup_state, 0, 2)]);
        UiWin(b->val, 48, buf);
        UiW(WT_LABEL, 0, 578, 336, 156, 18, states[clampi(startup_state, 0, 2)]);
        UiW(WT_GHOST, ID_B_SET_DEFAULT_MONITOR, 248, 366, 236, 30, L"Set current monitor as default");
        UiW(WT_LABEL, 0, 492, 372, 242, 18, L"colour applies to every output");
    }

    /* ---- appearance ---- */
    UiW(WT_DIV, 0, 248, 410, 486, 20, L"APPEARANCE");
    {
        Widget *t = UiW(WT_TOGGLE, ID_T_GLASS, 248, 434, 486, 30, L"Glass window effect");
        t->state = Ui_GlassEnabled();
        t = UiW(WT_TOGGLE, ID_T_REDUCE_MOTION, 248, 470, 486, 30, L"Reduce motion");
        t->state = Ui_ReduceMotion();
    }
    UiW(WT_LABEL, 0, 248, 504, 120, 18, L"Background");
    UiW(Ui_BgMode() == 0 ? WT_PRIMARY : WT_GHOST, ID_B_BG_NONE,     248, 526, 140, 30, L"None");
    UiW(Ui_BgMode() == 1 ? WT_PRIMARY : WT_GHOST, ID_B_BG_ABSTRACT, 396, 526, 150, 30, L"Abstract mesh");
    UiW(Ui_BgMode() == 2 ? WT_PRIMARY : WT_GHOST, ID_B_BG_IMAGE,    554, 526, 180, 30, L"Image (background.bmp)");
    UiW(WT_LABEL, 0, 248, 562, 120, 18, L"Animation");
    UiW(Ui_AnimLevel() == 2 ? WT_PRIMARY : WT_GHOST, ID_B_ANIM_ON,      248, 584, 110, 30, L"On");
    UiW(Ui_AnimLevel() == 1 ? WT_PRIMARY : WT_GHOST, ID_B_ANIM_REDUCED, 366, 584, 110, 30, L"Reduced");
    UiW(Ui_AnimLevel() == 0 ? WT_PRIMARY : WT_GHOST, ID_B_ANIM_OFF,     484, 584, 110, 30, L"Off");
    {
        Widget *b = UiW(WT_GHOST, ID_B_SET_THEME, 248, 620, 346, 30,
                        theme ? L"Theme: OLED black" : L"Theme: dark");
        (void)b;
    }

    /* ---- diagnostics & maintenance ---- */
    UiW(WT_DIV, 0, 746, 126, 488, 20, L"DIAGNOSTICS & MAINTENANCE");
    {
        const wchar_t *levels[3] = { L"Errors only", L"Normal", L"Verbose" };
        Widget *b = UiW(WT_GHOST, ID_B_SET_LOGGING, 746, 150, 262, 30, L"Event log level");
        _snwprintf(buf, 192, L"%s", levels[clampi(log_level, 0, 2)]);
        UiWin(b->val, 48, buf);
        UiW(WT_LABEL, 0, 1018, 156, 216, 18, levels[clampi(log_level, 0, 2)]);
    }
    UiW(WT_GHOST, ID_B_DIAG_COPY,   746, 186, 150, 30, L"Copy report");
    UiW(WT_GHOST, ID_B_DIAG_EXPORT, 906, 186, 150, 30, L"Export report");
    UiW(WT_GHOST, ID_B_OPEN_SETTINGS_DIR, 1066, 186, 168, 30, L"Open AppData folder");

    UiW(WT_DIV, 0, 746, 226, 488, 20, L"SAFE RESET");
    UiW(WT_GHOST,  ID_B_RESET_COLOR,   746, 250, 150, 30, L"Reset colour");
    UiW(WT_GHOST,  ID_B_MODE_NATIVE,   906, 250, 160, 30, L"Restore display mode");
    UiW(WT_ACCENT, ID_B_EMERGENCY_RESET, 1076, 250, 158, 30, L"Safe reset now");
    UiW(WT_GHOST,  ID_B_SET_RESET,     746, 286, 200, 30, L"Reset all settings");
    UiW(WT_LABEL, 0, 956, 292, 278, 18, L"profiles & presets are kept");

    UiW(WT_DIV, 0, 746, 326, 488, 20, L"HOTKEYS");
    UiW(WT_LABEL, 0, 746, 350, 488, 16, L"Ctrl+Alt+↑ / ↓  saturation");
    UiW(WT_LABEL, 0, 746, 368, 488, 16, L"Ctrl+Alt+0  reset colour");
    UiW(WT_LABEL, 0, 746, 386, 488, 16, L"Ctrl+Alt+X  crosshair on/off");
    UiW(WT_LABEL, 0, 746, 404, 488, 16, L"Ctrl+Alt+E  engine on/off");
    UiW(WT_LABEL, 0, 746, 422, 488, 16, L"Ctrl+Alt+Shift+R  emergency safe reset");

    /* ---- system facts ---- */
    UiW(WT_DIV, 0, 746, 452, 488, 20, L"SYSTEM");
    {
        Widget *k = UiW(WT_KV, 0, 746, 476, 488, 18, L"GPU");
        UiWin(k->val, 48, gpu && gpu->name[0] ? gpu->name : L"not reported");
    }
    {
        Widget *k = UiW(WT_KV, 0, 746, 496, 488, 18, L"Driver");
        UiWin(k->val, 48, gpu && gpu->driver_ver[0] ? gpu->driver_ver : L"not reported");
    }
    {
        Widget *k = UiW(WT_KV, 0, 746, 516, 488, 18, L"Magnification matrix");
        UiWin(k->val, 48, gpu && gpu->mag_available ? L"available" : L"unavailable");
    }
    {
        Widget *k = UiW(WT_KV, 0, 746, 536, 488, 18, L"GPU gamma ramp");
        UiWin(k->val, 48, gpu && gpu->gamma_available ? L"available" : L"unavailable");
    }
    {
        Widget *k = UiW(WT_KV, 0, 746, 556, 488, 18, L"Configuration");
        _snwprintf(buf, 192, L"%s%s", PxSet_CfgCorrupt() ? L"config was corrupt, defaults restored" : L"config loaded",
                   PxSet_ProfCorrupt() ? L" · profile library was corrupt" : L"");
        UiWin(k->val, 48, buf);
    }
    {
        Widget *k = UiW(WT_KV, 0, 746, 576, 488, 18, L"Version");
        _snwprintf(buf, 192, L"v%s · %s", PX_VERSION, PX_BUILD_DATE);
        UiWin(k->val, 48, buf);
    }

    preset_library();
}
