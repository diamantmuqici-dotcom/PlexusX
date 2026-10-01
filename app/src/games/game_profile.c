/* PlexusX — GameProfile implementation: UTF-8 bridge, JSON codec, library IO.
 *
 * Pure C (no <windows.h>) so tests/test_all.c compiles the exact same code the
 * application ships.  The JSON reader is a bounded scanner, not a tree parser:
 * every read is length-checked against the object text handed in, every number
 * is rejected when non-finite, and each field lands in a clamped domain.  A
 * corrupt export can therefore never inject a NaN look or a bogus monitor.
 */
#include "game_profile.h"

#include <math.h>

/* ---------------- UTF-8 <-> wide ------------------------------------------ */

size_t PxW_ToUtf8(const wchar_t *src, char *dst, size_t cap)
{
    size_t o = 0;
    if (!dst || !cap) return 0;
    if (!src) { dst[0] = 0; return 0; }
    for (; *src; src++) {
        unsigned long cp = (unsigned long)*src;
        if (cp >= 0xD800 && cp <= 0xDBFF) {                    /* high surrogate */
            unsigned long lo = (unsigned long)src[1];
            if (lo >= 0xDC00 && lo <= 0xDFFF) {
                cp = 0x10000UL + ((cp - 0xD800UL) << 10) + (lo - 0xDC00UL);
                src++;
            } else {
                cp = 0xFFFDUL;
            }
        } else if (cp >= 0xDC00 && cp <= 0xDFFF) {
            cp = 0xFFFDUL;
        }
        if (cp < 0x80UL) {
            if (o + 1 >= cap) break;
            dst[o++] = (char)cp;
        } else if (cp < 0x800UL) {
            if (o + 2 >= cap) break;
            dst[o++] = (char)(0xC0 | (cp >> 6));
            dst[o++] = (char)(0x80 | (cp & 0x3F));
        } else if (cp < 0x10000UL) {
            if (o + 3 >= cap) break;
            dst[o++] = (char)(0xE0 | (cp >> 12));
            dst[o++] = (char)(0x80 | ((cp >> 6) & 0x3F));
            dst[o++] = (char)(0x80 | (cp & 0x3F));
        } else {
            if (o + 4 >= cap) break;
            dst[o++] = (char)(0xF0 | (cp >> 18));
            dst[o++] = (char)(0x80 | ((cp >> 12) & 0x3F));
            dst[o++] = (char)(0x80 | ((cp >> 6) & 0x3F));
            dst[o++] = (char)(0x80 | (cp & 0x3F));
        }
    }
    dst[o] = 0;
    return o;
}

size_t PxW_FromUtf8(const char *src, wchar_t *dst, size_t cap)
{
    size_t o = 0;
    if (!dst || !cap) return 0;
    if (!src) { dst[0] = 0; return 0; }
    while (*src) {
        unsigned long cp;
        unsigned char c = (unsigned char)*src;
        int extra;
        if (c < 0x80)       { cp = c;             extra = 0; }
        else if ((c & 0xE0) == 0xC0) { cp = c & 0x1F; extra = 1; }
        else if ((c & 0xF0) == 0xE0) { cp = c & 0x0F; extra = 2; }
        else if ((c & 0xF8) == 0xF0) { cp = c & 0x07; extra = 3; }
        else { cp = 0xFFFDUL; extra = 0; }
        src++;
        for (int i = 0; i < extra; i++) {
            if ((*src & 0xC0) != 0x80) { cp = 0xFFFDUL; break; }
            cp = (cp << 6) | (unsigned long)((unsigned char)*src & 0x3F);
            src++;
        }
        if (cp >= 0xD800UL && cp <= 0xDFFFUL) cp = 0xFFFDUL;
        if (cp >= 0x10000UL) {
            if (o + 2 >= cap) break;
            cp -= 0x10000UL;
            dst[o++] = (wchar_t)(0xD800UL + (cp >> 10));
            dst[o++] = (wchar_t)(0xDC00UL + (cp & 0x3FFUL));
        } else {
            if (o + 1 >= cap) break;
            dst[o++] = (wchar_t)cp;
        }
    }
    dst[o] = 0;
    return o;
}

/* ---------------- bounded JSON scan helpers -------------------------------- */

/* Skip whitespace. */
static const char *jx_ws(const char *p) { while (p && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')) p++; return p; }

/* Is the byte at p the start of a string (outside an escape)? */
static const char *jx_str_end(const char *p)
{
    if (!p || *p != '"') return NULL;
    p++;
    while (*p) {
        if (*p == '\\' && p[1]) { p += 2; continue; }
        if (*p == '"') return p;
        p++;
    }
    return NULL;
}

/* Find `"key"` at any depth and return the first non-space byte after ':'. */
const char *PxJson_FindKey(const char *obj, const char *key)
{
    const char *p = obj;
    size_t klen;
    if (!obj || !key) return NULL;
    klen = strlen(key);
    while (p && *p) {
        if (*p == '"') {
            const char *end = jx_str_end(p);
            if (!end) return NULL;
            if ((size_t)(end - p - 1) == klen && strncmp(p + 1, key, klen) == 0) {
                const char *q = jx_ws(end + 1);
                if (q && *q == ':') return jx_ws(q + 1);
            }
            p = end + 1;
            continue;
        }
        p++;
    }
    return NULL;
}

int PxJson_ReadString(const char *obj, const char *key, wchar_t *out, size_t cap)
{
    const char *v = PxJson_FindKey(obj, key);
    char utf8[512];
    size_t o = 0;
    if (!out || !cap) return 0;
    out[0] = 0;
    if (!v || *v != '"') return 0;
    v++;
    while (*v && *v != '"' && o + 1 < sizeof utf8) {
        if (*v == '\\' && v[1]) {
            v++;
            switch (*v) {
            case 'n': utf8[o++] = '\n'; break;
            case 't': utf8[o++] = '\t'; break;
            case 'r': utf8[o++] = '\r'; break;
            case '"': utf8[o++] = '"';  break;
            case '\\': utf8[o++] = '\\'; break;
            case '/': utf8[o++] = '/';  break;
            case 'u': {
                unsigned cp = 0;
                int i;
                for (i = 0; i < 4 && v[1 + i]; i++) {
                    char h = v[1 + i];
                    unsigned d = (h >= '0' && h <= '9') ? (unsigned)(h - '0')
                               : (h >= 'a' && h <= 'f') ? (unsigned)(h - 'a' + 10)
                               : (h >= 'A' && h <= 'F') ? (unsigned)(h - 'A' + 10) : 99u;
                    if (d > 15u) break;
                    cp = (cp << 4) | d;
                }
                v += i;
                if (i == 4) {
                    /* encode as UTF-8 (surrogates pass through as-is; the wide
                     * conversion in PxW_FromUtf8 maps lone surrogates to U+FFFD) */
                    if (cp < 0x80) utf8[o++] = (char)cp;
                    else if (cp < 0x800) {
                        utf8[o++] = (char)(0xC0 | (cp >> 6));
                        if (o + 1 < sizeof utf8) utf8[o++] = (char)(0x80 | (cp & 0x3F));
                    } else {
                        utf8[o++] = (char)(0xE0 | (cp >> 12));
                        if (o + 1 < sizeof utf8) utf8[o++] = (char)(0x80 | ((cp >> 6) & 0x3F));
                        if (o + 1 < sizeof utf8) utf8[o++] = (char)(0x80 | (cp & 0x3F));
                    }
                }
                break;
            }
            default: utf8[o++] = (char)*v; break;
            }
            v++;
            continue;
        }
        utf8[o++] = *v++;
    }
    utf8[o] = 0;
    PxW_FromUtf8(utf8, out, cap);
    return out[0] ? 1 : 0;
}

int PxJson_ReadInt(const char *obj, const char *key, int dflt)
{
    double d = PxJson_ReadNum(obj, key, (double)dflt);
    if (d > 2147483000.0) d = 2147483000.0;
    if (d < -2147483000.0) d = -2147483000.0;
    return (int)(d < 0 ? d - 0.5 : d + 0.5);
}

double PxJson_ReadNum(const char *obj, const char *key, double dflt)
{
    const char *v = PxJson_FindKey(obj, key);
    char *end = NULL;
    double d;
    if (!v) return dflt;
    if (*v == '"') {                        /* tolerate "150" */
        char tmp[64];
        size_t n = 0;
        v++;
        while (*v && *v != '"' && n + 1 < sizeof tmp) tmp[n++] = *v++;
        tmp[n] = 0;
        d = strtod(tmp, &end);
        if (end == tmp) return dflt;
    } else {
        d = strtod(v, &end);
        if (end == v) return dflt;
    }
    if (!(d == d) || d > 1e30 || d < -1e30) return dflt;    /* NaN / Inf / absurd */
    return d;
}

int PxJson_ReadBool(const char *obj, const char *key, int dflt)
{
    const char *v = PxJson_FindKey(obj, key);
    if (!v) return dflt ? 1 : 0;
    if (strncmp(v, "true", 4) == 0)  return 1;
    if (strncmp(v, "false", 5) == 0) return 0;
    return PxJson_ReadNum(obj, key, dflt ? 1.0 : 0.0) != 0.0 ? 1 : 0;
}

int PxJson_ReadLook(const char *obj, const char *key, Look *out)
{
    const char *v = PxJson_FindKey(obj, key);
    Look lk;
    if (!out) return 0;
    if (!v || *v != '{') return 0;
    lk = (Look)LOOK_NEUTRAL_INIT;
    lk.enabled     = PxJson_ReadBool(v, "enabled", 1);
    lk.sat         = (float)PxJson_ReadNum(v, "sat", 100.0);
    lk.vibrance    = (float)PxJson_ReadNum(v, "vibrance", 100.0);
    lk.bri         = (float)PxJson_ReadNum(v, "brightness", PxJson_ReadNum(v, "bri", 100.0));
    lk.con         = (float)PxJson_ReadNum(v, "contrast", PxJson_ReadNum(v, "con", 100.0));
    lk.gamma       = (float)PxJson_ReadNum(v, "gamma", 1.0);
    lk.temp        = (float)PxJson_ReadNum(v, "temperature", PxJson_ReadNum(v, "temp", 6500.0));
    lk.tint        = (float)PxJson_ReadNum(v, "tint", 0.0);
    lk.r_gain      = (float)PxJson_ReadNum(v, "r", 100.0);
    lk.g_gain      = (float)PxJson_ReadNum(v, "g", 100.0);
    lk.b_gain      = (float)PxJson_ReadNum(v, "b", 100.0);
    lk.shadows     = (float)PxJson_ReadNum(v, "shadows", 100.0);
    lk.highlights  = (float)PxJson_ReadNum(v, "highlights", 100.0);
    lk.black_level = (float)PxJson_ReadNum(v, "blackLevel", PxJson_ReadNum(v, "black_level", 100.0));
    lk.white_point = (float)PxJson_ReadNum(v, "whitePoint", PxJson_ReadNum(v, "white_point", 100.0));
    lk.clarity     = (float)PxJson_ReadNum(v, "clarity", 100.0);
    lk.hue         = (float)PxJson_ReadNum(v, "hue", 0.0);
    cm_sanitize_look(&lk);
    *out = lk;
    return 1;
}

/* ---------------- writer --------------------------------------------------- */

static size_t jw_puts(char *buf, size_t cap, size_t o, const char *s)
{
    while (s && *s) {
        if (o + 1 >= cap) break;
        buf[o++] = *s++;
    }
    if (cap) buf[o < cap ? o : cap - 1] = 0;
    return o;
}

static size_t jw_num(char *buf, size_t cap, size_t o, double v)
{
    char tmp[48];
    if (!(v == v) || v > 1e30 || v < -1e30) v = 0.0;
    snprintf(tmp, sizeof tmp, "%.4g", v);
    return jw_puts(buf, cap, o, tmp);
}

static size_t jw_str(char *buf, size_t cap, size_t o, const char *s)
{
    o = jw_puts(buf, cap, o, "\"");
    for (; s && *s; s++) {
        unsigned char c = (unsigned char)*s;
        if (c == '"' || c == '\\') {
            if (o + 2 >= cap) break;
            buf[o++] = '\\';
            buf[o++] = (char)c;
        } else if (c < 0x20) {
            char tmp[8];
            snprintf(tmp, sizeof tmp, "\\u%04x", c);
            o = jw_puts(buf, cap, o, tmp);
        } else {
            if (o + 1 >= cap) break;
            buf[o++] = (char)c;
        }
    }
    return jw_puts(buf, cap, o, "\"");
}

static size_t jw_wstr(char *buf, size_t cap, size_t o, const wchar_t *w)
{
    char utf8[1400];
    PxW_ToUtf8(w, utf8, sizeof utf8);
    return jw_str(buf, cap, o, utf8);
}

static size_t jw_look(char *buf, size_t cap, size_t o, const Look *lk)
{
    Look c = *lk;
    cm_sanitize_look(&c);
    o = jw_puts(buf, cap, o, "{");
    o = jw_puts(buf, cap, o, "\"enabled\":");    o = jw_num(buf, cap, o, c.enabled);
    o = jw_puts(buf, cap, o, ",\"sat\":");       o = jw_num(buf, cap, o, c.sat);
    o = jw_puts(buf, cap, o, ",\"vibrance\":");  o = jw_num(buf, cap, o, c.vibrance);
    o = jw_puts(buf, cap, o, ",\"brightness\":");o = jw_num(buf, cap, o, c.bri);
    o = jw_puts(buf, cap, o, ",\"contrast\":");  o = jw_num(buf, cap, o, c.con);
    o = jw_puts(buf, cap, o, ",\"gamma\":");     o = jw_num(buf, cap, o, c.gamma);
    o = jw_puts(buf, cap, o, ",\"temperature\":");o = jw_num(buf, cap, o, c.temp);
    o = jw_puts(buf, cap, o, ",\"tint\":");      o = jw_num(buf, cap, o, c.tint);
    o = jw_puts(buf, cap, o, ",\"r\":");         o = jw_num(buf, cap, o, c.r_gain);
    o = jw_puts(buf, cap, o, ",\"g\":");         o = jw_num(buf, cap, o, c.g_gain);
    o = jw_puts(buf, cap, o, ",\"b\":");         o = jw_num(buf, cap, o, c.b_gain);
    o = jw_puts(buf, cap, o, ",\"shadows\":");   o = jw_num(buf, cap, o, c.shadows);
    o = jw_puts(buf, cap, o, ",\"highlights\":");o = jw_num(buf, cap, o, c.highlights);
    o = jw_puts(buf, cap, o, ",\"blackLevel\":");o = jw_num(buf, cap, o, c.black_level);
    o = jw_puts(buf, cap, o, ",\"whitePoint\":");o = jw_num(buf, cap, o, c.white_point);
    o = jw_puts(buf, cap, o, ",\"clarity\":");   o = jw_num(buf, cap, o, c.clarity);
    o = jw_puts(buf, cap, o, ",\"hue\":");       o = jw_num(buf, cap, o, c.hue);
    return jw_puts(buf, cap, o, "}");
}

size_t PxProf_ToJson(const Profile *p, char *buf, size_t cap)
{
    size_t o = 0;
    if (!buf || !cap) return 0;
    buf[0] = 0;
    if (!p) return 0;
    o = jw_puts(buf, cap, o, "{\n  \"plexusx_profile\": ");
    o = jw_num(buf, cap, o, PX_PROFILE_JSON_VERSION);
    o = jw_puts(buf, cap, o, ",\n  \"name\": ");   o = jw_wstr(buf, cap, o, p->name);
    o = jw_puts(buf, cap, o, ",\n  \"tag\": ");    o = jw_wstr(buf, cap, o, p->tag);
    o = jw_puts(buf, cap, o, ",\n  \"exe\": ");    o = jw_wstr(buf, cap, o, p->exe);
    o = jw_puts(buf, cap, o, ",\n  \"exe_aliases\": [");
    for (int i = 0; i < p->alias_count && i < PX_PROF_ALIASES; i++) {
        if (i) o = jw_puts(buf, cap, o, ", ");
        o = jw_wstr(buf, cap, o, p->exe_alias[i]);
    }
    o = jw_puts(buf, cap, o, "]");
    o = jw_puts(buf, cap, o, ",\n  \"exe_path\": "); o = jw_wstr(buf, cap, o, p->exe_path);
    o = jw_puts(buf, cap, o, ",\n  \"enabled\": ");  o = jw_num(buf, cap, o, p->enabled ? 1 : 0);
    o = jw_puts(buf, cap, o, ",\n  \"favorite\": "); o = jw_num(buf, cap, o, p->favorite ? 1 : 0);
    o = jw_puts(buf, cap, o, ",\n  \"auto_apply\": ");   o = jw_num(buf, cap, o, p->auto_apply ? 1 : 0);
    o = jw_puts(buf, cap, o, ",\n  \"auto_restore\": "); o = jw_num(buf, cap, o, p->auto_restore ? 1 : 0);
    o = jw_puts(buf, cap, o, ",\n  \"delay_ms\": ");     o = jw_num(buf, cap, o, p->delay_ms);
    o = jw_puts(buf, cap, o, ",\n  \"monitor\": ");      o = jw_num(buf, cap, o, p->monitor_idx);
    o = jw_puts(buf, cap, o, ",\n  \"hdr_preference\": "); o = jw_num(buf, cap, o, p->hdr_preference);
    o = jw_puts(buf, cap, o, ",\n  \"target_res_w\": "); o = jw_num(buf, cap, o, p->target_res_w);
    o = jw_puts(buf, cap, o, ",\n  \"target_res_h\": "); o = jw_num(buf, cap, o, p->target_res_h);
    o = jw_puts(buf, cap, o, ",\n  \"target_hz\": ");    o = jw_num(buf, cap, o, p->target_hz);
    o = jw_puts(buf, cap, o, ",\n  \"active_sub\": ");   o = jw_num(buf, cap, o, p->active_sub);
    o = jw_puts(buf, cap, o, ",\n  \"subs\": [");
    for (int i = 0; i < p->sub_count && i < MAX_SUB_MODES; i++) {
        o = jw_puts(buf, cap, o, i ? ",\n    {\"name\": " : "\n    {\"name\": ");
        o = jw_wstr(buf, cap, o, p->sub[i].name);
        o = jw_puts(buf, cap, o, ", \"look\": ");
        o = jw_look(buf, cap, o, &p->sub[i].look);
        o = jw_puts(buf, cap, o, "}");
    }
    o = jw_puts(buf, cap, o, "\n  ]\n}\n");
    return o;
}

/* ---------------- reader --------------------------------------------------- */

int PxProf_FromJson(const char *text, Profile *out)
{
    Profile p;
    const char *subs;
    int i;
    if (!text || !out) return 0;
    if (!PxJson_FindKey(text, "name") && !PxJson_FindKey(text, "exe") &&
        !PxJson_FindKey(text, "plexusx_profile"))
        return 0;                                   /* not a profile object */

    PxProf_Init(&p);
    if (!PxJson_ReadString(text, "name", p.name, PX_PROF_NAME_LEN) || !p.name[0])
        PxW_Copy(p.name, PX_PROF_NAME_LEN, L"Imported Profile");
    if (!PxJson_ReadString(text, "exe", p.exe, PX_PROF_EXE_LEN) || !p.exe[0]) {
        /* Some exporters only carry the alias list: adopt its first entry. */
        wchar_t first[PX_PROF_EXE_LEN];
        const char *v = PxJson_FindKey(text, "exe_aliases");
        if (v && *v == '[') {
            const char *q = jx_ws(v + 1);
            if (q && *q == '"') {
                char tmp[256];
                size_t n = 0;
                q++;
                while (*q && *q != '"' && n + 1 < sizeof tmp) tmp[n++] = *q++;
                tmp[n] = 0;
                PxW_FromUtf8(tmp, first, PX_PROF_EXE_LEN);
                PxProf_NormalizeExe(first, p.exe, PX_PROF_EXE_LEN);
            }
        }
    } else {
        wchar_t norm[PX_PROF_EXE_LEN];
        PxProf_NormalizeExe(p.exe, norm, PX_PROF_EXE_LEN);
        PxW_Copy(p.exe, PX_PROF_EXE_LEN, norm);
    }
    PxJson_ReadString(text, "tag", p.tag, PX_PROF_TAG_LEN);
    if (!p.tag[0]) PxW_Copy(p.tag, PX_PROF_TAG_LEN, L"Imported");

    {   /* aliases array: read every string inside it */
        const char *v = PxJson_FindKey(text, "exe_aliases");
        if (v && *v == '[') {
            const char *q = v + 1;
            int guard = 0;
            while (*q && *q != ']' && guard++ < 64) {
                while (*q && *q != '"' && *q != ']') q++;
                if (*q != '"') break;
                {
                    char tmp[256];
                    size_t n = 0;
                    q++;
                    while (*q && *q != '"' && n + 1 < sizeof tmp) tmp[n++] = *q++;
                    tmp[n] = 0;
                    if (*q == '"') q++;
                    {
                        wchar_t w[PX_PROF_EXE_LEN];
                        PxW_FromUtf8(tmp, w, PX_PROF_EXE_LEN);
                        PxProf_AddExe(&p, w);
                    }
                }
            }
        }
    }

    PxJson_ReadString(text, "exe_path", p.exe_path, PX_PROF_PATH_LEN);
    p.enabled        = PxJson_ReadBool(text, "enabled", 1);
    p.favorite       = PxJson_ReadBool(text, "favorite", 0);
    p.auto_apply     = PxJson_ReadBool(text, "auto_apply", 1);
    p.auto_restore   = PxJson_ReadBool(text, "auto_restore", 1);
    p.delay_ms       = PxJson_ReadInt(text, "delay_ms", 0);
    p.monitor_idx    = PxJson_ReadInt(text, "monitor", -1);
    p.hdr_preference = PxJson_ReadInt(text, "hdr_preference", 0);
    p.target_res_w   = PxJson_ReadInt(text, "target_res_w", 0);
    p.target_res_h   = PxJson_ReadInt(text, "target_res_h", 0);
    p.target_hz      = PxJson_ReadInt(text, "target_hz", 0);
    p.is_custom      = 1;                       /* anything imported is user-owned */
    if (p.delay_ms < 0) p.delay_ms = 0;
    if (p.delay_ms > 3000) p.delay_ms = 3000;
    if (p.monitor_idx < -1) p.monitor_idx = -1;
    if (p.monitor_idx > 7) p.monitor_idx = 7;
    if (p.hdr_preference < 0 || p.hdr_preference > 2) p.hdr_preference = 0;
    if (p.target_res_w < 0) p.target_res_w = 0;
    if (p.target_res_h < 0) p.target_res_h = 0;
    if (p.target_hz < 0) p.target_hz = 0;

    /* subs: walk the array of objects */
    p.sub_count = 0;
    subs = PxJson_FindKey(text, "subs");
    if (subs && *subs == '[') {
        const char *q = subs + 1;
        int guard = 0;
        while (*q && p.sub_count < MAX_SUB_MODES && guard++ < 64) {
            const char *obj;
            const char *end;
            /* skip to the next '{' or the closing ']' */
            while (*q && *q != '{' && *q != ']') q++;
            if (*q != '{') break;
            obj = q;
            /* find the matching close brace (no nesting beyond one level in our own
             * objects except the look object — count braces to stay exact) */
            {
                int depth = 0;
                end = obj;
                while (*end) {
                    if (*end == '{') depth++;
                    else if (*end == '}') { depth--; if (depth == 0) { end++; break; } }
                    end++;
                }
            }
            {
                char tmp[2048];
                size_t n = (size_t)(end - obj);
                if (n >= sizeof tmp) n = sizeof tmp - 1;
                memcpy(tmp, obj, n);
                tmp[n] = 0;

                if (!PxJson_ReadString(tmp, "name", p.sub[p.sub_count].name, PX_PROF_SUB_NAME) ||
                    !p.sub[p.sub_count].name[0]) {
                    p.sub[p.sub_count].name[0] = L'M';
                    p.sub[p.sub_count].name[1] = (wchar_t)(L'0' + ((p.sub_count + 1) % 10));
                    p.sub[p.sub_count].name[2] = 0;
                }
                if (!PxJson_ReadLook(tmp, "look", &p.sub[p.sub_count].look))
                    p.sub[p.sub_count].look = (Look)LOOK_NEUTRAL_INIT;
                p.sub_count++;
            }
            q = end;
        }
    }
    if (p.sub_count == 0) {
        p.sub_count = 1;
        PxW_Copy(p.sub[0].name, PX_PROF_SUB_NAME, L"Custom");
        p.sub[0].look = (Look)LOOK_NEUTRAL_INIT;
        {   /* tolerate a flat "look" object at the top level */
            Look lk;
            if (PxJson_ReadLook(text, "look", &lk)) p.sub[0].look = lk;
        }
    }
    p.active_sub = PxJson_ReadInt(text, "active_sub", 0);
    if (p.active_sub < 0 || p.active_sub >= p.sub_count) p.active_sub = 0;

    for (i = 0; i < p.sub_count; i++) cm_sanitize_look(&p.sub[i].look);
    *out = p;
    return 1;
}

size_t PxProf_LibraryToJson(const Profile *arr, int n, char *buf, size_t cap)
{
    size_t o = 0;
    if (!buf || !cap) return 0;
    buf[0] = 0;
    if (n < 0) n = 0;
    o = jw_puts(buf, cap, o, "{\n  \"plexusx_library\": ");
    o = jw_num(buf, cap, o, PX_PROFILE_JSON_VERSION);
    o = jw_puts(buf, cap, o, ",\n  \"count\": ");
    o = jw_num(buf, cap, o, n);
    o = jw_puts(buf, cap, o, ",\n  \"profiles\": [\n");
    for (int i = 0; i < n; i++) {
        char one[4096];
        size_t len = PxProf_ToJson(&arr[i], one, sizeof one);
        (void)len;
        o = jw_puts(buf, cap, o, i ? ",\n" : "");
        /* indent the nested object by two spaces on each line */
        for (const char *s = one; *s; s++) {
            if (o + 2 >= cap) break;
            buf[o++] = *s;
            if (*s == '\n' && s[1]) buf[o++] = ' ';
        }
    }
    o = jw_puts(buf, cap, o, "\n  ]\n}\n");
    return o;
}

int PxProf_LibraryFromJson(const char *text, Profile *out, int max)
{
    const char *arr;
    const char *q;
    int n = 0;
    if (!text || !out || max <= 0) return 0;
    arr = PxJson_FindKey(text, "profiles");
    if (!arr || *arr != '[') {
        /* a single profile object is a valid library of one */
        return PxProf_FromJson(text, out) ? 1 : 0;
    }
    q = arr + 1;
    while (*q && n < max) {
        const char *obj = NULL, *end = NULL;
        while (*q && *q != '{' && *q != ']') q++;
        if (*q != '{') break;
        obj = q;
        end = obj;
        {
            int depth = 0;
            while (*end) {
                if (*end == '{') depth++;
                else if (*end == '}') { depth--; if (depth == 0) { end++; break; } }
                end++;
            }
        }
        {
            char tmp[4096];
            size_t len = (size_t)(end - obj);
            if (len >= sizeof tmp) len = sizeof tmp - 1;
            memcpy(tmp, obj, len);
            tmp[len] = 0;
            if (PxProf_FromJson(tmp, &out[n])) n++;
        }
        q = end;
    }
    return n;
}
