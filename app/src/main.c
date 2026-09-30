/* PlexusX — Elite Windows Gaming Display & Visual Optimization Center
 * Main Entry Point: Window Lifecycle, Tray Icon, Hotkeys, Timers & Persistence.
 */
#include "common.h"
#include "color_math.h"
#include <shlobj.h>
#include <signal.h>

HWND      g_hwnd = NULL;
HINSTANCE g_inst = NULL;
wchar_t   g_appdir[MAX_PATH];
wchar_t   g_appdata[MAX_PATH];

static wchar_t g_exepath[MAX_PATH];
static wchar_t g_rampsfile[MAX_PATH];
static wchar_t g_dirtyfile[MAX_PATH];
static int     g_reapply_tick = 0;

#define TIMER_POLL 1

/* Tray IDs */
#define TRAY_ID        1
#define TRAY_OPEN      101
#define TRAY_LOOK      102
#define TRAY_XH        103
#define TRAY_GAME_MODE 104
#define TRAY_RESET     105
#define TRAY_EXIT      106
#define TRAY_EMERGENCY 107

const wchar_t *Main_GetExePath(void) { return g_exepath; }
const wchar_t *Main_GetAppDataPath(void) { return g_appdata; }

/* ---------------- Crash Diagnostics & Display Safety Net ---------------- */
/* An unhandled exception must never leave the user staring at a stuck colour matrix or a
 * modified gamma ramp.  The filter writes a report to %LocalAppData%\PlexusX\crash.log and then
 * calls Eng_Reset() (identity colour matrix + the original ramps).  It is deliberately
 * primitive: fixed buffers, raw Win32 file I/O and hand-rolled hex formatting, so it does not
 * depend on the CRT, the heap or any state that the crash may have damaged. */
static wchar_t      g_crashlog[MAX_PATH];
static char         g_crash_hdr[MAX_PATH * 3 + 64];   /* "version / build / exe", prepared at start-up */
static volatile LONG g_crash_stage = 0;

static char *cr_put(char *p, char *end, const char *s)
{
    while (*s && p < end - 1) *p++ = *s++;
    return p;
}

static char *cr_hex(char *p, char *end, unsigned long long v, int digits)
{
    static const char hexd[] = "0123456789ABCDEF";
    if (p + digits + 2 >= end) return p;
    *p++ = '0';
    *p++ = 'x';
    for (int i = digits - 1; i >= 0; i--) *p++ = hexd[(v >> (i * 4)) & 0xF];
    return p;
}

static char *cr_dec(char *p, char *end, unsigned long v, int min_digits)
{
    char tmp[16];
    int n = 0;
    do { tmp[n++] = (char)('0' + v % 10); v /= 10; } while (v && n < 15);
    while (n < min_digits && n < 15) tmp[n++] = '0';
    while (n && p < end - 1) *p++ = tmp[--n];
    return p;
}

/* "0x00007FF6A1B21234 [PlexusX.exe+0x00011234]" */
static char *cr_addr(char *p, char *end, unsigned long long addr)
{
    p = cr_hex(p, end, addr, 16);
    HMODULE mod = NULL;
    if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                           GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           (LPCWSTR)(ULONG_PTR)addr, &mod) && mod) {
        char name[MAX_PATH];
        DWORD n = GetModuleFileNameA(mod, name, MAX_PATH);
        if (n && n < MAX_PATH) {
            const char *base = name;
            for (DWORD i = 0; i < n; i++) if (name[i] == '\\') base = name + i + 1;
            p = cr_put(p, end, " [");
            p = cr_put(p, end, base);
            p = cr_put(p, end, "+");
            p = cr_hex(p, end, addr - (unsigned long long)(ULONG_PTR)mod, 8);
            p = cr_put(p, end, "]");
        }
    }
    return p;
}

static const char *cr_exception_name(DWORD code)
{
    switch (code) {
    case EXCEPTION_ACCESS_VIOLATION:      return "EXCEPTION_ACCESS_VIOLATION";
    case EXCEPTION_ARRAY_BOUNDS_EXCEEDED: return "EXCEPTION_ARRAY_BOUNDS_EXCEEDED";
    case EXCEPTION_BREAKPOINT:            return "EXCEPTION_BREAKPOINT";
    case EXCEPTION_DATATYPE_MISALIGNMENT: return "EXCEPTION_DATATYPE_MISALIGNMENT";
    case EXCEPTION_FLT_DIVIDE_BY_ZERO:    return "EXCEPTION_FLT_DIVIDE_BY_ZERO";
    case EXCEPTION_FLT_INVALID_OPERATION: return "EXCEPTION_FLT_INVALID_OPERATION";
    case EXCEPTION_ILLEGAL_INSTRUCTION:   return "EXCEPTION_ILLEGAL_INSTRUCTION";
    case EXCEPTION_IN_PAGE_ERROR:         return "EXCEPTION_IN_PAGE_ERROR";
    case EXCEPTION_INT_DIVIDE_BY_ZERO:    return "EXCEPTION_INT_DIVIDE_BY_ZERO";
    case EXCEPTION_PRIV_INSTRUCTION:      return "EXCEPTION_PRIV_INSTRUCTION";
    case EXCEPTION_STACK_OVERFLOW:        return "EXCEPTION_STACK_OVERFLOW";
    default:                              return "unknown exception";
    }
}

static void cr_append(const char *buf, size_t len)
{
    if (!g_crashlog[0] || !len) return;
    HANDLE h = CreateFileW(g_crashlog, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                           OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return;
    DWORD wr = 0;
    WriteFile(h, buf, (DWORD)len, &wr, NULL);
    CloseHandle(h);
}

/* Part 1: what happened.  Kept short and simple so it is written before anything risky runs. */
static void cr_write_summary(EXCEPTION_POINTERS *ep)
{
    static char buf[2048];
    char *p = buf, *end = buf + sizeof buf;
    const EXCEPTION_RECORD *er = ep->ExceptionRecord;
    SYSTEMTIME st;
    GetLocalTime(&st);

    p = cr_put(p, end, "\r\n==================== PlexusX crash report ====================\r\n");
    p = cr_put(p, end, "Time       : ");
    p = cr_dec(p, end, st.wYear, 4);   p = cr_put(p, end, "-");
    p = cr_dec(p, end, st.wMonth, 2);  p = cr_put(p, end, "-");
    p = cr_dec(p, end, st.wDay, 2);    p = cr_put(p, end, " ");
    p = cr_dec(p, end, st.wHour, 2);   p = cr_put(p, end, ":");
    p = cr_dec(p, end, st.wMinute, 2); p = cr_put(p, end, ":");
    p = cr_dec(p, end, st.wSecond, 2); p = cr_put(p, end, " (local)\r\n");
    p = cr_put(p, end, g_crash_hdr);
    p = cr_put(p, end, "Exception  : ");
    p = cr_hex(p, end, er->ExceptionCode, 8);
    p = cr_put(p, end, " ");
    p = cr_put(p, end, cr_exception_name(er->ExceptionCode));
    p = cr_put(p, end, "\r\nAddress    : ");
    p = cr_addr(p, end, (unsigned long long)(ULONG_PTR)er->ExceptionAddress);
    if ((er->ExceptionCode == EXCEPTION_ACCESS_VIOLATION || er->ExceptionCode == EXCEPTION_IN_PAGE_ERROR) &&
        er->NumberParameters >= 2) {
        p = cr_put(p, end, "\r\nAccess     : ");
        p = cr_put(p, end, er->ExceptionInformation[0] == 0 ? "read of " :
                           er->ExceptionInformation[0] == 1 ? "write to " : "execute of ");
        p = cr_hex(p, end, (unsigned long long)er->ExceptionInformation[1], 16);
    }
    p = cr_put(p, end, "\r\nThread     : ");
    p = cr_hex(p, end, GetCurrentThreadId(), 8);
    p = cr_put(p, end, "\r\n");
    cr_append(buf, (size_t)(p - buf));
}

/* Part 2: registers and a stack walk (x64 unwind tables).  Runs after the display has been restored. */
static void cr_write_trace(EXCEPTION_POINTERS *ep)
{
    static char buf[4096];
    char *p = buf, *end = buf + sizeof buf;

#if defined(__x86_64__)
    const CONTEXT *c = ep->ContextRecord;
    if (c) {
        p = cr_put(p, end, "Registers  : RIP="); p = cr_hex(p, end, c->Rip, 16);
        p = cr_put(p, end, " RSP="); p = cr_hex(p, end, c->Rsp, 16);
        p = cr_put(p, end, " RBP="); p = cr_hex(p, end, c->Rbp, 16);
        p = cr_put(p, end, "\r\n             RAX="); p = cr_hex(p, end, c->Rax, 16);
        p = cr_put(p, end, " RBX="); p = cr_hex(p, end, c->Rbx, 16);
        p = cr_put(p, end, " RCX="); p = cr_hex(p, end, c->Rcx, 16);
        p = cr_put(p, end, " RDX="); p = cr_hex(p, end, c->Rdx, 16);
        p = cr_put(p, end, "\r\nStack      :\r\n");

        CONTEXT ctx = *c;
        for (int i = 0; i < 32 && ctx.Rip; i++) {
            p = cr_put(p, end, "  #"); p = cr_dec(p, end, (unsigned long)i, 2); p = cr_put(p, end, " ");
            p = cr_addr(p, end, ctx.Rip);
            p = cr_put(p, end, "\r\n");

            DWORD64 image_base = 0;
            PRUNTIME_FUNCTION fe = RtlLookupFunctionEntry(ctx.Rip, &image_base, NULL);
            if (fe) {
                PVOID handler_data = NULL;
                DWORD64 frame = 0;
                RtlVirtualUnwind(UNW_FLAG_NHANDLER, image_base, ctx.Rip, fe, &ctx, &handler_data, &frame, NULL);
            } else {
                /* leaf function: the return address is at [rsp]; read it without risking a nested fault */
                DWORD64 ret = 0;
                SIZE_T got = 0;
                if (!ReadProcessMemory(GetCurrentProcess(), (LPCVOID)(ULONG_PTR)ctx.Rsp, &ret, sizeof ret, &got) ||
                    got != sizeof ret) break;
                ctx.Rip = ret;
                ctx.Rsp += 8;
            }
        }
    }
#else
    (void)ep;
#endif
    p = cr_put(p, end, "Display    : Eng_Reset() ran - colour matrix back to identity, gamma ramps restored\r\n");
    cr_append(buf, (size_t)(p - buf));
}

static LONG WINAPI crash_filter(EXCEPTION_POINTERS *ep)
{
    /* stage 1 = the original fault, stage 2 = a fault inside this handler, 3+ = give up */
    LONG stage = InterlockedIncrement(&g_crash_stage);
    if (stage == 1 && ep && ep->ExceptionRecord) {
        cr_write_summary(ep);     /* 1. record what happened (tiny, safe) */
        Eng_Reset();              /* 2. hand the desktop back: identity matrix + original ramps */
        cr_write_trace(ep);       /* 3. registers + stack walk (the risky part goes last) */
    } else if (stage == 2) {
        Eng_Reset();              /* the handler itself faulted: one more attempt to restore the display */
    }
    return EXCEPTION_EXECUTE_HANDLER;   /* terminate quietly: no "has stopped working" dialog */
}

/* abort() / assert failures never reach SEH: restore the display for them too */
static void crash_abort_handler(int sig)
{
    (void)sig;
    Eng_Reset();
    TerminateProcess(GetCurrentProcess(), 3);
}

static void crash_install(void)
{
    wchar_t dir[MAX_PATH];
    dir[0] = 0;
    if (SHGetFolderPathW(NULL, CSIDL_LOCAL_APPDATA, NULL, SHGFP_TYPE_CURRENT, dir) != S_OK) {
        DWORD n = GetTempPathW(MAX_PATH, dir);
        if (!n || n >= MAX_PATH) dir[0] = 0;
        else if (dir[n - 1] == L'\\') dir[n - 1] = 0;
    }
    if (dir[0] && lstrlenW(dir) < MAX_PATH - 24) {
        lstrcatW(dir, L"\\PlexusX");
        CreateDirectoryW(dir, NULL);
        wsprintfW(g_crashlog, L"%s\\crash.log", dir);

        /* keep the log small: start over once it grows past 256 KB */
        WIN32_FILE_ATTRIBUTE_DATA fa;
        if (GetFileAttributesExW(g_crashlog, GetFileExInfoStandard, &fa) &&
            (fa.nFileSizeHigh || fa.nFileSizeLow > 256 * 1024)) DeleteFileW(g_crashlog);
    }

    /* "Version / Build / Executable" lines, prepared now so the filter never has to convert strings */
    char ver[64], build[64], exe[MAX_PATH * 2];
    wchar_t exew[MAX_PATH];
    GetModuleFileNameW(NULL, exew, MAX_PATH);
    WideCharToMultiByte(CP_UTF8, 0, PX_VERSION, -1, ver, sizeof ver, NULL, NULL);
    WideCharToMultiByte(CP_UTF8, 0, PX_BUILD_DATE, -1, build, sizeof build, NULL, NULL);
    WideCharToMultiByte(CP_UTF8, 0, exew, -1, exe, sizeof exe, NULL, NULL);
    char *p = g_crash_hdr, *end = g_crash_hdr + sizeof g_crash_hdr;
    p = cr_put(p, end, "Version    : "); p = cr_put(p, end, ver);
    p = cr_put(p, end, " (build ");      p = cr_put(p, end, build);
    p = cr_put(p, end, ")\r\nExecutable : "); p = cr_put(p, end, exe);
    p = cr_put(p, end, "\r\n");
    *p = 0;

    SetUnhandledExceptionFilter(crash_filter);
    signal(SIGABRT, crash_abort_handler);
}

/* ---------------- High Quality Geometric Icon ---------------- */
static HICON build_app_icon(int sz)
{
    BITMAPINFOHEADER bi;
    memset(&bi, 0, sizeof bi);
    bi.biSize = sizeof bi;
    bi.biWidth = sz;
    bi.biHeight = -sz;
    bi.biPlanes = 1;
    bi.biBitCount = 32;
    bi.biCompression = BI_RGB;

    void *pv = NULL;
    HBITMAP color = CreateDIBSection(NULL, (BITMAPINFO *)&bi, DIB_RGB_COLORS, &pv, NULL, 0);
    HBITMAP mask  = CreateBitmap(sz, sz, 1, 1, NULL);
    if (!color || !pv) return LoadIcon(NULL, IDI_APPLICATION);

    DWORD *px = pv;
    for (int y = 0; y < sz; y++) {
        for (int x = 0; x < sz; x++) {
            int i = y * sz + x;
            int corner = (x < 2 || y < 2 || x >= sz - 2 || y >= sz - 2);
            DWORD bg = corner ? 0x00000000 : 0xFF121218;

            int t = max(sz / 7, 2);
            int in_x = ((abs(x - y) < t || abs(x + y - (sz - 1)) < t) &&
                        x > sz / 6 && x < sz - sz / 6 &&
                        y > sz / 6 && y < sz - sz / 6);
            px[i] = in_x ? 0xFFC6FF3D : bg; /* Lime X mark on dark tile */
        }
    }

    ICONINFO ii;
    ii.fIcon = TRUE;
    ii.xHotspot = ii.yHotspot = 0;
    ii.hbmMask = mask;
    ii.hbmColor = color;
    HICON ic = CreateIconIndirect(&ii);

    DeleteObject(color);
    DeleteObject(mask);
    return ic ? ic : LoadIcon(NULL, IDI_APPLICATION);
}

/* ---------------- Config Persistence ---------------- */
static void save_current_config(void)
{
    wchar_t f[MAX_PATH];
    wsprintfW(f, L"%s\\config.ini", g_appdata);
    wchar_t b[32];
    Look *l = Ui_Look();

    wsprintfW(b, L"%d", (int)l->enabled);     WritePrivateProfileStringW(L"current", L"enabled", b, f);
    wsprintfW(b, L"%d", (int)l->sat);         WritePrivateProfileStringW(L"current", L"sat", b, f);
    wsprintfW(b, L"%d", (int)l->vibrance);    WritePrivateProfileStringW(L"current", L"vibrance", b, f);
    wsprintfW(b, L"%d", (int)l->bri);         WritePrivateProfileStringW(L"current", L"bri", b, f);
    wsprintfW(b, L"%d", (int)l->con);         WritePrivateProfileStringW(L"current", L"con", b, f);
    wsprintfW(b, L"%d", (int)(l->gamma * 100)); WritePrivateProfileStringW(L"current", L"gamma", b, f);
    wsprintfW(b, L"%d", (int)l->temp);        WritePrivateProfileStringW(L"current", L"temp", b, f);
    wsprintfW(b, L"%d", (int)l->tint);        WritePrivateProfileStringW(L"current", L"tint", b, f);
    wsprintfW(b, L"%d", (int)l->r_gain);      WritePrivateProfileStringW(L"current", L"r_gain", b, f);
    wsprintfW(b, L"%d", (int)l->g_gain);      WritePrivateProfileStringW(L"current", L"g_gain", b, f);
    wsprintfW(b, L"%d", (int)l->b_gain);      WritePrivateProfileStringW(L"current", L"b_gain", b, f);
    wsprintfW(b, L"%d", (int)l->shadows);     WritePrivateProfileStringW(L"current", L"shadows", b, f);
    wsprintfW(b, L"%d", (int)l->highlights);  WritePrivateProfileStringW(L"current", L"highlights", b, f);
    wsprintfW(b, L"%d", (int)l->black_level); WritePrivateProfileStringW(L"current", L"black_level", b, f);
    wsprintfW(b, L"%d", (int)l->white_point); WritePrivateProfileStringW(L"current", L"white_point", b, f);
    wsprintfW(b, L"%d", (int)l->clarity);     WritePrivateProfileStringW(L"current", L"clarity", b, f);

    XhCfg x;
    Ui_GetXh(&x);
    wsprintfW(b, L"%d", x.on);         WritePrivateProfileStringW(L"xhair", L"on", b, f);
    wsprintfW(b, L"%d", x.shape);      WritePrivateProfileStringW(L"xhair", L"shape", b, f);
    wsprintfW(b, L"%d", x.size);       WritePrivateProfileStringW(L"xhair", L"size", b, f);
    wsprintfW(b, L"%d", x.gap);        WritePrivateProfileStringW(L"xhair", L"gap", b, f);
    wsprintfW(b, L"%d", x.thick);      WritePrivateProfileStringW(L"xhair", L"thick", b, f);
    wsprintfW(b, L"%d", x.opacity);    WritePrivateProfileStringW(L"xhair", L"opacity", b, f);
    wsprintfW(b, L"%d", x.outline);    WritePrivateProfileStringW(L"xhair", L"outline", b, f);
    wsprintfW(b, L"%d", x.center_dot); WritePrivateProfileStringW(L"xhair", L"center_dot", b, f);
    wsprintfW(b, L"%d", (int)x.color); WritePrivateProfileStringW(L"xhair", L"color", b, f);
}

static void load_current_config(void)
{
    wchar_t f[MAX_PATH];
    wsprintfW(f, L"%s\\config.ini", g_appdata);
    Look l;
    l.enabled = GetPrivateProfileIntW(L"current", L"enabled", 1, f);
    l.sat = (float)GetPrivateProfileIntW(L"current", L"sat", 150, f);
    l.vibrance = (float)GetPrivateProfileIntW(L"current", L"vibrance", 120, f);
    l.bri = (float)GetPrivateProfileIntW(L"current", L"bri", 100, f);
    l.con = (float)GetPrivateProfileIntW(L"current", L"con", 100, f);
    l.gamma = GetPrivateProfileIntW(L"current", L"gamma", 100, f) / 100.0f;
    l.temp = (float)GetPrivateProfileIntW(L"current", L"temp", 6500, f);
    l.tint = (float)GetPrivateProfileIntW(L"current", L"tint", 0, f);
    l.r_gain = (float)GetPrivateProfileIntW(L"current", L"r_gain", 100, f);
    l.g_gain = (float)GetPrivateProfileIntW(L"current", L"g_gain", 100, f);
    l.b_gain = (float)GetPrivateProfileIntW(L"current", L"b_gain", 100, f);
    l.shadows = (float)GetPrivateProfileIntW(L"current", L"shadows", 100, f);
    l.highlights = (float)GetPrivateProfileIntW(L"current", L"highlights", 100, f);
    l.black_level = (float)GetPrivateProfileIntW(L"current", L"black_level", 100, f);
    l.white_point = (float)GetPrivateProfileIntW(L"current", L"white_point", 100, f);
    l.clarity = (float)GetPrivateProfileIntW(L"current", L"clarity", 100, f);
    l.hue = 0.0f;
    cm_sanitize_look(&l);      /* a hand-edited or corrupt config can never feed NaN / absurd values to the engine */
    Ui_LoadLook(&l);

    XhCfg x;
    Ui_GetXh(&x);
    x.on = GetPrivateProfileIntW(L"xhair", L"on", 0, f);
    x.shape = GetPrivateProfileIntW(L"xhair", L"shape", XH_CROSS, f);
    x.size = GetPrivateProfileIntW(L"xhair", L"size", 16, f);
    x.gap = GetPrivateProfileIntW(L"xhair", L"gap", 4, f);
    x.thick = GetPrivateProfileIntW(L"xhair", L"thick", 2, f);
    x.opacity = GetPrivateProfileIntW(L"xhair", L"opacity", 100, f);
    x.outline = GetPrivateProfileIntW(L"xhair", L"outline", 1, f);
    x.center_dot = GetPrivateProfileIntW(L"xhair", L"center_dot", 1, f);
    x.color = (COLORREF)GetPrivateProfileIntW(L"xhair", L"color", RGB(0xC6, 0xFF, 0x3D), f);
    x.ocolor = (COLORREF)GetPrivateProfileIntW(L"xhair", L"ocolor", RGB(0, 0, 0), f);
    Ui_SetXh(&x);
}

void Main_Save(void)
{
    save_current_config();
    Prof_Save();
}

void Main_ApplyAll(void)
{
    Look *l = Ui_Look();
    Eng_Apply(l);

    XhCfg x;
    Ui_GetXh(&x);
    Xh_Update(&x);

    Phone_SetLook(l);
}

/* Ctrl+Alt+Shift+R / tray / Tools panel: the "get my screen back" button.
 * Closes every test pattern, bypasses the colour engine and restores the desktop
 * (identity colour matrix + original gamma ramps) unconditionally via Eng_Reset(). */
void Main_EmergencyReset(void)
{
    Tools_ClosePattern();

    Look *l = Ui_Look();
    *l = (Look)LOOK_NEUTRAL_INIT;
    l->enabled = 0;            /* stays bypassed until the user switches the engine back on */

    Eng_Reset();
    Ui_Notify(L"EMERGENCY RESET: patterns closed, color engine bypassed, display restored");
    Ui_RebuildPanel();
    Main_ApplyAll();           /* engine is bypassed: keeps crosshair / phone state in sync */
    if (g_hwnd) InvalidateRect(g_hwnd, NULL, FALSE);
}

/* ---------------- System Tray ---------------- */
static HICON g_tray_icon = NULL;

static void tray_add(HWND w)
{
    NOTIFYICONDATAW nid;
    memset(&nid, 0, sizeof nid);
    nid.cbSize = sizeof nid;
    nid.hWnd = w;
    nid.uID = TRAY_ID;
    nid.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    nid.uCallbackMessage = WM_APP_TRAY;
    nid.hIcon = g_tray_icon;
    lstrcpynW(nid.szTip, L"PlexusX — Gaming Display Optimizer", 128);
    Shell_NotifyIconW(NIM_ADD, &nid);
}

static void tray_del(void)
{
    NOTIFYICONDATAW nid;
    memset(&nid, 0, sizeof nid);
    nid.cbSize = sizeof nid;
    nid.hWnd = g_hwnd;
    nid.uID = TRAY_ID;
    Shell_NotifyIconW(NIM_DELETE, &nid);
}

static void tray_menu(void)
{
    POINT pt;
    GetCursorPos(&pt);
    XhCfg x;
    Ui_GetXh(&x);

    HMENU m = CreatePopupMenu();
    AppendMenuW(m, MF_STRING, TRAY_OPEN, L"Open PlexusX");
    AppendMenuW(m, MF_SEPARATOR, 0, NULL);
    AppendMenuW(m, MF_STRING, TRAY_LOOK, Ui_Look()->enabled ? L"Color Engine: Active" : L"Color Engine: Bypassed");
    AppendMenuW(m, MF_STRING, TRAY_XH, x.on ? L"Hide Crosshair Overlay" : L"Show Crosshair Overlay");
    AppendMenuW(m, MF_STRING, TRAY_GAME_MODE, Tools_IsGamingMode() ? L"Gaming Mode: ON" : L"Gaming Mode: OFF");
    AppendMenuW(m, MF_STRING, TRAY_RESET, L"Reset All Display Colors");
    AppendMenuW(m, MF_STRING, TRAY_EMERGENCY, L"Emergency Safe Reset  (Ctrl+Alt+Shift+R)");
    AppendMenuW(m, MF_SEPARATOR, 0, NULL);
    AppendMenuW(m, MF_STRING, TRAY_EXIT, L"Exit");

    SetForegroundWindow(g_hwnd);
    TrackPopupMenu(m, TPM_RIGHTBUTTON, pt.x, pt.y, 0, g_hwnd, NULL);
    DestroyMenu(m);
}

/* ---------------- Global Hotkeys ---------------- */
static void handle_hotkey(WPARAM id)
{
    Look *l = Ui_Look();
    switch (id) {
    case 1: /* Saturation +10% */
        l->sat = clampf(l->sat + 10.0f, 0, 300);
        Ui_Notify(L"Saturation +10%");
        break;
    case 2: /* Saturation -10% */
        l->sat = clampf(l->sat - 10.0f, 0, 300);
        Ui_Notify(L"Saturation −10%");
        break;
    case 3: /* Reset ALL channels (R/G/B gain, black/white) and tone curves (gamma, shadows, highlights, clarity) */
        *l = (Look)LOOK_NEUTRAL_INIT;
        l->enabled = 1;
        Eng_Reset();
        Ui_Notify(L"All Channels & Tone Curves Reset to Neutral");
        break;
    case 4: /* Toggle Crosshair */
        Xh_Toggle();
        Ui_Notify(Xh_IsActive() ? L"Crosshair Overlay: Enabled" : L"Crosshair Overlay: Hidden");
        break;
    case 5: /* Toggle Look On/Off */
        l->enabled = !l->enabled;
        Ui_Notify(l->enabled ? L"Color Engine: Active" : L"Color Engine: Bypassed");
        break;
    case 6: /* Toggle Gaming Mode */
        Tools_ToggleGamingMode();
        break;
    case 7: /* Ctrl+Alt+Shift+R: emergency safe reset */
        Main_EmergencyReset();
        return;
    }
    Ui_RebuildPanel();
    Main_ApplyAll();
    InvalidateRect(g_hwnd, NULL, FALSE);
}

/* ---------------- Window Procedure ---------------- */
static LRESULT CALLBACK wnd_proc(HWND wnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_CREATE:
        /* CreateWindowExW has not returned yet: publish the handle now.  Ui_Init() and the display
         * modes were initialised BEFORE this point, so nothing below can see half-built state. */
        g_hwnd = wnd;
        Ui_AttachWindow(wnd);
        return 0;
    case WM_SIZE:
        if (Ui_IsReady()) InvalidateRect(wnd, NULL, FALSE);
        return 0;
    case WM_DISPLAYCHANGE:
        /* A mode change resets the display pipeline on many drivers: re-assert the look */
        Eng_Resync();
        Main_ApplyAll();
        return 0;
    case WM_NCHITTEST: {
        POINT pt = { GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
        ScreenToClient(wnd, &pt);
        if (Ui_CapHit((int)pt.x, (int)pt.y)) return HTCLIENT;
        if (Ui_InTop((int)pt.x, (int)pt.y)) return HTCAPTION;
        return HTCLIENT;
    }
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(wnd, &ps);
        RECT rc;
        GetClientRect(wnd, &rc);

        HDC mem = CreateCompatibleDC(dc);
        HBITMAP bmp = CreateCompatibleBitmap(dc, rc.right, rc.bottom);
        HGDIOBJ ob = SelectObject(mem, bmp);

        Ui_Paint(mem, &rc);
        BitBlt(dc, 0, 0, rc.right, rc.bottom, mem, 0, 0, SRCCOPY);

        SelectObject(mem, ob);
        DeleteObject(bmp);
        DeleteDC(mem);
        EndPaint(wnd, &ps);
        return 0;
    }
    case WM_LBUTTONDOWN:
        SetCapture(wnd);
        Ui_MouseDown(GET_X_LPARAM(lp), GET_Y_LPARAM(lp));
        InvalidateRect(wnd, NULL, FALSE);
        return 0;
    case WM_MOUSEMOVE: {
        int r = Ui_MouseMove(GET_X_LPARAM(lp), GET_Y_LPARAM(lp), GetCapture() == wnd);
        if (r < 0) InvalidateRect(wnd, NULL, FALSE);
        return 0;
    }
    case WM_LBUTTONUP:
        ReleaseCapture();
        Ui_MouseUp(GET_X_LPARAM(lp), GET_Y_LPARAM(lp));
        InvalidateRect(wnd, NULL, FALSE);
        return 0;
    case WM_SETCURSOR: {
        if (LOWORD(lp) == HTCLIENT) {
            POINT pt;
            GetCursorPos(&pt);
            ScreenToClient(wnd, &pt);
            int id = Ui_Hover((int)pt.x, (int)pt.y);
            SetCursor(LoadCursor(NULL, (id > 0 && id < ID_CAP_MIN) ? IDC_HAND : IDC_ARROW));
            return TRUE;
        }
        break;
    }
    case WM_HOTKEY:
        handle_hotkey(wp);
        return 0;
    case WM_TIMER:
        if (wp == TIMER_POLL) {
            Prof_Poll();
            if (++g_reapply_tick >= 60) {
                g_reapply_tick = 0;
                Eng_Resync();          /* the driver / a game may have reset the LUT behind our back */
                Main_ApplyAll();
            }
            InvalidateRect(wnd, NULL, FALSE);
        }
        return 0;
    case WM_APP_LOOK: {
        char k = (char)wp;
        int v = (int)lp;
        Look *l = Ui_Look();
        switch (k) {
        case 's': l->sat = clampf((float)v, 0, 300); break;
        case 'v': l->vibrance = clampf((float)v, 0, 300); break;
        case 'b': l->bri = clampf((float)v, 0, 200); break;
        case 'c': l->con = clampf((float)v, 0, 200); break;
        case 't': l->temp = clampf((float)v, 3000, 10000); break;
        case 'g': l->gamma = clampf(v / 100.0f, 0.40f, 2.50f); break;
        }
        Ui_RebuildPanel();
        Main_ApplyAll();
        InvalidateRect(wnd, NULL, FALSE);
        return 0;
    }
    case WM_APP_TRAY:
        if (lp == WM_LBUTTONUP) {
            ShowWindow(wnd, SW_RESTORE);
            SetForegroundWindow(wnd);
        } else if (lp == WM_RBUTTONUP || lp == WM_CONTEXTMENU) {
            tray_menu();
        }
        return 0;
    case WM_COMMAND:
        switch (LOWORD(wp)) {
        case TRAY_OPEN:
            ShowWindow(wnd, SW_RESTORE);
            SetForegroundWindow(wnd);
            break;
        case TRAY_LOOK:
            Ui_Exec(ID_T_LOOK_ENABLE);
            break;
        case TRAY_XH:
            Xh_Toggle();
            break;
        case TRAY_GAME_MODE:
            Tools_ToggleGamingMode();
            break;
        case TRAY_RESET:
            Ui_Exec(ID_B_RESET_COLOR);
            break;
        case TRAY_EMERGENCY:
            Main_EmergencyReset();
            break;
        case TRAY_EXIT:
            PostMessageW(wnd, WM_CLOSE, 0, 0);
            break;
        default:
            Ui_Exec(LOWORD(wp));
            break;
        }
        return 0;
    case WM_CLOSE:
        DestroyWindow(wnd);
        return 0;
    case WM_DESTROY:
        tray_del();
        KillTimer(wnd, TIMER_POLL);
        Main_Save();
        Phone_Stop();
        Xh_Shutdown();
        Eng_Shutdown();
        Ui_Free();
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(wnd, msg, wp, lp);
}

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE prev, LPWSTR cmd, int show)
{
    (void)prev; (void)cmd;

    /* Safety net first: from here on any crash restores the desktop and leaves a report in
     * %LocalAppData%\PlexusX\crash.log */
    crash_install();

    /* Single Instance Mutex */
    HANDLE mutex = CreateMutexW(NULL, TRUE, PX_MUTEX);
    if (mutex && GetLastError() == ERROR_ALREADY_EXISTS) {
        HWND old = FindWindowW(PX_CLASS, NULL);
        if (old) {
            ShowWindow(old, SW_RESTORE);
            SetForegroundWindow(old);
        }
        return 0;
    }

    /* Per-Monitor DPI Awareness */
    HMODULE u32 = GetModuleHandleW(L"user32.dll");
    typedef BOOL (WINAPI *fn_SetCtx)(HANDLE);
    fn_SetCtx pSetCtx = (fn_SetCtx)GetProcAddress(u32, "SetProcessDpiAwarenessContext");
    if (pSetCtx) pSetCtx((HANDLE)-4 /* PER_MONITOR_AWARE_V2 */);
    else SetProcessDPIAware();

    g_inst = inst;
    GetModuleFileNameW(NULL, g_exepath, MAX_PATH);
    lstrcpynW(g_appdir, g_exepath, MAX_PATH);
    wchar_t *slash = wcsrchr(g_appdir, L'\\');
    if (slash) *slash = 0;

    /* AppData vs Portable directory */
    wchar_t portable_flag[MAX_PATH];
    wsprintfW(portable_flag, L"%s\\portable.dat", g_appdir);
    if (GetFileAttributesW(portable_flag) != INVALID_FILE_ATTRIBUTES) {
        lstrcpynW(g_appdata, g_appdir, MAX_PATH);
    } else {
        if (SHGetFolderPathW(NULL, CSIDL_APPDATA, NULL, 0, g_appdata) != S_OK) {
            lstrcpyW(g_appdata, g_appdir);
        }
        lstrcatW(g_appdata, L"\\PlexusX");
        CreateDirectoryW(g_appdata, NULL);
    }

    wsprintfW(g_rampsfile, L"%s\\ramps.dat", g_appdata);
    wsprintfW(g_dirtyfile, L"%s\\dirty.flg", g_appdata);

    /* Gamma crash recovery */
    int dirty = (GetFileAttributesW(g_dirtyfile) != INVALID_FILE_ATTRIBUTES);
    Eng_SetPaths(g_rampsfile, g_dirtyfile, dirty);
    Eng_Init();

    /* Register Window Class */
    WNDCLASSW wc;
    memset(&wc, 0, sizeof wc);
    wc.lpfnWndProc   = wnd_proc;
    wc.hInstance     = inst;
    wc.hCursor       = LoadCursor(NULL, IDC_ARROW);
    wc.hIcon         = g_tray_icon = build_app_icon(32);
    wc.lpszClassName = PX_CLASS;
    RegisterClassW(&wc);

    HDC sdc = GetDC(NULL);
    float sc = GetDeviceCaps(sdc, LOGPIXELSX) / 96.0f;
    ReleaseDC(NULL, sdc);
    if (sc < 0.75f) sc = 1.0f;

    int ww = (int)(PX_WIN_W * sc);
    int wh = (int)(PX_WIN_H * sc);

    int sx = (GetSystemMetrics(SM_CXSCREEN) - ww) / 2;
    int sy = (GetSystemMetrics(SM_CYSCREEN) - wh) / 2;
    if (sx < 0) sx = CW_USEDEFAULT;
    if (sy < 0) sy = CW_USEDEFAULT;

    /* Everything the first window messages (WM_CREATE, WM_SIZE, WM_PAINT, hit-testing) can touch
     * must exist BEFORE CreateWindowExW: display-mode / monitor enumeration, then the UI
     * (DPI scale, fonts, profiles, test-pattern class, widgets) and the saved look. */
    Modes_Refresh();
    Ui_Init(NULL, inst);
    load_current_config();
    Ui_RebuildPanel();          /* show the loaded look, not the built-in defaults */

    HWND created = CreateWindowExW(0, PX_CLASS, PX_APP_TITLE, WS_POPUP,
                                   sx, sy, ww, wh, NULL, NULL, inst, NULL);
    if (!created) {
        Eng_Shutdown();
        Ui_Free();
        return 1;
    }
    g_hwnd = created;           /* (already set by WM_CREATE; kept for clarity) */
    Ui_AttachWindow(g_hwnd);
    Xh_Init();

    ShowWindow(g_hwnd, show);
    UpdateWindow(g_hwnd);

    tray_add(g_hwnd);
    SetTimer(g_hwnd, TIMER_POLL, 1000, NULL);

    /* Register Global Hotkeys */
    RegisterHotKey(g_hwnd, 1, MOD_CONTROL | MOD_ALT, VK_UP);
    RegisterHotKey(g_hwnd, 2, MOD_CONTROL | MOD_ALT, VK_DOWN);
    RegisterHotKey(g_hwnd, 3, MOD_CONTROL | MOD_ALT, '0');
    RegisterHotKey(g_hwnd, 4, MOD_CONTROL | MOD_ALT, 'X');
    RegisterHotKey(g_hwnd, 5, MOD_CONTROL | MOD_ALT, 'E');
    RegisterHotKey(g_hwnd, 6, MOD_CONTROL | MOD_ALT, 'G');
    if (!RegisterHotKey(g_hwnd, 7, MOD_CONTROL | MOD_ALT | MOD_SHIFT | MOD_NOREPEAT, 'R'))
        Ui_Notify(L"Ctrl+Alt+Shift+R is used by another app - use the tray menu for Emergency Safe Reset");

    Main_ApplyAll();

    MSG m;
    while (GetMessageW(&m, NULL, 0, 0)) {
        TranslateMessage(&m);
        DispatchMessageW(&m);
    }

    if (mutex) {
        ReleaseMutex(mutex);
        CloseHandle(mutex);
    }
    return 0;
}
