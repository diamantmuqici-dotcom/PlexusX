/* PlexusX — Display Test Patterns, Diagnostics & Crash Recovery Tools
 * 100% Free · Anti-Cheat Safe
 */
#include "common.h"

static HWND g_pattern_wnd = NULL;
static int  g_cur_pattern = 0;
static int  g_gaming_mode = 0;

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

    /* Pattern info text at bottom */
    HFONT font = CreateFontW(-16, 0, 0, 0, FW_SEMIBOLD, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
    HGDIOBJ of = SelectObject(dc, font);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, RGB(255, 96, 96));
    RECT tr = { rc->left + 24, rc->bottom - 44, rc->right - 24, rc->bottom - 12 };
    DrawTextW(dc, L"Press ESC or Click anywhere to exit test pattern", -1, &tr, DT_RIGHT | DT_SINGLELINE);
    SelectObject(dc, of);
    DeleteObject(font);
}

static LRESULT CALLBACK pattern_proc(HWND wnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_KEYDOWN:
        if (wp == VK_ESCAPE || wp == VK_SPACE) {
            DestroyWindow(wnd);
            g_pattern_wnd = NULL;
            return 0;
        }
        break;
    case WM_LBUTTONDOWN:
    case WM_RBUTTONDOWN:
        DestroyWindow(wnd);
        g_pattern_wnd = NULL;
        return 0;
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(wnd, &ps);
        RECT rc;
        GetClientRect(wnd, &rc);
        draw_pattern(dc, &rc, g_cur_pattern);
        EndPaint(wnd, &ps);
        return 0;
    }
    case WM_DESTROY:
        g_pattern_wnd = NULL;
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
    if (g_pattern_wnd) DestroyWindow(g_pattern_wnd);

    g_cur_pattern = clampi(pattern_id, 0, PAT_COUNT - 1);

    int sx = GetSystemMetrics(SM_XVIRTUALSCREEN);
    int sy = GetSystemMetrics(SM_YVIRTUALSCREEN);
    int sw = GetSystemMetrics(SM_CXVIRTUALSCREEN);
    int sh = GetSystemMetrics(SM_CYVIRTUALSCREEN);

    g_pattern_wnd = CreateWindowExW(WS_EX_TOPMOST, PX_TEST_CLASS, L"PlexusX Pattern",
                                    WS_POPUP | WS_VISIBLE, sx, sy, sw, sh,
                                    NULL, NULL, g_inst, NULL);
    if (g_pattern_wnd) {
        SetForegroundWindow(g_pattern_wnd);
    }
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
