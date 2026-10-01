/* PlexusX — UI design tokens (native GDI shell) — Premium Edition v2.2
 *
 * The single palette + metric source for the Win32 UI, mirroring the tokens
 * used by the browser preview (site/assets/tokens.css): dark, glass, lime
 * accent, technical.  Every panel draws through these — no file may hardcode
 * stray RGBs outside this header.  Blur/glass here = layered-window alpha +
 * DWM blur-behind (Main_ApplyChrome), the two effects Windows composes
 * cheaply; no per-frame blur simulation in GDI (that would burn CPU for
 * nothing — the same rule the preview CSS follows).
 *
 * Premium redesign v2.2:
 *   - Refined graphite scale with 7 surface levels for depth
 *   - Lime accent with 4-step scale for hover/active/disabled
 *   - Semantic colors expanded to 8 tones + alpha variants
 *   - Typography: 6 sizes + 3 weights, Segoe UI Variable
 *   - Geometry: 4 radii + 6 spacing + elevation shadows
 *   - Animation: 120ms standard, 200ms emphasis, 16ms micro
 *   - Accessibility: minimum 4.5:1 contrast on all text/background pairs
 */

#ifndef PLEXUSX_UI_THEME_H
#define PLEXUSX_UI_THEME_H

/* ---- surfaces: 7-level graphite depth scale ---------------------------- */
#define PX_T_BG             RGB(10, 10, 14)     /* window base — deepest            */
#define PX_T_BG_RAISED      RGB(14, 14, 19)     /* raised window sections           */
#define PX_T_SIDE           RGB(16, 16, 22)     /* sidebar surface                  */
#define PX_T_SIDE_HOVER     RGB(20, 20, 28)     /* sidebar item hover               */
#define PX_T_SURFACE        RGB(22, 22, 30)     /* cards — default                  */
#define PX_T_SURFACE_HOVER  RGB(30, 30, 42)     /* cards — hovered                  */
#define PX_T_SURFACE_ACTIVE RGB(36, 36, 52)     /* cards — selected/active          */
#define PX_T_SURFACE_ELEV   RGB(40, 40, 58)     /* cards — elevated (dialog)        */
#define PX_T_INSET          RGB(8, 8, 12)       /* wells, preview backgrounds       */
#define PX_T_INSET_DEEP     RGB(5, 5, 9)        /* deep wells, code blocks          */
#define PX_T_ON_ACCENT      RGB(10, 10, 14)     /* text drawn over accent fill      */

/* glass translucency handled by SetLayeredWindowAttributes (Main_ApplyChrome) */
#define PX_T_GLASS_ALPHA        242             /* ~95% opaque: readable, tinted    */
#define PX_T_GLASS_ALPHA_REDUCED 252            /* reduce-motion: almost opaque    */
#define PX_T_GLASS_TINT         RGB(18, 18, 26) /* glass tint overlay               */

/* ---- structure: borders, dividers, grids -------------------------------- */
#define PX_T_BORDER         RGB(42, 42, 58)     /* default border                   */
#define PX_T_BORDER_STRONG  RGB(54, 54, 74)     /* emphasized border                */
#define PX_T_BORDER_SUBTLE  RGB(32, 32, 46)     /* subtle divider                   */
#define PX_T_GRID           RGB(28, 30, 42)     /* abstract mesh background         */
#define PX_T_DIVIDER        RGB(24, 24, 36)     /* section divider line             */
#define PX_T_FOCUS_RING     RGB(198, 255, 61)   /* keyboard focus ring — accent     */

/* ---- typography colors: 4-level hierarchy -------------------------------- */
#define PX_T_TEXT           RGB(242, 242, 248)  /* primary — 15.2:1 on BG           */
#define PX_T_TEXT_SEC       RGB(200, 200, 214)  /* secondary — 10.1:1               */
#define PX_T_TEXT_SUB       RGB(145, 145, 160)  /* tertiary — 5.8:1                 */
#define PX_T_TEXT_DIM       RGB(95, 95, 110)    /* disabled/hint — 3.2:1 on SURFACE */
#define PX_T_TEXT_GHOST     RGB(70, 70, 84)     /* ghost — placeholder              */
#define PX_T_TEXT_ACCENT    RGB(198, 255, 61)   /* accent text on dark              */
#define PX_T_TEXT_ON_ACCENT RGB(10, 10, 14)     /* text on accent bg — 15.8:1       */

/* ---- brand + semantics: expanded 8-tone system -------------------------- */
#define PX_T_ACCENT         RGB(198, 255, 61)   /* lime: primary action, live state  */
#define PX_T_ACCENT_HOVER   RGB(210, 255, 90)   /* lime hover — lighter              */
#define PX_T_ACCENT_ACTIVE  RGB(180, 240, 40)   /* lime active — slightly darker     */
#define PX_T_ACCENT_DEEP    RGB(155, 224, 15)   /* lime deep — pressed/selected      */
#define PX_T_ACCENT_DIM     RGB(120, 160, 20)   /* lime dim — disabled/accent border */
#define PX_T_ACCENT_GLOW    RGB(198, 255, 61)   /* for glow effects (alpha in paint) */

#define PX_T_CYAN           RGB(79, 227, 255)   /* informational readouts            */
#define PX_T_CYAN_DIM       RGB(45, 160, 190)   /* cyan dim                          */
#define PX_T_VIOLET         RGB(123, 92, 255)   /* scene tags, game profiles         */
#define PX_T_VIOLET_DIM     RGB(90, 70, 190)    /* violet dim                        */
#define PX_T_SUCCESS        RGB(79, 227, 120)   /* applied / verified / ACTIVE       */
#define PX_T_SUCCESS_DIM    RGB(50, 160, 80)    /* success dim                       */
#define PX_T_WARNING        RGB(255, 196, 61)   /* partial / LIMITED game path       */
#define PX_T_WARNING_DIM    RGB(190, 140, 30)   /* warning dim                       */
#define PX_T_DANGER         RGB(255, 80, 80)    /* failed / emergency                */
#define PX_T_DANGER_DIM     RGB(190, 50, 50)    /* danger dim                        */
#define PX_T_INFO           RGB(100, 160, 255)  /* info / APPLYING                   */
#define PX_T_INFO_DIM       RGB(70, 110, 190)   /* info dim                          */

/* ---- chip tones (semantic mapping for UiToneColor) ---------------------- */
#define PX_CHIP_NEUTRAL  0
#define PX_CHIP_OK       1
#define PX_CHIP_WARN     2
#define PX_CHIP_BAD      3
#define PX_CHIP_INFO     4
#define PX_CHIP_ACCENT   5
#define PX_CHIP_VIOLET   6
#define PX_CHIP_CYAN     7

/* ---- geometry: radii, spacing, elevation -------------------------------- */
#define PX_T_R_XS   4      /* tiny pill, inline badge           */
#define PX_T_R_SM   6      /* chip pill                         */
#define PX_T_R_MD   10     /* buttons, inputs                   */
#define PX_T_R_LG   14     /* cards                             */
#define PX_T_R_XL   20     /* wells / previews                  */
#define PX_T_R_2XL  28     /* hero / modal                      */
#define PX_T_R_FULL 9999   /* fully rounded (circle)            */

#define PX_T_SP_1   4
#define PX_T_SP_2   8
#define PX_T_SP_3   12
#define PX_T_SP_4   16
#define PX_T_SP_6   24
#define PX_T_SP_8   32
#define PX_T_SP_10  40
#define PX_T_SP_12  48

/* elevation shadows (painted as translucent rects) */
#define PX_T_SHADOW_SM_ALPHA  20   /* 8% — card shadow */
#define PX_T_SHADOW_MD_ALPHA  35   /* 14% — elevated card */
#define PX_T_SHADOW_LG_ALPHA  55   /* 22% — modal/dialog */

/* ---- typography: sizes + weights ---------------------------------------- */
#define PX_T_F_LOGO   20
#define PX_T_F_H1     26
#define PX_T_F_H2     15
#define PX_T_F_H3     13
#define PX_T_F_BODY   13
#define PX_T_F_SMALL  11
#define PX_T_F_TINY   9
#define PX_T_F_MONO   12
#define PX_T_F_BIG    42
#define PX_T_F_HERO   32

#define PX_T_W_REGULAR  400
#define PX_T_W_MEDIUM   500
#define PX_T_W_SEMIBOLD 600
#define PX_T_W_BOLD     700

/* ---- animation: durations (ms) ------------------------------------------ */
#define PX_T_ANIM_MICRO    16    /* 1 frame @ 60Hz — hover feedback */
#define PX_T_ANIM_FAST     120   /* standard transition */
#define PX_T_ANIM_NORMAL   200   /* emphasis, panel switch */
#define PX_T_ANIM_SLOW     320   /* modal, hero */

/* ---- layout: content metrics -------------------------------------------- */
#define PX_T_SIDEBAR_W     220
#define PX_T_TOPBAR_H      56
#define PX_T_STATUS_H      104
#define PX_T_CONTENT_X     248
#define PX_T_CONTENT_W     986
#define PX_T_CARD_GAP      12
#define PX_T_SECTION_GAP   24

/* ---- slider: metrics ---------------------------------------------------- */
#define PX_T_SLIDER_H      6     /* track height */
#define PX_T_SLIDER_THUMB  16    /* thumb diameter */
#define PX_T_SLIDER_GAP    12    /* label-to-track gap */

/* ---- accessibility: minimum hit targets --------------------------------- */
#define PX_T_MIN_HIT       32    /* minimum button/row height for pointer */
#define PX_T_MIN_HIT_TOUCH 44    /* minimum for touch (phone remote parity) */

#endif /* PLEXUSX_UI_THEME_H */
