/* PlexusX — ColorEngine: the single authoritative color orchestrator.
 *
 *      UI (sliders, presets, phone)          GameDetector (foreground facts)
 *                     │                                  │
 *                     ▼                                  ▼
 *               ColorState  ──── revision ────   GameDisplayState (game mode / idx)
 *                     │
 *                     ▼
 *             PxTransformPlan  (color_transform.h — the ONLY color math)
 *                     │
 *                     ▼
 *              ColorPipeline (DWM matrix + GPU ramps, color_pipeline.c)
 *                     │
 *                     ▼
 *             AppliedColorState (what the hardware CONFIRMED)
 *
 * Guarantees:
 *   - exactly ONE requested-state instance in the process (g_cs) — UI, phone,
 *     presets and hotkeys all write through it; nothing may keep a second
 *     "current" look.
 *   - a failed / partial hardware apply never rewrites the requested look.
 *   - resync after display events re-asserts REQUESTED state; the GPU cache is
 *     never a source of truth for the sliders (this is the ALT+TAB fix).
 *   - every transition lands in the event ring log (diagnostics).
 */
#ifndef PLEXUSX_COLOR_ENGINE_H
#define PLEXUSX_COLOR_ENGINE_H

#include <windows.h>

#include "../color/color_state.h"
#include "../color/applied_color_state.h"
#include "../color/color_pipeline.h"
#include "../diagnostics/diagnostics.h"

/* lifecycle (owned by main.c) */
void Eng_Init(void);
void Eng_Shutdown(void);
void Eng_SetPaths(const wchar_t *ramps, const wchar_t *dirty, int was_dirty);

/* authoritative requested-state access */
const ColorState *Eng_State(void);
void              Eng_SetLook(const Look *lk);       /* user edit → ColorState → apply */
void              Eng_SetMode(PxColorMode mode, int game_idx, int sub_idx);
const Look       *Eng_GetRequested(void);
void              Eng_Apply(const Look *lk);         /* legacy entry: set + apply */
void              Eng_ApplyNow(void);                /* apply current ColorState  */

/* applied / hardware truth */
const Look           *Eng_GetApplied(void);         /* NULL until first full apply */
const AppliedColorState *Eng_Applied(void);
int                   Eng_RequestedMatchesApplied(void);
int                   Eng_Available(void);          /* DWM path usable            */
const wchar_t        *Eng_LastInvalidateReason(void);
void                  Eng_Invalidate(const wchar_t *reason); /* forget hw cache + why */
void                  Eng_Resync(void);
void                  Eng_Reassert(void);           /* re-apply the requested look  */

/* targeting + recovery */
void Eng_SetTargetMonitor(int idx);
int  Eng_GetTargetMonitor(void);
void Eng_BackupCurrentState(void);
int  Eng_RestoreLastGood(void);
void Eng_Reset(void);                               /* forced identity + originals */

/* pure-math wrappers kept for UI preview / phone (same kernel, no second impl) */
void Eng_KelvinToRgb(float k, float *r, float *g, float *b);
void Eng_CalculateGammaRamp(const Look *lk, WORD ramp[3][256]);

/* diagnostics */
const struct PxLog *Eng_LogRing(void);
void Eng_Log(const char *tag, const char *fmt, ...);
/* fill the color half of the shared snapshot (display/game halves by their owners) */
void Eng_FillSnapshot(struct PxEngineSnapshot *s);

#endif /* PLEXUSX_COLOR_ENGINE_H */
