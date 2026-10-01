/* PlexusX — GAMES ▸ Game Profiles / Active Game / Custom Games.
 *
 * The profile library is the real game_preset_manager table: 18 built-in titles
 * with launcher/shipping executable aliases, plus user-created games.  The pages
 * never copy that table — they read it and edit it through the Prof_* API, so
 * the detection matcher, the automation flags and the JSON import/export all see
 * exactly the same data the UI shows.
 */
#include "ui_internal.h"
#include "../ui_theme.h"
#include <time.h>

static void time_ago(unsigned long unix_sec, wchar_t *out, size_t cap)
{
    unsigned long now = (unsigned long)time(NULL);
    unsigned long d;
    if (!unix_sec) { UiWin(out, cap, L"never activated"); return; }
    d = now > unix_sec ? now - unix_sec : 0;
    if (d < 3600)      _snwprintf(out, cap, L"last used %lu min ago", d / 60);
    else if (d < 86400) _snwprintf(out, cap, L"last used %lu h ago", d / 3600);
    else                _snwprintf(out, cap, L"last used %lu d ago", d / 86400);
}

static void profile_card(int slot, int idx)
{
    Profile *p = Prof_Get(idx);
    Widget *card;
    wchar_t sub[128], val[64];
    if (!p) return;
    card = UiW(WT_GAME_CARD, ID_GAME_CARD_BASE + idx, 248, 150 + slot * 62, 470, 56, p->name);
    card->state = (idx == Prof_ActiveIndex()) || (idx == g_ui.sel_game);
    _snwprintf(sub, 128, L"%s%s%s  ·  %s", p->exe[0] ? p->exe : L"no executable yet",
               p->alias_count ? L" +" : L"", p->alias_count ? L"aliases" : L"", p->tag);
    UiWin(card->sub, 64, sub);
    if (!p->enabled) UiWin(val, 64, L"disabled");
    else if (p->auto_apply) UiWin(val, 64, idx == Prof_ActiveIndex() ? L"active · auto" : L"auto-apply");
    else UiWin(val, 64, L"manual");
    UiWin(card->val, 48, val);
}

static void exe_rows(Profile *p, int y)
{
    int n = 1 + p->alias_count;
    for (int i = 0; i < n && i < 6; i++) {
        const wchar_t *name = (i == 0) ? p->exe : p->exe_alias[i - 1];
        Widget *row = UiW(WT_ROW, ID_GAME_EXE_SLOT_BASE + i, 248, y + i * 34, 470, 30, name);
        row->state = (g_ui.sel_exe_slot == i);
        UiWin(row->sub, 64, i == 0 ? L"primary executable (processed as a base name)"
                                   : L"additional executable name");
        UiWin(row->val, 48, i == 0 ? L"primary" : L"alias");
    }
    (void)n;
}

/* ---------------- Game Profiles library ---------------- */
void UiGames_Build(void)
{
    int n = Prof_Count();
    int idx = g_ui.sel_game;
    Profile *p;
    wchar_t buf[192];

    if (idx < 0 || idx >= n) { idx = Prof_ActiveIndex(); g_ui.sel_game = idx; }
    p = Prof_Get(idx);

    UiW(WT_HEAD, 0, 248, 62, 700, 30, L"Game Profiles");
    UiW(WT_LABEL, 0, 248, 92, 986, 18,
        L"Each profile carries its own colour look, executable matches, monitor target and auto-apply rules.");
    UiW(WT_PRIMARY, ID_B_GAME_ADD, 1050, 62, 184, 40, L"+ ADD GAME");

    if (n == 0) {
        UiW(WT_LABEL, 0, 248, 150, 700, 20, L"No profiles loaded — press ADD GAME or restore the built-ins.");
        UiW(WT_GHOST, ID_B_GAME_RESET_BUILTINS, 248, 180, 260, 40, L"Restore Built-in Profiles");
        return;
    }

    UiW(WT_DIV, 0, 248, 122, 470, 20, L"LIBRARY");
    for (int i = 0, slot = 0; i < n && slot < 8; i++, slot++) profile_card(slot, i);

    if (!p) return;

    /* ---- inspector ---- */
    UiW(WT_DIV, 0, 740, 122, 494, 20, L"SELECTED PROFILE");
    {
        Widget *k = UiWCard(740, 150, 494, 82, L"TITLE", p->name, p->tag);
        _snwprintf(buf, 192, L"%s%s%s", p->exe[0] ? p->exe : L"no executable yet",
                   p->exe_path[0] ? L"  ·  custom path: " : L"",
                   p->exe_path[0] ? p->exe_path : L"");
        UiWin(k->sub, 64, buf);
    }
    UiW(WT_LABEL, 0, 740, 240, 494, 18, p->is_custom ? L"User-created profile" : L"Built-in profile");

    UiW(WT_DIV, 0, 740, 266, 494, 20, L"SUB-MODES (each carries a complete look)");
    for (int s = 0; s < p->sub_count && s < 12; s++) {
        int col = s % 3, row = s / 3;
        Widget *b = UiW(s == p->active_sub ? WT_PRIMARY : WT_GHOST, ID_GAME_SUB_BASE + s,
                        740 + col * 166, 292 + row * 42, 158, 36, p->sub[s].name);
        (void)b;
    }

    /* quick automation toggles for this profile */
    {
        Widget *t;
        UiW(WT_DIV, 0, 740, 430, 494, 20, L"AUTOMATION");
        t = UiW(WT_TOGGLE, ID_B_GAME_ENABLE, 740, 456, 240, 30, L"Profile enabled");
        t->state = p->enabled;
        t = UiW(WT_TOGGLE, ID_B_GAME_AUTOAPPLY, 992, 456, 240, 30, L"Auto-apply on launch");
        t->state = p->auto_apply;
        t = UiW(WT_TOGGLE, ID_B_GAME_AUTORESTORE, 740, 492, 240, 30, L"Restore desktop on exit");
        t->state = p->auto_restore;
        t = UiW(WT_TOGGLE, ID_B_GAME_APPLY_DISPLAY, 992, 492, 240, 30, L"Apply display preference");
        t->state = p->apply_display;
    }
    {
        UiW(WT_DIV, 0, 740, 528, 494, 20, L"MONITOR TARGET");
        {
            Widget *b = UiW(p->monitor_idx < 0 ? WT_PRIMARY : WT_GHOST, ID_MONITOR_TARGET_BASE, 740, 552, 150, 32,
                            p->monitor_idx < 0 ? L"All / primary" : L"Specific monitor");
            (void)b;
        }
        for (int i = 0; i < Modes_MonitorCount() && i < 4; i++) {
            MonitorInfo *mi = Modes_GetMonitor(i);
            Widget *b = UiW(p->monitor_idx == i ? WT_PRIMARY : WT_GHOST, ID_MONITOR_TARGET_BASE + 1 + i,
                            900 + i * 86, 552, 80, 32, mi ? mi->friendly : L"Display");
            (void)b;
        }
        UiW(WT_LABEL, 0, 740, 588, 494, 18,
            L"The colour engine drives every output; the monitor target narrows display-mode changes.");
    }

    /* actions */
    {
        wchar_t last[64];
        time_ago(p->last_activated, last, 64);
        UiW(WT_LABEL, 0, 740, 616, 494, 18, last);
    }
    UiW(WT_PRIMARY, ID_B_GAME_ACTIVATE,  740, 632, 150, 34, L"Activate Now");
    UiW(WT_GHOST,   ID_B_GAME_SAVE_LOOK, 900, 632, 150, 34, L"Save Look Into");
    UiW(WT_GHOST,   ID_B_GAME_FAV,       1060, 632, 174, 34, p->favorite ? L"★ Favorite" : L"☆ Add to favorites");
    UiW(WT_GHOST,   ID_B_GAME_DUPLICATE, 740, 676, 116, 30, L"Duplicate");
    UiW(WT_GHOST,   ID_B_GAME_RENAME,    864, 676, 100, 30, L"Rename");
    UiW(WT_GHOST,   ID_B_GAME_DELETE,    972, 676, 90, 30, L"Delete");
    UiW(WT_GHOST,   ID_B_GAME_EXPORT,   1070, 676, 86, 30, L"Export");
    UiW(WT_GHOST,   ID_B_GAME_IMPORT,   1164, 676, 70, 30, L"Import");

    /* library footer */
    UiW(WT_LABEL, 0, 248, 646, 470, 18, L"Select a card to inspect and edit it on the right.");
    UiW(WT_GHOST, ID_B_GAME_RESET_BUILTINS, 248, 672, 220, 30, L"Restore Built-ins");
    UiW(WT_GHOST, ID_B_GAME_ADD,            478, 672, 240, 30, L"+ Add a custom game");
}

/* ---------------- Active Game ---------------- */
void UiActiveGame_Build(void)
{
    const PxGameDisplayState *gs = Prof_GameState();
    Profile *ap = Prof_Get(Prof_ActiveIndex());
    const Look *req = Eng_GetRequested();
    ModeInfo m;
    MonitorInfo *mi;
    wchar_t buf[192];
    char ascii[128];
    int running = gs->detected;

    Modes_Current(&m);
    mi = Modes_GetMonitor(Modes_CurrentMonitorIndex());

    UiW(WT_HEAD, 0, 248, 62, 700, 30, L"Active Game");
    UiW(WT_LABEL, 0, 248, 92, 986, 18,
        L"Event-driven foreground detection (WinEvent hooks, no polling of the game). What is shown here is exactly what the engine matched.");

    {
        Widget *t = UiW(WT_TOGGLE, ID_T_DETECT, 248, 122, 300, 30, L"Automatic game detection");
        t->state = Prof_Detect();
        t = UiW(WT_TOGGLE, ID_T_AUTO_RESTORE, 560, 122, 340, 30, L"Restore desktop look when the game exits");
        t->state = Prof_GetAutoRestore();
    }

    /* detection state */
    {
        int tone = running ? (gs->game_output == PX_GAMEOUT_ACTIVE ? PX_CHIP_OK :
                              gs->game_output == PX_GAMEOUT_LIMITED ? PX_CHIP_WARN : PX_CHIP_INFO)
                           : PX_CHIP_NEUTRAL;
        wchar_t text[96];
        MultiByteToWideChar(CP_UTF8, 0, px_pres_name(gs->presentation), -1, buf, 190);
        MultiByteToWideChar(CP_UTF8, 0, px_gameout_name(gs->game_output), -1, text, 96);
        UiW(WT_DIV, 0, 248, 168, 986, 20, L"CURRENT APPLICATION / DETECTION STATUS");
        UiWChip(248, 194, 240, running ? (gs->active ? L"GAME IN FOREGROUND" : L"GAME DETECTED") : L"NO GAME DETECTED", tone);
        {
            Widget *k = UiWCard(248, 226, 322, 92, L"CURRENT APPLICATION", L"—", L"—");
            if (running) {
                UiWin(k->val, 48, gs->exe);
                _snwprintf(buf, 192, L"pid %lu  ·  %s", gs->pid, buf);
                UiWin(k->sub, 64, buf);
            } else {
                const wchar_t *fg = Prof_CurrentForeground();
                UiWin(k->val, 48, fg && fg[0] ? fg : L"Desktop");
                UiWin(k->sub, 64, L"no registered game executable in the foreground");
            }
        }
        {
            Widget *k = UiWCard(578, 226, 322, 92, L"CURRENT GAME / PROFILE", L"—", L"—");
            if (ap && running) {
                UiWin(k->val, 48, ap->name);
                _snwprintf(buf, 192, L"%s  ·  sub-mode %s", ap->auto_apply ? L"auto-applied" : L"manual",
                           ap->sub[ap->active_sub].name);
                UiWin(k->sub, 64, buf);
            } else {
                UiWin(k->val, 48, L"Global look");
                UiWin(k->sub, 64, ap ? L"profile selected but not detected in the foreground"
                                     : L"no profile is active");
            }
        }
        {
            Widget *k = UiWCard(908, 226, 326, 92, L"OUTPUT PATH", L"—", L"—");
            UiWin(k->val, 48, text);
            UiWin(k->sub, 64, gs->detected
                              ? (gs->game_output == PX_GAMEOUT_LIMITED
                                 ? L"fullscreen surface: tone curves reach the scanout, chroma matrix may not"
                                 : gs->game_output == PX_GAMEOUT_ACTIVE
                                   ? L"composited window: the full colour matrix applies"
                                   : L"the engine is not reaching this window")
                              : L"start a game and the event-driven detector will match it");
        }
    }

    /* what the engine is doing for it */
    UiW(WT_DIV, 0, 248, 336, 986, 20, L"ENGINE STATE FOR THE FOREGROUND APPLICATION");
    {
        const PxEffectiveState *e = UiStatus();
        Widget *k = UiWCard(248, 362, 322, 92, L"STATUS", L"—", L"—");
        MultiByteToWideChar(CP_UTF8, 0, PxStatus_Name(e->status), -1, buf, 190);
        UiWin(k->val, 48, buf);
        MultiByteToWideChar(CP_UTF8, 0, e->reason, -1, buf, 190);
        UiWin(k->sub, 64, buf);
    }
    {
        Widget *k = UiWCard(578, 362, 322, 92, L"APPLIED LOOK", L"—", L"—");
        _snwprintf(buf, 192, L"sat %d%% · vib %d%% · gamma %.2f", (int)req->sat, (int)req->vibrance, (double)req->gamma);
        UiWin(k->val, 48, buf);
        _snwprintf(buf, 192, L"temp %dK · shadow %d%% · highlight %d%%", (int)req->temp,
                   (int)req->shadows, (int)req->highlights);
        UiWin(k->sub, 64, buf);
    }
    {
        Widget *k = UiWCard(908, 362, 326, 92, L"DISPLAY UNDER THE GAME", L"—", L"—");
        _snwprintf(buf, 192, L"%d × %d @ %d Hz", m.w, m.h, m.hz);
        UiWin(k->val, 48, buf);
        {
            int hdr = mi ? PxHdr_StateOf(mi) : PX_HDR_DISABLED;
            int hdr_tone = PxHdr_Tone(hdr);
            (void)hdr_tone;
            snprintf(ascii, sizeof ascii, "%s", PxHdr_Name(hdr));
            MultiByteToWideChar(CP_UTF8, 0, ascii, -1, buf, 190);
            UiWin(k->sub, 64, buf);
        }
    }

    /* switching behaviour */
    UiW(WT_DIV, 0, 248, 470, 986, 20, L"SWITCHING BEHAVIOUR");
    UiW(WT_LABEL, 0, 248, 494, 300, 18, L"Apply delay after a launch event");
    {
        int d = Prof_GetDelayMs();
        UiW(d == 0 ? WT_PRIMARY : WT_GHOST,    ID_B_DELAY_0,    560, 490, 100, 30, L"0 ms");
        UiW(d == 500 ? WT_PRIMARY : WT_GHOST,  ID_B_DELAY_500,  668, 490, 100, 30, L"500 ms");
        UiW(d == 1000 ? WT_PRIMARY : WT_GHOST, ID_B_DELAY_1000, 776, 490, 100, 30, L"1000 ms");
        UiW(d == 2000 ? WT_PRIMARY : WT_GHOST, ID_B_DELAY_2000, 884, 490, 100, 30, L"2000 ms");
    }
    UiW(WT_LABEL, 0, 248, 528, 986, 34,
        L"A short delay avoids fighting a game's own startup display changes. Detection itself is event-driven: no game is polled, hooked or modified.");

    UiW(WT_PRIMARY, ID_B_GAME_ACTIVATE, 248, 578, 220, 40, L"Activate Selected Profile");
    UiW(WT_GHOST,   ID_B_GAME_CLEAR,    478, 578, 220, 40, L"Return to Global Look");
    UiW(WT_GHOST,   ID_B_OPEN_GAME,     708, 578, 220, 40, L"Open Profile Library");

    UiW(WT_DIV, 0, 248, 636, 986, 20, L"EVENT HISTORY  ·  NEWEST FIRST");
    {
        const PxLog *log = Eng_LogRing();
        if (log && log->count > 0) {
            for (int n = 0; n < 4 && n < log->count; n++) {
                int li = (log->head - 1 - n + PXLOG_CAP) % PXLOG_CAP;
                wchar_t tag[24], msg[200], line[300];
                MultiByteToWideChar(CP_UTF8, 0, log->e[li].tag, -1, tag, 24);
                MultiByteToWideChar(CP_UTF8, 0, log->e[li].msg, -1, msg, 200);
                _snwprintf(line, 300, L"%s  ·  %s", tag, msg);
                UiW(WT_LABEL, 0, 248, 660 + n * 18, 986, 16, line);
            }
        } else {
            UiW(WT_LABEL, 0, 248, 660, 986, 18, L"No events recorded yet.");
        }
    }
}

/* ---------------- Custom Games ---------------- */
void UiCustomGames_Build(void)
{
    int n = Prof_Count();
    int shown = 0;
    Profile *p = Prof_Get(g_ui.sel_game);
    wchar_t buf[192];

    UiW(WT_HEAD, 0, 248, 62, 700, 30, L"Custom Games");
    UiW(WT_LABEL, 0, 248, 92, 986, 18,
        L"Games that are not in the built-in library: point PlexusX at the executable and it will be detected like any other title.");
    UiW(WT_PRIMARY, ID_B_GAME_ADD, 1050, 62, 184, 40, L"+ ADD GAME");

    if (n == 0) {
        UiW(WT_LABEL, 0, 248, 150, 986, 20, L"No profiles at all — add a game to begin.");
        return;
    }

    UiW(WT_DIV, 0, 248, 122, 470, 20, L"CUSTOM LIBRARY");
    for (int i = 0; i < n && shown < 8; i++) {
        Profile *q = Prof_Get(i);
        if (!q || !q->is_custom) continue;
        profile_card(shown, i);
        shown++;
    }
    if (shown == 0)
        UiW(WT_LABEL, 0, 248, 150, 470, 20, L"No custom games yet — the built-in library covers 18 titles.");

    /* editor for the selected profile */
    p = Prof_Get(g_ui.sel_game);
    if (!p) return;
    UiW(WT_DIV, 0, 740, 122, 494, 20, L"SELECTED PROFILE");
    {
        Widget *k = UiWCard(740, 150, 494, 82, L"TITLE", p->name, p->tag);
        _snwprintf(buf, 192, L"%s", p->exe_path[0] ? p->exe_path : (p->exe[0] ? p->exe : L"no executable yet"));
        UiWin(k->sub, 64, buf);
    }
    UiW(WT_DIV, 0, 740, 245, 494, 20, L"EXECUTABLE NAMES  ·  click a row to select it");
    exe_rows(p, 270);
    UiW(WT_GHOST, ID_B_GAME_EXE_ADD,  740, 476, 150, 32, L"Add exe name");
    UiW(WT_GHOST, ID_B_GAME_EXE_DEL,  900, 476, 150, 32, L"Remove selected");
    UiW(WT_GHOST, ID_B_GAME_PATH_SET, 1060, 476, 174, 32, L"Set executable path…");
    UiW(WT_LABEL, 0, 740, 514, 494, 34,
        L"A full path pins a specific installation; the base name still drives detection, so store, launcher and shipping variants all match.");

    UiW(WT_DIV, 0, 740, 556, 494, 20, L"BEHAVIOUR");
    {
        Widget *t;
        t = UiW(WT_TOGGLE, ID_B_GAME_ENABLE, 740, 572, 240, 28, L"Enabled");
        t->state = p->enabled;
        t = UiW(WT_TOGGLE, ID_B_GAME_AUTOAPPLY, 992, 572, 240, 28, L"Auto-apply");
        t->state = p->auto_apply;
        t = UiW(WT_TOGGLE, ID_B_GAME_AUTORESTORE, 740, 604, 240, 28, L"Restore on exit");
        t->state = p->auto_restore;
        t = UiW(WT_TOGGLE, ID_B_GAME_APPLY_DISPLAY, 992, 604, 240, 28, L"Apply display mode");
        t->state = p->apply_display;
    }
    UiW(WT_GHOST, ID_B_GAME_RENAME,           740,  638, 140, 30, L"Rename");
    UiW(WT_GHOST, ID_B_GAME_SAVE_LOOK,        890,  638, 160, 30, L"Save current look");
    UiW(WT_GHOST, ID_B_GAME_DELETE,           1060, 638, 174, 30, L"Delete profile");
    UiW(WT_GHOST, ID_B_GAME_EXPORT,           740,  674, 140, 30, L"Export");
    UiW(WT_GHOST, ID_B_GAME_IMPORT,           890,  674, 160, 30, L"Import profile");
    UiW(WT_GHOST, ID_B_GAME_RESET_BUILTINS,   1060, 674, 174, 30, L"Restore built-ins");
}

/* ---------------- actions ---------------- */
void UiGames_AddCustom(void)
{
    wchar_t name[64];
    wchar_t exe[96];
    int idx;
    if (!UiInputBox(L"New game profile — title", L"", name, 64)) return;
    idx = Prof_Create(name, L"", L"Custom", Eng_GetRequested());
    if (idx < 0) { Ui_Notify(L"Could not create the profile (library full)"); return; }
    if (UiInputBox(L"Executable base name (for example MyGame-Win64-Shipping.exe)", L"", exe, 96) && exe[0])
        Prof_AddExeName(idx, exe);
    g_ui.sel_game = idx;
    {
        wchar_t msg[160];
        _snwprintf(msg, 160, L"%s added — add every executable name the game uses", name);
        Ui_Notify(msg);
    }
    Ui_RebuildPanel();
}

void UiGames_DuplicateSelected(void)
{
    Profile *p = Prof_Get(g_ui.sel_game);
    wchar_t name[80];
    int idx;
    if (!p) return;
    _snwprintf(name, 80, L"%s Copy", p->name);
    idx = Prof_DuplicateProfile(g_ui.sel_game, name);
    if (idx < 0) { Ui_Notify(L"Could not duplicate the profile (library full)"); return; }
    g_ui.sel_game = idx;
    Ui_Notify(L"Profile duplicated");
    Ui_RebuildPanel();
}

void UiGames_DeleteSelected(void)
{
    Profile *p = Prof_Get(g_ui.sel_game);
    wchar_t typed[160];
    if (!p) return;
    if (!p->is_custom) {
        Ui_Notify(L"Built-in profiles cannot be deleted — disable the profile instead");
        return;
    }
    if (!UiInputBox(L"Type the profile name exactly to delete it", L"", typed, 160)) return;
    if (!PxW_EqCI(typed, p->name)) { Ui_Notify(L"Name did not match — nothing was deleted"); return; }
    Prof_Delete(g_ui.sel_game);
    if (g_ui.sel_game >= Prof_Count()) g_ui.sel_game = Prof_Count() - 1;
    Ui_Notify(L"Profile deleted");
    Ui_RebuildPanel();
}

void UiGames_RenameSelected(void)
{
    Profile *p = Prof_Get(g_ui.sel_game);
    wchar_t name[64];
    if (!p) return;
    if (!UiInputBox(L"Rename profile", p->name, name, 64)) return;
    if (Prof_Rename(g_ui.sel_game, name) == 0) Ui_Notify(L"Profile renamed");
    else Ui_Notify(L"That name cannot be used");
    Ui_RebuildPanel();
}

void UiGames_ActivateSelected(void)
{
    int idx = g_ui.sel_game >= 0 ? g_ui.sel_game : Prof_ActiveIndex();
    Profile *p = Prof_Get(idx);
    if (!p) { Ui_Notify(L"No profile selected"); return; }
    Prof_SetActiveIndex(idx);
    Ui_LoadLook(&p->sub[p->active_sub].look);
    Prof_MarkActivated(idx);
    Eng_SetMode(PX_CSMODE_GAME, idx, p->active_sub);
    Wm_ReassertNow(L"profile-activate");
    {
        wchar_t msg[160];
        _snwprintf(msg, 160, L"%s · %s is now the requested look", p->name, p->sub[p->active_sub].name);
        Ui_Notify(msg);
    }
    UiSl_Commit();
}

void UiGames_ClearActive(void)
{
    Eng_SetMode(PX_CSMODE_GLOBAL, -1, -1);
    Ui_Notify(L"Global look restored — no game profile is being applied");
    UiSl_Commit();
}

void UiGames_ExportSelected(void)
{
    Profile *p = Prof_Get(g_ui.sel_game);
    wchar_t path[MAX_PATH];
    if (!p) return;
    if (!UiSaveFile(path, MAX_PATH, L"PlexusX profile (*.json)\0*.json\0All files\0*.*\0", L"Export profile",
                    L"PlexusX_Profile.json")) return;
    if (Prof_ExportFile(g_ui.sel_game, path) == 0) Ui_Notify(L"Profile exported");
    else Ui_Notify(L"Export failed — check the path");
}

void UiGames_ImportFile(void)
{
    wchar_t path[MAX_PATH];
    int idx = -1;
    if (!UiOpenFile(path, MAX_PATH, L"PlexusX profile (*.json)\0*.json\0All files\0*.*\0", L"Import profile")) return;
    if (Prof_ImportFile(&idx, path) == 0 && idx >= 0) {
        g_ui.sel_game = idx;
        Ui_Notify(L"Profile imported");
        Ui_RebuildPanel();
    } else {
        Ui_Notify(L"Import failed — the file is not a PlexusX profile or library JSON");
    }
}

void UiGames_AddExeFromEditor(void)
{
    wchar_t exe[96];
    Profile *p = Prof_Get(g_ui.sel_game);
    if (!p) return;
    if (!UiInputBox(L"Executable base name to match", L"", exe, 96) || !exe[0]) return;
    if (Prof_AddExeName(g_ui.sel_game, exe) < 0) { Ui_Notify(L"Could not add that executable name"); return; }
    Ui_Notify(L"Executable name added");
    Ui_RebuildPanel();
}

void UiGames_RemoveExeSlot(void)
{
    int slot = g_ui.sel_exe_slot;
    if (slot < 0) { Ui_Notify(L"Select an executable row first"); return; }
    if (Prof_DeleteExeName(g_ui.sel_game, slot) == 0) {
        Ui_Notify(slot == 0 ? L"Primary name replaced by the first alias" : L"Executable name removed");
        g_ui.sel_exe_slot = -1;
    } else {
        Ui_Notify(L"Nothing to remove for that profile");
    }
    Ui_RebuildPanel();
}

void UiGames_SetPathFromEditor(void)
{
    wchar_t path[MAX_PATH];
    Profile *p = Prof_Get(g_ui.sel_game);
    if (!p) return;
    UiWin(path, MAX_PATH, p->exe_path);
    if (!UiOpenFile(path, MAX_PATH, L"Executable (*.exe)\0*.exe\0All files\0*.*\0", L"Select the game executable")) return;
    if (Prof_SetExePath(g_ui.sel_game, path) == 0)
        Ui_Notify(L"Executable path set — the base name is matched automatically");
    else
        Ui_Notify(L"Could not set the path");
    Ui_RebuildPanel();
}
