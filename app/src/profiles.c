/* PlexusX — Per-Game Profiles, Scenes Library, and Foreground Detection
 * Cheating-Free: Zero game memory access, zero DLL injection.
 */
#include "common.h"
#include <tlhelp32.h>

#define MAX_PROFILES 32

static Profile g_profiles[MAX_PROFILES];
static int     g_nprofiles = 0;
static int     g_active_profile = 0;
static int     g_detect = 1;
static int     g_auto_restore = 1;
static int     g_delay_ms = 0;
static wchar_t g_last_exe[96] = { 0 };
static wchar_t g_current_fg[96] = { 0 };

static Look    g_saved_pre_game_look;
static int     g_has_pre_game_look = 0;

/* ---------------- Preset Scenes / Looks ---------------- */
static const SceneDef g_scenes[] = {
    { L"Competitive",    L"Competitive", { 1, 185, 160, 106, 114, 0.95f, 6500,  0, 100, 100, 100, 120,  95, 100, 100, 115,  0 } },
    { L"Natural Neutral",L"Visual",      { 1, 100, 100, 100, 100, 1.00f, 6500,  0, 100, 100, 100, 100, 100, 100, 100, 100,  0 } },
    { L"Vibrant Boost",  L"Visual",      { 1, 190, 175, 104, 108, 1.00f, 6500,  0, 100, 100, 100, 105, 100, 100, 100, 108,  0 } },
    { L"Ultra 300%",     L"Visual",      { 1, 300, 240, 105, 112, 1.00f, 6500,  0, 100, 100, 100, 110, 100, 100, 100, 115,  0 } },
    { L"Cinematic Warm", L"Visual",      { 1, 135, 120, 102, 110, 1.04f, 5800,  4, 105, 100,  94,  95, 105, 100, 100, 105,  0 } },
    { L"Cool Clarity",   L"Visual",      { 1, 140, 130, 104, 108, 0.98f, 7500, -2,  96, 100, 108, 110,  95, 100, 100, 112,  0 } },
    { L"High Contrast",  L"Visual",      { 1, 165, 140, 105, 125, 0.95f, 6500,  0, 100, 100, 100, 115,  90, 100, 100, 125,  0 } },
    { L"Night Ops",      L"Environment", { 1, 235, 190, 122, 110, 0.78f, 6200,  0, 102, 100,  98, 155,  85, 108, 100, 120,  0 } },
    { L"Dark Room Lift", L"Environment", { 1, 220, 180, 125, 108, 0.75f, 6400,  0, 100, 100, 100, 160,  88, 110, 100, 115,  0 } },
    { L"Treeline Forest",L"Environment", { 1, 250, 210, 105, 114, 0.96f, 6300, -6,  96, 110,  95, 125,  95, 100, 100, 120,  0 } },
    { L"Snow Glare",     L"Environment", { 1, 180, 150,  92, 118, 1.05f, 7200,  0,  96,  98, 106,  90,  80,  96,  95, 120,  0 } },
    { L"Desert Dunes",   L"Environment", { 1, 210, 180, 104, 110, 0.98f, 5600,  2, 110, 100,  90, 110,  95, 100, 100, 110,  0 } },
    { L"Smoke & Dust",   L"Environment", { 1, 270, 230, 108, 126, 0.90f, 6200,  0, 104, 100,  95, 140,  90, 104, 100, 130,  0 } },
    { L"Bright Sky",     L"Environment", { 1, 190, 160,  96, 112, 1.02f, 6800, -4,  98,  98, 105, 100,  85,  98, 100, 110,  0 } },
    { L"OLED Punch",     L"Display",     { 1, 175, 150, 100, 112, 1.00f, 6500,  0, 100, 100, 100, 100, 100, 100, 100, 110,  0 } },
    { L"IPS Neutralizer",L"Display",     { 1, 150, 140, 102, 108, 0.98f, 6500,  0, 100, 100, 100, 110,  98, 102, 100, 105,  0 } },
    { L"VA Shadow Lift", L"Display",     { 1, 160, 150, 105, 106, 0.92f, 6500,  0, 100, 100, 100, 135, 100, 105, 100, 110,  0 } }
};

const SceneDef *Scene_GetList(int *count)
{
    *count = (int)(sizeof g_scenes / sizeof g_scenes[0]);
    return g_scenes;
}

/* ---------------- Default Games Library ---------------- */
static void init_default_profiles(void)
{
    g_nprofiles = 0;

    /* 1. RUST */
    {
        Profile *p = &g_profiles[g_nprofiles++];
        memset(p, 0, sizeof *p);
        lstrcpyW(p->name, L"Rust");
        lstrcpyW(p->exe,  L"RustClient.exe");
        lstrcpyW(p->tag,  L"Survival FPS");
        p->favorite = 1;
        p->auto_apply = 1;
        p->auto_restore = 1;
        p->sub_count = 11;

        lstrcpyW(p->sub[0].name, L"Competitive");
        p->sub[0].look = (Look){ 1, 245, 200, 106, 116, 0.94f, 6400, 0, 102, 102, 98, 135, 95, 102, 100, 120, 0 };

        lstrcpyW(p->sub[1].name, L"Forest");
        p->sub[1].look = (Look){ 1, 260, 220, 105, 115, 0.96f, 6200, -6, 96, 112, 94, 130, 95, 100, 100, 125, 0 };

        lstrcpyW(p->sub[2].name, L"Night");
        p->sub[2].look = (Look){ 1, 230, 190, 125, 112, 0.76f, 6200, 0, 104, 100, 96, 165, 85, 110, 100, 120, 0 };

        lstrcpyW(p->sub[3].name, L"Snow");
        p->sub[3].look = (Look){ 1, 185, 150, 92, 118, 1.05f, 7200, 0, 96, 98, 106, 90, 80, 96, 95, 120, 0 };

        lstrcpyW(p->sub[4].name, L"Desert");
        p->sub[4].look = (Look){ 1, 215, 185, 104, 110, 0.98f, 5600, 2, 110, 100, 90, 115, 95, 100, 100, 110, 0 };

        lstrcpyW(p->sub[5].name, L"Daylight");
        p->sub[5].look = (Look){ 1, 220, 180, 104, 112, 0.98f, 6500, 0, 100, 100, 100, 115, 95, 100, 100, 110, 0 };

        lstrcpyW(p->sub[6].name, L"Dark Room");
        p->sub[6].look = (Look){ 1, 225, 185, 122, 108, 0.74f, 6400, 0, 100, 100, 100, 160, 88, 110, 100, 115, 0 };

        lstrcpyW(p->sub[7].name, L"Bright");
        p->sub[7].look = (Look){ 1, 200, 170, 112, 110, 0.95f, 6500, 0, 100, 100, 100, 120, 90, 102, 100, 110, 0 };

        lstrcpyW(p->sub[8].name, L"Cinematic");
        p->sub[8].look = (Look){ 1, 140, 125, 102, 110, 1.04f, 5800, 4, 105, 100, 94, 95, 105, 100, 100, 105, 0 };

        lstrcpyW(p->sub[9].name, L"Natural");
        p->sub[9].look = (Look){ 1, 100, 100, 100, 100, 1.00f, 6500, 0, 100, 100, 100, 100, 100, 100, 100, 100, 0 };

        lstrcpyW(p->sub[10].name, L"High Visibility");
        p->sub[10].look = (Look){ 1, 275, 230, 115, 120, 0.88f, 6500, 0, 100, 100, 100, 145, 90, 105, 100, 130, 0 };
    }

    /* 2. CS2 */
    {
        Profile *p = &g_profiles[g_nprofiles++];
        memset(p, 0, sizeof *p);
        lstrcpyW(p->name, L"Counter-Strike 2");
        lstrcpyW(p->exe,  L"cs2.exe");
        lstrcpyW(p->tag,  L"Tactical Shooter");
        p->favorite = 1;
        p->auto_apply = 1;
        p->auto_restore = 1;
        p->sub_count = 6;

        lstrcpyW(p->sub[0].name, L"Competitive");
        p->sub[0].look = (Look){ 1, 235, 195, 105, 118, 0.96f, 6500, 0, 100, 100, 100, 125, 95, 100, 100, 118, 0 };

        lstrcpyW(p->sub[1].name, L"Bright");
        p->sub[1].look = (Look){ 1, 210, 175, 112, 114, 0.92f, 6600, 0, 100, 100, 100, 130, 90, 102, 100, 112, 0 };

        lstrcpyW(p->sub[2].name, L"Natural");
        p->sub[2].look = (Look){ 1, 100, 100, 100, 100, 1.00f, 6500, 0, 100, 100, 100, 100, 100, 100, 100, 100, 0 };

        lstrcpyW(p->sub[3].name, L"Cinematic");
        p->sub[3].look = (Look){ 1, 145, 130, 102, 112, 1.02f, 6100, 2, 104, 100, 96, 95, 102, 100, 100, 105, 0 };

        lstrcpyW(p->sub[4].name, L"Low-Light");
        p->sub[4].look = (Look){ 1, 250, 210, 120, 116, 0.82f, 6400, 0, 102, 100, 98, 150, 90, 105, 100, 122, 0 };

        lstrcpyW(p->sub[5].name, L"Default");
        p->sub[5].look = (Look){ 1, 150, 140, 100, 100, 1.00f, 6500, 0, 100, 100, 100, 100, 100, 100, 100, 100, 0 };
    }

    /* 3. FORTNITE */
    {
        Profile *p = &g_profiles[g_nprofiles++];
        memset(p, 0, sizeof *p);
        lstrcpyW(p->name, L"Fortnite");
        lstrcpyW(p->exe,  L"FortniteClient-Win64-Shipping.exe");
        lstrcpyW(p->tag,  L"Battle Royale");
        p->favorite = 1;
        p->auto_apply = 1;
        p->auto_restore = 1;
        p->sub_count = 6;

        lstrcpyW(p->sub[0].name, L"Competitive");
        p->sub[0].look = (Look){ 1, 205, 175, 104, 110, 0.96f, 6500, 0, 100, 100, 100, 118, 95, 100, 100, 112, 0 };

        lstrcpyW(p->sub[1].name, L"Colorful");
        p->sub[1].look = (Look){ 1, 260, 220, 106, 114, 1.00f, 6400, 0, 102, 100, 98, 115, 100, 100, 100, 115, 0 };

        lstrcpyW(p->sub[2].name, L"Bright");
        p->sub[2].look = (Look){ 1, 190, 160, 112, 108, 0.94f, 6500, 0, 100, 100, 100, 125, 90, 102, 100, 108, 0 };

        lstrcpyW(p->sub[3].name, L"Natural");
        p->sub[3].look = (Look){ 1, 100, 100, 100, 100, 1.00f, 6500, 0, 100, 100, 100, 100, 100, 100, 100, 100, 0 };

        lstrcpyW(p->sub[4].name, L"Cinematic");
        p->sub[4].look = (Look){ 1, 140, 125, 102, 110, 1.04f, 5900, 4, 105, 100, 94, 95, 105, 100, 100, 105, 0 };

        lstrcpyW(p->sub[5].name, L"Default");
        p->sub[5].look = (Look){ 1, 150, 130, 100, 100, 1.00f, 6500, 0, 100, 100, 100, 100, 100, 100, 100, 100, 0 };
    }

    /* 4. VALORANT */
    {
        Profile *p = &g_profiles[g_nprofiles++];
        memset(p, 0, sizeof *p);
        lstrcpyW(p->name, L"Valorant");
        lstrcpyW(p->exe,  L"VALORANT-Win64-Shipping.exe");
        lstrcpyW(p->tag,  L"Tactical Shooter");
        p->favorite = 1;
        p->auto_apply = 1;
        p->auto_restore = 1;
        p->sub_count = 4;

        lstrcpyW(p->sub[0].name, L"Competitive");
        p->sub[0].look = (Look){ 1, 240, 200, 104, 116, 0.95f, 6600, 0, 100, 100, 102, 120, 95, 100, 100, 116, 0 };

        lstrcpyW(p->sub[1].name, L"Natural");
        p->sub[1].look = (Look){ 1, 100, 100, 100, 100, 1.00f, 6500, 0, 100, 100, 100, 100, 100, 100, 100, 100, 0 };

        lstrcpyW(p->sub[2].name, L"High Contrast");
        p->sub[2].look = (Look){ 1, 220, 180, 105, 124, 0.92f, 6500, 0, 100, 100, 100, 130, 90, 100, 100, 122, 0 };

        lstrcpyW(p->sub[3].name, L"Default");
        p->sub[3].look = (Look){ 1, 160, 140, 100, 100, 1.00f, 6500, 0, 100, 100, 100, 100, 100, 100, 100, 100, 0 };
    }

    /* 5. ESCAPE FROM TARKOV */
    {
        Profile *p = &g_profiles[g_nprofiles++];
        memset(p, 0, sizeof *p);
        lstrcpyW(p->name, L"Escape From Tarkov");
        lstrcpyW(p->exe,  L"EscapeFromTarkov.exe");
        lstrcpyW(p->tag,  L"Hardcore Extraction");
        p->favorite = 1;
        p->auto_apply = 1;
        p->auto_restore = 1;
        p->sub_count = 5;

        lstrcpyW(p->sub[0].name, L"Dark Room");
        p->sub[0].look = (Look){ 1, 265, 215, 126, 114, 0.76f, 6300, -2, 102, 100, 98, 165, 85, 110, 100, 125, 0 };

        lstrcpyW(p->sub[1].name, L"Outdoor");
        p->sub[1].look = (Look){ 1, 235, 195, 106, 116, 0.94f, 6400, 0, 100, 102, 98, 125, 95, 100, 100, 118, 0 };

        lstrcpyW(p->sub[2].name, L"Natural");
        p->sub[2].look = (Look){ 1, 100, 100, 100, 100, 1.00f, 6500, 0, 100, 100, 100, 100, 100, 100, 100, 100, 0 };

        lstrcpyW(p->sub[3].name, L"High Visibility");
        p->sub[3].look = (Look){ 1, 280, 235, 120, 122, 0.82f, 6400, 0, 102, 100, 98, 155, 88, 108, 100, 130, 0 };

        lstrcpyW(p->sub[4].name, L"Night");
        p->sub[4].look = (Look){ 1, 240, 200, 128, 110, 0.72f, 6100, 0, 104, 100, 96, 170, 80, 112, 100, 120, 0 };
    }

    /* 6. PUBG */
    {
        Profile *p = &g_profiles[g_nprofiles++];
        memset(p, 0, sizeof *p);
        lstrcpyW(p->name, L"PUBG: BATTLEGROUNDS");
        lstrcpyW(p->exe,  L"TslGame.exe");
        lstrcpyW(p->tag,  L"Battle Royale");
        p->auto_apply = 1;
        p->auto_restore = 1;
        p->sub_count = 4;

        lstrcpyW(p->sub[0].name, L"Competitive");
        p->sub[0].look = (Look){ 1, 240, 200, 105, 116, 0.94f, 6500, 0, 100, 100, 100, 125, 95, 100, 100, 118, 0 };

        lstrcpyW(p->sub[1].name, L"Sunny");
        p->sub[1].look = (Look){ 1, 215, 180, 104, 110, 0.98f, 6000, 2, 106, 100, 94, 115, 95, 100, 100, 112, 0 };

        lstrcpyW(p->sub[2].name, L"Natural");
        p->sub[2].look = (Look){ 1, 100, 100, 100, 100, 1.00f, 6500, 0, 100, 100, 100, 100, 100, 100, 100, 100, 0 };

        lstrcpyW(p->sub[3].name, L"High Visibility");
        p->sub[3].look = (Look){ 1, 270, 225, 114, 120, 0.88f, 6500, 0, 100, 100, 100, 140, 90, 104, 100, 125, 0 };
    }

    /* 7. APEX LEGENDS */
    {
        Profile *p = &g_profiles[g_nprofiles++];
        memset(p, 0, sizeof *p);
        lstrcpyW(p->name, L"Apex Legends");
        lstrcpyW(p->exe,  L"r5apex.exe");
        lstrcpyW(p->tag,  L"Movement Shooter");
        p->auto_apply = 1;
        p->auto_restore = 1;
        p->sub_count = 3;

        lstrcpyW(p->sub[0].name, L"Competitive");
        p->sub[0].look = (Look){ 1, 225, 190, 104, 114, 0.96f, 6500, 0, 100, 100, 100, 120, 95, 100, 100, 115, 0 };

        lstrcpyW(p->sub[1].name, L"Vibrant");
        p->sub[1].look = (Look){ 1, 260, 220, 106, 112, 1.00f, 6400, 0, 102, 100, 98, 115, 100, 100, 100, 115, 0 };

        lstrcpyW(p->sub[2].name, L"Natural");
        p->sub[2].look = (Look){ 1, 100, 100, 100, 100, 1.00f, 6500, 0, 100, 100, 100, 100, 100, 100, 100, 100, 0 };
    }

    /* 8. CALL OF DUTY / WARZONE */
    {
        Profile *p = &g_profiles[g_nprofiles++];
        memset(p, 0, sizeof *p);
        lstrcpyW(p->name, L"Call of Duty: Warzone");
        lstrcpyW(p->exe,  L"cod.exe");
        lstrcpyW(p->tag,  L"FPS / Battle Royale");
        p->auto_apply = 1;
        p->auto_restore = 1;
        p->sub_count = 3;

        lstrcpyW(p->sub[0].name, L"Competitive");
        p->sub[0].look = (Look){ 1, 230, 190, 108, 118, 0.92f, 6500, 0, 100, 100, 100, 135, 92, 104, 100, 122, 0 };

        lstrcpyW(p->sub[1].name, L"Natural");
        p->sub[1].look = (Look){ 1, 100, 100, 100, 100, 1.00f, 6500, 0, 100, 100, 100, 100, 100, 100, 100, 100, 0 };

        lstrcpyW(p->sub[2].name, L"Gulag Visibility");
        p->sub[2].look = (Look){ 1, 255, 210, 122, 116, 0.78f, 6300, 0, 102, 100, 98, 160, 85, 110, 100, 126, 0 };
    }

    /* 9. OVERWATCH 2 */
    {
        Profile *p = &g_profiles[g_nprofiles++];
        memset(p, 0, sizeof *p);
        lstrcpyW(p->name, L"Overwatch 2");
        lstrcpyW(p->exe,  L"Overwatch.exe");
        lstrcpyW(p->tag,  L"Hero Shooter");
        p->auto_apply = 1;
        p->auto_restore = 1;
        p->sub_count = 3;

        lstrcpyW(p->sub[0].name, L"Competitive");
        p->sub[0].look = (Look){ 1, 220, 185, 104, 112, 0.98f, 6500, 0, 100, 100, 100, 115, 95, 100, 100, 112, 0 };

        lstrcpyW(p->sub[1].name, L"Vibrant");
        p->sub[1].look = (Look){ 1, 250, 215, 105, 110, 1.00f, 6400, 0, 102, 100, 98, 110, 100, 100, 100, 110, 0 };

        lstrcpyW(p->sub[2].name, L"Natural");
        p->sub[2].look = (Look){ 1, 100, 100, 100, 100, 1.00f, 6500, 0, 100, 100, 100, 100, 100, 100, 100, 100, 0 };
    }

    /* 10. RAINBOW SIX SIEGE */
    {
        Profile *p = &g_profiles[g_nprofiles++];
        memset(p, 0, sizeof *p);
        lstrcpyW(p->name, L"Rainbow Six Siege");
        lstrcpyW(p->exe,  L"RainbowSix.exe");
        lstrcpyW(p->tag,  L"Tactical CQB");
        p->auto_apply = 1;
        p->auto_restore = 1;
        p->sub_count = 3;

        lstrcpyW(p->sub[0].name, L"Competitive");
        p->sub[0].look = (Look){ 1, 225, 190, 110, 118, 0.90f, 6500, 0, 100, 100, 100, 135, 92, 105, 100, 122, 0 };

        lstrcpyW(p->sub[1].name, L"Dark Angle Boost");
        p->sub[1].look = (Look){ 1, 240, 205, 122, 114, 0.80f, 6400, 0, 102, 100, 98, 155, 88, 110, 100, 124, 0 };

        lstrcpyW(p->sub[2].name, L"Natural");
        p->sub[2].look = (Look){ 1, 100, 100, 100, 100, 1.00f, 6500, 0, 100, 100, 100, 100, 100, 100, 100, 100, 0 };
    }

    /* 11. MINECRAFT */
    {
        Profile *p = &g_profiles[g_nprofiles++];
        memset(p, 0, sizeof *p);
        lstrcpyW(p->name, L"Minecraft");
        lstrcpyW(p->exe,  L"javaw.exe");
        lstrcpyW(p->tag,  L"Sandbox / Adventure");
        p->auto_apply = 1;
        p->auto_restore = 1;
        p->sub_count = 3;

        lstrcpyW(p->sub[0].name, L"Vibrant");
        p->sub[0].look = (Look){ 1, 240, 200, 105, 110, 1.00f, 6500, 0, 100, 100, 100, 115, 100, 100, 100, 110, 0 };

        lstrcpyW(p->sub[1].name, L"Cave Explorer");
        p->sub[1].look = (Look){ 1, 210, 180, 128, 114, 0.72f, 6300, 0, 102, 100, 98, 170, 85, 112, 100, 125, 0 };

        lstrcpyW(p->sub[2].name, L"Natural");
        p->sub[2].look = (Look){ 1, 100, 100, 100, 100, 1.00f, 6500, 0, 100, 100, 100, 100, 100, 100, 100, 100, 0 };
    }

    /* 12. GTA V & GTA VI Placeholder */
    {
        Profile *p = &g_profiles[g_nprofiles++];
        memset(p, 0, sizeof *p);
        lstrcpyW(p->name, L"Grand Theft Auto V");
        lstrcpyW(p->exe,  L"GTA5.exe");
        lstrcpyW(p->tag,  L"Open World Action");
        p->auto_apply = 1;
        p->auto_restore = 1;
        p->sub_count = 3;

        lstrcpyW(p->sub[0].name, L"Natural");
        p->sub[0].look = (Look){ 1, 130, 120, 100, 106, 1.00f, 6500, 0, 100, 100, 100, 105, 100, 100, 100, 104, 0 };

        lstrcpyW(p->sub[1].name, L"Cinematic");
        p->sub[1].look = (Look){ 1, 150, 135, 102, 114, 1.04f, 5700, 4, 106, 100, 92, 95, 104, 100, 100, 108, 0 };

        lstrcpyW(p->sub[2].name, L"Sunset Neon");
        p->sub[2].look = (Look){ 1, 220, 190, 105, 115, 0.98f, 5400, 6, 110, 98, 92, 110, 100, 100, 100, 115, 0 };
    }

    /* 13. THE FINALS */
    {
        Profile *p = &g_profiles[g_nprofiles++];
        memset(p, 0, sizeof *p);
        lstrcpyW(p->name, L"THE FINALS");
        lstrcpyW(p->exe,  L"Discovery.exe");
        lstrcpyW(p->tag,  L"Arena Destruction");
        p->auto_apply = 1;
        p->auto_restore = 1;
        p->sub_count = 2;

        lstrcpyW(p->sub[0].name, L"Vibrant");
        p->sub[0].look = (Look){ 1, 250, 215, 105, 114, 0.98f, 6500, 0, 100, 100, 100, 120, 95, 100, 100, 115, 0 };

        lstrcpyW(p->sub[1].name, L"Competitive");
        p->sub[1].look = (Look){ 1, 220, 185, 106, 118, 0.92f, 6500, 0, 100, 100, 100, 130, 92, 102, 100, 120, 0 };
    }

    /* 14. DAYZ */
    {
        Profile *p = &g_profiles[g_nprofiles++];
        memset(p, 0, sizeof *p);
        lstrcpyW(p->name, L"DayZ");
        lstrcpyW(p->exe,  L"DayZ_x64.exe");
        lstrcpyW(p->tag,  L"Survival Hardcore");
        p->auto_apply = 1;
        p->auto_restore = 1;
        p->sub_count = 3;

        lstrcpyW(p->sub[0].name, L"Forest");
        p->sub[0].look = (Look){ 1, 250, 210, 105, 115, 0.96f, 6200, -6, 96, 112, 94, 130, 95, 100, 100, 125, 0 };

        lstrcpyW(p->sub[1].name, L"Night");
        p->sub[1].look = (Look){ 1, 235, 195, 126, 112, 0.74f, 6200, 0, 104, 100, 96, 168, 82, 112, 100, 122, 0 };

        lstrcpyW(p->sub[2].name, L"Natural");
        p->sub[2].look = (Look){ 1, 100, 100, 100, 100, 1.00f, 6500, 0, 100, 100, 100, 100, 100, 100, 100, 100, 0 };
    }

    /* 15. HELLDIVERS 2 */
    {
        Profile *p = &g_profiles[g_nprofiles++];
        memset(p, 0, sizeof *p);
        lstrcpyW(p->name, L"Helldivers 2");
        lstrcpyW(p->exe,  L"helldivers2.exe");
        lstrcpyW(p->tag,  L"Co-op Shooter");
        p->auto_apply = 1;
        p->auto_restore = 1;
        p->sub_count = 3;

        lstrcpyW(p->sub[0].name, L"Bug Planet Fog");
        p->sub[0].look = (Look){ 1, 260, 220, 108, 124, 0.90f, 6400, 0, 100, 100, 100, 140, 90, 104, 100, 130, 0 };

        lstrcpyW(p->sub[1].name, L"Cinematic");
        p->sub[1].look = (Look){ 1, 145, 130, 102, 112, 1.02f, 6000, 2, 104, 100, 96, 95, 102, 100, 100, 106, 0 };

        lstrcpyW(p->sub[2].name, L"Night Visibility");
        p->sub[2].look = (Look){ 1, 230, 190, 122, 112, 0.78f, 6300, 0, 102, 100, 98, 155, 88, 108, 100, 120, 0 };
    }

    /* 16. ARC RAIDERS */
    {
        Profile *p = &g_profiles[g_nprofiles++];
        memset(p, 0, sizeof *p);
        lstrcpyW(p->name, L"ARC Raiders");
        lstrcpyW(p->exe,  L"ArcRaiders.exe");
        lstrcpyW(p->tag,  L"Extraction Shooter");
        p->auto_apply = 1;
        p->auto_restore = 1;
        p->sub_count = 2;

        lstrcpyW(p->sub[0].name, L"Desert Scavenger");
        p->sub[0].look = (Look){ 1, 220, 185, 104, 112, 0.96f, 5800, 2, 108, 100, 92, 120, 95, 100, 100, 115, 0 };

        lstrcpyW(p->sub[1].name, L"Cinematic");
        p->sub[1].look = (Look){ 1, 140, 125, 102, 110, 1.02f, 6200, 0, 100, 100, 100, 100, 100, 100, 100, 105, 0 };
    }

    /* 17. BATTLEFIELD */
    {
        Profile *p = &g_profiles[g_nprofiles++];
        memset(p, 0, sizeof *p);
        lstrcpyW(p->name, L"Battlefield");
        lstrcpyW(p->exe,  L"BF2042.exe");
        lstrcpyW(p->tag,  L"Combined Arms");
        p->auto_apply = 1;
        p->auto_restore = 1;
        p->sub_count = 2;

        lstrcpyW(p->sub[0].name, L"Ground War");
        p->sub[0].look = (Look){ 1, 230, 195, 105, 116, 0.94f, 6500, 0, 100, 100, 100, 125, 95, 100, 100, 118, 0 };

        lstrcpyW(p->sub[1].name, L"High Visibility");
        p->sub[1].look = (Look){ 1, 260, 220, 112, 122, 0.88f, 6500, 0, 100, 100, 100, 140, 90, 104, 100, 125, 0 };
    }

    /* 18. DESTINY 2 */
    {
        Profile *p = &g_profiles[g_nprofiles++];
        memset(p, 0, sizeof *p);
        lstrcpyW(p->name, L"Destiny 2");
        lstrcpyW(p->exe,  L"destiny2.exe");
        lstrcpyW(p->tag,  L"Sci-Fi MMO / FPS");
        p->auto_apply = 1;
        p->auto_restore = 1;
        p->sub_count = 2;

        lstrcpyW(p->sub[0].name, L"Cosmos Vibrant");
        p->sub[0].look = (Look){ 1, 240, 205, 104, 114, 0.98f, 6600, 0, 100, 100, 102, 115, 98, 100, 100, 115, 0 };

        lstrcpyW(p->sub[1].name, L"Raid Visibility");
        p->sub[1].look = (Look){ 1, 220, 185, 112, 116, 0.90f, 6500, 0, 100, 100, 100, 135, 92, 104, 100, 120, 0 };
    }
}

/* ---------------- Persistence INI ---------------- */
static const wchar_t *cfg_file(void)
{
    static wchar_t p[MAX_PATH];
    if (!p[0]) wsprintfW(p, L"%s\\profiles.ini", g_appdata);
    return p;
}

int Prof_Init(void)
{
    init_default_profiles();
    const wchar_t *f = cfg_file();
    int saved_count = GetPrivateProfileIntW(L"general", L"count", -1, f);
    if (saved_count > 0) {
        g_detect = GetPrivateProfileIntW(L"general", L"detect", 1, f);
        g_auto_restore = GetPrivateProfileIntW(L"general", L"auto_restore", 1, f);
        g_delay_ms = GetPrivateProfileIntW(L"general", L"delay_ms", 0, f);
    }
    return g_nprofiles;
}

int Prof_Save(void)
{
    const wchar_t *f = cfg_file();
    wchar_t b[32];
    wsprintfW(b, L"%d", g_nprofiles);   WritePrivateProfileStringW(L"general", L"count", b, f);
    wsprintfW(b, L"%d", g_detect);      WritePrivateProfileStringW(L"general", L"detect", b, f);
    wsprintfW(b, L"%d", g_auto_restore);WritePrivateProfileStringW(L"general", L"auto_restore", b, f);
    wsprintfW(b, L"%d", g_delay_ms);    WritePrivateProfileStringW(L"general", L"delay_ms", b, f);
    return 0;
}

int Prof_Count(void) { return g_nprofiles; }
Profile *Prof_Get(int i) { return (i >= 0 && i < g_nprofiles) ? &g_profiles[i] : NULL; }
int Prof_ActiveIndex(void) { return g_active_profile; }
void Prof_SetActiveIndex(int i) { if (i >= 0 && i < g_nprofiles) g_active_profile = i; }

int Prof_SelectSubMode(int game_idx, int sub_idx)
{
    if (game_idx < 0 || game_idx >= g_nprofiles) return -1;
    Profile *p = &g_profiles[game_idx];
    if (sub_idx < 0 || sub_idx >= p->sub_count) return -1;
    p->active_sub = sub_idx;
    Ui_LoadLook(&p->sub[sub_idx].look);
    Main_ApplyAll();
    return 0;
}

int Prof_FindExe(const wchar_t *exe)
{
    if (!exe || !exe[0]) return -1;
    for (int i = 0; i < g_nprofiles; i++) {
        if (g_profiles[i].exe[0] && _wcsicmp(g_profiles[i].exe, exe) == 0)
            return i;
    }
    return -1;
}

int Prof_AddCustom(const wchar_t *name, const wchar_t *exe, const wchar_t *tag, const Look *lk)
{
    if (g_nprofiles >= MAX_PROFILES) return -1;
    Profile *p = &g_profiles[g_nprofiles];
    memset(p, 0, sizeof *p);
    lstrcpynW(p->name, name, 48);
    lstrcpynW(p->exe, exe, 96);
    lstrcpynW(p->tag, tag ? tag : L"Custom Game", 32);
    p->is_custom = 1;
    p->sub_count = 1;
    lstrcpyW(p->sub[0].name, L"Custom");
    p->sub[0].look = *lk;
    p->auto_apply = 1;
    p->auto_restore = 1;
    g_nprofiles++;
    Prof_Save();
    return g_nprofiles - 1;
}

int Prof_Delete(int i)
{
    if (i < 0 || i >= g_nprofiles) return -1;
    if (!g_profiles[i].is_custom) return -1; /* Don't delete built-in */
    for (int j = i; j < g_nprofiles - 1; j++) g_profiles[j] = g_profiles[j + 1];
    g_nprofiles--;
    Prof_Save();
    return 0;
}

int Prof_ToggleFavorite(int i)
{
    if (i >= 0 && i < g_nprofiles) {
        g_profiles[i].favorite = !g_profiles[i].favorite;
        Prof_Save();
        return g_profiles[i].favorite;
    }
    return 0;
}

const wchar_t *Prof_CurrentForeground(void)
{
    return g_current_fg;
}

void Prof_SetDetect(int on)      { g_detect = on ? 1 : 0; }
int  Prof_Detect(void)           { return g_detect; }
void Prof_SetAutoRestore(int on) { g_auto_restore = on ? 1 : 0; }
int  Prof_GetAutoRestore(void)   { return g_auto_restore; }
void Prof_SetDelayMs(int ms)     { g_delay_ms = clampi(ms, 0, 3000); }
int  Prof_GetDelayMs(void)       { return g_delay_ms; }

/* ---------------- Safe Foreground Polling ---------------- */
void Prof_Poll(void)
{
    if (!g_detect) return;
    HWND fg = GetForegroundWindow();
    if (!fg) return;

    DWORD pid = 0;
    GetWindowThreadProcessId(fg, &pid);
    if (!pid || pid == GetCurrentProcessId()) return;

    HANDLE hp = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!hp) return;

    wchar_t path[MAX_PATH * 2];
    DWORD len = MAX_PATH * 2;
    BOOL ok = QueryFullProcessImageNameW(hp, 0, path, &len);
    CloseHandle(hp);
    if (!ok) return;

    const wchar_t *base = path;
    for (const wchar_t *p = path; *p; p++) {
        if (*p == L'\\') base = p + 1;
    }

    lstrcpynW(g_current_fg, base, 96);
    if (_wcsicmp(base, g_last_exe) == 0) return;

    int old_idx = Prof_FindExe(g_last_exe);
    int new_idx = Prof_FindExe(base);

    lstrcpynW(g_last_exe, base, 96);

    /* Game launched / focused */
    if (new_idx >= 0) {
        if (!g_has_pre_game_look) {
            g_saved_pre_game_look = *Ui_Look();
            g_has_pre_game_look = 1;
        }

        Profile *p = &g_profiles[new_idx];
        if (p->auto_apply) {
            if (g_delay_ms > 0) Sleep(g_delay_ms);
            g_active_profile = new_idx;
            Ui_LoadLook(&p->sub[p->active_sub].look);
            wchar_t msg[128];
            wsprintfW(msg, L"%s: %s profile applied", p->name, p->sub[p->active_sub].name);
            Ui_Notify(msg);
            Eng_Resync();           /* a game taking the screen may have reset the LUT */
            Main_ApplyAll();
            InvalidateRect(g_hwnd, NULL, FALSE);
        }
    } else if (old_idx >= 0 && g_auto_restore && g_has_pre_game_look) {
        /* Game exited: restore display */
        Ui_LoadLook(&g_saved_pre_game_look);
        g_has_pre_game_look = 0;
        Ui_Notify(L"Desktop display profile restored");
        Eng_Resync();               /* the game's exit may have reset the LUT */
        Main_ApplyAll();
        InvalidateRect(g_hwnd, NULL, FALSE);
    }
}

/* ---------------- JSON Profile Schema Export & Import ---------------- */
int Prof_ExportJson(const Profile *p, const wchar_t *filepath)
{
    if (!p || !filepath) return -1;
    FILE *f = _wfopen(filepath, L"w, ccs=UTF-8");
    if (!f) return -1;

    const Look *lk = &p->sub[p->active_sub].look;
    fwprintf(f, L"{\n");
    fwprintf(f, L"  \"schema\": \"PlexusX/v2\",\n");
    fwprintf(f, L"  \"name\": \"%ls\",\n", p->name);
    fwprintf(f, L"  \"exe\": \"%ls\",\n", p->exe);
    fwprintf(f, L"  \"tag\": \"%ls\",\n", p->tag);
    fwprintf(f, L"  \"sub_mode\": \"%ls\",\n", p->sub[p->active_sub].name);
    fwprintf(f, L"  \"color\": {\n");
    fwprintf(f, L"    \"enabled\": %d,\n", lk->enabled);
    fwprintf(f, L"    \"sat\": %.1f,\n", lk->sat);
    fwprintf(f, L"    \"vibrance\": %.1f,\n", lk->vibrance);
    fwprintf(f, L"    \"bri\": %.1f,\n", lk->bri);
    fwprintf(f, L"    \"con\": %.1f,\n", lk->con);
    fwprintf(f, L"    \"gamma\": %.2f,\n", lk->gamma);
    fwprintf(f, L"    \"temp\": %.0f,\n", lk->temp);
    fwprintf(f, L"    \"tint\": %.1f,\n", lk->tint);
    fwprintf(f, L"    \"r_gain\": %.1f,\n", lk->r_gain);
    fwprintf(f, L"    \"g_gain\": %.1f,\n", lk->g_gain);
    fwprintf(f, L"    \"b_gain\": %.1f,\n", lk->b_gain);
    fwprintf(f, L"    \"shadows\": %.1f,\n", lk->shadows);
    fwprintf(f, L"    \"highlights\": %.1f,\n", lk->highlights);
    fwprintf(f, L"    \"black_level\": %.1f,\n", lk->black_level);
    fwprintf(f, L"    \"white_point\": %.1f,\n", lk->white_point);
    fwprintf(f, L"    \"clarity\": %.1f\n", lk->clarity);
    fwprintf(f, L"  }\n");
    fwprintf(f, L"}\n");

    fclose(f);
    return 0;
}

int Prof_ImportJson(Profile *out, const wchar_t *filepath)
{
    if (!out || !filepath) return -1;
    FILE *f = _wfopen(filepath, L"r");
    if (!f) return -1;

    char buf[2048];
    size_t rd = fread(buf, 1, sizeof buf - 1, f);
    fclose(f);
    if (!rd) return -1;
    buf[rd] = 0;

    memset(out, 0, sizeof *out);
    out->is_custom = 1;
    out->sub_count = 1;
    lstrcpyW(out->name, L"Imported Profile");
    lstrcpyW(out->sub[0].name, L"Custom");

    Look *lk = &out->sub[0].look;
    lk->enabled = 1;
    lk->sat = 150; lk->vibrance = 120; lk->bri = 100; lk->con = 100;
    lk->gamma = 1.0f; lk->temp = 6500; lk->r_gain = 100; lk->g_gain = 100; lk->b_gain = 100;
    lk->shadows = 100; lk->highlights = 100; lk->black_level = 100; lk->white_point = 100;
    lk->clarity = 100;

    char *p = strstr(buf, "\"name\":");
    if (p) {
        char val[64] = { 0 };
        sscanf(p, "\"name\": \"%63[^\"]\"", val);
        MultiByteToWideChar(CP_UTF8, 0, val, -1, out->name, 48);
    }
    p = strstr(buf, "\"sat\":");
    if (p) { float s; if (sscanf(p, "\"sat\": %f", &s) == 1) lk->sat = clampf(s, 0, 300); }
    p = strstr(buf, "\"vibrance\":");
    if (p) { float v; if (sscanf(p, "\"vibrance\": %f", &v) == 1) lk->vibrance = clampf(v, 0, 300); }
    p = strstr(buf, "\"gamma\":");
    if (p) { float g; if (sscanf(p, "\"gamma\": %f", &g) == 1) lk->gamma = clampf(g, 0.40f, 2.50f); }
    p = strstr(buf, "\"temp\":");
    if (p) { float t; if (sscanf(p, "\"temp\": %f", &t) == 1) lk->temp = clampf(t, 3000, 10000); }

    return 0;
}
