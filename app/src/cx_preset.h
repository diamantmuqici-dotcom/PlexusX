/* cx_preset.h \u2014 ChromaX profile/preset: documented JSON schema, v2
 *
 * {
 *   "app": "ChromaX",          // must be present on import
 *   "version": 2,              // schema version (int)
 *   "name": "Rust Night",      // required, <= 63 chars
 *   "category": "game",        // look|game|display|monitor|custom (optional)
 *   "color": {                 // all optional, clamped on import
 *     "sat":235,"vibrance":120,"brightness":108,"contrast":112,"gamma":0.92,
 *     "temperature":6800,"tint":-4,"red":102,"green":100,"blue":98,
 *     "shadows":124,"highlights":96,"blacklevel":104,"whitepoint":100,
 *     "sharpness":110,"clarity":105,"intensity":110,"hue":0,"dehaze":104,
 *     "enabled":1
 *   },
 *   "crosshair": {             // optional
 *     "on":0,"shape":0,"size":16,"gap":4,"thick":2,"opacity":100,
 *     "outline":1,"color":13369344,"ocolor":0,"rotation":0,"dot":0
 *   },
 *   "display": { "w":2560, "h":1440, "hz":0 },   // 0/absent = don't touch
 *   "automation": { "on_exit":1, "delay_ms":1500 }
 * }
 *
 * Imported content is pure data: values are clamped into engine ranges,
 * unknown keys are ignored, names are sanitised. Nothing is executable.
 */
#ifndef CX_PRESET_H
#define CX_PRESET_H

#include "cx_color.h"
#include <stdint.h>
#include <stddef.h>

#define CX_PRESET_SCHEMA 2
#define CX_PRESET_MAX_NAME 64

typedef struct CxPreset {
    char name[CX_PRESET_MAX_NAME];
    char category[32];
    CxLook look;
    int    xh_on, xh_shape, xh_size, xh_gap, xh_thick, xh_opacity, xh_outline;
    uint32_t xh_color, xh_ocolor;
    int    xh_rotation, xh_dot;
    int    disp_w, disp_h, disp_hz;
    int    auto_on_exit, delay_ms;
    int    version;
} CxPreset;

void CxPreset_Default(CxPreset *p, const char *name);

/* returns 0 if the preset is sane (all values in range); fills err otherwise */
int  CxPreset_Validate(const CxPreset *p, char *err, size_t errsz);

/* serialize (malloc'd, caller frees) */
char *CxPreset_ToJson(const CxPreset *p);          /* compact */
int   CxPreset_WriteFile(const char *path, const CxPreset *p); /* indented */

/* import from text or file. Returns 0 on success. Malformed / out-of-range
 * input is rejected with a diagnostic \u2014 never partially applied. */
int  CxPreset_FromJson(const char *text, size_t len, CxPreset *out, char *err, size_t errsz);
int  CxPreset_ImportFile(const char *path, CxPreset *out, char *err, size_t errsz);

#endif
