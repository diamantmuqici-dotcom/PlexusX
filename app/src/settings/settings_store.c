/* PlexusX — SettingsStore Win32 glue.
 *
 * Owns the two persistent files:
 *   %APPDATA%\PlexusX\config.ini     — [meta] [current] [ui] [xhair] [automation]
 *   %APPDATA%\PlexusX\profiles.ini   — [general] + [profileN] records
 *
 * Read path handles legacy files written by WritePrivateProfileStringW
 * (UTF-16LE + BOM) and plain ASCII alike.  A file that parses to nothing while
 * non-empty is moved aside as *.corrupt-N (data preserved for the user) and
 * the store starts from defaults — silent data loss AND silent corruption
 * propagation are both refused.  Writes are atomic (tmp + MoveFileEx) so a
 * crash can never truncate a config.
 */
#include "common.h"
#include "settings_store.h"

#define PXSET_MAXTEXT (192 * 1024)

static PxIni  g_cfg;
static PxIni  g_prof;
static wchar_t g_cfg_path[MAX_PATH];
static wchar_t g_prof_path[MAX_PATH];
static char  *g_read_buf = NULL;
static int    g_cfg_dirty = 0;
static int    g_prof_dirty = 0;
static int    g_cfg_corrupt = 0;
static int    g_prof_corrupt = 0;
static int    g_ready = 0;

/* ---------------- text reading (UTF-16 BOM aware) ---------------- */
static size_t read_text(const wchar_t *path, char *out, size_t cap)
{
    HANDLE h = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL,
                           OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return 0;

    static BYTE raw[PXSET_MAXTEXT + 4];
    DWORD rd = 0;
    BOOL ok = ReadFile(h, raw, sizeof raw - 4, &rd, NULL);
    CloseHandle(h);
    if (!ok || rd < 2) return 0;
    raw[rd] = 0; raw[rd + 1] = 0;

    if (raw[0] == 0xFF && raw[1] == 0xFE) {           /* UTF-16LE (legacy profile API) */
        int wchars = (int)(rd / sizeof(WCHAR)) - 1;    /* skip BOM */
        const WCHAR *src = (const WCHAR *)raw + 1;
        int need = WideCharToMultiByte(CP_UTF8, 0, src, wchars, NULL, 0, NULL, NULL);
        if (need <= 0) return 0;
        if ((size_t)need + 1 > cap) need = (int)cap - 1;
        int got = WideCharToMultiByte(CP_UTF8, 0, src, wchars, out, need, NULL, NULL);
        if (got <= 0) return 0;
        out[got] = 0;
        return (size_t)got;
    }
    {   /* ASCII / UTF-8 (this module's own format, or any hand-written file) */
        size_t n = rd;
        if (n >= 3 && raw[0] == 0xEF && raw[1] == 0xBB && raw[2] == 0xBF) { n -= 3; memmove(raw, raw + 3, n + 1); }
        if (n > cap - 1) n = cap - 1;
        memcpy(out, raw, n);
        out[n] = 0;
        return n;
    }
}

static void backup_corrupt(const wchar_t *path)
{
    wchar_t bak[MAX_PATH + 16];
    for (int n = 1; n <= 5; n++) {
        wsprintfW(bak, L"%s.corrupt%d", path, n);
        if (GetFileAttributesW(bak) == INVALID_FILE_ATTRIBUTES) {
            MoveFileW(path, bak);
            return;
        }
    }
    /* five backups exist already: leave the file untouched (never delete user data) */
}

static int load_ini(const wchar_t *path, PxIni *ini, int *corrupt_flag)
{
    if (!g_read_buf) g_read_buf = (char *)malloc(PXSET_MAXTEXT);
    if (!g_read_buf) return -1;
    size_t n = read_text(path, g_read_buf, PXSET_MAXTEXT);
    px_ini_init(ini);
    if (n == 0) return 0;                                   /* absent/empty: defaults */
    int got = px_ini_parse(ini, g_read_buf);
    if (got <= 0 && n > 3) {                                /* non-empty but garbage */
        backup_corrupt(path);
        *corrupt_flag = 1;
        px_ini_init(ini);
    }
    return got;
}

/* ---------------- atomic write ---------------- */
static int write_ini(const wchar_t *path, PxIni *ini)
{
    char *out = (char *)malloc(PXSET_MAXTEXT);
    if (!out) return -1;
    int len = px_ini_serialize(ini, out, PXSET_MAXTEXT);
    if (len <= 0) { free(out); return -1; }

    wchar_t tmp[MAX_PATH + 8];
    wsprintfW(tmp, L"%s.tmp", path);
    HANDLE h = CreateFileW(tmp, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) { free(out); return -1; }
    DWORD wr = 0;
    BOOL ok = WriteFile(h, out, (DWORD)len, &wr, NULL) && (int)wr == len && FlushFileBuffers(h);
    CloseHandle(h);
    free(out);
    if (!ok) { DeleteFileW(tmp); return -1; }
    if (!MoveFileExW(tmp, path, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        DeleteFileW(tmp);
        return -1;
    }
    return 0;
}

/* ---------------- public surface ---------------- */
void PxSet_Init(const wchar_t *appdata)
{
    wsprintfW(g_cfg_path, L"%s\\config.ini", appdata);
    wsprintfW(g_prof_path, L"%s\\profiles.ini", appdata);
    load_ini(g_cfg_path, &g_cfg, &g_cfg_corrupt);
    load_ini(g_prof_path, &g_prof, &g_prof_corrupt);
    g_ready = 1;

    /* schema migration runs on load so the first save writes the current form */
    if (px_cfg_migrate(&g_cfg) > 0) g_cfg_dirty = 1;
    if (px_cfg_schema(&g_prof) < PX_CFG_SCHEMA) {
        px_ini_set_int(&g_prof, "meta", "schema", PX_CFG_SCHEMA);
        g_prof_dirty = 1;
    }
    if (g_cfg_dirty || g_prof_dirty) PxSet_Flush();
}

int PxSet_CfgCorrupt(void)  { return g_cfg_corrupt; }
int PxSet_ProfCorrupt(void) { return g_prof_corrupt; }

PxIni *PxSet_Cfg(void)  { g_cfg_dirty = 1;  return &g_cfg; }    /* mutate + auto-dirty */
PxIni *PxSet_Prof(void) { g_prof_dirty = 1; return &g_prof; }   /* read-only users may pass dirty=0 via PxSet_CfgRO */
const PxIni *PxSet_CfgRO(void)  { return &g_cfg; }
const PxIni *PxSet_ProfRO(void) { return &g_prof; }

void PxSet_TouchProf(void) { g_prof_dirty = 1; }

int PxSet_Flush(void)
{
    if (!g_ready) return -1;
    int r = 0;
    if (g_cfg_dirty)  { if (write_ini(g_cfg_path, &g_cfg) == 0) g_cfg_dirty = 0;  else r = -1; }
    if (g_prof_dirty) { if (write_ini(g_prof_path, &g_prof) == 0) g_prof_dirty = 0; else r = -1; }
    return r;
}

/* Convenience accessors used by main.c (config) — every getter tolerates a
 * missing store (pre-init or catastrophic config) with the caller's default. */
int   PxSetGetInt(const char *sec, const char *key, int dflt)
{
    if (!g_ready) return dflt;
    return px_ini_get_int(&g_cfg, sec, key, dflt);
}
float PxSetGetFlt(const char *sec, const char *key, float dflt)
{
    if (!g_ready) return dflt;
    return px_ini_get_flt(&g_cfg, sec, key, dflt);
}
void  PxSetSetInt(const char *sec, const char *key, long v)
{
    if (!g_ready) return;
    px_ini_set_int(&g_cfg, sec, key, v);
    g_cfg_dirty = 1;
}
void  PxSetSetFlt(const char *sec, const char *key, double v)
{
    if (!g_ready) return;
    px_ini_set_dbl(&g_cfg, sec, key, v);
    g_cfg_dirty = 1;
}
void PxSetLoadLook(Look *out)
{
    if (!g_ready) { *out = (Look)LOOK_NEUTRAL_INIT; return; }
    px_cfg_load_look(&g_cfg, "current", out);
}
void PxSetStoreLook(const Look *lk)
{
    if (!g_ready || !lk) return;
    px_cfg_store_look(&g_cfg, "current", lk);
    g_cfg_dirty = 1;
}
