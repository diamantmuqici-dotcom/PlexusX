/* PlexusX — Centralized Versioning & Build Metadata
 *
 * Single source of truth for every version string in the product:
 *   - Native app (common.h, main.c, diagnostics, UI)
 *   - Website (site/index.html is generated from here by scripts/bump_version.py)
 *   - Installers and manifests
 *   - Logging and crash reports
 *
 * Bump ONLY this file when releasing.  Scripts sync the rest.
 *
 * Format: MAJOR.MINOR.PATCH (semver).  Build date is injected by the Makefile
 * via -DPX_BUILD_DATE_OVERRIDE or defaults to __DATE__ at compile time.
 */

#ifndef PLEXUSX_VERSION_H
#define PLEXUSX_VERSION_H

/* ---- Semantic version ---- */
#define PX_VERSION_MAJOR  2
#define PX_VERSION_MINOR  2
#define PX_VERSION_PATCH  0

/* Stringified version for C string literal concatenation */
#define PX_VER_STR2(x) #x
#define PX_VER_STR(x) PX_VER_STR2(x)
#define PX_VERSION_STRING \
    PX_VER_STR(PX_VERSION_MAJOR) "." \
    PX_VER_STR(PX_VERSION_MINOR) "." \
    PX_VER_STR(PX_VERSION_PATCH)

#define PX_VERSION_WSTRING L"2.2.0"

/* Wide version for Win32 resources — keep in sync with PX_VERSION_STRING */
#define PX_VERSION_W PX_VERSION_WSTRING

/* Numeric version for VERSIONINFO resource (e.g. 2,2,0,0) */
#define PX_VERSION_NUM  PX_VERSION_MAJOR,PX_VERSION_MINOR,PX_VERSION_PATCH,0

/* ---- Build metadata ---- */
#ifndef PX_BUILD_DATE_OVERRIDE
#   define PX_BUILD_DATE_STR __DATE__
#else
#   define PX_BUILD_DATE_STR PX_BUILD_DATE_OVERRIDE
#endif

#define PX_BUILD_DATE_WSTRING L"" PX_BUILD_DATE_STR

/* ---- Product identity ---- */
#define PX_PRODUCT_NAME         "PlexusX"
#define PX_PRODUCT_NAME_W       L"PlexusX"
#define PX_PRODUCT_TITLE        "PlexusX — Gaming Display Optimizer"
#define PX_PRODUCT_TITLE_W      L"PlexusX — Gaming Display Optimizer"
#define PX_PRODUCT_SUBTITLE     "Elite Windows Gaming Display & Visual Optimization Center"
#define PX_PRODUCT_SUBTITLE_W   L"Elite Windows Gaming Display & Visual Optimization Center"
#define PX_PRODUCT_COPYRIGHT    "Copyright (c) 2026 PlexusX Contributors — Apache-2.0"
#define PX_PRODUCT_COPYRIGHT_W  L"Copyright (c) 2026 PlexusX Contributors — Apache-2.0"
#define PX_PRODUCT_LICENSE      "Apache-2.0"
#define PX_PRODUCT_URL          "https://github.com/diamantmuqici-dotcom/PlexusX"
#define PX_PRODUCT_URL_W        L"https://github.com/diamantmuqici-dotcom/PlexusX"

/* ---- Compatibility ---- */
#define PX_MIN_WIN_MAJOR 10
#define PX_MIN_WIN_BUILD 19041   /* Windows 10 2004 — DXGI 1.6 baseline */
#define PX_SUPPORTED_ARCH "x64"
#define PX_SUPPORTED_ARCH_W L"x64"

/* ---- Feature flags (compile-time) ---- */
#define PX_FEATURE_GAMES        1
#define PX_FEATURE_CROSSHAIR    1
#define PX_FEATURE_DISPLAY_MGR  1
#define PX_FEATURE_PHONE        1
#define PX_FEATURE_PRESETS      1
#define PX_FEATURE_HDR_AWARE    1
#define PX_FEATURE_CRASH_RECOVERY 1

/* ---- Version comparison helpers (pure) ---- */
static inline int px_version_cmp(int maj_a, int min_a, int pat_a,
                                 int maj_b, int min_b, int pat_b)
{
    if (maj_a != maj_b) return maj_a < maj_b ? -1 : 1;
    if (min_a != min_b) return min_a < min_b ? -1 : 1;
    if (pat_a != pat_b) return pat_a < pat_b ? -1 : 1;
    return 0;
}

static inline int px_version_is_at_least(int maj, int min, int pat)
{
    return px_version_cmp(PX_VERSION_MAJOR, PX_VERSION_MINOR, PX_VERSION_PATCH,
                          maj, min, pat) >= 0;
}

#endif /* PLEXUSX_VERSION_H */
