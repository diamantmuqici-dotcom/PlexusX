# PlexusX Changelog

## Unreleased — Preset truth, website repair, honest download card

### Fixed
- **Website script abort (P0)**: `site/assets/app.js` referenced removed elements (`#stCode`, `#stCopy`, `#sha`, `#copySha`), throwing a TypeError during load that killed everything after it — scroll-reveal (most sections stayed at `opacity:0`), app-mock navigation, scene chips, crosshair picker, toasts and the profile recommender were all dead. All optional wiring is now null-checked, and the dead "preset code" (CHX-…) feature was removed. A new jsdom smoke test (`scripts/site_smoke.js`, wired into both CI workflows) fails the build on any uncaught page error.
- **Wrong brand in comparator**: the interactive comparator tag rendered "CHROMAX · 240%" instead of "PLEXUSX".
- **Invalid preset exports**: `PxPre_ToJson` emitted a trailing comma after the last preset, so exported `*.json` libraries were rejected by any strict JSON parser (only PlexusX's own tolerant reader accepted them). The export is now strict JSON, with a regression check in `tests/test_all.c`.
- **Misleading preset name**: the scene "Ultra 300%" shipped 180% saturation (a casualty of the archived bulk clamp `update_presets.py`, now moved to `scripts/archive_update_presets.py` with a do-not-run header). It is renamed "Vivid 300%" and carries the real 300/240 showcase values, identical in the scene table and the preset seed.
- **Divergent same-name presets**: "Competitive" and "Night Ops" had different values in the scene library vs the preset seed (185/160 vs 180/150; 235/190 vs 180/150). Both tables now agree; `tests/test_all.c` suite 14b enforces same-name ⇒ same-look across the two native tables.
- **Website preset drift**: the browser preview's preset buttons ("Competitive" 235/120, "Night Ops" 265/150, "Max Chroma" 300/300 …) matched nothing the app ships. `site/assets/presets.js` (new) mirrors the native seed exactly; `scripts/preset_parity.js` + `tests/preset_gen.c` prove it in CI. The preview also starts at the true neutral baseline (the old default claimed to be "the app's default boost" — the app actually starts neutral), its temperature slider now spans the 3000–10000 K the native UI exposes, and it degrades gracefully when 2D canvas is unavailable.
- **Website game gallery / recommender fiction**: gallery stats (sat 235–265) and recommender outputs (sat up to 260) advertised values no built-in profile uses. Both now mirror the real shipped looks from `game_preset_manager.c` (e.g. Rust Competitive 180/150, gamma 0.94; Tarkov Dark Room 0.76), and recommendations name the built-in preset they match.
- **Stale download facts**: README and website published SHA-256 hashes (`9c74c6…`, `21d360…`) and sizes (448/699 KB) that matched neither the committed binaries nor `SHA256SUMS.txt`. The pages now show the real hashes (`192c35…`, `cb56d1…`) with copy buttons, the Pages workflow injects the checksums of the binaries each deployment actually serves, and hard-coded sizes were dropped from buttons. "20+ game profiles" corrected to the real 18 built-ins everywhere.
- **Phone-server claims**: settings page and README said the LAN server "binds the LAN interface only"; it binds `INADDR_ANY` (all local interfaces, port 8777). Texts now state the real behavior plus the practical guidance (PIN privacy, stop when unused). No behavioral change.

### Added
- `site/assets/presets.js` — website mirror of the native built-in preset seed, with honest per-preset descriptions.
- `tests/preset_gen.c` + `scripts/preset_parity.js` — CI-enforced site↔native preset parity (8 presets × 17 fields).
- `scripts/site_smoke.js` — jsdom smoke test: executes the shipped site scripts, verifies reveal wiring, mock navigation, scene chips, recommender values, checksum card vs `SHA256SUMS.txt`, and graceful preview degradation. Negative-controlled against the original `#stCopy` regression.
- `tests/test_all.c` suite 14b (scene↔seed consistency, "300%" naming honesty, strict-JSON export) — 26 suites total.
- Toast notifications on the website are announced to screen readers (`role="status"` + `aria-live="polite"`).

## Unreleased — Hardware health reassertion

- Added a lightweight display-pipeline health check to detect when Windows or a display driver silently drops the active Magnification color effect or a PlexusX-owned gamma LUT.
- Reapplies the requested color state only after a real hardware-state mismatch is detected; normal slider changes remain event-driven and do not become a polling rewrite loop.
- Gamma verification is limited to LUTs currently owned by PlexusX, so untouched third-party calibration ramps are not overwritten.
- Preserves the existing honest exclusive-fullscreen/HDR limitations and legitimate Windows display APIs.

All notable changes to PlexusX are documented here. Format follows Keep a Changelog.

## [2.2.0] - 2026-10-01 — Premium Edition

### Added
- **Centralized versioning** (`core/version.h`): single source of truth for version, build date, product identity. Makefile injects build date via `-DPX_BUILD_DATE_OVERRIDE`.
- **Enhanced 10-state runtime status** (`core/runtime_status.h`): READY, APPLYING, APPLIED, LIMITED, HDR, EXCLUSIVE_FULLSCREEN, UNSUPPORTED, ERROR, RESTORING, SAFE_MODE with severity, tone, reason codes, actionable hints, JSON serialization. Maps from low-level `PxColorStatus` (6 states) + HDR + presentation + game facts.
- **Application state machine** (`core/app_state.h`): BOOT → LOADING → RECOVERING → READY → APPLYING → REASSERTING → SAFE_MODE → SHUTDOWN with flags, logging, first-run detection, portable mode, telemetry assertion (zero).
- **Event bus** (`core/event_bus.h`): synchronous, single-threaded, no polling. Events: foreground, display, color requested/applied/failed, game enter/exit/switch, safe reset, config dirty, display mode changing/changed, phone command, crosshair toggled, engine toggled, preset/profile loaded. History ring for diagnostics.
- **Security hardening module** (`security/security.h`): pure helpers for finite checks, clamped parsing, path traversal prevention, PIN/token validation, file size limits, audit struct. Documents guarantees: no injection, no game memory access, atomic writes, corrupt backup, no file/shell via phone.
- **Premium UI theme** (`ui_theme.h` v2.2): 7-level graphite depth, 5-step lime accent scale, 8 semantic tones + dim variants, 6 typography sizes + 4 weights, elevation shadows, animation durations (16ms micro, 120ms fast, 200ms normal, 320ms slow), accessibility hit targets (32px pointer, 44px touch).
- **Enhanced Home dashboard** (`panel_home.c`): 10-state runtime banner with tone, actionable hint, status cards with semantic flags, grouped quick actions (engine, looks, system), live preview with split + curve, keyboard hints.
- **Enhanced Global Color page** (`panel_color.c`): integrated runtime banner, triple cards with sync/diverged feedback, grouped controls with per-group reset and descriptions, quick-load presets, non-neutral visual feedback, version footer.
- **Makefile**: added `-Isrc/core -Isrc/security -Isrc/presets -Isrc/ui -Isrc/logging` include paths.

### Changed
- **common.h**: now includes `core/version.h` for centralized version. `PX_APP_NAME`, `PX_APP_TITLE` derive from `PX_PRODUCT_*_W`. `PX_VERSION` falls back to `PX_VERSION_W` if not defined.
- **ui_theme.h**: expanded from 14 defines to 45+ with depth, accent scale, semantic dim variants, chip tones (8), geometry (6 radii, 8 spacing), typography weights, animation, layout metrics.
- **Display manager**: HDR detection via DXGI 1.6 `IDXGIOutput6::GetDesc1` — `MaxFullFrameLuminance >= 300` heuristic for capable, `colorSpace` HDR families for active. `bpc`, `colorSpace`, `min/max/maxFF nits` reported, unknown renders as "—".
- **Game preset manager**: 18 built-in profiles (Rust 11 sub-modes, CS2 6, Fortnite 6, Valorant 4, Tarkov 5, PUBG 4, Apex 3, COD 4, Overwatch 4, R6 4, Minecraft 3, GTA 5, The Finals 4, DayZ 4, Helldivers2 4, ARC Raiders 3, BF2042 4, Destiny2 4) with alias table (FortniteLauncher, VALORANT, r5apex_dx12, ModernWarfare, Minecraft.Windows, GTA5_Enhanced, TslGame_BE, ArcRaiders shipping, Discovery shipping). Activation: no-stacking Global→Game→Global restore, delayed apply, monitor target restore, JSON export/import versioned.
- **Color pipeline**: DWM `MagSetFullscreenColorEffect` + `SetDeviceGammaRamp` split, dirty flag `dirty.flg`, crash recovery `ramps.dat` atomic rename, identity fallback, monotonic repair, verification via `MagGetFullscreenColorEffect`.
- **Runtime state**: 6 honest statuses DISABLED/PASSTHROUGH/APPLYING/ACTIVE/LIMITED/FAILED, `PxOutputFacts`, `PxEffectiveState` pure derivation, HDR → PASSTHROUGH reason "OS tone-mapper owns LUT", exclusive fullscreen → LIMITED curves-only.

### Fixed
- **Matrix layout**: `MagColorEffect` is `float[5][5]` 100 bytes, row-major, W' column `[0 0 0 0 1]` enforced. Old `float[4][5]` 80-byte bug would read 20 bytes stack garbage as translation row.
- **Translation row**: brightness/contrast/black-level live only in row 4 `m[4][0..2]`, never column 4. Mid-gray pivot preserved for contrast.
- **W' divisor safety**: `W' == 1.0` always, NaN/Inf → identity, weights clamped to `[-4, 4]` via uniform scale toward identity (gray stays gray).
- **Ramp monotonicity**: `cm_ramp_make_monotonic` repairs dither dips in place, degenerate ramps rejected. `cm_ramp_is_valid` checks monotonic + non-degenerate span.
- **Settings store**: UTF-16LE BOM aware, atomic tmp+MoveFileEx, corrupt file moved aside as `*.corrupt-N` (up to 5), never silent loss. `PxIni` total parser: bad_lines counted, duplicates FIRST wins, values round-trip normalized, NaN/Inf hostile.
- **Preset store**: same safety rules, `presets.ini` 256KB, JSON shareable export/import, built-in seed 20 presets (8 Global, 7 Display, 5 Crosshair).

### Security
- **Phone control**: LAN only (port 8777), 4-digit PIN 1000-9999 single-use → 32-hex-char token (128-bit), 5 tries → 30s lockout, 8 sessions LRU, TTL 8h, CORS `no-store`, no file/shell access, command surface limited to sliders/profile/engine/xhair, all values clamped to UI safe ranges.
- **Game detection**: `PROCESS_QUERY_LIMITED_INFORMATION` only, `QueryFullProcessImageNameW` read-only OS metadata, no injection, no hooks, no game memory/files modification.
- **File IO**: size checks before read (max 192KB config, 256KB presets, 256KB JSON), path traversal blocked (`..`, absolute, `:`, control chars), atomic writes, backup corrupt.
- **Input**: every numeric parse finite-checked, range-clamped, length-checked. `cm_sanitize_look` total.

### Performance
- **Event-driven**: `SetWinEventHook(EVENT_SYSTEM_FOREGROUND)` + coalesced settle timer 120ms one-shot, not polling. Burst folds into ONE re-assert.
- **Ramp planning**: `PxPlan_RampForDisplay` skips neutral/unchanged, `have_curr` tracking, identical curr → no `SetDeviceGammaRamp` call.
- **Matrix dedup**: `memcmp` identical → skip `MagSetFullscreenColorEffect`.
- **Idle**: no timers when nothing pending, log ring 128 entries event-driven.

### Documentation
- **README**: updated with 10-state runtime, security audit, architecture map.
- **Website**: parity badge, color engine JS exact `Math.fround` port of `color_math.h`, identity/mul/chromaEff/saturation/hue/tempTintGain/briCon/sanitize, calcRamp monotonic, interactive comparator + game gallery + app mock.
- **Diagnostics**: text + JSON builder `PxDiag_BuildText/Json`, requested/effective/applied triple, HDR explanation, GPU caps, game output verdict, config corrupt flags, event log chronological.

### Tests
- **25 suites** in `tests/test_all.c`: Kelvin Planckian (NaN/Inf/out-of-range → neutral), gamma ramp identity/monotonic/linear-leak, aspect ratio, JSON bounds clamping, 300% saturation matrix math (Rec.709 luminance preserved, gray fixed point), matrix layout 100 bytes row-major, W' safety, translation row, hue rotation preserves white/grays/luminance + rotation group, linear vs non-linear split, ramp monotonicity 20k+ sweeps, ramp write planning, ramps.dat validation (size, names, monotonic, non-degenerate, one bad poisons whole), shipped presets safe, high vibrance neutrals no green cast, requested/applied distinct + survives focus loss, transform plan sanitize/determinism/preview identity/ramp planning, display state presentation classes + HDR verdicts + mode picking, game state machine no-stacking cycle, window state coalescing, settings store lenient parse + v1 migration + packed look safety, profile store round trip, diagnostics ring + escaped JSON + applied outcome derived, preset library 20 seed x INI+JSON round trip + CRUD + hostile clamping, display capabilities HDR derivation honest unknowns.

## [2.1.0] - Previous
- Initial premium release with unified preset library, display capabilities, diagnostics report.

## [2.0.0] - Previous
- Core pipeline with DWM matrix + gamma ramps, game profiles, crosshair, phone remote.
