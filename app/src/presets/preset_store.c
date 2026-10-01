/* PlexusX — PresetStore Win32 glue: %APPDATA%\PlexusX\presets.ini.
 *
 * Same safety rules as the settings store: UTF-16/BOM aware reads, atomic
 * writes (tmp + MoveFileEx), and a file that parses to nothing while non-empty
 * is moved aside as presets.ini.corrupt-N instead of being silently dropped or
 * propagated.  The library model itself lives in preset_store.h (pure, tested).
 */
#include "common.h"
#include "preset_store.h"

#define PXPRE_MAXTEXT (256 * 1024)

static PxPresetLib g_lib;
static wchar_t     g_path[MAX_PATH];
static int         g_corrupt = 0;
static int         g_ready = 0;

static size_t pre_read_text(const wchar_t *path, char *out, size_t cap)
{
    HANDLE h = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL,
                           OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return 0;
    {
        static BYTE raw[PXPRE_MAXTEXT + 4];
        DWORD rd = 0;
        BOOL ok = ReadFile(h, raw, sizeof raw - 4, &rd, NULL);
        CloseHandle(h);
        if (!ok || rd < 2) return 0;
        raw[rd] = 0; raw[rd + 1] = 0;
        if (raw[0] == 0xFF && raw[1] == 0xFE) {
            int wchars = (int)(rd / sizeof(WCHAR)) - 1;
            const WCHAR *src = (const WCHAR *)raw + 1;
            int need = WideCharToMultiByte(CP_UTF8, 0, src, wchars, NULL, 0, NULL, NULL);
            if (need <= 0) return 0;
            if ((size_t)need + 1 > cap) need = (int)cap - 1;
            int got = WideCharToMultiByte(CP_UTF8, 0, src, wchars, out, need, NULL, NULL);
            if (got <= 0) return 0;
            out[got] = 0;
            return (size_t)got;
        }
        {
            size_t n = rd;
            if (n >= 3 && raw[0] == 0xEF && raw[1] == 0xBB && raw[2] == 0xBF) { n -= 3; memmove(raw, raw + 3, n + 1); }
            if (n > cap - 1) n = cap - 1;
            memcpy(out, raw, n);
            out[n] = 0;
            return n;
        }
    }
}

static int pre_write_text(const wchar_t *path, const char *text)
{
    wchar_t tmp[MAX_PATH + 8];
    HANDLE h;
    DWORD wr = 0;
    size_t len = strlen(text);
    wsprintfW(tmp, L"%s.tmp", path);
    h = CreateFileW(tmp, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return -1;
    if (!WriteFile(h, text, (DWORD)len, &wr, NULL) || (size_t)wr != len || !FlushFileBuffers(h)) {
        CloseHandle(h);
        DeleteFileW(tmp);
        return -1;
    }
    CloseHandle(h);
    if (!MoveFileExW(tmp, path, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        DeleteFileW(tmp);
        return -1;
    }
    return 0;
}

static void pre_backup_corrupt(const wchar_t *path)
{
    wchar_t bak[MAX_PATH + 16];
    for (int n = 1; n <= 5; n++) {
        wsprintfW(bak, L"%s.corrupt%d", path, n);
        if (GetFileAttributesW(bak) == INVALID_FILE_ATTRIBUTES) {
            MoveFileW(path, bak);
            return;
        }
    }
}

int PxPre_Init(const wchar_t *appdata)
{
    char *buf = (char *)malloc(PXPRE_MAXTEXT);
    size_t n = 0;
    wsprintfW(g_path, L"%s\\presets.ini", appdata);
    memset(&g_lib, 0, sizeof g_lib);
    if (!buf) { PxPre_SeedBuiltins(&g_lib); g_ready = 1; return g_lib.n; }

    n = pre_read_text(g_path, buf, PXPRE_MAXTEXT);
    if (n == 0) {
        /* first run (or a deliberately emptied file): seed the built-ins */
        PxPre_SeedBuiltins(&g_lib);
        g_ready = 1;
        PxPre_Save();
        free(buf);
        return g_lib.n;
    }
    if (PxPre_Parse(&g_lib, buf) <= 0) {
        pre_backup_corrupt(g_path);
        g_corrupt = 1;
        memset(&g_lib, 0, sizeof g_lib);
        PxPre_SeedBuiltins(&g_lib);
    }
    /* A file written by an older schema gets migrated on the next save: the
     * parser already filled every missing field with a safe default. */
    g_ready = 1;
    free(buf);
    return g_lib.n;
}

int PxPre_Save(void)
{
    char *out;
    int len;
    if (!g_ready) return -1;
    out = (char *)malloc(PXPRE_MAXTEXT);
    if (!out) return -1;
    len = PxPre_Serialize(&g_lib, out, PXPRE_MAXTEXT);
    if (len <= 0) { free(out); return -1; }
    {
        int rc = pre_write_text(g_path, out);
        free(out);
        return rc;
    }
}

PxPresetLib *PxPre_Lib(void) { return &g_lib; }
int PxPre_Corrupt(void) { return g_corrupt; }

int PxPre_ExportFile(const wchar_t *path)
{
    char *out;
    int len, rc;
    if (!path) return -1;
    out = (char *)malloc(PXPRE_MAXTEXT);
    if (!out) return -1;
    len = PxPre_ToJson(&g_lib, out, PXPRE_MAXTEXT);
    if (len <= 0) { free(out); return -1; }
    rc = pre_write_text(path, out);
    free(out);
    return rc;
}

int PxPre_ImportFile(const wchar_t *path)
{
    char *buf;
    size_t n;
    int added;
    if (!path) return -1;
    buf = (char *)malloc(PXPRE_MAXTEXT);
    if (!buf) return -1;
    n = pre_read_text(path, buf, PXPRE_MAXTEXT);
    if (n == 0) { free(buf); return -1; }
    /* Accept both our own INI and the shareable JSON. */
    added = (buf[0] == '{') ? PxPre_FromJson(&g_lib, buf)
                            : PxPre_Parse(&g_lib, buf);
    free(buf);
    if (added > 0) PxPre_Save();
    return added;
}
