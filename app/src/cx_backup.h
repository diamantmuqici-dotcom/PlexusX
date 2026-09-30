/* cx_backup.h \u2014 display-state backup snapshots (platform independent core)
 *
 * Before any change ChromaX makes, a snapshot of the affected state is
 * written locally (never uploaded).  The app keeps the last N snapshots in
 * `history` so the user (or crash recovery on next launch) can restore.
 */
#ifndef CX_BACKUP_H
#define CX_BACKUP_H

#include "cx_color.h"
#include <stddef.h>

#define CX_BACKUP_MAX_MON 4

typedef struct CxMonState {
    char device[32];   /* \\.\DISPLAY1 */
    char id[64];       /* stable EDID-derived identity */
    int  w, h, hz;
    int  hdr;          /* 0/1 */
} CxMonState;

typedef struct CxBackup {
    long long ts;            /* unix seconds */
    int       nmon;
    CxMonState mon[CX_BACKUP_MAX_MON];
    CxLook    look;
    char      source[48];    /* what triggered the change */
} CxBackup;

int  CxBackup_Fill(CxBackup *b, const CxLook *look, const char *source,
                   const CxMonState *mon, int nmon, long long ts);
/* compact JSON; returns bytes written (excluding NUL) or -1 */
int  CxBackup_ToJson(const CxBackup *b, char *buf, size_t sz);
int  CxBackup_FromJson(const char *buf, size_t len, CxBackup *out, char *err, size_t errsz);
int  CxBackup_WriteFile(const char *path, const CxBackup *b);
int  CxBackup_ReadFile(const char *path, CxBackup *out, char *err, size_t errsz);

#endif
