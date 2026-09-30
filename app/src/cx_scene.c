/* cx_scene.c \u2014 original procedural preview scenes (pure C, no assets) */
#include "cx_scene.h"
#include <math.h>
#include <string.h>
#include <stdio.h>

typedef struct { uint8_t r, g, b; } RGB;

static RGB blend(RGB a, RGB b, float t)
{
    RGB o;
    o.r = (uint8_t)(a.r + (b.r - a.r) * t + 0.5f);
    o.g = (uint8_t)(a.g + (b.g - a.g) * t + 0.5f);
    o.b = (uint8_t)(a.b + (b.b - a.b) * t + 0.5f);
    return o;
}
static float fclamp(float v, float a, float b) { return v < a ? a : (v > b ? b : v); }

/* deterministic hash -> 0..1 (unsigned math: well-defined wraparound) */
static float hash2(int x, int y, int seed)
{
    unsigned h = (unsigned)x * 374761393u
               + (unsigned)y * 668265263u
               + (unsigned)seed * 1274126177u;
    h = (h ^ (h >> 13)) * 1274126177u;
    h ^= h >> 16;
    return (float)(h & 0xFFFFFFu) / 16777216.f;
}
static float vnoise(int x, int y, int seed)
{
    int xi = x >> 3, yi = y >> 3;
    float fx = (x & 7) / 7.f, fy = (y & 7) / 7.f;
    fx = fx * fx * (3 - 2 * fx); fy = fy * fy * (3 - 2 * fy);
    float a = hash2(xi, yi, seed), b = hash2(xi + 1, yi, seed);
    float c = hash2(xi, yi + 1, seed), d = hash2(xi + 1, yi + 1, seed);
    return a + (b - a) * fx + (c - a) * fy + (a - b - c + d) * fx * fy;
}

typedef struct { uint8_t *px; int w, h; } Img;
static void put(Img im, int x, int y, RGB c)
{
    if (x < 0 || y < 0 || x >= im.w || y >= im.h) return;
    size_t i = ((size_t)y * im.w + x) * 4;
    im.px[i] = c.r; im.px[i + 1] = c.g; im.px[i + 2] = c.b; im.px[i + 3] = 255;
}
static void vgrad(Img im, RGB top, RGB bot)
{
    for (int y = 0; y < im.h; y++) {
        RGB c = blend(top, bot, (float)y / (im.h - 1));
        for (int x = 0; x < im.w; x++) put(im, x, y, c);
    }
}
static void disc(Img im, float cx, float cy, float r, RGB c, float glow)
{
    int x0 = (int)(cx - r * 3), x1 = (int)(cx + r * 3);
    int y0 = (int)(cy - r * 3), y1 = (int)(cy + r * 3);
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 >= im.w) x1 = im.w - 1;
    if (y1 >= im.h) y1 = im.h - 1;
    for (int y = y0; y <= y1; y++)
        for (int x = x0; x <= x1; x++) {
            float dx = x - cx, dy = y - cy;
            float d = sqrtf(dx * dx + dy * dy) / r;
            if (d < 1.f) put(im, x, y, c);
            else if (glow > 0 && d < 3.f) {
                size_t i = ((size_t)y * im.w + x) * 4;
                RGB p = { im.px[i], im.px[i + 1], im.px[i + 2] };
                float t = (1.f - (d - 1.f) / 2.f) * glow;
                put(im, x, y, blend(p, c, t * 0.5f));
            }
        }
}
static void tree_line(Img im, float base, float amp, RGB c, int seed, float jag)
{
    for (int x = 0; x < im.w; x++) {
        float n = vnoise(x, 0, seed) * jag + vnoise(x * 3, 1, seed + 7) * jag * 0.4f;
        int top = (int)(base - amp * (0.4f + 0.6f * n));
        for (int y = top; y < im.h; y++)
            put(im, x, y, blend(c, c, 1.f));
    }
}
static void dunes(Img im, float base, float amp, float freq, RGB c, float shade)
{
    for (int x = 0; x < im.w; x++) {
        float t = (float)x / im.w;
        int top = (int)(base + amp * (sinf(t * freq * 6.2831f) * 0.6f +
                                       sinf(t * freq * 12.566f + 1.7f) * 0.4f));
        for (int y = top; y < im.h; y++) {
            float fall = (y - top) / (im.h - top + 0.001f);
            put(im, x, y, blend(c, c, 1.f - shade * fclamp(fall * 2.f, 0.f, 1.f)));
        }
    }
}
static void clouds(Img im, int y0, int y1, RGB c, float thr, int seed)
{
    for (int y = y0; y < y1; y++)
        for (int x = 0; x < im.w; x++) {
            float n = vnoise(x / 2, y / 2, seed);
            n += 0.5f * vnoise(x / 5, y / 5, seed + 3);
            n *= 0.67f;
            if (n > thr) {
                size_t i = ((size_t)y * im.w + x) * 4;
                RGB p = { im.px[i], im.px[i + 1], im.px[i + 2] };
                float a = fclamp((n - thr) / (1.f - thr), 0.f, 1.f) * 0.85f;
                put(im, x, y, blend(p, c, a));
            }
        }
}
static void buildings(Img im, float base, RGB wall, RGB win, int seed, float lit)
{
    int x = 0;
    while (x < im.w) {
        int bw = 8 + (int)(hash2(x, 1, seed) * 26);
        int bh = 12 + (int)(hash2(x, 2, seed) * (base * 0.75f));
        RGB wc = blend(wall, wall, 1.f);
        for (int yy = (int)base - bh; yy < (int)base; yy++)
            for (int xx = x; xx < x + bw && xx < im.w; xx++)
                put(im, xx, yy, wc);
        /* windows */
        for (int wy = (int)base - bh + 3; wy < (int)base - 3; wy += 5)
            for (int wx = x + 2; wx < x + bw - 1; wx += 4)
                if (hash2(wx, wy, seed + 11) < lit)
                    for (int dy = 0; dy < 3; dy++)
                        for (int dx = 0; dx < 2; dx++)
                            put(im, wx + dx, wy + dy, win);
        x += bw + 2;
    }
}

void CxScene_Name(int i, char *out, int sz)
{
    static const char *n[] = { "Forest", "Night", "Snow", "Desert", "City",
                               "Sky", "Smoke", "Indoor", "Outdoor", "Dark Room" };
    if (sz <= 0) return;
    if (i < 0 || i >= CX_N_SCENE) { out[0] = 0; return; }
    snprintf(out, (size_t)sz, "%s", n[i]);
}

void CxScene_Generate(int which, uint8_t *rgba, int w, int h)
{
    if (!rgba || w <= 0 || h <= 0) return;
    Img im = { rgba, w, h };
    float H = h / 100.f, W = w / 100.f;

    switch (which) {
    case CX_SCENE_FOREST: {
        vgrad(im, (RGB){ 96, 140, 190 }, (RGB){ 205, 220, 205 });
        disc(im, 72 * W, 24 * H, 9 * H, (RGB){ 250, 244, 210 }, 0.55f);
        clouds(im, 4 * h / 10, 30 * h / 10, (RGB){ 240, 245, 240 }, 0.62f, 41);
        tree_line(im, 62 * H, 26 * H, (RGB){ 62, 96, 74 }, 7, 1.0f);
        tree_line(im, 74 * H, 34 * H, (RGB){ 40, 70, 52 }, 13, 1.0f);
        vgrad(im, (RGB){ 34, 60, 42 }, (RGB){ 24, 44, 32 });
        /* repaint ground only below 78% */
        for (int y = (int)(78 * H); y < h; y++)
            for (int x = 0; x < w; x++)
                put(im, x, y, blend((RGB){ 34, 60, 42 }, (RGB){ 20, 38, 28 }, (float)(y - 78 * H) / (h - 78 * H)));
        break; }
    case CX_SCENE_NIGHT: {
        vgrad(im, (RGB){ 8, 12, 30 }, (RGB){ 30, 40, 70 });
        for (int i = 0; i < 140; i++) {
            int x = (int)(hash2(i, 3, 99) * w), y = (int)(hash2(i, 7, 99) * h * 0.55f);
            int b = (int)(120 + hash2(i, 5, 99) * 135);
            put(im, x, y, (RGB){ (uint8_t)b, (uint8_t)b, (uint8_t)(b + 10) });
        }
        disc(im, 76 * W, 18 * H, 7 * H, (RGB){ 235, 238, 220 }, 0.8f);
        buildings(im, 78 * H, (RGB){ 12, 16, 28 }, (RGB){ 255, 214, 120 }, 5, 0.28f);
        tree_line(im, 92 * H, 22 * H, (RGB){ 6, 10, 14 }, 21, 0.9f);
        break; }
    case CX_SCENE_SNOW: {
        vgrad(im, (RGB){ 150, 175, 205 }, (RGB){ 225, 235, 245 });
        disc(im, 24 * W, 30 * H, 10 * H, (RGB){ 255, 250, 235 }, 0.7f);
        dunes(im, 58 * H, 14 * H, 1.3f, (RGB){ 228, 236, 244 }, 0.25f);
        dunes(im, 72 * H, 16 * H, 1.9f, (RGB){ 210, 222, 238 }, 0.35f);
        for (int i = 0; i < 260; i++) {
            int x = (int)(hash2(i, 1, 5) * w), y = (int)(hash2(i, 2, 5) * h);
            put(im, x, y, (RGB){ 255, 255, 255 });
        }
        break; }
    case CX_SCENE_DESERT: {
        vgrad(im, (RGB){ 214, 148, 88 }, (RGB){ 240, 205, 150 });
        disc(im, 62 * W, 30 * H, 12 * H, (RGB){ 255, 236, 190 }, 0.85f);
        dunes(im, 52 * H, 12 * H, 1.1f, (RGB){ 196, 138, 84 }, 0.30f);
        dunes(im, 68 * H, 14 * H, 1.7f, (RGB){ 168, 110, 66 }, 0.40f);
        break; }
    case CX_SCENE_CITY: {
        vgrad(im, (RGB){ 46, 38, 84 }, (RGB){ 216, 120, 84 });
        disc(im, 50 * W, 46 * H, 11 * H, (RGB){ 255, 170, 90 }, 0.9f);
        buildings(im, 82 * H, (RGB){ 24, 20, 44 }, (RGB){ 255, 214, 120 }, 17, 0.42f);
        for (int y = (int)(82 * H); y < h; y++)
            for (int x = 0; x < w; x++)
                put(im, x, y, (RGB){ 16, 14, 30 });
        break; }
    case CX_SCENE_SKY: {
        vgrad(im, (RGB){ 60, 130, 210 }, (RGB){ 170, 215, 245 });
        clouds(im, 12 * h / 100, 70 * h / 100, (RGB){ 250, 252, 255 }, 0.58f, 77);
        disc(im, 80 * W, 16 * H, 8 * H, (RGB){ 255, 250, 220 }, 0.9f);
        break; }
    case CX_SCENE_SMOKE: {
        vgrad(im, (RGB){ 120, 108, 96 }, (RGB){ 168, 150, 128 });
        for (int y = 0; y < h; y++)
            for (int x = 0; x < w; x++) {
                float n = vnoise(x / 3, y / 3, 31) * 0.7f + vnoise(x / 8, y / 8, 31) * 0.3f;
                if (n > 0.45f) {
                    size_t i = ((size_t)y * im.w + x) * 4;
                    RGB p = { im.px[i], im.px[i + 1], im.px[i + 2] };
                    put(im, x, y, blend(p, (RGB){ 200, 186, 160 }, fclamp((n - 0.45f) * 1.6f, 0.f, 0.8f)));
                }
            }
        disc(im, 50 * W, 34 * H, 16 * H, (RGB){ 255, 240, 210 }, 0.5f);
        for (int x = 0; x < w; x++) {
            float t = (float)x / w;
            int top = (int)(76 * H + 6 * H * sinf(t * 20.f));
            for (int y = top; y < h; y++)
                put(im, x, y, (RGB){ 52, 48, 44 });
        }
        break; }
    case CX_SCENE_INDOOR: {
        vgrad(im, (RGB){ 96, 88, 82 }, (RGB){ 70, 64, 60 });
        /* window */
        for (int y = (int)(18 * H); y < (int)(52 * H); y++)
            for (int x = (int)(58 * W); x < (int)(88 * W); x++)
                put(im, x, y, (RGB){ 200, 214, 226 });
        /* window light spill */
        for (int y = (int)(18 * H); y < (int)(80 * H); y++)
            for (int x = (int)(30 * W); x < (int)(88 * W); x++) {
                float dx = (x - 73 * W) / (58 * W), dy = (y - 35 * H) / (62 * H);
                float d = sqrtf(dx * dx + dy * dy);
                if (d < 1.6f) {
                    size_t i = ((size_t)y * im.w + x) * 4;
                    RGB p = { im.px[i], im.px[i + 1], im.px[i + 2] };
                    put(im, x, y, blend(p, (RGB){ 230, 232, 220 }, (1.f - fclamp(d, 0.f, 1.f)) * 0.25f));
                }
            }
        /* desk silhouette */
        for (int y = (int)(66 * H); y < h; y++)
            for (int x = (int)(6 * W); x < (int)(62 * W); x++)
                put(im, x, y, (RGB){ 40, 34, 30 });
        disc(im, 30 * W, 62 * H, 3 * H, (RGB){ 250, 220, 150 }, 0.7f);
        break; }
    case CX_SCENE_OUTDOOR: {
        vgrad(im, (RGB){ 80, 150, 215 }, (RGB){ 190, 225, 240 });
        clouds(im, 5 * h / 100, 40 * h / 100, (RGB){ 252, 252, 250 }, 0.6f, 55);
        for (int y = (int)(58 * H); y < h; y++)
            for (int x = 0; x < w; x++)
                put(im, x, y, blend((RGB){ 92, 150, 76 }, (RGB){ 60, 110, 56 }, (float)(y - 58 * H) / (h - 58 * H)));
        /* trees */
        for (int i = 0; i < 6; i++) {
            float tx = (0.12f + 0.16f * i + hash2(i, 9, 3) * 0.05f) * w;
            disc(im, tx, 54 * H, (6 + hash2(i, 4, 3) * 5) * H, (RGB){ 44, 92, 52 }, 0.f);
        }
        /* path */
        for (int y = (int)(70 * H); y < h; y++)
            for (int x = (int)(40 * W); x < (int)(58 * W); x++)
                if (hash2(x, y, 2) < 0.9f)
                    put(im, x, y, (RGB){ 178, 156, 120 });
        break; }
    case CX_SCENE_DARKROOM: {
        vgrad(im, (RGB){ 12, 11, 14 }, (RGB){ 22, 20, 24 });
        disc(im, 70 * W, 30 * H, 26 * H, (RGB){ 92, 76, 50 }, 0.55f);
        for (int y = (int)(70 * H); y < h; y++)
            for (int x = 0; x < w; x++)
                put(im, x, y, (RGB){ 8, 7, 9 });
        for (int y = (int)(62 * H); y < (int)(70 * H); y++)
            for (int x = (int)(50 * W); x < (int)(92 * W); x++)
                put(im, x, y, (RGB){ 14, 12, 15 });
        disc(im, 70 * W, 58 * H, 2.2 * H, (RGB){ 255, 226, 168 }, 0.9f);
        break; }
    default:
        vgrad(im, (RGB){ 40, 40, 46 }, (RGB){ 70, 70, 80 });
        break;
    }
}
