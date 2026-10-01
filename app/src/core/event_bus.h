/* PlexusX — Event Bus (pure, header-only)
 *
 * Decouples modules without polling: every state change is an explicit event
 * with a reason.  The bus is synchronous (no threads) — handlers run inline
 * on the UI thread, so no locks are needed.  Events that arrive in a burst
 * are coalesced by WindowManager before they reach the engine.
 *
 * Events:
 *   EV_FOREGROUND_CHANGED  — foreground window/process changed
 *   EV_DISPLAY_CHANGED     — monitor topology / mode changed
 *   EV_COLOR_REQUESTED     — user changed requested look
 *   EV_COLOR_APPLIED       — engine confirmed apply (or failure)
 *   EV_GAME_ENTER/EXIT     — game profile activation/restoration
 *   EV_SAFE_RESET          — emergency reset triggered
 *   EV_CONFIG_DIRTY        — persistence needed
 *
 * Pure C, no windows.h, testable on host.
 */

#ifndef PLEXUSX_EVENT_BUS_H
#define PLEXUSX_EVENT_BUS_H

#include <stddef.h>
#include <string.h>
#include <stdint.h>

typedef enum PxEventType {
    PX_EV_NONE = 0,
    PX_EV_FOREGROUND_CHANGED,
    PX_EV_DISPLAY_CHANGED,
    PX_EV_DEVICE_CHANGED,
    PX_EV_SESSION_CHANGED,
    PX_EV_RESUME,
    PX_EV_COLOR_REQUESTED,
    PX_EV_COLOR_APPLIED,
    PX_EV_COLOR_FAILED,
    PX_EV_GAME_ENTER,
    PX_EV_GAME_EXIT,
    PX_EV_GAME_SWITCH,
    PX_EV_SAFE_RESET,
    PX_EV_CONFIG_DIRTY,
    PX_EV_DISPLAY_MODE_CHANGING,
    PX_EV_DISPLAY_MODE_CHANGED,
    PX_EV_PHONE_COMMAND,
    PX_EV_CROSSHAIR_TOGGLED,
    PX_EV_ENGINE_TOGGLED,
    PX_EV_PRESET_LOADED,
    PX_EV_PROFILE_LOADED,
    PX_EV_COUNT
} PxEventType;

static inline const char *PxEvent_Name(PxEventType t)
{
    switch (t) {
    case PX_EV_NONE:                    return "NONE";
    case PX_EV_FOREGROUND_CHANGED:      return "FOREGROUND_CHANGED";
    case PX_EV_DISPLAY_CHANGED:         return "DISPLAY_CHANGED";
    case PX_EV_DEVICE_CHANGED:          return "DEVICE_CHANGED";
    case PX_EV_SESSION_CHANGED:         return "SESSION_CHANGED";
    case PX_EV_RESUME:                  return "RESUME";
    case PX_EV_COLOR_REQUESTED:         return "COLOR_REQUESTED";
    case PX_EV_COLOR_APPLIED:           return "COLOR_APPLIED";
    case PX_EV_COLOR_FAILED:            return "COLOR_FAILED";
    case PX_EV_GAME_ENTER:              return "GAME_ENTER";
    case PX_EV_GAME_EXIT:               return "GAME_EXIT";
    case PX_EV_GAME_SWITCH:             return "GAME_SWITCH";
    case PX_EV_SAFE_RESET:              return "SAFE_RESET";
    case PX_EV_CONFIG_DIRTY:            return "CONFIG_DIRTY";
    case PX_EV_DISPLAY_MODE_CHANGING:   return "DISPLAY_MODE_CHANGING";
    case PX_EV_DISPLAY_MODE_CHANGED:    return "DISPLAY_MODE_CHANGED";
    case PX_EV_PHONE_COMMAND:           return "PHONE_COMMAND";
    case PX_EV_CROSSHAIR_TOGGLED:       return "CROSSHAIR_TOGGLED";
    case PX_EV_ENGINE_TOGGLED:          return "ENGINE_TOGGLED";
    case PX_EV_PRESET_LOADED:           return "PRESET_LOADED";
    case PX_EV_PROFILE_LOADED:          return "PROFILE_LOADED";
    default:                            return "UNKNOWN";
    }
}

typedef struct PxEvent {
    PxEventType type;
    unsigned    timestamp_ms;
    int         int_data;               /* profile idx, mode idx, etc */
    unsigned    flags;
    char        reason[64];             /* human reason for diagnostics */
    char        str_data[96];           /* exe name, preset name, etc */
} PxEvent;

static inline void PxEvent_Init(PxEvent *e, PxEventType t, unsigned now_ms, const char *reason)
{
    if (!e) return;
    memset(e, 0, sizeof *e);
    e->type = t;
    e->timestamp_ms = now_ms;
    if (reason) {
        size_t i = 0;
        for (; reason[i] && i + 1 < sizeof e->reason; i++) e->reason[i] = reason[i];
        e->reason[i] = 0;
    }
}

/* Simple ring buffer for event history (diagnostics) */
#define PX_EVENT_HISTORY_CAP 64

typedef struct PxEventHistory {
    PxEvent events[PX_EVENT_HISTORY_CAP];
    int head;
    int count;
    unsigned dropped;
} PxEventHistory;

static inline void PxEventHistory_Init(PxEventHistory *h)
{
    if (!h) return;
    memset(h, 0, sizeof *h);
}

static inline void PxEventHistory_Push(PxEventHistory *h, const PxEvent *e)
{
    if (!h || !e) return;
    h->events[h->head] = *e;
    h->head = (h->head + 1) % PX_EVENT_HISTORY_CAP;
    if (h->count < PX_EVENT_HISTORY_CAP) h->count++;
    else h->dropped++;
}

/* Event bus — synchronous, single-threaded */
#define PX_EVENT_MAX_HANDLERS 16

typedef void (*PxEventHandler)(const PxEvent *e, void *userdata);

typedef struct PxEventBus {
    struct {
        PxEventType     type;           /* PX_EV_NONE = any */
        PxEventHandler  handler;
        void           *userdata;
    } handlers[PX_EVENT_MAX_HANDLERS];
    int handler_count;
    PxEventHistory history;
    unsigned emitted;
} PxEventBus;

static inline void PxEventBus_Init(PxEventBus *bus)
{
    if (!bus) return;
    memset(bus, 0, sizeof *bus);
    PxEventHistory_Init(&bus->history);
}

static inline int PxEventBus_Subscribe(PxEventBus *bus, PxEventType type,
                                       PxEventHandler h, void *userdata)
{
    if (!bus || !h || bus->handler_count >= PX_EVENT_MAX_HANDLERS) return -1;
    bus->handlers[bus->handler_count].type = type;
    bus->handlers[bus->handler_count].handler = h;
    bus->handlers[bus->handler_count].userdata = userdata;
    bus->handler_count++;
    return 0;
}

static inline void PxEventBus_Emit(PxEventBus *bus, const PxEvent *e)
{
    if (!bus || !e) return;
    PxEventHistory_Push(&bus->history, e);
    bus->emitted++;
    for (int i = 0; i < bus->handler_count; i++) {
        if (bus->handlers[i].type == PX_EV_NONE || bus->handlers[i].type == e->type) {
            bus->handlers[i].handler(e, bus->handlers[i].userdata);
        }
    }
}

static inline void PxEventBus_EmitSimple(PxEventBus *bus, PxEventType t,
                                         unsigned now_ms, const char *reason)
{
    PxEvent e;
    PxEvent_Init(&e, t, now_ms, reason);
    PxEventBus_Emit(bus, &e);
}

#endif /* PLEXUSX_EVENT_BUS_H */
