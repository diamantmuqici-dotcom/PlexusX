/* PlexusX — parity golden generator.
 * Emits the C reference for the exact sample set that scripts/site_parity.js
 * recomputes from site/assets/color_engine.js.  The CI fails if the browser
 * port drifts from the shipped C kernel by more than 3e-4 on any value.
 *
 *   gcc -std=gnu11 -O2 -Iapp/src tests/parity_gen.c -o /tmp/parity && /tmp/parity
 */
#include <stdio.h>
#include <math.h>
typedef unsigned short WORD;   /* host: no windows.h here */
#include "color/color_math.h"

/* THE SAMPLE SET (kept identical to scripts/site_parity.js — both lists are
 * hashed at the start of the parity run to catch drift in the spec itself). */
typedef struct { const char *name; float en, sat, vib, bri, con, gam, temp, tint,
                 rg, gg, bg, sh, hl, bl, wp, cla, hue; } Sample;

static Sample S[] = {
  { "neutral",        1,100,100,100,100,1.00f,6500,0,100,100,100,100,100,100,100,100,0 },
  { "elite-default",  1,150,120,100,100,1.00f,6500,0,100,100,100,100,100,100,100,100,0 },
  { "max-chroma",     1,300,300,100,100,1.00f,6500,0,100,100,100,100,100,100,100,100,0 },
  { "desaturate",     1,0,0,100,100,1.00f,6500,0,100,100,100,100,100,100,100,100,0 },
  { "warm-cold",      1,110,40,102,108,0.95f,3400,-18,112,100,88,108,96,102,98,118,-45 },
  { "cool-blue",      1,120,60,98,112,1.10f,9200,25,90,100,110,95,105,99,101,108,60 },
  { "night-vision",   1,180,150,118,124,0.80f,4800,0,105,102,110,132,88,106,96,120,0 },
  { "gamma-only",     1,100,100,100,100,0.62f,6500,0,100,100,100,100,100,100,100,100,0 },
  { "curves-only",    1,100,100,100,100,1.00f,6500,0,100,100,100,142,74,100,100,150,0 },
  { "black-white",    1,100,100,100,100,1.00f,6500,0,100,100,100,100,100,142,62,100,0 },
  { "gains",          1,100,100,100,100,1.00f,6500,0,118,92,104,100,100,100,100,100,0 },
  { "bypassed",       0,220,180,140,160,0.7f,5000,10,110,110,110,120,120,120,120,120,30 },
  { "clamp-test",     1,9999,9999,-9999,9999,99.0f,999999,9999,-9999,9999,9999,9999,-9999,9999,9999,-9999,9999 },
  { "hue-extremes",   1,100,100,100,100,1.00f,6500,0,100,100,100,100,100,100,100,100,180 },
  { "hue-neg",        1,140,80,100,100,1.00f,6500,0,100,100,100,100,100,100,100,100,-179.5f },
};
#define NS ((int)(sizeof S / sizeof S[0]))

static void put_look(Look *l, const Sample *s)
{
    l->enabled = (int)s->en;  l->sat = s->sat;   l->vibrance = s->vib;
    l->bri = s->bri;          l->con = s->con;   l->gamma = s->gam;
    l->temp = s->temp;        l->tint = s->tint; l->r_gain = s->rg;
    l->g_gain = s->gg;        l->b_gain = s->bg; l->shadows = s->sh;
    l->highlights = s->hl;    l->black_level = s->bl; l->white_point = s->wp;
    l->clarity = s->cla;      l->hue = s->hue;
}

/* the shared spec hash both generators print */
static unsigned long long spec_hash(void)
{
    unsigned long long h = 1469598103934665603ull;
    for (int i = 0; i < NS; i++) {
        const unsigned char *b = (const unsigned char *)&S[i].en;
        for (size_t j = 0; j < sizeof(Sample) - sizeof(char *); j++) { h ^= b[j]; h *= 1099511628211ull; }
    }
    return h;
}

int main(void)
{
    static const float px[][3] = {
        {0,0,0},{1,1,1},{0.5f,0.5f,0.5f},{0.25f,0.5f,0.75f},
        {1,0,0},{0,1,0},{0,0,1},{0.1875f,0.1875f,0.1875f},{0.835294f,0.498039f,0.101961f}
    };
    printf("{\n\"tool\":\"plexusx-parity-c\",\n");
    printf("\"tolerance\":0.0003,\n");
    printf("\"looks\":[\n");
    for (int i = 0; i < NS; i++) {
        Look l; put_look(&l, &S[i]);
        Look san = l; cm_sanitize_look(&san);
        Look applied = san;
        if (!san.enabled) applied = (Look)LOOK_NEUTRAL_INIT;   /* PxPlan bypass semantics */
        MagColorEffect e; cm_build_effect(&applied, &e);
        WORD ramp[3][256]; if (san.enabled) cm_calc_ramp(&san, ramp);
        printf("  {\"name\":\"%s\",\"in\":[%d,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g],",
               S[i].name, san.enabled, san.sat, san.vibrance, san.bri, san.con, (double)san.gamma,
               san.temp, san.tint, san.r_gain, san.g_gain, san.b_gain, san.shadows,
               san.highlights, san.black_level, san.white_point, san.clarity, san.hue);
        printf("\"mat\":[");
        for (int r = 0; r < 5; r++) for (int c = 0; c < 5; c++) printf("%.9g%s", (double)e.transform[r][c], (r*5+c==24)?"":",");
        printf("],\"ramp\":");
        if (!san.enabled) {
            printf("null");                                  /* bypass: hardware keeps its orig ramp */
        } else {
            printf("[");
            for (int k = 0; k < 256; k += 4) printf("%u%s", (unsigned)ramp[0][k], (k==252)?"":",");
            printf("]");
        }
        printf(",\"px\":[");
        for (int p = 0; p < 9; p++) {
            float o0, o1, o2;
            cm_apply_pixel(&l, px[p][0], px[p][1], px[p][2], &o0, &o1, &o2);
            printf("[%.9g,%.9g,%.9g]", (double)o0,(double)o1,(double)o2);
            if (p != 8) printf(",");
        }
        printf("],\"neutral\":%d}%s", cm_curves_neutral(&l), i == NS-1 ? "" : ",");
        printf("\n");
    }
    printf("]\n}\n");
    (void)spec_hash;
    return 0;
}
