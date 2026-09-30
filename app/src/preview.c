/* preview.c \u2014 generated preview scenes + colour before/after renderer
 *
 * Scenes are procedurally generated (cx_scene) \u2014 original pixel art, no
 * copyrighted assets.  The "after" image runs the exact same colour maths
 * the engine applies (CxColor_ApplyBuffer), so the preview is a true 1:1 of
 * the pipeline (matrix stage + curve stage; spatial sharpness and per-pixel
 * vibrance are also visible here).
 *
 * The returned HBITMAP is an internal persistent DIB \u2014 callers must NOT
 * DeleteObject it; it is owned until the next render / Prev_Shutdown.
 */
#include "common.h"

static uint8_t *g_base;     /* scene, untouched */
static uint8_t *g_tmp;      /* scratch */
static uint8_t *g_bits;     /* persistent DIB pixels */
static HBITMAP  g_dib;
static int g_w, g_h;
static int g_scene = CX_SCENE_FOREST;

int Prev_Init(int w, int h)
{
    Prev_Shutdown();
    g_w = w; g_h = h;
    g_base = (uint8_t *)malloc((size_t)w * h * 4);
    g_tmp  = (uint8_t *)malloc((size_t)w * h * 4);
    if (!g_base || !g_tmp) { Prev_Shutdown(); return 0; }

    HDC screen = GetDC(NULL);
    BITMAPINFO bi;
    memset(&bi, 0, sizeof bi);
    bi.bmiHeader.biSize = sizeof(bi.bmiHeader);
    bi.bmiHeader.biWidth = w;
    bi.bmiHeader.biHeight = -h;        /* top-down */
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    g_dib = CreateDIBSection(screen, &bi, DIB_RGB_COLORS, (void **)&g_bits, NULL, 0);
    ReleaseDC(NULL, screen);
    if (!g_dib) { Prev_Shutdown(); return 0; }

    Prev_Scene(g_scene);
    return 1;
}

void Prev_Shutdown(void)
{
    free(g_base); free(g_tmp); free(g_bits);
    g_base = g_tmp = g_bits = NULL;
    if (g_dib) { DeleteObject(g_dib); g_dib = NULL; }
    g_w = g_h = 0;
}

void Prev_Scene(int scene)
{
    if (!g_base) return;
    if (scene < 0 || scene >= CX_N_SCENE) scene = 0;
    g_scene = scene;
    CxScene_Generate(g_scene, g_base, g_w, g_h);
}

int Prev_W(void) { return g_w; }
int Prev_H(void) { return g_h; }

int Prev_RenderHBITMAP(int mode, float splitPos, HBITMAP *out)
{
    if (!g_base || !out) return 0;

    if (mode == 0) {
        memcpy(g_tmp, g_base, (size_t)g_w * g_h * 4);
    } else if (mode == 1) {
        memcpy(g_tmp, g_base, (size_t)g_w * g_h * 4);
        CxColor_ApplyBuffer(&g_look, g_tmp, g_w, g_h);
    } else { /* split: left = after, right = before, divider */
        splitPos = clampf(splitPos, 0.15f, 0.85f);
        int cut = (int)(g_w * splitPos);
        memcpy(g_tmp, g_base, (size_t)g_w * g_h * 4);
        CxColor_ApplyBuffer(&g_look, g_tmp, g_w, g_h);
        for (int y = 0; y < g_h; y++)
            for (int x = cut; x < g_w; x++) {
                size_t i = ((size_t)y * g_w + x) * 4;
                const uint8_t *s = g_base + i;
                g_tmp[i] = s[0]; g_tmp[i + 1] = s[1];
                g_tmp[i + 2] = s[2]; g_tmp[i + 3] = s[3];
            }
        for (int y = 0; y < g_h; y++)
            for (int x = cut - 1; x <= cut && x < g_w; x++) {
                size_t i = ((size_t)y * g_w + x) * 4;
                g_tmp[i] = g_tmp[i + 1] = g_tmp[i + 2] = 0;
                g_tmp[i + 3] = 255;
            }
    }

    if (g_bits) memcpy(g_bits, g_tmp, (size_t)g_w * g_h * 4);
    *out = g_dib;
    return g_dib ? 1 : 0;
}
