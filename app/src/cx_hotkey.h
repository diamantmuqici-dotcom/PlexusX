/* cx_hotkey.h \u2014 hotkey combo parse/format/validate (platform independent)
 *
 * Canonical form: "Ctrl+Alt+Shift+Win+<Key>"  e.g. "Ctrl+Alt+X"
 * The <Key> token is a single printable ASCII char or one of:
 * Up Down Left Right F1..F24 D0..D9 Space Enter Tab
 *
 * A valid ChromaX hotkey MUST carry at least one modifier (we refuse to
 * hijack bare keys system-wide).
 */
#ifndef CX_HOTKEY_H
#define CX_HOTKEY_H

typedef struct CxHotkey {
    int mod_ctrl, mod_alt, mod_shift, mod_win;
    char key[16];   /* canonical key token, e.g. "X", "F9", "Up" */
} CxHotkey;

int  CxHotkey_Parse(const char *s, CxHotkey *out);   /* 0 ok, -1 invalid */
void CxHotkey_Format(const CxHotkey *h, char *out, int sz); /* "Ctrl+Alt+X" */
int  CxHotkey_Same(const CxHotkey *a, const CxHotkey *b);
int  CxHotkey_Valid(const CxHotkey *h);              /* has modifiers + key  */

#endif
