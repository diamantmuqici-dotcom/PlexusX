/* cx_json.h \u2014 tiny safe JSON parser/writer (platform independent)
 *
 * Used for preset import/export, backups, history and diagnostics.
 * All imported content is treated as untrusted data: fixed limits, no
 * recursion beyond depth 16, no allocation of caller-controlled sizes
 * beyond caps.  Nothing is ever executed.
 */
#ifndef CX_JSON_H
#define CX_JSON_H

#include <stdio.h>
#include <stddef.h>

#define CXJ_DEPTH_MAX 16
#define CXJ_NODES_MAX 4096
#define CXJ_STR_MAX   4096

typedef enum { CXJ_NULL, CXJ_BOOL, CXJ_NUM, CXJ_STR, CXJ_ARR, CXJ_OBJ } CxJsonType;

typedef struct CxJson CxJson;
typedef struct { char *key; CxJson *val; } CxJsonPair; /* key == NULL for arrays */

struct CxJson {
    CxJsonType type;
    double num;
    int    boolean;
    char  *str;
    int    nchild, cap;
    CxJsonPair *child;
};

/* parse; returns 0 on success. On failure returns -1 and fills err. */
int  CxJson_Parse(const char *text, size_t len, CxJson **out, char *err, size_t errsz);
void CxJson_Free(CxJson *j);

/* object member access (NULL-safe) */
CxJson *    CxJson_Get(const CxJson *obj, const char *key);
double      CxJson_GetNum(const CxJson *obj, const char *key, double dflt);
const char *CxJson_GetStr(const CxJson *obj, const char *key, const char *dflt);
int         CxJson_GetBool(const CxJson *obj, const char *key, int dflt);
int         CxJson_IsNum(const CxJson *obj, const char *key);

/* array: child[i].val */
int         CxJson_ArrLen(const CxJson *arr);

/* writers */
int   CxJson_WriteF(FILE *f, const CxJson *j, int indent);
char *CxJson_WriteStr(const CxJson *j);      /* malloc'd compact form, NULL on OOM */

/* builders (for creating documents to write out) */
CxJson *CxJson_NewObj(void);
CxJson *CxJson_NewArr(void);
CxJson *CxJson_NewNum(double v);
CxJson *CxJson_NewStr(const char *s);        /* copies */
CxJson *CxJson_NewBool(int b);
void    CxJson_ObjSet(CxJson *obj, const char *key, CxJson *val); /* takes ownership */
void    CxJson_ArrAdd(CxJson *arr, CxJson *val);                   /* takes ownership */

#endif
