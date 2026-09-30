/* cx_json.c \u2014 tiny safe JSON parser/writer */
#include "cx_json.h"
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <math.h>

static int g_nodes;

/* ---------------- builders ---------------- */

static CxJson *jnew(CxJsonType t)
{
    if (g_nodes >= CXJ_NODES_MAX) return NULL;
    CxJson *j = (CxJson *)calloc(1, sizeof *j);
    if (!j) return NULL;
    j->type = t;
    g_nodes++;
    return j;
}

CxJson *CxJson_NewObj(void)  { return jnew(CXJ_OBJ); }
CxJson *CxJson_NewArr(void)  { return jnew(CXJ_ARR); }
CxJson *CxJson_NewNum(double v) { CxJson *j = jnew(CXJ_NUM);  if (j) j->num = v; return j; }
CxJson *CxJson_NewBool(int b)   { CxJson *j = jnew(CXJ_BOOL); if (j) j->boolean = b; return j; }
CxJson *CxJson_NewStr(const char *s)
{
    CxJson *j = jnew(CXJ_STR);
    if (!j) return NULL;
    size_t n = s ? strlen(s) : 0;
    if (n > CXJ_STR_MAX) n = CXJ_STR_MAX;
    j->str = (char *)malloc(n + 1);
    if (!j->str) { CxJson_Free(j); return NULL; }
    memcpy(j->str, s ? s : "", n);
    j->str[n] = 0;
    return j;
}

static void child_add(CxJson *j, char *key, CxJson *val)
{
    if (j->nchild == j->cap) {
        int nc = j->cap ? j->cap * 2 : 8;
        if (nc > 1024) nc = 1024;
        CxJsonPair *np = (CxJsonPair *)realloc(j->child, (size_t)nc * sizeof *np);
        if (!np) { free(key); return; }
        j->child = np; j->cap = nc;
    }
    j->child[j->nchild].key = key;
    j->child[j->nchild].val = val;
    j->nchild++;
}

void CxJson_ObjSet(CxJson *obj, const char *key, CxJson *val)
{
    if (!obj || obj->type != CXJ_OBJ || !val) return;
    char *k = (char *)malloc(strlen(key) + 1);
    if (!k) return;
    strcpy(k, key);
    child_add(obj, k, val);
}
void CxJson_ArrAdd(CxJson *arr, CxJson *val)
{
    if (!arr || arr->type != CXJ_ARR || !val) return;
    child_add(arr, NULL, val);
}

void CxJson_Free(CxJson *j)
{
    if (!j) return;
    for (int i = 0; i < j->nchild; i++) {
        free(j->child[i].key);
        CxJson_Free(j->child[i].val);
    }
    free(j->child);
    free(j->str);
    free(j);
    g_nodes = 0; /* reset global guard for the next parse/build */
}

/* ---------------- accessors ---------------- */

CxJson *CxJson_Get(const CxJson *obj, const char *key)
{
    if (!obj || obj->type != CXJ_OBJ) return NULL;
    for (int i = 0; i < obj->nchild; i++)
        if (obj->child[i].key && !strcmp(obj->child[i].key, key))
            return obj->child[i].val;
    return NULL;
}
double CxJson_GetNum(const CxJson *obj, const char *key, double dflt)
{
    CxJson *v = CxJson_Get(obj, key);
    return (v && v->type == CXJ_NUM) ? v->num : dflt;
}
int CxJson_IsNum(const CxJson *obj, const char *key)
{
    CxJson *v = CxJson_Get(obj, key);
    return (v && v->type == CXJ_NUM);
}
const char *CxJson_GetStr(const CxJson *obj, const char *key, const char *dflt)
{
    CxJson *v = CxJson_Get(obj, key);
    return (v && v->type == CXJ_STR && v->str) ? v->str : dflt;
}
int CxJson_GetBool(const CxJson *obj, const char *key, int dflt)
{
    CxJson *v = CxJson_Get(obj, key);
    if (v && v->type == CXJ_BOOL) return v->boolean;
    if (v && v->type == CXJ_NUM)  return v->num != 0;
    return dflt;
}
int CxJson_ArrLen(const CxJson *arr)
{
    return (arr && arr->type == CXJ_ARR) ? arr->nchild : 0;
}

/* ---------------- parser ---------------- */

typedef struct {
    const char *p, *end;
    char err[128];
    int  depth;
} P;

static void perr(P *st, const char *msg)
{
    if (!st->err[0]) {
        size_t rem = sizeof st->err - 1;
        size_t off = (size_t)(st->p - st->end); /* negative */
        snprintf(st->err, rem, "%s (offset ~%d)", msg, (int)off);
    }
}
static void skipws(P *st)
{
    while (st->p < st->end && (unsigned char)*st->p <= ' ') st->p++;
}

static CxJson *parse_value(P *st);

static CxJson *parse_string_raw(P *st)
{
    /* assumes *st->p == '"' */
    st->p++;
    char *buf = (char *)malloc(CXJ_STR_MAX + 1);
    if (!buf) { perr(st, "oom"); return NULL; }
    size_t n = 0;
    while (st->p < st->end && *st->p != '"') {
        char c = *st->p++;
        if (c == '\\') {
            if (st->p >= st->end) break;
            char e = *st->p++;
            switch (e) {
            case '"': c = '"'; break;
            case '\\': c = '\\'; break;
            case '/': c = '/'; break;
            case 'b': c = '\b'; break;
            case 'f': c = '\f'; break;
            case 'n': c = '\n'; break;
            case 'r': c = '\r'; break;
            case 't': c = '\t'; break;
            case 'u': {
                if (st->p + 4 > st->end) { perr(st, "bad \\u"); free(buf); return NULL; }
                unsigned cp = 0;
                for (int i = 0; i < 4; i++) {
                    char h = st->p[i];
                    cp <<= 4;
                    if (h >= '0' && h <= '9') cp |= (unsigned)(h - '0');
                    else if (h >= 'a' && h <= 'f') cp |= (unsigned)(h - 'a' + 10);
                    else if (h >= 'A' && h <= 'F') cp |= (unsigned)(h - 'A' + 10);
                    else { perr(st, "bad \\u"); free(buf); return NULL; }
                }
                st->p += 4;
                /* encode UTF-8 (BMP only; surrogates -> replacement) */
                if (cp < 0x80) buf[n++] = (char)cp;
                else if (cp < 0x800) { buf[n++] = (char)(0xC0 | (cp >> 6)); buf[n++] = (char)(0x80 | (cp & 63)); }
                else if (cp >= 0xD800 && cp < 0xE000) { buf[n++] = 0xEF; buf[n++] = 0xBF; buf[n++] = 0xBD; }
                else { buf[n++] = (char)(0xE0 | (cp >> 12)); buf[n++] = (char)(0x80 | ((cp >> 6) & 63)); buf[n++] = (char)(0x80 | (cp & 63)); }
                continue;
            }
            default: perr(st, "bad escape"); free(buf); return NULL;
            }
        } else if ((unsigned char)c < 0x20) {
            perr(st, "raw control in string"); free(buf); return NULL;
        }
        if (n >= CXJ_STR_MAX) { perr(st, "string too long"); free(buf); return NULL; }
        buf[n++] = c;
    }
    if (st->p >= st->end) { perr(st, "unterminated string"); free(buf); return NULL; }
    st->p++; /* closing quote */
    buf[n] = 0;
    CxJson *j = CxJson_NewStr(buf);
    free(buf);
    return j;
}

static CxJson *parse_number(P *st)
{
    const char *s = st->p;
    if (*st->p == '-' || *st->p == '+') st->p++;
    int digits = 0;
    while (st->p < st->end && (isdigit((unsigned char)*st->p) || *st->p == '.' ||
           *st->p == 'e' || *st->p == 'E' || *st->p == '+' || *st->p == '-')) {
        if (isdigit((unsigned char)*st->p)) digits++;
        st->p++;
    }
    if (!digits) { perr(st, "bad number"); return NULL; }
    char tmp[64];
    size_t n = (size_t)(st->p - s);
    if (n > sizeof tmp - 1) n = sizeof tmp - 1;
    memcpy(tmp, s, n); tmp[n] = 0;
    double v = strtod(tmp, NULL);
    if (!isfinite(v)) { perr(st, "number out of range"); return NULL; }
    return CxJson_NewNum(v);
}

static CxJson *parse_value(P *st)
{
    if (st->depth > CXJ_DEPTH_MAX) { perr(st, "depth limit"); return NULL; }
    skipws(st);
    if (st->p >= st->end) { perr(st, "unexpected end"); return NULL; }
    char c = *st->p;
    if (c == '{') {
        st->p++; st->depth++;
        CxJson *o = CxJson_NewObj();
        if (!o) { st->depth--; return NULL; }
        skipws(st);
        if (st->p < st->end && *st->p == '}') { st->p++; st->depth--; return o; }
        for (;;) {
            skipws(st);
            if (st->p >= st->end || *st->p != '"') { perr(st, "expected key"); CxJson_Free(o); st->depth--; return NULL; }
            CxJson *ks = parse_string_raw(st);
            if (!ks) { CxJson_Free(o); st->depth--; return NULL; }
            char *key = ks->str; ks->str = NULL; CxJson_Free(ks);
            skipws(st);
            if (st->p >= st->end || *st->p != ':') { perr(st, "expected ':'"); free(key); CxJson_Free(o); st->depth--; return NULL; }
            st->p++;
            CxJson *v = parse_value(st);
            if (!v) { free(key); CxJson_Free(o); st->depth--; return NULL; }
            child_add(o, key, v);
            skipws(st);
            if (st->p < st->end && *st->p == ',') { st->p++; continue; }
            if (st->p < st->end && *st->p == '}') { st->p++; st->depth--; return o; }
            perr(st, "expected , or }"); CxJson_Free(o); st->depth--; return NULL;
        }
    }
    if (c == '[') {
        st->p++; st->depth++;
        CxJson *a = CxJson_NewArr();
        if (!a) { st->depth--; return NULL; }
        skipws(st);
        if (st->p < st->end && *st->p == ']') { st->p++; st->depth--; return a; }
        for (;;) {
            CxJson *v = parse_value(st);
            if (!v) { CxJson_Free(a); st->depth--; return NULL; }
            child_add(a, NULL, v);
            skipws(st);
            if (st->p < st->end && *st->p == ',') { st->p++; continue; }
            if (st->p < st->end && *st->p == ']') { st->p++; st->depth--; return a; }
            perr(st, "expected , or ]"); CxJson_Free(a); st->depth--; return NULL;
        }
    }
    if (c == '"') return parse_string_raw(st);
    if (c == 't' && st->end - st->p >= 4 && !strncmp(st->p, "true", 4))  { st->p += 4; return CxJson_NewBool(1); }
    if (c == 'f' && st->end - st->p >= 5 && !strncmp(st->p, "false", 5)) { st->p += 5; return CxJson_NewBool(0); }
    if (c == 'n' && st->end - st->p >= 4 && !strncmp(st->p, "null", 4))  { st->p += 4; return jnew(CXJ_NULL); }
    if (c == '-' || (c >= '0' && c <= '9')) return parse_number(st);
    perr(st, "unexpected character");
    return NULL;
}

int CxJson_Parse(const char *text, size_t len, CxJson **out, char *err, size_t errsz)
{
    P st = { text, text + len, {0}, 0 };
    g_nodes = 0;
    CxJson *j = parse_value(&st);
    if (!j) { if (errsz) snprintf(err, errsz, "%s", st.err[0] ? st.err : "parse error"); return -1; }
    skipws(&st);
    if (st.p < st.end) { perr(&st, "trailing data"); CxJson_Free(j); if (errsz) snprintf(err, errsz, "%s", st.err); return -1; }
    *out = j;
    return 0;
}

/* ---------------- writer ---------------- */

static int write_str(FILE *f, const char *s)
{
    fputc('"', f);
    for (const char *p = s; *p; p++) {
        unsigned char c = (unsigned char)*p;
        switch (c) {
        case '"':  fputs("\\\"", f); break;
        case '\\': fputs("\\\\", f); break;
        case '\b': fputs("\\b", f); break;
        case '\f': fputs("\\f", f); break;
        case '\n': fputs("\\n", f); break;
        case '\r': fputs("\\r", f); break;
        case '\t': fputs("\\t", f); break;
        default:
            if (c < 0x20) fprintf(f, "\\u%04x", c);
            else fputc(c, f);
        }
    }
    fputc('"', f);
    return 0;
}

static int write_one(FILE *f, const CxJson *j, int indent, int depth)
{
    if (!j) { fputs("null", f); return 0; }
    const char *nl = indent ? "\n" : "";
    const char *ind = "";
    char buf[64];
    if (indent) { snprintf(buf, sizeof buf, "%*s", depth * indent, ""); ind = buf; }
    switch (j->type) {
    case CXJ_NULL: fputs("null", f); break;
    case CXJ_BOOL: fputs(j->boolean ? "true" : "false", f); break;
    case CXJ_NUM:
        if (j->num == (double)(long long)j->num && fabs(j->num) < 1e15)
            fprintf(f, "%lld", (long long)j->num);
        else
            fprintf(f, "%.10g", j->num);
        break;
    case CXJ_STR: write_str(f, j->str ? j->str : ""); break;
    case CXJ_ARR:
        if (!j->nchild) { fputs("[]", f); break; }
        fputs("[", f);
        for (int i = 0; i < j->nchild; i++) {
            fputs(nl, f); fputs(indent ? ind : "", f);
            if (i) { fputc(',', f); fputs(nl, f); fputs(indent ? ind : "", f); }
            write_one(f, j->child[i].val, indent, depth + 1);
        }
        fputs(nl, f);
        if (indent) { char b2[64]; snprintf(b2, sizeof b2, "%*s", (depth - 1) * indent, ""); fputs(b2, f); }
        fputc(']', f);
        break;
    case CXJ_OBJ:
        if (!j->nchild) { fputs("{}", f); break; }
        fputs("{", f);
        for (int i = 0; i < j->nchild; i++) {
            fputs(nl, f); fputs(indent ? ind : "", f);
            if (i) { fputc(',', f); fputs(nl, f); fputs(indent ? ind : "", f); }
            write_str(f, j->child[i].key ? j->child[i].key : "");
            fputs(indent ? ": " : ":", f);
            write_one(f, j->child[i].val, indent, depth + 1);
        }
        fputs(nl, f);
        if (indent) { char b2[64]; snprintf(b2, sizeof b2, "%*s", (depth - 1) * indent, ""); fputs(b2, f); }
        fputc('}', f);
        break;
    }
    return 0;
}

int CxJson_WriteF(FILE *f, const CxJson *j, int indent) { return write_one(f, j, indent, 0); }

/* string-form writer (for small docs; bounded size) */
typedef struct { char *buf; size_t n, cap; } SB;
static int write_to_str(SB *s, const char *data, size_t len)
{
    if (s->n + len + 1 > s->cap) {
        size_t nc = s->cap ? s->cap * 2 : 256;
        while (nc < s->n + len + 1) nc *= 2;
        if (nc > 1024 * 1024) return -1;
        char *nb = (char *)realloc(s->buf, nc);
        if (!nb) return -1;
        s->buf = nb; s->cap = nc;
    }
    memcpy(s->buf + s->n, data, len);
    s->n += len;
    s->buf[s->n] = 0;
    return 0;
}
static int sw_str(SB *s, const char *str)
{
    char tmp[8];
    if (write_to_str(s, "\"", 1)) return -1;
    for (const char *p = str; *p; p++) {
        unsigned char c = (unsigned char)*p;
        switch (c) {
        case '"':  if (write_to_str(s, "\\\"", 2)) return -1; break;
        case '\\': if (write_to_str(s, "\\\\", 2)) return -1; break;
        case '\n': if (write_to_str(s, "\\n", 2)) return -1; break;
        case '\r': if (write_to_str(s, "\\r", 2)) return -1; break;
        case '\t': if (write_to_str(s, "\\t", 2)) return -1; break;
        default:
            if (c < 0x20) {
                snprintf(tmp, sizeof tmp, "\\u%04x", c);
                if (write_to_str(s, tmp, 6)) return -1;
            } else {
                tmp[0] = (char)c;
                if (write_to_str(s, tmp, 1)) return -1;
            }
        }
    }
    return write_to_str(s, "\"", 1);
}
static int write_one_str(SB *s, const CxJson *j, int depth)
{
    char num[40];
    if (!j) return write_to_str(s, "null", 4);
    switch (j->type) {
    case CXJ_NULL: return write_to_str(s, "null", 4);
    case CXJ_BOOL: return write_to_str(s, j->boolean ? "true" : "false", j->boolean ? 4 : 5);
    case CXJ_NUM:
        if (j->num == (double)(long long)j->num && fabs(j->num) < 1e15)
            snprintf(num, sizeof num, "%lld", (long long)j->num);
        else
            snprintf(num, sizeof num, "%.10g", j->num);
        return write_to_str(s, num, strlen(num));
    case CXJ_STR: return sw_str(s, j->str ? j->str : "");
    case CXJ_ARR:
        if (write_to_str(s, "[", 1)) return -1;
        for (int i = 0; i < j->nchild; i++) {
            if (i) if (write_to_str(s, ",", 1)) return -1;
            if (write_one_str(s, j->child[i].val, depth + 1)) return -1;
        }
        return write_to_str(s, "]", 1);
    case CXJ_OBJ:
        if (write_to_str(s, "{", 1)) return -1;
        for (int i = 0; i < j->nchild; i++) {
            if (i) if (write_to_str(s, ",", 1)) return -1;
            if (sw_str(s, j->child[i].key ? j->child[i].key : "")) return -1;
            if (write_to_str(s, ":", 1)) return -1;
            if (write_one_str(s, j->child[i].val, depth + 1)) return -1;
        }
        return write_to_str(s, "}", 1);
    }
    return -1;
}

char *CxJson_WriteStr(const CxJson *j)
{
    SB s = {0};
    if (write_one_str(&s, j, 0)) { free(s.buf); return NULL; }
    return s.buf;
}
