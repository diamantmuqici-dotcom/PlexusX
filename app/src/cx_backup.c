/* cx_backup.c */
#include "cx_backup.h"
#include "cx_json.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int CxBackup_Fill(CxBackup *b, const CxLook *look, const char *source,
                  const CxMonState *mon, int nmon, long long ts)
{
    memset(b, 0, sizeof *b);
    b->ts = ts;
    if (look) b->look = *look;
    if (source) {
        size_t n = strlen(source);
        if (n >= sizeof b->source) n = sizeof b->source - 1;
        memcpy(b->source, source, n);
    }
    b->nmon = 0;
    if (mon && nmon > 0) {
        b->nmon = nmon > CX_BACKUP_MAX_MON ? CX_BACKUP_MAX_MON : nmon;
        memcpy(b->mon, mon, (size_t)b->nmon * sizeof b->mon[0]);
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

int CxBackup_ToJson(const CxBackup *b, char *buf, size_t sz)
{
    CxJson *root = CxJson_NewObj();
    CxJson_ObjSet(root, "app", CxJson_NewStr("ChromaX-backup"));
    CxJson_ObjSet(root, "ts", CxJson_NewNum((double)b->ts));
    CxJson_ObjSet(root, "source", CxJson_NewStr(b->source));
    CxJson_ObjSet(root, "color", look_obj(&b->look));
    CxJson *arr = CxJson_NewArr();
    for (int i = 0; i < b->nmon; i++) {
        CxJson *m = CxJson_NewObj();
        CxJson_ObjSet(m, "device", CxJson_NewStr(b->mon[i].device));
        CxJson_ObjSet(m, "id", CxJson_NewStr(b->mon[i].id));
        CxJson_ObjSet(m, "w", CxJson_NewNum(b->mon[i].w));
        CxJson_ObjSet(m, "h", CxJson_NewNum(b->mon[i].h));
        CxJson_ObjSet(m, "hz", CxJson_NewNum(b->mon[i].hz));
        CxJson_ObjSet(m, "hdr", CxJson_NewBool(b->mon[i].hdr));
        CxJson_ArrAdd(arr, m);
    }
    CxJson_ObjSet(root, "monitors", arr);

    char *s = CxJson_WriteStr(root);
    CxJson_Free(root);
    if (!s) return -1;
    int n = (int)strlen(s);
    if (sz > 0) {
        if ((size_t)n >= sz) n = (int)sz - 1;
        memcpy(buf, s, (size_t)n);
        buf[n] = 0;
    }
    free(s);
    return n;
}

int CxBackup_FromJson(const char *buf, size_t len, CxBackup *out, char *err, size_t errsz)
{
    CxJson *root = NULL;
    if (CxJson_Parse(buf, len, &root, err, errsz)) return -1;
    if (!root || root->type != CXJ_OBJ) { CxJson_Free(root); return -1; }
    if (strcmp(CxJson_GetStr(root, "app", ""), "ChromaX-backup") != 0) {
        CxJson_Free(root);
        if (errsz) snprintf(err, errsz, "not a ChromaX backup file");
        return -1;
    }

    CxBackup b;
    memset(&b, 0, sizeof b);
    CxLook_Default(&b.look);
    b.ts = (long long)CxJson_GetNum(root, "ts", 0);
    const char *src = CxJson_GetStr(root, "source", "");
    size_t sn = strlen(src);
    if (sn >= sizeof b.source) sn = sizeof b.source - 1;
    memcpy(b.source, src, sn);

    CxJson *co = CxJson_Get(root, "color");
    if (co && co->type == CXJ_OBJ) {
        for (int i = 0; i < CXLOOK_NPARAMS; i++)
            if (CxJson_IsNum(co, CxLook_ParamName(i)))
                CxLook_Set(&b.look, i, (float)CxJson_GetNum(co, CxLook_ParamName(i), CxLook_Get(&b.look, i)));
        b.look.enabled = CxJson_GetBool(co, "enabled", 1);
        CxLook_Clamp(&b.look);
    }

    CxJson *arr = CxJson_Get(root, "monitors");
    if (arr && arr->type == CXJ_ARR) {
        for (int i = 0; i < arr->nchild && b.nmon < CX_BACKUP_MAX_MON; i++) {
            CxJson *m = arr->child[i].val;
            if (!m || m->type != CXJ_OBJ) continue;
            CxMonState *ms = &b.mon[b.nmon++];
            const char *d = CxJson_GetStr(m, "device", "");
            size_t n = strlen(d); if (n >= sizeof ms->device) n = sizeof ms->device - 1;
            memcpy(ms->device, d, n);
            const char *id = CxJson_GetStr(m, "id", "");
            n = strlen(id); if (n >= sizeof ms->id) n = sizeof ms->id - 1;
            memcpy(ms->id, id, n);
            ms->w = (int)CxJson_GetNum(m, "w", 0);
            ms->h = (int)CxJson_GetNum(m, "h", 0);
            ms->hz = (int)CxJson_GetNum(m, "hz", 0);
            ms->hdr = CxJson_GetBool(m, "hdr", 0);
        }
    }
    CxJson_Free(root);
    *out = b;
    return 0;
}

int CxBackup_WriteFile(const char *path, const CxBackup *b)
{
    char buf[8192];
    int n = CxBackup_ToJson(b, buf, sizeof buf);
    if (n < 0) return -1;
    FILE *f = fopen(path, "wb");
    if (!f) return -1;
    size_t wr = fwrite(buf, 1, (size_t)n + 1, f);
    fclose(f);
    return wr == (size_t)n + 1 ? 0 : -1;
}

int CxBackup_ReadFile(const char *path, CxBackup *out, char *err, size_t errsz)
{
    FILE *f = fopen(path, "rb");
    if (!f) { if (errsz) snprintf(err, errsz, "cannot open %s", path); return -1; }
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz < 0 || sz > 64 * 1024) { fclose(f); return -1; }
    char *buf = (char *)malloc((size_t)sz + 1);
    if (!buf) { fclose(f); return -1; }
    size_t rd = fread(buf, 1, (size_t)sz, f);
    fclose(f);
    buf[rd] = 0;
    int rc = CxBackup_FromJson(buf, rd, out, err, errsz);
    free(buf);
    return rc;
}
