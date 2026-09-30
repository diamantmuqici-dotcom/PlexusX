/* crosshair.c — click-through layered overlay drawn above everything */
#include "common.h"

static XhCfg   g_xh = { 0, XH_CROSS, 16, 4, 2, 100, 1, RGB(0x7C,0xFF,0x40), RGB(0,0,0) };
static HWND    g_xh_wnd;
static int     g_xh_shown;

/* ---- draw shape at supersample S into a top-down 32bpp DIB ---- */
static void draw_shape(BYTE *bits, int W, int H, const XhCfg *c, int S)
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
    if (!bmp || !pb) { if (bmp) DeleteObject(bmp); CloseHandle(dc); return; }
    memset(pb, 0, (size_t)W * H * 4);
    SelectObject(dc, bmp);

    int cx = W / 2, cy = H / 2;
    int arm  = c->size * S;
    int gap  = c->gap * S;
    int th   = c->thick * S;
    int out  = c->outline ? (2 * S) : 0;

    HPEN penO = CreatePen(PS_SOLID | PS_ENDCAP_SQUARE, th + out * 2, g_xh.ocolor);
    HPEN penF = CreatePen(PS_SOLID | PS_ENDCAP_SQUARE, th, g_xh.color);
    HBRUSH brF = CreateSolidBrush(g_xh.color);
    HBRUSH brNull = (HBRUSH)GetStockObject(NULL_BRUSH);
    HGDIOBJ oldPen = SelectObject(dc, penO), oldBr = SelectObject(dc, brNull);

    int dot_r = (th + out) ;           /* dot radius before scale */
    int need_dot = (c->shape == XH_DOT);
    int need_shape = !need_dot;

#define OUTLINE_ON()  SelectObject(dc, penO); SelectObject(dc, brNull)
#define FILL_ON()     SelectObject(dc, penF); SelectObject(dc, brNull)

    if (need_shape) {
        /* pass 1 — outline strokes */
        if (c->outline) {
            switch (c->shape) {
            case XH_CROSS:
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
                POINT p[3] = {
                    { cx - arm - gap, cy - arm - gap },
                    { cx + gap,       cy },
                    { cx - arm - gap, cy + arm + gap } };
                Polyline(dc, p, 3);
                break; }
            case XH_CIRCLE: {
                int r = gap + arm;
                Ellipse(dc, cx - r, cy - r, cx + r, cy + r);
                break; }
            }
        }
        /* pass 2 — colour strokes */
        FILL_ON();
        switch (c->shape) {
        case XH_CROSS:
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
            POINT p[3] = {
                { cx - arm - gap, cy - arm - gap },
                { cx + gap,       cy },
                { cx - arm - gap, cy + arm + gap } };
            Polyline(dc, p, 3);
            break; }
        case XH_CIRCLE: {
            int r = gap + arm;
            Ellipse(dc, cx - r, cy - r, cx + r, cy + r);
            break; }
        }
    }

    /* alpha pass 1: opaque where anything drawn */
    {
        DWORD *px = (DWORD *)pb;
        for (int i = 0; i < W * H; i++)
            if (px[i] & 0x00FFFFFF) px[i] |= 0xFF000000;
    }

    /* dot — separate so it can carry its own opacity */
    if (c->shape == XH_DOT || c->shape == XH_CROSS || c->shape == XH_CIRCLE) {
        /* centre dot is optional on top of shapes only for XH_DOT */
    }
    if (c->shape == XH_DOT) {
        SelectObject(dc, penF); SelectObject(dc, brF);
        int r = dot_r;
        Ellipse(dc, cx - r, cy - r, cx + r, cy + r);
        DWORD *px = (DWORD *)pb;
        int a = clampi(g_xh.dotop, 0, 100) * 255 / 100;
        /* pixels the brush just touched have rgb but no alpha yet — tag via colour match */
        for (int i = 0; i < W * H; i++) {
            DWORD v = px[i];
            if ((v & 0x00FFFFFF) && !(v & 0xFF000000)) {
                BYTE rr = GetRValue(v), gg = GetGValue(v), bb = GetBValue(v);
                px[i] = ((DWORD)a << 24) | ((DWORD)(rr * a / 255) << 16) |
                        ((DWORD)(gg * a / 255) << 8) | (DWORD)(bb * a / 255);
            }
        }
        /* premultiply fully opaque shape pixels */
        for (int i = 0; i < W * H; i++) {
            DWORD v = px[i];
            if ((v & 0xFF000000) == 0xFF000000) {
                BYTE rr = GetRValue(v), gg = GetGValue(v), bb = GetBValue(v);
                px[i] = 0xFF000000 | ((DWORD)rr << 16) | ((DWORD)gg << 8) | bb;
            }
        }
    }

    /* copy result out */
    memcpy(bits, pb, (size_t)W * H * 4);

    SelectObject(dc, oldPen); SelectObject(dc, oldBr);
    DeleteObject(penO); DeleteObject(penF); DeleteObject(brF);
    DeleteObject(bmp);
    DeleteDC(dc);
}

static void xh_render_and_show(void)
{
    if (!g_xh_wnd) return;
    if (!g_xh.on) {
        if (g_xh_shown) { ShowWindow(g_xh_wnd, SW_HIDE); g_xh_shown = 0; }
        return;
    }

    int S = 4;                       /* supersample */
    int pad = 8;
    int w1 = 2 * (g_xh.size + g_xh.gap + g_xh.thick + g_xh.outline * 2 + pad) + 8;
    int h1 = w1;
    if (w1 < 24) w1 = h1 = 24;
    int W = w1 * S, H = h1 * S;

    /* draw big */
    BYTE *big = (BYTE *)calloc((size_t)W * H, 4);
    if (!big) return;
    draw_shape(big, W, H, &g_xh, S);

    /* downsample to 1x (halftone-ish box via StretchDIBits) */
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
    if (!small || !pb1) { free(big); if (small) DeleteObject(small); return; }

    HDC sdc = CreateCompatibleDC(NULL);
    HGDIOBJ olds = SelectObject(sdc, small);
    SetStretchBltMode(sdc, HALFTONE);
    SetBrushOrgEx(sdc, 0, 0, NULL);
    /* stretch the supersampled buffer straight into the 1x DIB */
    StretchDIBits(sdc, 0, 0, w1, h1, 0, 0, W, H, big, &bi,
                  DIB_RGB_COLORS, SRCCOPY);

    POINT ptSrc = { 0, 0 };
    SIZE sz = { w1, h1 };
    RECT mon;
    POINT cp;
    GetCursorPos(&cp);
    HMONITOR monh = MonitorFromPoint(cp, MONITOR_DEFAULTTOPRIMARY);
    MONITORINFO mi; mi.cbSize = sizeof mi;
    GetMonitorInfoW(monh, &mi);
    mon = mi.rcMonitor;
    POINT pos = { (mon.left + mon.right) / 2 - w1 / 2,
                  (mon.top + mon.bottom) / 2 - h1 / 2 };

    BLENDFUNCTION bf = { AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };
    UpdateLayeredWindow(g_xh_wnd, NULL, &pos, &sz, sdc, &ptSrc, 0, &bf, ULW_ALPHA);

    SelectObject(sdc, olds);
    DeleteDC(sdc);
    DeleteObject(small);
    free(big);

    if (!g_xh_shown) { ShowWindow(g_xh_wnd, SW_SHOWNOACTIVATE); g_xh_shown = 1; }
    SetWindowPos(g_xh_wnd, HWND_TOPMOST, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
}

static LRESULT CALLBACK xh_proc(HWND wnd, UINT msg, WPARAM wp, LPARAM lp)
{
    (void)wp; (void)lp;
    switch (msg) {
    case WM_NCHITTEST: return HTTRANSPARENT;
    case WM_ERASEBKGND: return 1;
    case WM_PAINT: { PAINTSTRUCT ps; BeginPaint(wnd, &ps); EndPaint(wnd, &ps); return 0; }
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
    wc.lpszClassName = CX_XH_CLASS;
    RegisterClassW(&wc);

    g_xh_wnd = CreateWindowExW(
        WS_EX_LAYERED | WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_TRANSPARENT |
        WS_EX_NOACTIVATE,
        CX_XH_CLASS, L"", WS_POPUP, 0, 0, 10, 10, NULL, NULL, g_inst, NULL);
}

void Xh_Shutdown(void)
{
    g_xh.on = 0;
    if (g_xh_wnd) { DestroyWindow(g_xh_wnd); g_xh_wnd = NULL; }
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
