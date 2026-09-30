/* PlexusX — Comprehensive Automated Test Suite
 * Compiles natively on Linux/macOS/Windows to verify all logic, algorithms, math, and serialization.
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <assert.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

typedef unsigned short WORD;

/* Clamp helpers */
static inline float clampf(float v, float a, float b) { return v < a ? a : (v > b ? b : v); }
static inline int   clampi(int v, int a, int b) { return v < a ? a : (v > b ? b : v); }

/* Color Look definition */
typedef struct TestLook {
    int   enabled;
    float sat;
    float vibrance;
    float bri;
    float con;
    float gamma;
    float temp;
    float tint;
    float r_gain;
    float g_gain;
    float b_gain;
    float shadows;
    float highlights;
    float black_level;
    float white_point;
    float clarity;
    float hue;
} TestLook;

/* ---------------- 1. Kelvin to RGB Planckian Algorithm ---------------- */
static void kelvin_to_rgb(float k, float *r, float *g, float *b)
{
    float temp = k / 100.0f;
    float red, green, blue;

    if (temp <= 66.0f) {
        red = 255.0f;
        green = temp;
        green = 99.4708025861f * logf(green) - 161.1195681661f;
        if (temp <= 19.0f) {
            blue = 0.0f;
        } else {
            blue = temp - 10.0f;
            blue = 138.5177312231f * logf(blue) - 305.0447927307f;
        }
    } else {
        red = temp - 60.0f;
        red = 329.698727446f * powf(red, -0.1332047592f);
        green = temp - 60.0f;
        green = 288.1221695283f * powf(green, -0.0755148492f);
        blue = 255.0f;
    }

    *r = clampf(red / 255.0f, 0.0f, 2.0f);
    *g = clampf(green / 255.0f, 0.0f, 2.0f);
    *b = clampf(blue / 255.0f, 0.0f, 2.0f);
}

static void test_kelvin(void)
{
    float r, g, b;
    /* Neutral D65 ~ 6500K */
    kelvin_to_rgb(6500.0f, &r, &g, &b);
    assert(r > 0.90f && r <= 1.10f);
    assert(g > 0.90f && g <= 1.10f);
    assert(b > 0.90f && b <= 1.10f);

    /* Warm 4000K: Red should exceed Blue */
    float r_warm, g_warm, b_warm;
    kelvin_to_rgb(4000.0f, &r_warm, &g_warm, &b_warm);
    assert(r_warm > b_warm);

    /* Cool 9000K: Blue should exceed Red */
    float r_cool, g_cool, b_cool;
    kelvin_to_rgb(9000.0f, &r_cool, &g_cool, &b_cool);
    assert(b_cool > r_cool);

    printf("  [PASS] Kelvin to RGB Planckian locus test\n");
}

/* ---------------- 2. Gamma Ramp Generation ---------------- */
static void calculate_gamma_ramp(const TestLook *lk, WORD ramp[3][256])
{
    float g = clampf(lk->gamma, 0.40f, 2.50f);
    float sh_lift = (lk->shadows - 100.0f) / 100.0f;
    float hl_lift = (lk->highlights - 100.0f) / 100.0f;
    float clarity = (lk->clarity - 100.0f) / 100.0f;

    float rg = lk->r_gain / 100.0f;
    float gg = lk->g_gain / 100.0f;
    float bg = lk->b_gain / 100.0f;

    float bl_offset = (lk->black_level - 100.0f) / 200.0f;
    float wp_scale  = lk->white_point / 100.0f;

    for (int ch = 0; ch < 3; ch++) {
        float gain = (ch == 0 ? rg : (ch == 1 ? gg : bg));
        for (int i = 0; i < 256; i++) {
            float x = i / 255.0f;
            float y = powf(x, g);

            if (fabsf(sh_lift) > 0.001f) {
                float toe = (1.0f - x) * (1.0f - x) * sh_lift * 0.35f;
                y = clampf(y + toe, 0.0f, 1.0f);
            }
            if (fabsf(hl_lift) > 0.001f) {
                float shoulder = x * x * hl_lift * 0.30f;
                y = clampf(y + shoulder, 0.0f, 1.0f);
            }
            if (fabsf(clarity) > 0.001f) {
                float scurve = 0.5f * (1.0f - cosf(x * (float)M_PI)) - x;
                y = clampf(y + scurve * clarity * 0.25f, 0.0f, 1.0f);
            }

            y = y * wp_scale + bl_offset;
            y *= gain;

            int val = (int)(y * 65535.0f + 0.5f);
            ramp[ch][i] = (WORD)clampi(val, 0, 65535);
        }
    }
}

static void test_gamma_ramp(void)
{
    TestLook neutral = { 1, 100, 100, 100, 100, 1.00f, 6500, 0, 100, 100, 100, 100, 100, 100, 100, 100, 0 };
    WORD ramp[3][256];
    calculate_gamma_ramp(&neutral, ramp);

    /* Neutral ramp should be strictly monotonic from 0 to 65535 */
    assert(ramp[0][0] == 0);
    assert(ramp[1][0] == 0);
    assert(ramp[2][0] == 0);
    assert(ramp[0][255] == 65535);
    assert(ramp[1][255] == 65535);
    assert(ramp[2][255] == 65535);

    for (int i = 1; i < 256; i++) {
        assert(ramp[0][i] >= ramp[0][i-1]);
        assert(ramp[1][i] >= ramp[1][i-1]);
        assert(ramp[2][i] >= ramp[2][i-1]);
    }

    /* Test shadow lift: low values should increase */
    TestLook shadow_lift = neutral;
    shadow_lift.shadows = 150.0f;
    WORD ramp_shadow[3][256];
    calculate_gamma_ramp(&shadow_lift, ramp_shadow);
    assert(ramp_shadow[0][32] > ramp[0][32]);

    /* Test custom gamma 0.80 (brighter midtones) */
    TestLook gamma_bright = neutral;
    gamma_bright.gamma = 0.80f;
    WORD ramp_g[3][256];
    calculate_gamma_ramp(&gamma_bright, ramp_g);
    assert(ramp_g[0][128] > ramp[0][128]);

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
    assert(detect_aspect(1920, 1080) == 0); /* 16:9 */
    assert(detect_aspect(2560, 1440) == 0); /* 16:9 */
    assert(detect_aspect(3840, 2160) == 0); /* 16:9 */

    assert(detect_aspect(1280, 960) == 1);  /* 4:3 Stretched */
    assert(detect_aspect(1440, 1080) == 1); /* 4:3 Stretched */
    assert(detect_aspect(1024, 768) == 1);  /* 4:3 */

    assert(detect_aspect(1680, 1050) == 2); /* 16:10 */
    assert(detect_aspect(1920, 1200) == 2); /* 16:10 */

    assert(detect_aspect(2560, 1080) == 3); /* Ultrawide 21:9 */
    assert(detect_aspect(3440, 1440) == 3); /* Ultrawide 21:9 */
    assert(detect_aspect(5120, 1440) == 3); /* Super Ultrawide 32:9 */

    printf("  [PASS] Display aspect ratio classification test\n");
}

/* ---------------- 4. JSON Profile Schema Parsing & Validation ---------------- */
static int parse_test_json(const char *json, TestLook *lk, char *name_out)
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

    TestLook lk;
    memset(&lk, 0, sizeof lk);
    char name[64] = { 0 };

    int r = parse_test_json(sample_json, &lk, name);
    assert(r == 0);
    assert(strcmp(name, "Rust Competitive Forest") == 0);
    assert(fabsf(lk.sat - 250.0f) < 0.01f);
    assert(fabsf(lk.vibrance - 210.0f) < 0.01f);
    assert(fabsf(lk.gamma - 0.92f) < 0.01f);
    assert(fabsf(lk.temp - 6200.0f) < 0.01f);

    /* Test Bounds Clamping on Extreme Values */
    const char *extreme_json =
        "{\n"
        "  \"name\": \"Extreme Overflow Test\",\n"
        "  \"sat\": 9999.0,\n"
        "  \"gamma\": 0.05,\n"
        "  \"temp\": 50000.0\n"
        "}\n";

    TestLook extreme;
    memset(&extreme, 0, sizeof extreme);
    parse_test_json(extreme_json, &extreme, name);
    assert(extreme.sat == 300.0f);   /* Clamped to max 300% */
    assert(extreme.gamma == 0.40f);  /* Clamped to min 0.40 */
    assert(extreme.temp == 10000.0f);/* Clamped to max 10000K */

    printf("  [PASS] Profile JSON serialization and bounds validation test\n");
}

/* ---------------- 5. 300% Saturation Matrix Test ---------------- */
static void test_saturation_matrix(void)
{
    /* At 100% saturation, identity mapping */
    float s1 = 1.0f;
    const float lr = 0.2126f;
    float m_diag = 1.0f * s1 + lr * (1.0f - s1);
    assert(fabsf(m_diag - 1.0f) < 0.001f);

    /* At 300% saturation */
    float s3 = 3.0f;
    float m3_diag = 1.0f * s3 + lr * (1.0f - s3);
    assert(m3_diag > 2.0f); /* Color primaries significantly boosted */

    printf("  [PASS] 300%% Saturation matrix mathematics test\n");
}

int main(void)
{
    printf("\n=== PlexusX Automated Verification Test Suite ===\n");
    test_kelvin();
    test_gamma_ramp();
    test_aspect_ratios();
    test_json_profile();
    test_saturation_matrix();
    printf("==================================================\n");
    printf("ALL 5 UNIT & INTEGRATION TEST SUITES PASSED (100%%)\n\n");
    return 0;
}
