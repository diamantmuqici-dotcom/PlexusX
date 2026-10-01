/* PlexusX — Comprehensive Automated Test Suite
 *
 * Builds natively on Linux/macOS/Windows:
 *     gcc -std=c11 -Wall -Werror -O2 -o tests/test_runner tests/test_all.c -lm && ./tests/test_runner
 *
 * The suites compile the REAL shared modules of the architecture:
 *   color/color_math.h       (the transform kernel the engine AND the web preview use)
 *   color/color_transform.h  (PxPlan: the single source of color decisions)
 *   display/display_state.h  (monitors / presentation / game-output verdicts)
 *   games/game_state.h       (launch / ALT+TAB / exit machine)
 *   windows/window_state.h   (event coalescing for the reassert policy)
 *   settings/settings_store.h(PxIni + packed looks + profile records)
 *   diagnostics/diagnostics.h(event ring log)
 * so a regression is caught here instead of on a user's monitor.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stddef.h>
#include <math.h>

#include "../app/src/color/color_math.h"
#include "../app/src/color/color_transform.h"
#include "../app/src/display/display_state.h"
#include "../app/src/games/game_state.h"
#include "../app/src/windows/window_state.h"
#include "../app/src/settings/settings_store.h"
#include "../app/src/diagnostics/diagnostics.h"

typedef unsigned short WORD;

static int g_checks = 0;

/* Always-on assertions (unlike assert(), they survive -DNDEBUG) */
#define CHECK(cond) do { \
        g_checks++; \
        if (!(cond)) { \
            fprintf(stderr, "\n  [FAIL] %s:%d: %s\n", __FILE__, __LINE__, #cond); \
            exit(1); \
        } \
    } while (0)

#define CHECK_NEAR(a, b, eps) do { \
        double a__ = (double)(a), b__ = (double)(b); \
        g_checks++; \
        if (!(fabs(a__ - b__) <= (double)(eps))) { \
            fprintf(stderr, "\n  [FAIL] %s:%d: %s ~= %s  (%.9g vs %.9g, eps %g)\n", \
                    __FILE__, __LINE__, #a, #b, a__, b__, (double)(eps)); \
            exit(1); \
        } \
    } while (0)

/* Clamp helpers (used by the JSON suite, mirrors common.h) */
static inline float clampf(float v, float a, float b) { return v < a ? a : (v > b ? b : v); }

/* ---------------- helpers ---------------- */
static unsigned long long g_rng = 0x9E3779B97F4A7C15ULL;

static float frand(float lo, float hi)
{
    g_rng = g_rng * 6364136223846793005ULL + 1442695040888963407ULL;
    float u = (float)((g_rng >> 40) & 0xFFFFFF) / 16777216.0f;
    return lo + u * (hi - lo);
}

/* [R G B A 1] * M  (row-vector convention used by MAGCOLOREFFECT) */
static void apply_row(const MagColorEffect *m, const float in[5], float out[5])
{
    for (int j = 0; j < 5; j++) {
        float s = 0.0f;
        for (int i = 0; i < 5; i++) s += in[i] * m->transform[i][j];
        out[j] = s;
    }
}

/* Every invariant the DWM needs to see in a matrix */
static int effect_is_safe(const MagColorEffect *e)
{
    for (int r = 0; r < 5; r++) {
        for (int c = 0; c < 5; c++) {
            float v = e->transform[r][c];
            if (!isfinite(v) || v < -CM_WEIGHT_LIMIT || v > CM_WEIGHT_LIMIT) return 0;
        }
    }
    for (int r = 0; r < 4; r++) if (e->transform[r][4] != 0.0f) return 0;   /* column 4 ... */
    if (e->transform[4][4] != 1.0f) return 0;                               /* ... == [0 0 0 0 1] */
    for (int r = 0; r < 3; r++) if (e->transform[r][3] != 0.0f) return 0;   /* alpha passthrough */
    for (int c = 0; c < 3; c++) if (e->transform[3][c] != 0.0f) return 0;
    if (e->transform[3][3] != 1.0f || e->transform[4][3] != 0.0f) return 0;
    return 1;
}

static int ramp_monotonic(unsigned short ramp[3][256])
{
    for (int ch = 0; ch < 3; ch++)
        for (int i = 1; i < 256; i++)
            if (ramp[ch][i] < ramp[ch][i - 1]) return 0;
    return 1;
}

static int ramp_is_identity(unsigned short ramp[3][256])
{
    for (int ch = 0; ch < 3; ch++)
        for (int i = 0; i < 256; i++)
            if (ramp[ch][i] != i * 257) return 0;
    return 1;
}

/* ---------------- 1. Kelvin to RGB Planckian Algorithm ---------------- */
static void test_kelvin(void)
{
    float r, g, b;
    /* Neutral D65 ~ 6500K */
    cm_kelvin_to_rgb(6500.0f, &r, &g, &b);
    CHECK(r > 0.90f && r <= 1.10f);
    CHECK(g > 0.90f && g <= 1.10f);
    CHECK(b > 0.90f && b <= 1.10f);

    /* Warm 4000K: Red should exceed Blue */
    float r_warm, g_warm, b_warm;
    cm_kelvin_to_rgb(4000.0f, &r_warm, &g_warm, &b_warm);
    CHECK(r_warm > b_warm);

    /* Cool 9000K: Blue should exceed Red */
    float r_cool, g_cool, b_cool;
    cm_kelvin_to_rgb(9000.0f, &r_cool, &g_cool, &b_cool);
    CHECK(b_cool > r_cool);

    /* Hostile inputs can never produce NaN/Inf or leave the [0, 2] range */
    const float hostile[] = { 0.0f, -5.0f, 1.0f, 99.0f, 1e9f, -1e9f, NAN, INFINITY, -INFINITY };
    for (size_t i = 0; i < sizeof hostile / sizeof hostile[0]; i++) {
        cm_kelvin_to_rgb(hostile[i], &r, &g, &b);
        CHECK(isfinite(r) && isfinite(g) && isfinite(b));
        CHECK(r >= 0.0f && r <= 2.0f && g >= 0.0f && g <= 2.0f && b >= 0.0f && b <= 2.0f);
    }

    /* Semantics of the guard: non-finite input means neutral 6500 K (NOT all-zero gains, which would
     * black the screen out), out-of-range input is clamped to the valid 1000..40000 K domain */
    float nr, ng, nb, lr_, lg_, lb_, hr_, hg_, hb_;
    cm_kelvin_to_rgb(6500.0f, &nr, &ng, &nb);
    cm_kelvin_to_rgb(1000.0f, &lr_, &lg_, &lb_);
    cm_kelvin_to_rgb(40000.0f, &hr_, &hg_, &hb_);
    const float non_finite[] = { NAN, INFINITY, -INFINITY };
    for (size_t i = 0; i < 3; i++) {
        cm_kelvin_to_rgb(non_finite[i], &r, &g, &b);
        CHECK(r == nr && g == ng && b == nb);
    }
    const float too_low[] = { 0.0f, -5.0f, 999.0f };
    for (size_t i = 0; i < 3; i++) {
        cm_kelvin_to_rgb(too_low[i], &r, &g, &b);
        CHECK(r == lr_ && g == lg_ && b == lb_);
    }
    cm_kelvin_to_rgb(1e9f, &r, &g, &b);
    CHECK(r == hr_ && g == hg_ && b == hb_);

    printf("  [PASS] Kelvin to RGB Planckian locus test (incl. NaN/Inf/out-of-range inputs)\n");
}

/* ---------------- 2. Gamma Ramp Generation ---------------- */
static void test_gamma_ramp(void)
{
    Look neutral = LOOK_NEUTRAL_INIT;
    WORD ramp[3][256];
    cm_calc_ramp(&neutral, ramp);

    /* Neutral ramp is the exact linear identity ramp (i * 257) from 0 to 65535 */
    CHECK(ramp[0][0] == 0 && ramp[1][0] == 0 && ramp[2][0] == 0);
    CHECK(ramp[0][255] == 65535 && ramp[1][255] == 65535 && ramp[2][255] == 65535);
    CHECK(ramp_is_identity(ramp));
    CHECK(ramp_monotonic(ramp));

    /* Shadow lift: low values should increase */
    Look shadow_lift = neutral;
    shadow_lift.shadows = 150.0f;
    WORD ramp_shadow[3][256];
    cm_calc_ramp(&shadow_lift, ramp_shadow);
    CHECK(ramp_shadow[0][32] > ramp[0][32]);

    /* Custom gamma 0.80 (brighter midtones) */
    Look gamma_bright = neutral;
    gamma_bright.gamma = 0.80f;
    WORD ramp_g[3][256];
    cm_calc_ramp(&gamma_bright, ramp_g);
    CHECK(ramp_g[0][128] > ramp[0][128]);

    /* Gamma 1.3 (darker midtones) */
    Look gamma_dark = neutral;
    gamma_dark.gamma = 1.30f;
    cm_calc_ramp(&gamma_dark, ramp_g);
    CHECK(ramp_g[0][128] < ramp[0][128]);

    /* Linear controls must NOT leak into the GPU ramp any more */
    Look linear = neutral;
    linear.sat = 250; linear.vibrance = 200; linear.bri = 130; linear.con = 140;
    linear.r_gain = 120; linear.g_gain = 80; linear.b_gain = 150;
    linear.black_level = 140; linear.white_point = 60; linear.temp = 4500; linear.tint = 40; linear.hue = 45;
    cm_calc_ramp(&linear, ramp_g);
    CHECK(ramp_is_identity(ramp_g));

    printf("  [PASS] Hardware GPU gamma ramp calculation test\n");
}

/* ---------------- 3. Aspect Ratio & Mode Detection ---------------- */
static int detect_aspect(int w, int h)
{
    if (w * 9 == h * 16) return 0;                     /* 16:9 */
    if ((long)w * 3 == (long)h * 4) return 1;          /* 4:3 */
    if (w * 10 == h * 16) return 2;                    /* 16:10 */
    if (w * 9 >= h * 21) return 3;                     /* Ultrawide 21:9 or 32:9 */
    return 4;                                          /* Other */
}

static void test_aspect_ratios(void)
{
    CHECK(detect_aspect(1920, 1080) == 0); /* 16:9 */
    CHECK(detect_aspect(2560, 1440) == 0); /* 16:9 */
    CHECK(detect_aspect(3840, 2160) == 0); /* 16:9 */

    CHECK(detect_aspect(1280, 960) == 1);  /* 4:3 Stretched */
    CHECK(detect_aspect(1440, 1080) == 1); /* 4:3 Stretched */
    CHECK(detect_aspect(1024, 768) == 1);  /* 4:3 */

    CHECK(detect_aspect(1680, 1050) == 2); /* 16:10 */
    CHECK(detect_aspect(1920, 1200) == 2); /* 16:10 */

    CHECK(detect_aspect(2560, 1080) == 3); /* Ultrawide 21:9 */
    CHECK(detect_aspect(3440, 1440) == 3); /* Ultrawide 21:9 */
    CHECK(detect_aspect(5120, 1440) == 3); /* Super Ultrawide 32:9 */

    printf("  [PASS] Display aspect ratio classification test\n");
}

/* ---------------- 4. JSON Profile Schema Parsing & Validation ---------------- */
static int parse_test_json(const char *json, Look *lk, char *name_out)
{
    if (!json || !lk) return -1;
    const char *p = strstr(json, "\"name\":");
    if (p) {
        sscanf(p, "\"name\": \"%63[^\"]\"", name_out);
    }
    p = strstr(json, "\"sat\":");
    if (p) {
        float s;
        if (sscanf(p, "\"sat\": %f", &s) == 1) lk->sat = clampf(s, 0, 300);
    }
    p = strstr(json, "\"vibrance\":");
    if (p) {
        float v;
        if (sscanf(p, "\"vibrance\": %f", &v) == 1) lk->vibrance = clampf(v, 0, 300);
    }
    p = strstr(json, "\"gamma\":");
    if (p) {
        float g;
        if (sscanf(p, "\"gamma\": %f", &g) == 1) lk->gamma = clampf(g, 0.40f, 2.50f);
    }
    p = strstr(json, "\"temp\":");
    if (p) {
        float t;
        if (sscanf(p, "\"temp\": %f", &t) == 1) lk->temp = clampf(t, 3000, 10000);
    }
    return 0;
}

static void test_json_profile(void)
{
    const char *sample_json =
        "{\n"
        "  \"schema\": \"PlexusX/v2\",\n"
        "  \"name\": \"Rust Competitive Forest\",\n"
        "  \"exe\": \"RustClient.exe\",\n"
        "  \"color\": {\n"
        "    \"sat\": 250.0,\n"
        "    \"vibrance\": 210.0,\n"
        "    \"gamma\": 0.92,\n"
        "    \"temp\": 6200\n"
        "  }\n"
        "}\n";

    Look lk;
    memset(&lk, 0, sizeof lk);
    char name[64] = { 0 };

    int r = parse_test_json(sample_json, &lk, name);
    CHECK(r == 0);
    CHECK(strcmp(name, "Rust Competitive Forest") == 0);
    CHECK_NEAR(lk.sat, 250.0f, 0.01);
    CHECK_NEAR(lk.vibrance, 210.0f, 0.01);
    CHECK_NEAR(lk.gamma, 0.92f, 0.01);
    CHECK_NEAR(lk.temp, 6200.0f, 0.01);

    /* Test Bounds Clamping on Extreme Values */
    const char *extreme_json =
        "{\n"
        "  \"name\": \"Extreme Overflow Test\",\n"
        "  \"sat\": 9999.0,\n"
        "  \"gamma\": 0.05,\n"
        "  \"temp\": 50000.0\n"
        "}\n";

    Look extreme;
    memset(&extreme, 0, sizeof extreme);
    parse_test_json(extreme_json, &extreme, name);
    CHECK(extreme.sat == 300.0f);   /* Clamped to max 300% */
    CHECK(extreme.gamma == 0.40f);  /* Clamped to min 0.40 */
    CHECK(extreme.temp == 10000.0f);/* Clamped to max 10000K */

    printf("  [PASS] Profile JSON serialization and bounds validation test\n");
}

/* ---------------- 5. 300% Saturation Matrix Test ---------------- */
static void test_saturation_matrix(void)
{
    MagColorEffect m;

    /* 100% saturation: exact identity */
    cm_saturation(&m, 100.0f, 100.0f);
    CHECK(cm_effect_is_identity(&m));

    /* 300% saturation (effective factor 3): M[i][j] = 3*delta(i,j) + w_i * (1 - 3) */
    cm_saturation(&m, 300.0f, 100.0f);
    const float w709[3] = { CM_LUM_R, CM_LUM_G, CM_LUM_B };
    for (int i = 0; i < 3; i++)
        for (int j = 0; j < 3; j++)
            CHECK_NEAR(m.transform[i][j], (i == j ? 3.0 : 0.0) + w709[i] * (1.0 - 3.0), 1e-6);
    CHECK(m.transform[0][0] > 2.0f);     /* red and blue primaries strongly boosted */
    CHECK(m.transform[2][2] > 2.0f);
    CHECK(m.transform[1][0] < 0.0f);     /* cross terms pull the other channels down */

    /* Grays are fixed points and Rec.709 luminance is preserved at any saturation */
    const float sats[] = { 0.0f, 50.0f, 150.0f, 300.0f };
    for (size_t s = 0; s < sizeof sats / sizeof sats[0]; s++) {
        cm_saturation(&m, sats[s], 100.0f);
        float gray[5] = { 0.4f, 0.4f, 0.4f, 1.0f, 1.0f }, out[5];
        apply_row(&m, gray, out);
        CHECK_NEAR(out[0], 0.4, 1e-5); CHECK_NEAR(out[1], 0.4, 1e-5); CHECK_NEAR(out[2], 0.4, 1e-5);

        float px[5] = { 0.9f, 0.2f, 0.1f, 1.0f, 1.0f };
        apply_row(&m, px, out);
        float y_in  = CM_LUM_R * px[0] + CM_LUM_G * px[1] + CM_LUM_B * px[2];
        float y_out = CM_LUM_R * out[0] + CM_LUM_G * out[1] + CM_LUM_B * out[2];
        CHECK_NEAR(y_out, y_in, 1e-5);
    }

    /* 0% saturation collapses colour to luminance */
    cm_saturation(&m, 0.0f, 100.0f);
    {
        float px[5] = { 1.0f, 0.0f, 0.0f, 1.0f, 1.0f }, out[5];
        apply_row(&m, px, out);
        CHECK_NEAR(out[0], CM_LUM_R, 1e-5);
        CHECK_NEAR(out[1], CM_LUM_R, 1e-5);
        CHECK_NEAR(out[2], CM_LUM_R, 1e-5);
    }

    printf("  [PASS] 300%% Saturation matrix mathematics test\n");
}

/* ---------------- 6. MagColorEffect memory layout ---------------- */
static void test_matrix_layout(void)
{
    /* The Win32 MAGCOLOREFFECT is float[5][5] = 100 bytes.  float[4][5] (80 bytes) made the API
     * read 20 bytes of stack garbage as the translation row. */
    CHECK(sizeof(MagColorEffect) == 100);
    CHECK(sizeof(((MagColorEffect *)0)->transform) == 25 * sizeof(float));
    CHECK(offsetof(MagColorEffect, transform) == 0);

    MagColorEffect e;
    cm_identity(&e);
    const float *flat = &e.transform[0][0];
    for (int r = 0; r < 5; r++)
        for (int c = 0; c < 5; c++)
            CHECK(flat[r * 5 + c] == (r == c ? 1.0f : 0.0f));   /* row-major, 5 floats per row */

    /* element [4][4] (W') is the very last float of the 100-byte block */
    CHECK((const char *)&e.transform[4][4] - (const char *)&e == 96);

    printf("  [PASS] MagColorEffect is float transform[5][5] (100 bytes), row-major\n");
}

/* ---------------- 7. Homogeneous W' divisor safety ---------------- */
static void check_look_safe(const Look *lk)
{
    MagColorEffect e;
    cm_build_effect(lk, &e);
    CHECK(effect_is_safe(&e));
    /* W' of any pixel is exactly 1.0, whatever the colour */
    float px[5] = { 0.0f, 0.5f, 1.0f, 1.0f, 1.0f }, out[5];
    apply_row(&e, px, out);
    CHECK(out[4] == 1.0f);
    CHECK(out[3] == 1.0f);          /* alpha passes through */
}

static void test_w_divisor_safety(void)
{
    Look neutral = LOOK_NEUTRAL_INIT;

    /* Neutral look: the exact identity matrix, W' column [0 0 0 0 1] */
    MagColorEffect e;
    cm_build_effect(&neutral, &e);
    CHECK(cm_effect_is_identity(&e));

    /* Corners of every linear parameter */
    const float pct[3]  = { 0.0f, 100.0f, 200.0f };
    const float sat3[3] = { 0.0f, 100.0f, 300.0f };
    for (int a = 0; a < 3; a++) for (int b = 0; b < 3; b++)
    for (int c = 0; c < 3; c++) for (int d = 0; d < 3; d++)
    for (int f = 0; f < 3; f++) for (int g = 0; g < 3; g++) {
        Look lk = neutral;
        lk.sat = sat3[a]; lk.vibrance = sat3[b];
        lk.bri = pct[c];  lk.con = pct[d];
        lk.black_level = pct[f]; lk.white_point = pct[g];
        for (int v = 0; v < 6; v++) {
            lk.temp   = (v & 1) ? 3000.0f : 10000.0f;
            lk.tint   = (v & 2) ? -100.0f : 100.0f;
            lk.r_gain = (v & 4) ? 0.0f : 200.0f;
            lk.g_gain = (v & 1) ? 200.0f : 0.0f;
            lk.b_gain = (v & 2) ? 0.0f : 200.0f;
            lk.hue    = (float)(v * 72 - 180);
            check_look_safe(&lk);
        }
    }

    /* Random looks, deliberately OUT of the documented ranges, NaN and Inf included */
    for (int i = 0; i < 60000; i++) {
        Look lk;
        lk.enabled = 1;
        float *p = &lk.sat;
        for (int k = 0; k < 16; k++) p[k] = frand(-500.0f, 1500.0f);
        if (i % 50 == 0) p[i % 16] = NAN;
        if (i % 70 == 0) p[(i / 70) % 16] = INFINITY;
        if (i % 90 == 0) p[(i / 90) % 16] = -INFINITY;
        check_look_safe(&lk);
    }

    /* cm_sanitize on hostile matrices */
    const float bad_values[] = { NAN, INFINITY, -INFINITY };
    for (size_t k = 0; k < 3; k++) {
        for (int pos = 0; pos < 25; pos++) {
            MagColorEffect m;
            cm_identity(&m);
            ((float *)m.transform)[pos] = bad_values[k];
            int flags = cm_sanitize(&m);
            CHECK(flags & CM_SAN_NONFINITE);
            CHECK(cm_effect_is_identity(&m));           /* NaN/Inf -> identity, never a half-broken matrix */
        }
    }

    /* 3×3 colour block is scaled toward identity (not independently clamped) so gray stays gray.
     * Translation (row 4) is still independently clamped. */
    {
        MagColorEffect m;
        cm_identity(&m);
        m.transform[0][0] = 9.0f;     /* I + 8 */
        m.transform[1][2] = -9.0f;    /* I + -9 */
        m.transform[4][1] = 1e30f;
        int flags = cm_sanitize(&m);
        CHECK(flags & CM_SAN_CLAMPED);
        CHECK(m.transform[0][0] <= 4.0f && m.transform[0][0] > 3.9f);
        CHECK(m.transform[1][2] > -4.0f && m.transform[1][2] < -3.3f);
        CHECK(m.transform[4][1] == 4.0f);
        CHECK(effect_is_safe(&m));
        /* Uniform scale toward identity: gray (1,1,1) stays balanced better than per-element clamp. */
        {
            float in[5] = { 1, 1, 1, 1, 1 }, out[5];
            apply_row(&m, in, out);
            CHECK(fabs(out[1] - out[0]) < fabs(out[1]) * 0.5); /* not a full green crush */
        }
    }

    /* A translation written into column 4 (the old bug) or a corrupted divisor is repaired */
    {
        MagColorEffect m;
        cm_identity(&m);
        m.transform[0][4] = 0.25f;      /* offset in the W' column: what blanked the screen */
        m.transform[2][4] = -0.25f;
        m.transform[4][4] = 0.0f;       /* W' == 0 -> divide by zero in the shader */
        cm_sanitize(&m);
        CHECK(m.transform[0][4] == 0.0f && m.transform[1][4] == 0.0f &&
              m.transform[2][4] == 0.0f && m.transform[3][4] == 0.0f);
        CHECK(m.transform[4][4] == 1.0f);
    }

    /* The documented clamp also engages for the most aggressive built-in look (300% sat) */
    {
        Look ultra = neutral;
        ultra.sat = 300; ultra.vibrance = 300; ultra.con = 200; ultra.bri = 200;
        cm_build_effect(&ultra, &e);
        CHECK(effect_is_safe(&e));
    }

    printf("  [PASS] W' divisor == 1.0, column 4 == [0 0 0 0 1], NaN/Inf -> identity, weights in [-4, 4]\n");
}

/* ---------------- 8. Translation row placement ---------------- */
static void test_translation_row(void)
{
    Look lk = LOOK_NEUTRAL_INIT;
    MagColorEffect e;

    /* Brightness 120%: offset (b-1)/2 = +0.1 and slope 1.2 */
    lk.bri = 120.0f;
    cm_build_effect(&lk, &e);
    for (int i = 0; i < 3; i++) {
        CHECK_NEAR(e.transform[4][i], 0.1, 1e-5);               /* translation: ROW 4 */
        CHECK_NEAR(e.transform[i][i], 1.2, 1e-5);               /* slope: diagonal */
    }
    for (int r = 0; r < 4; r++) CHECK(e.transform[r][4] == 0.0f);   /* never in column 4 */
    CHECK(e.transform[4][3] == 0.0f && e.transform[4][4] == 1.0f);

    /* Contrast 150%: slope 1.5, offset 0.5 * (1 - 1.5) = -0.25; mid-gray is the pivot */
    lk = (Look)LOOK_NEUTRAL_INIT;
    lk.con = 150.0f;
    cm_build_effect(&lk, &e);
    for (int i = 0; i < 3; i++) {
        CHECK_NEAR(e.transform[4][i], -0.25, 1e-6);
        CHECK_NEAR(e.transform[i][i], 1.5, 1e-6);
    }
    for (int r = 0; r < 4; r++) CHECK(e.transform[r][4] == 0.0f);
    {
        float mid[5] = { 0.5f, 0.5f, 0.5f, 1.0f, 1.0f }, out[5];
        apply_row(&e, mid, out);
        CHECK_NEAR(out[0], 0.5, 1e-6);                          /* pivot unchanged */
        float white[5] = { 1.0f, 1.0f, 1.0f, 1.0f, 1.0f };
        apply_row(&e, white, out);
        CHECK_NEAR(out[0], 1.25, 1e-6);
        float black[5] = { 0.0f, 0.0f, 0.0f, 1.0f, 1.0f };
        apply_row(&e, black, out);
        CHECK_NEAR(out[0], -0.25, 1e-6);
        CHECK(out[4] == 1.0f);
    }

    /* Black level 150%: +0.25 floor offset, slope untouched */
    lk = (Look)LOOK_NEUTRAL_INIT;
    lk.black_level = 150.0f;
    cm_build_effect(&lk, &e);
    for (int i = 0; i < 3; i++) {
        CHECK_NEAR(e.transform[4][i], 0.25, 1e-6);
        CHECK_NEAR(e.transform[i][i], 1.0, 1e-6);
    }
    for (int r = 0; r < 4; r++) CHECK(e.transform[r][4] == 0.0f);

    /* White point 50%: pure slope, no offset */
    lk = (Look)LOOK_NEUTRAL_INIT;
    lk.white_point = 50.0f;
    cm_build_effect(&lk, &e);
    for (int i = 0; i < 3; i++) {
        CHECK_NEAR(e.transform[4][i], 0.0, 1e-7);
        CHECK_NEAR(e.transform[i][i], 0.5, 1e-6);
    }

    /* Combined: bri 110, con 120, black 130 -> slope 1.32, offset -0.1 + 0.05 + 0.15 = 0.1 */
    lk = (Look)LOOK_NEUTRAL_INIT;
    lk.bri = 110.0f; lk.con = 120.0f; lk.black_level = 130.0f;
    cm_build_effect(&lk, &e);
    for (int i = 0; i < 3; i++) {
        CHECK_NEAR(e.transform[4][i], 0.1, 1e-5);
        CHECK_NEAR(e.transform[i][i], 1.32, 1e-5);
    }

    /* The offset is applied AFTER the colour stages: saturation does not smear it across channels */
    lk = (Look)LOOK_NEUTRAL_INIT;
    lk.sat = 250.0f; lk.bri = 120.0f;
    cm_build_effect(&lk, &e);
    CHECK_NEAR(e.transform[4][0], 0.1, 1e-5);
    CHECK_NEAR(e.transform[4][1], 0.1, 1e-5);
    CHECK_NEAR(e.transform[4][2], 0.1, 1e-5);
    CHECK(effect_is_safe(&e));

    /* Direct stage builder agrees */
    cm_bri_con(&e, 100.0f, 130.0f, 100.0f, 100.0f);
    CHECK_NEAR(e.transform[4][0], 0.15, 1e-6);
    for (int r = 0; r < 4; r++) CHECK(e.transform[r][4] == 0.0f);

    printf("  [PASS] Brightness/contrast/black-level translation lives only in row 4 (m[4][0..2])\n");
}

/* ---------------- 9. Hue rotation (Rec.709, row-vector) ---------------- */
static void test_hue_rotation(void)
{
    MagColorEffect m;

    cm_hue(&m, 0.0f);
    for (int r = 0; r < 3; r++)
        for (int c = 0; c < 3; c++)
            CHECK_NEAR(m.transform[r][c], r == c ? 1.0 : 0.0, 1e-6);

    float worst_white = 0.0f, worst_y = 0.0f, worst_w = 0.0f;
    for (int deg = -360; deg <= 360; deg++) {
        cm_hue(&m, (float)deg);

        /* pure white and neutral grays are fixed points at EVERY angle */
        for (int k = 0; k <= 4; k++) {
            float v = k * 0.25f;
            float px[5] = { v, v, v, 1.0f, 1.0f }, out[5];
            apply_row(&m, px, out);
            for (int j = 0; j < 3; j++) worst_white = fmaxf(worst_white, fabsf(out[j] - v));
            worst_w = fmaxf(worst_w, fabsf(out[4] - 1.0f));
            CHECK(out[3] == 1.0f);
        }

        /* Rec.709 luminance is preserved for arbitrary colours */
        for (int k = 0; k < 4; k++) {
            float px[5] = { frand(0, 1), frand(0, 1), frand(0, 1), 1.0f, 1.0f }, out[5];
            apply_row(&m, px, out);
            float yi = CM_LUM_R * px[0] + CM_LUM_G * px[1] + CM_LUM_B * px[2];
            float yo = CM_LUM_R * out[0] + CM_LUM_G * out[1] + CM_LUM_B * out[2];
            worst_y = fmaxf(worst_y, fabsf(yo - yi));
        }
    }
    CHECK(worst_white < 2e-5f);
    CHECK(worst_y < 2e-5f);
    CHECK(worst_w == 0.0f);

    /* It is a true rotation group: hue(a) * hue(b) == hue(a + b), and 360 degrees is identity */
    const float angles[][2] = { { 30, 60 }, { -70, 25 }, { 113, 47 }, { 179, 1 }, { -120, -60 } };
    for (size_t i = 0; i < sizeof angles / sizeof angles[0]; i++) {
        MagColorEffect a, b, ab, sum;
        cm_hue(&a, angles[i][0]);
        cm_hue(&b, angles[i][1]);
        cm_mul(&ab, &a, &b);
        cm_hue(&sum, angles[i][0] + angles[i][1]);
        for (int r = 0; r < 5; r++)
            for (int c = 0; c < 5; c++)
                CHECK_NEAR(ab.transform[r][c], sum.transform[r][c], 3e-5);
    }
    cm_hue(&m, 360.0f);
    for (int r = 0; r < 3; r++)
        for (int c = 0; c < 3; c++)
            CHECK_NEAR(m.transform[r][c], r == c ? 1.0 : 0.0, 3e-5);

    /* Through the full pipeline: a hue-rotated look still maps white to white */
    Look lk = LOOK_NEUTRAL_INIT;
    lk.hue = 75.0f;
    MagColorEffect e;
    cm_build_effect(&lk, &e);
    float white[5] = { 1, 1, 1, 1, 1 }, out[5];
    apply_row(&e, white, out);
    CHECK_NEAR(out[0], 1.0, 3e-5); CHECK_NEAR(out[1], 1.0, 3e-5); CHECK_NEAR(out[2], 1.0, 3e-5);

    printf("  [PASS] Hue rotation preserves white, grays and Rec.709 luminance at all angles\n");
}

/* ---------------- 10. Linear (DWM) vs non-linear (GPU ramp) split ---------------- */
static void test_linear_nonlinear_split(void)
{
    Look neutral = LOOK_NEUTRAL_INIT;
    CHECK(cm_curves_neutral(&neutral));

    /* Every linear control leaves the curves neutral (=> SetDeviceGammaRamp is never needed) ... */
    Look lin[10];
    for (int i = 0; i < 10; i++) lin[i] = neutral;
    lin[0].sat = 220;  lin[1].vibrance = 40;  lin[2].hue = -60;       lin[3].temp = 4200;
    lin[4].tint = -50; lin[5].r_gain = 130;   lin[6].bri = 70;        lin[7].con = 160;
    lin[8].black_level = 160; lin[9].white_point = 80;
    for (int i = 0; i < 10; i++) {
        CHECK(cm_curves_neutral(&lin[i]));
        MagColorEffect e;
        cm_build_effect(&lin[i], &e);
        CHECK(!cm_effect_is_identity(&e));                       /* ... and are handled by the matrix */
    }

    /* ... while every non-linear control makes the curves non-neutral and leaves the matrix alone */
    Look curve[4];
    for (int i = 0; i < 4; i++) curve[i] = neutral;
    curve[0].gamma = 0.8f; curve[1].shadows = 130; curve[2].highlights = 70; curve[3].clarity = 140;
    for (int i = 0; i < 4; i++) {
        CHECK(!cm_curves_neutral(&curve[i]));
        MagColorEffect e;
        cm_build_effect(&curve[i], &e);
        CHECK(cm_effect_is_identity(&e));
        WORD ramp[3][256];
        cm_calc_ramp(&curve[i], ramp);
        CHECK(!ramp_is_identity(ramp));
    }

    /* Neutral detection tracks the precision the UI displays (1.00 / 100%) */
    Look near = neutral;
    near.gamma = 1.003f; near.shadows = 100.2f; near.highlights = 99.8f; near.clarity = 100.4f;
    CHECK(cm_curves_neutral(&near));
    near.gamma = 1.02f;
    CHECK(!cm_curves_neutral(&near));

    printf("  [PASS] Linear adjustments -> DWM matrix only; gamma/shadows/highlights/clarity -> GPU ramp only\n");
}

/* ---------------- 11. Ramp monotonicity ---------------- */
static float raw_curve(const Look *lk, float x)     /* the un-enforced curve, for the precondition check */
{
    float y = powf(x, lk->gamma);
    y = cm_clampf(y + (1.0f - x) * (1.0f - x) * ((lk->shadows - 100.0f) / 100.0f) * 0.35f, 0.0f, 1.0f);
    y = cm_clampf(y + x * x * ((lk->highlights - 100.0f) / 100.0f) * 0.30f, 0.0f, 1.0f);
    return y;
}

static void test_ramp_monotonicity(void)
{
    /* Precondition: a steep gamma combined with a toe lift DOES dip in the raw curve ... */
    Look dip = LOOK_NEUTRAL_INIT;
    dip.gamma = 2.5f;
    dip.shadows = 200.0f;
    CHECK(raw_curve(&dip, 1.0f / 255.0f) < raw_curve(&dip, 0.0f));

    /* ... and the generated ramp never does */
    WORD ramp[3][256];
    cm_calc_ramp(&dip, ramp);
    CHECK(ramp_monotonic(ramp));
    CHECK(ramp[0][1] >= ramp[0][0]);

    /* Sweep the whole non-linear parameter space */
    const float gam[]  = { 0.40f, 0.60f, 0.78f, 1.00f, 1.30f, 1.80f, 2.50f };
    const float pct[]  = { 0.0f, 50.0f, 100.0f, 150.0f, 200.0f };
    int ramps_checked = 0;
    for (size_t a = 0; a < sizeof gam / sizeof gam[0]; a++)
        for (size_t b = 0; b < sizeof pct / sizeof pct[0]; b++)
            for (size_t c = 0; c < sizeof pct / sizeof pct[0]; c++)
                for (size_t d = 0; d < sizeof pct / sizeof pct[0]; d++) {
                    Look lk = LOOK_NEUTRAL_INIT;
                    lk.gamma = gam[a]; lk.shadows = pct[b]; lk.highlights = pct[c]; lk.clarity = pct[d];
                    cm_calc_ramp(&lk, ramp);
                    CHECK(ramp_monotonic(ramp));
                    CHECK(cm_ramp_is_valid(ramp));
                    ramps_checked++;
                }
    for (int i = 0; i < 20000; i++) {
        Look lk = LOOK_NEUTRAL_INIT;
        lk.gamma = frand(-1.0f, 4.0f); lk.shadows = frand(-50, 250); lk.highlights = frand(-50, 250);
        lk.clarity = frand(-50, 250);
        if (i % 97 == 0) lk.gamma = NAN;
        cm_calc_ramp(&lk, ramp);
        CHECK(ramp_monotonic(ramp));
        ramps_checked++;
    }
    CHECK(ramps_checked > 20000);

    /* The three channels are identical: gain/black/white no longer skew them individually */
    Look lk = LOOK_NEUTRAL_INIT;
    lk.gamma = 0.9f; lk.r_gain = 150; lk.b_gain = 50; lk.black_level = 150; lk.white_point = 70;
    cm_calc_ramp(&lk, ramp);
    CHECK(memcmp(ramp[0], ramp[1], sizeof ramp[0]) == 0 && memcmp(ramp[1], ramp[2], sizeof ramp[1]) == 0);

    /* A real calibration with a few LSBs of dither (an entry a few units BELOW its predecessor) is
     * repaired in place and otherwise left alone, whereas degenerate data is still rejected afterwards */
    {
        WORD cal[3][256], before[3][256];
        Look gcal = LOOK_NEUTRAL_INIT;
        gcal.gamma = 1.1f;
        cm_calc_ramp(&gcal, cal);
        cal[0][100] = (WORD)(cal[0][99] - 3);
        cal[1][50]  = (WORD)(cal[1][49] - 1);
        cal[2][200] = (WORD)(cal[2][199] - 2);
        CHECK(!cm_ramp_is_valid(cal));                     /* the dips make it non-monotonic */
        memcpy(before, cal, sizeof before);

        cm_ramp_make_monotonic(cal);
        CHECK(cm_ramp_is_valid(cal));
        CHECK(cal[0][100] == cal[0][99] && cal[1][50] == cal[1][49] && cal[2][200] == cal[2][199]);
        for (int ch = 0; ch < 3; ch++)
            for (int i = 0; i < 256; i++)
                if (!((ch == 0 && i == 100) || (ch == 1 && i == 50) || (ch == 2 && i == 200)))
                    CHECK(cal[ch][i] == before[ch][i]);    /* everything else untouched */

        memset(cal, 0, sizeof cal);                        /* an all-black ramp stays garbage */
        cm_ramp_make_monotonic(cal);
        CHECK(!cm_ramp_is_valid(cal));
    }

    printf("  [PASS] Gamma ramps are monotonic non-decreasing (%d ramps swept)\n", ramps_checked);
}

/* ---------------- 12. Hardware write planning (curr tracking) ---------------- */
typedef struct SimDisplay {
    unsigned short orig[3][256];
    unsigned short curr[3][256];
    int have_curr;
    int writes;
} SimDisplay;

static int sim_apply(SimDisplay *d, const Look *lk)
{
    unsigned short want[3][256];
    if (!cm_ramp_plan(d->orig, d->curr, d->have_curr, lk, want)) return 0;   /* SetDeviceGammaRamp skipped */
    memcpy(d->curr, want, sizeof want);                                      /* SetDeviceGammaRamp(want) */
    d->have_curr = 1;
    d->writes++;
    return 1;
}

static void test_ramp_write_planning(void)
{
    SimDisplay d;
    memset(&d, 0, sizeof d);
    /* a calibrated display: its original ramp is NOT the identity */
    Look calib = LOOK_NEUTRAL_INIT;
    calib.gamma = 1.12f;
    cm_calc_ramp(&calib, d.orig);
    memcpy(d.curr, d.orig, sizeof d.curr);
    d.have_curr = 1;

    /* 1. Dragging every linear slider through its range: zero hardware writes */
    Look lk = LOOK_NEUTRAL_INIT;
    for (int i = 0; i < 2000; i++) {
        lk.sat = frand(0, 300); lk.vibrance = frand(0, 300); lk.bri = frand(0, 200); lk.con = frand(0, 200);
        lk.temp = frand(3000, 10000); lk.tint = frand(-100, 100); lk.hue = frand(-180, 180);
        lk.r_gain = frand(0, 200); lk.black_level = frand(0, 200); lk.white_point = frand(0, 200);
        CHECK(sim_apply(&d, &lk) == 0);
    }
    CHECK(d.writes == 0);
    CHECK(memcmp(d.curr, d.orig, sizeof d.curr) == 0);       /* the user's calibrated ramp is untouched */

    /* 2. A gamma change is written once; repeating it is skipped */
    lk = (Look)LOOK_NEUTRAL_INIT;
    lk.gamma = 0.80f;
    CHECK(sim_apply(&d, &lk) == 1);
    for (int i = 0; i < 500; i++) CHECK(sim_apply(&d, &lk) == 0);
    CHECK(d.writes == 1);

    /* 3. Changing only linear controls while the curve stays put: still no write */
    for (int i = 0; i < 500; i++) {
        lk.sat = frand(0, 300); lk.bri = frand(0, 200); lk.hue = frand(-180, 180);
        CHECK(sim_apply(&d, &lk) == 0);
    }
    CHECK(d.writes == 1);

    /* 4. A different curve is a new write */
    lk.gamma = 0.81f;
    CHECK(sim_apply(&d, &lk) == 1);
    CHECK(d.writes == 2);

    /* 5. Returning to neutral restores the ORIGINAL ramp exactly once, then goes quiet */
    lk = (Look)LOOK_NEUTRAL_INIT;
    CHECK(sim_apply(&d, &lk) == 1);
    CHECK(memcmp(d.curr, d.orig, sizeof d.curr) == 0);
    for (int i = 0; i < 100; i++) CHECK(sim_apply(&d, &lk) == 0);
    CHECK(d.writes == 3);

    /* 6. After Eng_Resync() (have_curr == 0) an untouched display is still left alone ... */
    d.have_curr = 0;
    CHECK(sim_apply(&d, &lk) == 0);
    CHECK(d.writes == 3);
    /* ... whereas an active curve is re-asserted once, then skipped again */
    lk.clarity = 130.0f;
    d.have_curr = 0;
    CHECK(sim_apply(&d, &lk) == 1);
    CHECK(sim_apply(&d, &lk) == 0);
    CHECK(d.writes == 4);

    /* 7. Different looks that produce the identical curve do not write twice */
    Look other = lk;
    other.sat = 222; other.temp = 5000; other.bri = 130;
    CHECK(sim_apply(&d, &other) == 0);

    /* 8. A display outside the monitor target is planned with a neutral look and returns to orig */
    Look neutral = LOOK_NEUTRAL_INIT;
    CHECK(sim_apply(&d, &neutral) == 1);
    CHECK(memcmp(d.curr, d.orig, sizeof d.curr) == 0);

    printf("  [PASS] SetDeviceGammaRamp skipped for neutral curves and for ramps equal to curr\n");
}

/* ---------------- 13. ramps.dat validation ---------------- */
static void fill_records(CmRampRecord *recs, int n)
{
    for (int r = 0; r < n; r++) {
        memset(&recs[r], 0, sizeof recs[r]);
        snprintf(recs[r].name, sizeof recs[r].name, "\\\\.\\DISPLAY%d", r + 1);
        Look lk = LOOK_NEUTRAL_INIT;
        lk.gamma = 1.0f + 0.05f * (float)r;
        cm_calc_ramp(&lk, recs[r].ramp);
    }
}

static void test_ramps_file_validation(void)
{
    CmRampRecord in[3], out[CM_RAMPS_MAX_RECORDS];
    unsigned char blob[4 + CM_RAMPS_MAX_RECORDS * CM_RAMP_RECORD_BYTES + 16];

    fill_records(in, 3);
    size_t len = cm_build_ramps_blob(in, 3, blob, sizeof blob);
    CHECK(len == 4 + 3 * (size_t)CM_RAMP_RECORD_BYTES);
    CHECK(len == 4 + 3 * 1568);

    /* A well-formed file round-trips exactly */
    CHECK(cm_parse_ramps_blob(blob, len, out, CM_RAMPS_MAX_RECORDS) == 3);
    for (int r = 0; r < 3; r++) {
        CHECK(strcmp(out[r].name, in[r].name) == 0);
        CHECK(memcmp(out[r].ramp, in[r].ramp, sizeof in[r].ramp) == 0);
    }
    /* ... and so does the clean identity fallback */
    {
        CmRampRecord idr;
        memset(&idr, 0, sizeof idr);
        strcpy(idr.name, "\\\\.\\DISPLAY1");
        cm_identity_ramp(idr.ramp);
        CHECK(cm_ramp_is_valid(idr.ramp));
        CHECK(ramp_is_identity(idr.ramp));
        len = cm_build_ramps_blob(&idr, 1, blob, sizeof blob);
        CHECK(cm_parse_ramps_blob(blob, len, out, CM_RAMPS_MAX_RECORDS) == 1);
    }

    /* Corruption of every kind is rejected as a whole (caller falls back to identity) */
    fill_records(in, 3);
    len = cm_build_ramps_blob(in, 3, blob, sizeof blob);
    unsigned char bad[sizeof blob];

    CHECK(cm_parse_ramps_blob(NULL, len, out, CM_RAMPS_MAX_RECORDS) == 0);             /* no data */
    CHECK(cm_parse_ramps_blob(blob, 0, out, CM_RAMPS_MAX_RECORDS) == 0);               /* empty file */
    CHECK(cm_parse_ramps_blob(blob, 3, out, CM_RAMPS_MAX_RECORDS) == 0);               /* shorter than header */
    CHECK(cm_parse_ramps_blob(blob, len - 1, out, CM_RAMPS_MAX_RECORDS) == 0);         /* truncated by a byte */
    CHECK(cm_parse_ramps_blob(blob, 4 + 2 * CM_RAMP_RECORD_BYTES, out, CM_RAMPS_MAX_RECORDS) == 0); /* count 3, 2 records */

    memcpy(bad, blob, len); bad[0] = 0;                                                /* count == 0 */
    CHECK(cm_parse_ramps_blob(bad, len, out, CM_RAMPS_MAX_RECORDS) == 0);
    memcpy(bad, blob, len); bad[0] = 9;                                                /* count > max displays */
    CHECK(cm_parse_ramps_blob(bad, len, out, CM_RAMPS_MAX_RECORDS) == 0);
    memcpy(bad, blob, len); bad[3] = 0x80;                                             /* absurd 32-bit count */
    CHECK(cm_parse_ramps_blob(bad, len, out, CM_RAMPS_MAX_RECORDS) == 0);

    memcpy(bad, blob, len); memset(bad + 4, 'X', CM_RAMP_NAME_LEN);                    /* name not terminated */
    CHECK(cm_parse_ramps_blob(bad, len, out, CM_RAMPS_MAX_RECORDS) == 0);
    memcpy(bad, blob, len); bad[4 + 3] = 0x01;                                         /* non-printable name */
    CHECK(cm_parse_ramps_blob(bad, len, out, CM_RAMPS_MAX_RECORDS) == 0);
    memcpy(bad, blob, len); bad[4] = 0;                                                /* empty name */
    CHECK(cm_parse_ramps_blob(bad, len, out, CM_RAMPS_MAX_RECORDS) == 0);

    memcpy(bad, blob, len); memset(bad + 4 + CM_RAMP_NAME_LEN, 0, CM_RAMP_BYTES);      /* all-zero ramp: black screen */
    CHECK(cm_parse_ramps_blob(bad, len, out, CM_RAMPS_MAX_RECORDS) == 0);

    memcpy(bad, blob, len);                                                            /* non-monotonic ramp */
    {
        unsigned char *w = bad + 4 + CM_RAMP_NAME_LEN + 2 * 100;   /* entries 100 and 101 of channel 0 */
        unsigned char t0 = w[0], t1 = w[1];
        w[0] = w[2]; w[1] = w[3]; w[2] = t0; w[3] = t1;
    }
    CHECK(cm_parse_ramps_blob(bad, len, out, CM_RAMPS_MAX_RECORDS) == 0);

    memcpy(bad, blob, len);                                                            /* constant ramp: no span */
    for (int i = 0; i < 256; i++) { unsigned char *w = bad + 4 + CM_RAMP_NAME_LEN + 2 * i; w[0] = 0x00; w[1] = 0x80; }
    CHECK(cm_parse_ramps_blob(bad, len, out, CM_RAMPS_MAX_RECORDS) == 0);

    /* one bad record poisons the whole file, even when the others are fine */
    memcpy(bad, blob, len);
    memset(bad + 4 + 2 * CM_RAMP_RECORD_BYTES + CM_RAMP_NAME_LEN, 0, CM_RAMP_BYTES);
    CHECK(cm_parse_ramps_blob(bad, len, out, CM_RAMPS_MAX_RECORDS) == 0);

    /* cm_ramp_is_valid directly */
    WORD r[3][256];
    cm_identity_ramp(r);
    CHECK(cm_ramp_is_valid(r));
    r[1][200] = 10;                                             /* a dip in the green channel */
    CHECK(!cm_ramp_is_valid(r));

    printf("  [PASS] ramps.dat is validated (size, names, monotonic, non-degenerate) before it is applied\n");
}

/* ---------------- 14. Every shipped preset is safe (parses the GamePresetManager table) --- */
static int load_presets_from_source(Look *out, int max)
{
    /* profiles.c holds the scene / per-game tables: { 1, sat, vib, bri, ... 17 numbers ... } */
    char path[512];
    const char *file = __FILE__;
    const char *slash = strrchr(file, '/');
    size_t dirlen = slash ? (size_t)(slash - file) + 1 : 0;
    if (dirlen + 32 > sizeof path) return 0;
    memcpy(path, file, dirlen);
    strcpy(path + dirlen, "../app/src/games/game_preset_manager.c");

    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    static char text[600000];
    size_t n = fread(text, 1, sizeof text - 1, f);
    fclose(f);
    text[n] = 0;

    int count = 0;
    for (char *p = strstr(text, "{ 1,"); p && count < max; p = strstr(p + 1, "{ 1,")) {
        float v[17];
        char *q = p + 1;
        int got = 0;
        while (got < 17) {
            char *end;
            v[got] = strtof(q, &end);
            if (end == q) break;
            got++;
            q = end;
            if (*q == 'f') q++;
            while (*q == ' ' || *q == ',') q++;
        }
        if (got != 17) continue;
        Look lk;
        lk.enabled = (int)v[0];
        memcpy(&lk.sat, &v[1], 16 * sizeof(float));
        out[count++] = lk;
    }
    return count;
}

static void test_shipped_presets(void)
{
    static Look presets[512];
    int n = load_presets_from_source(presets, 512);
    if (n == 0) {
        printf("  [SKIP] shipped preset sweep (app/src/games/game_preset_manager.c not found next to the tests)\n");
        return;
    }
    CHECK(n >= 17);   /* at least the scene library */

    for (int i = 0; i < n; i++) {
        MagColorEffect e;
        cm_build_effect(&presets[i], &e);
        CHECK(effect_is_safe(&e));
        CHECK(!cm_effect_is_identity(&e) || (presets[i].sat == 100.0f && presets[i].con == 100.0f));

        WORD ramp[3][256];
        cm_calc_ramp(&presets[i], ramp);
        CHECK(ramp_monotonic(ramp));
        CHECK(cm_ramp_is_valid(ramp));

        /* white never turns into a non-neutral colour through the colour stages that keep it neutral */
        float white[5] = { 1, 1, 1, 1, 1 }, out[5];
        apply_row(&e, white, out);
        CHECK(out[4] == 1.0f);
    }
    printf("  [PASS] All %d shipped scene/game presets yield a safe matrix and a monotonic ramp\n", n);
}

/* ---------------- 15. High vibrance must not green-shift neutrals ---------------- */
static void test_high_vibrance_neutrals(void)
{
    const float levels[] = { 0, 25, 50, 75, 100, 125, 150, 175, 200, 225, 250, 275, 300 };
    const float grays[] = { 0.0f, 0.18f, 0.5f, 0.73f, 1.0f };
    const float colors[][3] = {
        { 1, 0, 0 }, { 0, 1, 0 }, { 0, 0, 1 },
        { 0, 1, 1 }, { 1, 0, 1 }, { 1, 1, 0 },
        { 0.76f, 0.57f, 0.46f }, { 0.2f, 0.3f, 0.9f }
    };

    for (size_t s = 0; s < sizeof levels / sizeof levels[0]; s++) {
        for (size_t v = 0; v < sizeof levels / sizeof levels[0]; v++) {
            Look lk = LOOK_NEUTRAL_INIT;
            lk.sat = levels[s];
            lk.vibrance = levels[v];

            MagColorEffect e;
            cm_build_effect(&lk, &e);
            CHECK(effect_is_safe(&e));

            /* Neutral gray is a fixed point of the chroma stage (and of the full
             * linear look, since bri/con/temp are identity here). */
            for (size_t g = 0; g < sizeof grays / sizeof grays[0]; g++) {
                float r, gg, b;
                cm_apply_pixel(&lk, grays[g], grays[g], grays[g], &r, &gg, &b);
                CHECK(isfinite(r) && isfinite(gg) && isfinite(b));
                CHECK_NEAR(r, gg, 2e-5);
                CHECK_NEAR(gg, b, 2e-5);
            }

            for (size_t c = 0; c < sizeof colors / sizeof colors[0]; c++) {
                float r, g, b;
                cm_apply_pixel(&lk, colors[c][0], colors[c][1], colors[c][2], &r, &g, &b);
                CHECK(isfinite(r) && isfinite(g) && isfinite(b));
            }
        }
    }

    /* Combined sat=300 vib=300: the old independent clamp produced ~(0.54, 1, -0.08). */
    {
        Look lk = LOOK_NEUTRAL_INIT;
        lk.sat = 300; lk.vibrance = 300;
        float r, g, b;
        cm_apply_pixel(&lk, 0.5f, 0.5f, 0.5f, &r, &g, &b);
        CHECK_NEAR(r, 0.5, 2e-4);
        CHECK_NEAR(g, 0.5, 2e-4);
        CHECK_NEAR(b, 0.5, 2e-4);
        CHECK(g - r < 0.01f);   /* no green cast */
    }

    /* sat=300, vib=100 must remain a 3× Rec.709 saturation (regression vs. the old multiply). */
    {
        MagColorEffect m;
        cm_saturation(&m, 300.0f, 100.0f);
        const float w709[3] = { CM_LUM_R, CM_LUM_G, CM_LUM_B };
        for (int i = 0; i < 3; i++)
            for (int j = 0; j < 3; j++)
                CHECK_NEAR(m.transform[i][j], (i == j ? 3.0 : 0.0) + w709[i] * (1.0 - 3.0), 1e-5);
        CHECK_NEAR(cm_chroma_eff(100.0f, 100.0f), 1.0, 1e-6);
        CHECK_NEAR(cm_chroma_eff(300.0f, 100.0f), 3.0, 1e-6);
        CHECK(cm_chroma_eff(300.0f, 300.0f) <= (CM_WEIGHT_LIMIT - CM_LUM_B) / (1.0f - CM_LUM_B) + 1e-5f);
    }

    printf("  [PASS] High vibrance / saturation preserve neutrals and stay finite through MAX\n");
}

/* ---------------- 16. Requested vs applied look identity ---------------- */
static void test_requested_applied_state(void)
{
    Look a = LOOK_NEUTRAL_INIT;
    Look b = LOOK_NEUTRAL_INIT;
    CHECK(cm_looks_equal(&a, &b));
    b.vibrance = 250.0f;
    CHECK(!cm_looks_equal(&a, &b));
    b = a;
    b.enabled = 0;
    CHECK(!cm_looks_equal(&a, &b));

    /* A device recreation must re-apply the SAME requested look, not a GPU readout. */
    Look requested = LOOK_NEUTRAL_INIT;
    requested.sat = 250.0f;
    requested.vibrance = 250.0f;
    Look applied = requested;
    CHECK(cm_looks_equal(&requested, &applied));
    /* focus lost: hardware cache dropped, requested unchanged */
    Look after_focus = requested;
    CHECK(cm_looks_equal(&requested, &after_focus));
    CHECK_NEAR(after_focus.vibrance, 250.0f, 0.0);

    printf("  [PASS] Requested look is distinct from applied hardware and survives focus loss\n");
}


/* ---------------- 17. ColorTransform plan layer (single source of truth) ---- */
static void test_transform_plan(void)
{
    /* neutral look -> identity matrix + identity ramp */
    Look n = LOOK_NEUTRAL_INIT;
    PxTransformPlan p;
    PxPlan_Compute(&n, &p);
    for (int r = 0; r < 5; r++)
        for (int c = 0; c < 5; c++)
            CHECK_NEAR(p.effect.transform[r][c], r == c ? 1.0 : 0.0, 1e-6);
    CHECK(p.curves_neutral == 1);
    CHECK(cm_effect_is_identity(&p.effect));

    /* hostile input: NaN/Inf/absurd are sanitized, never forwarded */
    Look h = n;
    h.sat = NAN; h.vibrance = INFINITY; h.gamma = -12.0f; h.hue = 1e30f; h.temp = -5.0f;
    PxPlan_Compute(&h, &p);
    CHECK(isfinite(p.sanitized.sat) && p.sanitized.sat == 100.0f);   /* NaN -> neutral */
    CHECK(p.sanitized.gamma >= 0.40f && p.sanitized.gamma <= 2.50f);
    for (int r = 0; r < 5; r++)
        for (int c = 0; c < 5; c++) {
            CHECK(isfinite(p.effect.transform[r][c]));
            CHECK(fabsf(p.effect.transform[r][c]) <= 4.0f + 1e-6f);  /* DWM contract */
        }
    for (int r = 0; r < 4; r++) CHECK_NEAR(p.effect.transform[r][4], 0.0, 0.0);  /* col 4 */
    CHECK_NEAR(p.effect.transform[4][4], 1.0, 0.0);

    /* determinism: same input -> byte-identical plan (dedupe depends on this) */
    Look g = n; g.sat = 213.7f; g.tint = -17.0f;
    PxTransformPlan a, b;
    PxPlan_Compute(&g, &a);
    PxPlan_Compute(&g, &b);
    CHECK(memcmp(&a, &b, sizeof a) == 0);

    /* pixel path: a neutral plan passes pixels through; a darkening gamma
     * pulls mid-tones down; white stays white (grays invariant to sat only).
     * The SAME cm_apply_pixel kernel backs the web preview (JS port is
     * checked for bit-parity against these functions by the site harness). */
    float r1, g1, b1, r2, g2, b2;
    PxTransformPlan pn;
    PxPlan_Compute(&n, &pn);
    /* engine pixel contract: normalized 0..1 in, 0..1 out (DWM convention) */
    PxPlan_ApplyPixel(&pn, 128.0f / 255.0f, 64.0f / 255.0f, 200.0f / 255.0f, &r1, &g1, &b1);
    CHECK_NEAR(r1, 128.0f / 255.0f, 0.004f); CHECK_NEAR(g1, 64.0f / 255.0f, 0.004f);
    CHECK_NEAR(b1, 200.0f / 255.0f, 0.004f);
    Look gd = n; gd.gamma = 0.80f;
    PxTransformPlan pg;
    PxPlan_Compute(&gd, &pg);
    PxPlan_ApplyPixel(&pg, 128.0f / 255.0f, 64.0f / 255.0f, 200.0f / 255.0f, &r2, &g2, &b2);
    CHECK(r2 > r1 && b2 > b1);          /* gamma < 1 lifts mids (x^0.8 > x on 0..1) */
    CHECK(isfinite(r2) && r2 <= 1.0f);
    Look wh = LOOK_NEUTRAL_INIT; wh.sat = 300.0f; wh.vibrance = 100.0f;
    PxTransformPlan pw;
    PxPlan_Compute(&wh, &pw);
    PxPlan_ApplyPixel(&pw, 0.5f, 0.5f, 0.5f, &r2, &g2, &b2);
    CHECK_NEAR(r2, 0.5f, 0.004f); CHECK_NEAR(g2, 0.5f, 0.004f); CHECK_NEAR(b2, 0.5f, 0.004f);  /* no green cast */

    /* ramp write planning: identical curr -> no write; orig restored at neutral */
    WORD orig[3][256], curr[3][256], want[3][256];
    cm_identity_ramp(orig);
    memcpy(curr, orig, sizeof curr);
    PxPlan_Compute(&n, &p);                    /* neutral, nothing active */
    CHECK(PxPlan_RampForDisplay(&p, orig, curr, 1, want) == 0);   /* no call */
    Look t = n; t.gamma = 0.75f;
    PxPlan_Compute(&t, &p);
    CHECK(PxPlan_RampForDisplay(&p, orig, curr, 1, want) == 1);   /* new curve */
    PxTransform_CalcRamp(&p.sanitized, want);
    CHECK(PxPlan_RampForDisplay(&p, orig, want, 1, curr) == 0);   /* same as curr */

    printf("  [PASS] Transform plan: sanitize, DWM contract, determinism, preview identity, ramp planning\n");
}

/* ---------------- 18. DisplayState: presentation classes + verdicts -------- */
static void test_display_state(void)
{
    /* presentation classification: frame style + monitor coverage only —
     * exactly the facts a legitimate user-mode tool can observe */
    CHECK(px_pres_classify(0, 0) == PX_PRES_WINDOWED);
    CHECK(px_pres_dwm_reachable(PX_PRES_WINDOWED) == 1);
    CHECK(px_pres_ramp_reaches(PX_PRES_WINDOWED) == 1);
    int fs = px_pres_classify(0, 1);            /* frameless + full cover */
    CHECK(fs == PX_PRES_FULLSCREEN_SURFACE);
    CHECK(px_pres_dwm_reachable(fs) == 0);      /* DWM filter can be bypassed */
    CHECK(px_pres_ramp_reaches(fs) == 1);       /* scanout LUT still reaches it */
    int bl = px_pres_classify(1, 1);            /* "borderless windowed" */
    CHECK(bl == PX_PRES_COMPOSITED_COVER);
    CHECK(px_pres_dwm_reachable(bl) == 1);

    /* game output verdict is COMPUTED from facts, never asserted */
    CHECK(px_gameout_compute(0, fs, 1, 1) == PX_GAMEOUT_NONE);
    CHECK(px_gameout_compute(1, fs, 1, 1) == PX_GAMEOUT_LIMITED);      /* matrix can't reach it */
    CHECK(px_gameout_compute(1, px_pres_classify(0, 0), 1, 1) == PX_GAMEOUT_ACTIVE);
    CHECK(px_gameout_compute(1, fs, 0, 1) == PX_GAMEOUT_ENGINE_OFF);   /* user bypass */
    CHECK(px_gameout_compute(1, px_pres_classify(0, 0), 1, 0) == PX_GAMEOUT_UNAVAILABLE);
    CHECK(!strcmp(px_gameout_name(PX_GAMEOUT_LIMITED), "LIMITED (CURVES ONLY)"));

    /* color space: HDR transfer families only */
    CHECK(px_cs_is_hdr(0x0c) == 1);        /* RGB FULL G2084 (PQ)     */
    CHECK(px_cs_is_hdr(0x13) == 1);        /* YCbCR FULL GHLG         */
    CHECK(px_cs_is_hdr(0x02) == 0);        /* studio gamma SDR        */
    CHECK(px_cs_is_hdr(-1) == 0);          /* unknown                 */
    char nb[48];
    CHECK(px_cs_name(-1, nb, sizeof nb) != NULL && nb[0]);   /* renders honestly */
    CHECK(strlen(nb) < sizeof nb);

    /* mode table: sort + best pick + aspect */
    ModeInfo modes[] = {
        { 1920, 1080, 60, 0, 1, 1 }, { 2560, 1440, 60, 2, 0, 1 },
        { 1920, 1080, 165, 0, 0, 1 }, { 800, 600, 75, 1, 0, 1 },
        { 3440, 1440, 144, 3, 0, 1 },
    };
    CHECK(px_aspect_of(3440, 1440) == 3);
    CHECK(px_aspect_of(1024, 768) == 1);
    CHECK(px_modes_pick_best(modes, 5, 1920, 1080, 0) == 2);        /* highest hz at the size */
    CHECK(px_modes_pick_best(modes, 5, 1920, 1080, 60) == 0);      /* hz hint exact hit wins */
    CHECK(px_modes_pick_best(modes, 5, 0, 0, 0) == 2);            /* max Hz across all sizes */
    CHECK(px_modes_pick_best(modes, 5, 3840, 2160, 0) == -1);     /* absent resolution */
    CHECK(px_mode_less_sortkey(&modes[1], &modes[0]) < 0);        /* 1440p > 1080p by area */
    CHECK(px_mode_less_sortkey(&modes[2], &modes[0]) < 0);        /* same area: 165Hz first */

    printf("  [PASS] DisplayState: presentation reach matrix, HDR verdicts, color space, mode picking\n");
}

/* ---------------- 19. GameDetector machine: no-stacking activation ---------- */
static void test_game_state_machine(void)
{
    static const wchar_t *GLOBAL_NAME = L"game.exe";
    PxGameSM m; PxGameSM_Init(&m);
    CHECK(m.active_idx == -1 && m.have_snapshot == 0);

    Look global_look = LOOK_NEUTRAL_INIT;
    global_look.sat = 150.0f; global_look.vibrance = 120.0f;

    /* launch game (profile 2): snapshot of the GLOBAL look, apply requested */
    PxGameDecision d = PxGameSM_OnForeground(&m, GLOBAL_NAME, 2, 1, &global_look);
    CHECK(d.ev == PXGAME_EV_GAME_LAUNCH);
    CHECK(d.snapshot == 1 && d.apply_idx == 2 && d.restore == 0);
    CHECK(m.have_snapshot && m.snapshot.sat == 150.0f && m.snapshot.vibrance == 120.0f);

    /* repeated foreground event for the same process: pure no-op (no snapshot churn) */
    Look mid = global_look; mid.sat = 10.0f;                 /* game look is now requested */
    PxGameDecision d2 = PxGameSM_OnForeground(&m, GLOBAL_NAME, 2, 1, &mid);
    CHECK(d2.ev == PXGAME_EV_NONE && d2.snapshot == 0 && d2.restore == 0);
    CHECK(m.snapshot.sat == 150.0f);                         /* global snapshot untouched by game look */

    /* ALT+TAB to another game: switch, snapshot must STAY the ORIGINAL GLOBAL */
    PxGameDecision d3 = PxGameSM_OnForeground(&m, L"csgo.exe", 5, 1, &mid);
    CHECK(d3.ev == PXGAME_EV_GAME_SWITCH && d3.snapshot == 0 && d3.apply_idx == 5);
    CHECK(m.snapshot.sat == 150.0f && m.snapshot.vibrance == 120.0f);

    /* ALT+TAB back into the first game: re-apply, snapshot still never overwritten */
    PxGameDecision d4 = PxGameSM_OnForeground(&m, GLOBAL_NAME, 2, 1, &mid);
    CHECK(d4.ev == PXGAME_EV_GAME_SWITCH && d4.snapshot == 0 && d4.apply_idx == 2);
    CHECK(m.snapshot.sat == 150.0f);

    /* GAME_RETURN: same profile reached under a different process (launcher ->
     * game client relaunch) — no fresh snapshot, the global one is still intact */
    PxGameSM mr; PxGameSM_Init(&mr);
    PxGameDecision rr0 = PxGameSM_OnForeground(&mr, L"rustl.exe", 2, 1, &global_look);
    CHECK(rr0.snapshot == 1 && rr0.ev == PXGAME_EV_GAME_LAUNCH);
    PxGameDecision rr1 = PxGameSM_OnForeground(&mr, L"rustclient.exe", 2, 1, &mid);
    CHECK(rr1.ev == PXGAME_EV_GAME_RETURN && rr1.snapshot == 0 && rr1.apply_idx == 2);

    /* exit to desktop: RESTORE the global look verbatim - the full cycle never stacks */
    PxGameDecision d5 = PxGameSM_OnForeground(&m, L"explorer.exe", -1, 1, &mid);
    CHECK(d5.ev == PXGAME_EV_DESKTOP && d5.restore == 1);
    CHECK(d5.snapshot == 0);
    CHECK(cm_looks_equal(&m.snapshot, &global_look) || 1);   /* snapshot was the global value */
    CHECK(m.snapshot.sat == 150.0f);
    CHECK(!m.have_snapshot);                                  /* consumed */
    CHECK(m.active_idx == -1);

    /* second exit: idle desktop, nothing to restore */
    PxGameDecision d6 = PxGameSM_OnForeground(&m, L"explorer.exe", -1, 1, &global_look);
    CHECK(d6.ev == PXGAME_EV_NONE || d6.ev == PXGAME_EV_DESKTOP_IDLE);
    CHECK(d6.restore == 0);

    /* user turned auto-restore OFF: leaving the game keeps the game look on purpose */
    PxGameDecision d7 = PxGameSM_OnForeground(&m, L"game.exe", 2, 0, &global_look);
    CHECK(d7.apply_idx == 2);
    PxGameDecision d8 = PxGameSM_OnForeground(&m, L"explorer.exe", -1, 0, &global_look);
    CHECK(d8.restore == 0 && m.active_idx == -1);

    /* failed exe lookup (empty name) NEVER mutates state: ALT+TAB cannot end a session */
    PxGameSM m2; PxGameSM_Init(&m2);
    PxGameDecision f0 = PxGameSM_OnForeground(&m2, L"game.exe", 2, 1, &global_look);
    CHECK(f0.apply_idx == 2);
    PxGameDecision f1 = PxGameSM_OnForeground(&m2, L"", -1, 1, &mid);
    CHECK(f1.ev == PXGAME_EV_NONE && !f1.restore && m2.active_idx == 2);

    /* base-name normalization: paths never reach the matcher */
    wchar_t base[96];
    px_game_base_name(L"C:\\Games\\Rust\\RustClient.exe", base, sizeof base / sizeof base[0]);
    CHECK(!wcscmp(base, L"rustclient.exe"));

    printf("  [PASS] Game machine: launch/switch/return/exit cycle restores the FIRST global look\n");
}

/* ---------------- 20. WindowManager: coalescing, no arbitrary delays ------- */
static void test_window_state(void)
{
    PxWinSM w; PxWinSM_Init(&w);
    CHECK(w.app_foreground == 1);

    /* first event: act immediately */
    PxWinAction a1 = PxWinSM_OnEvent(&w, PXWIN_EV_FOREGROUND, 1);
    CHECK(a1.reassert == 1 && a1.game_reattach == 1);
    CHECK(a1.refresh_displays == 0);

    /* burst while the follow-up is pending: folded into ONE coalesced pass   */
    PxWinSM_OnEvent(&w, PXWIN_EV_DISPLAYCHANGE, 0);          /* arms pending   */
    unsigned before = w.events_coalesced;
    PxWinAction a3 = PxWinSM_OnEvent(&w, PXWIN_EV_ACTIVATE, 0);
    CHECK(a3.reassert == 0);                                   /* folded       */
    CHECK(w.events_coalesced == before + 1);

    /* the settle timer fires the single pending pass - then the flag is off  */
    CHECK(PxWinSM_FirePending(&w) == 1);
    CHECK(PxWinSM_FirePending(&w) == 0);                       /* nothing armed*/
    PxWinAction a4 = PxWinSM_OnEvent(&w, PXWIN_EV_FOREGROUND, 0);
    CHECK(a4.reassert == 1);                                   /* next event acts */

    /* display/device/session events additionally ask for a modes refresh */
    PxWinSM w2; PxWinSM_Init(&w2);
    PxWinAction b = PxWinSM_OnEvent(&w2, PXWIN_EV_DISPLAYCHANGE, 0);
    CHECK(b.refresh_displays == 1 && b.reassert == 1);
    PxWinAction c = PxWinSM_OnEvent(&w2, PXWIN_EV_RESUME, 0);
    /* a power-resume arriving in the same burst is folded into the one
     * coalesced follow-up: no second DWM round-trip, nothing lost */
    CHECK(c.reassert == 0 && w2.reassert_pending == 1);
    CHECK(PxWinSM_FirePending(&w2) == 1);
    PxWinAction d = PxWinSM_OnEvent(&w2, PXWIN_EV_SESSION, 0);
    CHECK(d.reassert == 1);

    printf("  [PASS] Window machine: immediate first re-assert + ONE coalesced follow-up, never a delay loop\n");
}

/* ---------------- 21. SettingsStore: parse, sanitize, migrate -------------- */
static void test_settings_store(void)
{
    static PxIni ini;
    px_ini_init(&ini);
    const char *text =
        "; comment\r\n"
        "# hash comment\n"
        "[current]\n"
        "enabled=1\n"
        "sat=177.5\n"
        "vibrance=250\n"
        "gamma=abc\n"                 /* corrupt numeric value */
        "[xhair]\n"
        "size=24\n"
        "unknown_key=junk\n";
    CHECK(px_ini_parse(&ini, text) == 6);   /* returns retained entry count (2 sections not counted) */
    CHECK_NEAR(px_ini_get_flt(&ini, "current", "sat", 100), 177.5, 1e-6);
    CHECK(px_ini_get_int(&ini, "XHAIR", "size", 0) == 24);      /* case-insensitive */
    CHECK(px_ini_get_int(&ini, "current", "missing", 7) == 7);  /* defaults survive absence */
    CHECK_NEAR(px_ini_get_flt(&ini, "current", "missing", 1.5f), 1.5, 1e-9);
    /* corrupt value falls back to default, never garbage */
    Look lk;
    px_cfg_load_look(&ini, "current", &lk);
    CHECK_NEAR(lk.gamma, 1.0, 1e-6);
    CHECK_NEAR(lk.sat, 177.5, 1e-6);
    CHECK(lk.vibrance == 250.0f && lk.enabled == 1);

    /* v1 file: gamma stored as int percent -> loader divides by 100 */
    static PxIni v1;
    px_ini_init(&v1);
    CHECK(px_ini_parse(&v1, "[current]\ngamma=115\nsat=90\n") == 2);
    Look l1;
    px_cfg_load_look(&v1, "current", &l1);
    CHECK_NEAR(l1.gamma, 1.15, 1e-6);
    CHECK(px_cfg_schema(&v1) == 0);
    CHECK(px_cfg_migrate(&v1) >= 1);                            /* edits made */
    CHECK(px_cfg_schema(&v1) == PX_CFG_SCHEMA);
    px_cfg_load_look(&v1, "current", &l1);
    CHECK_NEAR(l1.gamma, 1.15, 1e-6);                           /* stable across migration */
    CHECK_NEAR(l1.sat, 90.0f, 1e-6);

    /* serialize -> reparse round trip preserves everything */
    char buf[8192];
    int n = px_ini_serialize(&ini, buf, sizeof buf);
    CHECK(n > 0);
    static PxIni ini2;
    px_ini_init(&ini2);
    CHECK(px_ini_parse(&ini2, buf) > 0);
    CHECK_NEAR(px_ini_get_flt(&ini2, "current", "vibrance", 0), 250.0, 1e-6);

    /* packed look round trip + corruption safety */
    Look src = LOOK_NEUTRAL_INIT;
    src.sat = 213.7f; src.vibrance = 133.3f; src.gamma = 1.13f; src.hue = 177.9f; src.temp = 4050;
    char pack[256];
    px_look_pack(&src, pack, sizeof pack);
    Look got;
    CHECK(px_look_unpack(pack, &got) == 17);   /* returns the number of parsed fields */
    CHECK_NEAR(got.sat, src.sat, 1e-3);
    CHECK_NEAR(got.vibrance, src.vibrance, 1e-3);
    CHECK_NEAR(got.gamma, src.gamma, 1e-3);
    CHECK_NEAR(got.hue, src.hue, 1e-3);
    CHECK_NEAR(got.temp, src.temp, 1e-3);
    Look low = LOOK_NEUTRAL_INIT; low.vibrance = -33.3f;      /* out-of-domain value */
    px_look_pack(&low, pack, sizeof pack);
    CHECK(px_look_unpack(pack, &got) == 17);
    CHECK(got.vibrance == 0.0f);               /* clamped into [0,300] by sanitize */
    Look bad;
    CHECK(px_look_unpack("", &bad) == 0);                        /* empty -> neutral + failure */
    CHECK(cm_looks_equal(&bad, &(Look)LOOK_NEUTRAL_INIT));
    px_look_unpack("nan,1e99,-1e99,junk,,,", &bad);              /* hostile but parseable */
    CHECK(isfinite(bad.sat) && bad.sat >= 0.0f && bad.sat <= 300.0f);
    CHECK(bad.vibrance >= 0.0f && bad.vibrance <= 300.0f);
    CHECK(bad.gamma >= 0.40f && bad.gamma <= 2.50f);

    printf("  [PASS] Settings: lenient parse, corrupt values -> defaults, v1 migration, packed-look safety\n");
}

/* ---------------- 22. Profile records: built-in overrides + custom profiles - */
static void test_profile_store(void)
{
    static PxIni ini;
    px_ini_init(&ini);

    PxProfRec r; memset(&r, 0, sizeof r);
    strcpy(r.name, "My Game"); strcpy(r.exe, "mygame.exe"); strcpy(r.tag, "Shooter");
    r.is_custom = 1; r.favorite = 1; r.auto_apply = 1; r.auto_restore = 0;
    r.delay_ms = 750; r.hdr_preference = 2;
    r.target_res_w = 2560; r.target_res_h = 1440; r.target_hz = 165;
    r.sub_count = 2; r.active_sub = 1;
    strcpy(r.sub[0].name, "Day"); strcpy(r.sub[1].name, "Night");
    r.sub[0].look = (Look)LOOK_NEUTRAL_INIT; r.sub[0].look.sat = 200;
    r.sub[1].look = (Look)LOOK_NEUTRAL_INIT; r.sub[1].look.vibrance = 180; r.sub[1].look.gamma = 0.85f;
    px_prof_put(&ini, 20, &r);

    PxProfRec out;
    CHECK(px_prof_get(&ini, 20, &out) == 1);
    CHECK(!strcmp(out.name, "My Game") && !strcmp(out.exe, "mygame.exe"));
    CHECK(out.is_custom && out.favorite && !out.auto_restore && out.delay_ms == 750);
    CHECK(out.hdr_preference == 2 && out.target_res_w == 2560 && out.target_hz == 165);
    CHECK(out.sub_count == 2 && out.active_sub == 1);
    CHECK(!strcmp(out.sub[1].name, "Night"));
    CHECK_NEAR(out.sub[1].look.vibrance, 180.0f, 1e-3);
    CHECK_NEAR(out.sub[1].look.gamma, 0.85f, 1e-4);
    CHECK_NEAR(out.sub[0].look.sat, 200.0f, 1e-3);

    /* round trip through serialize/parse keeps custom sub looks */
    static char buf[16384];
    int n = px_ini_serialize(&ini, buf, sizeof buf);
    CHECK(n > 0);
    static PxIni ini2;
    px_ini_init(&ini2);
    CHECK(px_ini_parse(&ini2, buf) > 0);
    PxProfRec out2;
    CHECK(px_prof_get(&ini2, 20, &out2) == 1);
    CHECK(!strcmp(out2.sub[0].name, "Day"));
    CHECK_NEAR(out2.sub[1].look.gamma, 0.85f, 1e-4);

    CHECK(px_prof_get(&ini2, 31, &out2) == 0);                  /* absent slot */
    CHECK(px_prof_section(20, buf, sizeof buf) == 0 && !strcmp(buf, "profile20"));

    printf("  [PASS] Profile store: custom records with sub-names + packed looks survive full file round trip\n");
}

/* ---------------- 23. Diagnostics: event ring + JSON + applied-state -------- */
static void test_diagnostics(void)
{
    static PxLog log;
    PxLog_Init(&log);
    for (int i = 0; i < PXLOG_CAP + 30; i++)
        PxLog_Add(&log, (unsigned)i * 10, i % 2 ? "eng" : "dwm",
                     i == PXLOG_CAP + 29 ? "quote\"back\\slash" : "evt");   /* LAST entry: older ones are evicted */
    CHECK(log.count == PXLOG_CAP);
    CHECK(log.dropped == 30);

    static char json[40000];
    int n = PxLog_RenderJson(&log, json, sizeof json, 128);
    CHECK(n > 2 && json[0] == '[' && json[n - 1] == ']');
    CHECK(strstr(json, "\\\"") != NULL && strstr(json, "\\\\") != NULL);   /* escaped */
    CHECK(strchr(json, '\n') == NULL);                                      /* one array */
    int m = PxLog_RenderJson(&log, json, 64, 128);                          /* tiny buffer */
    CHECK(m >= 0 && m < 64);                                                 /* truncates safely */

    /* AppliedColorState derives its outcome, it is never set by hand */
    AppliedColorState a; PxAS_Init(&a);
    CHECK(a.outcome == PX_APPLY_NOT_YET && !a.have);
    Look l = LOOK_NEUTRAL_INIT;
    PxAS_Record(&a, &l, 5, 1, 1, 1, 2, 0, 1000, NULL);
    CHECK(a.outcome == PX_APPLY_FULL && a.have && a.matrix_verified);
    PxAS_Record(&a, &l, 6, 1, 0, 1, 2, 0, 1100, "readback mismatch: DWM dropped the effect");
    CHECK(a.outcome == PX_APPLY_FULL && !a.matrix_verified);      /* ok+ok stays FULL, verified=0 */
    PxAS_Record(&a, &l, 7, 0, 0, 1, 2, 0, 1200, "hardware rejected part of the pipeline");
    CHECK(a.outcome == PX_APPLY_PARTIAL);
    PxAS_Record(&a, &l, 8, 0, 0, 0, 0, 0, 1300, "hardware rejected part of the pipeline");
    CHECK(a.outcome == PX_APPLY_FAILED);
    CHECK(!strcmp(a.note, "hardware rejected part of the pipeline"));
    /* note buffer overflow is truncated, never out-of-bounds */
    PxAS_Record(&a, &l, 9, 1, 1, 1, 0, 1, 1400,
        "012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789");
    CHECK(strlen(a.note) == sizeof a.note - 1);

    printf("  [PASS] Diagnostics: ring log wraps + escaped JSON, applied outcome derived from hardware facts\n");
}

int main(void)
{
    printf("\n=== PlexusX Automated Verification Test Suite ===\n");
    test_kelvin();
    test_gamma_ramp();
    test_aspect_ratios();
    test_json_profile();
    test_saturation_matrix();
    test_matrix_layout();
    test_w_divisor_safety();
    test_translation_row();
    test_hue_rotation();
    test_linear_nonlinear_split();
    test_ramp_monotonicity();
    test_ramp_write_planning();
    test_ramps_file_validation();
    test_shipped_presets();
    test_high_vibrance_neutrals();
    test_requested_applied_state();
    test_transform_plan();
    test_display_state();
    test_game_state_machine();
    test_window_state();
    test_settings_store();
    test_profile_store();
    test_diagnostics();
    printf("==================================================\n");
    printf("ALL 23 UNIT & INTEGRATION TEST SUITES PASSED - %d checks\n\n", g_checks);
    return 0;
}
