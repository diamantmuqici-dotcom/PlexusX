/* PlexusX — ColorPipeline interface (Win32 side).
 *
 * ColorPipeline connects the ColorTransform plan to the ACTUAL output
 * mechanisms PlexusX ships:
 *
 *   ColorState (requested)  →  PxTransformPlan (color_transform.h)
 *                            →  ColorPipeline
 *                                ├─ DWM 5x5 MAGCOLOREFFECT (MagSetFullscreenColorEffect)
 *                                │    = every LINEAR adjustment, desktop-composited content
 *                                └─ per-display 16-bit gamma LUT (SetDeviceGammaRamp)
 *                                     = only NON-LINEAR tone curves, scanout side
 *
 * It also owns crash recovery: the ORIGINAL LUT of every display is persisted
 * to ramps.dat (atomic write, validated parse), a dirty flag marks "the user
 * may be left with a modified display if we die", and a dirty start restores
 * the saved originals BEFORE anything else touches the GPU.
 */
#ifndef PLEXUSX_COLOR_PIPELINE_H
#define PLEXUSX_COLOR_PIPELINE_H

#include <windows.h>

#include "../color/color_transform.h"

typedef struct PxPipelineResult {
    int matrix_supported;   /* magnification path exists at all             */
    int matrix_sent;        /* MagSetFullscreenColorEffect was called       */
    int matrix_ok;          /* it accepted the effect (or nothing was due)  */
    int matrix_verified;    /* MagGetFullscreenColorEffect read it back     */
    int matrix_held;        /* readback matches what we sent                */
    int ramps_called;       /* SetDeviceGammaRamp invocations this apply     */
    int ramps_ok;           /* every needed write was acknowledged          */
    int displays_prepared;  /* display DCs with a usable gamma path         */
    int restore_only;       /* apply bypassed: this was a "give it back" pass */
} PxPipelineResult;

/* lifecycle ----------------------------------------------------------------- */
void PxPipe_SetPaths(const wchar_t *ramps, const wchar_t *dirty, int was_dirty);
int  PxPipe_Init(void);                 /* 1 = at least one output path ready */
void PxPipe_Shutdown(void);

/* apply / restore ----------------------------------------------------------- */
void PxPipe_Run(const PxTransformPlan *plan, PxPipelineResult *out);
void PxPipe_ForceReset(void);           /* emergency: identity + originals   */
void PxPipe_Resync(void);               /* forget hardware cache; force next */
int  PxPipe_HealthCheck(void);          /* 1 = hardware state was lost and needs re-apply */

/* state / targeting ---------------------------------------------------------- */
int  PxPipe_MagAvailable(void);
int  PxPipe_RampDisplays(void);
void PxPipe_SetTargetDisplay(int idx);  /* -1 = all displays */
int  PxPipe_GetTargetDisplay(void);
int  PxPipe_BackupOriginals(void);      /* ramps.dat rewrite; returns records */
int  PxPipe_RestoreOriginals(void);     /* crash-safe restore; returns records */

#endif /* PLEXUSX_COLOR_PIPELINE_H */
