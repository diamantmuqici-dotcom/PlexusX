/* PlexusX — PresetStore: ONE versioned preset library for every category.
 *
 * The product brief asks for a unified preset system with four categories and
 * full library operations.  Before this file, presets were three unrelated
 * things (built-in scene looks, JSON profile exports, crosshair presets baked
 * into crosshair.c).  FormatVersion keys make future migrations safe:
 *
 *   [meta] schema=3
 *   [preset0]
 *   name=Competitive      cat=0 (global)  builtin=0
 *   look=1,185,160,...    the packed 17-field Look (settings_store.h)
 *   xh_shape=1 xh_size=12 ...                 (crosshair payload)
 *   disp_w=2560 disp_h=1440 disp_hz=240 ...   (display payload)
 *
 * PURITY: the whole library model + (de)serialisation is pure C in this header
 * and is compiled by tests/test_all.c; only the file IO lives in the Win32 half
 * (preset_store.c) and reuses the same atomic-write rules as the settings store.
 * A corrupt presets file can therefore never take the app down: the parser is
 * total, every value is clamped, unknown/garbage sections are skipped.
 */
#ifndef PLEXUSX_PRESET_STORE_H
#define PLEXUSX_PRESET_STORE_H

#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include <stdlib.h>

#include "../color/color_math.h"
#include "../settings/settings_store.h"
#include "../games/game_profile.h"   /* PxJson_* readers (pure) */

#define PX_PRESET_MAX      64
#define PX_PRESET_NAME     64
#define PX_PRESET_SCHEMA   3

typedef enum PxPresetCat {
    PX_PRESET_GLOBAL = 0,
    PX_PRESET_GAME,
    PX_PRESET_DISPLAY,
    PX_PRESET_CROSSHAIR,
    PX_PRESET_CAT_COUNT
} PxPresetCat;

static inline const char *PxPreset_CatName(int cat)
{
    switch (cat) {
    case PX_PRESET_GLOBAL:    return "Global";
    case PX_PRESET_GAME:      return "Game";
    case PX_PRESET_DISPLAY:   return "Display";
    case PX_PRESET_CROSSHAIR: return "Crosshair";
    default:                  return "?";
    }
}

typedef struct PxPreset {
    char     name[PX_PRESET_NAME];   /* UTF-8; the INI value is the raw bytes   */
    int      category;
    int      builtin;                /* 0 = user preset (editable/deletable)    */

    Look     look;                   /* Global + Game                           */

    /* crosshair payload */
    int      xh_shape, xh_size, xh_gap, xh_thick, xh_opacity, xh_dot, xh_dot_size;
    int      xh_outline, xh_outline_th, xh_rot;
    unsigned xh_color, xh_ocolor;

    /* display payload */
    int      disp_w, disp_h, disp_hz, disp_hdr, monitor_idx;

    unsigned long created;           /* unix seconds, 0 = unknown               */
} PxPreset;

typedef struct PxPresetLib {
    PxPreset p[PX_PRESET_MAX];
    int      n;
    int      schema;
} PxPresetLib;

static inline void PxPreset_Neutral(PxPreset *ps)
{
    memset(ps, 0, sizeof *ps);
    ps->look = (Look)LOOK_NEUTRAL_INIT;
    ps->xh_shape = 1; ps->xh_size = 12; ps->xh_gap = 4; ps->xh_thick = 2;
    ps->xh_opacity = 90; ps->xh_dot = 1; ps->xh_dot_size = 2;
    ps->xh_outline = 1; ps->xh_outline_th = 1; ps->xh_rot = 0;
    ps->xh_color = 0x00FF3DC6u;   /* stored RGB (0x00BBGGRR, COLORREF order) */
    ps->xh_ocolor = 0x00000000u;
    ps->monitor_idx = -1;
    ps->disp_hdr = 0;
}

static inline void PxPreset_Sanitize(PxPreset *ps)
{
    if (ps->category < 0 || ps->category >= PX_PRESET_CAT_COUNT) ps->category = PX_PRESET_GLOBAL;
    cm_sanitize_look(&ps->look);
    ps->xh_shape      = ps->xh_shape < 0 ? 0 : (ps->xh_shape > 7 ? 7 : ps->xh_shape);
    ps->xh_size       = ps->xh_size < 4 ? 4 : (ps->xh_size > 64 ? 64 : ps->xh_size);
    ps->xh_gap        = ps->xh_gap < 0 ? 0 : (ps->xh_gap > 32 ? 32 : ps->xh_gap);
    ps->xh_thick      = ps->xh_thick < 1 ? 1 : (ps->xh_thick > 12 ? 12 : ps->xh_thick);
    ps->xh_opacity    = ps->xh_opacity < 10 ? 10 : (ps->xh_opacity > 100 ? 100 : ps->xh_opacity);
    ps->xh_dot        = ps->xh_dot ? 1 : 0;
    ps->xh_dot_size   = ps->xh_dot_size < 1 ? 1 : (ps->xh_dot_size > 8 ? 8 : ps->xh_dot_size);
    ps->xh_outline    = ps->xh_outline ? 1 : 0;
    ps->xh_outline_th = ps->xh_outline_th < 1 ? 1 : (ps->xh_outline_th > 4 ? 4 : ps->xh_outline_th);
    ps->xh_rot        = ((ps->xh_rot % 360) + 360) % 360;
    ps->monitor_idx   = ps->monitor_idx < -1 ? -1 : (ps->monitor_idx > 7 ? 7 : ps->monitor_idx);
    ps->disp_w        = ps->disp_w < 0 ? 0 : ps->disp_w;
    ps->disp_h        = ps->disp_h < 0 ? 0 : ps->disp_h;
    ps->disp_hz       = ps->disp_hz < 0 ? 0 : ps->disp_hz;
    ps->disp_hdr      = ps->disp_hdr < 0 ? 0 : (ps->disp_hdr > 2 ? 2 : ps->disp_hdr);
}

/* Library operations, serialisation and the built-in seed are implemented
 * inline below (pure C: tests/test_all.c compiles them directly). */

/* ---------------- Win32 file IO (preset_store.c) -------------------------- */
/* The pure API above is implemented below this marker; the Win32 half only adds
 * file IO + seeding. */

/* ---------------- Win32 file IO (preset_store.c) -------------------------- */
int  PxPre_Init(const wchar_t *appdata);        /* load presets.ini (or seed) */
int  PxPre_Save(void);
PxPresetLib *PxPre_Lib(void);
int  PxPre_Corrupt(void);                        /* 1 = the file was moved aside */
int  PxPre_ExportFile(const wchar_t *path);
int  PxPre_ImportFile(const wchar_t *path);


/* ================= pure implementation ==================================== */

static inline int PxPre_Find(const PxPresetLib *lib, const char *name, int category)
{
    if (!lib || !name) return -1;
    for (int i = 0; i < lib->n && i < PX_PRESET_MAX; i++) {
        if (lib->p[i].category != category) continue;
        if (px_ci_eq(lib->p[i].name, name)) return i;
    }
    return -1;
}

static inline int PxPre_Add(PxPresetLib *lib, const PxPreset *ps)
{
    if (!lib || !ps || lib->n >= PX_PRESET_MAX) return -1;
    lib->p[lib->n] = *ps;
    PxPreset_Sanitize(&lib->p[lib->n]);
    if (!lib->p[lib->n].name[0])
        snprintf(lib->p[lib->n].name, PX_PRESET_NAME, "Preset %d", lib->n + 1);
    lib->n++;
    return lib->n - 1;
}

static inline void pxpre_copyname(char *dst, size_t cap, const char *src, int suffix)
{
    if (!dst || !cap) return;
    if (!src || !src[0]) src = "Preset";
    if (suffix > 1) snprintf(dst, cap, "%.*s %d", (int)cap - 8, src, suffix);
    else            snprintf(dst, cap, "%s", src);
    dst[cap - 1] = 0;
}

static inline int PxPre_Duplicate(PxPresetLib *lib, int idx, const char *new_name)
{
    PxPreset copy;
    int suffix = 2;
    if (!lib || idx < 0 || idx >= lib->n) return -1;
    copy = lib->p[idx];
    copy.builtin = 0;
    pxpre_copyname(copy.name, PX_PRESET_NAME, new_name && new_name[0] ? new_name : lib->p[idx].name, 1);
    while (PxPre_Find(lib, copy.name, copy.category) >= 0 && suffix < 1000)
        pxpre_copyname(copy.name, PX_PRESET_NAME, lib->p[idx].name, suffix++);
    return PxPre_Add(lib, &copy);
}

static inline int PxPre_Rename(PxPresetLib *lib, int idx, const char *new_name)
{
    if (!lib || idx < 0 || idx >= lib->n || !new_name || !new_name[0]) return -1;
    pxpre_copyname(lib->p[idx].name, PX_PRESET_NAME, new_name, 1);
    lib->p[idx].builtin = 0;      /* renaming makes it the user's */
    return 0;
}

static inline int PxPre_Delete(PxPresetLib *lib, int idx)
{
    if (!lib || idx < 0 || idx >= lib->n) return -1;
    for (int i = idx; i < lib->n - 1; i++) lib->p[i] = lib->p[i + 1];
    lib->n--;
    return 0;
}

static inline int PxPre_Serialize(const PxPresetLib *lib, char *buf, size_t cap)
{
    PxIni ini;
    char line[PXINI_VALUE];
    if (!lib || !buf || !cap) return -1;
    px_ini_init(&ini);
    px_ini_set_int(&ini, "meta", "schema", PX_PRESET_SCHEMA);
    px_ini_set_int(&ini, "meta", "count", lib->n);
    for (int i = 0; i < lib->n && i < PX_PRESET_MAX; i++) {
        const PxPreset *p = &lib->p[i];
        char sec[PXINI_SECTION], key[PXINI_KEY];
        snprintf(sec, sizeof sec, "preset%d", i);
        px_ini_set(&ini, sec, "name", p->name);
        px_ini_set_int(&ini, sec, "cat", p->category);
        px_ini_set_int(&ini, sec, "builtin", p->builtin);
        px_look_pack(&p->look, line, sizeof line);
        px_ini_set(&ini, sec, "look", line);
        px_ini_set_int(&ini, sec, "xh_shape", p->xh_shape);
        px_ini_set_int(&ini, sec, "xh_size", p->xh_size);
        px_ini_set_int(&ini, sec, "xh_gap", p->xh_gap);
        px_ini_set_int(&ini, sec, "xh_thick", p->xh_thick);
        px_ini_set_int(&ini, sec, "xh_opacity", p->xh_opacity);
        px_ini_set_int(&ini, sec, "xh_dot", p->xh_dot);
        px_ini_set_int(&ini, sec, "xh_dot_size", p->xh_dot_size);
        px_ini_set_int(&ini, sec, "xh_outline", p->xh_outline);
        px_ini_set_int(&ini, sec, "xh_outline_th", p->xh_outline_th);
        px_ini_set_int(&ini, sec, "xh_rot", p->xh_rot);
        snprintf(key, sizeof key, "%u", p->xh_color);
        px_ini_set(&ini, sec, "xh_color", key);
        snprintf(key, sizeof key, "%u", p->xh_ocolor);
        px_ini_set(&ini, sec, "xh_ocolor", key);
        px_ini_set_int(&ini, sec, "disp_w", p->disp_w);
        px_ini_set_int(&ini, sec, "disp_h", p->disp_h);
        px_ini_set_int(&ini, sec, "disp_hz", p->disp_hz);
        px_ini_set_int(&ini, sec, "disp_hdr", p->disp_hdr);
        px_ini_set_int(&ini, sec, "monitor", p->monitor_idx);
        snprintf(key, sizeof key, "%lu", p->created);
        px_ini_set(&ini, sec, "created", key);
    }
    return px_ini_serialize(&ini, buf, cap);
}

static inline unsigned long pxpre_ul(const char *s, unsigned long dflt)
{
    char *end = 0;
    unsigned long v;
    if (!s || !*s) return dflt;
    v = strtoul(s, &end, 10);
    return (end && end != s) ? v : dflt;
}

static inline int PxPre_Parse(PxPresetLib *lib, const char *text)
{
    PxIni ini;
    int count;
    if (!lib || !text) return 0;
    lib->n = 0;
    lib->schema = 0;
    px_ini_init(&ini);
    if (px_ini_parse(&ini, text) < 0) return 0;
    lib->schema = px_ini_get_int(&ini, "meta", "schema", 0);
    count = px_ini_get_int(&ini, "meta", "count", -1);
    for (int i = 0; i < PX_PRESET_MAX; i++) {
        char sec[PXINI_SECTION];
        PxPreset ps;
        const char *v;
        snprintf(sec, sizeof sec, "preset%d", i);
        if (!px_ini_has_section(&ini, sec)) {
            if (count >= 0 && i >= count && lib->n > 0) break;   /* trust the count when present */
            continue;
        }
        PxPreset_Neutral(&ps);
        if ((v = px_ini_get(&ini, sec, "name"))) {
            size_t k = 0;
            for (; v[k] && k + 1 < PX_PRESET_NAME; k++) ps.name[k] = v[k];
            ps.name[k] = 0;
        }
        ps.category  = px_ini_get_int(&ini, sec, "cat", 0);
        ps.builtin   = px_ini_get_int(&ini, sec, "builtin", 0) ? 1 : 0;
        if ((v = px_ini_get(&ini, sec, "look"))) px_look_unpack(v, &ps.look);
        ps.xh_shape      = px_ini_get_int(&ini, sec, "xh_shape", 1);
        ps.xh_size       = px_ini_get_int(&ini, sec, "xh_size", 12);
        ps.xh_gap        = px_ini_get_int(&ini, sec, "xh_gap", 4);
        ps.xh_thick      = px_ini_get_int(&ini, sec, "xh_thick", 2);
        ps.xh_opacity    = px_ini_get_int(&ini, sec, "xh_opacity", 90);
        ps.xh_dot        = px_ini_get_int(&ini, sec, "xh_dot", 1);
        ps.xh_dot_size   = px_ini_get_int(&ini, sec, "xh_dot_size", 2);
        ps.xh_outline    = px_ini_get_int(&ini, sec, "xh_outline", 1);
        ps.xh_outline_th = px_ini_get_int(&ini, sec, "xh_outline_th", 1);
        ps.xh_rot        = px_ini_get_int(&ini, sec, "xh_rot", 0);
        ps.xh_color      = (unsigned)pxpre_ul(px_ini_get(&ini, sec, "xh_color"), 0x00FF3DC6u);
        ps.xh_ocolor     = (unsigned)pxpre_ul(px_ini_get(&ini, sec, "xh_ocolor"), 0);
        ps.disp_w        = px_ini_get_int(&ini, sec, "disp_w", 0);
        ps.disp_h        = px_ini_get_int(&ini, sec, "disp_h", 0);
        ps.disp_hz       = px_ini_get_int(&ini, sec, "disp_hz", 0);
        ps.disp_hdr      = px_ini_get_int(&ini, sec, "disp_hdr", 0);
        ps.monitor_idx   = px_ini_get_int(&ini, sec, "monitor", -1);
        ps.created       = pxpre_ul(px_ini_get(&ini, sec, "created"), 0);
        PxPreset_Sanitize(&ps);
        if (!ps.name[0] && lib->n > 0) continue;      /* nameless ghost section */
        PxPre_Add(lib, &ps);
    }
    return lib->n;
}

/* ---------------- JSON (shareable, same data) ------------------------------ */

static inline int pxpre_put(char *buf, size_t cap, int o, const char *s)
{
    while (s && *s) { if (o + 1 >= (int)cap) break; buf[o++] = *s++; }
    if (cap) buf[o < (int)cap ? o : (int)cap - 1] = 0;
    return o;
}

static inline int pxpre_esc(char *buf, size_t cap, int o, const char *s)
{
    o = pxpre_put(buf, cap, o, "\"");
    for (; s && *s; s++) {
        unsigned char c = (unsigned char)*s;
        if (c == '"' || c == '\\') {
            if (o + 2 >= (int)cap) break;
            buf[o++] = '\\';
            buf[o++] = (char)c;
        } else if (c < 0x20) {
            char tmp[8];
            snprintf(tmp, sizeof tmp, "\\u%04x", c);
            o = pxpre_put(buf, cap, o, tmp);
        } else {
            if (o + 1 >= (int)cap) break;
            buf[o++] = (char)c;
        }
    }
    return pxpre_put(buf, cap, o, "\"");
}

static inline int pxpre_num(char *buf, size_t cap, int o, double v)
{
    char tmp[48];
    if (!(v == v) || v > 1e30 || v < -1e30) v = 0;
    snprintf(tmp, sizeof tmp, "%.6g", v);
    return pxpre_put(buf, cap, o, tmp);
}

static inline int PxPre_ToJson(const PxPresetLib *lib, char *buf, size_t cap)
{
    int o = 0;
    if (!lib || !buf || !cap) return -1;
    buf[0] = 0;
    o = pxpre_put(buf, cap, o, "{\n  \"plexusx_presets\": ");
    o = pxpre_num(buf, cap, o, PX_PRESET_SCHEMA);
    o = pxpre_put(buf, cap, o, ",\n  \"count\": ");
    o = pxpre_num(buf, cap, o, lib->n);
    o = pxpre_put(buf, cap, o, ",\n  \"presets\": [");
    for (int i = 0; i < lib->n && i < PX_PRESET_MAX; i++) {
        const PxPreset *p = &lib->p[i];
        Look lk = p->look;
        cm_sanitize_look(&lk);
        o = pxpre_put(buf, cap, o, i ? ",\n    {" : "\n    {");
        o = pxpre_put(buf, cap, o, "\"name\": ");      o = pxpre_esc(buf, cap, o, p->name);
        o = pxpre_put(buf, cap, o, ", \"category\": ");o = pxpre_num(buf, cap, o, p->category);
        o = pxpre_put(buf, cap, o, ", \"builtin\": "); o = pxpre_num(buf, cap, o, p->builtin);
        o = pxpre_put(buf, cap, o, ", \"look\": {\"enabled\": ");
        o = pxpre_num(buf, cap, o, lk.enabled);
        o = pxpre_put(buf, cap, o, ", \"sat\": ");        o = pxpre_num(buf, cap, o, lk.sat);
        o = pxpre_put(buf, cap, o, ", \"vibrance\": ");   o = pxpre_num(buf, cap, o, lk.vibrance);
        o = pxpre_put(buf, cap, o, ", \"brightness\": "); o = pxpre_num(buf, cap, o, lk.bri);
        o = pxpre_put(buf, cap, o, ", \"contrast\": ");   o = pxpre_num(buf, cap, o, lk.con);
        o = pxpre_put(buf, cap, o, ", \"gamma\": ");      o = pxpre_num(buf, cap, o, lk.gamma);
        o = pxpre_put(buf, cap, o, ", \"temperature\": ");o = pxpre_num(buf, cap, o, lk.temp);
        o = pxpre_put(buf, cap, o, ", \"tint\": ");       o = pxpre_num(buf, cap, o, lk.tint);
        o = pxpre_put(buf, cap, o, ", \"r\": ");          o = pxpre_num(buf, cap, o, lk.r_gain);
        o = pxpre_put(buf, cap, o, ", \"g\": ");          o = pxpre_num(buf, cap, o, lk.g_gain);
        o = pxpre_put(buf, cap, o, ", \"b\": ");          o = pxpre_num(buf, cap, o, lk.b_gain);
        o = pxpre_put(buf, cap, o, ", \"shadows\": ");    o = pxpre_num(buf, cap, o, lk.shadows);
        o = pxpre_put(buf, cap, o, ", \"highlights\": "); o = pxpre_num(buf, cap, o, lk.highlights);
        o = pxpre_put(buf, cap, o, ", \"blackLevel\": "); o = pxpre_num(buf, cap, o, lk.black_level);
        o = pxpre_put(buf, cap, o, ", \"whitePoint\": "); o = pxpre_num(buf, cap, o, lk.white_point);
        o = pxpre_put(buf, cap, o, ", \"clarity\": ");    o = pxpre_num(buf, cap, o, lk.clarity);
        o = pxpre_put(buf, cap, o, ", \"hue\": ");        o = pxpre_num(buf, cap, o, lk.hue);
        o = pxpre_put(buf, cap, o, "}");
        o = pxpre_put(buf, cap, o, ", \"crosshair\": {\"shape\": "); o = pxpre_num(buf, cap, o, p->xh_shape);
        o = pxpre_put(buf, cap, o, ", \"size\": ");        o = pxpre_num(buf, cap, o, p->xh_size);
        o = pxpre_put(buf, cap, o, ", \"gap\": ");         o = pxpre_num(buf, cap, o, p->xh_gap);
        o = pxpre_put(buf, cap, o, ", \"thickness\": ");   o = pxpre_num(buf, cap, o, p->xh_thick);
        o = pxpre_put(buf, cap, o, ", \"opacity\": ");     o = pxpre_num(buf, cap, o, p->xh_opacity);
        o = pxpre_put(buf, cap, o, ", \"centerDot\": ");   o = pxpre_num(buf, cap, o, p->xh_dot);
        o = pxpre_put(buf, cap, o, ", \"dotSize\": ");     o = pxpre_num(buf, cap, o, p->xh_dot_size);
        o = pxpre_put(buf, cap, o, ", \"outline\": ");     o = pxpre_num(buf, cap, o, p->xh_outline);
        o = pxpre_put(buf, cap, o, ", \"outlineWidth\": ");o = pxpre_num(buf, cap, o, p->xh_outline_th);
        o = pxpre_put(buf, cap, o, ", \"rotation\": ");    o = pxpre_num(buf, cap, o, p->xh_rot);
        o = pxpre_put(buf, cap, o, ", \"color\": ");       o = pxpre_num(buf, cap, o, p->xh_color);
        o = pxpre_put(buf, cap, o, ", \"outlineColor\": ");o = pxpre_num(buf, cap, o, p->xh_ocolor);
        o = pxpre_put(buf, cap, o, "}");
        o = pxpre_put(buf, cap, o, ", \"display\": {\"width\": "); o = pxpre_num(buf, cap, o, p->disp_w);
        o = pxpre_put(buf, cap, o, ", \"height\": ");      o = pxpre_num(buf, cap, o, p->disp_h);
        o = pxpre_put(buf, cap, o, ", \"hz\": ");          o = pxpre_num(buf, cap, o, p->disp_hz);
        o = pxpre_put(buf, cap, o, ", \"hdr\": ");         o = pxpre_num(buf, cap, o, p->disp_hdr);
        o = pxpre_put(buf, cap, o, ", \"monitor\": ");     o = pxpre_num(buf, cap, o, p->monitor_idx);
        o = pxpre_put(buf, cap, o, "}},\n");
    }
    o = pxpre_put(buf, cap, o, "  ]\n}\n");
    return o;
}

/* A tiny tolerant reader: presets are objects inside the "presets" array, so we
 * slice object-by-object (brace counting) and read flat keys out of each. */
static inline int PxPre_FromJson(PxPresetLib *lib, const char *text)
{
    const char *p;
    int added = 0;
    if (!lib || !text) return 0;
    p = strstr(text, "presets");
    if (!p) p = text;
    p = strchr(p, '[');
    if (!p) return 0;
    p++;
    while (*p) {
        const char *obj, *end;
        int depth = 0;
        PxPreset ps;
        while (*p && *p != '{' && *p != ']') p++;
        if (*p != '{') break;
        obj = p;
        end = p;
        while (*end) {
            if (*end == '{') depth++;
            else if (*end == '}') { depth--; if (depth == 0) { end++; break; } }
            end++;
        }
        {
            char tmp[4096];
            size_t n = (size_t)(end - obj);
            if (n >= sizeof tmp) n = sizeof tmp - 1;
            memcpy(tmp, obj, n);
            tmp[n] = 0;
            PxPreset_Neutral(&ps);
            {
                const char *nm = strstr(tmp, "\"name\"");
                if (nm) {
                    const char *q = strchr(nm + 6, '"');
                    if (q) {
                        size_t k = 0;
                        q++;
                        while (*q && *q != '"' && k + 1 < PX_PRESET_NAME) {
                            if (*q == '\\' && q[1]) q++;
                            ps.name[k++] = *q++;
                        }
                        ps.name[k] = 0;
                    }
                }
            }
            ps.category = PxJson_ReadInt(tmp, "category", 0);
            ps.builtin  = PxJson_ReadBool(tmp, "builtin", 0);
            if (!PxJson_ReadLook(tmp, "look", &ps.look)) ps.look = (Look)LOOK_NEUTRAL_INIT;
            ps.xh_shape       = PxJson_ReadInt(tmp, "shape", ps.xh_shape);
            ps.xh_size        = PxJson_ReadInt(tmp, "size", ps.xh_size);
            ps.xh_gap         = PxJson_ReadInt(tmp, "gap", ps.xh_gap);
            ps.xh_thick       = PxJson_ReadInt(tmp, "thickness", ps.xh_thick);
            ps.xh_opacity     = PxJson_ReadInt(tmp, "opacity", ps.xh_opacity);
            ps.xh_dot         = PxJson_ReadBool(tmp, "centerDot", ps.xh_dot);
            ps.xh_dot_size    = PxJson_ReadInt(tmp, "dotSize", ps.xh_dot_size);
            ps.xh_outline     = PxJson_ReadBool(tmp, "outline", ps.xh_outline);
            ps.xh_outline_th  = PxJson_ReadInt(tmp, "outlineWidth", ps.xh_outline_th);
            ps.xh_rot         = PxJson_ReadInt(tmp, "rotation", 0);
            ps.xh_color       = (unsigned)PxJson_ReadNum(tmp, "color", (double)ps.xh_color);
            ps.xh_ocolor      = (unsigned)PxJson_ReadNum(tmp, "outlineColor", (double)ps.xh_ocolor);
            ps.disp_w         = PxJson_ReadInt(tmp, "width", 0);
            ps.disp_h         = PxJson_ReadInt(tmp, "height", 0);
            ps.disp_hz        = PxJson_ReadInt(tmp, "hz", 0);
            ps.disp_hdr       = PxJson_ReadInt(tmp, "hdr", 0);
            ps.monitor_idx    = PxJson_ReadInt(tmp, "monitor", -1);
            PxPreset_Sanitize(&ps);
            if (ps.name[0] && PxPre_Add(lib, &ps) >= 0) added++;
        }
        p = end;
    }
    return added;
}

/* ---------------- built-in seed ------------------------------------------- */
/* Seeded once, when no presets file exists.  Users keep them (deletable like
 * any other preset) and their tweaks are what the file then stores. */
static inline void PxPre_SeedBuiltins(PxPresetLib *lib)
{
    static const struct { const char *name; Look look; } globals[] = {
        { "Neutral",        { 1, 100, 100, 100, 100, 1.00f, 6500, 0, 100, 100, 100, 100, 100, 100, 100, 100, 0 } },
        { "Competitive",    { 1, 185, 160, 106, 114, 0.95f, 6500, 0, 100, 100, 100, 120,  95, 100, 100, 115, 0 } },
        { "Vivid 300%",     { 1, 300, 240, 105, 112, 1.00f, 6500, 0, 100, 100, 100, 110, 100, 100, 100, 115, 0 } },
        { "Night Ops",      { 1, 235, 190, 122, 110, 0.78f, 6200, 0, 102, 100,  98, 155,  85, 108, 100, 120, 0 } },
        { "Cinematic Warm", { 1, 135, 120, 102, 110, 1.04f, 5800, 4, 105, 100,  94,  95, 105, 100, 100, 105, 0 } },
        { "Cool Clarity",   { 1, 140, 130, 104, 108, 0.98f, 7500,-2,  96, 100, 108, 110,  95, 100, 100, 112, 0 } },
        { "High Contrast",  { 1, 165, 140, 105, 125, 0.95f, 6500, 0, 100, 100, 100, 115,  90, 100, 100, 125, 0 } },
        { "OLED Punch",     { 1, 175, 150, 100, 112, 1.00f, 6500, 0, 100, 100, 100, 100, 100, 100, 100, 110, 0 } }
    };
    static const struct { const char *name; int w, h, hz; } displays[] = {
        { "Native 16:9",      0,    0,    0   },
        { "4:3 1280x960",     1280, 960,  0   },
        { "4:3 1440x1080",    1440, 1080, 0   },
        { "4:3 1600x1200",    1600, 1200, 0   },
        { "16:10 1680x1050",  1680, 1050, 0   },
        { "Ultrawide 2560x1080", 2560, 1080, 0 },
        { "Max Refresh (current res)", 0, 0, -1 }
    };
    static const struct { const char *name; int shape, size, gap, thick, opacity, dot; } xhairs[] = {
        { "Dot",        0, 10, 0,  2, 90,  1 },
        { "Tiny Cross", 1,  8, 3,  2, 90,  0 },
        { "Wide Cross", 1, 14, 8,  2, 85,  1 },
        { "Circle",     2, 16, 4,  2, 85,  1 },
        { "T-Style",    6, 14, 4,  2, 85,  0 }
    };
    if (!lib) return;
    for (size_t i = 0; i < sizeof globals / sizeof globals[0]; i++) {
        PxPreset ps;
        PxPreset_Neutral(&ps);
        snprintf(ps.name, PX_PRESET_NAME, "%s", globals[i].name);
        ps.category = PX_PRESET_GLOBAL;
        ps.builtin = 1;
        ps.look = globals[i].look;
        PxPre_Add(lib, &ps);
    }
    for (size_t i = 0; i < sizeof displays / sizeof displays[0]; i++) {
        PxPreset ps;
        PxPreset_Neutral(&ps);
        snprintf(ps.name, PX_PRESET_NAME, "%s", displays[i].name);
        ps.category = PX_PRESET_DISPLAY;
        ps.builtin = 1;
        ps.disp_w = displays[i].w;
        ps.disp_h = displays[i].h;
        ps.disp_hz = displays[i].hz;
        PxPre_Add(lib, &ps);
    }
    for (size_t i = 0; i < sizeof xhairs / sizeof xhairs[0]; i++) {
        PxPreset ps;
        PxPreset_Neutral(&ps);
        snprintf(ps.name, PX_PRESET_NAME, "%s", xhairs[i].name);
        ps.category = PX_PRESET_CROSSHAIR;
        ps.builtin = 1;
        ps.xh_shape = xhairs[i].shape;
        ps.xh_size = xhairs[i].size;
        ps.xh_gap = xhairs[i].gap;
        ps.xh_thick = xhairs[i].thick;
        ps.xh_opacity = xhairs[i].opacity;
        ps.xh_dot = xhairs[i].dot;
        PxPre_Add(lib, &ps);
    }
}


#endif /* PLEXUSX_PRESET_STORE_H */
