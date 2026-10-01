/* PlexusX — UI design tokens (native GDI shell).
 *
 * The single palette + metric source for the Win32 UI, mirroring the tokens
 * used by the browser preview (site/assets/tokens.css): dark, glass, lime
 * accent, technical.  Every panel draws through these — no file may hardcode
 * stray RGBs outside this header.  Blur/glass here = layered-window alpha +
 * DWM blur-behind (Main_ApplyChrome), the two effects Windows composes
 * cheaply; no per-frame blur simulation in GDI (that would burn CPU for
 * nothing — the same rule the preview CSS follows).
 */
#ifndef PLEXUSX_UI_THEME_H
#define PLEXUSX_UI_THEME_H

/* ---- surfaces ---------------------------------------------------------- */
#define PX_T_BG             RGB(12, 12, 16)     /* window base                     */
#define PX_T_SIDE           RGB(16, 16, 22)     /* sidebar surface                 */
#define PX_T_SURFACE        RGB(22, 22, 30)     /* cards                           */
#define PX_T_SURFACE_HOVER  RGB(30, 30, 42)     /* raised / hovered card           */
#define PX_T_SURFACE_ACTIVE RGB(36, 36, 52)     /* selected card                   */
#define PX_T_INSET          RGB(10, 10, 14)     /* wells, preview backgrounds      */
#define PX_T_ON_ACCENT      RGB(10, 10, 14)     /* text drawn over accent fill     */

/* glass translucency handled by SetLayeredWindowAttributes (Main_ApplyChrome) */
#define PX_T_GLASS_ALPHA        242             /* ~95% opaque: readable, tinted    */
#define PX_T_GLASS_ALPHA_REDUCED 252            /* reduce-motion: almost opaque    */

/* ---- structure ---------------------------------------------------------- */
#define PX_T_BORDER         RGB(42, 42, 58)
#define PX_T_GRID           RGB(28, 30, 42)     /* abstract mesh background          */

/* ---- typography colors --------------------------------------------------- */
#define PX_T_TEXT           RGB(242, 242, 248)
#define PX_T_TEXT_SUB       RGB(145, 145, 160)
#define PX_T_TEXT_DIM       RGB(95, 95, 110)

/* ---- brand + semantics --------------------------------------------------- */
#define PX_T_ACCENT         RGB(198, 255, 61)   /* lime: primary action, live state  */
#define PX_T_ACCENT_DEEP    RGB(155, 224, 15)
#define PX_T_CYAN           RGB(79, 227, 255)   /* informational readouts            */
#define PX_T_VIOLET         RGB(123, 92, 255)   /* scene tags                        */
#define PX_T_SUCCESS        RGB(79, 227, 120)   /* applied / verified / ACTIVE       */
#define PX_T_WARNING        RGB(255, 196, 61)   /* partial / LIMITED game path       */
#define PX_T_DANGER         RGB(255, 80, 80)    /* failed / emergency                */

/* ---- geometry -------------------------------------------------------------- */
#define PX_T_R_SM   6      /* chip pill            */
#define PX_T_R_MD   10     /* buttons, inputs      */
#define PX_T_R_LG   14     /* cards                */
#define PX_T_R_XL   20     /* wells / previews     */

#define PX_T_SP_1   4
#define PX_T_SP_2   8
#define PX_T_SP_3   12
#define PX_T_SP_4   16
#define PX_T_SP_6   24
#define PX_T_SP_8   32

/* font point sizes (ui.c scales by DPI) */
#define PX_T_F_LOGO   20
#define PX_T_F_H1     26
#define PX_T_F_H2     15
#define PX_T_F_BODY   13
#define PX_T_F_SMALL  11
#define PX_T_F_MONO   13
#define PX_T_F_BIG    42

#endif /* PLEXUSX_UI_THEME_H */
