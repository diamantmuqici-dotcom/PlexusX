/* PlexusX — structured diagnostics log + engine snapshot (pure core)
 *
 * Diagnostics are EVENT-driven, never a polling printer: modules record one
 * entry per state transition (apply outcome, invalidate reason, display
 * change, game enter/exit, driver rejection), so a 24h session with idle
 * sliders writes nothing.  The fixed 128-entry ring drops the oldest entry;
 * rendering walks it in chronological order.  The snapshot struct is the
 * single data source shared by the native DiagnosticsPanel, the JSON exporter
 * and the status chips — they cannot disagree because they read the same
 * struct.
 */
#ifndef PLEXUSX_DIAGNOSTICS_H
#define PLEXUSX_DIAGNOSTICS_H

#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include "../color/look.h"
#include "../color/applied_color_state.h"
#include "../games/game_display_state.h"
#include "../display/display_state.h"

#define PXLOG_TAG 12
#define PXLOG_MSG 120
#define PXLOG_CAP 128

typedef struct PxLogEntry {
    unsigned ms;                 /* GetTickCount() when recorded             */
    char     tag[PXLOG_TAG];     /* "eng" | "dwm" | "gpu" | "disp" | "game" | "cfg" | "ui" */
    char     msg[PXLOG_MSG];
} PxLogEntry;

typedef struct PxLog {
    PxLogEntry e[PXLOG_CAP];
    int head;                    /* next write slot   */
    int count;                   /* entries retained  */
    unsigned dropped;            /* entries overwritten */
} PxLog;

static inline void PxLog_Init(PxLog *l) { memset(l, 0, sizeof *l); }

static inline void PxLog_Add(PxLog *l, unsigned now_ms, const char *tag, const char *msg)
{
    if (!l || !tag || !msg) return;
    PxLogEntry *en = &l->e[l->head];
    en->ms = now_ms;
    {
        size_t i = 0;
        for (; tag[i] && i + 1 < PXLOG_TAG; i++) en->tag[i] = tag[i];
        en->tag[i] = 0;
        for (i = 0; msg[i] && i + 1 < PXLOG_MSG; i++) en->msg[i] = msg[i];
        en->msg[i] = 0;
    }
    l->head = (l->head + 1) % PXLOG_CAP;
    if (l->count < PXLOG_CAP) l->count++;
    else l->dropped++;
}

/* Add with printf formatting (host + MSVCRT both have vsnprintf). */
static inline void PxLog_Fmt(PxLog *l, unsigned now_ms, const char *tag, const char *fmt, ...)
{
    char buf[PXLOG_MSG];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    buf[sizeof buf - 1] = 0;
    PxLog_Add(l, now_ms, tag, buf);
}

/* JSON array, chronological order, safely escaped (only " \ and control chars
 * occur in our own messages).  Returns the number of bytes written. */
/* append one char if it fits; returns 0 on overflow so the caller can bail */
static inline int pxlog_putc(char *buf, size_t cap, size_t *pos, char c)
{
    if (*pos + 1 >= cap) return 0;
    buf[(*pos)++] = c;
    return 1;
}

/* JSON string value with full escaping: quotes, backslashes, control chars.
 * Log messages embed exe names and file fragments; malformed JSON in a crash
 * report would be worse than a truncated one. */
static inline int pxlog_put_str(char *buf, size_t cap, size_t *pos, const char *s)
{
    for (; s && *s; s++) {
        unsigned char c = (unsigned char)*s;
        if (c == '"' || c == '\\') {
            if (!pxlog_putc(buf, cap, pos, '\\')) return 0;
            if (!pxlog_putc(buf, cap, pos, (char)c)) return 0;
        } else if (c == '\n') {
            if (!pxlog_putc(buf, cap, pos, '\\')) return 0;
            if (!pxlog_putc(buf, cap, pos, 'n')) return 0;
        } else if (c == '\r') {
            if (!pxlog_putc(buf, cap, pos, '\\')) return 0;
            if (!pxlog_putc(buf, cap, pos, 'r')) return 0;
        } else if (c == '\t') {
            if (!pxlog_putc(buf, cap, pos, '\\')) return 0;
            if (!pxlog_putc(buf, cap, pos, 't')) return 0;
        } else if (c < 0x20) {
            char u[7];
            snprintf(u, sizeof u, "\\u%04x", c);
            for (int i = 0; u[i]; i++) if (!pxlog_putc(buf, cap, pos, u[i])) return 0;
        } else {
            if (!pxlog_putc(buf, cap, pos, (char)c)) return 0;
        }
    }
    return 1;
}

static inline int pxlog_put_dec(char *buf, size_t cap, size_t *pos, unsigned v)
{
    char d[12];
    int n = snprintf(d, sizeof d, "%u", v);
    for (int i = 0; i < n; i++) if (!pxlog_putc(buf, cap, pos, d[i])) return 0;
    return 1;
}

static inline int PxLog_RenderJson(const PxLog *l, char *buf, size_t cap, int max_entries)
{
    if (!l || !buf || !cap) return 0;
    size_t pos = 0;
    int first = 1;
    int n = l->count;
    if (max_entries > 0 && n > max_entries) n = max_entries;
    /* keep the LAST n entries, walk them chronologically */
    int start = (l->head - n + PXLOG_CAP * 2) % PXLOG_CAP;
    if (!pxlog_putc(buf, cap, &pos, '[')) { buf[0] = 0; return 0; }
    for (int i = 0; i < n; i++) {
        const PxLogEntry *en = &l->e[(start + i) % PXLOG_CAP];
        size_t mark = pos;
        if (!first && !pxlog_putc(buf, cap, &pos, ',')) break;
        int ok = pxlog_putc(buf, cap, &pos, '{');
        if (ok) for (const char *k = "\"ms\":"; *k && ok; k++) ok = pxlog_putc(buf, cap, &pos, *k);
        if (ok) ok = pxlog_put_dec(buf, cap, &pos, en->ms);
        if (ok) for (const char *k = ",\"tag\":\""; *k && ok; k++) ok = pxlog_putc(buf, cap, &pos, *k);
        if (ok) ok = pxlog_put_str(buf, cap, &pos, en->tag);
        if (ok) ok = pxlog_putc(buf, cap, &pos, '"');
        if (ok) for (const char *k = ",\"msg\":\""; *k && ok; k++) ok = pxlog_putc(buf, cap, &pos, *k);
        if (ok) ok = pxlog_put_str(buf, cap, &pos, en->msg);
        if (ok) ok = pxlog_putc(buf, cap, &pos, '"');
        if (ok) ok = pxlog_putc(buf, cap, &pos, '}');
        if (!ok) { pos = mark; break; }              /* never emit a half entry */
        first = 0;
    }
    if (!pxlog_putc(buf, cap, &pos, ']')) { buf[0] = 0; return 0; }
    buf[pos] = 0;
    return (int)pos;
}

/* ---------------- Engine snapshot (everything the diagnostics UI shows) ---- */
typedef struct PxEngineSnapshot {
    /* color halves */
    int        engine_ready;
    int        engine_enabled;          /* ColorState.requested.enabled        */
    unsigned   requested_revision;
    Look       requested;
    AppliedColorState applied;
    /* pipeline / hardware */
    int        mag_available;           /* DWM 5x5 path usable                 */
    int        ramp_paths;              /* displays with a working gamma path  */
    int        displays_total;
    int        target_display;          /* -1 = all                            */
    int        desktop_output;          /* 1 = matrix verified held by DWM     */
    /* display state (primary) */
    MonitorInfo monitor;
    GpuInfo     gpu;
    /* game state */
    PxGameDisplayState game;
    /* event log */
    unsigned   log_count;
    unsigned   log_dropped;
    unsigned   coalesced_events;        /* WindowManager dedup counter          */
} PxEngineSnapshot;

static inline void PxSnap_Init(PxEngineSnapshot *s) { memset(s, 0, sizeof *s); }

#endif /* PLEXUSX_DIAGNOSTICS_H */
