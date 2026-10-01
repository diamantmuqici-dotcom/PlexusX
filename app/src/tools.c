/* PlexusX — Display Test Patterns, Diagnostics & Crash Recovery Tools
 * 100% Free · Anti-Cheat Safe
 */
#include "common.h"

static HWND g_pattern_wnd = NULL;
static int  g_cur_pattern = 0;
static int  g_gaming_mode = 0;

/* Fail-safe: a test pattern closes by itself after 10 s, and ANY key or mouse click dismisses it,
 * so a solid black / green / white screen can never trap the user. */
#define PAT_TIMER_ID    0x5058       /* "PX" */
#define PAT_TIMER_MS    100
#define PAT_TIMEOUT_MS  10000

static ULONGLONG g_pat_deadline = 0;     /* GetTickCount64() value at which the pattern closes */
static int       g_pat_secs = 0;         /* seconds currently shown in the banner */
static BYTE      g_pat_down[256];        /* keys / buttons already held down when the pattern opened */

enum {
    PAT_BLACK = 0,
    PAT_WHITE,
    PAT_RED,
    PAT_GREEN,
    PAT_BLUE,
    PAT_GRADIENT,
    PAT_GAMMA22,
    PAT_CONTRAST,
    PAT_SHARPNESS,
    PAT_BANDING,
    PAT_HDR_PEAK,
    PAT_COUNT
};

/* ---------------- Display Test Patterns Rendering ---------------- */
static void draw_pattern(HDC dc, const RECT *rc, int pat)
{
    int w = rc->right - rc->left;
    int h = rc->bottom - rc->top;

    switch (pat) {
    case PAT_BLACK: {
        HBRUSH b = CreateSolidBrush(RGB(0, 0, 0));
        FillRect(dc, rc, b);
        DeleteObject(b);
        break;
    }
    case PAT_WHITE: {
        HBRUSH b = CreateSolidBrush(RGB(255, 255, 255));
        FillRect(dc, rc, b);
        DeleteObject(b);
        break;
    }
    case PAT_RED: {
        HBRUSH b = CreateSolidBrush(RGB(255, 0, 0));
        FillRect(dc, rc, b);
        DeleteObject(b);
        break;
    }
    case PAT_GREEN: {
        HBRUSH b = CreateSolidBrush(RGB(0, 255, 0));
        FillRect(dc, rc, b);
        DeleteObject(b);
        break;
    }
    case PAT_BLUE: {
        HBRUSH b = CreateSolidBrush(RGB(0, 0, 255));
        FillRect(dc, rc, b);
        DeleteObject(b);
        break;
    }
    case PAT_GRADIENT: {
        /* 16-step grayscale gradient bar */
        int steps = 16;
        for (int i = 0; i < steps; i++) {
            int x1 = rc->left + (w * i) / steps;
            int x2 = rc->left + (w * (i + 1)) / steps;
            int v = (i * 255) / (steps - 1);
            RECT r = { x1, rc->top, x2, rc->bottom };
            HBRUSH b = CreateSolidBrush(RGB(v, v, v));
            FillRect(dc, &r, b);
            DeleteObject(b);
        }
        break;
    }
    case PAT_GAMMA22: {
        /* Background 50% dither / lines vs solid 186/255 gray (gamma 2.2 target) */
        HBRUSH bg = CreateSolidBrush(RGB(186, 186, 186));
        FillRect(dc, rc, bg);
        DeleteObject(bg);

        /* Alternating black and white horizontal lines on central box */
        int box_w = w / 2, box_h = h / 2;
        int bx = rc->left + (w - box_w) / 2;
        int by = rc->top + (h - box_h) / 2;
        for (int y = by; y < by + box_h; y += 2) {
            HPEN p = CreatePen(PS_SOLID, 1, RGB(0, 0, 0));
            HGDIOBJ op = SelectObject(dc, p);
            MoveToEx(dc, bx, y, NULL);
            LineTo(dc, bx + box_w, y);
            SelectObject(dc, op);
            DeleteObject(p);

            HPEN pw = CreatePen(PS_SOLID, 1, RGB(255, 255, 255));
            op = SelectObject(dc, pw);
            MoveToEx(dc, bx, y + 1, NULL);
            LineTo(dc, bx + box_w, y + 1);
            SelectObject(dc, op);
            DeleteObject(pw);
        }
        break;
    }
    case PAT_CONTRAST: {
        /* Near-black patches and near-white patches */
        HBRUSH bg = CreateSolidBrush(RGB(128, 128, 128));
        FillRect(dc, rc, bg);
        DeleteObject(bg);

        int sz = h / 4;
        /* Near black: 0%, 1%, 2%, 3%, 4% */
        for (int i = 0; i < 5; i++) {
            int v = (int)(i * 2.55f);
            RECT r = { rc->left + 40 + i * (sz + 20), rc->top + h / 4 - sz / 2,
                       rc->left + 40 + i * (sz + 20) + sz, rc->top + h / 4 + sz / 2 };
            HBRUSH b = CreateSolidBrush(RGB(v, v, v));
            FillRect(dc, &r, b);
            DeleteObject(b);
        }
        /* Near white: 96%, 97%, 98%, 99%, 100% */
        for (int i = 0; i < 5; i++) {
            int v = 255 - (int)((4 - i) * 2.55f);
            RECT r = { rc->left + 40 + i * (sz + 20), rc->top + (3 * h) / 4 - sz / 2,
                       rc->left + 40 + i * (sz + 20) + sz, rc->top + (3 * h) / 4 + sz / 2 };
            HBRUSH b = CreateSolidBrush(RGB(v, v, v));
            FillRect(dc, &r, b);
            DeleteObject(b);
        }
        break;
    }
    case PAT_SHARPNESS: {
        /* 1px alternating grid */
        HBRUSH bg = CreateSolidBrush(RGB(20, 20, 20));
        FillRect(dc, rc, bg);
        DeleteObject(bg);

        HPEN p = CreatePen(PS_SOLID, 1, RGB(200, 200, 200));
        HGDIOBJ op = SelectObject(dc, p);
        for (int x = rc->left; x < rc->right; x += 4) {
            MoveToEx(dc, x, rc->top, NULL); LineTo(dc, x, rc->bottom);
        }
        for (int y = rc->top; y < rc->bottom; y += 4) {
            MoveToEx(dc, rc->left, y, NULL); LineTo(dc, rc->right, y);
        }
        SelectObject(dc, op);
        DeleteObject(p);
        break;
    }
    case PAT_BANDING: {
        /* Subtle continuous gradient */
        for (int x = rc->left; x < rc->right; x++) {
            float t = (float)(x - rc->left) / (float)w;
            int v = (int)(t * 255.0f);
            HPEN p = CreatePen(PS_SOLID, 1, RGB(v, v, v));
            HGDIOBJ op = SelectObject(dc, p);
            MoveToEx(dc, x, rc->top, NULL); LineTo(dc, x, rc->bottom);
            SelectObject(dc, op);
            DeleteObject(p);
        }
        break;
    }
    case PAT_HDR_PEAK: {
        HBRUSH bg = CreateSolidBrush(RGB(0, 0, 0));
        FillRect(dc, rc, bg);
        DeleteObject(bg);

        /* 10% window pure white block */
        int bw = w / 3, bh = h / 3;
        RECT box = { rc->left + (w - bw) / 2, rc->top + (h - bh) / 2,
                     rc->left + (w + bw) / 2, rc->top + (h + bh) / 2 };
        HBRUSH wh = CreateSolidBrush(RGB(255, 255, 255));
        FillRect(dc, &box, wh);
        DeleteObject(wh);
        break;
    }
    }
}

/* ---------------- Fail-safe banner & input handling ---------------- */

static int pat_secs_left(void)
{
    ULONGLONG now = GetTickCount64();
    if (now >= g_pat_deadline) return 0;
    return (int)((g_pat_deadline - now + 999) / 1000);
}

/* Remember what is already held down (e.g. the mouse button that launched the pattern) */
static void pat_snapshot_input(void)
{
    for (int vk = 1; vk < 256; vk++)
        g_pat_down[vk] = (GetAsyncKeyState(vk) & 0x8000) ? 1 : 0;
}

/* Global poll for a NEW key press / mouse button, independent of keyboard focus:
 * if another window steals the focus the pattern must still be dismissable. */
static int pat_any_new_input(void)
{
    int hit = 0;
    for (int vk = 1; vk < 256; vk++) {
        int down = (GetAsyncKeyState(vk) & 0x8000) ? 1 : 0;
        if (down && !g_pat_down[vk]) hit = 1;
        g_pat_down[vk] = (BYTE)down;
    }
    return hit;
}

/* High-contrast pill banner at the bottom of one monitor:
 *     "Display Test Pattern • Click or press ANY key to exit (Xs)"
 * Near-black pill + bright lime ring + white text stays readable on pure black, white,
 * red, green and blue patterns alike.  `mon` is the monitor rect in window-client coordinates. */
static void draw_banner(HDC dc, const RECT *mon, int secs)
{
    int mon_w = mon->right - mon->left;
    int mon_h = mon->bottom - mon->top;
    if (mon_w < 200 || mon_h < 120) return;

    int fh = mon_h / 50;
    if (fh < 16) fh = 16;
    if (fh > 48) fh = 48;

    wchar_t text[112], widest[112];
    wsprintfW(text,   L"Display Test Pattern • Click or press ANY key to exit (%ds)", secs);
    wsprintfW(widest, L"Display Test Pattern • Click or press ANY key to exit (%ds)", PAT_TIMEOUT_MS / 1000);

    HFONT font = CreateFontW(-fh, 0, 0, 0, FW_BOLD, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
    HGDIOBJ of = SelectObject(dc, font);
    SIZE ext = { 0, 0 };
    GetTextExtentPoint32W(dc, widest, lstrlenW(widest), &ext);   /* fixed width: the pill never resizes */
    SelectObject(dc, of);

    int pad_x = fh * 2;
    int pad_y = fh / 2 + 4;
    int bw = ext.cx + pad_x * 2;
    int bh = ext.cy + pad_y * 2;
    if (bw > mon_w - 24) bw = mon_w - 24;
    int bx = mon->left + (mon_w - bw) / 2;
    int by = mon->bottom - bh - mon_h / 16;

    /* Compose off-screen and blit through a pill-shaped clip: no flicker, transparent corners */
    HDC mem = CreateCompatibleDC(dc);
    HBITMAP bmp = CreateCompatibleBitmap(dc, bw, bh);
    if (mem && bmp) {
        HGDIOBJ obmp = SelectObject(mem, bmp);

        HBRUSH ringbr = CreateSolidBrush(RGB(198, 255, 61));
        RECT all = { 0, 0, bw, bh };
        FillRect(mem, &all, ringbr);                       /* any edge pixel is ring-coloured */
        DeleteObject(ringbr);

        int ring = fh / 7 + 2;
        HBRUSH fill = CreateSolidBrush(RGB(14, 14, 20));
        HPEN pen = CreatePen(PS_SOLID, ring, RGB(198, 255, 61));
        HGDIOBJ open = SelectObject(mem, pen);
        HGDIOBJ obr = SelectObject(mem, fill);
        RoundRect(mem, ring / 2, ring / 2, bw - ring / 2, bh - ring / 2, bh, bh);
        SelectObject(mem, obr);
        SelectObject(mem, open);
        DeleteObject(fill);
        DeleteObject(pen);

        HGDIOBJ ofont = SelectObject(mem, font);
        SetBkMode(mem, TRANSPARENT);
        SetTextColor(mem, RGB(255, 255, 255));
        RECT tr = { pad_x / 2, 0, bw - pad_x / 2, bh };
        DrawTextW(mem, text, -1, &tr, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        SelectObject(mem, ofont);

        HRGN pill = CreateRoundRectRgn(bx, by, bx + bw + 1, by + bh + 1, bh, bh);
        SelectClipRgn(dc, pill);
        BitBlt(dc, bx, by, bw, bh, mem, 0, 0, SRCCOPY);
        SelectClipRgn(dc, NULL);
        DeleteObject(pill);

        SelectObject(mem, obmp);
    }
    if (bmp) DeleteObject(bmp);
    if (mem) DeleteDC(mem);
    DeleteObject(font);
}

typedef struct BannerCtx {
    HDC dc;
    int secs;
    POINT origin;       /* screen position of the window's client origin */
} BannerCtx;

static BOOL CALLBACK banner_monitor_proc(HMONITOR hm, HDC hdc, LPRECT mrc, LPARAM lp)
{
    (void)hm; (void)hdc;
    BannerCtx *c = (BannerCtx *)lp;
    RECT mon = { mrc->left - c->origin.x, mrc->top - c->origin.y,
                 mrc->right - c->origin.x, mrc->bottom - c->origin.y };
    draw_banner(c->dc, &mon, c->secs);
    return TRUE;
}

/* One banner on EVERY monitor, so the exit hint is visible wherever the user is looking */
static void paint_banners(HWND wnd, HDC dc, int secs)
{
    BannerCtx c;
    c.dc = dc;
    c.secs = secs;
    c.origin.x = 0;
    c.origin.y = 0;
    ClientToScreen(wnd, &c.origin);
    EnumDisplayMonitors(NULL, NULL, banner_monitor_proc, (LPARAM)&c);
}

static void pat_tick(HWND wnd)
{
    if (GetTickCount64() >= g_pat_deadline || pat_any_new_input()) {
        DestroyWindow(wnd);                 /* 10 s elapsed, or a key / click happened anywhere */
        return;
    }
    int secs = pat_secs_left();
    if (secs != g_pat_secs) {               /* redraw only the pill, once per displayed second */
        g_pat_secs = secs;
        HDC dc = GetDC(wnd);
        if (dc) {
            paint_banners(wnd, dc, secs);
            ReleaseDC(wnd, dc);
        }
    }
}

static LRESULT CALLBACK pattern_proc(HWND wnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    /* ANY key press or mouse click dismisses the pattern immediately */
    case WM_KEYDOWN:
    case WM_SYSKEYDOWN:
    case WM_LBUTTONDOWN:
    case WM_RBUTTONDOWN:
    case WM_MBUTTONDOWN:
    case WM_XBUTTONDOWN:
        DestroyWindow(wnd);
        return 0;
    case WM_TIMER:
        if (wp == PAT_TIMER_ID) {
            pat_tick(wnd);
            return 0;
        }
        break;
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(wnd, &ps);
        RECT rc;
        GetClientRect(wnd, &rc);
        draw_pattern(dc, &rc, g_cur_pattern);
        paint_banners(wnd, dc, pat_secs_left());
        EndPaint(wnd, &ps);
        return 0;
    }
    case WM_DESTROY:
        KillTimer(wnd, PAT_TIMER_ID);
        if (g_pattern_wnd == wnd) g_pattern_wnd = NULL;
        return 0;
    }
    return DefWindowProcW(wnd, msg, wp, lp);
}

void Tools_Init(void)
{
    WNDCLASSW wc;
    memset(&wc, 0, sizeof wc);
    wc.lpfnWndProc = pattern_proc;
    wc.hInstance = g_inst;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.lpszClassName = PX_TEST_CLASS;
    RegisterClassW(&wc);
}

void Tools_Shutdown(void)
{
    Tools_ClosePattern();
}

void Tools_LaunchPattern(int pattern_id)
{
    Tools_ClosePattern();

    g_cur_pattern = clampi(pattern_id, 0, PAT_COUNT - 1);

    int sx = GetSystemMetrics(SM_XVIRTUALSCREEN);
    int sy = GetSystemMetrics(SM_YVIRTUALSCREEN);
    int sw = GetSystemMetrics(SM_CXVIRTUALSCREEN);
    int sh = GetSystemMetrics(SM_CYVIRTUALSCREEN);

    /* Created hidden: the countdown and the input baseline are in place before the first paint */
    HWND wnd = CreateWindowExW(WS_EX_TOPMOST, PX_TEST_CLASS, L"PlexusX Pattern",
                               WS_POPUP, sx, sy, sw, sh, NULL, NULL, g_inst, NULL);
    if (!wnd) return;
    g_pattern_wnd = wnd;

    g_pat_deadline = GetTickCount64() + PAT_TIMEOUT_MS;
    g_pat_secs = PAT_TIMEOUT_MS / 1000;
    pat_snapshot_input();

    /* A full-screen pattern that cannot close itself is a trap: if the timer is unavailable, don't show it */
    if (!SetTimer(wnd, PAT_TIMER_ID, PAT_TIMER_MS, NULL)) {
        DestroyWindow(wnd);
        g_pattern_wnd = NULL;
        return;
    }

    ShowWindow(wnd, SW_SHOW);
    SetForegroundWindow(wnd);
    SetFocus(wnd);
}

void Tools_ClosePattern(void)
{
    if (g_pattern_wnd) {
        DestroyWindow(g_pattern_wnd);
        g_pattern_wnd = NULL;
    }
}

/* ---------------- Diagnostics Exporter ---------------- */
int Tools_ExportDiagnostics(const wchar_t *filepath)
{
    if (!filepath) return -1;
    FILE *f = _wfopen(filepath, L"w, ccs=UTF-8");
    if (!f) return -1;

    const GpuInfo *gpu = Eng_GetGpuInfo();
    ModeInfo cur;
    Modes_Current(&cur);

    fwprintf(f, L"{\n");
    fwprintf(f, L"  \"application\": \"PlexusX\",\n");
    fwprintf(f, L"  \"version\": \"%ls\",\n", PX_VERSION);
    fwprintf(f, L"  \"build_date\": \"%ls\",\n", PX_BUILD_DATE);
    fwprintf(f, L"  \"architecture\": \"x86_64-windows\",\n");
    fwprintf(f, L"  \"os\": \"Windows 10/11 x64\",\n");
    fwprintf(f, L"  \"gpu\": {\n");
    fwprintf(f, L"    \"vendor\": \"%ls\",\n", gpu->vendor_name);
    fwprintf(f, L"    \"name\": \"%ls\",\n", gpu->name);
    fwprintf(f, L"    \"mag_api_available\": %ls,\n", gpu->mag_available ? L"true" : L"false");
    fwprintf(f, L"    \"gamma_ramp_available\": %ls\n", gpu->gamma_available ? L"true" : L"false");
    fwprintf(f, L"  },\n");
    fwprintf(f, L"  \"active_display\": {\n");
    fwprintf(f, L"    \"resolution\": \"%dx%d\",\n", cur.w, cur.h);
    fwprintf(f, L"    \"refresh_rate_hz\": %d,\n", cur.hz);
    fwprintf(f, L"    \"native\": %ls\n", cur.native ? L"true" : L"false");
    fwprintf(f, L"  },\n");
    fwprintf(f, L"  \"monitors_count\": %d,\n", Modes_MonitorCount());
    fwprintf(f, L"  \"crosshair_active\": %ls,\n", Xh_IsActive() ? L"true" : L"false");
    fwprintf(f, L"  \"phone_control_active\": %ls\n", Phone_IsRunning() ? L"true" : L"false");
    fwprintf(f, L"}\n");

    fclose(f);
    return 0;
}

int Tools_BackupDisplayState(const wchar_t *filepath)
{
    (void)filepath;
    Eng_BackupCurrentState();
    return 0;
}

int Tools_RestoreDisplayState(const wchar_t *filepath)
{
    (void)filepath;
    return Eng_RestoreLastGood();
}

void Tools_ToggleGamingMode(void)
{
    g_gaming_mode = !g_gaming_mode;
    Ui_Notify(g_gaming_mode ? L"Gaming Mode ACTIVATED" : L"Gaming Mode Deactivated");
}

int Tools_IsGamingMode(void)
{
    return g_gaming_mode;
}
