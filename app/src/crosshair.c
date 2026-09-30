/* crosshair.c \u2014 desktop crosshair overlay
 *
 * A real, legitimate tool: a transparent, click-through, topmost layered
 * window on the desktop (the same technique every legal crosshair utility
 * uses).  It draws 9 original vector shapes, honours per-monitor placement
 * and whole-overlay opacity, and is never injected into any process.
 */
#include "common.h"

static HWND g_xhwnd;
static XhCfg g_cfg;
static HBITMAP g_bmp;

static struct XhDraw {
    uint32_t *px;
    int w, h, cx, cy;
    int s, g, t;         /* size, gap, thick */
    DWORD fc, oc;        /* fill / outline colour (premultiplied by opacity) */
    int outline;
} D;

static void putpx(int x, int y, DWORD c)
{
    if (x >= 0 && y >= 0 && x < D.w && y < D.h && c)
        D.px[y * D.w + x] = c;
}

static void draw_cross(void)
{
    for (int pass = 0; pass < 2; pass++) {
        int th = pass ? D.t : (D.outline ? D.t + 2 : 0);
        if (!th) continue;
        DWORD col = pass ? D.fc : D.oc;
        int hh = th / 2, ext = D.s + D.g + hh;
        for (int yy = D.cy - hh; yy < D.cy + th; yy++)
            for (int xx = D.cx - ext; xx < D.cx + ext; xx++)
                if (xx < D.cx - D.g - hh || xx >= D.cx + D.g + hh) putpx(xx, yy, col);
        for (int xx = D.cx - hh; xx < D.cx + th; xx++)
            for (int yy = D.cy - ext; yy < D.cy + ext; yy++)
                if (yy < D.cy - D.g - hh || yy >= D.cy + D.g + hh) putpx(xx, yy, col);
    }
}

static void draw_dot(void)
{
    int r = D.s / 2;
    for (int y = -r; y <= r; y++)
        for (int x = -r; x <= r; x++)
            if (x * x + y * y <= r * r) {
                int edge = D.outline && (x * x + y * y > (r - 1) * (r - 1));
                putpx(D.cx + x, D.cy + y, edge ? D.oc : D.fc);
            }
}

static void draw_ring(void)
{
    int r = D.s, ri = D.s - D.t;
    for (int y = -r - 2; y <= r + 2; y++)
        for (int x = -r - 2; x <= r + 2; x++) {
            int d2 = x * x + y * y;
            int outer = (r + (D.outline ? 1 : 0));
            if (d2 <= outer * outer && d2 >= ri * ri) {
                int edge = D.outline && (d2 > r * r);
                putpx(D.cx + x, D.cy + y, edge ? D.oc : D.fc);
            }
        }
}

static void draw_square(void)
{
    int s = D.s;
    for (int y = -s; y <= s; y++)
        for (int x = -s; x <= s; x++) {
            int ax = abs(x), ay = abs(y);
            int o = s - 1, i = s - D.t;
            if (ax <= o + 1 && ay <= o + 1 &&
                (ax == o || ay == o || ax == i || ay == i)) {
                int edge = D.outline && (ax == o + 1 || ay == o + 1);
                putpx(D.cx + x, D.cy + y, edge ? D.oc : D.fc);
            }
        }
}

static void draw_plus(void)
{
    int arm = D.t / 2 + 1;
    for (int y = -D.s; y <= D.s; y++)
        for (int x = -D.s; x <= D.s; x++) {
            if ((abs(y) < arm && abs(x) <= D.s) || (abs(x) < arm && abs(y) <= D.s)) {
                int edge = D.outline && ((abs(y) == arm - 1) || (abs(x) == arm - 1));
                putpx(D.cx + x, D.cy + y, edge ? D.oc : D.fc);
            }
        }
}

static void draw_chevron(void)
{
    float ang = (float)g_cfg.rotation * 3.14159265f / 180.f;
    float ca = cosf(ang), sa = sinf(ang);
    for (int y = -D.s - 2; y <= D.s + 2; y++)
        for (int x = -D.s - 2; x <= D.s + 2; x++) {
            float rx = x * ca - y * sa;
            float ry = x * sa + y * ca;
            float v = fabsf(rx) / (float)D.s + (float)D.s - ry;
            if (v < (float)D.t && ry < D.s - 1 && ry > -D.s) {
                int edge = D.outline && (v > (float)D.t - 2.f);
                putpx(D.cx + x, D.cy + y, edge ? D.oc : D.fc);
            }
        }
}

static void draw_T(void)
{
    int arm = D.t / 2 + 1;
    for (int y = -D.s; y <= D.s; y++)
        for (int x = -D.s; x <= D.s; x++) {
            int top = (y >= -D.s && y <= -D.s + D.t);
            int stem = (abs(x) < arm && y > -D.s + D.t);
            if (top || stem) {
                int edge = D.outline && ((y == -D.s) || (stem && abs(x) == arm - 1));
                putpx(D.cx + x, D.cy + y, edge ? D.oc : D.fc);
            }
        }
}

static void draw_Ttype(void)
{
    int arm = D.t / 2 + 1;
    for (int y = -D.s; y <= D.s; y++)
        for (int x = -D.s; x <= D.s; x++) {
            int bot = (y >= D.s - D.t && y <= D.s);
            int stem = (abs(x) < arm && y < D.s - D.t);
            if (bot || stem) {
                int edge = D.outline && ((y == D.s) || (stem && abs(x) == arm - 1));
                putpx(D.cx + x, D.cy + y, edge ? D.oc : D.fc);
            }
        }
}

static void draw_fourdot(void)
{
    int r = (D.t + 2) / 2, d = D.s + D.g;
    int pts[4][2] = { { -d, 0 }, { d, 0 }, { 0, -d }, { 0, d } };
    for (int i = 0; i < 4; i++)
        for (int y = -r; y <= r; y++)
            for (int x = -r; x <= r; x++)
                if (x * x + y * y <= r * r) {
                    int edge = D.outline && (x * x + y * y > (r - 1) * (r - 1));
                    putpx(D.cx + pts[i][0] + x, D.cy + pts[i][1] + y, edge ? D.oc : D.fc);
                }
}

static HBITMAP build_bitmap(int w, int h)
{
    HDC screen = GetDC(NULL);
    HDC dc = CreateCompatibleDC(screen);
    BITMAPV5HEADER bi;
    memset(&bi, 0, sizeof bi);
    bi.bV5Size = sizeof bi;
    bi.bV5Width = w;
    bi.bV5Height = -h;              /* top-down */
    bi.bV5Planes = 1;
    bi.bV5BitCount = 32;
    bi.bV5Compression = BI_BITFIELDS;
    bi.bV5RedMask = 0x00FF0000;
    bi.bV5GreenMask = 0x0000FF00;
    bi.bV5BlueMask = 0x000000FF;
    void *bits = NULL;
    HBITMAP bmp = CreateDIBSection(screen, (BITMAPINFO *)&bi, DIB_RGB_COLORS,
                                   &bits, NULL, 0);
    if (!bmp || !bits) {
        if (bmp) DeleteObject(bmp);
        DeleteDC(dc);
        ReleaseDC(NULL, screen);
        return NULL;
    }
    memset(bits, 0, (size_t)w * h * 4);
    HBITMAP old = (HBITMAP)SelectObject(dc, bmp);

    int a = (int)(g_cfg.opacity * 255 / 100);
    D.px = (uint32_t *)bits;
    D.w = w; D.h = h;
    D.cx = w / 2; D.cy = h / 2;
    D.s = g_cfg.size; D.g = g_cfg.gap; D.t = g_cfg.thick;
    D.fc = (a << 24) | (GetRValue(g_cfg.color) << 16) | (GetGValue(g_cfg.color) << 8) | GetBValue(g_cfg.color);
    D.oc = (a << 24) | (GetRValue(g_cfg.ocolor) << 16) | (GetGValue(g_cfg.ocolor) << 8) | GetBValue(g_cfg.ocolor);
    D.outline = g_cfg.outline;

    switch (g_cfg.shape) {
    case CXXH_DOT:      draw_dot(); break;
    case CXXH_CIRCLE:   draw_ring(); break;
    case CXXH_SQUARE:   draw_square(); break;
    case CXXH_PLUS:     draw_plus(); break;
    case CXXH_CHEVRON:  draw_chevron(); break;
    case CXXH_T:        draw_T(); break;
    case CXXH_TTYPE:    draw_Ttype(); break;
    case CXXH_FOURDOT:  draw_fourdot(); break;
    case CXXH_CROSS:
    default:            draw_cross(); break;
    }

    if (g_cfg.dot && (g_cfg.shape == CXXH_CROSS || g_cfg.shape == CXXH_PLUS ||
                      g_cfg.shape == CXXH_CIRCLE)) {
        int r = (D.t + 2) / 2;
        for (int y = -r; y <= r; y++)
            for (int x = -r; x <= r; x++)
                if (x * x + y * y <= r * r)
                    putpx(D.cx + x, D.cy + y, D.fc);
    }

    SelectObject(dc, old);
    DeleteDC(dc);
    ReleaseDC(NULL, screen);
    return bmp;
}

static int monitor_rect(int which, RECT *out)
{
    if (which < 0) {
        RECT wa;
        SystemParametersInfoW(SPI_GETWORKAREA, 0, &wa, 0);
        *out = wa;
        return 1;
    }
    const MonInfo *m = MonGet(which);
    if (!m) return 0;
    out->left = m->x; out->top = m->y;
    out->right = m->x + m->w; out->bottom = m->y + m->h;
    return 1;
}

static void ensure_window(void)
{
    if (g_xhwnd) return;
    WNDCLASSEXW wc;
    memset(&wc, 0, sizeof wc);
    wc.cbSize = sizeof wc;
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = DefWindowProcW;
    wc.hInstance = g_inst;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.lpszClassName = CX_XH_CLASS;
    if (!RegisterClassExW(&wc)) return;
    g_xhwnd = CreateWindowExW(
        WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOPMOST | WS_EX_TOOLWINDOW,
        CX_XH_CLASS, L"ChromaX Crosshair", WS_POPUP,
        0, 0, 1, 1, NULL, NULL, g_inst, NULL);
}

void Xh_Init(void)
{
    g_cfg.on = 0;
    g_cfg.shape = CXXH_CROSS;
    g_cfg.size = 22;
    g_cfg.gap = 6;
    g_cfg.thick = 3;
    g_cfg.opacity = 100;
    g_cfg.outline = 1;
    g_cfg.dot = 0;
    g_cfg.rotation = 0;
    g_cfg.monitor = -1;
    g_cfg.color = RGB(255, 255, 255);
    g_cfg.ocolor = RGB(0, 0, 0);
}

void Xh_Update(const XhCfg *cfg)
{
    g_cfg = *cfg;
    g_cfg.size = clampi(g_cfg.size, 6, 64);
    g_cfg.gap = clampi(g_cfg.gap, 0, 24);
    g_cfg.thick = clampi(g_cfg.thick, 1, 10);
    g_cfg.opacity = clampi(g_cfg.opacity, 0, 100);
    g_cfg.rotation = (g_cfg.rotation % 360 + 360) % 360;
    if (g_cfg.monitor >= MonCount()) g_cfg.monitor = -1;

    if (!g_cfg.on) {
        if (g_xhwnd) ShowWindow(g_xhwnd, SW_HIDE);
        if (g_bmp) { DeleteObject(g_bmp); g_bmp = NULL; }
        return;
    }

    ensure_window();
    if (!g_xhwnd) return;

    RECT r;
    if (!monitor_rect(g_cfg.monitor, &r)) return;

    HBITMAP bmp = build_bitmap(r.right - r.left, r.bottom - r.top);
    if (!bmp) return;
    if (g_bmp) DeleteObject(g_bmp);
    g_bmp = bmp;

    MoveWindow(g_xhwnd, r.left, r.top, r.right - r.left, r.bottom - r.top, TRUE);
    BLENDFUNCTION bf;
    bf.BlendOp = AC_SRC_OVER;
    bf.SourceConstantAlpha = 255;
    bf.AlphaFormat = AC_SRC_ALPHA;
    /* UpdateLayeredWindow composites from a source DC holding the DIB */
    HDC screen = GetDC(NULL);
    HDC mem = CreateCompatibleDC(screen);
    HGDIOBJ old = SelectObject(mem, bmp);
    SIZE sz = { r.right - r.left, r.bottom - r.top };
    POINT src = { 0, 0 };
    UpdateLayeredWindow(g_xhwnd, NULL, NULL, &sz, mem, &src, 0, &bf, ULW_ALPHA);
    SelectObject(mem, old);
    DeleteDC(mem);
    ReleaseDC(NULL, screen);
    ShowWindow(g_xhwnd, SW_SHOWNOACTIVATE);
}

void Xh_Toggle(void)
{
    g_cfg.on = !g_cfg.on;
    Xh_Update(&g_cfg);
}

void Xh_Shutdown(void)
{
    if (g_bmp) { DeleteObject(g_bmp); g_bmp = NULL; }
    if (g_xhwnd) { DestroyWindow(g_xhwnd); g_xhwnd = NULL; }
}
