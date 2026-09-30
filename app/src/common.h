/* ChromaX — monitor colour engine for Windows 10/11
 * All features free. Changes what your monitor shows, never touches games.
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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <math.h>

#define CX_APP_NAME   L"ChromaX"
#define CX_VERSION    L"1.0.0"
#define CX_CLASS      L"ChromaXMainWnd"
#define CX_XH_CLASS   L"ChromaXCrosshairWnd"
#define CX_MUTEX      L"Local\\ChromaX_SingleInstance"
#define CX_PORT       8777

#define WM_APP_LOOK   (WM_APP + 1)   /* phone thread -> ui/engine */
#define WM_APP_TRAY   (WM_APP + 2)

/* ---------------- colour look ---------------- */
typedef struct Look {
    float sat;    /* 100..300  (%)  100 = untouched */
    float bri;    /* -100..100 (%) */
    float con;    /* -100..100 (%) */
    float temp;   /* -100..100 (%)  warm / cool */
    float hue;    /* -60..60   (deg) */
    float gamma;  /* 0.40..2.40, 1.00 = untouched (GPU ramp) */
    int   enabled;
} Look;

typedef struct Profile {
    wchar_t name[64];
    wchar_t exe[96];
    Look    look;
} Profile;

typedef struct SceneDef {
    const wchar_t *name;
    Look look;
} SceneDef;

enum {
    ID_NONE = 0,
    /* sidebar */
    ID_SIDE_BASE = 100,           /* 100..105 */
    ID_SIDE_DISPLAY = 100,
    ID_SIDE_GAMES = 101,
    ID_SIDE_SCENES = 102,
    ID_SIDE_CROSS = 103,
    ID_SIDE_MODES = 104,
    ID_SIDE_SET = 105,
    /* sliders */
    ID_SAT = 200,
    ID_BRI,
    ID_CON,
    ID_TEMP,
    ID_HUE,
    ID_GAMMA,
    ID_XH_SIZE,
    ID_XH_GAP,
    ID_XH_THICK,
    ID_XH_DOTOP,
    ID_MODE_SCROLL,
    /* toggles */
    ID_T_ENABLE = 300,
    ID_T_DETECT,
    ID_T_XH,
    ID_T_OUTLINE,
    ID_T_STARTWIN,
    ID_T_PHONE,
    /* buttons */
    ID_B_RESET = 400,
    ID_B_DISABLE,
    ID_XH_CROSS,
    ID_XH_DOT,
    ID_XH_CIRCLE,
    ID_XH_CHEVRON,
    ID_XH_T,
    ID_XH_TTYPE,
    ID_B_MODE_APPLY,
    ID_B_MODE_NATIVE,
    ID_B_OPENHDR,
    ID_B_PROF_ADD,
    ID_B_PROF_DEL,
    ID_B_PROF_APPLY,
    ID_B_PHONEURL,
    ID_B_CB_OFF,
    ID_B_CB_DEUT,
    ID_B_CB_PROT,
    ID_B_CB_TRIT,
    ID_B_CB_GRAY,
    ID_B_OPENFOLDER,
    /* scenes */
    ID_SCENE_BASE = 500,          /* 500..507 */
    /* profiles rows */
    ID_PROF_BASE = 600,           /* 600..611 saved profiles */
    ID_PROF_RUN  = 650,           /* 650..674 running-now rows */
    /* mode rows */
    ID_MODE_BASE = 700,           /* 700..719 */
    /* colour swatches (crosshair) */
    ID_SWATCH_BASE = 800,         /* 800..811 */
    /* caption */
    ID_CAP_MIN = 900,
    ID_CAP_CLOSE
};

/* widget */
enum { WT_LABEL, WT_HEAD, WT_BTN, WT_PRIMARY, WT_GHOST, WT_SLIDER, WT_TOGGLE,
       WT_SIDE, WT_SCENE, WT_ROW, WT_DIV, WT_SWATCH, WT_SHAPE, WT_READOUT,
       WT_CARD_INFO };

typedef struct Widget {
    int   type;
    int   id;
    RECT  rc;
    float vmin, vmax;
    wchar_t text[96];
    wchar_t val[48];
    int   state;      /* toggle on / row selected / shape active */
    int   flags;      /* 1 = primary colour, 2 = dimmed */
} Widget;

#define MAX_WIDGETS 160

/* ---------------- engine (engine.c) ---------------- */
void  Eng_Init(void);
void  Eng_SetPaths(const wchar_t *ramps, const wchar_t *dirty, int was_dirty);
void  Eng_Shutdown(void);
void  Eng_Apply(const Look *lk);      /* matrix + gamma together */
void  Eng_Reset(void);                /* identity matrix, original ramps */
int   Eng_Available(void);            /* magnification API present? */

/* ---------------- crosshair (crosshair.c) ---------------- */
enum { XH_CROSS, XH_DOT, XH_CIRCLE, XH_CHEVRON, XH_T, XH_TTYPE };

typedef struct XhCfg {
    int   on;
    int   shape;
    int   size;      /* px, 6..64 */
    int   gap;       /* px, 0..24 */
    int   thick;     /* px, 1..10 */
    int   dotop;     /* 0..100 */
    int   outline;   /* on/off */
    COLORREF color;
    COLORREF ocolor;
} XhCfg;

void Xh_Init(void);
void Xh_Shutdown(void);
void Xh_Update(const XhCfg *cfg);   /* applies live */
void Xh_Toggle(void);

/* ---------------- profiles & scenes (profiles.c) ---------------- */
int   Prof_Load(void);
int   Prof_Save(void);
int   Prof_Count(void);
Profile *Prof_Get(int i);
int   Prof_Add(const wchar_t *name, const wchar_t *exe, const Look *lk);
void  Prof_Del(int i);
int   Prof_FindExe(const wchar_t *exe);
void  Prof_Poll(void);              /* foreground game -> apply look */
void  Prof_SetDetect(int on);
int   Prof_Detect(void);
const SceneDef *Scene_List(int *count);
int   Prof_RunningScan(void);            /* rebuild running-window list */
int   Prof_RunningCount(void);
const wchar_t *Prof_RunningGet(int i);   /* exe name of running entry */

/* ---------------- display modes (modes.c) ---------------- */
typedef struct ModeInfo {
    int w, h, hz;
    int native;
} ModeInfo;

int  Modes_Refresh(void);
int  Modes_Count(void);
ModeInfo *Modes_Get(int i);
int  Modes_Current(ModeInfo *out);
int  Modes_Apply(int idx);          /* 0 ok */
void Modes_OpenHdr(void);           /* opens Windows display settings */

/* ---------------- phone control (phone.c) ---------------- */
int   Phone_Start(void);
void  Phone_Stop(void);
void  Phone_SetLook(const Look *lk); /* push latest to clients */
wchar_t *Phone_Summary(void);        /* "http://192.168.1.5:8777" */

/* ---------------- colorblind via Windows filter ---------------- */
void  Cb_Apply(int filterType);      /* -1 off, else FilterType */
int   Cb_Active(void);

/* ---------------- ui (ui.c) ---------------- */
void  Ui_Init(HWND hwnd, HINSTANCE inst);
void  Ui_Free(void);
void  Ui_Paint(HDC hdc, const RECT *rc);
int   Ui_MouseDown(int x, int y);
int   Ui_MouseMove(int x, int y, int dragging);
void  Ui_MouseUp(int x, int y);
int   Ui_Hover(int x, int y);        /* returns id under cursor (0 none) */
int   Ui_Wheel(int x, int y, int delta);
void  Ui_SetPanel(int side);
int   Ui_Panel(void);
Look *Ui_Look(void);
void  Ui_LoadLook(const Look *lk);
void  Ui_Title(const wchar_t *t);
void  Ui_Notify(const wchar_t *msg);  /* toast line under title */
void  Ui_SyncProfiles(void);
int   Ui_SelProfile(void);
void  Ui_SelProfileSet(int i);
void  Ui_RebuildPanel(void);
int   Ui_SelMode(void);
int   Ui_Exec(int id);               /* run action for button id */
void  Ui_GetXh(XhCfg *out);
void  Ui_SetXh(const XhCfg *in);
int   Ui_Detect(void);
int   Ui_StartWin(void);
int   Ui_PhoneOn(void);
int   Ui_CapHit(int x, int y);
int   Ui_InTop(int x, int y);
int   Ui_ScaleLogicalW(void);
int   Ui_ScaleLogicalH(void);

/* ---------------- main helpers (main.c) ---------------- */
void  Main_ApplyAll(void);           /* engine + crosshair + phone push */
void  Main_Save(void);
const wchar_t *g_appdir_exe(void);   /* full path to this exe */
extern HWND   g_hwnd;
extern HINSTANCE g_inst;
extern wchar_t g_appdir[MAX_PATH];
extern wchar_t g_appdata[MAX_PATH];

static inline int clampi(int v, int a, int b) { return v < a ? a : (v > b ? b : v); }
static inline float clampf(float v, float a, float b) { return v < a ? a : (v > b ? b : v); }

#endif
