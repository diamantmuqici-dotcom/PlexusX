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
/* called from GetDeviceGammaRamp with the 0-based display index: lets a test run code in the middle of Eng_Init */
extern void (*g_mock_hook_get_ramp)(int display_index);
#endif
