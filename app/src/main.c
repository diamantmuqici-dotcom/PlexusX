/* main.c \u2014 ChromaX app glue
 *
 * Wires together: shared colour/core (cx_*), the display engine, monitors,
 * modes, HDR, game-watch, crosshair, phone control, diagnostics, and the UI.
 * All state lives under %APPDATA%\ChromaX (portable per user, no system-dir
 * writes, no telemetry).
 */
#include "common.h"
#include <shlobj.h>
#include <time.h>

/* ---------------- globals ---------------- */

HWND      g_hwnd;
HINSTANCE g_inst;
wchar_t   g_appdir[MAX_PATH];
wchar_t   g_appdata[MAX_PATH];
CxLook    g_look;
XhCfg     g_xh;
int       g_gaming;
Settings  g_settings;

/* ---------------- string helpers ---------------- */

wchar_t *Main_Utf8ToUtf16Alloc(const char *s)
{
    int n = MultiByteToWideChar(CP_UTF8, 0, s, -1, NULL, 0);
    if (n <= 0) { wchar_t *o = (wchar_t *)malloc(2); o[0] = 0; return o; }
    wchar_t *o = (wchar_t *)malloc(n * sizeof(wchar_t));
    MultiByteToWideChar(CP_UTF8, 0, s, -1, o, n);
    return o;
}

char *Main_Utf16ToUtf8Alloc(const wchar_t *s)
{
    int n = WideCharToMultiByte(CP_UTF8, 0, s, -1, NULL, 0, NULL, NULL);
    if (n <= 0) { char *o = (char *)malloc(1); o[0] = 0; return o; }
    char *o = (char *)malloc(n);
    WideCharToMultiByte(CP_UTF8, 0, s, -1, o, n, NULL, NULL);
    return o;
}

/* ---------------- logging ---------------- */

static void log_path(wchar_t *out, int sz)
{
    (void)sz;
    wsprintfW(out, L"%s\\chromax.log", g_appdata);
}

void Main_Log(const wchar_t *level, const wchar_t *msg)
{
    wchar_t path[MAX_PATH];
    log_path(path, MAX_PATH);
    FILE *f = _wfopen(path, L"a,ccs=UTF-8");
    if (!f) return;
    SYSTEMTIME st;
    GetLocalTime(&st);
    fprintf(f, "%04d-%02d-%02d %02d:%02d:%02d [%ls] ", st.wYear, st.wMonth,
            st.wDay, st.wHour, st.wMinute, st.wSecond, level);
    char mb[1024];
    WideCharToMultiByte(CP_UTF8, 0, msg, -1, mb, 1023, NULL, NULL);
    mb[1023] = 0;
    fputs(mb, f);
    fputc('\n', f);
    fclose(f);
    /* keep the log bounded */
    WIN32_FILE_ATTRIBUTE_DATA d;
    if (GetFileAttributesExW(path, GetFileExInfoStandard, &d) &&
        ((LONGLONG)d.nFileSizeHigh << 32 | d.nFileSizeLow) > 262144) {
        DeleteFileW(path);
    }
}

void Main_ClearLogs(void)
{
    wchar_t path[MAX_PATH];
    log_path(path, MAX_PATH);
    DeleteFileW(path);
}

void Main_OpenLogs(void)
{
    wchar_t path[MAX_PATH];
    log_path(path, MAX_PATH);
    if (!GetFileAttributesW(path)) {
        Main_Log(L"INFO", L"log opened");
    }
    ShellExecuteW(NULL, L"open", path, NULL, NULL, SW_SHOWNORMAL);
}

void Main_OpenDataFolder(void)
{
    wchar_t shcmd[MAX_PATH + 32];
    wsprintfW(shcmd, L"explorer.exe \"%s\"", g_appdata);
    ShellExecuteW(NULL, L"open", L"explorer.exe", g_appdata, NULL, SW_SHOWNORMAL);
    (void)shcmd;
}

/* ---------------- applied history ---------------- */

#define HIST_MAX 16
typedef struct { wchar_t source[64]; CxLook look; wchar_t ts[64]; int monitor; } HistItem;
static HistItem g_hist[HIST_MAX];
static int g_histN;

void Main_AppliedHistoryAdd(const wchar_t *source, const CxLook *look, int monitor)
{
    HistItem *h = &g_hist[g_histN % HIST_MAX];
    if (g_histN < HIST_MAX) g_histN++;
    lstrcpynW(h->source, source ? source : L"?", 64);
    h->look = *look;
    h->monitor = monitor;
    SYSTEMTIME st;
    GetLocalTime(&st);
    wsprintfW(h->ts, L"%02d:%02d:%02d", st.wHour, st.wMinute, st.wSecond);
}

int Main_AppliedHistoryCount(void) { return g_histN < HIST_MAX ? g_histN : HIST_MAX; }

int Main_AppliedHistoryGet(int i, CxLook *look, wchar_t *source, int ssz,
                           wchar_t *ts, int tsz, int *monitor)
{
    /* newest first */
    int idx = g_histN - 1 - i;
    if (idx < 0) return -1;
    HistItem *h = &g_hist[idx];
    if (look) *look = h->look;
    if (source) lstrcpynW(source, h->source, ssz);
    if (ts) lstrcpynW(ts, h->ts, tsz);
    if (monitor) *monitor = h->monitor;
    return 0;
}

/* ---------------- favourites ---------------- */

static int g_favs[16];
static int g_nfavs;

int Main_FavoriteCount(void) { return g_nfavs; }
int Main_FavoriteAt(int i) { return (i >= 0 && i < g_nfavs) ? g_favs[i] : -1; }
int Main_IsFavorite(int gameIdx)
{
    for (int i = 0; i < g_nfavs; i++)
        if (g_favs[i] == gameIdx) return 1;
    return 0;
}
void Main_ToggleFavorite(int gameIdx)
{
    for (int i = 0; i < g_nfavs; i++)
        if (g_favs[i] == gameIdx) {
            g_favs[i] = g_favs[--g_nfavs];
            return;
        }
    if (g_nfavs < 16) g_favs[g_nfavs++] = gameIdx;
}

/* ---------------- notifications ---------------- */

static NOTIFYICONDATAW g_nid;

static void tray_ensure(void)
{
    if (g_nid.uID) return;
    memset(&g_nid, 0, sizeof g_nid);
    g_nid.cbSize = sizeof g_nid;
    g_nid.hWnd = g_hwnd;
    g_nid.uID = 1;
    g_nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    g_nid.uCallbackMessage = WM_APP_TRAY;
    g_nid.hIcon = LoadIcon(g_inst, MAKEINTRESOURCEW(1));
    if (!g_nid.hIcon) g_nid.hIcon = LoadIcon(NULL, IDI_APPLICATION);
    lstrcpynW(g_nid.szTip, L"ChromaX \u2014 display control", 128);
    Shell_NotifyIconW(NIM_ADD, &g_nid);
}

void Main_TrayBlink(void)
{
    tray_ensure();
    Shell_NotifyIconW(NIM_MODIFY, &g_nid);
}

void Main_ShutdownTray(void)
{
    if (g_nid.uID) {
        Shell_NotifyIconW(NIM_DELETE, &g_nid);
        g_nid.uID = 0;
    }
}

void Main_Notify(const wchar_t *msg)
{
    if (g_settings.trayNotify && g_hwnd) {
        tray_ensure();
        Shell_NotifyIconW(NIM_MODIFY, &g_nid);
    }
    UI_Toast(msg);
    Main_Log(L"NOTIFY", msg);
}

/* ---------------- settings persistence ---------------- */

static void cfg_path(wchar_t *out, int sz)
{
    (void)sz;
    wsprintfW(out, L"%s\\config.json", g_appdata);
}

void Main_Save(void)
{
    if (!g_appdata[0]) return;
    CxJson *j = CxJson_NewObj();
    CxJson *look = CxJson_NewObj();
    CxJson_ObjSet(look, "saturation", CxJson_NewNum(g_look.sat));
    CxJson_ObjSet(look, "vibrance", CxJson_NewNum(g_look.vibrance));
    CxJson_ObjSet(look, "brightness", CxJson_NewNum(g_look.brightness));
    CxJson_ObjSet(look, "contrast", CxJson_NewNum(g_look.contrast));
    CxJson_ObjSet(look, "gamma", CxJson_NewNum(g_look.gamma));
    CxJson_ObjSet(look, "temperature", CxJson_NewNum(g_look.temperature));
    CxJson_ObjSet(look, "tint", CxJson_NewNum(g_look.tint));
    CxJson_ObjSet(look, "red", CxJson_NewNum(g_look.red));
    CxJson_ObjSet(look, "green", CxJson_NewNum(g_look.green));
    CxJson_ObjSet(look, "blue", CxJson_NewNum(g_look.blue));
    CxJson_ObjSet(look, "shadows", CxJson_NewNum(g_look.shadows));
    CxJson_ObjSet(look, "highlights", CxJson_NewNum(g_look.highlights));
    CxJson_ObjSet(look, "black", CxJson_NewNum(g_look.blacklevel));
    CxJson_ObjSet(look, "white", CxJson_NewNum(g_look.whitepoint));
    CxJson_ObjSet(look, "sharpness", CxJson_NewNum(g_look.sharpness));
    CxJson_ObjSet(look, "clarity", CxJson_NewNum(g_look.clarity));
    CxJson_ObjSet(look, "intensity", CxJson_NewNum(g_look.intensity));
    CxJson_ObjSet(look, "hue", CxJson_NewNum(g_look.hue));
    CxJson_ObjSet(look, "dehaze", CxJson_NewNum(g_look.dehaze));
    CxJson_ObjSet(look, "enabled", CxJson_NewBool(g_look.enabled));

    CxJson *xh = CxJson_NewObj();
    CxJson_ObjSet(xh, "on", CxJson_NewBool(g_xh.on));
    CxJson_ObjSet(xh, "shape", CxJson_NewNum(g_xh.shape));
    CxJson_ObjSet(xh, "size", CxJson_NewNum(g_xh.size));
    CxJson_ObjSet(xh, "gap", CxJson_NewNum(g_xh.gap));
    CxJson_ObjSet(xh, "thick", CxJson_NewNum(g_xh.thick));
    CxJson_ObjSet(xh, "opacity", CxJson_NewNum(g_xh.opacity));
    CxJson_ObjSet(xh, "outline", CxJson_NewBool(g_xh.outline));
    CxJson_ObjSet(xh, "dot", CxJson_NewBool(g_xh.dot));
    CxJson_ObjSet(xh, "rotation", CxJson_NewNum(g_xh.rotation));
    CxJson_ObjSet(xh, "monitor", CxJson_NewNum(g_xh.monitor));
    CxJson_ObjSet(xh, "color", CxJson_NewNum(
        (GetRValue(g_xh.color) << 16) | (GetGValue(g_xh.color) << 8) |
        GetBValue(g_xh.color)));
    CxJson_ObjSet(xh, "ocolor", CxJson_NewNum(
        (GetRValue(g_xh.ocolor) << 16) | (GetGValue(g_xh.ocolor) << 8) |
        GetBValue(g_xh.ocolor)));

    CxJson *set = CxJson_NewObj();
    CxJson_ObjSet(set, "autoApply", CxJson_NewBool(g_settings.autoApply));
    CxJson_ObjSet(set, "restoreOnExit", CxJson_NewBool(g_settings.restoreOnExit));
    CxJson_ObjSet(set, "delayMs", CxJson_NewNum(g_settings.delayMs));
    CxJson_ObjSet(set, "startWin", CxJson_NewBool(g_settings.startWin));
    CxJson_ObjSet(set, "minTray", CxJson_NewBool(g_settings.minTray));
    CxJson_ObjSet(set, "trayNotify", CxJson_NewBool(g_settings.trayNotify));

    CxJson *favs = CxJson_NewArr();
    for (int i = 0; i < g_nfavs; i++) CxJson_ArrAdd(favs, CxJson_NewNum(g_favs[i]));

    CxJson *gl = CxJson_NewArr();
    for (int i = 0; i < CxGames_Count(); i++)
        CxJson_ArrAdd(gl, CxJson_NewNum(Gw_GameLook(i)));

    CxJson_ObjSet(j, "app", CxJson_NewStr("ChromaX"));
    CxJson_ObjSet(j, "version", CxJson_NewNum(2));
    CxJson_ObjSet(j, "look", look);
    CxJson_ObjSet(j, "crosshair", xh);
    CxJson_ObjSet(j, "settings", set);
    CxJson_ObjSet(j, "favorites", favs);
    CxJson_ObjSet(j, "gameLooks", gl);
    CxJson_ObjSet(j, "gaming", CxJson_NewBool(g_gaming));

    char *s = CxJson_WriteStr(j);
    CxJson_Free(j);
    if (s) {
        wchar_t path[MAX_PATH];
        cfg_path(path, MAX_PATH);
        FILE *f = _wfopen(path, L"w");
        if (f) { fputs(s, f); fclose(f); }
        free(s);
    }
}

void Main_Load(void)
{
    wchar_t path[MAX_PATH];
    cfg_path(path, MAX_PATH);
    char *u8 = Main_Utf16ToUtf8Alloc(path);
    FILE *f = fopen(u8, "rb");
    if (f) {
        fseek(f, 0, SEEK_END);
        long sz = ftell(f);
        fseek(f, 0, SEEK_SET);
        if (sz > 0 && sz < 65536) {
            char *txt = (char *)malloc(sz + 1);
            if (fread(txt, 1, sz, f) == (size_t)sz) {
                txt[sz] = 0;
                CxJson *j = NULL;
                char err[96] = { 0 };
                if (CxJson_Parse(txt, sz, &j, err, sizeof err) == 0 && j) {
                    CxJson *look = CxJson_Get(j, "look");
                    if (look) {
                        g_look.sat = (float)CxJson_GetNum(look, "saturation", 100);
                        g_look.vibrance = (float)CxJson_GetNum(look, "vibrance", 100);
                        g_look.brightness = (float)CxJson_GetNum(look, "brightness", 100);
                        g_look.contrast = (float)CxJson_GetNum(look, "contrast", 100);
                        g_look.gamma = (float)CxJson_GetNum(look, "gamma", 1.0);
                        g_look.temperature = (float)CxJson_GetNum(look, "temperature", 6500);
                        g_look.tint = (float)CxJson_GetNum(look, "tint", 0);
                        g_look.red = (float)CxJson_GetNum(look, "red", 100);
                        g_look.green = (float)CxJson_GetNum(look, "green", 100);
                        g_look.blue = (float)CxJson_GetNum(look, "blue", 100);
                        g_look.shadows = (float)CxJson_GetNum(look, "shadows", 100);
                        g_look.highlights = (float)CxJson_GetNum(look, "highlights", 100);
                        g_look.blacklevel = (float)CxJson_GetNum(look, "black", 100);
                        g_look.whitepoint = (float)CxJson_GetNum(look, "white", 100);
                        g_look.sharpness = (float)CxJson_GetNum(look, "sharpness", 100);
                        g_look.clarity = (float)CxJson_GetNum(look, "clarity", 100);
                        g_look.intensity = (float)CxJson_GetNum(look, "intensity", 100);
                        g_look.hue = (float)CxJson_GetNum(look, "hue", 0);
                        g_look.dehaze = (float)CxJson_GetNum(look, "dehaze", 100);
                        g_look.enabled = CxJson_GetBool(look, "enabled", 1);
                        CxLook_Clamp(&g_look);
                    }
                    CxJson *xh = CxJson_Get(j, "crosshair");
                    if (xh) {
                        g_xh.on = CxJson_GetBool(xh, "on", 0);
                        g_xh.shape = (int)CxJson_GetNum(xh, "shape", 0);
                        g_xh.size = (int)CxJson_GetNum(xh, "size", 22);
                        g_xh.gap = (int)CxJson_GetNum(xh, "gap", 6);
                        g_xh.thick = (int)CxJson_GetNum(xh, "thick", 3);
                        g_xh.opacity = (int)CxJson_GetNum(xh, "opacity", 100);
                        g_xh.outline = CxJson_GetBool(xh, "outline", 1);
                        g_xh.dot = CxJson_GetBool(xh, "dot", 0);
                        g_xh.rotation = (int)CxJson_GetNum(xh, "rotation", 0);
                        g_xh.monitor = (int)CxJson_GetNum(xh, "monitor", -1);
                        g_xh.color = RGB((int)CxJson_GetNum(xh, "color", 0xFFFFFF) >> 16 & 0xFF,
                                         (int)CxJson_GetNum(xh, "color", 0xFFFFFF) >> 8 & 0xFF,
                                         (int)CxJson_GetNum(xh, "color", 0xFFFFFF) & 0xFF);
                        g_xh.ocolor = RGB((int)CxJson_GetNum(xh, "ocolor", 0) >> 16 & 0xFF,
                                          (int)CxJson_GetNum(xh, "ocolor", 0) >> 8 & 0xFF,
                                          (int)CxJson_GetNum(xh, "ocolor", 0) & 0xFF);
                    }
                    CxJson *set = CxJson_Get(j, "settings");
                    if (set) {
                        g_settings.autoApply = CxJson_GetBool(set, "autoApply", 1);
                        g_settings.restoreOnExit = CxJson_GetBool(set, "restoreOnExit", 1);
                        g_settings.delayMs = (int)CxJson_GetNum(set, "delayMs", 500);
                        g_settings.startWin = CxJson_GetBool(set, "startWin", 0);
                        g_settings.minTray = CxJson_GetBool(set, "minTray", 1);
                        g_settings.trayNotify = CxJson_GetBool(set, "trayNotify", 1);
                    }
                    CxJson *favs = CxJson_Get(j, "favorites");
                    g_nfavs = 0;
                    if (favs && favs->type == CXJ_ARR) {
                        for (int i = 0; i < favs->nchild && g_nfavs < 16; i++) {
                            CxJson *el = favs->child[i].val;
                            if (el && el->type == CXJ_NUM) {
                                int gi = (int)el->num;
                                if (gi >= 0 && gi < CxGames_Count())
                                    g_favs[g_nfavs++] = gi;
                            }
                        }
                    }
                    CxJson *gl = CxJson_Get(j, "gameLooks");
                    if (gl && gl->type == CXJ_ARR) {
                        for (int i = 0; i < gl->nchild && i < CxGames_Count(); i++) {
                            CxJson *el = gl->child[i].val;
                            if (el && el->type == CXJ_NUM) Gw_SetGameLook(i, (int)el->num);
                        }
                    }
                    g_gaming = CxJson_GetBool(j, "gaming", 0);
                    CxJson_Free(j);
                }
            }
            free(txt);
        }
        fclose(f);
    }
    free(u8);
    Gw_SetAutoApply(g_settings.autoApply);
    Gw_SetRestoreOnExit(g_settings.restoreOnExit);
    Gw_SetDelayMs(g_settings.delayMs);
}

void Main_ResetSettings(void)
{
    wchar_t path[MAX_PATH];
    cfg_path(path, MAX_PATH);
    DeleteFileW(path);
    memset(&g_settings, 0, sizeof g_settings);
    g_settings.autoApply = 1;
    g_settings.restoreOnExit = 1;
    g_settings.delayMs = 500;
    g_settings.minTray = 1;
    g_settings.trayNotify = 1;
    g_nfavs = 0;
    g_gaming = 0;
    for (int i = 0; i < CxGames_Count(); i++) Gw_SetGameLook(i, 0);
    Main_Save();
    Main_Notify(L"Settings reset to defaults");
}

/* ---------------- backup / restore ---------------- */

void Main_Backup(const wchar_t *source)
{
    wchar_t dir[MAX_PATH];
    wsprintfW(dir, L"%s\\backups", g_appdata);
    CreateDirectoryW(dir, NULL);

    wchar_t ts[64], fname[96], path[MAX_PATH];
    SYSTEMTIME st;
    GetLocalTime(&st);
    wsprintfW(ts, L"%04d%02d%02d-%02d%02d%02d", st.wYear, st.wMonth, st.wDay,
              st.wHour, st.wMinute, st.wSecond);
    wsprintfW(fname, L"chromax-%ls-%ls.json", ts, source ? source : L"backup");
    wsprintfW(path, L"%s\\%s", dir, fname);

    CxBackup b;
    CxMonState mons[CX_BACKUP_MAX_MON];
    int nm = 0;
    for (int i = 0; i < MonCount() && nm < CX_BACKUP_MAX_MON; i++) {
        const MonInfo *m = MonGet(i);
        mons[nm].w = m->res_w; mons[nm].h = m->res_h; mons[nm].hz = m->hz;
        mons[nm].hdr = 0;
        HdrState hs;
        Hdr_ScanFor(i, &hs);
        mons[nm].hdr = hs.enabled;
        {
            char db[32] = { 0 }, idn[64] = { 0 };
            WideCharToMultiByte(CP_UTF8, 0, m->device, -1, db, 31, NULL, NULL);
            WideCharToMultiByte(CP_UTF8, 0, m->id, -1, idn, 63, NULL, NULL);
            lstrcpynA(mons[nm].device, db, 32);
            lstrcpynA(mons[nm].id, idn, 64);
        }
        nm++;
    }
    char *src = Main_Utf16ToUtf8Alloc(source ? source : L"manual");
    {
        time_t tt = time(NULL);
        CxBackup_Fill(&b, &g_look, src, mons, nm, (long long)tt);
    }
    free(src);
    char *upath = Main_Utf16ToUtf8Alloc(path);
    CxBackup_WriteFile(upath, &b);
    free(upath);
    Main_Log(L"INFO", L"backup created");

    /* keep at most 20 */
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW(path, &fd);
    if (h != INVALID_HANDLE_VALUE) {
        int n = 0;
        do {
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
            n++;
        } while (FindNextFileW(h, &fd));
        FindClose(h);
        if (n > 20) {
            /* delete the oldest (first in list order is not oldest;
               sort by write time would need more code \u2014 delete oldest by name
               (timestamps are in the name, so lexicographic = chronological) */
            h = FindFirstFileW(path, &fd);
            wchar_t oldest[MAX_PATH] = { 0 };
            if (h != INVALID_HANDLE_VALUE) {
                do {
                    if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
                    if (!oldest[0] || lstrcmpW(fd.cFileName, oldest) < 0)
                        lstrcpynW(oldest, fd.cFileName, MAX_PATH);
                } while (FindNextFileW(h, &fd));
                FindClose(h);
            }
            if (oldest[0]) {
                wchar_t op[MAX_PATH];
                wsprintfW(op, L"%s\\%s", dir, oldest);
                DeleteFileW(op);
            }
        }
    }
}

int Main_RestoreBackup(void)
{
    wchar_t dir[MAX_PATH], fname[64], path[MAX_PATH];
    wsprintfW(dir, L"%s\\backups", g_appdata);
    /* newest by name (timestamps) */
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW(dir, &fd);
    if (h == INVALID_HANDLE_VALUE) return 0;
    wchar_t newest[64] = { 0 };
    do {
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
        if (fd.cFileName[0] == L'.') continue;
        if (!newest[0] || lstrcmpW(fd.cFileName, newest) > 0)
            lstrcpynW(newest, fd.cFileName, 64);
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    if (!newest[0]) return 0;
    wsprintfW(fname, L"%s", newest);
    wsprintfW(path, L"%s\\%s", dir, fname);

    CxBackup b;
    char err[160] = { 0 };
    char *u8 = Main_Utf16ToUtf8Alloc(path);
    int ok = CxBackup_ReadFile(u8, &b, err, sizeof err) == 0;
    free(u8);
    if (!ok) return 0;
    g_look = b.look;
    CxLook_Clamp(&g_look);
    UI_ApplyLook(L"restore");
    return 1;
}

void Main_ResetAllChanges(void)
{
    Eng_ResetAll();
    CxLook_Default(&g_look);
    Main_AppliedHistoryAdd(L"reset", &g_look, -1);
    Main_Save();
    Main_Notify(L"All display changes reset");
    Main_Log(L"INFO", L"all changes reset");
}

/* ---------------- gaming mode ---------------- */

static CxLook g_preGaming;
static int g_havePreGaming;

void Main_ToggleGaming(void)
{
    g_gaming = !g_gaming;
    if (g_gaming) {
        if (!g_havePreGaming) { g_preGaming = g_look; g_havePreGaming = 1; }
        g_look.sat = 130;
        g_look.vibrance = 130;
        g_look.brightness = 95;
        g_look.contrast = 125;
        g_look.gamma = 1.06f;
        g_look.temperature = 6300;
        g_look.shadows = 112;
        g_look.highlights = 92;
        g_look.blacklevel = 96;
        g_look.enabled = 1;
        CxLook_Clamp(&g_look);
        if (!g_xh.on) {
            g_xh.on = 1;
            Xh_Update(&g_xh);
        }
        UI_ApplyLook(L"gaming mode");
        Main_Notify(L"Gaming mode on");
    } else {
        if (g_havePreGaming) {
            g_look = g_preGaming;
            CxLook_Clamp(&g_look);
            UI_ApplyLook(L"gaming off");
            g_havePreGaming = 0;
        }
        Main_Notify(L"Gaming mode off");
    }
    Main_Save();
}

int Main_GamingMode(void) { return g_gaming; }

/* ---------------- start with windows ---------------- */

void Main_ApplyStartWithWin(int on)
{
    HKEY k;
    if (RegCreateKeyExW(HKEY_CURRENT_USER,
                        L"Software\\Microsoft\\Windows\\CurrentVersion\\Run",
                        0, NULL, 0, KEY_SET_VALUE, NULL, &k, NULL) == ERROR_SUCCESS) {
        if (on) {
            wchar_t exe[MAX_PATH];
            GetModuleFileNameW(NULL, exe, MAX_PATH);
            wchar_t v[MAX_PATH + 8];
            wsprintfW(v, L"\"%s\"", exe);
            RegSetValueExW(k, L"ChromaX", 0, REG_SZ, (const BYTE *)v,
                           (DWORD)((wcslen(v) + 1) * sizeof(wchar_t)));
        } else {
            RegDeleteValueW(k, L"ChromaX");
        }
        RegCloseKey(k);
    }
}

/* ---------------- global hotkeys ---------------- */

static CxHotkey g_hk[4];
static int g_hkValid[4];
static wchar_t g_hkText[4][64];
static HWND g_hkHwnd;

static void hk_defaults(void)
{
    static const char *d[4] = { "Ctrl+Alt+X", "Ctrl+Alt+R", "Ctrl+Alt+G", "Ctrl+Alt+H" };
    for (int i = 0; i < 4; i++) {
        g_hkValid[i] = CxHotkey_Parse(d[i], &g_hk[i]) == 0;
        CxHotkey_Format(&g_hk[i], (char *)g_hkText[i], 64);
        g_hkText[i][63] = 0;
    }
}

void Main_HookHotkeys(void);

static int hk_modmask(const CxHotkey *h)
{
    int m = 0;
    if (h->mod_ctrl)  m |= MOD_CONTROL;
    if (h->mod_alt)   m |= MOD_ALT;
    if (h->mod_shift) m |= MOD_SHIFT;
    if (h->mod_win)   m |= MOD_WIN;
    return m;
}

static int hk_vk(const CxHotkey *h)
{
    const char *k = h->key;
    if (lstrlenA(k) == 1) return (int)(unsigned char)k[0];
    if (k[0] == 'D' && k[1] >= '0' && k[1] <= '9') return (int)k[1];
    if (k[0] == 'F' && k[1] >= '1') {
        int n = atoi(k + 1);
        if (n >= 1 && n <= 24) return VK_F1 + n - 1;
    }
    if (!lstrcmpiA(k, "Up"))    return VK_UP;
    if (!lstrcmpiA(k, "Down"))  return VK_DOWN;
    if (!lstrcmpiA(k, "Left"))  return VK_LEFT;
    if (!lstrcmpiA(k, "Right")) return VK_RIGHT;
    if (!lstrcmpiA(k, "Space")) return VK_SPACE;
    if (!lstrcmpiA(k, "Enter")) return VK_RETURN;
    if (!lstrcmpiA(k, "Tab"))   return VK_TAB;
    return 0;
}

void Main_HotkeysInit(void)
{
    g_hkHwnd = g_hwnd;
    hk_defaults();
    Main_HookHotkeys();
}

void Main_HookHotkeys(void)
{
    for (int i = 0; i < 4; i++) {
        if (g_hkValid[i] && hk_vk(&g_hk[i]))
            RegisterHotKey(g_hkHwnd, 100 + i, hk_modmask(&g_hk[i]), hk_vk(&g_hk[i]));
    }
}

void Main_HotkeysFree(void)
{
    for (int i = 0; i < 4; i++)
        if (g_hkValid[i]) UnregisterHotKey(g_hkHwnd, 100 + i);
}

const wchar_t *Main_HotkeyText(int i)
{
    if (i < 0 || i >= 4) return L"(off)";
    return g_hkText[i];
}

int Main_HotkeySet(int i, const char *combo)
{
    if (i < 0 || i >= 4) return -1;
    CxHotkey h;
    if (CxHotkey_Parse(combo, &h) != 0) return -1;
    UnregisterHotKey(g_hkHwnd, 100 + i);
    if (!hk_vk(&h) ||
        !RegisterHotKey(g_hkHwnd, 100 + i, hk_modmask(&h), hk_vk(&h))) {
        /* conflict or bad key \u2014 keep the old one */
        if (g_hkValid[i])
            RegisterHotKey(g_hkHwnd, 100 + i, hk_modmask(&g_hk[i]), hk_vk(&g_hk[i]));
        return -1;
    }
    g_hk[i] = h;
    g_hkValid[i] = 1;
    CxHotkey_Format(&h, (char *)g_hkText[i], 64);
    g_hkText[i][63] = 0;
    return 0;
}

void Main_HotkeyAction(int id)
{
    switch (id) {
    case 100:
        g_xh.on = !g_xh.on;
        Xh_Update(&g_xh);
        Main_Save();
        Main_Notify(g_xh.on ? L"Crosshair on" : L"Crosshair off");
        break;
    case 101:
        Main_ResetAllChanges();
        break;
    case 102:
        Main_ToggleGaming();
        break;
    case 103:
        if (IsWindowVisible(g_hwnd)) ShowWindow(g_hwnd, SW_HIDE);
        else { ShowWindow(g_hwnd, SW_SHOW); SetForegroundWindow(g_hwnd); }
        break;
    }
}

/* ---------------- phone action processing (UI thread) ---------------- */

void Phone_ProcessPending(void)
{
    PhoneAct a;
    while (Phone_Pending(&a)) {
        switch (a.action) {
        case PH_LOOK:
            g_look = a.look;
            CxLook_Clamp(&g_look);
            UI_ApplyLook(L"phone");
            break;
        case PH_PRESET: {
            /* find preset by name */
            WIN32_FIND_DATAW fd;
            wchar_t pat[MAX_PATH];
            wsprintfW(pat, L"%s\\presets\\*.json", g_appdata);
            HANDLE h = FindFirstFileW(pat, &fd);
            wchar_t base[160];
            MultiByteToWideChar(CP_UTF8, 0, a.name, -1, base, 160);
            if (h != INVALID_HANDLE_VALUE) {
                do {
                    if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
                    wchar_t nb[64];
                    lstrcpynW(nb, fd.cFileName, 64);
                    nb[58] = 0;
                    if (lstrcmpW(nb, base) == 0) {
                        wchar_t full[MAX_PATH];
                        wsprintfW(full, L"%s\\presets\\%s", g_appdata, fd.cFileName);
                        CxPreset p;
                        char err[128] = { 0 };
                        char *u8 = Main_Utf16ToUtf8Alloc(full);
                        if (CxPreset_ImportFile(u8, &p, err, sizeof err) == 0) {
                            g_look = p.look;
                            CxLook_Clamp(&g_look);
                            UI_ApplyLook(L"phone preset");
                        }
                        free(u8);
                        break;
                    }
                } while (FindNextFileW(h, &fd));
                FindClose(h);
            }
            break;
        }
        case PH_GAME: {
            if (a.a >= 0 && a.a < CxGames_Count()) {
                const CxGame *g = CxGames_Get(a.a);
                int li = a.b >= 0 && a.b < g->nlooks ? a.b : 0;
                g_look = g->looks[li].look;
                g_look.enabled = 1;
                CxLook_Clamp(&g_look);
                UI_ApplyLook(L"phone game");
            }
            break;
        }
        case PH_XH:
            g_xh = a.xh;
            Xh_Update(&g_xh);
            Main_Save();
            break;
        case PH_HDR:
            Hdr_SetEnabled(-1, a.a ? 1 : 0);
            break;
        case PH_RESET:
            Main_ResetAllChanges();
            break;
        }
        UI_Refresh();
    }
}

void Main_ApplyAll(void)
{
    Eng_ApplyLook(&g_look, -1, 1);
    Xh_Update(&g_xh);
    Main_Save();
}

/* ---------------- WinMain ---------------- */

static void setup_appdirs(void)
{
    if (SHGetFolderPathW(NULL, CSIDL_APPDATA, NULL, 0, g_appdata) != S_OK)
        lstrcpyW(g_appdata, L".");
    lstrcatW(g_appdata, L"\\ChromaX");
    CreateDirectoryW(g_appdata, NULL);
    GetModuleFileNameW(NULL, g_appdir, MAX_PATH);
    /* strip the exe name */
    wchar_t *sl = wcsrchr(g_appdir, L'\\');
    if (sl) *sl = 0;
    wchar_t sub[MAX_PATH];
    wsprintfW(sub, L"%s\\presets", g_appdata);
    CreateDirectoryW(sub, NULL);
    wsprintfW(sub, L"%s\\backups", g_appdata);
    CreateDirectoryW(sub, NULL);
}

int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE hPrev, PWSTR cmdLine, int nCmdShow)
{
    (void)hPrev; (void)nCmdShow;
    g_inst = hInst;

    /* single instance */
    HANDLE mtx = CreateMutexW(NULL, TRUE, CX_MUTEX);
    if (!mtx || GetLastError() == ERROR_ALREADY_EXISTS) {
        MessageBoxW(NULL, L"ChromaX is already running.", L"ChromaX", MB_OK);
        return 0;
    }

    /* selftest mode for CI: print capabilities and exit */
    if (cmdLine && (wcsstr(cmdLine, L"--selftest"))) {
        setup_appdirs();
        MonRefresh();
        Modes_Refresh();
        Eng_Init();
        Xh_Init();
        CxLook l;
        CxLook_Default(&l);
        float m[4][4];
        CxLook_BuildMatrix(&l, m);
        uint16_t ramp[3][256];
        CxLook_BuildLUT(&l, ramp, 0);
        wprintf(L"ChromaX selftest v%s\n", CX_VERSION);
        wprintf(L"monitors=%d mag=%d ramp=%d modes0=%d scenes=%d games=%d looks=%d\n",
                MonCount(), Eng_MagAvailable(), Eng_RampAvailable(),
                Modes_CountFor(0), CX_N_SCENE, CxGames_Count(), CxLooks_Count());
        wprintf(L"matrix-neutral: %d\n", CxLook_IsNeutral(&l));
        Eng_Shutdown();
        return 0;
    }

    CxLook_Default(&g_look);
    Xh_Init();
    memset(&g_settings, 0, sizeof g_settings);
    g_settings.autoApply = 1;
    g_settings.restoreOnExit = 1;
    g_settings.delayMs = 500;
    g_settings.minTray = 1;
    g_settings.trayNotify = 1;

    setup_appdirs();
    Main_Load();

    MonRefresh();
    Modes_Refresh();
    Eng_Init();
    Xh_Init();

    /* crash recovery: if a non-neutral look was last applied, re-apply it */
    CxLook lkg;
    if (Eng_LoadLast(&lkg)) {
        g_look = lkg;
        Eng_ApplyLook(&g_look, -1, 1);
    }

    UI_Init(hInst);
    tray_ensure();
    Main_HotkeysInit();
    Gw_Init();
    if (g_xh.on) Xh_Update(&g_xh);
    if (Phone_Running() == 0 && g_settings.trayNotify) {
        /* phone starts on demand */
    }
    Main_Log(L"INFO", L"ChromaX started");

    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    Main_HotkeysFree();
    Gw_Shutdown();
    Phone_Stop();
    Xh_Shutdown();
    Eng_Shutdown();
    Main_ShutdownTray();
    return 0;
}
