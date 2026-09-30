/* test_core.c — unit tests for the platform-independent core
 * Build: make -C app test   (native gcc, also used by CI)
 */
#include "cx_color.h"
#include "cx_json.h"
#include "cx_preset.h"
#include "cx_games.h"
#include "cx_hotkey.h"
#include "cx_backup.h"
#include "cx_scene.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>

static int g_fail = 0, g_pass = 0;
#define CHECK(cond, msg) do { \
    if (cond) { g_pass++; } \
    else { g_fail++; printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, msg); } \
} while (0)
#define CHECKF(cond, fmt, ...) do { \
    if (cond) { g_pass++; } \
    else { g_fail++; printf("FAIL %s:%d  " fmt "\n", __FILE__, __LINE__, ##__VA_ARGS__); } \
} while (0)

/* ---------------- colour ---------------- */

static void test_color_identity(void)
{
    CxLook l; CxLook_Default(&l);
    float m[4][4];
    CxLook_BuildMatrix(&l, m);
    int ident = 1;
    for (int i = 0; i < 4; i++)
        for (int j = 0; j < 4; j++)
            if (fabsf(m[i][j] - ((i == j) ? 1.f : 0.f)) > 0.001f) ident = 0;
    CHECK(ident, "neutral look must produce identity matrix");

    uint16_t ramp[3][256];
    CxLook_BuildLUT(&l, ramp, 0);
    int lut_ident = 1;
    for (int c = 0; c < 3; c++)
        for (int i = 0; i < 256; i++)
            if (ramp[c][i] != (uint16_t)(i * 65535.0 / 255.0 + 0.5f)) lut_ident = 0;
    CHECK(lut_ident, "neutral look must produce identity LUT");

    uint8_t px[4] = { 137, 90, 201, 255 };
    uint8_t copy[4]; memcpy(copy, px, 4);
    CxColor_ApplyBuffer(&l, px, 1, 1);
    CHECK(memcmp(px, copy, 4) == 0, "neutral preview must leave pixels unchanged");
}

static void test_color_saturation(void)
{
    CxLook l; CxLook_Default(&l);
    l.sat = 300;
    float m[4][4];
    CxLook_BuildMatrix(&l, m);
    /* saturation matrix: luminance weights on off-diagonals must be (1-s)*w */
    CHECK(fabsf(m[0][1] - 0.7152f * (1.f - 3.f)) < 0.02f, "sat 300%% off-diagonal R<-G");
    CHECK(fabsf(m[1][1] - (1.f * 3.f + 0.7152f * (1.f - 3.f))) < 0.02f, "sat 300%% diagonal G");

    /* a grey pixel must stay grey under saturation */
    uint8_t grey[4] = { 128, 128, 128, 255 };
    CxColor_ApplyBuffer(&l, grey, 1, 1);
    CHECK(grey[0] == grey[1] && grey[1] == grey[2], "saturation must not shift grey");
    /* a saturated pixel must gain chroma */
    uint8_t red[4] = { 200, 120, 120, 255 };
    CxColor_ApplyBuffer(&l, red, 1, 1);
    CHECK(red[0] > 200 || (int)red[1] < 120, "saturation must increase chroma");
}

static void test_color_temperature(void)
{
    CxLook l; CxLook_Default(&l);
    l.temperature = 3000; /* warm */
    float m[4][4];
    CxLook_BuildMatrix(&l, m);
    CHECK(m[0][0] > 1.f && m[2][2] < 1.f, "3000K must boost red, cut blue");

    l.temperature = 10000;
    CxLook_BuildMatrix(&l, m);
    CHECK(m[0][0] < 1.f && m[2][2] > 1.f, "10000K must boost blue, cut red");
}

static void test_color_lut_monotonic(void)
{
    /* random-ish combos: LUT must stay monotonic non-decreasing */
    for (int t = 0; t < 200; t++) {
        CxLook l; CxLook_Default(&l);
        l.sat        = 0 + (t * 1371) % 301;
        l.vibrance   = 0 + (t * 2221) % 301;
        l.brightness = 0 + (t * 331) % 201;
        l.contrast   = 0 + (t * 171) % 201;
        l.gamma      = 0.5f + (t * 7) % 201 / 100.f;
        l.temperature= 3000 + (t * 37) % 7001;
        l.tint       = -100 + (t * 53) % 201;
        l.shadows    = 0 + (t * 61) % 201;
        l.highlights = 0 + (t * 83) % 201;
        l.blacklevel = 0 + (t * 97) % 201;
        l.whitepoint = 0 + (t * 109) % 201;
        l.dehaze     = 0 + (t * 127) % 201;
        uint16_t ramp[3][256];
        CxLook_BuildLUT(&l, ramp, 0);
        int ok = 1;
        for (int c = 0; c < 3 && ok; c++)
            for (int i = 1; i < 256; i++)
                if (ramp[c][i] < ramp[c][i - 1]) { ok = 0; break; }
        if (!ok) {
            printf("  (t=%d sat=%d gam=%.2f sh=%d hi=%d bl=%d wp=%d dh=%d)\n",
                   t, (int)l.sat, l.gamma, (int)l.shadows, (int)l.highlights,
                   (int)l.blacklevel, (int)l.whitepoint, (int)l.dehaze);
        }
        CHECK(ok, "LUT must be monotonic for random combo");
    }
}

static void test_color_clamp(void)
{
    CxLook l;
    memset(&l, 0xAB, sizeof l);
    l.enabled = 1;
    CxLook_Clamp(&l);
    CHECK(l.sat >= 0 && l.sat <= 300, "sat clamp");
    CHECK(l.brightness >= 0 && l.brightness <= 200, "brightness clamp");
    CHECK(l.gamma >= 0.5f && l.gamma <= 2.5f, "gamma clamp");
    CHECK(l.temperature >= 3000 && l.temperature <= 10000, "temperature clamp");
    CHECK(l.tint >= -100 && l.tint <= 100, "tint clamp");
    CHECK(l.enabled == 1, "enabled normalises to 1");
}

static void test_color_preview_buffer(void)
{
    int w = 48, h = 27;
    uint8_t *a = (uint8_t *)malloc((size_t)w * h * 4);
    uint8_t *b = (uint8_t *)malloc((size_t)w * h * 4);
    CxScene_Generate(CX_SCENE_FOREST, a, w, h);
    memcpy(b, a, (size_t)w * h * 4);
    CxLook l; CxLook_Default(&l);
    l.sat = 220; l.contrast = 120; l.gamma = 0.9f; l.sharpness = 140;
    CxColor_ApplyBuffer(&l, b, w, h);
    int changed = 0;
    for (size_t i = 0; i < (size_t)w * h; i++)
        if (memcmp(&a[i * 4], &b[i * 4], 3)) { changed = 1; break; }
    CHECK(changed, "preview must change pixels for non-neutral look");
    free(a); free(b);
}

/* ---------------- json ---------------- */

static void test_json_basic(void)
{
    const char *src =
        "{\"app\":\"ChromaX\",\"n\":42.5,\"neg\":-7,\"arr\":[1,2,3],\"s\":\"hi\\n\\\"q\\\"\","
        "\"b\":true,\"nil\":null,\"nested\":{\"x\":\"y\"}}";
    CxJson *j = NULL;
    char err[128] = {0};
    CHECKF(CxJson_Parse(src, strlen(src), &j, err, sizeof err) == 0, "json parse basic: %s", err);
    CHECK(j && j->type == CXJ_OBJ, "json root obj");
    CHECK(!strcmp(CxJson_GetStr(j, "app", ""), "ChromaX"), "json str");
    CHECK(CxJson_GetNum(j, "n", 0) == 42.5, "json num");
    CHECK(CxJson_GetNum(j, "neg", 0) == -7, "json neg num");
    CHECK(CxJson_ArrLen(CxJson_Get(j, "arr")) == 3, "json arr len");
    CHECK(CxJson_GetBool(j, "b", 0) == 1, "json bool");
    CHECK(CxJson_Get(j, "nil") && CxJson_Get(j, "nil")->type == CXJ_NULL, "json null");
    CHECK(!strcmp(CxJson_GetStr(CxJson_Get(j, "nested"), "x", ""), "y"), "json nested");
    CHECK(!strcmp(CxJson_GetStr(j, "s", ""), "hi\n\"q\""), "json escapes");

    /* roundtrip via writer + reparse */
    char *out = CxJson_WriteStr(j);
    CHECK(out && strlen(out) > 10, "json write str");
    CxJson_Free(j);
    CxJson *j2 = NULL;
    if (out) {
        CHECKF(CxJson_Parse(out, strlen(out), &j2, err, sizeof err) == 0, "json reparse: %s", err);
        free(out);
    }
    if (j2) {
        CHECK(!strcmp(CxJson_GetStr(j2, "s", ""), "hi\n\"q\""), "json roundtrip escape");
        CxJson_Free(j2);
    }
}

static void test_json_hostile(void)
{
    CxJson *j; char err[128];
    CHECK(CxJson_Parse("not json", 8, &j, err, sizeof err) != 0, "reject plain text");
    CHECK(CxJson_Parse("{\"a\":}", 7, &j, err, sizeof err) != 0, "reject truncated obj");
    CHECK(CxJson_Parse("{\"a\":\"unterminated", 18, &j, err, sizeof err) != 0, "reject unterminated str");
    CHECK(CxJson_Parse("[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[", 50, &j, err, sizeof err) != 0,
                       "reject deep nesting");
    /* trailing junk */
    CHECK(CxJson_Parse("{}x", 3, &j, err, sizeof err) != 0, "reject trailing data");
    /* huge number */
    CHECK(CxJson_Parse("{\"n\":1e99999}", 13, &j, err, sizeof err) != 0, "reject overflow number");
    /* control chars in string */
    CHECK(CxJson_Parse("{\"s\":\"a\nb\"}", 11, &j, err, sizeof err) != 0, "reject raw newline in string");
    /* valid: empty docs */
    CHECK(CxJson_Parse("{}", 2, &j, err, sizeof err) == 0, "accept {}");
    if (j) CxJson_Free(j);
    CHECK(CxJson_Parse("[]", 2, &j, err, sizeof err) == 0, "accept []");
    if (j) CxJson_Free(j);
}

/* ---------------- presets ---------------- */

static void test_preset_roundtrip(void)
{
    CxPreset p;
    CxPreset_Default(&p, "Rust Night");
    strcpy(p.category, "game");
    p.look.sat = 185; p.look.brightness = 128; p.look.gamma = 0.85f;
    p.look.temperature = 6200; p.look.shadows = 150; p.look.enabled = 1;
    p.xh_shape = 2; p.xh_size = 22; p.xh_color = 0x4FE3FF;
    p.disp_w = 2560; p.disp_h = 1440; p.disp_hz = 240;
    p.delay_ms = 1500; p.auto_on_exit = 1;
    p.version = 2;

    char *s = CxPreset_ToJson(&p);
    CHECK(s && strlen(s) > 50, "preset serialize");

    CxPreset q; char err[160] = {0};
    CHECKF(CxPreset_FromJson(s, s ? strlen(s) : 0, &q, err, sizeof err) == 0, "preset import: %s", err);
    if (s) free(s);
    CHECK(!strcmp(q.name, "Rust Night"), "preset name");
    CHECK(fabsf(q.look.sat - 185.f) < 0.01f, "preset sat");
    CHECK(fabsf(q.look.gamma - 0.85f) < 0.001f, "preset gamma");
    CHECK(q.xh_shape == 2 && q.xh_size == 22, "preset xh");
    CHECK(q.disp_w == 2560 && q.disp_hz == 240, "preset disp");
    CHECK(q.delay_ms == 1500 && q.auto_on_exit == 1, "preset automation");
}

static void test_preset_import_safety(void)
{
    CxPreset p; char err[160];
    /* not a preset */
    const char *bad1 = "{\"app\":\"Malware\",\"name\":\"x\"}";
    CHECK(CxPreset_FromJson(bad1, strlen(bad1), &p, err, sizeof err) != 0, "reject wrong app tag");
    /* malformed */
    const char *bad2 = "{\"app\":\"ChromaX\",\"name\":\"x\",\"color\":}";
    CHECK(CxPreset_FromJson(bad2, strlen(bad2), &p, err, sizeof err) != 0, "reject malformed");
    /* out of range values must be clamped or rejected, never crash */
    const char *big = "{\"app\":\"ChromaX\",\"name\":\"x\",\"color\":{\"sat\":99999,\"gamma\":99}}";
    int rc = CxPreset_FromJson(big, strlen(big), &p, err, sizeof err);
    if (rc == 0) {
        CHECK(p.look.sat <= 300 && p.look.gamma <= 2.5f, "out-of-range clamped");
    } else {
        CHECK(1, "out-of-range rejected (also acceptable)");
    }
    /* hostile name */
    const char *evil = "{\"app\":\"ChromaX\",\"name\":\"; rm -rf / \"}";
    CHECK(CxPreset_FromJson(evil, strlen(evil), &p, err, sizeof err) == 0, "import evil name ok");
    /* the "name" must be inert text */
    CHECK(strchr(p.name, 0) == p.name + strlen(p.name), "name is a normal C string");
    /* missing name -> default */
    const char *noname = "{\"app\":\"ChromaX\"}";
    CHECK(CxPreset_FromJson(noname, strlen(noname), &p, err, sizeof err) == 0, "import without name");
    /* root not object */
    CHECK(CxPreset_FromJson("[1,2,3]", 7, &p, err, sizeof err) != 0, "reject array root");
    /* null/empty */
    CHECK(CxPreset_FromJson("", 0, &p, err, sizeof err) != 0, "reject empty");
}

/* ---------------- games ---------------- */

static void test_games(void)
{
    CHECK(CxGames_Count() >= 18, "catalog has all required games");
    CHECK(CxGames_FindExe("RustClient.exe") && CxGames_FindExe("RustClient.exe")->id == CXGAME_RUST, "rust exe match");
    CHECK(CxGames_FindExe("rustclient.exe"), "exe match case-insensitive");
    CHECK(CxGames_FindExe("cs2.exe") && CxGames_FindExe("cs2.exe")->id == CXGAME_CS2, "cs2 match");
    CHECK(CxGames_FindExe("FortniteClient-Win64-Shipping.exe") &&
          CxGames_FindExe("FortniteClient-Win64-Shipping.exe")->id == CXGAME_FORTNITE, "fortnite match");
    CHECK(CxGames_FindExe("VALORANT-Win64-Shipping.exe")->id == CXGAME_VALORANT, "valorant match");
    CHECK(CxGames_FindExe("EscapeFromTarkov.exe")->id == CXGAME_TARKOV, "tarkov match");
    CHECK(CxGames_FindExe("TslGame.exe")->id == CXGAME_PUBG, "pubg match");
    CHECK(CxGames_FindExe("unknown.exe") == NULL, "unknown exe -> NULL");
    CHECK(CxGames_FindExe("") == NULL && CxGames_FindExe(NULL) == NULL, "empty exe -> NULL");

    int out[32];
    int n = CxGames_Search("rust", out, 32);
    CHECK(n >= 1 && out[0] == CXGAME_RUST, "search 'rust' finds Rust");
    n = CxGames_Search("cs", out, 32);
    CHECK(n >= 1 && out[0] == CXGAME_CS2, "search 'cs' finds CS2");
    n = CxGames_Search("fort", out, 32);
    CHECK(n >= 1 && out[0] == CXGAME_FORTNITE, "search 'fort' finds Fortnite");

    /* every built-in look must be in valid range after clamp */
    for (int gi = 0; gi < CxGames_Count(); gi++) {
        const CxGame *g = CxGames_Get(gi);
        CHECK(g && g->nlooks > 0 && g->looks, "game has looks");
        for (int li = 0; li < g->nlooks; li++) {
            CxLook l = g->looks[li].look;
            CxLook_Clamp(&l);
            CHECK(CxLook_Equal(&l, &g->looks[li].look),
                  "built-in look already in range");
        }
    }
    for (int i = 0; i < CxLooks_Count(); i++) {
        CxLook l = CxLooks_Get(i)->look;
        CxLook_Clamp(&l);
        CHECK(CxLook_Equal(&l, &CxLooks_Get(i)->look), "built-in look in range");
    }
}

/* ---------------- hotkeys ---------------- */

static void test_hotkeys(void)
{
    CxHotkey h; char buf[64];
    CHECK(CxHotkey_Parse("Ctrl+Alt+X", &h) == 0, "parse Ctrl+Alt+X");
    CHECK(h.mod_ctrl && h.mod_alt && !h.mod_shift && !strcmp(h.key, "X"), "Ctrl+Alt+X fields");
    CxHotkey_Format(&h, buf, sizeof buf);
    CHECK(!strcmp(buf, "Ctrl+Alt+X"), "format roundtrip");

    CHECK(CxHotkey_Parse("Ctrl+Alt+Shift+Win+F9", &h) == 0, "parse 4-mod");
    CHECK(!strcmp(h.key, "F9"), "F9 key token");
    CxHotkey_Format(&h, buf, sizeof buf);
    CHECK(!strcmp(buf, "Ctrl+Alt+Shift+Win+F9"), "format 4-mod");

    CHECK(CxHotkey_Parse("X", &h) != 0, "reject bare key (no modifier)");
    CHECK(CxHotkey_Parse("Ctrl+", &h) != 0, "reject missing key");
    CHECK(CxHotkey_Parse("", &h) != 0, "reject empty");
    CHECK(CxHotkey_Parse("Ctrl+X+Y", &h) != 0, "reject two keys");
    CHECK(CxHotkey_Parse("Ctrl+Z9", &h) != 0, "reject garbage key");

    CxHotkey a, b;
    CxHotkey_Parse("Ctrl+Alt+X", &a);
    CxHotkey_Parse("ctrl+alt+x", &b);
    CHECK(CxHotkey_Same(&a, &b), "hotkeys case-insensitive equal");
}

/* ---------------- backup ---------------- */

static void test_backup(void)
{
    CxLook l; CxLook_Default(&l);
    l.sat = 210; l.gamma = 0.9f;
    CxMonState mon[2];
    memset(mon, 0, sizeof mon);
    strcpy(mon[0].device, "\\\\.\\DISPLAY1");
    strcpy(mon[0].id, "ASUS/VA249H/001");
    mon[0].w = 2560; mon[0].h = 1440; mon[0].hz = 240; mon[0].hdr = 0;
    strcpy(mon[1].device, "\\\\.\\DISPLAY2");
    strcpy(mon[1].id, "Dell/U2723QE/999");
    mon[1].w = 1920; mon[1].h = 1080; mon[1].hz = 60; mon[1].hdr = 1;

    CxBackup b, back; char err[160];
    CxBackup_Fill(&b, &l, "Rust applied", mon, 2, 1700000000LL);
    char *path = "/tmp/chromax_test_backup.json";

    CHECK(CxBackup_WriteFile(path, &b) == 0, "backup write");
    CHECKF(CxBackup_ReadFile(path, &back, err, sizeof err) == 0, "backup read: %s", err);
    CHECK(back.ts == 1700000000LL, "backup ts");
    CHECK(!strcmp(back.source, "Rust applied"), "backup source");
    CHECK(back.nmon == 2, "backup mon count");
    CHECK(back.mon[0].w == 2560 && back.mon[0].hz == 240, "backup mon0 mode");
    CHECK(back.mon[1].hdr == 1, "backup mon1 hdr");
    CHECK(fabsf(back.look.sat - 210.f) < 0.01f, "backup look sat");
    CHECK(fabsf(back.look.gamma - 0.9f) < 0.001f, "backup look gamma");
    CHECK(CxLook_Equal(&back.look, &l), "backup look equal");

    /* hostile file */
    FILE *f = fopen(path, "w");
    fprintf(f, "{\"app\":\"ChromaX-backup\",\"ts\":1,\"color\":{\"sat\":100}}garbage");
    fclose(f);
    CxBackup x;
    CHECK(CxBackup_ReadFile(path, &x, err, sizeof err) != 0, "reject malformed backup");
    remove(path);
}

/* ---------------- scenes ---------------- */

static void test_scenes(void)
{
    int w = 96, h = 54;
    uint8_t *buf = (uint8_t *)malloc((size_t)w * h * 4);
    for (int s = 0; s < CX_N_SCENE; s++) {
        memset(buf, 0, (size_t)w * h * 4);
        CxScene_Generate(s, buf, w, h);
        /* filled + not blank */
        uint32_t sum = 0;
        for (int i = 0; i < w * h; i++)
            sum += buf[i * 4] + buf[i * 4 + 1] + buf[i * 4 + 2];
        CHECKF(sum > (uint32_t)w * h, "scene %d is not blank", s);
        /* deterministic */
        uint8_t *buf2 = (uint8_t *)malloc((size_t)w * h * 4);
        CxScene_Generate(s, buf2, w, h);
        CHECKF(memcmp(buf, buf2, (size_t)w * h * 4) == 0, "scene %d deterministic", s);
        free(buf2);
        char name[32];
        CxScene_Name(s, name, sizeof name);
        CHECKF(name[0] != 0, "scene %d has name", s);
    }
    free(buf);
}
static void tmark(const char *n)
{
    static time_t last = 0;
    time_t t = time(NULL);
    if (!last) last = t;
    printf("[+%ds] %s\n", (int)(t - last), n);
    last = t;
    fflush(stdout);
}

int main(void)
{
    tmark("color_identity");
    test_color_identity();
    tmark("color_saturation");
    test_color_saturation();
    tmark("color_temperature");
    test_color_temperature();
    tmark("color_lut_monotonic");
    test_color_lut_monotonic();
    tmark("color_clamp");
    test_color_clamp();
    tmark("color_preview");
    test_color_preview_buffer();
    tmark("json_basic");
    test_json_basic();
    tmark("json_hostile");
    test_json_hostile();
    tmark("preset_roundtrip");
    test_preset_roundtrip();
    tmark("preset_safety");
    test_preset_import_safety();
    tmark("games");
    test_games();
    tmark("hotkeys");
    test_hotkeys();
    tmark("backup");
    test_backup();
    tmark("scenes");
    test_scenes();

    printf("\n%d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
