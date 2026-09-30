/* cx_games.c \u2014 built-in catalog data
 *
 * All values are *recommended starting points* for display/colour tuning \u2014
 * never claims about in-game visibility or "best settings".
 */
#include "cx_games.h"
#include <string.h>
#ifdef _WIN32
#include <strings.h>
#else
#include <strings.h>
#define _stricmp strcasecmp
#define _strnicmp strncasecmp
#endif

#define LK(_sat, _bri, _con, _gam, _tem) {                                       \
    .sat = (_sat), .vibrance = 100, .brightness = (_bri), .contrast = (_con),    \
    .gamma = (_gam), .temperature = (_tem), .tint = 0, .red = 100, .green = 100, \
    .blue = 100, .shadows = 100, .highlights = 100, .blacklevel = 100,           \
    .whitepoint = 100, .sharpness = 100, .clarity = 100, .intensity = 100,       \
    .hue = 0, .dehaze = 100, .enabled = 1 }

#define LKF(_sat, _vib, _bri, _con, _gam, _tem, _tint, _sh, _hi, _bl, _wp, _deh) { \
    .sat = (_sat), .vibrance = (_vib), .brightness = (_bri), .contrast = (_con),   \
    .gamma = (_gam), .temperature = (_tem), .tint = (_tint), .red = 100,           \
    .green = 100, .blue = 100, .shadows = (_sh), .highlights = (_hi),              \
    .blacklevel = (_bl), .whitepoint = (_wp), .sharpness = 100, .clarity = 100,    \
    .intensity = 100, .hue = 0, .dehaze = (_deh), .enabled = 1 }

/* ================= games ================= */

static const CxGameLook k_rust[] = {
    { "Rust Default",        LK(150, 104, 106, 1.00, 6500) },
    { "Rust Competitive",    LKF(135, 110, 112, 118, 0.96, 6300, 0, 116, 96, 100, 100, 106) },
    { "Rust Daylight",       LK(160,  96, 110, 1.04, 5800) },
    { "Rust Night",          LKF(185, 110, 128, 112, 0.85, 6200, 0, 150, 88,  94, 100, 104) },
    { "Rust Forest",         LKF(175, 112, 106, 108, 0.98, 6000, 0, 120, 100, 100, 100, 108) },
    { "Rust Dark Room",      LKF(190, 110, 132, 110, 0.82, 6400, 0, 165, 90,  90, 100, 102) },
    { "Rust Snow",           LKF(140, 104,  94, 118, 1.02, 7200, 0, 100, 96, 100,  96, 104) },
    { "Rust Desert",         LK(165,  98, 112, 1.05, 5400) },
    { "Rust Bright",         LK(155,  88, 115, 1.06, 5600) },
    { "Rust Cinematic",      LKF(125, 100, 102, 122, 1.12, 5900, 0, 100, 92, 100, 100, 100) },
    { "Rust Natural",        LK(115, 102, 102, 1.00, 6500) },
    { "Rust High Visibility",LKF(195, 115, 118, 120, 0.90, 6300, 0, 140, 96,  96, 100, 118) },
};

static const CxGameLook k_cs2[] = {
    { "CS2 Default",     LK(130, 104, 108, 1.00, 6500) },
    { "CS2 Competitive", LKF(125, 105, 108, 116, 0.97, 6400, 0, 112, 98, 100, 100, 108) },
    { "CS2 Bright",      LK(130,  94, 112, 1.05, 6000) },
    { "CS2 Natural",     LK(112, 102, 102, 1.00, 6500) },
    { "CS2 Cinematic",   LK(122, 104, 118, 1.10, 5800) },
    { "CS2 Low-Light",   LKF(150, 110, 124, 108, 0.84, 6200, 0, 145, 92,  92, 100, 104) },
};

static const CxGameLook k_fortnite[] = {
    { "Fortnite Default",    LK(155, 104, 105, 1.00, 6500) },
    { "Fortnite Competitive",LKF(140, 110, 108, 114, 0.98, 6300, 0, 114, 98, 100, 100, 106) },
    { "Fortnite Bright",     LK(150,  94, 110, 1.03, 6000) },
    { "Fortnite Natural",    LK(125, 102, 102, 1.00, 6500) },
    { "Fortnite Colorful",   LKF(230, 160, 106, 108, 1.00, 6400, 0, 102, 100, 100, 100, 100) },
    { "Fortnite Cinematic",  LK(165, 100, 116, 1.08, 5600) },
};

static const CxGameLook k_valorant[] = {
    { "Valorant Competitive", LKF(128, 108, 110, 118, 0.95, 6300, 0, 114, 96, 100, 100, 108) },
    { "Valorant Natural",     LK(115, 102, 103, 1.00, 6500) },
    { "Valorant Bright",      LK(130,  95, 112, 1.04, 6000) },
    { "Valorant Dark",        LKF(155, 110, 122, 110, 0.86, 6200, 0, 138, 94,  96, 100, 102) },
};

static const CxGameLook k_tarkov[] = {
    { "Tarkov Default",   LK(150, 106, 108, 1.00, 6500) },
    { "Tarkov Competitive", LKF(145, 110, 112, 114, 0.97, 6300, 0, 118, 96, 100, 100, 108) },
    { "Tarkov Dark Room", LKF(175, 112, 130, 108, 0.82, 6300, 0, 160, 92,  92, 100, 104) },
    { "Tarkov Outdoor",   LKF(160, 112,  98, 112, 1.03, 5900, 0, 108, 100, 100, 100, 112) },
    { "Tarkov Natural",   LK(120, 102, 102, 1.00, 6500) },
    { "Tarkov Night",     LKF(170, 110, 134, 106, 0.80, 6100, 0, 170, 88,  90, 100, 106) },
};

static const CxGameLook k_pubg[] = {
    { "PUBG Competitive", LKF(140, 110, 112, 116, 0.96, 6200, 0, 120, 96, 100, 100, 110) },
    { "PUBG Natural",     LK(120, 102, 102, 1.00, 6500) },
    { "PUBG Bright",      LK(135,  94, 112, 1.04, 6000) },
};

static const CxGameLook k_apex[] = {
    { "Apex Competitive", LKF(145, 110, 108, 114, 0.97, 6300, 0, 116, 98, 100, 100, 106) },
    { "Apex Vibrant",     LKF(200, 150, 104, 108, 1.00, 6400, 0, 102, 100, 100, 100, 100) },
    { "Apex Natural",     LK(120, 102, 102, 1.00, 6500) },
};

static const CxGameLook k_cod[] = {
    { "COD Competitive", LKF(138, 110, 110, 116, 0.96, 6300, 0, 118, 96, 100, 100, 108) },
    { "COD Natural",     LK(120, 102, 103, 1.00, 6500) },
    { "COD Bright",      LK(140,  94, 112, 1.03, 6000) },
};

static const CxGameLook k_ow2[] = {
    { "OW2 Competitive", LKF(150, 112, 108, 110, 0.98, 6400, 0, 112, 98, 100, 100, 104) },
    { "OW2 Natural",     LK(125, 102, 102, 1.00, 6500) },
};

static const CxGameLook k_r6[] = {
    { "R6 Competitive", LKF(148, 110, 114, 116, 0.94, 6200, 0, 122, 94,  98, 100, 110) },
    { "R6 Natural",     LK(122, 102, 102, 1.00, 6500) },
};

static const CxGameLook k_minecraft[] = {
    { "Minecraft Vibrant", LKF(210, 155, 104, 108, 1.00, 6200, 0, 104, 100, 100, 100, 100) },
    { "Minecraft Natural", LK(130, 102, 102, 1.00, 6500) },
};

static const CxGameLook k_gta5[] = {
    { "GTA V Natural",   LK(135, 102, 105, 1.00, 6000) },
    { "GTA V Cinematic", LK(128, 100, 118, 1.10, 5500) },
};

static const CxGameLook k_gta6[] = {
    { "GTA VI Cinematic", LK(130, 100, 116, 1.08, 5400) },
    { "GTA VI Natural",   LK(130, 102, 104, 1.00, 6100) },
};

static const CxGameLook k_finals[] = {
    { "The Finals Vibrant",    LKF(190, 145, 106, 110, 1.00, 6300, 0, 104, 100, 100, 100, 100) },
    { "The Finals Competitive",LKF(150, 110, 108, 114, 0.97, 6300, 0, 114, 96, 100, 100, 106) },
};

static const CxGameLook k_dayz[] = {
    { "DayZ Natural",  LK(128, 104, 104, 1.00, 6500) },
    { "DayZ Forest",   LKF(150, 112, 106, 110, 0.99, 6000, 0, 114, 100, 100, 100, 106) },
    { "DayZ Night",    LKF(165, 110, 132, 106, 0.82, 6100, 0, 160, 90,  92, 100, 104) },
};

static const CxGameLook k_hd2[] = {
    { "Helldivers 2 Vibrant",    LKF(200, 150, 104, 110, 1.00, 6300, 0, 104, 100, 100, 100, 100) },
    { "Helldivers 2 Competitive",LKF(160, 112, 108, 112, 0.97, 6400, 0, 115, 96, 100, 100, 104) },
    { "Helldivers 2 Natural",    LK(125, 102, 103, 1.00, 6500) },
};

static const CxGameLook k_arc[] = {
    { "ARC Raiders Competitive", LKF(155, 112, 110, 114, 0.96, 6200, 0, 118, 96, 100, 100, 110) },
    { "ARC Raiders Natural",     LK(122, 102, 102, 1.00, 6500) },
};

static const CxGameLook k_bf[] = {
    { "Battlefield Competitive", LKF(145, 110, 110, 116, 0.96, 6300, 0, 118, 96, 100, 100, 108) },
    { "Battlefield Natural",     LK(122, 102, 102, 1.00, 6500) },
};

static const CxGameLook k_destiny[] = {
    { "Destiny 2 Natural", LK(130, 102, 104, 1.00, 6400) },
    { "Destiny 2 Vibrant", LKF(185, 140, 104, 108, 1.00, 6300, 0, 104, 100, 100, 100, 100) },
};

static const CxGame k_games[CXGAME_N] = {
    { CXGAME_RUST, "Rust", "Survival", "rustclient.exe", NULL, "rust",
      "Long-range forest visibility and night interiors", 12, k_rust, 2 },
    { CXGAME_CS2, "Counter-Strike 2", "FPS", "cs2.exe", NULL, "cs cs2 counter strike counter-strike",
      "Bright, stable contrast for consistent aim", 6, k_cs2, 3 },
    { CXGAME_FORTNITE, "Fortnite", "Battle Royale", "fortniteclient-win64-shipping.exe", NULL, "fort fortnite",
      "Punchy building and drop colours", 6, k_fortnite, 4 },
    { CXGAME_VALORANT, "Valorant", "FPS", "valorant-win64-shipping.exe", "valorant.exe", "val valorant",
      "Clean shapes and readable smoke", 4, k_valorant, 1 },
    { CXGAME_TARKOV, "Escape from Tarkov", "FPS / Extraction", "escapefromtarkov.exe", NULL, "tarkov escape",
      "Dark-room lifting and outdoor haze", 6, k_tarkov, 0 },
    { CXGAME_PUBG, "PUBG", "Battle Royale", "tslgame.exe", "pubg.exe", "pubg",
      "Open-field visibility at long range", 3, k_pubg, 2 },
    { CXGAME_APEX, "Apex Legends", "Battle Royale", "apex.exe", NULL, "apex",
      "Fast movement clarity and vibrance", 3, k_apex, 1 },
    { CXGAME_COD, "Call of Duty / Warzone", "FPS", "callofduty.exe", "warzone.exe", "cod warzone call of duty call-of-duty",
      "Stable contrast for fast maps", 3, k_cod, 1 },
    { CXGAME_OW2, "Overwatch 2", "Hero Shooter", "ow2.exe", "overwatch.exe", "ow overwatch",
      "Skill-shot clarity, team colours", 2, k_ow2, 4 },
    { CXGAME_R6, "Rainbow Six Siege", "Tactical FPS", "rainbowsix.exe", NULL, "r6 siege rainbow six rainbow-six",
      "Defuse-map visibility, smoke reading", 2, k_r6, 0 },
    { CXGAME_MINECRAFT, "Minecraft", "Sandbox", "minecraft.exe", "minecraftwin64.exe", "minecraft mc",
      "Block clarity and biome colours", 2, k_minecraft, 5 },
    { CXGAME_GTA5, "GTA V", "Open World", "gta5.exe", NULL, "gta gta5 gta v",
      "City daylight and cinematic grading", 2, k_gta5, 3 },
    { CXGAME_GTA6, "GTA VI", "Open World", "gta6.exe", NULL, "gta6 gta vi",
      "Profile-ready \u2014 profile loads when the game ships", 2, k_gta6, 3 },
    { CXGAME_FINALS, "The Finals", "FPS", "thefinals.exe", NULL, "finals",
      "Destructible-arena vibrance", 2, k_finals, 4 },
    { CXGAME_DAYZ, "DayZ", "Survival", "dayz.exe", NULL, "dayz",
      "Forest and night endurance tuning", 3, k_dayz, 0 },
    { CXGAME_HELLDIVERS2, "Helldivers 2", "Shooter", "helldivers2.exe", NULL, "helldivers hd2",
      "Battlefield glow and clarity", 3, k_hd2, 2 },
    { CXGAME_ARCRAIDERS, "ARC Raiders", "Extraction", "arcraiders.exe", "arcreivers.exe", "arc raiders arc-raiders",
      "Long-range engagement haze", 2, k_arc, 3 },
    { CXGAME_BF, "Battlefield", "FPS", "bf2042.exe", "bfv.exe", "battlefield bf",
      "Vehicle and smoke readability", 2, k_bf, 1 },
    { CXGAME_DESTINY2, "Destiny 2", "Looter Shooter", "destiny2.exe", NULL, "destiny",
      "Directional lighting and exotic glow", 2, k_destiny, 4 },
};

int CxGames_Count(void) { return CXGAME_N; }
const CxGame *CxGames_Get(int i)
{
    return (i >= 0 && i < CXGAME_N) ? &k_games[i] : NULL;
}

static int ieq(const char *a, const char *b)
{
    return a && b && _stricmp(a, b) == 0;
}
static int istartswith_i(const char *a, const char *b)
{
    return a && b && _strnicmp(a, b, strlen(b)) == 0;
}

const CxGame *CxGames_FindExe(const char *exeBase)
{
    if (!exeBase || !exeBase[0]) return NULL;
    for (int i = 0; i < CXGAME_N; i++) {
        if (ieq(k_games[i].exe, exeBase) ||
            (k_games[i].exe_alt && ieq(k_games[i].exe_alt, exeBase)))
            return &k_games[i];
    }
    return NULL;
}

static int alias_match(const char *aliases, const char *query)
{
    if (!aliases) return 0;
    const char *p = aliases;
    while (*p) {
        while (*p == ' ') p++;
        const char *start = p;
        while (*p && *p != ' ') p++;
        int len = (int)(p - start);
        if (len == 0) continue;
        char tok[64];
        int n = len < 63 ? len : 63;
        memcpy(tok, start, (size_t)n);
        tok[n] = 0;
        if (_stricmp(tok, query) == 0 || istartswith_i(tok, query) ||
            strstr(tok, query) || strstr(query, tok))
            return 1;
    }
    return 0;
}

int CxGames_Search(const char *query, int out[], int max)
{
    int n = 0;
    if (!query || !query[0] || max <= 0) return 0;
    for (int i = 0; i < CXGAME_N && n < max; i++) {
        if (_stricmp(k_games[i].name, query) == 0 ||
            _stricmp(k_games[i].category, query) == 0 ||
            istartswith_i(k_games[i].name, query) ||
            strstr(k_games[i].name, query) ||
            alias_match(k_games[i].aliases, query)) {
            out[n++] = i;
        }
    }
    return n;
}

/* ================= global looks ================= */

static const CxLookDef k_looks[] = {
    { "Competitive",   "Competitive", LKF(140, 108, 110, 116, 0.97, 6300, 0, 116, 96, 100, 100, 108) },
    { "Natural",       "Natural",     LK(115, 102, 102, 1.00, 6500) },
    { "Vibrant",       "Vibrant",     LKF(210, 150, 104, 106, 1.00, 6400, 0, 104, 100, 100, 100, 100) },
    { "Ultra Vibrant", "Vibrant",     LKF(280, 220, 106, 108, 1.00, 6400, 0, 106, 100, 100, 100, 100) },
    { "Cinematic",     "Cinematic",   LK(125, 100, 118, 1.10, 5700) },
    { "Warm",          "Warm",        LK(135, 104, 105, 1.00, 4800) },
    { "Cool",          "Cool",        LK(130, 102, 106, 1.00, 8200) },
    { "High Contrast", "Contrast",    LKF(145, 104, 102, 145, 1.05, 6500, 0, 100, 94, 100, 100, 104) },
    { "Soft",          "Soft",        LK(105, 104,  92, 1.04, 6600) },
    { "Night",         "Night",       LKF(170, 110, 128, 108, 0.84, 6200, 0, 155, 90,  94, 100, 104) },
    { "Bright",        "Bright",      LK(150,  90, 112, 1.05, 6000) },
    { "Forest",        "Scenes",      LKF(170, 112, 105, 108, 0.99, 6000, 0, 115, 100, 100, 100, 108) },
    { "Snow",          "Scenes",      LKF(135, 102,  94, 116, 1.01, 7400, 0, 100, 96, 100,  96, 102) },
    { "Desert",        "Scenes",      LK(165,  98, 110, 1.04, 5200) },
    { "Dark",          "Dark",        LKF(160, 110, 124, 108, 0.86, 6300, 0, 145, 92,  95, 100, 100) },
    { "OLED",          "Panel",       LKF(150, 108, 100, 122, 1.00, 6500, 0, 104, 96, 112,  96, 100) },
    { "IPS",           "Panel",       LKF(140, 106, 106, 108, 1.00, 6400, 0, 112, 100, 106, 100, 100) },
    { "VA",            "Panel",       LKF(145, 108, 104, 112, 0.98, 6400, 0, 108, 98, 104, 100, 100) },
};

int CxLooks_Count(void) { return (int)(sizeof k_looks / sizeof k_looks[0]); }
const CxLookDef *CxLooks_Get(int i)
{
    return (i >= 0 && i < (int)(sizeof k_looks / sizeof k_looks[0])) ? &k_looks[i] : NULL;
}
const CxLookDef *CxLooks_Find(const char *name)
{
    if (!name) return NULL;
    for (int i = 0; i < (int)(sizeof k_looks / sizeof k_looks[0]); i++)
        if (_stricmp(k_looks[i].name, name) == 0) return &k_looks[i];
    return NULL;
}

/* ================= display presets ================= */

static const CxDispPreset k_disp[] = {
    { "16:9 \u00b7 1080p", "16:9", 1920, 1080 },
    { "16:9 \u00b7 1440p", "16:9", 2560, 1440 },
    { "16:9 \u00b7 4K",    "16:9", 3840, 2160 },
    { "4:3 \u00b7 960",    "4:3",  1280,  960 },
    { "4:3 \u00b7 1080",   "4:3",  1440, 1080 },
    { "4:3 \u00b7 1200",   "4:3",  1600, 1200 },
    { "16:10 \u00b7 900",  "16:10", 1440,  900 },
    { "16:10 \u00b7 1050", "16:10", 1680, 1050 },
    { "16:10 \u00b7 1200", "16:10", 1920, 1200 },
    { "Stretched 4:3 \u00b7 1080-class", "4:3", 1920, 1440 },
    { "Stretched 4:3 \u00b7 1440-class", "4:3", 2560, 1920 },
    { "Ultrawide \u00b7 21:9", "21:9", 3440, 1440 },
    { "Ultrawide \u00b7 32:9", "32:9", 5120, 1440 },
    { "Ultrawide \u00b7 2:1",  "2:1",  2560, 1080 },
};

int CxDispPresets_Count(void) { return (int)(sizeof k_disp / sizeof k_disp[0]); }
const CxDispPreset *CxDispPresets_Get(int i)
{
    return (i >= 0 && i < (int)(sizeof k_disp / sizeof k_disp[0])) ? &k_disp[i] : NULL;
}

/* ================= crosshair presets ================= */

static const CxXhPreset k_xh[] = {
    { "Classic Dot", CXXH_DOT,     10, 0, 3, 90,  0, 1, 0, 0xFFFFFF, 0x000000 },
    { "Small Cross", CXXH_CROSS,   12, 2, 2, 95,  0, 0, 0, 0xC6FF3D, 0x000000 },
    { "CS-style",    CXXH_CROSS,   18, 4, 2, 100, 1, 0, 0, 0xC6FF3D, 0x000000 },
    { "Minimal",     CXXH_CROSS,    8, 1, 1, 80,  0, 0, 0, 0xFFFFFF, 0x000000 },
    { "Precision",   CXXH_CROSS,   24, 6, 2, 100, 1, 0, 0, 0xFFFFFF, 0x000000 },
    { "Circle",      CXXH_CIRCLE,  14, 0, 2, 90,  0, 0, 0, 0x4FE3FF, 0x000000 },
    { "Four Dot",    CXXH_FOURDOT, 12, 3, 2, 95,  0, 0, 0, 0xFFFFFF, 0x000000 },
    { "Tactical",    CXXH_CHEVRON, 14, 3, 3, 95,  0, 0, 0, 0xFF9E3D, 0x000000 },
    { "Clean",       CXXH_PLUS,    16, 5, 2, 85,  0, 0, 0, 0xFFFFFF, 0x000000 },
    { "Custom",      CXXH_CROSS,   16, 4, 2, 100, 1, 0, 0, 0x7CFF40, 0x000000 },
};

int CxXhPresets_Count(void) { return (int)(sizeof k_xh / sizeof k_xh[0]); }
const CxXhPreset *CxXhPresets_Get(int i)
{
    return (i >= 0 && i < (int)(sizeof k_xh / sizeof k_xh[0])) ? &k_xh[i] : NULL;
}

const char *CxXhShapeName(int shape)
{
    switch (shape) {
    case CXXH_CROSS:   return "Cross";
    case CXXH_DOT:     return "Dot";
    case CXXH_CIRCLE:  return "Circle";
    case CXXH_SQUARE:  return "Square";
    case CXXH_PLUS:    return "Plus";
    case CXXH_CHEVRON: return "Chevron";
    case CXXH_T:       return "T";
    case CXXH_TTYPE:   return "T-type";
    case CXXH_FOURDOT: return "Four dot";
    }
    return "?";
}
