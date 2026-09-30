/* PlexusX — Elite Windows Gaming Display & Visual Optimization Center
 * Free & Open Source · No Cheats · Legit Display & Magnification APIs
 * Architecture: Win32 C11 · Cross-compiled with Zig/MinGW
 */
#ifndef PLEXUSX_COMMON_H
#define PLEXUSX_COMMON_H

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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <math.h>

#define PX_APP_NAME       L"PlexusX"
#define PX_APP_TITLE      L"PlexusX — Gaming Display Optimizer"
#define PX_VERSION        L"2.0.0"
#define PX_BUILD_DATE     L"2026-09-30"
#define PX_CLASS          L"PlexusXMainWnd"
#define PX_XH_CLASS       L"PlexusXCrosshairWnd"
#define PX_TEST_CLASS     L"PlexusXTestPatternWnd"
#define PX_MUTEX          L"Local\\PlexusX_SingleInstance_v2"
#define PX_PORT           8777

#define PX_WIN_W          1280
#define PX_WIN_H          820
#define PX_SIDE_W         220
#define PX_TOP_H          56

#define WM_APP_LOOK       (WM_APP + 1)   /* remote / phone -> app look */
#define WM_APP_TRAY       (WM_APP + 2)   /* tray icon callback */
#define WM_APP_TOAST      (WM_APP + 3)   /* notification toast */

/* ---------------- Color Engine Parameters ---------------- */
typedef struct Look {
    int   enabled;         /* 1 = active, 0 = bypassed/neutral */
    float sat;             /* 0..300 (%)   100 = neutral, 300 = max application boost */
    float vibrance;        /* 0..300 (%)   100 = neutral, smart saturation */
    float bri;             /* 0..200 (%)   100 = neutral (0.0 to 2.0x) */
    float con;             /* 0..200 (%)   100 = neutral (0.0 to 2.0x) */
    float gamma;           /* 0.50..2.50   1.00 = neutral gamma curve */
    float temp;            /* 3000..10000  (Kelvin) 6500 = standard D65 neutral */
    float tint;            /* -100..100    (%) negative = green, positive = magenta */
    float r_gain;          /* 0..200 (%)   100 = neutral */
    float g_gain;          /* 0..200 (%)   100 = neutral */
    float b_gain;          /* 0..200 (%)   100 = neutral */
    float shadows;         /* 0..200 (%)   100 = neutral (toe shadow lift / crush) */
    float highlights;      /* 0..200 (%)   100 = neutral (shoulder compression / boost) */
    float black_level;     /* 0..200 (%)   100 = neutral (floor level) */
    float white_point;     /* 0..200 (%)   100 = neutral (ceiling level) */
    float clarity;         /* 0..200 (%)   100 = neutral (midtone S-curve dehaze) */
    float hue;             /* -180..180    (deg) 0 = neutral */
} Look;

/* ---------------- Per-Game Profile Structure ---------------- */
#define MAX_SUB_MODES 12

typedef struct SubMode {
    wchar_t name[32];      /* e.g. "Competitive", "Forest", "Night" */
    Look    look;
} SubMode;

typedef struct Profile {
    wchar_t name[48];      /* e.g. "Rust", "CS2", "Fortnite" */
    wchar_t exe[96];       /* e.g. "RustClient.exe" */
    wchar_t tag[32];       /* e.g. "Survival FPS", "Tactical Shooter" */
    int     is_custom;     /* 1 = user created custom game */
    int     favorite;      /* 1 = favorite game */
    int     sub_count;     /* number of sub-modes */
    int     active_sub;    /* currently selected sub-mode index */
    SubMode sub[MAX_SUB_MODES];
    /* Per-game display preference */
    int     target_res_w;  /* 0 = default / don't change */
    int     target_res_h;
    int     target_hz;
    int     hdr_preference;/* 0 = keep, 1 = SDR, 2 = HDR */
    int     auto_apply;    /* 1 = apply on launch */
    int     auto_restore;  /* 1 = restore previous when game exits */
    int     delay_ms;      /* 0..3000 delay before applying profile */
} Profile;

/* ---------------- Preset / Look Definition ---------------- */
typedef struct SceneDef {
    const wchar_t *name;
    const wchar_t *category; /* "Competitive", "Visual", "Environment", "Display" */
    Look look;
} SceneDef;

/* ---------------- Desktop Overlay Crosshair ---------------- */
enum {
    XH_DOT = 0,
    XH_CROSS,
    XH_CIRCLE,
    XH_SQUARE,
    XH_PLUS,
    XH_CHEVRON,
    XH_T,
    XH_TTYPE,
    XH_SHAPE_COUNT
};

typedef struct XhCfg {
    int      on;           /* 0 = hidden, 1 = visible */
    int      shape;        /* XH_* enum */
    int      size;         /* px, 4..64 */
    int      gap;          /* px, 0..32 */
    int      thick;        /* px, 1..12 */
    int      rotation;     /* deg, 0..360 */
    int      opacity;      /* 10..100 (%) */
    int      center_dot;   /* 0 = off, 1 = on */
    int      dot_size;     /* px, 1..8 */
    int      outline;      /* 0 = off, 1 = on */
    int      outline_th;   /* px, 1..4 */
    COLORREF color;        /* crosshair primary color */
    COLORREF ocolor;       /* outline color */
    int      monitor_idx;  /* 0 = primary, 1.. = specific monitor */
} XhCfg;

typedef struct XhPreset {
    const wchar_t *name;
    XhCfg cfg;
} XhPreset;

/* ---------------- Display Mode & Monitor Info ---------------- */
typedef struct ModeInfo {
    int w, h, hz;
    int aspect;            /* 0 = 16:9, 1 = 4:3 stretched, 2 = 16:10, 3 = Ultrawide, 4 = Other */
    int native;
    int supported;
} ModeInfo;

typedef struct MonitorInfo {
    wchar_t dev_name[32];   /* e.g. \\.\DISPLAY1 */
    wchar_t friendly[64];   /* e.g. "ASUS ROG PG279QM" or "Generic PnP Monitor" */
    wchar_t adapter[128];   /* e.g. "NVIDIA GeForce RTX 4080" */
    RECT    rc;
    int     is_primary;
    int     current_w;
    int     current_h;
    int     current_hz;
    int     hdr_enabled;
    int     hdr_capable;
    int     bpc;            /* bits per channel: 8, 10 */
} MonitorInfo;

/* ---------------- GPU & Driver Capability Info ---------------- */
enum {
    GPU_VENDOR_UNKNOWN = 0,
    GPU_VENDOR_NVIDIA,
    GPU_VENDOR_AMD,
    GPU_VENDOR_INTEL
};

typedef struct GpuInfo {
    int     vendor;         /* GPU_VENDOR_* */
    wchar_t vendor_name[32];/* "NVIDIA", "AMD", "Intel", "Generic" */
    wchar_t name[128];      /* e.g. "NVIDIA GeForce RTX 4090" */
    wchar_t driver_ver[64]; /* Driver version */
    int     mag_available;  /* Windows Magnification API supported */
    int     gamma_available;/* GDI SetDeviceGammaRamp supported */
    int     hdr_detected;   /* OS HDR API available */
} GpuInfo;

/* ---------------- UI Navigation Tabs ---------------- */
enum {
    ID_SIDE_BASE = 100,
    ID_SIDE_HOME = 100,
    ID_SIDE_GAMES,
    ID_SIDE_DISPLAY,
    ID_SIDE_COLOR,
    ID_SIDE_PRESETS,
    ID_SIDE_CROSS,
    ID_SIDE_MONITORS,
    ID_SIDE_AUTOMATION,
    ID_SIDE_TOOLS,
    ID_SIDE_SETTINGS,
    ID_SIDE_COUNT = 10
};

/* ---------------- Widget Identifiers ---------------- */
enum {
    ID_NONE = 0,
    /* Caption */
    ID_CAP_MIN = 200,
    ID_CAP_CLOSE,

    /* Home Quick Actions */
    ID_B_HOME_APPLY_GAME = 210,
    ID_B_HOME_RESET_DISP,
    ID_B_HOME_MAX_VIB,
    ID_B_HOME_NIGHT_VIS,
    ID_B_HOME_COMPETITIVE,
    ID_B_HOME_CINEMATIC,
    ID_B_HOME_NATURAL,
    ID_B_HOME_SAVE_PROF,
    ID_B_HOME_PREV_NEXT,
    ID_B_HOME_GAMING_MODE,

    /* Quick Saturation Buttons */
    ID_B_SAT_100 = 230,
    ID_B_SAT_150,
    ID_B_SAT_200,
    ID_B_SAT_250,
    ID_B_SAT_300,

    /* Sliders - Color Engine */
    ID_SL_SAT = 250,
    ID_SL_VIB,
    ID_SL_BRI,
    ID_SL_CON,
    ID_SL_GAMMA,
    ID_SL_TEMP,
    ID_SL_TINT,
    ID_SL_R_GAIN,
    ID_SL_G_GAIN,
    ID_SL_B_GAIN,
    ID_SL_SHADOWS,
    ID_SL_HIGHLIGHTS,
    ID_SL_BLACK_LEVEL,
    ID_SL_WHITE_POINT,
    ID_SL_CLARITY,
    ID_SL_HUE,

    /* Sliders - Crosshair */
    ID_SL_XH_SIZE = 280,
    ID_SL_XH_GAP,
    ID_SL_XH_THICK,
    ID_SL_XH_ROT,
    ID_SL_XH_OPACITY,
    ID_SL_XH_DOTSIZE,

    /* Toggles */
    ID_T_LOOK_ENABLE = 300,
    ID_T_DETECT,
    ID_T_XH,
    ID_T_XH_OUTLINE,
    ID_T_XH_DOT,
    ID_T_STARTWIN,
    ID_T_PHONE,
    ID_T_GAMING_MODE,
    ID_T_AUTO_RESTORE,
    ID_T_REDUCE_MOTION,

    /* Buttons - Actions */
    ID_B_RESET_COLOR = 400,
    ID_B_SAVE_PRESET,
    ID_B_COPY_PRESET,
    ID_B_IMPORT_PRESET,
    ID_B_EXPORT_PRESET,
    ID_B_MODE_APPLY,
    ID_B_MODE_NATIVE,
    ID_B_MODE_MAX_HZ,
    ID_B_MODE_OPENHDR,
    ID_B_BACKUP_NOW,
    ID_B_RESTORE_BACKUP,
    ID_B_RESET_ALL,
    ID_B_DIAG_EXPORT,
    ID_B_DIAG_RUN_TEST,
    ID_B_PHONE_NEW_PIN,
    ID_B_OPEN_SETTINGS_DIR,
    ID_B_IDENTIFY_MONITORS,
    ID_B_CUSTOM_GAME_ADD,
    ID_B_GAME_EDIT,
    ID_B_GAME_FAV_TOGGLE,

    /* Stretched Mode Presets */
    ID_B_MODE_43_COMP,
    ID_B_MODE_43_STRETCH,
    ID_B_MODE_43_CLASSIC,
    ID_B_MODE_1610,
    ID_B_MODE_ULTRAWIDE,

    /* Automation Delays */
    ID_B_DELAY_0,
    ID_B_DELAY_500,
    ID_B_DELAY_1000,
    ID_B_DELAY_2000,

    /* Monitor Targets */
    ID_B_MONITOR_TARGET_ALL,
    ID_B_MONITOR_TARGET_SEL,

    /* Diagnostics Tests */
    ID_B_DIAG_DISP_TEST,
    ID_B_DIAG_COLOR_TEST,
    ID_B_DIAG_HDR_TEST,

    /* Crosshair Shapes */
    ID_XH_SHAPE_BASE = 450,
    ID_XH_PRESET_BASE = 470,     /* 470..485 */

    /* Games Cards & Rows */
    ID_GAME_CARD_BASE = 500,     /* 500..540 */
    ID_GAME_SUB_BASE  = 550,     /* 550..570 sub-mode buttons */

    /* Scenes / Looks Cards */
    ID_LOOK_CARD_BASE = 600,     /* 600..630 */

    /* Modes Rows */
    ID_MODE_ROW_BASE  = 700,     /* 700..730 */

    /* Swatches */
    ID_SWATCH_BASE    = 800,     /* 800..819 crosshair color */
    ID_SWATCH_O_BASE  = 830,     /* 830..849 crosshair outline color */

    /* Test Patterns */
    ID_TEST_PAT_BASE  = 860,     /* 860..875 */

    /* Monitors Selection */
    ID_MONITOR_CARD_BASE = 880   /* 880..890 */
};

/* ---------------- Widget Types ---------------- */
enum {
    WT_LABEL = 0,
    WT_HEAD,
    WT_BTN,
    WT_PRIMARY,
    WT_GHOST,
    WT_ACCENT,
    WT_SLIDER,
    WT_TOGGLE,
    WT_SIDE,
    WT_CARD,
    WT_GAME_CARD,
    WT_LOOK_CARD,
    WT_MONITOR_CARD,
    WT_ROW,
    WT_DIV,
    WT_SWATCH,
    WT_SHAPE,
    WT_SPLIT_PREVIEW,
    WT_CURVE_PREVIEW,
    WT_CHIP,
    WT_QUICK_SAT
};

typedef struct Widget {
    int     type;
    int     id;
    RECT    rc;
    float   vmin, vmax;
    wchar_t text[96];
    wchar_t val[48];
    wchar_t sub[64];
    int     state;      /* toggle active / card selected */
    int     flags;      /* custom data / index */
} Widget;

#define MAX_WIDGETS 320

/* ---------------- Module Interfaces ---------------- */

/* Engine (engine.c) */
void        Eng_Init(void);
void        Eng_Shutdown(void);
void        Eng_SetPaths(const wchar_t *ramps, const wchar_t *dirty, int was_dirty);
void        Eng_Apply(const Look *lk);
void        Eng_Reset(void);
int         Eng_Available(void);
void        Eng_SetTargetMonitor(int idx); /* -1 = all, 0.. = specific display */
int         Eng_GetTargetMonitor(void);
const GpuInfo *Eng_GetGpuInfo(void);
void        Eng_BackupCurrentState(void);
int         Eng_RestoreLastGood(void);
void        Eng_KelvinToRgb(float k, float *r, float *g, float *b);
void        Eng_CalculateGammaRamp(const Look *lk, WORD ramp[3][256]);

/* Profiles & Games (profiles.c) */
int         Prof_Init(void);
int         Prof_Save(void);
int         Prof_Count(void);
Profile    *Prof_Get(int i);
int         Prof_ActiveIndex(void);
void        Prof_SetActiveIndex(int i);
int         Prof_SelectSubMode(int game_idx, int sub_idx);
int         Prof_FindExe(const wchar_t *exe);
void        Prof_Poll(void);
void        Prof_SetDetect(int on);
int         Prof_Detect(void);
int         Prof_AddCustom(const wchar_t *name, const wchar_t *exe, const wchar_t *tag, const Look *lk);
int         Prof_Delete(int i);
int         Prof_ToggleFavorite(int i);
const wchar_t *Prof_CurrentForeground(void);
const SceneDef *Scene_GetList(int *count);
int         Prof_ExportJson(const Profile *p, const wchar_t *filepath);
int         Prof_ImportJson(Profile *out, const wchar_t *filepath);
void        Prof_SetAutoRestore(int on);
int         Prof_GetAutoRestore(void);
void        Prof_SetDelayMs(int ms);
int         Prof_GetDelayMs(void);

/* Display & Modes (modes.c) */
int         Modes_Refresh(void);
int         Modes_Count(void);
ModeInfo   *Modes_Get(int i);
int         Modes_Current(ModeInfo *out);
int         Modes_Apply(int idx);
int         Modes_ApplyMaxHz(void);
int         Modes_ApplyNative(void);
int         Modes_ApplyRes(int target_w, int target_h);
void        Modes_OpenHdrSettings(void);
int         Modes_MonitorCount(void);
MonitorInfo *Modes_GetMonitor(int i);
int         Modes_CurrentMonitorIndex(void);
void        Modes_SetCurrentMonitor(int idx);
void        Modes_IdentifyMonitors(void);

/* Crosshair Overlay (crosshair.c) */
void        Xh_Init(void);
void        Xh_Shutdown(void);
void        Xh_Update(const XhCfg *cfg);
void        Xh_Toggle(void);
int         Xh_IsActive(void);
const XhPreset *Xh_GetPresets(int *count);
void        Xh_ApplyPreset(int idx);

/* Phone / LAN Control (phone.c) */
int         Phone_Start(void);
void        Phone_Stop(void);
void        Phone_SetLook(const Look *lk);
void        Phone_PushProfileChange(const wchar_t *name);
wchar_t    *Phone_SummaryUrl(void);
int         Phone_GetPin(void);
void        Phone_RegeneratePin(void);
int         Phone_GetClientCount(void);
int         Phone_IsRunning(void);

/* Tools & Test Patterns (tools.c) */
void        Tools_Init(void);
void        Tools_Shutdown(void);
void        Tools_LaunchPattern(int pattern_id);
void        Tools_ClosePattern(void);
int         Tools_ExportDiagnostics(const wchar_t *filepath);
int         Tools_BackupDisplayState(const wchar_t *filepath);
int         Tools_RestoreDisplayState(const wchar_t *filepath);
void        Tools_ToggleGamingMode(void);
int         Tools_IsGamingMode(void);

/* UI & Window Rendering (ui.c) */
void        Ui_Init(HWND hwnd, HINSTANCE inst);
void        Ui_Free(void);
void        Ui_Paint(HDC hdc, const RECT *rc);
int         Ui_MouseDown(int x, int y);
int         Ui_MouseMove(int x, int y, int dragging);
void        Ui_MouseUp(int x, int y);
int         Ui_Hover(int x, int y);
int         Ui_Wheel(int x, int y, int delta);
void        Ui_SetPanel(int side_id);
int         Ui_Panel(void);
Look       *Ui_Look(void);
void        Ui_LoadLook(const Look *lk);
void        Ui_Notify(const wchar_t *msg);
void        Ui_RebuildPanel(void);
int         Ui_Exec(int id);
void        Ui_GetXh(XhCfg *out);
void        Ui_SetXh(const XhCfg *in);
int         Ui_CapHit(int x, int y);
int         Ui_InTop(int x, int y);
void        Ui_FilterGames(const wchar_t *filter);
const wchar_t *Ui_GetFilter(void);

/* Main Application Helpers (main.c) */
void        Main_ApplyAll(void);
void        Main_Save(void);
const wchar_t *Main_GetExePath(void);
const wchar_t *Main_GetAppDataPath(void);
extern HWND      g_hwnd;
extern HINSTANCE g_inst;
extern wchar_t   g_appdir[MAX_PATH];
extern wchar_t   g_appdata[MAX_PATH];

static inline int   clampi(int v, int a, int b) { return v < a ? a : (v > b ? b : v); }
static inline float clampf(float v, float a, float b) { return v < a ? a : (v > b ? b : v); }

#endif /* PLEXUSX_COMMON_H */
