/* cx_hotkey.c */
#include "cx_hotkey.h"
#include <string.h>
#include <stdio.h>
#include <ctype.h>

static const char *k_keys[] = {
    "Up", "Down", "Left", "Right", "Space", "Enter", "Tab",
    "F1", "F2", "F3", "F4", "F5", "F6", "F7", "F8", "F9", "F10", "F11", "F12",
    "F13", "F14", "F15", "F16", "F17", "F18", "F19", "F20", "F21", "F22", "F23", "F24",
    "D0", "D1", "D2", "D3", "D4", "D5", "D6", "D7", "D8", "D9",
    0
};

static int key_index(const char *k)
{
    for (int i = 0; k_keys[i]; i++)
        if (!strcasecmp(k, k_keys[i])) return i;
    return -1;
}

int CxHotkey_Parse(const char *s, CxHotkey *out)
{
    CxHotkey h = {0};
    if (!s || !out) return -1;
    const char *p = s;
    int saw_key = 0;
    while (*p) {
        /* read one token up to '+' */
        const char *q = p;
        while (*q && *q != '+') q++;
        int n = (int)(q - p);
        char tok[16] = {0};
        if (n > 15) return -1;
        memcpy(tok, p, n);
        if (n == 1 && isalpha((unsigned char)tok[0])) tok[0] = (char)toupper((unsigned char)tok[0]);

        if (!strcasecmp(tok, "Ctrl")) h.mod_ctrl = 1;
        else if (!strcasecmp(tok, "Alt")) h.mod_alt = 1;
        else if (!strcasecmp(tok, "Shift")) h.mod_shift = 1;
        else if (!strcasecmp(tok, "Win") || !strcasecmp(tok, "Cmd") || !strcasecmp(tok, "Windows")) h.mod_win = 1;
        else if (n == 1 && isalpha((unsigned char)tok[0])) {
            if (saw_key) return -1;
            h.key[0] = tok[0]; saw_key = 1;
        }
        else if (n == 1 && isdigit((unsigned char)tok[0])) {
            if (saw_key) return -1;
            h.key[0] = 'D'; h.key[1] = tok[0]; saw_key = 1;
        }
        else if (key_index(tok) >= 0) {
            if (saw_key) return -1;
            snprintf(h.key, sizeof h.key, "%s", tok);
            saw_key = 1;
        }
        else return -1;

        p = q;
        if (*p == '+') p++;
        else if (*p) return -1; /* garbage after token */
    }
    if (!saw_key) return -1;
    *out = h;
    return CxHotkey_Valid(out) ? 0 : -1;
}

void CxHotkey_Format(const CxHotkey *h, char *out, int sz)
{
    if (!sz) return;
    out[0] = 0;
    int off = 0;
#define APP(...) do { int r = snprintf(out + off, sz - off, __VA_ARGS__); if (r > 0 && off + r < sz) off += r; } while (0)
    if (h->mod_ctrl)  APP("Ctrl+");
    if (h->mod_alt)   APP("Alt+");
    if (h->mod_shift) APP("Shift+");
    if (h->mod_win)   APP("Win+");
    APP("%s", h->key[0] ? h->key : "?");
#undef APP
}

int CxHotkey_Same(const CxHotkey *a, const CxHotkey *b)
{
    return a && b &&
        a->mod_ctrl == b->mod_ctrl && a->mod_alt == b->mod_alt &&
        a->mod_shift == b->mod_shift && a->mod_win == b->mod_win &&
        !strcasecmp(a->key, b->key);
}

int CxHotkey_Valid(const CxHotkey *h)
{
    if (!h || !h->key[0]) return 0;
    return h->mod_ctrl || h->mod_alt || h->mod_shift || h->mod_win;
}
