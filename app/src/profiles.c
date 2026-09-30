/* profiles.c — per-game looks, scene presets, foreground detection */
#include "common.h"
#include <tlhelp32.h>

#define MAX_PROF 16
#define MAX_RUN  24

static Profile g_prof[MAX_PROF];
static int     g_nprof;
static int     g_detect = 1;
static wchar_t g_lastExe[96];
static wchar_t g_run[MAX_RUN][96];
static int     g_nrun;

static const SceneDef g_scenes[] = {
    { L"Default",      { 130,   0,   0,   0, 0, 1.00f, 1 } },
    { L"Treeline",     { 255,   4,  14,   6, 0, 1.00f, 1 } },
    { L"Night Ops",    { 235,  30,  10,  -8, 0, 0.78f, 1 } },
    { L"Snow Glare",   { 190, -14,  18, -12, 0, 1.00f, 1 } },
    { L"Smoke & Dust", { 275,  -4,  26,   4, 0, 1.00f, 1 } },
    { L"Dark Room",    { 225,  32,   6,   8, 0, 0.72f, 1 } },
    { L"Bright Sky",   { 200, -16,  14,  -6, 0, 1.05f, 1 } },
    { L"Long Range",   { 250,   2,  22,   0, 0, 1.00f, 1 } },
};

const SceneDef *Scene_List(int *count)
{
    *count = (int)(sizeof g_scenes / sizeof g_scenes[0]);
    return g_scenes;
}

static const wchar_t *cfg_path(void)
{
    static wchar_t p[MAX_PATH];
    if (!p[0]) wsprintfW(p, L"%s\\config.ini", g_appdata);
    return p;
}

static void defaults(void)
{
    static const struct { const wchar_t *n, *e; Look l; } d[] = {
        { L"Desktop",   L"",                            { 140,   0,  0,  0, 0, 1.00f, 1 } },
        { L"Rust",      L"RustClient.exe",              { 250,   6, 14,  4, 0, 1.00f, 1 } },
        { L"CS2",       L"cs2.exe",                     { 235,   0, 18, -4, 0, 1.00f, 1 } },
        { L"Valorant",  L"VALORANT-Win64-Shipping.exe", { 240,   2, 14,  0, 0, 1.00f, 1 } },
        { L"Fortnite",  L"FortniteClient-Win64-Shipping.exe", { 200, 0, 8, 0, 0, 1.00f, 1 } },
        { L"Tarkov",    L"EscapeFromTarkov.exe",        { 265,  28, 12, -6, 0, 0.85f, 1 } },
    };
    g_nprof = (int)(sizeof d / sizeof d[0]);
    for (int i = 0; i < g_nprof; i++) {
        lstrcpynW(g_prof[i].name, d[i].n, 64);
        lstrcpynW(g_prof[i].exe, d[i].e, 96);
        g_prof[i].look = d[i].l;
    }
}

static void rd_look(const wchar_t *sec, Look *lk, const Look *def)
{
    const wchar_t *f = cfg_path();
    lk->enabled = GetPrivateProfileIntW(sec, L"enabled", def->enabled, f);
    lk->sat  = (float)GetPrivateProfileIntW(sec, L"sat",  (int)def->sat,  f);
    lk->bri  = (float)GetPrivateProfileIntW(sec, L"bri",  (int)def->bri,  f);
    lk->con  = (float)GetPrivateProfileIntW(sec, L"con",  (int)def->con,  f);
    lk->temp = (float)GetPrivateProfileIntW(sec, L"temp", (int)def->temp, f);
    lk->hue  = (float)GetPrivateProfileIntW(sec, L"hue",  (int)def->hue,  f);
    lk->gamma = GetPrivateProfileIntW(sec, L"gamma", (int)(def->gamma * 100), f) / 100.f;
    lk->sat   = clampf(lk->sat,   50, 300);
    lk->bri   = clampf(lk->bri,  -100, 100);
    lk->con   = clampf(lk->con,  -100, 100);
    lk->temp  = clampf(lk->temp, -100, 100);
    lk->hue   = clampf(lk->hue,   -60,  60);
    lk->gamma = clampf(lk->gamma, 0.40f, 2.40f);
}

int Prof_Load(void)
{
    const wchar_t *f = cfg_path();
    g_detect = GetPrivateProfileIntW(L"general", L"detect", 1, f);
    int n = GetPrivateProfileIntW(L"general", L"count", -1, f);
    if (n <= 0) { defaults(); return g_nprof; }
    g_nprof = clampi(n, 0, MAX_PROF);
    for (int i = 0; i < g_nprof; i++) {
        wchar_t sec[32], buf[160];
        wsprintfW(sec, L"profile%d", i);
        GetPrivateProfileStringW(sec, L"name", L"?", buf, 160, f);
        lstrcpynW(g_prof[i].name, buf, 64);
        GetPrivateProfileStringW(sec, L"exe", L"", buf, 160, f);
        lstrcpynW(g_prof[i].exe, buf, 96);
        Look def = g_prof[i].look; def.sat = 150; def.gamma = 1.f; def.enabled = 1;
        rd_look(sec, &g_prof[i].look, &def);
    }
    if (!g_nprof) defaults();
    return g_nprof;
}

static void wr_look(const wchar_t *sec, const Look *lk)
{
    wchar_t b[32];
    const wchar_t *f = cfg_path();
    wsprintfW(b, L"%d", (int)lk->enabled); WritePrivateProfileStringW(sec, L"enabled", b, f);
    wsprintfW(b, L"%d", (int)lk->sat);     WritePrivateProfileStringW(sec, L"sat",     b, f);
    wsprintfW(b, L"%d", (int)lk->bri);     WritePrivateProfileStringW(sec, L"bri",     b, f);
    wsprintfW(b, L"%d", (int)lk->con);     WritePrivateProfileStringW(sec, L"con",     b, f);
    wsprintfW(b, L"%d", (int)lk->temp);    WritePrivateProfileStringW(sec, L"temp",    b, f);
    wsprintfW(b, L"%d", (int)lk->hue);     WritePrivateProfileStringW(sec, L"hue",     b, f);
    wsprintfW(b, L"%d", (int)(lk->gamma * 100)); WritePrivateProfileStringW(sec, L"gamma", b, f);
}

int Prof_Save(void)
{
    const wchar_t *f = cfg_path();
    wchar_t b[32];
    wsprintfW(b, L"%d", g_nprof); WritePrivateProfileStringW(L"general", L"count", b, f);
    wsprintfW(b, L"%d", g_detect); WritePrivateProfileStringW(L"general", L"detect", b, f);
    for (int i = 0; i < g_nprof; i++) {
        wchar_t sec[32];
        wsprintfW(sec, L"profile%d", i);
        WritePrivateProfileStringW(sec, L"name", g_prof[i].name, f);
        WritePrivateProfileStringW(sec, L"exe", g_prof[i].exe, f);
        wr_look(sec, &g_prof[i].look);
    }
    return 0;
}

int Prof_Count(void) { return g_nprof; }
Profile *Prof_Get(int i) { return (i >= 0 && i < g_nprof) ? &g_prof[i] : NULL; }

int Prof_FindExe(const wchar_t *exe)
{
    if (!exe || !exe[0]) return -1;
    for (int i = 0; i < g_nprof; i++)
        if (g_prof[i].exe[0] && _wcsicmp(g_prof[i].exe, exe) == 0)
            return i;
    return -1;
}

int Prof_Add(const wchar_t *name, const wchar_t *exe, const Look *lk)
{
    if (g_nprof >= MAX_PROF) return -1;
    Profile *p = &g_prof[g_nprof];
    lstrcpynW(p->name, name, 64);
    lstrcpynW(p->exe, exe, 96);
    p->look = *lk;
    g_nprof++;
    Prof_Save();
    return g_nprof - 1;
}

void Prof_Del(int i)
{
    if (i < 0 || i >= g_nprof) return;
    for (int j = i; j < g_nprof - 1; j++) g_prof[j] = g_prof[j + 1];
    g_nprof--;
    Prof_Save();
}

void Prof_SetDetect(int on) { g_detect = on ? 1 : 0; }
int  Prof_Detect(void)      { return g_detect; }

/* ---- running window executables ---- */
static DWORD g_winpids[64];
static int   g_nwinpids;

static BOOL CALLBACK enum_win_cb(HWND wnd, LPARAM lp)
{
    (void)lp;
    if (!IsWindowVisible(wnd)) return TRUE;
    DWORD pid = 0;
    GetWindowThreadProcessId(wnd, &pid);
    if (!pid) return TRUE;
    for (int i = 0; i < g_nwinpids; i++) if (g_winpids[i] == pid) return TRUE;
    if (g_nwinpids < 64) g_winpids[g_nwinpids++] = pid;
    return TRUE;
}

int Prof_RunningScan(void)
{
    g_nwinpids = 0;
    g_nrun = 0;
    EnumWindows(enum_win_cb, 0);

    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return 0;
    PROCESSENTRY32W pe; pe.dwSize = sizeof pe;
    if (Process32FirstW(snap, &pe)) {
        do {
            int known = 0;
            for (int i = 0; i < g_nwinpids; i++)
                if (g_winpids[i] == pe.th32ProcessID) { known = 1; break; }
            if (!known) continue;
            if (!pe.szExeFile[0]) continue;
            if (_wcsicmp(pe.szExeFile, L"explorer.exe") == 0) continue;
            if (_wcsicmp(pe.szExeFile, L"chromax.exe") == 0) continue;
            int dup = 0;
            for (int i = 0; i < g_nrun; i++) if (_wcsicmp(g_run[i], pe.szExeFile) == 0) { dup = 1; break; }
            if (dup) continue;
            if (g_nrun >= MAX_RUN) break;
            lstrcpynW(g_run[g_nrun++], pe.szExeFile, 96);
        } while (Process32NextW(snap, &pe));
    }
    CloseHandle(snap);
    return g_nrun;
}

int Prof_RunningCount(void) { return g_nrun; }
const wchar_t *Prof_RunningGet(int i)
{
    return (i >= 0 && i < g_nrun) ? g_run[i] : L"";
}

/* ---- foreground watch ---- */
void Prof_Poll(void)
{
    if (!g_detect) return;
    HWND fg = GetForegroundWindow();
    if (!fg) return;
    DWORD pid = 0;
    GetWindowThreadProcessId(fg, &pid);
    if (!pid || pid == GetCurrentProcessId()) return;

    HANDLE hp = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!hp) return;
    wchar_t path[MAX_PATH * 2];
    DWORD len = MAX_PATH * 2;
    BOOL ok = QueryFullProcessImageNameW(hp, 0, path, &len);
    CloseHandle(hp);
    if (!ok) return;

    const wchar_t *base = path;
    for (const wchar_t *p = path; *p; p++) if (*p == L'\\') base = p + 1;
    if (_wcsicmp(base, g_lastExe) == 0) return;
    lstrcpynW(g_lastExe, base, 96);

    int idx = Prof_FindExe(base);
    if (idx < 0) return;
    Ui_LoadLook(&g_prof[idx].look);
    Ui_SelProfileSet(idx);
    Ui_Notify(g_prof[idx].name);
    Main_ApplyAll();
    InvalidateRect(g_hwnd, NULL, FALSE);
}
