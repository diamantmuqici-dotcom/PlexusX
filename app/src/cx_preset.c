/* cx_preset.c \u2014 profile/preset serialization + strict import validation */
#include "cx_preset.h"
#include "cx_json.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

void CxPreset_Default(CxPreset *p, const char *name)
{
    memset(p, 0, sizeof *p);
    CxLook_Default(&p->look);
    if (name) {
        size_t n = strlen(name);
        if (n >= CX_PRESET_MAX_NAME) n = CX_PRESET_MAX_NAME - 1;
        memcpy(p->name, name, n);
    }
    if (!p->name[0]) strcpy(p->name, "Untitled");
    strcpy(p->category, "custom");
    p->xh_shape = 1; p->xh_size = 16; p->xh_gap = 4; p->xh_thick = 2;
    p->xh_opacity = 100; p->xh_outline = 1;
    p->xh_color = 0x7CFF40; p->xh_ocolor = 0;
    p->version = CX_PRESET_SCHEMA;
}

int CxPreset_Validate(const CxPreset *p, char *err, size_t errsz)
{
    CxLook c = p->look;
    CxLook_Clamp(&c);
    float f;
    for (int i = 0; i < CXLOOK_NPARAMS; i++) {
        f = CxLook_Get(&c, i);
        if (f < -1001.f || f > 10001.f) {
            if (errsz) snprintf(err, errsz, "color.%s out of range", CxLook_ParamName(i));
            return -1;
        }
    }
    if (!p->name[0]) { if (errsz) snprintf(err, errsz, "missing name"); return -1; }
    if (p->xh_size < 2 || p->xh_size > 96 || p->xh_thick < 1 || p->xh_thick > 16 ||
        p->xh_gap < 0 || p->xh_gap > 48 || p->xh_opacity < 0 || p->xh_opacity > 100) {
        if (errsz) snprintf(err, errsz, "crosshair values out of range");
        return -1;
    }
    if (p->disp_w && (p->disp_w < 320 || p->disp_w > 12800 ||
                      p->disp_h < 240 || p->disp_h > 7200 ||
                      p->disp_hz < 0 || p->disp_hz > 1000)) {
        if (errsz) snprintf(err, errsz, "display mode out of range");
        return -1;
    }
    if (p->delay_ms < 0 || p->delay_ms > 30000) {
        if (errsz) snprintf(err, errsz, "automation.delay_ms out of range");
        return -1;
    }
    return 0;
}

static CxJson *look_obj(const CxLook *l)
{
    CxJson *o = CxJson_NewObj();
    for (int i = 0; i < CXLOOK_NPARAMS; i++)
        CxJson_ObjSet(o, CxLook_ParamName(i), CxJson_NewNum(CxLook_Get(l, i)));
    CxJson_ObjSet(o, "enabled", CxJson_NewBool(l->enabled));
    return o;
}

char *CxPreset_ToJson(const CxPreset *p)
{
    CxJson *root = CxJson_NewObj();
    CxJson_ObjSet(root, "app", CxJson_NewStr("ChromaX"));
    CxJson_ObjSet(root, "version", CxJson_NewNum(p->version ? p->version : CX_PRESET_SCHEMA));
    CxJson_ObjSet(root, "name", CxJson_NewStr(p->name));
    CxJson_ObjSet(root, "category", CxJson_NewStr(p->category));
    CxJson_ObjSet(root, "color", look_obj(&p->look));

    CxJson *xh = CxJson_NewObj();
    CxJson_ObjSet(xh, "on", CxJson_NewBool(p->xh_on));
    CxJson_ObjSet(xh, "shape", CxJson_NewNum(p->xh_shape));
    CxJson_ObjSet(xh, "size", CxJson_NewNum(p->xh_size));
    CxJson_ObjSet(xh, "gap", CxJson_NewNum(p->xh_gap));
    CxJson_ObjSet(xh, "thick", CxJson_NewNum(p->xh_thick));
    CxJson_ObjSet(xh, "opacity", CxJson_NewNum(p->xh_opacity));
    CxJson_ObjSet(xh, "outline", CxJson_NewBool(p->xh_outline));
    CxJson_ObjSet(xh, "color", CxJson_NewNum(p->xh_color));
    CxJson_ObjSet(xh, "ocolor", CxJson_NewNum(p->xh_ocolor));
    CxJson_ObjSet(xh, "rotation", CxJson_NewNum(p->xh_rotation));
    CxJson_ObjSet(xh, "dot", CxJson_NewBool(p->xh_dot));
    CxJson_ObjSet(root, "crosshair", xh);

    CxJson *d = CxJson_NewObj();
    CxJson_ObjSet(d, "w", CxJson_NewNum(p->disp_w));
    CxJson_ObjSet(d, "h", CxJson_NewNum(p->disp_h));
    CxJson_ObjSet(d, "hz", CxJson_NewNum(p->disp_hz));
    CxJson_ObjSet(root, "display", d);

    CxJson *a = CxJson_NewObj();
    CxJson_ObjSet(a, "on_exit", CxJson_NewBool(p->auto_on_exit));
    CxJson_ObjSet(a, "delay_ms", CxJson_NewNum(p->delay_ms));
    CxJson_ObjSet(root, "automation", a);

    char *s = CxJson_WriteStr(root);
    CxJson_Free(root);
    return s;
}

int CxPreset_WriteFile(const char *path, const CxPreset *p)
{
    CxJson *root = CxJson_NewObj();
    CxJson_ObjSet(root, "app", CxJson_NewStr("ChromaX"));
    CxJson_ObjSet(root, "version", CxJson_NewNum(p->version ? p->version : CX_PRESET_SCHEMA));
    CxJson_ObjSet(root, "name", CxJson_NewStr(p->name));
    CxJson_ObjSet(root, "category", CxJson_NewStr(p->category));
    CxJson_ObjSet(root, "color", look_obj(&p->look));
    CxJson *xh = CxJson_NewObj();
    CxJson_ObjSet(xh, "on", CxJson_NewBool(p->xh_on));
    CxJson_ObjSet(xh, "shape", CxJson_NewNum(p->xh_shape));
    CxJson_ObjSet(xh, "size", CxJson_NewNum(p->xh_size));
    CxJson_ObjSet(xh, "gap", CxJson_NewNum(p->xh_gap));
    CxJson_ObjSet(xh, "thick", CxJson_NewNum(p->xh_thick));
    CxJson_ObjSet(xh, "opacity", CxJson_NewNum(p->xh_opacity));
    CxJson_ObjSet(xh, "outline", CxJson_NewBool(p->xh_outline));
    CxJson_ObjSet(xh, "color", CxJson_NewNum(p->xh_color));
    CxJson_ObjSet(xh, "ocolor", CxJson_NewNum(p->xh_ocolor));
    CxJson_ObjSet(xh, "rotation", CxJson_NewNum(p->xh_rotation));
    CxJson_ObjSet(xh, "dot", CxJson_NewBool(p->xh_dot));
    CxJson_ObjSet(root, "crosshair", xh);
    CxJson *d = CxJson_NewObj();
    CxJson_ObjSet(d, "w", CxJson_NewNum(p->disp_w));
    CxJson_ObjSet(d, "h", CxJson_NewNum(p->disp_h));
    CxJson_ObjSet(d, "hz", CxJson_NewNum(p->disp_hz));
    CxJson_ObjSet(root, "display", d);
    CxJson *a = CxJson_NewObj();
    CxJson_ObjSet(a, "on_exit", CxJson_NewBool(p->auto_on_exit));
    CxJson_ObjSet(a, "delay_ms", CxJson_NewNum(p->delay_ms));
    CxJson_ObjSet(root, "automation", a);

    FILE *f = fopen(path, "wb");
    if (!f) { CxJson_Free(root); return -1; }
    int rc = CxJson_WriteF(f, root, 2);
    fputc('\n', f);
    fclose(f);
    CxJson_Free(root);
    return rc;
}

/* ---------------- import ---------------- */

static void look_from_obj(CxLook *l, const CxJson *o)
{
    for (int i = 0; i < CXLOOK_NPARAMS; i++)
        if (CxJson_IsNum(o, CxLook_ParamName(i)))
            CxLook_Set(l, i, (float)CxJson_GetNum(o, CxLook_ParamName(i), CxLook_Get(l, i)));
    l->enabled = CxJson_GetBool(o, "enabled", l->enabled);
}

static int clampi(int v, int a, int b) { return v < a ? a : (v > b ? b : v); }

int CxPreset_FromJson(const char *text, size_t len, CxPreset *out, char *err, size_t errsz)
{
    CxJson *root = NULL;
    if (CxJson_Parse(text, len, &root, err, errsz)) return -1;
    if (!root || root->type != CXJ_OBJ) {
        CxJson_Free(root);
        if (errsz) snprintf(err, errsz, "root must be an object");
        return -1;
    }
    if (strcmp(CxJson_GetStr(root, "app", ""), "ChromaX") != 0) {
        CxJson_Free(root);
        if (errsz) snprintf(err, errsz, "not a ChromaX preset file");
        return -1;
    }

    CxPreset p;
    CxPreset_Default(&p, CxJson_GetStr(root, "name", "Imported"));
    p.version = (int)CxJson_GetNum(root, "version", CX_PRESET_SCHEMA);

    /* sanitise name: printable ASCII only */
    for (size_t i = 0; p.name[i]; i++) {
        unsigned char c = (unsigned char)p.name[i];
        if (c < 0x20 || c >= 0x7f) p.name[i] = '?';
    }

    const char *cat = CxJson_GetStr(root, "category", "custom");
    size_t cn = strlen(cat);
    if (cn > sizeof p.category - 1) cn = sizeof p.category - 1;
    memcpy(p.category, cat, cn); p.category[cn] = 0;

    look_from_obj(&p.look, CxJson_Get(root, "color"));

    CxJson *xh = CxJson_Get(root, "crosshair");
    if (xh && xh->type == CXJ_OBJ) {
        p.xh_on      = clampi(CxJson_GetBool(xh, "on", 0), 0, 1);
        p.xh_shape   = clampi((int)CxJson_GetNum(xh, "shape", p.xh_shape), 0, 9);
        p.xh_size    = clampi((int)CxJson_GetNum(xh, "size", p.xh_size), 2, 96);
        p.xh_gap     = clampi((int)CxJson_GetNum(xh, "gap", p.xh_gap), 0, 48);
        p.xh_thick   = clampi((int)CxJson_GetNum(xh, "thick", p.xh_thick), 1, 16);
        p.xh_opacity = clampi((int)CxJson_GetNum(xh, "opacity", p.xh_opacity), 0, 100);
        p.xh_outline = clampi(CxJson_GetBool(xh, "outline", p.xh_outline), 0, 1);
        p.xh_color   = (uint32_t)CxJson_GetNum(xh, "color", p.xh_color);
        p.xh_ocolor  = (uint32_t)CxJson_GetNum(xh, "ocolor", p.xh_ocolor);
        p.xh_rotation= clampi((int)CxJson_GetNum(xh, "rotation", 0), 0, 359);
        p.xh_dot     = clampi(CxJson_GetBool(xh, "dot", 0), 0, 1);
    }

    CxJson *d = CxJson_Get(root, "display");
    if (d && d->type == CXJ_OBJ) {
        p.disp_w = (int)CxJson_GetNum(d, "w", 0);
        p.disp_h = (int)CxJson_GetNum(d, "h", 0);
        p.disp_hz = (int)CxJson_GetNum(d, "hz", 0);
    }

    CxJson *a = CxJson_Get(root, "automation");
    if (a && a->type == CXJ_OBJ) {
        p.auto_on_exit = clampi(CxJson_GetBool(a, "on_exit", 0), 0, 1);
        p.delay_ms = clampi((int)CxJson_GetNum(a, "delay_ms", 0), 0, 30000);
    }

    CxJson_Free(root);
    CxLook_Clamp(&p.look);
    if (CxPreset_Validate(&p, err, errsz)) return -1;
    *out = p;
    return 0;
}

int CxPreset_ImportFile(const char *path, CxPreset *out, char *err, size_t errsz)
{
    FILE *f = fopen(path, "rb");
    if (!f) { if (errsz) snprintf(err, errsz, "cannot open %s", path); return -1; }
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz < 0 || sz > 256 * 1024) {
        fclose(f);
        if (errsz) snprintf(err, errsz, "file too large or invalid");
        return -1;
    }
    char *buf = (char *)malloc((size_t)sz + 1);
    if (!buf) { fclose(f); return -1; }
    size_t rd = fread(buf, 1, (size_t)sz, f);
    fclose(f);
    buf[rd] = 0;
    int rc = CxPreset_FromJson(buf, rd, out, err, errsz);
    free(buf);
    return rc;
}
