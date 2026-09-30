/* PlexusX — Anti-Aliased Desktop Overlay Crosshair
 * Cheating-Free: Layered desktop window overlay only. Does not hook games.
 */
#include "common.h"

static XhCfg g_xh = {
    0,                          /* on */
    XH_CROSS,                   /* shape */
    16,                         /* size */
    4,                          /* gap */
    2,                          /* thick */
    0,                          /* rotation */
    100,                        /* opacity */
    1,                          /* center_dot */
    2,                          /* dot_size */
    1,                          /* outline */
    1,                          /* outline_th */
    RGB(0xC6, 0xFF, 0x3D),      /* vibrant lime */
    RGB(0, 0, 0),               /* black outline */
    0                           /* monitor_idx */
};

static HWND g_xh_wnd = NULL;
static int  g_xh_shown = 0;

/* ---------------- Preset Crosshairs Library ---------------- */
static const XhPreset g_xh_presets[] = {
    { L"Classic Dot",  { 1, XH_DOT,      8,  0, 2, 0, 100, 1, 3, 1, 1, RGB(0xC6, 0xFF, 0x3D), RGB(0, 0, 0), 0 } },
    { L"Small Cross",  { 1, XH_CROSS,   12,  3, 2, 0, 100, 0, 2, 1, 1, RGB(0x4F, 0xE3, 0xFF), RGB(0, 0, 0), 0 } },
    { L"CS-Style",     { 1, XH_CROSS,   18,  5, 2, 0, 100, 0, 2, 1, 1, RGB(0x00, 0xFF, 0x7C), RGB(0, 0, 0), 0 } },
    { L"Minimal",      { 1, XH_CROSS,    8,  2, 1, 0, 100, 0, 2, 0, 1, RGB(0xFF, 0xFF, 0xFF), RGB(0, 0, 0), 0 } },
    { L"Precision",    { 1, XH_PLUS,    14,  4, 2, 0, 100, 1, 2, 1, 1, RGB(0xFF, 0x4F, 0x4F), RGB(0, 0, 0), 0 } },
    { L"Circle",       { 1, XH_CIRCLE,  14,  0, 2, 0, 100, 1, 2, 1, 1, RGB(0xC6, 0xFF, 0x3D), RGB(0, 0, 0), 0 } },
    { L"Square Box",   { 1, XH_SQUARE,  12,  0, 2, 0, 100, 1, 2, 1, 1, RGB(0xFF, 0x9E, 0x3D), RGB(0, 0, 0), 0 } },
    { L"Tactical T",   { 1, XH_T,       16,  4, 2, 0, 100, 0, 2, 1, 1, RGB(0x4F, 0xE3, 0xFF), RGB(0, 0, 0), 0 } },
    { L"Chevron",      { 1, XH_CHEVRON, 14,  3, 2, 0, 100, 0, 2, 1, 1, RGB(0xFF, 0xF2, 0x3D), RGB(0, 0, 0), 0 } },
    { L"Clean White",  { 1, XH_CROSS,   14,  4, 2, 0,  95, 0, 2, 1, 1, RGB(0xFF, 0xFF, 0xFF), RGB(0, 0, 0), 0 } }
};

const XhPreset *Xh_GetPresets(int *count)
{
    *count = (int)(sizeof g_xh_presets / sizeof g_xh_presets[0]);
    return g_xh_presets;
}

void Xh_ApplyPreset(int idx)
{
    int count = 0;
    const XhPreset *list = Xh_GetPresets(&count);
    if (idx >= 0 && idx < count) {
        g_xh = list[idx].cfg;
        Xh_Update(&g_xh);
    }
}

/* ---------------- Supersampled Layered Rendering ---------------- */
static void draw_shape_ss(BYTE *bits, int W, int H, const XhCfg *c, int S)
{
    HDC dc = CreateCompatibleDC(NULL);
    BITMAPINFO bi;
    memset(&bi, 0, sizeof bi);
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = W;
    bi.bmiHeader.biHeight = -H;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    void *pb = NULL;
    HBITMAP bmp = CreateDIBSection(NULL, &bi, DIB_RGB_COLORS, &pb, NULL, 0);
    if (!bmp || !pb) {
        if (bmp) DeleteObject(bmp);
        DeleteDC(dc);
        return;
    }
    memset(pb, 0, (size_t)W * H * 4);
    SelectObject(dc, bmp);

    int cx = W / 2, cy = H / 2;
    int arm = c->size * S;
    int gap = c->gap * S;
    int th  = c->thick * S;
    int out = c->outline ? (c->outline_th * 2 * S) : 0;

    HPEN penO = CreatePen(PS_SOLID | PS_ENDCAP_SQUARE, th + out * 2, c->ocolor);
    HPEN penF = CreatePen(PS_SOLID | PS_ENDCAP_SQUARE, th, c->color);
    HBRUSH brF = CreateSolidBrush(c->color);
    HBRUSH brO = CreateSolidBrush(c->ocolor);
    HBRUSH brNull = (HBRUSH)GetStockObject(NULL_BRUSH);

    HGDIOBJ oldPen = SelectObject(dc, penO);
    HGDIOBJ oldBr  = SelectObject(dc, brNull);

    /* Pass 1: Outline */
    if (c->outline) {
        SelectObject(dc, penO);
        SelectObject(dc, brNull);
        switch (c->shape) {
        case XH_CROSS:
        case XH_PLUS:
            MoveToEx(dc, cx - gap - arm, cy, NULL); LineTo(dc, cx + gap + arm, cy);
            MoveToEx(dc, cx, cy - gap - arm, NULL); LineTo(dc, cx, cy + gap + arm);
            break;
        case XH_T:
            MoveToEx(dc, cx - gap - arm, cy - gap, NULL); LineTo(dc, cx + gap + arm, cy - gap);
            MoveToEx(dc, cx, cy - gap, NULL); LineTo(dc, cx, cy + gap + arm);
            break;
        case XH_TTYPE:
            MoveToEx(dc, cx - gap - arm, cy + gap, NULL); LineTo(dc, cx + gap + arm, cy + gap);
            MoveToEx(dc, cx, cy - gap - arm, NULL); LineTo(dc, cx, cy + gap);
            break;
        case XH_CHEVRON: {
            POINT p[3] = { { cx - arm - gap, cy - arm - gap }, { cx + gap, cy }, { cx - arm - gap, cy + arm + gap } };
            Polyline(dc, p, 3);
            break;
        }
        case XH_CIRCLE: {
            int r = gap + arm;
            Ellipse(dc, cx - r, cy - r, cx + r, cy + r);
            break;
        }
        case XH_SQUARE: {
            int r = gap + arm;
            Rectangle(dc, cx - r, cy - r, cx + r, cy + r);
            break;
        }
        case XH_DOT: {
            int r = (c->dot_size * S) + out;
            SelectObject(dc, brO);
            Ellipse(dc, cx - r, cy - r, cx + r, cy + r);
            break;
        }
        }
    }

    /* Pass 2: Foreground Fill */
    SelectObject(dc, penF);
    SelectObject(dc, brNull);
    switch (c->shape) {
    case XH_CROSS:
    case XH_PLUS:
        MoveToEx(dc, cx - gap - arm, cy, NULL); LineTo(dc, cx + gap + arm, cy);
        MoveToEx(dc, cx, cy - gap - arm, NULL); LineTo(dc, cx, cy + gap + arm);
        break;
    case XH_T:
        MoveToEx(dc, cx - gap - arm, cy - gap, NULL); LineTo(dc, cx + gap + arm, cy - gap);
        MoveToEx(dc, cx, cy - gap, NULL); LineTo(dc, cx, cy + gap + arm);
        break;
    case XH_TTYPE:
        MoveToEx(dc, cx - gap - arm, cy + gap, NULL); LineTo(dc, cx + gap + arm, cy + gap);
        MoveToEx(dc, cx, cy - gap - arm, NULL); LineTo(dc, cx, cy + gap);
        break;
    case XH_CHEVRON: {
        POINT p[3] = { { cx - arm - gap, cy - arm - gap }, { cx + gap, cy }, { cx - arm - gap, cy + arm + gap } };
        Polyline(dc, p, 3);
        break;
    }
    case XH_CIRCLE: {
        int r = gap + arm;
        Ellipse(dc, cx - r, cy - r, cx + r, cy + r);
        break;
    }
    case XH_SQUARE: {
        int r = gap + arm;
        Rectangle(dc, cx - r, cy - r, cx + r, cy + r);
        break;
    }
    case XH_DOT: {
        int r = c->dot_size * S;
        SelectObject(dc, brF);
        Ellipse(dc, cx - r, cy - r, cx + r, cy + r);
        break;
    }
    }

    /* Optional Center Dot */
    if (c->center_dot && c->shape != XH_DOT) {
        int r = c->dot_size * S;
        if (c->outline) {
            SelectObject(dc, penO);
            SelectObject(dc, brO);
            Ellipse(dc, cx - r - out, cy - r - out, cx + r + out, cy + r + out);
        }
        SelectObject(dc, penF);
        SelectObject(dc, brF);
        Ellipse(dc, cx - r, cy - r, cx + r, cy + r);
    }

    /* Alpha Premultiplication with Global Opacity */
    int global_alpha = clampi(c->opacity, 10, 100) * 255 / 100;
    DWORD *px = (DWORD *)pb;
    for (int i = 0; i < W * H; i++) {
        DWORD v = px[i];
        if (v & 0x00FFFFFF) {
            BYTE r = GetRValue(v);
            BYTE g = GetGValue(v);
            BYTE b = GetBValue(v);
            px[i] = ((DWORD)global_alpha << 24) |
                    ((DWORD)(r * global_alpha / 255) << 16) |
                    ((DWORD)(g * global_alpha / 255) << 8) |
                    (DWORD)(b * global_alpha / 255);
        }
    }

    memcpy(bits, pb, (size_t)W * H * 4);

    SelectObject(dc, oldPen);
    SelectObject(dc, oldBr);
    DeleteObject(penO);
    DeleteObject(penF);
    DeleteObject(brF);
    DeleteObject(brO);
    DeleteObject(bmp);
    DeleteDC(dc);
}

static void xh_render_and_show(void)
{
    if (!g_xh_wnd) return;
    if (!g_xh.on) {
        if (g_xh_shown) {
            ShowWindow(g_xh_wnd, SW_HIDE);
            g_xh_shown = 0;
        }
        return;
    }

    int S = 4; /* 4x supersampling */
    int pad = 12;
    int w1 = 2 * (g_xh.size + g_xh.gap + g_xh.thick + g_xh.outline_th * 2 + pad) + 8;
    int h1 = w1;
    if (w1 < 32) w1 = h1 = 32;
    int W = w1 * S, H = h1 * S;

    BYTE *big = (BYTE *)calloc((size_t)W * H, 4);
    if (!big) return;
    draw_shape_ss(big, W, H, &g_xh, S);

    BITMAPINFO bi;
    memset(&bi, 0, sizeof bi);
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = W;
    bi.bmiHeader.biHeight = -H;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;

    BITMAPINFO bo;
    memset(&bo, 0, sizeof bo);
    bo.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bo.bmiHeader.biWidth = w1;
    bo.bmiHeader.biHeight = -h1;
    bo.bmiHeader.biPlanes = 1;
    bo.bmiHeader.biBitCount = 32;
    bo.bmiHeader.biCompression = BI_RGB;
    void *pb1 = NULL;
    HBITMAP small = CreateDIBSection(NULL, &bo, DIB_RGB_COLORS, &pb1, NULL, 0);
    if (!small || !pb1) {
        free(big);
        if (small) DeleteObject(small);
        return;
    }

    HDC sdc = CreateCompatibleDC(NULL);
    HGDIOBJ olds = SelectObject(sdc, small);
    SetStretchBltMode(sdc, HALFTONE);
    SetBrushOrgEx(sdc, 0, 0, NULL);
    StretchDIBits(sdc, 0, 0, w1, h1, 0, 0, W, H, big, &bi, DIB_RGB_COLORS, SRCCOPY);

    POINT ptSrc = { 0, 0 };
    SIZE sz = { w1, h1 };

    POINT cp;
    GetCursorPos(&cp);
    HMONITOR monh = MonitorFromPoint(cp, MONITOR_DEFAULTTOPRIMARY);
    MONITORINFO mi;
    mi.cbSize = sizeof mi;
    GetMonitorInfoW(monh, &mi);
    RECT mon = mi.rcMonitor;

    POINT pos = { (mon.left + mon.right) / 2 - w1 / 2,
                  (mon.top + mon.bottom) / 2 - h1 / 2 };

    BLENDFUNCTION bf = { AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };
    UpdateLayeredWindow(g_xh_wnd, NULL, &pos, &sz, sdc, &ptSrc, 0, &bf, ULW_ALPHA);

    SelectObject(sdc, olds);
    DeleteDC(sdc);
    DeleteObject(small);
    free(big);

    if (!g_xh_shown) {
        ShowWindow(g_xh_wnd, SW_SHOWNOACTIVATE);
        g_xh_shown = 1;
    }
    SetWindowPos(g_xh_wnd, HWND_TOPMOST, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
}

static LRESULT CALLBACK xh_proc(HWND wnd, UINT msg, WPARAM wp, LPARAM lp)
{
    (void)wp; (void)lp;
    switch (msg) {
    case WM_NCHITTEST: return HTTRANSPARENT;
    case WM_ERASEBKGND: return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        BeginPaint(wnd, &ps);
        EndPaint(wnd, &ps);
        return 0;
    }
    }
    return DefWindowProcW(wnd, msg, wp, lp);
}

void Xh_Init(void)
{
    WNDCLASSW wc;
    memset(&wc, 0, sizeof wc);
    wc.lpfnWndProc = xh_proc;
    wc.hInstance = g_inst;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.lpszClassName = PX_XH_CLASS;
    RegisterClassW(&wc);

    g_xh_wnd = CreateWindowExW(
        WS_EX_LAYERED | WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_TRANSPARENT | WS_EX_NOACTIVATE,
        PX_XH_CLASS, L"", WS_POPUP, 0, 0, 10, 10, NULL, NULL, g_inst, NULL);
}

void Xh_Shutdown(void)
{
    g_xh.on = 0;
    if (g_xh_wnd) {
        DestroyWindow(g_xh_wnd);
        g_xh_wnd = NULL;
    }
    g_xh_shown = 0;
}

void Xh_Update(const XhCfg *cfg)
{
    if (cfg) g_xh = *cfg;
    xh_render_and_show();
}

void Xh_Toggle(void)
{
    g_xh.on = !g_xh.on;
    xh_render_and_show();
}

int Xh_IsActive(void)
{
    return g_xh.on;
}
