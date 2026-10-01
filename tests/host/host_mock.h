#ifndef HOST_MOCK_H
#define HOST_MOCK_H
#include <windows.h>
#include "color_math.h"
#define MOCK_MAX_DISP 8
typedef struct { int present; unsigned short hw[3][256]; int set_calls; int set_ok; } MockDisplay;
extern MockDisplay g_mock_disp[MOCK_MAX_DISP];
extern int g_mock_set_total;
extern int g_mock_mag_calls, g_mock_mag_bad, g_mock_mag_init, g_mock_mag_uninit;
extern MagColorEffect g_mock_mag_last;
extern int g_mock_dirty_creates;
extern int g_mock_mag_set_fail, g_mock_mag_noreadback, g_mock_mag_readback_drift;
extern int g_mock_dm_report_mag, g_mock_dm_report_ramps, g_mock_dm_report_calls;
/* called from GetDeviceGammaRamp with the 0-based display index: lets a test run code in the middle of Eng_Init */
extern void (*g_mock_hook_get_ramp)(int display_index);

/* ---- DisplayState / GameDisplayState facts (display_manager.c owners) ----
 * color_engine.c derives the EFFECTIVE state from these, so the host harness
 * owns their values exactly like it owns the Win32 display stack. */
#include "display_state.h"
#include "game_display_state.h"
typedef struct MockFacts {
    int            monitor_count;
    int            current_monitor;
    int            hdr_any;
    MonitorInfo    mon[MOCK_MAX_DISP];
    PxGameDisplayState game;
} MockFacts;
extern MockFacts g_mock_facts;
void MockFacts_Reset(void);
#endif
