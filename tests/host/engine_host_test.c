/* PlexusX — ColorEngine + ColorPipeline host scenario tests
 *
 * Drives the REAL app/src/color/color_engine.c + color_pipeline.c (not a copy) against
 * mocked Win32 display / Magnification / file APIs (see shim/windows.h and host_mock.c),
 * and counts what would have hit the hardware:
 * SetDeviceGammaRamp calls, MagSetFullscreenColorEffect calls and the matrices they carried.
 *
 *   usage: engine_host_test <scratch-dir>        (normally started through tests/host/run.sh)
 */
#include <stdlib.h>
#include <unistd.h>
#include <sys/stat.h>
#include "host_mock.h"
#include "common.h"

static int g_checks = 0, g_scen = 0;
#define CHECK(c) do { g_checks++; if (!(c)) { fprintf(stderr, "  [FAIL] line %d: %s\n", __LINE__, #c); exit(1); } } while (0)

static wchar_t g_ramps_w[512], g_dirty_w[512];
static char g_ramps_c[512], g_dirty_c[512];

static int exists(const char *p) { struct stat st; return stat(p, &st) == 0; }
static void rm(const char *p) { unlink(p); }
static void write_file(const char *p, const void *d, size_t n) { FILE *f = fopen(p, "wb"); fwrite(d, 1, n, f); fclose(f); }
static size_t read_file(const char *p, unsigned char *buf, size_t cap) { FILE *f = fopen(p, "rb"); if (!f) return 0; size_t n = fread(buf, 1, cap, f); fclose(f); return n; }

static void make_ramp(unsigned short r[3][256], float gamma) { Look l = LOOK_NEUTRAL_INIT; l.gamma = gamma; cm_calc_ramp(&l, r); }
static int ramp_eq(unsigned short a[3][256], unsigned short b[3][256]) { return memcmp(a, b, CM_RAMP_BYTES) == 0; }

static void reset_world(int ndisp)
{
    MockFacts_Reset();
    memset(g_mock_disp, 0, sizeof g_mock_disp);
    for (int i = 0; i < ndisp; i++) { g_mock_disp[i].present = 1; g_mock_disp[i].set_ok = 1; cm_identity_ramp(g_mock_disp[i].hw); }
    g_mock_set_total = g_mock_mag_calls = g_mock_mag_bad = g_mock_mag_init = g_mock_mag_uninit = g_mock_dirty_creates = 0;
    memset(&g_mock_mag_last, 0, sizeof g_mock_mag_last);
    rm(g_ramps_c); rm(g_dirty_c);
    Eng_SetTargetMonitor(-1);
}
static void start(int was_dirty) { Eng_SetPaths(g_ramps_w, g_dirty_w, was_dirty); Eng_Init(); }
static void scen(const char *name) { g_scen++; printf("  [RUN ] %s\n", name); }

static Look neutral(void) { Look l = LOOK_NEUTRAL_INIT; return l; }

static void write_orig_file(int n, float gamma)
{
    CmRampRecord recs[MOCK_MAX_DISP]; unsigned char blob[4 + 8 * CM_RAMP_RECORD_BYTES];
    for (int i = 0; i < n; i++) { memset(&recs[i], 0, sizeof recs[i]); snprintf(recs[i].name, 32, "\\\\.\\DISPLAY%d", i + 1); make_ramp(recs[i].ramp, gamma); }
    size_t len = cm_build_ramps_blob(recs, n, blob, sizeof blob);
    write_file(g_ramps_c, blob, len);
}


/* S14 support: a "crash handler" (Eng_Reset) firing while Eng_Init is still enumerating displays */
static int g_hook_fired = 0, g_hook_sets_before = 0, g_hook_sets_after = 0, g_hook_dirty_after = 0;
static void crash_during_init(int display_index)
{
    if (display_index != 1 || g_hook_fired) return;       /* display 1 is already captured, display 2 is being read */
    g_hook_fired = 1;
    g_hook_sets_before = g_mock_set_total;
    Eng_Reset();
    g_hook_sets_after = g_mock_set_total;
    g_hook_dirty_after = exists(g_dirty_c);
}

int main(int argc, char **argv)
{
    const char *work = argc > 1 ? argv[1] : "./host-work";
    mkdir(work, 0755);
    snprintf(g_ramps_c, sizeof g_ramps_c, "%s/ramps.dat", work);
    snprintf(g_dirty_c, sizeof g_dirty_c, "%s/dirty.flg", work);
    mbstowcs(g_ramps_w, g_ramps_c, 512); mbstowcs(g_dirty_w, g_dirty_c, 512);
    unsigned short identity[3][256], calib[3][256], polluted[3][256];
    cm_identity_ramp(identity); make_ramp(calib, 1.10f); make_ramp(polluted, 0.60f);

    /* ---- S1: clean start, neutral, dragging linear sliders ---- */
    scen("S1 clean start / neutral / linear sliders never touch the LUT");
    reset_world(2); start(0);
    CHECK(g_mock_mag_init == 1);
    CHECK(g_mock_set_total == 0);                                   /* clean start writes nothing */
    { unsigned char b[4 + 8 * 1568]; size_t n = read_file(g_ramps_c, b, sizeof b); CmRampRecord r[8];
      CHECK(n == 4 + 2 * 1568); CHECK(cm_parse_ramps_blob(b, n, r, 8) == 2); CHECK(ramp_eq(r[0].ramp, identity)); CHECK(ramp_eq(r[1].ramp, identity)); }
    Look lk = neutral();
    Eng_Apply(&lk);
    CHECK(g_mock_set_total == 0); CHECK(g_mock_mag_calls == 1); CHECK(cm_effect_is_identity(&g_mock_mag_last));
    Eng_Apply(&lk); Eng_Apply(&lk);
    CHECK(g_mock_mag_calls == 1);                                   /* identical matrix: DWM is not poked again */
    for (int i = 0; i < 500; i++) {
        lk.sat = 40 + (i * 7) % 260; lk.vibrance = (i * 13) % 300; lk.bri = 20 + (i * 3) % 180; lk.con = 30 + (i * 5) % 170;
        lk.temp = 3000 + (i * 97) % 7000; lk.tint = -100 + (i % 200); lk.hue = -180 + (i * 11) % 360;
        lk.r_gain = (i * 9) % 200; lk.black_level = (i * 17) % 200; lk.white_point = 10 + (i * 19) % 190;
        Eng_Apply(&lk);
    }
    CHECK(g_mock_set_total == 0);                                   /* <-- the blackout fix: zero SetDeviceGammaRamp */
    CHECK(g_mock_mag_calls > 400); CHECK(g_mock_mag_bad == 0);
    CHECK(!exists(g_dirty_c)); CHECK(g_mock_dirty_creates == 0);    /* linear-only use never needs crash recovery */
    Eng_Shutdown(); CHECK(g_mock_set_total == 0); CHECK(g_mock_mag_uninit == 1);

    /* ---- S2: non-linear curves: skip-if-equal, restore-once ---- */
    scen("S2 curves: write once, skip identical, dirty flag once, restore orig once");
    reset_world(2); for (int d = 0; d < 2; d++) memcpy(g_mock_disp[d].hw, calib, sizeof calib);   /* calibrated displays */
    start(0);
    lk = neutral(); Eng_Apply(&lk);
    CHECK(g_mock_set_total == 0);                                   /* neutral curves: user's calibrated LUT untouched */
    lk.gamma = 0.80f; Eng_Apply(&lk);
    CHECK(g_mock_set_total == 2); CHECK(exists(g_dirty_c)); CHECK(g_mock_dirty_creates == 1);
    { unsigned short want[3][256]; cm_calc_ramp(&lk, want); CHECK(ramp_eq(g_mock_disp[0].hw, want) && ramp_eq(g_mock_disp[1].hw, want)); }
    for (int i = 0; i < 300; i++) Eng_Apply(&lk);
    CHECK(g_mock_set_total == 2);
    lk.gamma = 0.81f; Eng_Apply(&lk); CHECK(g_mock_set_total == 4); CHECK(g_mock_dirty_creates == 1);   /* dirty.flg not rewritten */
    for (int i = 0; i < 200; i++) { lk.sat = 50 + i; lk.bri = 50 + i / 2; Eng_Apply(&lk); }
    CHECK(g_mock_set_total == 4);                                   /* colour drags while a curve is active: no writes */
    lk = neutral(); Eng_Apply(&lk);
    CHECK(g_mock_set_total == 6);                                   /* restored once */
    CHECK(ramp_eq(g_mock_disp[0].hw, calib) && ramp_eq(g_mock_disp[1].hw, calib));   /* the ORIGINAL calibrated ramp, not identity */
    CHECK(!exists(g_dirty_c));
    Eng_Apply(&lk); Eng_Apply(&lk); CHECK(g_mock_set_total == 6);
    CHECK(g_mock_mag_bad == 0);
    Eng_Shutdown();

    /* ---- S3: Eng_Reset is forced ---- */
    scen("S3 Eng_Reset: forced identity matrix + original ramps, even when nothing changed");
    reset_world(2); start(0);
    lk = neutral(); lk.gamma = 0.7f; lk.sat = 200; Eng_Apply(&lk);
    int before = g_mock_set_total, mag_before = g_mock_mag_calls;
    Eng_Reset();
    CHECK(g_mock_set_total == before + 2); CHECK(g_mock_mag_calls == mag_before + 1); CHECK(cm_effect_is_identity(&g_mock_mag_last));
    CHECK(ramp_eq(g_mock_disp[0].hw, identity)); CHECK(!exists(g_dirty_c));
    before = g_mock_set_total; Eng_Reset(); CHECK(g_mock_set_total == before + 2);       /* forced: writes again */
    Eng_Shutdown();

    /* ---- S4: crash recovery with a valid ramps.dat ---- */
    scen("S4 dirty start + valid ramps.dat: original ramps restored, dirty flag cleared");
    reset_world(2); for (int d = 0; d < 2; d++) memcpy(g_mock_disp[d].hw, polluted, sizeof polluted);   /* left behind by the crash */
    write_orig_file(2, 1.10f); write_file(g_dirty_c, "x", 1);
    start(1);
    CHECK(ramp_eq(g_mock_disp[0].hw, calib) && ramp_eq(g_mock_disp[1].hw, calib));
    CHECK(!exists(g_dirty_c));
    lk = neutral(); Eng_Apply(&lk); CHECK(g_mock_set_total == 2);                         /* still no extra writes */
    Eng_Shutdown();

    /* ---- S5: corrupted ramps.dat -> identity ---- */
    scen("S5 dirty start + CORRUPTED ramps.dat: never applied, identity fallback");
    unsigned char good[4 + 2 * 1568], bad[sizeof good];
    reset_world(2); write_orig_file(2, 1.10f); read_file(g_ramps_c, good, sizeof good);
    struct { const char *name; size_t len; int kind; } corrupt[] = {
        { "truncated", sizeof good - 100, 0 }, { "zero ramp", sizeof good, 1 }, { "non-monotonic", sizeof good, 2 },
        { "count=0", sizeof good, 3 }, { "garbage name", sizeof good, 4 }, { "empty file", 0, 0 }, { "count>records", sizeof good, 5 } };
    for (size_t c = 0; c < sizeof corrupt / sizeof corrupt[0]; c++) {
        reset_world(2); for (int d = 0; d < 2; d++) memcpy(g_mock_disp[d].hw, polluted, sizeof polluted);
        memcpy(bad, good, sizeof bad);
        switch (corrupt[c].kind) {
        case 1: memset(bad + 4 + 32, 0, 1536); break;
        case 2: { unsigned short *w = (unsigned short *)(bad + 4 + 32); unsigned short t = w[100]; w[100] = w[101]; w[101] = t; break; }
        case 3: bad[0] = 0; break;
        case 4: bad[4] = 0x07; break;
        case 5: bad[0] = 3; break;
        }
        write_file(g_ramps_c, bad, corrupt[c].len); write_file(g_dirty_c, "x", 1);
        start(1);
        printf("         %-14s -> hw identity: %s\n", corrupt[c].name, (ramp_eq(g_mock_disp[0].hw, identity) && ramp_eq(g_mock_disp[1].hw, identity)) ? "yes" : "NO");
        CHECK(ramp_eq(g_mock_disp[0].hw, identity) && ramp_eq(g_mock_disp[1].hw, identity));
        CHECK(!exists(g_dirty_c));
        Eng_Shutdown();
    }

    /* ---- S6: missing ramps.dat -> identity ---- */
    scen("S6 dirty start + missing ramps.dat: identity fallback");
    reset_world(2); for (int d = 0; d < 2; d++) memcpy(g_mock_disp[d].hw, polluted, sizeof polluted);
    write_file(g_dirty_c, "x", 1); start(1);
    CHECK(ramp_eq(g_mock_disp[0].hw, identity) && ramp_eq(g_mock_disp[1].hw, identity)); CHECK(!exists(g_dirty_c));
    Eng_Shutdown();

    /* ---- S7: garbage on the GPU at a clean start; dithered calibration left alone ---- */
    scen("S7 clean start: all-black GPU ramp is replaced by identity; a dithered calibration is kept");
    reset_world(2); memset(g_mock_disp[0].hw, 0, sizeof g_mock_disp[0].hw);                 /* display 1: black ramp */
    memcpy(g_mock_disp[1].hw, calib, sizeof calib); g_mock_disp[1].hw[0][100] = (unsigned short)(g_mock_disp[1].hw[0][99] - 3);   /* display 2: dither dip */
    start(0);
    CHECK(ramp_eq(g_mock_disp[0].hw, identity)); CHECK(g_mock_disp[0].set_calls == 1);
    CHECK(g_mock_disp[1].set_calls == 0);                                                   /* user's LUT not overwritten */
    { unsigned char b[4 + 8 * 1568]; size_t n = read_file(g_ramps_c, b, sizeof b); CmRampRecord r[8];
      CHECK(cm_parse_ramps_blob(b, n, r, 8) == 2); CHECK(ramp_eq(r[0].ramp, identity)); CHECK(cm_ramp_is_valid(r[1].ramp)); }
    Eng_Shutdown();

    /* ---- S8: target monitor ---- */
    scen("S8 monitor targeting: only the selected display gets the curve, others return to original");
    reset_world(2); start(0);
    Eng_SetTargetMonitor(0); lk = neutral(); lk.gamma = 0.8f; Eng_Apply(&lk);
    CHECK(g_mock_disp[0].set_calls == 1 && g_mock_disp[1].set_calls == 0);
    Eng_SetTargetMonitor(-1); Eng_Apply(&lk);
    CHECK(g_mock_disp[0].set_calls == 1 && g_mock_disp[1].set_calls == 1);                   /* display 2 now follows, display 1 skipped */
    Eng_SetTargetMonitor(1); Eng_Apply(&lk);
    CHECK(g_mock_disp[0].set_calls == 2 && ramp_eq(g_mock_disp[0].hw, identity));            /* display 1 restored */
    CHECK(g_mock_disp[1].set_calls == 1);
    Eng_SetTargetMonitor(-1); Eng_Shutdown();

    /* ---- S9: resync ---- */
    scen("S9 Eng_Resync: active curve re-asserted once; untouched display left alone");
    reset_world(2); start(0);
    lk = neutral(); Eng_Apply(&lk); int calls0 = g_mock_mag_calls;
    Eng_Resync(); Eng_Apply(&lk);
    CHECK(g_mock_set_total == 0); CHECK(g_mock_mag_calls == calls0 + 1);                     /* matrix re-sent, LUT untouched */
    lk.gamma = 0.85f; Eng_Apply(&lk); CHECK(g_mock_set_total == 2);
    for (int d = 0; d < 2; d++) cm_identity_ramp(g_mock_disp[d].hw);                         /* a game resets the LUT behind our back */
    Eng_Apply(&lk); CHECK(g_mock_set_total == 2);                                            /* cache says up to date: no write (why Resync exists) */
    Eng_Resync(); Eng_Apply(&lk); CHECK(g_mock_set_total == 4);
    { unsigned short want[3][256]; cm_calc_ramp(&lk, want); CHECK(ramp_eq(g_mock_disp[0].hw, want)); }
    Eng_Apply(&lk); CHECK(g_mock_set_total == 4);
    Eng_Shutdown();

    /* ---- S10: shutdown ---- */
    scen("S10 shutdown restores what was changed and removes the dirty flag");
    reset_world(2); start(0);
    lk = neutral(); lk.clarity = 150; Eng_Apply(&lk); CHECK(exists(g_dirty_c));
    Eng_Shutdown();
    CHECK(ramp_eq(g_mock_disp[0].hw, identity) && ramp_eq(g_mock_disp[1].hw, identity)); CHECK(!exists(g_dirty_c));
    CHECK(g_mock_mag_uninit == 1); CHECK(cm_effect_is_identity(&g_mock_mag_last));

    /* ---- S11: failing writes keep the dirty flag ---- */
    scen("S11 a failed restore keeps dirty.flg so the next start retries");
    reset_world(2); start(0);
    lk = neutral(); lk.gamma = 0.7f; Eng_Apply(&lk); CHECK(exists(g_dirty_c));
    g_mock_disp[1].set_ok = 0; Eng_Reset();
    CHECK(exists(g_dirty_c));                                                                /* display 2 could not be restored */
    g_mock_disp[1].set_ok = 1; Eng_Reset(); CHECK(!exists(g_dirty_c));
    Eng_Shutdown();

    /* ---- S12: bypass ---- */
    scen("S12 engine bypassed (enabled=0): restores once, then stays quiet");
    reset_world(2); start(0);
    lk = neutral(); lk.gamma = 0.75f; lk.sat = 250; Eng_Apply(&lk); int w = g_mock_set_total;
    lk.enabled = 0; Eng_Apply(&lk);
    CHECK(g_mock_set_total == w + 2); CHECK(cm_effect_is_identity(&g_mock_mag_last)); CHECK(!exists(g_dirty_c));
    int m = g_mock_mag_calls; for (int i = 0; i < 100; i++) Eng_Apply(&lk);
    CHECK(g_mock_set_total == w + 2); CHECK(g_mock_mag_calls == m);
    Eng_Shutdown();

    /* ---- S13: hostile looks through the real Eng_Apply ---- */
    scen("S13 NaN / Inf / absurd looks through Eng_Apply: DWM only ever sees safe matrices");
    reset_world(2); start(0);
    for (int i = 0; i < 3000; i++) {
        Look h; h.enabled = 1; float *p = &h.sat;
        for (int k = 0; k < 16; k++) p[k] = (float)(((i * 2654435761u) >> (k % 7)) % 2000) - 500.0f;
        if (i % 11 == 0) { p[i % 16] = NAN; }
        if (i % 13 == 0) { p[(i / 13) % 16] = INFINITY; }
        if (i % 17 == 0) { p[(i / 17) % 16] = -INFINITY; }
        Eng_Apply(&h);
    }
    CHECK(g_mock_mag_bad == 0); CHECK(g_mock_mag_calls > 100);
    Eng_Shutdown(); CHECK(ramp_eq(g_mock_disp[0].hw, identity));


    /* ---- S14: a crash while recovery is still pending ---- */
    scen("S14 crash handler during a dirty start's Eng_Init must not bless the polluted ramp");
    reset_world(2); for (int d = 0; d < 2; d++) memcpy(g_mock_disp[d].hw, polluted, sizeof polluted);
    write_orig_file(2, 1.10f); write_file(g_dirty_c, "x", 1);
    g_hook_fired = 0; g_mock_hook_get_ramp = crash_during_init;
    start(1);
    g_mock_hook_get_ramp = NULL;
    CHECK(g_hook_fired);
    CHECK(g_hook_sets_after == g_hook_sets_before);            /* nothing written from an untrusted capture */
    CHECK(g_hook_dirty_after);                                 /* dirty flag kept for the retry */
    CHECK(ramp_eq(g_mock_disp[0].hw, calib) && ramp_eq(g_mock_disp[1].hw, calib));   /* the real recovery still completes */
    CHECK(!exists(g_dirty_c));
    Eng_Shutdown();
    /* and a crash before any display was even enumerated keeps a stale dirty flag too */
    reset_world(2); write_file(g_dirty_c, "x", 1);
    Eng_SetPaths(g_ramps_w, g_dirty_w, 1);
    Eng_Reset();                                               /* engine never initialised */
    CHECK(exists(g_dirty_c));
    rm(g_dirty_c);

    /* ---- S15: requested look survives a pipeline invalidation (ALT+TAB / Eng_Resync) ---- */
    scen("S15 requested vs applied: Resync must not overwrite the user's look");
    reset_world(2); start(0);
    lk = neutral(); lk.sat = 250; lk.vibrance = 250; lk.enabled = 1;
    Eng_Apply(&lk);
    CHECK(Eng_GetRequested() != NULL);
    CHECK(Eng_GetRequested()->sat == 250.f && Eng_GetRequested()->vibrance == 250.f);
    CHECK(Eng_GetApplied() != NULL && Eng_RequestedMatchesApplied());
    int mag_before_resync = g_mock_mag_calls;
    Eng_Invalidate(L"alt-tab");
    CHECK(Eng_GetRequested()->sat == 250.f);                 /* requested never clobbered */
    CHECK(Eng_GetRequested()->vibrance == 250.f);
    Eng_Reassert();
    CHECK(g_mock_mag_calls == mag_before_resync + 1);        /* DWM re-fed the same matrix */
    CHECK(Eng_GetApplied() && Eng_GetApplied()->vibrance == 250.f);
    CHECK(Eng_RequestedMatchesApplied());
    Eng_Shutdown();

    /* ---- S16 AppliedColorState is hardware truth, and the log dedupes ---- */
    scen("S16 applied-state truth: FULL after verified send, one log line per revision");
    reset_world(2); start(0);
    {
        unsigned logs_at_init = (unsigned)Eng_LogRing()->count;
        lk = neutral(); lk.sat = 180; lk.gamma = 0.9f;
        Eng_Apply(&lk);
        const AppliedColorState *ap = Eng_Applied();
        CHECK(ap && ap->have);
        CHECK(ap->outcome == PX_APPLY_FULL);
        CHECK(ap->matrix_ok && ap->matrix_verified && !ap->matrix_skipped);
        CHECK(ap->ramps_ok && ap->ramp_writes == 2);
        CHECK(ap->note[0] == 0);
        CHECK(Eng_RequestedMatchesApplied());
        unsigned logs_after = (unsigned)Eng_LogRing()->count;
        CHECK(logs_after >= logs_at_init + 1);
        Eng_ApplyNow(); Eng_ApplyNow();                       /* same revision + outcome: no re-log */
        CHECK((unsigned)Eng_LogRing()->count == logs_after);
        PxEngineSnapshot snap; PxSnap_Init(&snap);
        Eng_FillSnapshot(&snap);
        CHECK(snap.engine_ready && snap.engine_enabled);
        CHECK(snap.requested_revision == ap->revision);
        CHECK(snap.desktop_output == 1);                        /* readback verified */
        CHECK(snap.ramp_paths == 2 && snap.mag_available == 1);
        CHECK(snap.log_count >= 1);
        CHECK(snap.game.detected == 0);
    }
    Eng_Shutdown();

    /* ---- S17 readback proves a DWM drop -> PARTIAL, re-assert recovers to FULL ---- */
    scen("S17 readback mismatch (ALT+TAB drop): PARTIAL + note, never a silent FULL");
    reset_world(2); start(0);
    lk = neutral(); lk.vibrance = 250;
    Eng_Apply(&lk);
    CHECK(Eng_Applied()->outcome == PX_APPLY_FULL && Eng_RequestedMatchesApplied());
    g_mock_mag_readback_drift = 1;
    Eng_Invalidate(L"alt-tab-drop");
    Eng_ApplyNow();                                             /* DWM "accepts" but does not hold */
    CHECK(Eng_Applied()->outcome == PX_APPLY_PARTIAL);
    CHECK(!strcmp(Eng_Applied()->note, "readback mismatch: DWM dropped the effect"));
    CHECK(!Eng_RequestedMatchesApplied());                      /* UI must show RE-ASSERTING */
    CHECK(Eng_GetRequested()->vibrance == 250.f);               /* user's value NEVER rewritten */
    g_mock_mag_readback_drift = 0;                              /* recomposition finished */
    Eng_Reassert();
    CHECK(Eng_Applied()->outcome == PX_APPLY_FULL && Eng_RequestedMatchesApplied());
    Eng_Shutdown();

    /* ---- S18 OS without the readback API: honest FULL-but-unverified, no false alarm ---- */
    scen("S18 no MagGetFullscreenColorEffect: FULL, matrix_verified = 0 (unverified, not failed)");
    reset_world(2); g_mock_mag_noreadback = 1; start(0);
    lk = neutral(); lk.con = 120; Eng_Apply(&lk);
    CHECK(Eng_Applied()->outcome == PX_APPLY_FULL);
    CHECK(Eng_Applied()->matrix_ok && !Eng_Applied()->matrix_verified);
    g_mock_mag_noreadback = 0;
    Eng_Shutdown();

    /* ---- S19 hard DWM failure + mode bookkeeping + capability report ---- */
    scen("S19 MagSet failure: PARTIAL (ramps still ok); Eng_SetMode + Dm_ReportOutputs wired");
    reset_world(2); start(0);
    CHECK(g_mock_dm_report_calls >= 1 && g_mock_dm_report_mag == 1 && g_mock_dm_report_ramps == 2);
    Eng_SetMode(PX_CSMODE_GAME, 4, 2);
    CHECK(Eng_State()->mode == PX_CSMODE_GAME && Eng_State()->game_index == 4 && Eng_State()->game_sub == 2);
    unsigned rev0 = Eng_State()->revision;
    Eng_SetMode(PX_CSMODE_GLOBAL, -1, 0);
    CHECK(Eng_State()->revision == rev0 + 1);
    Eng_SetMode(PX_CSMODE_GLOBAL, -1, 0);                       /* no change: no bump */
    CHECK(Eng_State()->revision == rev0 + 1);
    g_mock_mag_set_fail = 1;
    Eng_Invalidate(L"inject");
    lk = neutral(); lk.bri = 150; Eng_Apply(&lk);
    CHECK(Eng_Applied()->outcome == PX_APPLY_PARTIAL);          /* ramps ok, matrix failed */
    CHECK(!Eng_RequestedMatchesApplied());
    g_mock_mag_set_fail = 0;
    Eng_Reassert();
    CHECK(Eng_Applied()->outcome == PX_APPLY_FULL);
    Eng_Shutdown();

    /* ---- S20 HDR output: PASSTHROUGH, and the effective look is identity ---- */
    scen("S20 HDR output: PASSTHROUGH with identity effective look, never ACTIVE");
    reset_world(2); start(0);
    g_mock_facts.hdr_any = 1;
    g_mock_facts.mon[0].hdr_enabled = 1;
    g_mock_facts.mon[0].color_space_raw = 0x0c;          /* HDR10 PQ */
    {
        Look hdrlook = neutral();
        hdrlook.sat = 200; hdrlook.vibrance = 180; hdrlook.gamma = 1.20f;
        Eng_Apply(&hdrlook);
        {
            const PxEffectiveState *eff = Eng_Effective();
            CHECK(eff->status == PX_STATUS_PASSTHROUGH);
            CHECK(eff->hdr_active == 1);
            CHECK(eff->linear_deliverable == 0 && eff->curves_deliverable == 0);
            CHECK(PxEff_Eq(eff->effective.sat, 100.0f) && PxEff_Eq(eff->effective.gamma, 1.0f));
            CHECK(eff->reason[0] != 0);
        }
    }
    Eng_Shutdown();

    /* ---- S21 exclusive fullscreen: LIMITED, curves-only effective look ---- */
    scen("S21 exclusive fullscreen: LIMITED, effective look is curves-only");
    reset_world(2); start(0);
    g_mock_facts.game.detected = 1;
    g_mock_facts.game.active = 1;
    g_mock_facts.game.presentation = PX_PRES_FULLSCREEN_SURFACE;
    g_mock_facts.game.profile_idx = 0;
    {
        Look ex = neutral();
        ex.sat = 220; ex.vibrance = 160;      /* linear part the matrix would carry */
        ex.gamma = 1.15f; ex.shadows = 130;   /* tone curves the LUT can carry      */
        Eng_Apply(&ex);
        {
            const PxEffectiveState *eff = Eng_Effective();
            CHECK(eff->exclusive_like == 1);
            CHECK(eff->status == PX_STATUS_LIMITED);
            CHECK(eff->linear_deliverable == 0);
            CHECK(eff->curves_deliverable == 1);
            CHECK(PxEff_Eq(eff->effective.sat, 100.0f));      /* chroma cannot be carried */
            CHECK(PxEff_Eq(eff->effective.gamma, 1.15f));     /* tone curve still can     */
            CHECK(eff->reason[0] != 0);
        }
    }
    Eng_Shutdown();

    printf("ENGINE HOST SCENARIOS PASSED: %d scenarios, %d checks\n", g_scen, g_checks);
    return 0;
}
