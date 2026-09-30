/* main.c — window, tray, hotkeys, timers, persistence */
#include "common.h"
#include <shlobj.h>

HWND   g_hwnd;
HINSTANCE g_inst;
wchar_t g_appdir[MAX_PATH];
wchar_t g_appdata[MAX_PATH];

static wchar_t g_exepath[MAX_PATH];
static wchar_t g_rampsfile[MAX_PATH];
static wchar_t g_dirtyfile[MAX_PATH];
static int     g_reapply_tick;

#define TIMER_POLL 1

/* tray ids */
#define TRAY_ID 1
#define TRAY_OPEN  101
#define TRAY_LOOK  102
#define TRAY_XH    103
#define TRAY_EXIT  104

const wchar_t *g_appdir_exe(void) { return g_exepath; }

static HICON build_icon(int sz)
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
    HBITMAP mask = CreateBitmap(sz, sz, 1, 1, NULL);
    if (!color || !pv) return LoadIcon(NULL, IDI_APPLICATION);
    DWORD *px = pv;
    for (int y = 0; y < sz; y++)
        for (int x = 0; x < sz; x++) {
            int i = y * sz + x;
            int corner = (x < 2 || y < 2 || x >= sz - 2 || y >= sz - 2);
            DWORD bg = corner ? 0 : 0xFF141010;      /* opaque dark, transparent edge */
            /* lime X strokes — both diagonals, cropped to a tile */
            int t = sz / 6;
            int on = ((abs(x - y) < t || abs(x + y - (sz - 1)) < t) &&
                      x > sz / 6 && x < sz - sz / 6 &&
                      y > sz / 6 && y < sz - sz / 6);
            px[i] = on ? 0xFFC6FF3D : bg;             /* AARRGGBB — lime X */
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

/* ---------- persistence of current look + crosshair ---------- */
static void save_current(void)
{
    wchar_t f[MAX_PATH];
    wsprintfW(f, L"%s\\config.ini", g_appdata);
    wchar_t b[32];
    Look *l = Ui_Look();
    wsprintfW(b, L"%d", (int)l->enabled); WritePrivateProfileStringW(L"current", L"enabled", b, f);
    wsprintfW(b, L"%d", (int)l->sat);     WritePrivateProfileStringW(L"current", L"sat", b, f);
    wsprintfW(b, L"%d", (int)l->bri);     WritePrivateProfileStringW(L"current", L"bri", b, f);
    wsprintfW(b, L"%d", (int)l->con);     WritePrivateProfileStringW(L"current", L"con", b, f);
    wsprintfW(b, L"%d", (int)l->temp);    WritePrivateProfileStringW(L"current", L"temp", b, f);
    wsprintfW(b, L"%d", (int)l->hue);     WritePrivateProfileStringW(L"current", L"hue", b, f);
    wsprintfW(b, L"%d", (int)(l->gamma * 100)); WritePrivateProfileStringW(L"current", L"gamma", b, f);

    XhCfg x;
    Ui_GetXh(&x);
    wsprintfW(b, L"%d", x.on);     WritePrivateProfileStringW(L"xhair", L"on", b, f);
    wsprintfW(b, L"%d", x.shape);  WritePrivateProfileStringW(L"xhair", L"shape", b, f);
    wsprintfW(b, L"%d", x.size);   WritePrivateProfileStringW(L"xhair", L"size", b, f);
    wsprintfW(b, L"%d", x.gap);    WritePrivateProfileStringW(L"xhair", L"gap", b, f);
    wsprintfW(b, L"%d", x.thick);  WritePrivateProfileStringW(L"xhair", L"thick", b, f);
    wsprintfW(b, L"%d", x.dotop);  WritePrivateProfileStringW(L"xhair", L"dotop", b, f);
    wsprintfW(b, L"%d", x.outline); WritePrivateProfileStringW(L"xhair", L"outline", b, f);
    wsprintfW(b, L"%d", (int)x.color);  WritePrivateProfileStringW(L"xhair", L"color", b, f);
    wsprintfW(b, L"%d", (int)x.ocolor); WritePrivateProfileStringW(L"xhair", L"ocolor", b, f);
}

static void load_current(void)
{
    wchar_t f[MAX_PATH];
    wsprintfW(f, L"%s\\config.ini", g_appdata);
    Look l;
    l.enabled = GetPrivateProfileIntW(L"current", L"enabled", 1, f);
    if (GetPrivateProfileIntW(L"current", L"sat", -1, f) == -1) {
        l.sat = 150; l.bri = l.con = l.temp = l.hue = 0; l.gamma = 1.f;
    } else {
        l.sat = (float)GetPrivateProfileIntW(L"current", L"sat", 150, f);
        l.bri = (float)GetPrivateProfileIntW(L"current", L"bri", 0, f);
        l.con = (float)GetPrivateProfileIntW(L"current", L"con", 0, f);
        l.temp = (float)GetPrivateProfileIntW(L"current", L"temp", 0, f);
        l.hue = (float)GetPrivateProfileIntW(L"current", L"hue", 0, f);
        l.gamma = GetPrivateProfileIntW(L"current", L"gamma", 100, f) / 100.f;
    }
    Ui_LoadLook(&l);

    XhCfg x;
    Ui_GetXh(&x);
    x.on = GetPrivateProfileIntW(L"xhair", L"on", 0, f);
    x.shape = GetPrivateProfileIntW(L"xhair", L"shape", XH_CROSS, f);
    x.size = GetPrivateProfileIntW(L"xhair", L"size", 16, f);
    x.gap = GetPrivateProfileIntW(L"xhair", L"gap", 4, f);
    x.thick = GetPrivateProfileIntW(L"xhair", L"thick", 2, f);
    x.dotop = GetPrivateProfileIntW(L"xhair", L"dotop", 100, f);
    x.outline = GetPrivateProfileIntW(L"xhair", L"outline", 1, f);
    x.color = (COLORREF)GetPrivateProfileIntW(L"xhair", L"color", RGB(0x7C,0xFF,0x40), f);
    x.ocolor = (COLORREF)GetPrivateProfileIntW(L"xhair", L"ocolor", RGB(0,0,0), f);
    Ui_SetXh(&x);
}

void Main_Save(void)
{
    save_current();
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

/* ---------- tray ---------- */
static HICON g_tray_icon;

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
    lstrcpynW(nid.szTip, L"ChromaX — free monitor colour", 128);
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
    AppendMenuW(m, MF_STRING, TRAY_OPEN, L"Open ChromaX");
    AppendMenuW(m, MF_STRING, TRAY_LOOK,
                Ui_Look()->enabled ? L"Turn look off" : L"Turn look on");
    AppendMenuW(m, MF_STRING, TRAY_XH,
                x.on ? L"Hide crosshair" : L"Show crosshair");
    AppendMenuW(m, MF_SEPARATOR, 0, NULL);
    AppendMenuW(m, MF_STRING, TRAY_EXIT, L"Exit");
    SetForegroundWindow(g_hwnd);
    TrackPopupMenu(m, TPM_RIGHTBUTTON, pt.x, pt.y, 0, g_hwnd, NULL);
    DestroyMenu(m);
}

/* ---------- window proc ---------- */
static void handle_hotkey(WPARAM id)
{
    Look *l = Ui_Look();
    switch (id) {
    case 1: l->sat = clampf(l->sat + 10, 100, 300); Ui_Notify(L"Saturation +10%"); break;
    case 2: l->sat = clampf(l->sat - 10, 100, 300); Ui_Notify(L"Saturation −10%"); break;
    case 3: l->sat = 100; l->bri = l->con = l->temp = l->hue = 0; l->gamma = 1.f;
            l->enabled = 1; Ui_Notify(L"Reset — 100%"); break;
    case 4: Ui_Exec(ID_T_XH); return;
    case 5: Ui_Exec(ID_T_ENABLE); return;
    }
    Ui_RebuildPanel();
    Main_ApplyAll();
}

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
    case WM_ERASEBKGND: return 1;
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
        Ui_MouseDown(GET_X_LPARAM(lp), GET_Y_LPARAM(lp));
        InvalidateRect(wnd, NULL, FALSE);
        return 0;
    case WM_MOUSEMOVE: {
        int r = Ui_MouseMove(GET_X_LPARAM(lp), GET_Y_LPARAM(lp), GetCapture() == wnd);
        if (r < 0) InvalidateRect(wnd, NULL, FALSE);
        return 0;
    }
    case WM_LBUTTONUP:
        Ui_MouseUp(GET_X_LPARAM(lp), GET_Y_LPARAM(lp));
        InvalidateRect(wnd, NULL, FALSE);
        return 0;
    case WM_MOUSEWHEEL: {
        POINT pt = { GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
        ScreenToClient(wnd, &pt);
        if (Ui_Wheel((int)pt.x, (int)pt.y, GET_WHEEL_DELTA_WPARAM(wp) / WHEEL_DELTA))
            InvalidateRect(wnd, NULL, FALSE);
        return 0;
    }
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
        case 's': l->sat = clampf((float)v, 100, 300); break;
        case 'b': l->bri = clampf((float)v, -100, 100); break;
        case 'c': l->con = clampf((float)v, -100, 100); break;
        case 't': l->temp = clampf((float)v, -100, 100); break;
        case 'g': l->gamma = clampf(v / 100.f, 0.40f, 2.40f); break;
        case 'o': l->enabled = v ? 1 : 0; break;
        }
        Ui_RebuildPanel();
        Main_ApplyAll();
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
        case TRAY_LOOK: Ui_Exec(ID_T_ENABLE); break;
        case TRAY_XH:   Ui_Exec(ID_T_XH); break;
        case TRAY_EXIT: PostMessageW(wnd, WM_CLOSE, 0, 0); break;
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
        Eng_Shutdown();          /* restores gamma + clears dirty flag file */
        Ui_Free();
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(wnd, msg, wp, lp);
}

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE prev, LPWSTR cmd, int show)
{
    (void)prev; (void)cmd;

    HANDLE mutex = CreateMutexW(NULL, TRUE, CX_MUTEX);
    if (mutex && GetLastError() == ERROR_ALREADY_EXISTS) {
        HWND old = FindWindowW(CX_CLASS, NULL);
        if (old) { ShowWindow(old, SW_RESTORE); SetForegroundWindow(old); }
        return 0;
    }

    /* per-monitor DPI aware (v2 where available) */
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

    if (SHGetFolderPathW(NULL, CSIDL_APPDATA, NULL, 0, g_appdata) != S_OK)
        lstrcpyW(g_appdata, g_appdir);
    lstrcatW(g_appdata, L"\\ChromaX");
    CreateDirectoryW(g_appdata, NULL);
    wsprintfW(g_rampsfile, L"%s\\ramps.dat", g_appdata);
    wsprintfW(g_dirtyfile, L"%s\\dirty.flg", g_appdata);

    /* single-instance, load config */
    Prof_Load();

    /* gamma crash recovery */
    int dirty = (GetFileAttributesW(g_dirtyfile) != INVALID_FILE_ATTRIBUTES);
    Eng_SetPaths(g_rampsfile, g_dirtyfile, dirty);
    Eng_Init();

    /* window */
    WNDCLASSW wc;
    memset(&wc, 0, sizeof wc);
    wc.lpfnWndProc = wnd_proc;
    wc.hInstance = inst;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hIcon = g_tray_icon = build_icon(32);
    wc.lpszClassName = CX_CLASS;
    RegisterClassW(&wc);

    HMODULE cur = GetModuleHandleW(NULL);
    (void)cur;
    HDC sdc = GetDC(NULL);
    float sc = GetDeviceCaps(sdc, LOGPIXELSX) / 96.f;
    ReleaseDC(NULL, sdc);
    if (sc < 0.5f) sc = 1.f;
    int ww = (int)(1240 * sc), wh = (int)(800 * sc);

    g_hwnd = CreateWindowExW(0, CX_CLASS, CX_APP_NAME, WS_POPUP,
                             CW_USEDEFAULT, CW_USEDEFAULT, ww, wh,
                             NULL, NULL, inst, NULL);
    if (!g_hwnd) return 1;

    Ui_Init(g_hwnd, inst);
    load_current();
    Xh_Init();

    ShowWindow(g_hwnd, show);
    UpdateWindow(g_hwnd);

    tray_add(g_hwnd);
    SetTimer(g_hwnd, TIMER_POLL, 1000, NULL);

    RegisterHotKey(g_hwnd, 1, MOD_CONTROL | MOD_ALT, VK_UP);
    RegisterHotKey(g_hwnd, 2, MOD_CONTROL | MOD_ALT, VK_DOWN);
    RegisterHotKey(g_hwnd, 3, MOD_CONTROL | MOD_ALT, '0');
    RegisterHotKey(g_hwnd, 4, MOD_CONTROL | MOD_ALT, 'X');
    RegisterHotKey(g_hwnd, 5, MOD_CONTROL | MOD_ALT, 'E');

    Main_ApplyAll();

    MSG m;
    while (GetMessageW(&m, NULL, 0, 0)) {
        TranslateMessage(&m);
        DispatchMessageW(&m);
    }

    if (mutex) { ReleaseMutex(mutex); CloseHandle(mutex); }
    return 0;
}
