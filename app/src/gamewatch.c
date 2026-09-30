/* gamewatch.c \u2014 event-driven foreground-game detection
 *
 * Uses SetWinEventHook(EVENT_SYSTEM_FOREGROUND) \u2014 the OS pushes an event
 * when the foreground window changes.  No polling, no timers, no re-apply
 * loops: a game look is applied at most once per foreground transition,
 * debounced by a minimum interval, and never re-applied while the same
 * game stays in the foreground.
 */
#include "common.h"

static HWINEVENTHOOK g_hook;
static wchar_t g_exe[96];        /* current foreground exe base (hook thread) */
static wchar_t g_matched[96];    /* exe that currently has a game look applied */
static int     g_autoApply = 1;
static int     g_restoreOnExit = 1;
static int     g_delayMs = 500;
static DWORD   g_lastApplyTick;
static int     g_hookLive;

static wchar_t *base_name_w(const wchar_t *path, wchar_t *out, int sz)
{
    const wchar_t *s = wcsrchr(path, L'\\');
    s = s ? s + 1 : path;
    lstrcpynW(out, s, sz);
    for (wchar_t *p = out; *p; p++) *p = (wchar_t)towlower(*p);
    return out;
}

static void CALLBACK hook_cb(HWINEVENTHOOK h, DWORD ev, HWND hwnd, LONG idObject,
                    LONG idChild, DWORD dwEventThread, DWORD dwmsEventTime)
{
    (void)h; (void)ev; (void)idObject; (void)idChild; (void)dwEventThread; (void)dwmsEventTime;
    if (!hwnd) return;
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (!pid) return;
    HANDLE hp = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!hp) return;
    wchar_t path[MAX_PATH];
    DWORD sz = MAX_PATH;
    if (QueryFullProcessImageNameW(hp, 0, path, &sz)) {
        base_name_w(path, g_exe, sizeof g_exe / sizeof g_exe[0]);
    }
    CloseHandle(hp);
    /* main thread matches + (debounced) applies \u2014 never here */
    PostMessageW(g_hwnd, WM_APP_GAME, 0, 0);
}

void Gw_Init(void)
{
    g_exe[0] = g_matched[0] = 0;
    g_hook = SetWinEventHook(EVENT_SYSTEM_FOREGROUND, EVENT_SYSTEM_FOREGROUND,
                             NULL, hook_cb, 0, 0,
                             WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS);
    g_hookLive = g_hook ? 1 : 0;
}

void Gw_Shutdown(void)
{
    if (g_hook) UnhookWinEvent(g_hook);
    g_hook = NULL;
    g_hookLive = 0;
}

int Gw_Enabled(void) { return g_hookLive; }

void Gw_SetAutoApply(int on) { g_autoApply = on; }
int  Gw_AutoApply(void) { return g_autoApply; }
void Gw_SetRestoreOnExit(int on) { g_restoreOnExit = on; }
int  Gw_RestoreOnExit(void) { return g_restoreOnExit; }
void Gw_SetDelayMs(int ms) { g_delayMs = clampi(ms, 0, 10000); }
int  Gw_DelayMs(void) { return g_delayMs; }

const wchar_t *Gw_ForegroundExe(void) { return g_exe; }

/* main-thread work after WM_APP_GAME.
 * Returns 1 if a (re)apply happened this call. */
int Gw_Process(void)
{
    const char *base = Main_Utf16ToUtf8Alloc(g_exe);
    const CxGame *game = base ? CxGames_FindExe(base) : NULL;
    DWORD now = GetTickCount();

    int gameChanged = 0;
    wchar_t cur[96] = { 0 };
    if (g_exe[0]) lstrcpynW(cur, g_exe, 96);

    if (!game) {
        /* user-defined custom game? */
        int ci = -1;
        if (Main_CustomMatch(g_exe, &ci)) {
            wchar_t want[96];
            const char *ex = Main_CustomExe(ci);
            MultiByteToWideChar(CP_UTF8, 0, ex, -1, want, 96);
            want[95] = 0;
            for (wchar_t *p = want; *p; p++) *p = (wchar_t)towlower(*p);
            if (lstrcmpW(g_matched, want) == 0) { free((void *)base); return 0; }
            if (now - g_lastApplyTick < (DWORD)g_delayMs && g_lastApplyTick != 0) {
                free((void *)base);
                return 0;   /* debounce */
            }
            lstrcpynW(g_matched, want, 96);
            g_lastApplyTick = now;
            free((void *)base);
            return 1;
        }
        /* left a game: restore base look if we had applied a game look */
        if (g_restoreOnExit && g_matched[0] && _wcsicmp(g_matched, cur) != 0) {
            g_matched[0] = 0;
            gameChanged = 1;
        }
        free((void *)base);
        return gameChanged;
    }

    wchar_t want[96];
    {
        char tmp[96];
        strcpy(tmp, game->exe);
        MultiByteToWideChar(CP_UTF8, 0, tmp, -1, want, 96);
        for (wchar_t *p = want; *p; p++) *p = (wchar_t)towlower(*p);
    }

    if (lstrcmpW(g_matched, want) == 0) {
        /* same game still in foreground \u2014 never re-apply */
        free((void *)base);
        return 0;
    }
    if (now - g_lastApplyTick < (DWORD)g_delayMs && g_lastApplyTick != 0) {
        free((void *)base);
        return 0;   /* debounce */
    }

    lstrcpynW(g_matched, want, 96);
    g_lastApplyTick = now;
    gameChanged = 1;
    free((void *)base);
    return 1;
}

/* look index the user picked for a game (-1 = none), stored in settings */
static int s_gameLookIdx[32];
void Gw_SetGameLook(int gameIdx, int lookIdx)
{
    if (gameIdx >= 0 && gameIdx < 32) s_gameLookIdx[gameIdx] = lookIdx;
}
int Gw_GameLook(int gameIdx)
{
    return (gameIdx >= 0 && gameIdx < 32) ? s_gameLookIdx[gameIdx] : -1;
}
int Gw_CurrentGame(void)
{
    if (!g_matched[0]) return -1;
    char base[96];
    int n = WideCharToMultiByte(CP_UTF8, 0, g_matched, -1, base, 95, NULL, NULL);
    if (n <= 0) return -1;
    base[n - 1] = 0;
    const CxGame *g = CxGames_FindExe(base);
    if (!g) return -1;
    for (int i = 0; i < CxGames_Count(); i++)
        if (CxGames_Get(i)->id == g->id) return i;
    return -1;
}
