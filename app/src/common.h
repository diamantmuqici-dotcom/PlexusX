/* common.h \u2014 ChromaX v2 shared declarations (Windows layer)
 *
 * ChromaX is a free, open Windows display & gaming-visual control center.
 * It only uses legitimate OS mechanisms:
 *   - magnification.dll colour matrix (the OS layer Magnifier uses)
 *   - SetDeviceGammaRamp per monitor
 *   - EnumDisplaySettings / ChangeDisplaySettingsEx (like Windows Settings)
 *   - DXGI advanced-colour queries for HDR capability/state
 *   - a desktop layered window for the crosshair
 * It never injects into games, reads/writes game memory, edits game files,
 * or touches anti-cheat.
 */
#ifndef CHROMAX_COMMON_H
#define CHROMAX_COMMON_H

#define WIN32_LEAN_AND_MEAN
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif

#include <windows.h>
#include <windowsx.h>
#include <shellapi.h>
#include <commctrl.h>
#include <commdlg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <math.h>

#include "cx_color.h"
#include "cx_games.h"
#include "cx_preset.h"
#include "cx_backup.h"
#include "cx_hotkey.h"
#include "cx_scene.h"
#include "cx_json.h"

#define CX_APP_NAME   L"ChromaX"
#define CX_VERSION    L"2.0.0"
#define CX_CLASS      L"ChromaXMainWnd"
#define CX_XH_CLASS   L"ChromaXCrosshairWnd"
#define CX_TEST_CLASS L"ChromaXTestWnd"
#define CX_MUTEX      L"Local\\ChromaX_SingleInstance"
#define CX_PORT       8777

#define WM_APP_LOOK   (WM_APP + 1)   /* phone thread -> main (single param) */
#define WM_APP_LOOKV  (WM_APP + 2)   /* phone thread -> main (param + value) */
#define WM_APP_PICK   (WM_APP + 3)   /* phone thread -> main (preset/profile by index) */
#define WM_APP_TRAY   (WM_APP + 4)
#define WM_APP_XH     (WM_APP + 5)   /* crosshair toggle from phone */
#define WM_APP_GAME   (WM_APP + 6)   /* gamewatch: foreground game changed */

/* ------------------------------------------------------------------ */
/* crosshair model (extended from v1)                                  */
typedef struct XhCfg {
    int   on;
    int   shape;     /* CXXH_* (9 shapes) */
    int   size;      /* 6..64 px */
    int   gap;       /* 0..24 px */
    int   thick;     /* 1..10 px */
    int   opacity;   /* 0..100 (whole overlay) */
    int   outline;   /* 0/1 */
    int   dot;       /* center dot on top (cross/plus/circle) */
    int   rotation;  /* 0..359 (chevron/T) */
    int   monitor;   /* -1 = primary, else monitor index */
    COLORREF color;
    COLORREF ocolor;
    int   oopacity;  /* outline opacity 5..100 */
    int   follow;    /* 1 = overlay follows the cursor */
} XhCfg;

/* ------------------------------------------------------------------ */
/* monitors (monitors.c)                                               */
typedef struct MonInfo {
    wchar_t device[32];   /* \\.\DISPLAY1 */
    wchar_t id[64];       /* stable EDID identity (vendor/model/serial) */
    wchar_t name[96];     /* human friendly */
    wchar_t manufacturer[64];
    wchar_t model[48];
    wchar_t serial[32];
    int   x, y, w, h;
    int   primary;
    int   dpi;
    int   width_mm, height_mm;
    int   bpc;          /* colour depth */
    int   portrait;
    /* live state */
    int   res_w, res_h, hz;
    int   hdr_capable, hdr_on;
    int   sdr_white;    /* SDR white level (cd/m) if controllable, else 0 */
} MonInfo;

int         MonCount(void);
const MonInfo *MonGet(int i);
int         MonRefresh(void);            /* re-enumerate + update live state */
int         MonFindDevice(const wchar_t *device);
const char *MonGpuName(void);            /* adapter description (static) */
void MonSetHdr(int i, int capable, int on, int sdrWhite);

/* ------------------------------------------------------------------ */
/* display modes (modes.c) \u2014 per monitor                               */
typedef struct ModeInfo {
    int w, h, hz;
    int native;      /* 1 = mode that was current when app started */
    int preferred;   /* 1 = EDID preferred mode */
} ModeInfo;

int  Modes_Refresh(void);
int  Modes_CountFor(int monitor);
ModeInfo *Modes_GetFor(int monitor, int i);
int  Modes_CurrentFor(int monitor, ModeInfo *out);
/* test then apply; 0 = ok. Uses CDS_UPDATEREGISTRY so Windows keeps its
 * "keep these changes?" safety net, and we additionally pre-test. */
int  Modes_Apply(int monitor, int modeIdx);
int  Modes_FindMode(int monitor, int w, int h, int hz); /* hz 0 = any, -1 = max */
int  Modes_ApplySpec(int monitor, int w, int h, int hz); /* hz 0 = keep/max */

/* ------------------------------------------------------------------ */
/* HDR (hdr.c) \u2014 DXGI advanced colour (Win11 22H2+) + graceful fallback */
typedef struct HdrState {
    int supported;    /* API available on this OS */
    int found;        /* an output was found */
    int capable;      /* display reports HDR capable */
    int enabled;      /* HDR currently on */
    int sdr_white_supported;
    float sdr_white;
} HdrState;

void Hdr_Scan(HdrState *out);                    /* all monitors, first capable */
void Hdr_ScanFor(int monitor, HdrState *out);
int  Hdr_SetEnabled(int monitor, int on);        /* monitor -1 = primary; 0 ok */
int  Hdr_SetSdrWhite(float nits);                /* 0 ok */
void Hdr_OpenSettings(void);                     /* ms-settings:display */

/* ------------------------------------------------------------------ */
/* engine (engine.c) \u2014 matrix stage + per-monitor curve stage          */
void  Eng_Init(void);
void  Eng_Shutdown(void);
int   Eng_MagAvailable(void);
int   Eng_RampAvailable(void);
void  Eng_ApplyLook(const CxLook *look, int monitor, int allMonitors);
void  Eng_ResetAll(void);
/* last-applied look (crash recovery "last known good") */
void  Eng_SaveLast(const CxLook *look);
int   Eng_LoadLast(CxLook *out);          /* 1 if a non-neutral look was stored */

/* ------------------------------------------------------------------ */
/* game watch (gamewatch.c) \u2014 event-driven foreground detection        */
void  Gw_Init(void);
void  Gw_Shutdown(void);
const wchar_t *Gw_ForegroundExe(void);   /* base name or L"" */
void  Gw_SetAutoApply(int on);
int   Gw_AutoApply(void);
void  Gw_SetRestoreOnExit(int on);
int   Gw_RestoreOnExit(void);
void  Gw_SetDelayMs(int ms);
int   Gw_DelayMs(void);
int   Gw_Enabled(void);                   /* win-event hook alive? */
int   Gw_Process(void);                   /* main-thread; 1 = state changed */
void  Gw_SetGameLook(int gameIdx, int lookIdx);
int   Gw_GameLook(int gameIdx);
int   Gw_CurrentGame(void);               /* game idx with look applied, -1 */

/* ------------------------------------------------------------------ */
/* crosshair (crosshair.c)                                             */
void  Xh_Init(void);
void  Xh_Shutdown(void);
void  Xh_Update(const XhCfg *cfg);
void  Xh_Toggle(void);

/* ------------------------------------------------------------------ */
/* phone control (phone.c)                                             */
#define PH_LOOK   1
#define PH_PRESET 2
#define PH_GAME   3
#define PH_XH     4
#define PH_HDR    5
#define PH_RESET  6

typedef struct PhoneAct {
    int action;
    CxLook look;
    XhCfg xh;
    char  name[160];
    int   a, b;
} PhoneAct;

int   Phone_Start(void);
void  Phone_Stop(void);
int   Phone_Running(void);
wchar_t *Phone_Summary(void);             /* "http://ip:8777" */
const wchar_t *Phone_Token(void);
int   Phone_DeviceCount(void);
int   Phone_DeviceIp(int i, wchar_t *out, int sz);
int   Phone_Pending(PhoneAct *out);       /* UI thread: dequeue one action */
void  Phone_ProcessPending(void);         /* main.c: apply queued actions */

/* ------------------------------------------------------------------ */
/* diagnostics (diag.c)                                                */
int   Diag_Export(const wchar_t *path);   /* 0 ok */
void  Diag_RunPattern(int kind, int seconds);
enum { DIAG_PAT_DISPLAY = 0, DIAG_PAT_COLOR, DIAG_PAT_HDR, DIAG_PAT_MULTIMON };

/* ------------------------------------------------------------------ */
/* preview (preview.c) \u2014 generated scenes + colour preview             */
int   Prev_Init(int w, int h);
void  Prev_Shutdown(void);
void  Prev_Scene(int scene);              /* regenerate base scene */
/* render preview into a 32bpp DIB: 0=before, 1=after, 2=split at pos 0..1 */
int   Prev_RenderHBITMAP(int mode, float splitPos, HBITMAP *out);
int   Prev_W(void);
int   Prev_H(void);

/* ------------------------------------------------------------------ */
/* app glue (main.c)                                                   */
void  Main_ApplyAll(void);                 /* engine + crosshair + phone */
void  Main_Save(void);
void  Main_Backup(const wchar_t *source);  /* snapshot state now */
int   Main_RestoreBackup(void);        /* 1 = restored, 0 = none */
void  Main_ResetAllChanges(void);
void  Main_Notify(const wchar_t *msg);     /* toast + balloon */
void  Main_Log(const wchar_t *level, const wchar_t *msg);
void  Main_OpenDataFolder(void);
void  Main_OpenLogs(void);
void  Main_ClearLogs(void);
int   Main_GamingMode(void);
void  Main_SetGamingMode(int on);
void  Main_AppliedHistoryAdd(const wchar_t *source, const CxLook *look, int monitor);
int   Main_AppliedHistoryCount(void);
int   Main_AppliedHistoryGet(int i, CxLook *look, wchar_t *source, int ssz,
                             wchar_t *ts, int tsz, int *monitor);
int   Main_FavoriteCount(void);
int   Main_FavoriteAt(int i);              /* game index */
int   Main_IsFavorite(int gameIdx);
void  Main_ToggleFavorite(int gameIdx);
void  Main_ApplyGameLook(int gameIdx, int lookIdx, int silent);
wchar_t *Main_Utf8ToUtf16Alloc(const char *s);
char    *Main_Utf16ToUtf8Alloc(const wchar_t *s);

/* custom games (user-defined, main.c) */
int          Main_CustomCount(void);
const char  *Main_CustomName(int i);
const char  *Main_CustomExe(int i);
int          Main_CustomGetLook(int i, CxLook *out);
int          Main_CustomAdd(const char *name, const char *exe,
                            const CxLook *look);   /* 0 = ok */
void         Main_CustomRemove(int i);
int          Main_CustomMatch(const wchar_t *exe, int *outIdx); /* 1 = match */

/* user-saved crosshair presets (main.c) */
int          Main_XhPresetCount(void);
const wchar_t *Main_XhPresetName(int i);
int          Main_XhPresetSave(const wchar_t *name);  /* 0 = ok */
int          Main_XhPresetLoad(int i, XhCfg *out);    /* 0 = ok */
void         Main_XhPresetRemove(int i);

/* tray context menu (main.c) */
void         Main_TrayMenu(void);

extern HWND      g_hwnd;
extern HINSTANCE g_inst;
extern wchar_t   g_appdir[MAX_PATH];
extern wchar_t   g_appdata[MAX_PATH];
extern CxLook    g_look;
extern XhCfg     g_xh;
extern int       g_gaming;

typedef struct Settings {
    int autoApply, restoreOnExit, delayMs;
    int startWin, minTray, trayNotify;
    int highContrast;
} Settings;
extern Settings  g_settings;

/* hotkey management (main.c) */
void  Main_HotkeysInit(void);
void  Main_HotkeysFree(void);
const wchar_t *Main_HotkeyText(int i);   /* formatted combo or L"(off)" */
void  Main_HotkeyAction(int i);          /* run hotkey i's action */
int   Main_HotkeySet(int i, const char *combo); /* 0 ok (conflicts -> -1) */

/* gaming mode / settings helpers (main.c) */
void  Main_ToggleGaming(void);
void  Main_ApplyStartWithWin(int on);
void  Main_ResetSettings(void);
void  Main_TrayBlink(void);
void  Main_ShutdownTray(void);

void  UI_ApplyLook(const wchar_t *source);
void  UI_SetHighContrast(int on);           /* settings > appearance */
void  UI_Toast(const wchar_t *s);
void  UI_Refresh(void);
void  UI_Init(HINSTANCE inst);

/* widget helpers */
static inline int  clampi(int v, int a, int b) { return v < a ? a : (v > b ? b : v); }
static inline float clampf(float v, float a, float b) { return v < a ? a : (v > b ? b : v); }

#endif
