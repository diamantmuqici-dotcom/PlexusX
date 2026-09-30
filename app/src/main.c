/* PlexusX — Elite Windows Gaming Display & Visual Optimization Center
 * Main Entry Point: Window Lifecycle, Tray Icon, Hotkeys, Timers & Persistence.
 */
#include "common.h"
#include <shlobj.h>

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

const wchar_t *Main_GetExePath(void) { return g_exepath; }
const wchar_t *Main_GetAppDataPath(void) { return g_appdata; }

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
    case 3: /* Reset 100% */
        l->sat = 100.0f; l->vibrance = 100.0f; l->bri = 100.0f; l->con = 100.0f;
        l->gamma = 1.0f; l->temp = 6500.0f; l->tint = 0.0f;
        l->enabled = 1;
        Ui_Notify(L"Display Colors Reset to Neutral");
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
    }
    Ui_RebuildPanel();
    Main_ApplyAll();
    InvalidateRect(g_hwnd, NULL, FALSE);
}

/* ---------------- Window Procedure ---------------- */
static LRESULT CALLBACK wnd_proc(HWND wnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
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

    g_hwnd = CreateWindowExW(0, PX_CLASS, PX_APP_TITLE, WS_POPUP,
                             sx, sy, ww, wh, NULL, NULL, inst, NULL);
    if (!g_hwnd) return 1;

    Ui_Init(g_hwnd, inst);
    load_current_config();
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
