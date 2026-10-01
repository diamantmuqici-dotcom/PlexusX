/* PlexusX — GameProfile: the per-game profile model + pure profile algebra.
 *
 * A profile is the complete display recipe for one title:
 *
 *   identity    name, tag, category, favorite, enabled
 *   matching    primary exe + up to PX_PROF_ALIASES extra executable names
 *               (launchers, shipping builds, store variants) + an optional
 *               full custom executable path
 *   look        up to MAX_SUB_MODES named sub-modes (Competitive / Night / ...)
 *   automation  auto_apply, auto_restore, delay_ms, monitor target,
 *               optional display/hdr preference
 *   history     last_activated (unix seconds), apply_count
 *
 * The model lives here — free of <windows.h> — so the matching, duplication,
 * renaming and JSON import/export rules are unit-tested on any host instead of
 * being buried in the Win32 preset manager.  `game_preset_manager.c` owns the
 * live table; this header owns the semantics.
 *
 * Versioned JSON (single profile or a whole library) is the import/export
 * format: {"plexusx_profile":1,...} / {"plexusx_library":1,"profiles":[...]}.
 * The reader is deliberately lenient — every numeric field is range-clamped and
 * every string is bounded, so a hand-edited or hostile file can never produce a
 * NaN look or an out-of-range monitor index.
 */
#ifndef PLEXUSX_GAME_PROFILE_H
#define PLEXUSX_GAME_PROFILE_H

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#include "../color/look.h"
#include "../color/color_math.h"

#define MAX_SUB_MODES     12
#define PX_PROF_NAME_LEN  48
#define PX_PROF_EXE_LEN   96
#define PX_PROF_PATH_LEN  180
#define PX_PROF_TAG_LEN   32
#define PX_PROF_ALIASES   4      /* extra executable names beyond the primary */
#define PX_PROF_SUB_NAME  32
#define PX_PROF_MAX_SUBS  MAX_SUB_MODES
#define PX_PROFILE_JSON_VERSION 1

typedef struct SubMode {
    wchar_t name[PX_PROF_SUB_NAME];
    Look    look;
} SubMode;

typedef struct Profile {
    wchar_t  name[PX_PROF_NAME_LEN];
    wchar_t  exe[PX_PROF_EXE_LEN];                       /* primary exe name (base name, no path) */
    wchar_t  exe_alias[PX_PROF_ALIASES][PX_PROF_EXE_LEN];/* extra executable names               */
    int      alias_count;
    wchar_t  exe_path[PX_PROF_PATH_LEN];                 /* optional full path (custom games)     */
    wchar_t  tag[PX_PROF_TAG_LEN];
    int      is_custom;      /* 1 = user created / imported */
    int      favorite;
    int      enabled;        /* 0 = detection ignores this profile entirely */
    int      sub_count;
    int      active_sub;

    SubMode  sub[MAX_SUB_MODES];

    /* per-game display preference (0 = leave the desktop mode alone) */
    int      target_res_w, target_res_h, target_hz;
    int      hdr_preference; /* 0 = keep, 1 = prefer SDR, 2 = prefer HDR */
    int      monitor_idx;    /* -1 = all/primary, >= 0 = that monitor */

    /* automation */
    int      auto_apply;     /* apply on launch            */
    int      auto_restore;   /* restore global on exit     */
    int      delay_ms;       /* 0..3000 ms before applying */
    int      apply_display;  /* 1 = also apply target_res/hz on activation (opt-in) */
    int      looks_edited;   /* 1 = the user edited a built-in look (persist it) */

    /* history (diagnostics / library cards) */
    unsigned long last_activated;  /* unix seconds, 0 = never */
    unsigned      apply_count;
} Profile;

static inline void PxProf_Init(Profile *p)
{
    memset(p, 0, sizeof *p);
    p->enabled = 1;
    p->monitor_idx = -1;
    p->sub_count = 1;
    p->active_sub = 0;
    p->auto_apply = 1;
    p->auto_restore = 1;
    p->sub[0].look = (Look)LOOK_NEUTRAL_INIT;
}

/* ---------------- string helpers (bounded, always NUL-terminated) ---------- */
static inline void PxW_Copy(wchar_t *dst, size_t cap, const wchar_t *src)
{
    size_t i = 0;
    if (!cap) return;
    if (src) for (; src[i] && i + 1 < cap; i++) dst[i] = src[i];
    dst[i] = 0;
}

static inline int PxW_EqCI(const wchar_t *a, const wchar_t *b)
{
    if (!a || !b) return a == b;
    while (*a && *b) {
        wchar_t ca = *a, cb = *b;
        if (ca >= L'A' && ca <= L'Z') ca = (wchar_t)(ca - L'A' + L'a');
        if (cb >= L'A' && cb <= L'Z') cb = (wchar_t)(cb - L'A' + L'a');
        if (ca != cb) return 0;
        a++; b++;
    }
    return *a == *b;
}

/* Normalise whatever the OS handed us — "C:\Games\Rust\RustClient.EXE" or
 * "C:/Games/rustclient.exe" — to the comparable form every matcher uses:
 * base name only, lowercase, no quotes. */
static inline void PxProf_NormalizeExe(const wchar_t *raw, wchar_t *out, size_t cap)
{
    size_t n = 0, start = 0, i = 0;
    if (!out || !cap) return;
    out[0] = 0;
    if (!raw) return;
    if (raw[0] == L'"') {                       /* "\"C:\...\game.exe\"" */
        i = 1;
        start = 1;
        for (; raw[i] && raw[i] != L'"'; i++) {}
        n = i - start;
    } else {
        for (i = 0; raw[i]; i++) {
            if (raw[i] == L'\\' || raw[i] == L'/') start = i + 1;
        }
        n = i - start;
    }
    if (n + 1 > cap) n = cap - 1;
    for (i = 0; i < n; i++) {
        wchar_t c = raw[start + i];
        if (c >= L'A' && c <= L'Z') c = (wchar_t)(c - L'A' + L'a');
        out[i] = c;
    }
    out[n] = 0;
}

/* ---------------- exe matching / alias management ------------------------- */

/* Does this profile claim the given (already normalised OR raw) executable? */
static inline int PxProf_MatchesExe(const Profile *p, const wchar_t *exe_or_path)
{
    wchar_t norm[PX_PROF_EXE_LEN * 2];
    int i;
    if (!p || !exe_or_path || !exe_or_path[0]) return 0;
    PxProf_NormalizeExe(exe_or_path, norm, sizeof norm / sizeof norm[0]);
    if (!norm[0]) return 0;
    if (p->exe[0] && PxW_EqCI(p->exe, norm)) return 1;
    for (i = 0; i < p->alias_count && i < PX_PROF_ALIASES; i++) {
        if (p->exe_alias[i][0] && PxW_EqCI(p->exe_alias[i], norm)) return 1;
    }
    /* a custom executable path matches its own base name too */
    if (p->exe_path[0]) {
        wchar_t pbase[PX_PROF_EXE_LEN * 2];
        PxProf_NormalizeExe(p->exe_path, pbase, sizeof pbase / sizeof pbase[0]);
        if (pbase[0] && PxW_EqCI(pbase, norm)) return 1;
    }
    return 0;
}

/* Add an executable name (primary slot first, then aliases).  Returns the alias
 * slot used, -1 on duplicate/no room/invalid. */
static inline int PxProf_AddExe(Profile *p, const wchar_t *exe)
{
    wchar_t norm[PX_PROF_EXE_LEN];
    int i;
    if (!p || !exe) return -1;
    PxProf_NormalizeExe(exe, norm, sizeof norm / sizeof norm[0]);
    if (!norm[0]) return -1;
    if (!p->exe[0]) { PxW_Copy(p->exe, PX_PROF_EXE_LEN, norm); return 0; }
    if (PxW_EqCI(p->exe, norm)) return -1;
    for (i = 0; i < p->alias_count && i < PX_PROF_ALIASES; i++) {
        if (PxW_EqCI(p->exe_alias[i], norm)) return -1;
    }
    if (p->alias_count >= PX_PROF_ALIASES) return -1;
    PxW_Copy(p->exe_alias[p->alias_count], PX_PROF_EXE_LEN, norm);
    p->alias_count++;
    return p->alias_count - 1;
}

/* Remove alias slot i and compact.  Returns 1 when something was removed. */
static inline int PxProf_RemoveExe(Profile *p, int slot)
{
    int i;
    if (!p || slot < 0 || slot >= p->alias_count) return 0;
    for (i = slot; i < p->alias_count - 1; i++)
        PxW_Copy(p->exe_alias[i], PX_PROF_EXE_LEN, p->exe_alias[i + 1]);
    if (p->alias_count > 0) {
        p->alias_count--;
        p->exe_alias[p->alias_count][0] = 0;
    }
    return 1;
}

/* Total executable names this profile answers to (primary included). */
static inline int PxProf_ExeCount(const Profile *p)
{
    if (!p) return 0;
    return (p->exe[0] ? 1 : 0) + (p->alias_count > 0 ? p->alias_count : 0);
}

/* ---------------- construction / duplication ------------------------------ */

/* A brand-new custom profile with one sub-mode. */
static inline void PxProf_MakeCustom(const wchar_t *name, const wchar_t *exe,
                                     const wchar_t *tag, const Look *lk, Profile *out)
{
    Profile p;
    PxProf_Init(&p);
    PxW_Copy(p.name, PX_PROF_NAME_LEN, (name && name[0]) ? name : L"Custom Game");
    PxProf_NormalizeExe(exe, p.exe, PX_PROF_EXE_LEN);
    PxW_Copy(p.tag, PX_PROF_TAG_LEN, (tag && tag[0]) ? tag : L"Custom Game");
    p.is_custom = 1;
    p.sub_count = 1;
    PxW_Copy(p.sub[0].name, PX_PROF_SUB_NAME, L"Custom");
    p.sub[0].look = lk ? *lk : (Look)LOOK_NEUTRAL_INIT;
    cm_sanitize_look(&p.sub[0].look);
    *out = p;
}

/* Duplicate (name is generated by the caller; stays inside the bounds). */
static inline void PxProf_Duplicate(const Profile *src, const wchar_t *new_name, Profile *out)
{
    Profile p = *src;
    PxW_Copy(p.name, PX_PROF_NAME_LEN, new_name && new_name[0] ? new_name : src->name);
    p.is_custom = 1;                  /* a copy is the user's, even of a built-in */
    p.favorite = 0;
    p.apply_count = 0;
    p.last_activated = 0;
    *out = p;
}

/* Adopt another name in place (menu action "Rename"). */
static inline void PxProf_Rename(Profile *p, const wchar_t *new_name)
{
    if (p && new_name && new_name[0]) PxW_Copy(p->name, PX_PROF_NAME_LEN, new_name);
}

/* ---------------- UTF-8 <-> wide (pure; used by JSON + file IO) ------------ */
size_t PxW_ToUtf8(const wchar_t *src, char *dst, size_t cap);
size_t PxW_FromUtf8(const char *src, wchar_t *dst, size_t cap);

/* ---------------- JSON (versioned, lenient) -------------------------------- */
/* Serialise one profile (pretty, escaped, NUL-terminated).  Returns chars written. */
size_t PxProf_ToJson(const Profile *p, char *buf, size_t cap);
/* Parse one profile object.  Returns 1 on success (out sanitised), 0 on garbage. */
int    PxProf_FromJson(const char *text, Profile *out);

/* Serialise a whole library: {"plexusx_library":1,"count":N,"profiles":[...]}. */
size_t PxProf_LibraryToJson(const Profile *arr, int n, char *buf, size_t cap);
/* Parse a library.  Returns the number of profiles read (0 = nothing usable). */
int    PxProf_LibraryFromJson(const char *text, Profile *out, int max);

/* Bounded scan helpers exposed for the library reader and the tests. */
const char *PxJson_FindKey(const char *obj, const char *key);
int         PxJson_ReadString(const char *obj, const char *key, wchar_t *out, size_t cap);
int         PxJson_ReadInt(const char *obj, const char *key, int dflt);
double      PxJson_ReadNum(const char *obj, const char *key, double dflt);
int         PxJson_ReadBool(const char *obj, const char *key, int dflt);
/* Look as a compact object {"enabled":1,"sat":150,...}; unknown keys ignored. */
int         PxJson_ReadLook(const char *obj, const char *key, Look *out);

#endif /* PLEXUSX_GAME_PROFILE_H */
