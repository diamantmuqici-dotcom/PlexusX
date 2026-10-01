/* PlexusX — Elite Windows Gaming Display & Visual Optimization Center
 * Main Entry Point: Window Lifecycle, Tray Icon, Hotkeys, Timers & Persistence.
 */
#include "common.h"
#include <shlobj.h>
#include <signal.h>
#include <dwmapi.h>
#include <wtsapi32.h>

#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif
#ifndef DWMWA_WINDOW_CORNER_PREFERENCE
#define DWMWA_WINDOW_CORNER_PREFERENCE 33
#define DWMWCP_ROUND 2
#endif

HWND      g_hwnd = NULL;
static int g_force_quit;      /* tray Exit / shutdown: bypass minimize-to-tray */
HINSTANCE g_inst = NULL;
wchar_t   g_appdir[MAX_PATH];
wchar_t   g_appdata[MAX_PATH];

static wchar_t g_exepath[MAX_PATH];
static wchar_t g_rampsfile[MAX_PATH];
static wchar_t g_dirtyfile[MAX_PATH];
static int     g_detect_tick = 0;

static int startup_enabled(void);

#define TIMER_POLL 1
#define TIMER_POLL_MS 250

/* Tray IDs */
#define TRAY_ID        1
#define TRAY_OPEN      101
#define TRAY_LOOK      102
#define TRAY_XH        103
#define TRAY_GAME_MODE 104
#define TRAY_RESET     105
#define TRAY_EXIT      106
#define TRAY_EMERGENCY 107
#define TRAY_KEEP_MODE 108
#define TRAY_REVERT_MODE 109

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

/* ---------------- Config Persistence ----------------
 * The file format, atomicity and corruption handling belong to the SettingsStore
 * (settings/settings_store.*).  main.c only translates live UI state into keys. */
static void save_current_config(void)
{
    PxSetStoreLook(Ui_Look());                 /* [current] keeps the FULL-precision look */

    PxIni *c = PxSet_Cfg();
    px_ini_set_int(c, "ui", "glass",         Ui_GlassEnabled());
    px_ini_set_int(c, "ui", "bg",           Ui_BgMode());
    px_ini_set_int(c, "ui", "reduce_motion", Ui_ReduceMotion());
    px_ini_set_int(c, "ui", "anim",         Ui_AnimLevel());
    px_ini_set_int(c, "ui", "startup",      startup_enabled());

    XhCfg x;
    Ui_GetXh(&x);
    px_ini_set_int(c, "xhair", "on",         x.on);
    px_ini_set_int(c, "xhair", "shape",      x.shape);
    px_ini_set_int(c, "xhair", "size",       x.size);
    px_ini_set_int(c, "xhair", "gap",        x.gap);
    px_ini_set_int(c, "xhair", "thick",      x.thick);
    px_ini_set_int(c, "xhair", "opacity",    x.opacity);
    px_ini_set_int(c, "xhair", "outline",    x.outline);
    px_ini_set_int(c, "xhair", "center_dot", x.center_dot);
    px_ini_set_int(c, "xhair", "dot_size",   x.dot_size);
    px_ini_set_int(c, "xhair", "color",      (int)x.color);
    px_ini_set_int(c, "xhair", "ocolor",     (int)x.ocolor);
    PxSet_Flush();
}

static void load_current_config(void)
{
    const PxIni *c = PxSet_CfgRO();

    Look l;
    PxSetLoadLook(&l);          /* already sanitized + range-clamped by the store */
    Ui_LoadLook(&l);

    Ui_LoadAppearance(px_ini_get_int(c, "ui", "glass", 1),
                      px_ini_get_int(c, "ui", "bg", 1),
                      px_ini_get_int(c, "ui", "reduce_motion", 0),
                      px_ini_get_int(c, "ui", "anim", 2),
                      startup_enabled());

    XhCfg x;
    Ui_GetXh(&x);
    x.on         = px_ini_get_int(c, "xhair", "on", 0);
    x.shape      = px_ini_get_int(c, "xhair", "shape", XH_CROSS);
    x.size       = px_ini_get_int(c, "xhair", "size", 16);
    x.gap        = px_ini_get_int(c, "xhair", "gap", 4);
    x.thick      = px_ini_get_int(c, "xhair", "thick", 2);
    x.opacity    = px_ini_get_int(c, "xhair", "opacity", 100);
    x.outline    = px_ini_get_int(c, "xhair", "outline", 1);
    x.center_dot = px_ini_get_int(c, "xhair", "center_dot", 1);
    x.dot_size   = px_ini_get_int(c, "xhair", "dot_size", 2);
    x.color      = (COLORREF)px_ini_get_int(c, "xhair", "color", (int)RGB(0xC6, 0xFF, 0x3D));
    x.ocolor     = (COLORREF)px_ini_get_int(c, "xhair", "ocolor", 0);
    Ui_SetXh(&x);
}

void Main_Save(void)
{
    save_current_config();
    Prof_Save();
}

/* Diagnostics composer: the app-wide truth = engine half + display/game halves.
 * The engine never reaches into display/game modules; the snapshot is assembled
 * here, at the top of the stack. */
void Main_FillSnapshot(PxEngineSnapshot *s)
{
    if (!s) return;
    PxSnap_Init(s);
    Eng_FillSnapshot(s);
    MonitorInfo *mi = Modes_GetMonitor(Modes_CurrentMonitorIndex());
    if (mi) s->monitor = *mi;
    const GpuInfo *gpu = Dm_GpuInfo();
    if (gpu) s->gpu = *gpu;
    const PxGameDisplayState *gs = Prof_GameState();
    if (gs) s->game = *gs;
    s->coalesced_events = Wm_CoalescedEvents();
}

void Main_ApplyAll(void)
{
    Look *l = Ui_Look();
    Eng_Apply(l);

    XhCfg x;
    Ui_GetXh(&x);
    Xh_Update(&x);

    Phone_SetLook(l);
    if (Ui_IsReady()) Prof_SyncApplied(L"apply");
}

void Main_SetStartup(int on)
{
    HKEY k;
    if (RegOpenKeyExW(HKEY_CURRENT_USER,
                      L"Software\\Microsoft\\Windows\\CurrentVersion\\Run",
                      0, KEY_SET_VALUE, &k) != ERROR_SUCCESS)
        return;
    if (on) {
        RegSetValueExW(k, L"PlexusX", 0, REG_SZ, (const BYTE *)g_exepath,
                       (DWORD)((lstrlenW(g_exepath) + 1) * sizeof(wchar_t)));
    } else {
        RegDeleteValueW(k, L"PlexusX");
    }
    RegCloseKey(k);
}

static int startup_enabled(void)
{
    HKEY k;
    wchar_t v[MAX_PATH];
    DWORD sz = sizeof v, type = 0;
    if (RegOpenKeyExW(HKEY_CURRENT_USER,
                      L"Software\\Microsoft\\Windows\\CurrentVersion\\Run",
                      0, KEY_QUERY_VALUE, &k) != ERROR_SUCCESS)
        return 0;
    LONG r = RegQueryValueExW(k, L"PlexusX", NULL, &type, (LPBYTE)v, &sz);
    RegCloseKey(k);
    return r == ERROR_SUCCESS;
}

void Main_ApplyChrome(void)
{
    if (!g_hwnd) return;

    BOOL dark = TRUE;
    DwmSetWindowAttribute(g_hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &dark, sizeof dark);
    int corner = DWMWCP_ROUND;
    DwmSetWindowAttribute(g_hwnd, DWMWA_WINDOW_CORNER_PREFERENCE, &corner, sizeof corner);

    LONG_PTR ex = GetWindowLongPtrW(g_hwnd, GWL_EXSTYLE);
    if (Ui_GlassEnabled()) {
        SetWindowLongPtrW(g_hwnd, GWL_EXSTYLE, ex | WS_EX_LAYERED);
        BYTE alpha = Ui_ReduceMotion() ? (BYTE)252 : (BYTE)242;
        SetLayeredWindowAttributes(g_hwnd, 0, alpha, LWA_ALPHA);
        DWM_BLURBEHIND bb;
        memset(&bb, 0, sizeof bb);
        bb.dwFlags = DWM_BB_ENABLE;
        bb.fEnable = TRUE;
        DwmEnableBlurBehindWindow(g_hwnd, &bb);
    } else {
        SetWindowLongPtrW(g_hwnd, GWL_EXSTYLE, ex & ~(LONG_PTR)WS_EX_LAYERED);
        DWM_BLURBEHIND bb;
        memset(&bb, 0, sizeof bb);
        bb.dwFlags = DWM_BB_ENABLE;
        bb.fEnable = FALSE;
        DwmEnableBlurBehindWindow(g_hwnd, &bb);
    }
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
    if (Modes_PendingChange()) {
        char label[64];
        wchar_t wlabel[80];
        Modes_PendingMode(label, sizeof label);
        MultiByteToWideChar(CP_UTF8, 0, label, -1, wlabel, 80);
        {
            wchar_t line[160];
            wsprintfW(line, L"Keep display mode %s  (%d s)", wlabel, Modes_PendingSecondsLeft());
            AppendMenuW(m, MF_STRING, TRAY_KEEP_MODE, line);
        }
        AppendMenuW(m, MF_STRING, TRAY_REVERT_MODE, L"Revert display mode now");
        AppendMenuW(m, MF_SEPARATOR, 0, NULL);
    }
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
    /* copy → edit → Eng_SetLook: the engine bumps its revision, so a hotkey that
     * changes nothing is skipped and a hotkey that changes something always
     * re-applies and re-verifies.  Never edit the authoritative state in place. */
    Look l = *Eng_GetRequested();
    switch (id) {
    case 1: /* Saturation +10% */
        l.sat = clampf(l.sat + 10.0f, 0, 300);
        Ui_Notify(L"Saturation +10%");
        break;
    case 2: /* Saturation -10% */
        l.sat = clampf(l.sat - 10.0f, 0, 300);
        Ui_Notify(L"Saturation −10%");
        break;
    case 3: /* Reset ALL channels (R/G/B gain, black/white) and tone curves (gamma, shadows, highlights, clarity) */
        l = (Look)LOOK_NEUTRAL_INIT;
        l.enabled = 1;
        Eng_Reset();
        Ui_Notify(L"All Channels & Tone Curves Reset to Neutral");
        break;
    case 4: /* Toggle Crosshair */
        Xh_Toggle();
        Ui_Notify(Xh_IsActive() ? L"Crosshair Overlay: Enabled" : L"Crosshair Overlay: Hidden");
        Ui_RebuildPanel();
        InvalidateRect(g_hwnd, NULL, FALSE);
        return;
    case 5: /* Toggle Look On/Off */
        l.enabled = !l.enabled;
        Ui_Notify(l.enabled ? L"Color Engine: Active" : L"Color Engine: Bypassed");
        break;
    case 6: /* Toggle Gaming Mode */
        Tools_ToggleGamingMode();
        Ui_RebuildPanel();
        InvalidateRect(g_hwnd, NULL, FALSE);
        return;
    case 7: /* Ctrl+Alt+Shift+R: emergency safe reset */
        Main_EmergencyReset();
        return;
    }
    Eng_SetLook(&l);
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
    /* All focus / display / device / session reactions live in the WindowManager
     * (windows/window_manager.c): the engine cache is invalidated, the REQUESTED
     * look is re-asserted, and the one coalesced follow-up is scheduled.  Never a
     * fixed sleep; never a rewrite of user settings from a GPU readout. */
    case WM_DISPLAYCHANGE:
        Modes_Refresh();
        Wm_OnSystemEvent(PXWIN_EV_DISPLAYCHANGE);
        if (Ui_IsReady()) { Ui_RebuildPanel(); InvalidateRect(wnd, NULL, FALSE); }
        return 0;
    case WM_ACTIVATEAPP:
        /* ALT+TAB back: DWM often drops MagSetFullscreenColorEffect while the
         * game was focused.  Re-assert exactly what the ColorState requests. */
        Wm_OnActivate(wp ? 1 : 0);
        if (!wp && Ui_IsReady()) { Phone_PushProfileChange(L"app-hidden"); Eng_BackupCurrentState(); }
        return 0;
    case WM_WTSSESSION_CHANGE:
        if (wp == WTS_SESSION_UNLOCK || wp == WTS_CONSOLE_CONNECT || wp == WTS_SESSION_LOGON)
            Wm_OnSystemEvent(PXWIN_EV_SESSION);
        return 0;
    case WM_POWERBROADCAST:
        if (wp == PBT_APMRESUMEAUTOMATIC || wp == PBT_APMRESUMESUSPEND)
            Wm_OnSystemEvent(PXWIN_EV_RESUME);
        return TRUE;
    case WM_DEVICECHANGE:
        Modes_Refresh();
        Wm_OnSystemEvent(PXWIN_EV_DEVICECHANGE);
        return TRUE;
    case WM_APP_FOREGROUND:
        Wm_OnForegroundEvent();
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
    case WM_LBUTTONDBLCLK:
        if (Ui_DoubleClick(GET_X_LPARAM(lp), GET_Y_LPARAM(lp)))
            InvalidateRect(wnd, NULL, FALSE);
        return 0;
    case WM_MOUSEWHEEL: {
        POINT pt = { GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
        ScreenToClient(wnd, &pt);
        if (Ui_Wheel((int)pt.x, (int)pt.y, GET_WHEEL_DELTA_WPARAM(wp)))
            InvalidateRect(wnd, NULL, FALSE);
        return 0;
    }
    case WM_KEYDOWN:
        if (Ui_Key((int)wp,
                   (GetKeyState(VK_CONTROL) & 0x8000) != 0,
                   (GetKeyState(VK_SHIFT) & 0x8000) != 0))
            InvalidateRect(wnd, NULL, FALSE);
        return 0;
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
        if (wp == PX_SETTLE_MS) {
            Wm_OnSettleTimer();          /* coalesced ALT+TAB follow-up re-assert */
            InvalidateRect(wnd, NULL, FALSE);
        } else if (wp == TIMER_POLL) {
            Wm_Tick();                   /* delayed game-profile applies */
            /* Display safety: unconfirmed mode changes roll back on their own. */
            if (Modes_RollbackTick()) {
                Ui_Notify(L"Display change was not confirmed — the previous mode was restored");
                Ui_RebuildPanel();
                InvalidateRect(wnd, NULL, FALSE);
            } else if (Modes_PendingChange()) {
                InvalidateRect(wnd, NULL, FALSE);   /* repaint the countdown bar */
            }
            /* Slow fallback only for missed WinEvents — never a color reapply loop. */
            if (++g_detect_tick >= 16) {
                g_detect_tick = 0;
                if (Prof_Poll()) {
                    Wm_ReassertNow(L"detect-fallback");
                    InvalidateRect(wnd, NULL, FALSE);
                }
            }
        }
        return 0;
    case WM_APP_LOOK: {
        /* remote slider edit (phone): one character per channel, value in lParam.
         * Ranges are clamped HERE as well as in the engine, so no remote input
         * can ever leave the domain the pipeline accepts. */
        char k = (char)wp;
        int v = (int)lp;
        /* copy → edit → Eng_SetLook: the revision bump happens inside the
         * engine, so an edit that changes nothing does not cause a re-apply. */
        Look l = *Eng_GetRequested();
        switch (k) {
        case 's': l.sat = clampf((float)v, 0, 300); break;
        case 'v': l.vibrance = clampf((float)v, 0, 300); break;
        case 'b': l.bri = clampf((float)v, 0, 200); break;
        case 'c': l.con = clampf((float)v, 0, 200); break;
        case 't': l.temp = clampf((float)v, 3000, 10000); break;
        case 'g': l.gamma = clampf(v / 100.0f, 0.40f, 2.50f); break;
        case 'n': l.tint = clampf((float)v, -100, 100); break;
        case 'h': l.hue = clampf((float)v, -180, 180); break;
        case 'x': l.enabled = v ? 1 : 0; break;
        default: return 0;
        }
        Eng_SetLook(&l);
        Ui_RebuildPanel();
        Main_ApplyAll();
        InvalidateRect(wnd, NULL, FALSE);
        return 0;
    }
    case WM_APP_PROFILE: {
        /* remote profile switch: load the profile's active sub-mode as the
         * complete requested look (same no-stacking path as detection). */
        int idx = (int)wp;
        Profile *p = Prof_Get(idx);
        if (p) {
            Prof_SetActiveIndex(idx);
            Ui_LoadLook(&p->sub[p->active_sub].look);
            Eng_SetMode(PX_CSMODE_GAME, idx, p->active_sub);
            {
                wchar_t msg[128];
                wsprintfW(msg, L"%s: %s profile applied (remote)", p->name, p->sub[p->active_sub].name);
                Ui_Notify(msg);
            }
            Ui_RebuildPanel();
            Wm_ReassertNow(L"remote-profile");
            InvalidateRect(wnd, NULL, FALSE);
        }
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
        case TRAY_KEEP_MODE:
            if (Modes_ConfirmPending()) Ui_Notify(L"Display mode kept");
            Ui_RebuildPanel();
            InvalidateRect(wnd, NULL, FALSE);
            break;
        case TRAY_REVERT_MODE:
            if (Modes_RollbackPending()) Ui_Notify(L"Previous display mode restored");
            Ui_RebuildPanel();
            InvalidateRect(wnd, NULL, FALSE);
            break;
        case TRAY_RESET:
            Ui_Exec(ID_B_RESET_COLOR);
            break;
        case TRAY_EMERGENCY:
            Main_EmergencyReset();
            break;
        case TRAY_EXIT:
            g_force_quit = 1;
            PostMessageW(wnd, WM_CLOSE, 0, 0);
            break;
        default:
            Ui_Exec(LOWORD(wp));
            break;
        }
        return 0;
    case WM_CLOSE:
        /* "Minimize to tray" means the X button hides the window; the tray menu
         * (or a second WM_CLOSE from there) still exits. */
        if (PxSetGetInt("ui", "minimize_tray", 0) && !g_force_quit) {
            ShowWindow(wnd, SW_HIDE);
            return 0;
        }
        DestroyWindow(wnd);
        return 0;
    case WM_SYSCOMMAND:
        if ((wp & 0xFFF0) == SC_MINIMIZE && PxSetGetInt("ui", "minimize_tray", 0)) {
            ShowWindow(wnd, SW_HIDE);
            return 0;
        }
        break;
    case WM_DESTROY:
        tray_del();
        KillTimer(wnd, TIMER_POLL);
        KillTimer(wnd, PX_SETTLE_MS);
        Wm_Shutdown();                   /* unhook WinEvent + drop marker file */
        WTSUnRegisterSessionNotification(wnd);
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

    /* The single settings store: loads + migrates + validates config.ini / profiles.ini. */
    PxSet_Init(g_appdata);

    /* Gamma crash recovery: paths BEFORE Eng_Init so the pipeline loads ramps.dat */
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
    Modes_SetStateDir(g_appdata);
    Modes_Refresh();
    Ui_Init(NULL, inst);
    load_current_config();

    /* Engine startup policy: 0 = start bypassed, 1 = start enabled, 2 = restore
     * exactly what was saved.  It only touches the requested enabled flag, so no
     * value the user ever set is lost. */
    {
        int ss = PxSetGetInt("engine", "startup_state", 2);
        if (ss == 0 || ss == 1) {
            Look l = *Eng_GetRequested();
            int want = (ss == 1) ? 1 : 0;
            if (l.enabled != want) {
                l.enabled = want;
                Eng_SetLook(&l);
            }
        }
    }
    Ui_RebuildPanel();          /* show the loaded look, not the built-in defaults */

    HWND created = CreateWindowExW(WS_EX_APPWINDOW, PX_CLASS, PX_APP_TITLE, WS_POPUP,
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
    Main_ApplyChrome();
    WTSRegisterSessionNotification(g_hwnd, NOTIFY_FOR_THIS_SESSION);

    /* WindowManager: one WinEvent hook + the display/session watchers; it posts
     * WM_APP_FOREGROUND to THIS thread and owns the reassert + settle policy. */
    Wm_Init(g_hwnd);

    SetTimer(g_hwnd, TIMER_POLL, TIMER_POLL_MS, NULL);

    /* Phone remote: only when the user left it enabled. */
    if (PxSetGetInt("phone", "enabled", 0)) {
        if (Phone_Start() != 0)
            Ui_Notify(L"The LAN phone remote could not bind port 8777");
    }

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
    Prof_SyncApplied(L"startup");

    /* A mode change left unconfirmed by a previous session (crash, kill, power
     * loss during the countdown) is put back the moment the UI exists to say so.
     * The mode itself lives in the driver/registry, so nothing else can do this. */
    if (Modes_RecoverPendingFromDisk())
        Ui_Notify(L"An unconfirmed display mode from the last session was restored");

    /* Establish the initial foreground context once, through the same event path
     * an ALT+TAB uses — no sleep, no second code path at start-up. */
    PostMessageW(g_hwnd, WM_APP_FOREGROUND, 0, 0);

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
