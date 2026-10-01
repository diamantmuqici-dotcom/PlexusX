/* PlexusX — AppliedColorState (header only, platform independent)
 *
 * What PlexusX ACTUALLY managed to put on the display — kept strictly apart
 * from the requested ColorState.  A failed or partial apply NEVER rewrites the
 * user's requested numbers:
 *
 *   Requested: vibrance 250   Applied: (nothing yet)          outcome NOT_YET
 *   Requested: vibrance 250   Applied: vibrance 250           outcome FULL
 *   Requested: vibrance 250   Applied: game path unavailable  outcome PARTIAL
 *
 * `revision` records which ColorState revision the applied block reflects, so
 * "in sync" is provable instead of assumed: in sync  <=>  have && revision
 * matches && outcome == FULL.  matrix_ok / ramps_ok report the two independent
 * hardware halves (DWM 5x5 filter and the per-display gamma LUT).
 */
#ifndef PLEXUSX_APPLIED_COLOR_STATE_H
#define PLEXUSX_APPLIED_COLOR_STATE_H

#include <string.h>

#include "look.h"

typedef enum PxApplyOutcome {
    PX_APPLY_NOT_YET = 0,   /* engine has not finished an apply          */
    PX_APPLY_FULL    = 1,   /* matrix + every display ramp acknowledged   */
    PX_APPLY_PARTIAL = 2,   /* one half of the pipeline failed            */
    PX_APPLY_FAILED  = 3    /* neither half could be applied              */
} PxApplyOutcome;

typedef struct AppliedColorState {
    int      have;              /* 1 = the fields below are meaningful     */
    Look     look;              /* last look fully confirmed on hardware   */
    unsigned revision;          /* ColorState revision this reflects       */
    PxApplyOutcome outcome;
    int      matrix_ok;         /* DWM accepted (or held) the effect        */
    int      matrix_verified;   /* the readback said so, not just the call  */
    int      ramps_ok;          /* every needed SetDeviceGammaRamp ack'd    */
    int      ramp_writes;       /* LUT writes performed by the last apply   */
    int      matrix_skipped;    /* unchanged: no MagSetFullscreenColorEffect*/
    unsigned applied_ms;        /* GetTickCount() at completion             */
    char     note[64];          /* ASCII reason on partial/failed           */
} AppliedColorState;

static inline void PxAS_Init(AppliedColorState *a)
{
    memset(a, 0, sizeof *a);
    a->outcome = PX_APPLY_NOT_YET;
}

/* Record a completed apply.  outcome is derived, never passed in by hand. */
static inline void PxAS_Record(AppliedColorState *a, const Look *lk, unsigned revision,
                               int matrix_ok, int matrix_verified, int ramps_ok,
                               int ramp_writes, int matrix_skipped,
                               unsigned now_ms, const char *note)
{
    a->have = 1;
    a->look = *lk;
    a->revision = revision;
    a->matrix_ok = matrix_ok ? 1 : 0;
    a->matrix_verified = matrix_verified ? 1 : 0;
    a->ramps_ok = ramps_ok ? 1 : 0;
    a->ramp_writes = ramp_writes;
    a->matrix_skipped = matrix_skipped ? 1 : 0;
    a->applied_ms = now_ms;
    if (note) {
        size_t i = 0;
        for (; note[i] && i + 1 < sizeof a->note; i++) a->note[i] = note[i];
        a->note[i] = 0;
    } else {
        a->note[0] = 0;
    }
    a->outcome = (a->matrix_ok && a->ramps_ok) ? PX_APPLY_FULL
               : (!a->matrix_ok && !a->ramps_ok) ? PX_APPLY_FAILED
               : PX_APPLY_PARTIAL;
}

static inline const char *PxAS_OutcomeName(PxApplyOutcome o)
{
    switch (o) {
    case PX_APPLY_FULL:    return "FULL";
    case PX_APPLY_PARTIAL: return "PARTIAL";
    case PX_APPLY_FAILED:  return "FAILED";
    default:               return "NOT APPLIED YET";
    }
}

#endif /* PLEXUSX_APPLIED_COLOR_STATE_H */
