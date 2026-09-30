/* cx_games.h \u2014 built-in game catalog, global Looks, display presets,
 * crosshair presets (platform independent data) */
#ifndef CX_GAMES_H
#define CX_GAMES_H

#include "cx_color.h"
#include <stdint.h>

/* ---------------- games ---------------- */

typedef struct CxGameLook { char name[40]; CxLook look; } CxGameLook;

typedef struct CxGame {
    int id;
    const char *name;
    const char *category;     /* "FPS", "Survival", ... */
    const char *exe;          /* primary exe basename (lower-case) */
    const char *exe_alt;      /* secondary exe basename (or NULL) */
    const char *aliases;      /* space-separated search aliases, e.g. "cs cs2" */
    const char *blurb;
    int nlooks;
    const CxGameLook *looks;
    int accent;               /* 0..5 \u2014 card art colour */
} CxGame;

enum {
    CXGAME_RUST = 0, CXGAME_CS2, CXGAME_FORTNITE, CXGAME_VALORANT,
    CXGAME_TARKOV, CXGAME_PUBG, CXGAME_APEX, CXGAME_COD, CXGAME_OW2,
    CXGAME_R6, CXGAME_MINECRAFT, CXGAME_GTA5, CXGAME_GTA6, CXGAME_FINALS,
    CXGAME_DAYZ, CXGAME_HELLDIVERS2, CXGAME_ARCRAIDERS, CXGAME_BF,
    CXGAME_DESTINY2, CXGAME_N
};

int           CxGames_Count(void);
const CxGame *CxGames_Get(int i);
/* case-insensitive exe basename match (e.g. "RustClient.exe" -> Rust) */
const CxGame *CxGames_FindExe(const char *exeBase);
/* substring search over name/category; returns up to `max` game indices,
 * gives the number filled */
int           CxGames_Search(const char *query, int out[], int max);

/* ---------------- global looks ---------------- */

typedef struct CxLookDef { const char *name; const char *category; CxLook look; } CxLookDef;

int          CxLooks_Count(void);
const CxLookDef *CxLooks_Get(int i);
const CxLookDef *CxLooks_Find(const char *name);

/* ---------------- display presets ---------------- */

typedef struct CxDispPreset {
    const char *name;
    const char *ratio;   /* "16:9", "4:3", "16:10", "21:9" */
    int w, h;
} CxDispPreset;

int          CxDispPresets_Count(void);
const CxDispPreset *CxDispPresets_Get(int i);

/* ---------------- crosshair presets ---------------- */

enum { CXXH_CROSS = 0, CXXH_DOT, CXXH_CIRCLE, CXXH_SQUARE, CXXH_PLUS,
       CXXH_CHEVRON, CXXH_T, CXXH_TTYPE, CXXH_FOURDOT, CXXH_NSHAPES };

typedef struct CxXhPreset {
    const char *name;
    int shape, size, gap, thick, opacity, outline, dot, rotation;
    uint32_t color, ocolor;
} CxXhPreset;

int          CxXhPresets_Count(void);
const CxXhPreset *CxXhPresets_Get(int i);
const char *CxXhShapeName(int shape);

#endif
