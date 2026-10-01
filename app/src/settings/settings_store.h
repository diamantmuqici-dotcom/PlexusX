/* PlexusX — SettingsStore core (header only, platform independent)
 *
 * A tiny, total INI implementation: the on-disk format is the same
 * [section] / key=value layout PlexusX has always used, but parsed by this
 * code instead of relying on what GetPrivateProfileStringW tolerates.  Rules:
 *
 *   - corrupt input can never crash or hang: every line is length-checked,
 *     malformed lines are counted (bad_lines) and skipped;
 *   - missing keys return the caller's default (defaults ARE the migration
 *     floor — the app never needs to "handle" a missing file specially);
 *   - duplicates: FIRST wins (same contract as Win32's profile API);
 *   - values round-trip through px_ini_set so writes are normalized;
 *   - look/float parses are NaN/Inf hostile: strtod output is range-checked
 *     before it can poison a color state.
 *
 * File I/O, UTF-16 BOM handling and atomic replace live in the Win32 glue
 * (settings_store.c); tests exercise this layer directly with text buffers.
 */
#ifndef PLEXUSX_SETTINGS_STORE_H
#define PLEXUSX_SETTINGS_STORE_H

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../color/color_math.h"      /* cm_sanitize_look for loaded looks */

#define PXINI_MAX      1024   /* ~216 B/entry ≈ 220 KB; sized for 32 profiles × 12 sub-modes */
#define PXINI_SECTION  24
#define PXINI_KEY      32
#define PXINI_VALUE    256   /* must hold the worst-case packed Look line (17 x %g) without truncation */

typedef struct PxIniEntry {
    char sec[PXINI_SECTION];
    char key[PXINI_KEY];
    char val[PXINI_VALUE];
} PxIniEntry;

typedef struct PxIni {
    PxIniEntry e[PXINI_MAX];
    int n;
    int bad_lines;       /* malformed lines that were skipped          */
    int overflow;        /* entries beyond PXINI_MAX that were dropped  */
    int saw_bom;         /* input started with a UTF-16/8 BOM (converted already) */
} PxIni;

static inline void px_ini_init(PxIni *ini) { memset(ini, 0, sizeof *ini); }

/* Case-insensitive ASCII compare on two bounded fixed buffers. */
static inline int px_ci_eq(const char *a, const char *b)
{
    while (*a && *b) {
        char ca = *a, cb = *b;
        if (ca >= 'A' && ca <= 'Z') ca += 32;
        if (cb >= 'A' && cb <= 'Z') cb += 32;
        if (ca != cb) return 0;
        a++; b++;
    }
    return *a == *b;
}

static inline void px_ci_copy(char *dst, size_t cap, const char *src, size_t len)
{
    size_t i = 0;
    if (!cap) return;
    for (; i + 1 < cap && i < len; i++) {
        char c = src[i];
        if (c == '\r' || c == '\n' || c == '=') c = ' ';   /* keep the store parseable */
        dst[i] = c;
    }
    dst[i] = 0;
}

/* Parse a NUL-terminated ASCII/UTF-8 buffer (already converted away from any
 * UTF-16 BOM by the caller).  Tolerates \n, \r\n and \r separators. */
static inline int px_ini_parse(PxIni *ini, const char *text)
{
    const char *cur_sec = "general";
    char secbuf[PXINI_SECTION];
    if (!ini || !text) return -1;
    px_ini_init(ini);
    secbuf[0] = 0;

    while (*text) {
        const char *eol = text;
        while (*eol && *eol != '\n' && *eol != '\r') eol++;

        size_t len = (size_t)(eol - text);
        const char *line = text;
        while (len && (*line == ' ' || *line == '\t')) { line++; len--; }
        while (len && (line[len - 1] == ' ' || line[len - 1] == '\t')) len--;

        if (len) {
            if (line[0] == ';' || line[0] == '#') {
                /* comment */
            } else if (line[0] == '[') {
                const char *close = memchr(line, ']', len);
                if (!close || (size_t)(close - line) < 2) {
                    ini->bad_lines++;
                } else {
                    px_ci_copy(secbuf, sizeof secbuf, line + 1, (size_t)(close - line - 1));
                    cur_sec = secbuf;
                }
            } else {
                const char *eq = memchr(line, '=', len);
                if (!eq || eq == line) {
                    ini->bad_lines++;
                } else {
                    if (ini->n >= PXINI_MAX) { ini->overflow = 1; }
                    else {
                        PxIniEntry *ent = &ini->e[ini->n];
                        const char *k = line; size_t kl = (size_t)(eq - line);
                        while (kl && (k[kl - 1] == ' ' || k[kl - 1] == '\t')) kl--;
                        const char *v = eq + 1; size_t vl = (size_t)(eol - v);
                        while (vl && (*v == ' ' || *v == '\t')) { v++; vl--; }
                        while (vl && (v[vl - 1] == ' ' || v[vl - 1] == '\t')) vl--;
                        px_ci_copy(ent->sec, sizeof ent->sec, cur_sec, strlen(cur_sec));
                        px_ci_copy(ent->key, sizeof ent->key, k, kl);
                        /* value keeps its bytes verbatim (numbers, paths, names) */
                        {
                            size_t i = 0;
                            for (; i + 1 < PXINI_VALUE && i < vl; i++) ent->val[i] = v[i];
                            ent->val[i] = 0;
                        }
                        ini->n++;
                    }
                }
            }
        }

        text = eol;
        while (*text == '\n' || *text == '\r') text++;
    }
    return ini->n;
}

static inline const char *px_ini_get(const PxIni *ini, const char *sec, const char *key)
{
    if (!ini || !sec || !key) return 0;
    for (int i = 0; i < ini->n; i++) {
        if (px_ci_eq(ini->e[i].sec, sec) && px_ci_eq(ini->e[i].key, key))
            return ini->e[i].val;
    }
    return 0;
}

static inline int px_ini_get_int(const PxIni *ini, const char *sec, const char *key, int dflt)
{
    const char *v = px_ini_get(ini, sec, key);
    if (!v || !*v) return dflt;
    char *end = 0;
    long x = strtol(v, &end, 10);
    if (!end || end == v) return dflt;
    return (int)x;
}

/* Finite double or fallback. Never trusts the text. */
static inline double px_ini_get_dbl(const PxIni *ini, const char *sec, const char *key, double dflt)
{
    const char *v = px_ini_get(ini, sec, key);
    if (!v || !*v) return dflt;
    char *end = 0;
    double x = strtod(v, &end);
    if (!end || end == v) return dflt;
    if (x != x || x > 1e30 || x < -1e30) return dflt;
    return x;
}

static inline float px_ini_get_flt(const PxIni *ini, const char *sec, const char *key, float dflt)
{
    return (float)px_ini_get_dbl(ini, sec, key, (double)dflt);
}

/* set / overwrite (normalized, always exactly one entry per sec+key) */
static inline int px_ini_set(PxIni *ini, const char *sec, const char *key, const char *val)
{
    if (!ini || !sec || !key) return -1;
    for (int i = 0; i < ini->n; i++) {
        if (px_ci_eq(ini->e[i].sec, sec) && px_ci_eq(ini->e[i].key, key)) {
            size_t n = 0;
            for (; val && val[n] && n + 1 < PXINI_VALUE; n++) ini->e[i].val[n] = val[n];
            ini->e[i].val[n] = 0;
            return 0;
        }
    }
    if (ini->n >= PXINI_MAX) { ini->overflow = 1; return -1; }
    PxIniEntry *ent = &ini->e[ini->n++];
    px_ci_copy(ent->sec, sizeof ent->sec, sec, strlen(sec));
    px_ci_copy(ent->key, sizeof ent->key, key, strlen(key));
    {
        size_t n = 0;
        for (; val && val[n] && n + 1 < PXINI_VALUE; n++) ent->val[n] = val[n];
        ent->val[n] = 0;
    }
    return 0;
}

static inline int px_ini_set_int(PxIni *ini, const char *sec, const char *key, long v)
{
    char b[24];
    snprintf(b, sizeof b, "%ld", v);
    return px_ini_set(ini, sec, key, b);
}

static inline int px_ini_set_dbl(PxIni *ini, const char *sec, const char *key, double v)
{
    char b[40];
    snprintf(b, sizeof b, "%g", v);
    return px_ini_set(ini, sec, key, b);
}

/* Drop every entry of a section (used when rewriting the profile table). */
static inline int px_ini_clear_section(PxIni *ini, const char *sec)
{
    if (!ini || !sec) return 0;
    int removed = 0, w = 0;
    for (int r = 0; r < ini->n; r++) {
        if (px_ci_eq(ini->e[r].sec, sec)) { removed++; continue; }
        if (w != r) ini->e[w] = ini->e[r];
        w++;
    }
    ini->n = w;
    return removed;
}

/* Serialize with [section] headers, in first-appearance order, \r\n endings
 * (what a human expects next to legacy Windows INI files). */
static inline int px_ini_serialize(const PxIni *ini, char *buf, size_t cap)
{
    if (!ini || !buf || !cap) return -1;
    size_t pos = 0;
    const char *cur = 0;
    char secname[PXINI_SECTION] = { 0 };
    for (int i = 0; i < ini->n; i++) {
        if (!cur || !px_ci_eq(cur, ini->e[i].sec)) {
            cur = secname;
            px_ci_copy(secname, sizeof secname, ini->e[i].sec, strlen(ini->e[i].sec));
            int w = snprintf(buf + pos, cap - pos, "%s[%s]\r\n", pos ? "\r\n" : "", secname);
            if (w < 0 || (size_t)w >= cap - pos) return -2;
            pos += (size_t)w;
        }
        int w = snprintf(buf + pos, cap - pos, "%s=%s\r\n", ini->e[i].key, ini->e[i].val);
        if (w < 0 || (size_t)w >= cap - pos) return -2;
        pos += (size_t)w;
    }
    return (int)pos;
}

/* ---------------- Look section (schema v2: floats stored directly) ---------- */
/* v1 stored gamma as int (100 = 1.00) and had no schema key.  The loader
 * accepts both: a value with a '.' is v2, a bare int for gamma is v1/100. */

#define PX_CFG_SCHEMA 2

static inline int px_ini_has_section(const PxIni *ini, const char *sec)
{
    if (!ini) return 0;
    for (int i = 0; i < ini->n; i++)
        if (px_ci_eq(ini->e[i].sec, sec)) return 1;
    return 0;
}

static inline void px_cfg_load_look(const PxIni *ini, const char *sec, Look *out)
{
    /* every field defaults to NEUTRAL, so a corrupt/absent section can only
     * produce a harmless identity look, never garbage */
    *out = (Look)LOOK_NEUTRAL_INIT;
    out->enabled     = px_ini_get_int(ini, sec, "enabled", 1) ? 1 : 0;
    out->sat         = px_ini_get_flt(ini, sec, "sat", 100.0f);
    out->vibrance    = px_ini_get_flt(ini, sec, "vibrance", 100.0f);
    out->bri         = px_ini_get_flt(ini, sec, "bri", 100.0f);
    out->con         = px_ini_get_flt(ini, sec, "con", 100.0f);
    {
        const char *raw = px_ini_get(ini, sec, "gamma");
        if (raw && *raw && !strchr(raw, '.') && !strchr(raw, 'e') && !strchr(raw, 'E')) {
            long iv = px_ini_get_int(ini, sec, "gamma", 100);      /* v1: percent */
            out->gamma = iv > 0 ? (float)iv / 100.0f : 1.0f;
        } else {
            out->gamma = px_ini_get_flt(ini, sec, "gamma", 1.0f);  /* v2: float  */
        }
    }
    out->temp        = px_ini_get_flt(ini, sec, "temp", 6500.0f);
    out->tint        = px_ini_get_flt(ini, sec, "tint", 0.0f);
    out->r_gain      = px_ini_get_flt(ini, sec, "r_gain", 100.0f);
    out->g_gain      = px_ini_get_flt(ini, sec, "g_gain", 100.0f);
    out->b_gain      = px_ini_get_flt(ini, sec, "b_gain", 100.0f);
    out->shadows     = px_ini_get_flt(ini, sec, "shadows", 100.0f);
    out->highlights  = px_ini_get_flt(ini, sec, "highlights", 100.0f);
    out->black_level = px_ini_get_flt(ini, sec, "black_level", 100.0f);
    out->white_point = px_ini_get_flt(ini, sec, "white_point", 100.0f);
    out->clarity     = px_ini_get_flt(ini, sec, "clarity", 100.0f);
    out->hue         = px_ini_get_flt(ini, sec, "hue", 0.0f);
    cm_sanitize_look(out);
}

static inline void px_cfg_store_look(PxIni *ini, const char *sec, const Look *lk)
{
    Look c = *lk;
    cm_sanitize_look(&c);
    px_ini_set_int(ini, sec, "enabled", c.enabled);
    px_ini_set_dbl(ini, sec, "sat", c.sat);
    px_ini_set_dbl(ini, sec, "vibrance", c.vibrance);
    px_ini_set_dbl(ini, sec, "bri", c.bri);
    px_ini_set_dbl(ini, sec, "con", c.con);
    px_ini_set_dbl(ini, sec, "gamma", c.gamma);
    px_ini_set_dbl(ini, sec, "temp", c.temp);
    px_ini_set_dbl(ini, sec, "tint", c.tint);
    px_ini_set_dbl(ini, sec, "r_gain", c.r_gain);
    px_ini_set_dbl(ini, sec, "g_gain", c.g_gain);
    px_ini_set_dbl(ini, sec, "b_gain", c.b_gain);
    px_ini_set_dbl(ini, sec, "shadows", c.shadows);
    px_ini_set_dbl(ini, sec, "highlights", c.highlights);
    px_ini_set_dbl(ini, sec, "black_level", c.black_level);
    px_ini_set_dbl(ini, sec, "white_point", c.white_point);
    px_ini_set_dbl(ini, sec, "clarity", c.clarity);
    px_ini_set_dbl(ini, sec, "hue", c.hue);
}

/* ---------------- schema stamp & migration ---------------------------------- */
static inline int px_cfg_schema(const PxIni *ini)
{
    return px_ini_get_int(ini, "meta", "schema", 0);
}

/* Migrate anything older to the current schema IN MEMORY; returns the number
 * of edits made.  v1 → v2: gamma already auto-detected by the loader; this
 * only has to stamp the file (writers persist the normalized float form). */
static inline int px_cfg_migrate(PxIni *ini)
{
    int edits = 0;
    int v = px_cfg_schema(ini);
    if (v <= 0) {
        /* legacy file: clamp whatever was stored into the v2 domain NOW so the
         * stamp is honest about the data underneath it */
        Look lk;
        px_cfg_load_look(ini, "current", &lk);
        px_cfg_store_look(ini, "current", &lk);
        edits++;
        px_ini_set_int(ini, "meta", "schema", PX_CFG_SCHEMA);
        edits++;
    }
    return edits;
}

/* ---------------- packed Look (one INI line per sub-mode) ------------------
 * Format: enabled,sat,vibrance,bri,con,gamma,temp,tint,r,g,b,shadows,
 *         highlights,black,white,clarity,hue — 17 %g fields, missing tail =
 *         neutral.  Every field is strtod-checked and range-sanitised, so a
 *         hand-edited / corrupt / truncated value can only produce a safe
 *         look, never NaN or out-of-range hardware numbers. */
static inline void px_look_pack(const Look *lk, char *buf, size_t cap)
{
    Look c = *lk;
    cm_sanitize_look(&c);
    snprintf(buf, cap,
             "%d,%g,%g,%g,%g,%g,%g,%g,%g,%g,%g,%g,%g,%g,%g,%g,%g",
             c.enabled, c.sat, c.vibrance, c.bri, c.con, (double)c.gamma,
             c.temp, c.tint, c.r_gain, c.g_gain, c.b_gain, c.shadows,
             c.highlights, c.black_level, c.white_point, c.clarity, c.hue);
}

static inline int px_look_unpack(const char *s, Look *lk)
{
    *lk = (Look)LOOK_NEUTRAL_INIT;
    if (!s || !*s) return 0;
    float fields[17];
    int got = 0;
    const char *p = s;
    while (got < 17) {
        char *end = 0;
        double v = strtod(p, &end);
        if (!end || end == p) break;
        if (v != v || v > 1e30 || v < -1e30) v = 0;
        fields[got++] = (float)v;
        p = end;
        while (*p == ',' || *p == ' ') p++;
        if (!*p) break;
    }
    if (got >= 1)  lk->enabled     = fields[0] ? 1 : 0;
    if (got >= 2)  lk->sat         = fields[1];
    if (got >= 3)  lk->vibrance    = fields[2];
    if (got >= 4)  lk->bri         = fields[3];
    if (got >= 5)  lk->con         = fields[4];
    if (got >= 6)  lk->gamma       = fields[5];
    if (got >= 7)  lk->temp        = fields[6];
    if (got >= 8)  lk->tint        = fields[7];
    if (got >= 9)  lk->r_gain      = fields[8];
    if (got >= 10) lk->g_gain      = fields[9];
    if (got >= 11) lk->b_gain      = fields[10];
    if (got >= 12) lk->shadows     = fields[11];
    if (got >= 13) lk->highlights  = fields[12];
    if (got >= 14) lk->black_level = fields[13];
    if (got >= 15) lk->white_point = fields[14];
    if (got >= 16) lk->clarity     = fields[15];
    if (got >= 17) lk->hue         = fields[16];
    cm_sanitize_look(lk);
    return got;
}

/* ---------------- Game profile records (persisted by game_preset_manager) --
 * ONE [profileN] section per game.  BUILT-IN profiles persist only the mutable
 * fields (the code table is the source of truth for their looks); CUSTOM
 * profiles persist every sub-mode as `subN=name|packed-look`.  All reads are
 * clamped + sanitised.  Strings are UTF-8; the Win32 side converts. */

#define PX_PROFS_MAX   32
#define PX_SUBS_MAX    12
#define PX_ALIAS_MAX   4       /* extra executable names per profile (see game_profile.h) */

typedef struct PxSubRec {
    char name[32];
    Look look;
} PxSubRec;

typedef struct PxProfRec {
    char    name[64];
    char    exe[96];
    char    exe_alias[PX_ALIAS_MAX][96];   /* launcher / store / shipping variants  */
    char    exe_path[180];                 /* custom executable path               */
    int     alias_count;
    char    tag[32];
    int     is_custom;
    int     favorite;
    int     enabled;                       /* 0 = detection skips this profile     */
    int     auto_apply;
    int     auto_restore;
    int     delay_ms;
    int     apply_display;
    int     looks_edited;
    int     hdr_preference;
    int     monitor_idx;                   /* -1 = default/all                     */
    unsigned long last_activated;          /* unix seconds, 0 = never              */
    unsigned apply_count;
    int     target_res_w, target_res_h, target_hz;
    int     sub_count;
    int     active_sub;
    PxSubRec sub[PX_SUBS_MAX];
} PxProfRec;

static inline int px_prof_section(int idx, char *buf, size_t cap)
{
    if (idx < 0 || idx >= PX_PROFS_MAX) return -1;
    snprintf(buf, cap, "profile%d", idx);
    return 0;
}

/* Encode one record (overwrites its section). */
static inline void px_prof_put(PxIni *ini, int idx, const PxProfRec *r)
{
    char sec[PXINI_SECTION];
    if (px_prof_section(idx, sec, sizeof sec) != 0) return;
    px_ini_clear_section(ini, sec);
    px_ini_set(ini, sec, "name", r->name);
    px_ini_set(ini, sec, "exe",  r->exe);
    px_ini_set(ini, sec, "tag",  r->tag);
    {
        int na = r->alias_count;
        if (na < 0) na = 0;
        if (na > PX_ALIAS_MAX) na = PX_ALIAS_MAX;
        px_ini_set_int(ini, sec, "aliases", na);
        for (int i = 0; i < na; i++) {
            char key[PXINI_KEY];
            snprintf(key, sizeof key, "alias%d", i);
            px_ini_set(ini, sec, key, r->exe_alias[i]);
        }
    }
    px_ini_set(ini, sec, "exe_path", r->exe_path);
    px_ini_set_int(ini, sec, "custom", r->is_custom);
    px_ini_set_int(ini, sec, "fav", r->favorite);
    px_ini_set_int(ini, sec, "enabled", r->enabled ? 1 : 0);
    px_ini_set_int(ini, sec, "mon", r->monitor_idx);
    px_ini_set_int(ini, sec, "auto", r->auto_apply);
    px_ini_set_int(ini, sec, "appldisp", r->apply_display);
    px_ini_set_int(ini, sec, "looks", r->looks_edited);
    px_ini_set_int(ini, sec, "restore", r->auto_restore);
    px_ini_set_int(ini, sec, "delay", r->delay_ms);
    px_ini_set_int(ini, sec, "hdr", r->hdr_preference);
    px_ini_set_int(ini, sec, "resw", r->target_res_w);
    px_ini_set_int(ini, sec, "resh", r->target_res_h);
    px_ini_set_int(ini, sec, "hz", r->target_hz);
    if (r->last_activated) {
        char lb[32];
        snprintf(lb, sizeof lb, "%lu", r->last_activated);
        px_ini_set(ini, sec, "last", lb);
    }
    if (r->apply_count) {
        char ab[32];
        snprintf(ab, sizeof ab, "%u", r->apply_count);
        px_ini_set(ini, sec, "applies", ab);
    }
    {
        int n = r->sub_count;
        if (n < 0) n = 0;
        if (n > PX_SUBS_MAX) n = PX_SUBS_MAX;
        px_ini_set_int(ini, sec, "subs", n);
        int act = r->active_sub;
        if (act < 0 || act >= (n > 0 ? n : 1)) act = 0;
        px_ini_set_int(ini, sec, "act", act);

        if (r->is_custom || r->looks_edited) {  /* full look data when the user owns/edited it */
            char line[PXINI_VALUE];
            for (int sb = 0; sb < n; sb++) {
                char key[PXINI_KEY];
                snprintf(key, sizeof key, "sub%d", sb);
                px_ini_set(ini, sec, key, r->sub[sb].name);
                {
                    char vk[PXINI_KEY];
                    snprintf(vk, sizeof vk, "sub%d.look", sb);
                    px_look_pack(&r->sub[sb].look, line, sizeof line);
                    px_ini_set(ini, sec, vk, line);
                }
            }
        }
    }
}

/* Decode one record.  Returns 1 when the section exists. */
static inline int px_prof_get(const PxIni *ini, int idx, PxProfRec *r)
{
    char sec[PXINI_SECTION];
    if (px_prof_section(idx, sec, sizeof sec) != 0) return 0;
    if (!px_ini_has_section(ini, sec)) return 0;
    memset(r, 0, sizeof *r);
    {
        const char *v;
        if ((v = px_ini_get(ini, sec, "name"))) { size_t i = 0; for (; v[i] && i + 1 < sizeof r->name; i++) r->name[i] = v[i]; }
        if ((v = px_ini_get(ini, sec, "exe")))  { size_t i = 0; for (; v[i] && i + 1 < sizeof r->exe; i++) r->exe[i] = v[i]; }
        if ((v = px_ini_get(ini, sec, "tag")))  { size_t i = 0; for (; v[i] && i + 1 < sizeof r->tag; i++) r->tag[i] = v[i]; }
    }
    {
        /* exe aliases: bounded count, each entry clamped, duplicates dropped */
        const char *v = 0;
        int na = px_ini_get_int(ini, sec, "aliases", 0);
        if (na < 0) na = 0;
        if (na > PX_ALIAS_MAX) na = PX_ALIAS_MAX;
        for (int i = 0; i < na; i++) {
            char key[PXINI_KEY];
            const char *av;
            snprintf(key, sizeof key, "alias%d", i);
            av = px_ini_get(ini, sec, key);
            if (!av || !av[0]) continue;
            {
                size_t n = 0;
                for (; av[n] && n + 1 < sizeof r->exe_alias[0]; n++)
                    r->exe_alias[r->alias_count][n] = av[n];
                r->exe_alias[r->alias_count][n] = 0;
                r->alias_count++;
            }
        }
        if ((v = px_ini_get(ini, sec, "exe_path"))) {
            size_t i = 0;
            for (; v[i] && i + 1 < sizeof r->exe_path; i++) r->exe_path[i] = v[i];
        }
    }
    r->is_custom      = px_ini_get_int(ini, sec, "custom", 0) ? 1 : 0;
    r->favorite       = px_ini_get_int(ini, sec, "fav", 0) ? 1 : 0;
    r->enabled        = px_ini_get_int(ini, sec, "enabled", 1) ? 1 : 0;
    r->monitor_idx    = px_ini_get_int(ini, sec, "mon", -1);
    if (r->monitor_idx < -1) r->monitor_idx = -1;
    if (r->monitor_idx > 7) r->monitor_idx = 7;
    r->last_activated = (unsigned long)px_ini_get_int(ini, sec, "last", 0);
    {
        int ac = px_ini_get_int(ini, sec, "applies", 0);
        r->apply_count = (ac > 0) ? (unsigned)ac : 0u;
    }
    r->auto_apply     = px_ini_get_int(ini, sec, "auto", r->is_custom ? 1 : 0) ? 1 : 0;
    r->auto_restore   = px_ini_get_int(ini, sec, "restore", 1) ? 1 : 0;
    r->apply_display  = px_ini_get_int(ini, sec, "appldisp", 0) ? 1 : 0;
    r->looks_edited   = px_ini_get_int(ini, sec, "looks", 0) ? 1 : 0;
    r->delay_ms       = px_ini_get_int(ini, sec, "delay", 0);
    if (r->delay_ms < 0) r->delay_ms = 0;
    if (r->delay_ms > 3000) r->delay_ms = 3000;
    r->hdr_preference = px_ini_get_int(ini, sec, "hdr", 0);
    if (r->hdr_preference < 0 || r->hdr_preference > 2) r->hdr_preference = 0;
    r->target_res_w   = px_ini_get_int(ini, sec, "resw", 0);
    r->target_res_h   = px_ini_get_int(ini, sec, "resh", 0);
    r->target_hz      = px_ini_get_int(ini, sec, "hz", 0);
    r->sub_count      = px_ini_get_int(ini, sec, "subs", 1);
    if (r->sub_count < 1) r->sub_count = 1;
    if (r->sub_count > PX_SUBS_MAX) r->sub_count = PX_SUBS_MAX;
    r->active_sub     = px_ini_get_int(ini, sec, "act", 0);
    if (r->active_sub < 0 || r->active_sub >= r->sub_count) r->active_sub = 0;
    for (int sb = 0; sb < r->sub_count; sb++) {
        char key[PXINI_KEY], vk[PXINI_KEY];
        snprintf(key, sizeof key, "sub%d", sb);
        snprintf(vk, sizeof vk, "sub%d.look", sb);
        const char *v = px_ini_get(ini, sec, key);
        if (v) { size_t i = 0; for (; v[i] && i + 1 < sizeof r->sub[sb].name; i++) r->sub[sb].name[i] = v[i]; }
        else snprintf(r->sub[sb].name, sizeof r->sub[sb].name, "Mode %d", sb + 1);
        v = px_ini_get(ini, sec, vk);
        if (v) px_look_unpack(v, &r->sub[sb].look);
        else r->sub[sb].look = (Look)LOOK_NEUTRAL_INIT;
    }
    return 1;
}

#endif /* PLEXUSX_SETTINGS_STORE_H */
