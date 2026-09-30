/* diag.c \u2014 diagnostics, test patterns, export
 *
 * Test patterns are drawn with plain GDI onto a fullscreen popup window:
 *  - DISPLAY : grey steps + colour bars (blur / ghosting / uniformity)
 *  - COLOR   : colour accuracy reference grid
 *  - HDR     : bright-field patch (should look brighter than the SDR
 *              surround when the display's HDR pipeline is active)
 *  - MULTIMON: the pattern on every monitor at once
 * The diagnostic export writes a plain-text report (GPU, EDID identity,
 * modes, HDR state, engine capabilities, recent log) to a user-chosen
 * path \u2014 all local, nothing leaves the machine.
 */
#include "common.h"

/* ---------------- export ---------------- */

static void w2f(FILE *f, const wchar_t *s)
{
    char buf[1024];
    int n = WideCharToMultiByte(CP_UTF8, 0, s, -1, buf, 1023, NULL, NULL);
    if (n > 0) { buf[n - 1] = 0; fputs(buf, f); }
}

int Diag_Export(const wchar_t *path)
{
    FILE *f = _wfopen(path, L"w,ccs=UTF-8");
    if (!f) return -1;
    OSVERSIONINFOEXW os;
    memset(&os, 0, sizeof os);
    os.dwOSVersionInfoSize = sizeof os;
    GetVersionExW((OSVERSIONINFO *)&os);
    fprintf(f, "ChromaX v"); w2f(f, CX_VERSION); fprintf(f, " diagnostic report\n");
    wchar_t t[64];
    SYSTEMTIME st;
    GetLocalTime(&st);
    wsprintfW(t, L"%04d-%02d-%02d %02d:%02d:%02d", st.wYear, st.wMonth, st.wDay,
              st.wHour, st.wMinute, st.wSecond);
    fprintf(f, "Date: "); w2f(f, t); fprintf(f, "\n");
    fprintf(f, "OS: Windows 10/11 build %lu\n", os.dwBuildNumber);
    fprintf(f, "GPU: %s\n\n", MonGpuName());

    fprintf(f, "Engine capabilities\n");
    fprintf(f, "  colour matrix layer (magnification.dll): %s\n",
            Eng_MagAvailable() ? "available" : "NOT available (per-monitor curve fallback)");
    fprintf(f, "  gamma ramp writes: %s\n",
            Eng_RampAvailable() ? "available" : "NOT available");
    fprintf(f, "  game-watch foreground hook: %s\n", Gw_Enabled() ? "active" : "inactive");
    fprintf(f, "  phone control: %s\n\n", Phone_Running() ? "running" : "stopped");

    fprintf(f, "Monitors (%d)\n", MonCount());
    for (int i = 0; i < MonCount(); i++) {
        const MonInfo *m = MonGet(i);
        HdrState hs;
        Hdr_ScanFor(i, &hs);
        fprintf(f, "  [%d] ", i + 1);
        w2f(f, m->name);
        fprintf(f, "  id=");
        w2f(f, m->id);
        fprintf(f, "\n");
        fprintf(f, "       %s %dx%d@%dHz %d bpc dpi~%d pos(%d,%d) %s\n",
                m->primary ? "primary" : "        ", m->res_w, m->res_h, m->hz,
                m->bpc, m->dpi, m->x, m->y, m->portrait ? "portrait" : "");
        fprintf(f, "       HDR: api=%s capable=%s on=%s sdr_white=%s\n",
                hs.supported ? "yes" : "no",
                hs.capable ? "yes" : "no",
                hs.enabled ? "yes" : "no",
                hs.sdr_white > 0 ? "supported" : "n/a");
        fprintf(f, "       modes listed: %d\n", Modes_CountFor(i));
    }
    fprintf(f, "\nNote: no telemetry. This report is created locally.\n");
    fclose(f);
    return 0;
}

/* ---------------- test patterns ---------------- */

static int g_pat_kind;

static void draw_pattern(HDC dc, int w, int h, int kind)
{
    /* base black */
    RECT all = { 0, 0, w, h };
    HBRUSH b = CreateSolidBrush(RGB(8, 8, 8));
    FillRect(dc, &all, b);
    DeleteObject(b);

    if (kind == DIAG_PAT_DISPLAY) {
        /* left 2/3: 8 grey steps; right 1/3: colour bars */
        int gw = (w * 2) / 3;
        for (int i = 0; i < 8; i++) {
            int v = (int)(20 + i * 30);
            RECT r = { (int)((long)i * gw / 8), 0, (int)((long)(i + 1) * gw / 8), h };
            b = CreateSolidBrush(RGB(v, v, v));
            FillRect(dc, &r, b);
            DeleteObject(b);
        }
        int bars[8][3] = {
            { 255, 255, 255 }, { 255, 255, 0 }, { 0, 255, 255 }, { 0, 200, 0 },
            { 255, 0, 255 }, { 255, 0, 0 }, { 0, 0, 255 }, { 40, 40, 40 },
        };
        int bw = (w - gw) / 8;
        for (int i = 0; i < 8; i++) {
            RECT r = { gw + i * bw, 0, gw + (i + 1) * bw, h };
            b = CreateSolidBrush(RGB(bars[i][0], bars[i][1], bars[i][2]));
            FillRect(dc, &r, b);
            DeleteObject(b);
        }
    } else if (kind == DIAG_PAT_COLOR) {
        /* accuracy grid: 6x4 patches (greys, soft colours, primaries, tones) */
        int cw = w / 6, ch = h / 4;
        for (int ry = 0; ry < 4; ry++)
            for (int cx = 0; cx < 6; cx++) {
                int pr, pg, pb;
                if (ry == 0)      { pr = 30 + cx * 40; pg = pr; pb = pr; }
                else if (ry == 1) { static const int c1[6][3] = { { 220,60,60 }, { 60,180,60 }, { 60,60,220 }, { 220,200,60 }, { 220,120,60 }, { 60,160,160 } };
                                    pr = c1[cx][0]; pg = c1[cx][1]; pb = c1[cx][2]; }
                else if (ry == 2) { static const int c2[6][3] = { { 255,0,0 }, { 0,255,0 }, { 0,0,255 }, { 255,255,0 }, { 255,0,255 }, { 0,255,255 } };
                                    pr = c2[cx][0]; pg = c2[cx][1]; pb = c2[cx][2]; }
                else              { pr = 240 - cx * 40; pg = pr; pb = pr; }
                RECT r = { cx * cw, ry * ch, (cx + 1) * cw, (ry + 1) * ch };
                b = CreateSolidBrush(RGB(pr, pg, pb));
                FillRect(dc, &r, b);
                DeleteObject(b);
            }
    } else if (kind == DIAG_PAT_HDR) {
        /* full white patch (should exceed SDR white in HDR mode) +
           60% field to judge relative brightness */
        RECT r1 = { 0, 0, w / 2, h };
        HBRUSH b1 = CreateSolidBrush(RGB(255, 255, 255));
        FillRect(dc, &r1, b1);
        DeleteObject(b1);
        RECT r2 = { w / 2, 0, w, h };
        HBRUSH b2 = CreateSolidBrush(RGB(153, 153, 153));
        FillRect(dc, &r2, b2);
        DeleteObject(b2);
    } else { /* MULTIMON: simple cross + identity */
        HPEN p = CreatePen(PS_SOLID, 3, RGB(255, 255, 0));
        HPEN old = (HPEN)SelectObject(dc, p);
        MoveToEx(dc, w / 2 - 60, h / 2, NULL); LineTo(dc, w / 2 + 60, h / 2);
        MoveToEx(dc, w / 2, h / 2 - 60, NULL); LineTo(dc, w / 2, h / 2 + 60);
        SelectObject(dc, old);
        DeleteObject(p);
    }

    /* label */
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, RGB(255, 255, 255));
    RECT tr = { 12, 8, w - 12, 34 };
    DrawTextW(dc, L"ChromaX test pattern - press Esc or click to close", -1, &tr,
              DT_LEFT | DT_SINGLELINE | DT_NOPREFIX);
}

static LRESULT CALLBACK pat_proc(HWND h, UINT m, WPARAM w, LPARAM l)
{
    switch (m) {
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(h, &ps);
        RECT r;
        GetClientRect(h, &r);
        draw_pattern(dc, r.right, r.bottom, g_pat_kind);
        EndPaint(h, &ps);
        return 0;
    }
    case WM_ERASEBKGND: return 1;
    case WM_KEYDOWN:
        if (w == VK_ESCAPE) { DestroyWindow(h); return 0; }
        break;
    case WM_LBUTTONDOWN:
        DestroyWindow(h);
        return 0;
    case WM_TIMER:
        InvalidateRect(h, NULL, FALSE);
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(h, m, w, l);
}

static int create_pat_window(RECT r, int fullscreen)
{
    WNDCLASSEXW wc;
    static int clsDone = 0;
    if (!clsDone) {
        memset(&wc, 0, sizeof wc);
        wc.cbSize = sizeof wc;
        wc.style = CS_HREDRAW | CS_VREDRAW;
        wc.lpfnWndProc = pat_proc;
        wc.hInstance = g_inst;
        wc.hCursor = LoadCursor(NULL, IDC_ARROW);
        wc.lpszClassName = CX_TEST_CLASS;
        RegisterClassExW(&wc);
        clsDone = 1;
    }
    DWORD style = WS_POPUP | (fullscreen ? 0 : WS_THICKFRAME);
    HWND h = CreateWindowExW(WS_EX_TOPMOST, CX_TEST_CLASS, L"ChromaX test",
                             style, r.left, r.top, r.right - r.left, r.bottom - r.top,
                             NULL, NULL, g_inst, NULL);
    if (!h) return 0;
    SetForegroundWindow(h);
    SetFocus(h);
    ShowWindow(h, SW_SHOW);
    return 1;
}

void Diag_RunPattern(int kind, int seconds)
{
    g_pat_kind = kind;
    int nmon = MonCount();

    if (kind == DIAG_PAT_MULTIMON) {
        for (int i = 0; i < nmon; i++) {
            const MonInfo *m = MonGet(i);
            RECT r = { m->x, m->y, m->x + m->w, m->y + m->h };
            create_pat_window(r, 1);
        }
    } else {
        const MonInfo *m = MonGet(0);
        RECT r = { 0, 0, 1920, 1080 };
        if (m) r = (RECT){ m->x, m->y, m->x + m->w, m->y + m->h };
        create_pat_window(r, 1);
    }

    /* modal loop with optional auto-close */
    MSG msg;
    DWORD start = GetTickCount();
    int autoMs = seconds > 0 ? seconds * 1000 : 0;
    int alive = 1;
    while (alive) {
        while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
            if (msg.message == WM_QUIT) alive = 0;
        }
        if (autoMs && (GetTickCount() - start) > (DWORD)autoMs) alive = 0;
        if (alive) Sleep(20);
    }
}
