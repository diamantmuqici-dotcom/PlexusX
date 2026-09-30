/* cx_scene.h \u2014 original, procedurally generated preview scenes
 *
 * All preview imagery is generated in code (gradients, silhouettes, noise)
 * so no copyrighted artwork is bundled.  The same buffer is used by the
 * before/after live preview that runs the real colour-engine maths.
 */
#ifndef CX_SCENE_H
#define CX_SCENE_H

#include <stdint.h>

enum {
    CX_SCENE_FOREST = 0,
    CX_SCENE_NIGHT,
    CX_SCENE_SNOW,
    CX_SCENE_DESERT,
    CX_SCENE_CITY,
    CX_SCENE_SKY,
    CX_SCENE_SMOKE,
    CX_SCENE_INDOOR,
    CX_SCENE_OUTDOOR,
    CX_SCENE_DARKROOM,
    CX_N_SCENE
};

void        CxScene_Name(int i, char *out, int sz);
/* fills w*h RGBA pixels; deterministic for a given (which, w, h) */
void        CxScene_Generate(int which, uint8_t *rgba, int w, int h);

#endif
