/* PlexusX — GameDetector implementation: foreground process + presentation. */
#include "common.h"
#include "game_detector.h"

int PxDetect_PresentationFor(HWND wnd)
{
    if (!wnd) return PX_PRES_NONE;

    RECT wr;
    if (!GetWindowRect(wnd, &wr)) return PX_PRES_WINDOWED;

    HMONITOR hm = MonitorFromWindow(wnd, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi;
    memset(&mi, 0, sizeof mi);
    mi.cbSize = sizeof mi;
    if (!GetMonitorInfoW(hm, &mi)) return PX_PRES_WINDOWED;

    /* Full monitor coverage, 2px tolerance for odd borders / rounding. */
    int covers = (wr.left  <= mi.rcMonitor.left + 2 &&
                  wr.top   <= mi.rcMonitor.top + 2 &&
                  wr.right >= mi.rcMonitor.right - 2 &&
                  wr.bottom >= mi.rcMonitor.bottom - 2);

    LONG_PTR st = GetWindowLongPtrW(wnd, GWL_STYLE);
    int has_frame = (st & (WS_CAPTION | WS_THICKFRAME | WS_BORDER | WS_DLGFRAME)) != 0;

    return px_pres_classify(has_frame, covers);
}

void PxDetect_Scan(PxDetectedForeground *out)
{
    memset(out, 0, sizeof *out);
    out->presentation = PX_PRES_NONE;

    HWND fg = GetForegroundWindow();
    if (!fg) return;
    out->window_key = (unsigned long long)(ULONG_PTR)fg;
    out->our_window = (fg == g_hwnd) ? 1 : 0;

    DWORD pid = 0;
    GetWindowThreadProcessId(fg, &pid);
    if (!pid) return;
    out->pid = (unsigned long)pid;

    HANDLE hp = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!hp) return;                      /* access denied: resolved stays 0  */

    wchar_t path[MAX_PATH * 2];
    DWORD len = (DWORD)(sizeof path / sizeof path[0]);
    BOOL ok = QueryFullProcessImageNameW(hp, 0, path, &len);
    CloseHandle(hp);
    if (!ok) return;

    /* normalize: lowercase base name (same rules the pure machine expects) */
    px_game_base_name(path, out->exe, sizeof out->exe);
    out->resolved = 1;
    out->presentation = PxDetect_PresentationFor(fg);
}
