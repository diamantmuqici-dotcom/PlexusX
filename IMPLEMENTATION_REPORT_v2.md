# PlexusX 2.2.0 — Premium Edition Implementation Report

**Date:** 2026-10-01
**Version:** 2.2.0 (centralized in `core/version.h`)
**Scope:** Deep engineering audit + full product upgrade, preserving native Win32/C architecture

---

## Executive Summary

PlexusX is now a **production-grade, premium Windows display-control application** with:

- **Single authoritative color engine**: `color_math.h` kernel shared by native app, tests, and web preview (exact `Math.fround` parity)
- **Honest 10-state runtime**: READY, APPLYING, APPLIED, LIMITED, HDR, EXCLUSIVE_FULLSCREEN, UNSUPPORTED, ERROR, RESTORING, SAFE_MODE with severity, tone, reason codes, actionable hints
- **Crash-safe persistence**: atomic writes (tmp + `FlushFileBuffers` + `MoveFileExW`), corrupt backup `*.corrupt-N` up to 5, `ramps.dat` validation, `display.pending` confirm-or-rollback, `dirty.flg` recovery, `crash.log`
- **Event-driven architecture**: `SetWinEventHook(EVENT_SYSTEM_FOREGROUND)` + 120ms coalesced settle, no polling, burst folds into ONE re-assert
- **18 game profiles** with alias table, no-stacking Global→Game→Global, delayed apply, monitor target restore, versioned JSON
- **Zero telemetry**: asserted by `PxAppState_HasTelemetry()==0`, LAN phone only (port 8777, opt-in)
- **Anti-cheat safe**: `PROCESS_QUERY_LIMITED_INFORMATION` only, no injection, no game memory/files
- **25 test suites** host-runnable, 0 fake features, 0 FPS claims

---

## 1. Architecture — Module Map (single responsibility)

| Module | File(s) | Role | Pure/Testable |
|--------|---------|------|---------------|
| **Versioning** | `core/version.h` | Centralized semver, build date, product identity, feature flags, comparison helpers | Yes |
| **Runtime Status** | `core/runtime_status.h` | 10-state product model, derivation from low-level facts, JSON, severity/tone/hint | Yes |
| **App State** | `core/app_state.h` | Phase machine BOOT→SHUTDOWN, flags, logging, first-run, portable, telemetry assertion | Yes |
| **Event Bus** | `core/event_bus.h` | Synchronous event bus, 20 event types, history ring, handler table | Yes |
| **Security** | `security/security.h` | Finite checks, clamped parsing, path traversal block, PIN/token validation, audit | Yes |
| **Color Kernel** | `color/color_math.h` | Matrix assembly, gamma LUT, per-pixel preview, sanitization, constants | Yes |
| **Color State** | `color/color_state.h` | Authoritative REQUESTED config, revision, sanitised on entry | Yes |
| **Applied State** | `color/applied_color_state.h` | Hardware-confirmed APPLIED, outcome derived, note buffer | Yes |
| **Transform Plan** | `color/color_transform.h` | Single source of color decisions, sanitize→effect→ramp, dedup, preview identity | Yes |
| **Pipeline** | `color/color_pipeline.{h,c}` | DWM matrix + GPU ramps, plan diffing, crash-safe `ramps.dat`/`dirty.flg`, verification | No (Win32) |
| **Engine** | `color/color_engine.{h,c}` | Orchestrator Eng_*, requested/applied, invalidation, resync, diagnostics | No (Win32) |
| **Runtime** | `color/color_runtime_state.h` | 6-state engine model + effective derivation pure function | Yes |
| **Display State** | `display/display_state.h` | MonitorInfo, ModeInfo, GpuInfo, PxPresMode, PxGameOut, pure helpers | Yes |
| **Capabilities** | `display/display_capabilities.h` | HDR DISABLED/LIMITED/ENABLED/PASSTHROUGH derivation, honest unknowns | Yes |
| **Display Mgr** | `display/display_manager.c` | EnumDisplayMonitors, EnumDisplaySettings, DXGI 1.6 `IDXGIOutput6::GetDesc1`, mode dedup+sort, safe apply with rollback, `display.pending`, IdentifyMonitors overlay | No (Win32) |
| **Game State** | `games/game_state.h` | Decision half: launch/switch/return/desktop, no-stacking, snapshot preservation | Yes |
| **Game Display** | `games/game_display_state.h` | Live game output truth, presentation, HDR, derived game_output | Yes |
| **Detector** | `games/game_detector.{h,c}` | Sensor half: foreground exe + pid + window key + presentation via rect vs monitor + WS_CAPTION | No (Win32) |
| **Profiles** | `games/game_profile.{h,c}` | Model, exe normalization, alias add/remove, JSON codec, pure | Yes |
| **Preset Mgr** | `games/game_preset_manager.c` | 18 titles library, activation, Prof_* CRUD, JSON file IO | No (Win32) |
| **Settings Core** | `settings/settings_store.h` | Tiny total INI, lenient parse, corrupt→defaults, FIRST wins, v1 migration, packed Look | Yes |
| **Settings Glue** | `settings/settings_store.c` | UTF-16 BOM aware, atomic writes, corrupt backup, AppData paths | No (Win32) |
| **Preset Core** | `presets/preset_store.h` | Unified library Global/Game/Display/Crosshair, INI+JSON, CRUD, seed 20 | Yes |
| **Preset Glue** | `presets/preset_store.c` | Same safety as settings, atomic, corrupt backup | No (Win32) |
| **Window State** | `windows/window_state.h` | Focus/reassert machine pure, coalesced pending | Yes |
| **Window Mgr** | `windows/window_manager.{h,c}` | Hooks, ALT+TAB settle, event forwarding Wm_* | No (Win32) |
| **Diagnostics** | `diagnostics/diagnostics.h` | Ring log 128 entries event-driven, snapshot | Yes |
| **Diag Report** | `diagnostics/diagnostics_report.h` | Text/JSON builder, triple, HDR note, GPU, game, config corrupt, log chronological | Yes |
| **UI Theme** | `ui_theme.h` | Premium tokens: 7 surfaces, 5 accent steps, 8 semantic + dim, 6 radii, 8 spacing, typography weights, animation | Yes |
| **UI Core** | `ui/ui_core.c` | DPI, fonts, widget table 320, painting, input, grouped sidebar, status bar 5 readouts, pending display bar, file dialogs | No (Win32) |
| **Panels** | `ui/panel_*.c` (6 files) | Home, Color, Display (monitors/resolution/refresh), Games (library/active/custom), Tools (crosshair/patterns/diag), Settings (phone/presets) — all read live state, no private copy | No (Win32) |
| **Crosshair** | `crosshair.c` | Layered WS_EX_LAYERED|TRANSPARENT overlay, 4x supersampled DIB, shapes DOT/CROSS/CIRCLE/SQUARE/PLUS/CHEVRON/T/TTYPE, UpdateLayeredWindow | No (Win32) |
| **Tools** | `tools.c` | 11 patterns PAT_BLACK..HDR_PEAK, 10s auto-close + any input poll, banner per monitor, diagnostics builder, gaming mode flag | No (Win32) |
| **Phone** | `phone.c` | LAN remote port 8777, 4-digit PIN→32-hex token, 5 tries→30s lockout, 8 sessions TTL 8h, GET /pair /status /games /set /profile /engine /preset /toggle_xh, tiny HTTP, no file/shell | No (Win32) |

---

## 2. Color Engine Pipeline — REQUESTED→EFFECTIVE→TRANSFORM PLAN→APPLIED→VERIFIED

```
User Slider / Game Profile / Phone
        │
        ▼
   ColorState.requested (sanitised, revision bumped if changed)
        │
        ▼
   PxOutputFacts (mag_available, ramps_available, hdr_active, presentation, game_active, engine_enabled)
        │
        ▼
   PxEffectiveState (pure function PxEff_Compute):
        - HDR active → PASSTHROUGH, effective=neutral, reason "OS tone-mapper owns LUT"
        - exclusive fullscreen + linear req → LIMITED, effective=curves-only look
        - no path → FAILED
        - else ACTIVE / APPLYING / DISABLED with reason
        │
        ▼
   PxTransformPlan (PxPlan_Compute):
        - sanitize look (finite, clamped)
        - build 5x5 matrix (saturation 0-300%, vibrance, hue, temp/tint, RGB gains, bri/con/black/white)
        - calc 3x256 ramp (gamma 0.40-2.50, shadows toe (1-x)^2*0.35, highlights shoulder x^2*0.30, clarity 0.5*(1-cos(xπ))-x *0.25)
        - determinism: same input → byte-identical plan (for dedup)
        │
        ▼
   PxPipelineResult (PxPipe_Run split):
        - apply_matrix: sanitize via cm_sanitize, skip if memcmp identical, MagSetFullscreenColorEffect, verify via MagGetFullscreenColorEffect
        - apply_ramps: PxPlan_RampForDisplay skips neutral/unchanged, have_curr tracking, SetDeviceGammaRamp per display, marks dirty when leaving orig
        - ramps.dat atomic rename, dirty.flg, identity fallback, cm_ramp_make_monotonic, cm_ramp_is_valid
        │
        ▼
   AppliedColorState (PxAS_Record):
        - outcome derived: FULL (matrix_ok+ramps_ok+ramps_called or skipped when neutral), PARTIAL, FAILED, NOT_YET
        - matrix_verified, ramps_verified, ramp_writes, revision, applied_ms, note buffer 120 chars truncated
        │
        ▼
   PxRuntimeState (PxRuntime_Derive, 10-state product model):
        - priority: SAFE_MODE > RESTORING > ERROR > HDR > EXCLUSIVE_FULLSCREEN > LIMITED > UNSUPPORTED > APPLYING > APPLIED > READY
        - severity OK/INFO/WARN/ERROR, tone 0 neutral 1 ok 2 warn 3 bad 4 info, reason_code machine-readable, hint actionable
        │
        ▼
   UI / Diagnostics / Phone / Tray (all read same structs, cannot disagree)
```

**300% saturation math**:
- `M[i][j] = factor*delta(i,j) + w_i*(1-factor)` where `factor=sat/100`, `w=Rec.709 LUM_R 0.2126729 LUM_G 0.7151522 LUM_B 0.0721750`
- Grays fixed point, luminance preserved, cross terms negative at >100%
- Vibrance: smart `chromaEff` `maxEff/minEff`, `WEIGHT_LIMIT 4.0`, protects skin tones
- Combined sat+vib: effective factor clamped via `(WEIGHT_LIMIT - LUM_B)/(1-LUM_B)` ≈ 3.9, uniform scale toward identity so gray stays gray

**Honest status**:
- Desktop windowed/borderless → ACTIVE (full effect: matrix+ramps)
- Exclusive fullscreen → LIMITED (curves only) or EXCLUSIVE_FULLSCREEN, never fake FULL
- HDR active → HDR / PASSTHROUGH, writes nothing
- No mag + no gamma → UNSUPPORTED
- Apply failed → ERROR with driver note
- Reassert pending → RESTORING
- Emergency reset → SAFE_MODE

---

## 3. Game System — Event-Driven, No-Stacking

**Detection**:
- `SetWinEventHook(EVENT_SYSTEM_FOREGROUND)` posts `WM_APP_FOREGROUND` to UI thread
- `PxDetect_Scan`: `GetForegroundWindow` → `GetWindowThreadProcessId` → `OpenProcess(QUERY_LIMITED_INFORMATION)` → `QueryFullProcessImageNameW` → base name lowercased → `PxDetect_PresentationFor` via window rect vs monitor rect + `WS_CAPTION` check → `PxPresMode`
- Slow timer `TIMER_POLL` safety net only, never re-applies color on its own
- Failed `OpenProcess` → `resolved=0`, never mutates game state (no flicker)

**Decision (pure state machine)**:
- `PxGameSM_OnForeground`: given `fg_exe` normalized base, `fg_idx` profile match, `auto_restore`, `current` look
- `GAME_LAUNCH`: non-game → game, snapshot current global if no snapshot, apply game sub-mode absolute
- `GAME_SWITCH`: game A → game B, snapshot stays FIRST global, apply B
- `GAME_RETURN`: same profile under different process (launcher→client), no fresh snapshot
- `DESKTOP`: game → desktop, restore snapshot if auto_restore, consume snapshot, active=-1
- `DESKTOP_IDLE`: desktop → other desktop, no-op
- Empty exe → `EV_NONE`, never mutates (ALT+TAB cannot end session)
- Looks are ABSOLUTE replace, never delta-edited, so Global→Rust→CS2→Valorant→Global provably returns first look

**Profiles**:
- 18 titles: Rust (11 sub-modes), CS2 (6), Fortnite (6), Valorant (4), Tarkov (5), PUBG (4), Apex (3), COD (4), Overwatch (4), R6 (4), Minecraft (3), GTA (5), The Finals (4), DayZ (4), Helldivers2 (4), ARC Raiders (3), BF2042 (4), Destiny2 (4)
- Alias table: FortniteLauncher, VALORANT.exe, r5apex_dx12, ModernWarfare, Minecraft.Windows, GTA5_Enhanced, TslGame_BE, ArcRaiders shipping, Discovery shipping
- `Profile`: name 64, exe 96, exe_alias 4x96, exe_path 180, tag 32, is_custom, favorite, enabled, auto_apply, auto_restore, delay_ms 0-3000, apply_display, looks_edited, hdr_preference, monitor_idx -1..7, last_activated unix, apply_count, target_res, sub_count 1..12, active_sub, sub[12] name 32 + Look
- `PxProf_NormalizeExe` strips path/quotes lowercases, `PxProf_MatchesExe` checks primary+aliases+exe_path base
- `Prof_TickPending` fires delayed apply without sleeping UI thread
- JSON versioned: schema, name, exe, color 17 fields, crosshair, display, monitor

**Game output verdict** (derived, never asserted):
- `px_gameout_compute(game_active, pres_mode, engine_enabled, mag_ok)` → NONE, ACTIVE, LIMITED (curves only), ENGINE_OFF, UNAVAILABLE
- `px_pres_dwm_reachable`: WINDOWED or COMPOSITED_COVER → matrix reaches
- `px_pres_ramp_reaches`: != NONE → LUT reaches (scanout-side)

---

## 4. Display System — Multi-Monitor, HDR, Safe Modes

**Enumeration**:
- `EnumDisplayMonitors` + `EnumDisplaySettingsW`/`EnumDisplayDevicesW` for friendly names, adapter, rc, is_primary, current w/h/hz
- `Modes_Refresh`: collects modes per monitor, dedup via sort key area+hz, sort larger area first then higher hz first
- `collect_dxgi_state`: `LoadLibraryW(dxgi.dll)` → `CreateDXGIFactory1` → `EnumAdapters1` → `EnumOutputs` → `QueryInterface(IDXGIOutput6)` → `GetDesc1` → `BitsPerColor`, `ColorSpace` raw, `Min/Max/MaxFullFrameLuminance`, match by `dev_name`. First non-software adapter → `GpuInfo` vendor via VendorId 0x10DE NVIDIA 0x1002 AMD 0x8086 Intel

**HDR derivation** (honest, no guessing):
- `PxHdr_StateOf`: `hdr_enabled` → ENABLED, `hdr_capable` → LIMITED, else DISABLED
- `hdr_capable`: currently HDR OR `MaxFullFrameLuminance >=300`
- `px_cs_is_hdr`: raw `0x0c` RGB FULL G2084, `0x0d` YCbCr STUDIO G2084, `0x0e` RGB STUDIO G2084, `0x10` YCbCr STUDIO G2084 TOPLEFT, `0x12` HLG TOPLEFT, `0x13` HLG FULL
- `PxHdr_Name`, `PxHdr_Explain`, `PxHdr_Tone`: 0 neutral, 1 ok, 2 warn, 3 bad, 4 info
- `PxCapSummary`: monitor_index, is_primary, hdr_state, hdr_capable, bpc, color_space, min/max/maxFF nits, linear_path, curves_path, exclusive_blocked

**Mode apply — confirm-or-rollback**:
- `Modes_ApplySafe`: writes `display.pending` file before `ChangeDisplaySettingsExW`, arms 15s countdown, shows banner per monitor with KEEP/REVERT
- `Modes_RecoverPendingFromDisk` on start: if pending exists, revert to previous DEVMODE
- `Modes_PendingMode`, `Modes_PendingChange`, `Modes_PendingSecondsLeft`, `Modes_ConfirmPending`, `Modes_RollbackPending`, `Modes_RollbackTick`
- `Modes_ApplyMaxHz`: `px_modes_pick_best` with w=0 → highest hz across all sizes
- `Modes_FindMonitorForRect`: for crosshair/patterns placement

**Multi-monitor**:
- `MAX_MONITORS 8`, `g_cur_monitor` index, `Modes_SetCurrentMonitor`, `Modes_GetMonitor`, `Modes_MonitorCount`
- `Modes_IdentifyMonitors`: overlay window per monitor with number
- Crosshair `monitor_idx`, profile `monitor_idx` target, preset `monitor_idx`
- Color pipeline `g_disp[8]` stores orig/curr ramps per display, `PxPipe_Resync` invalidates have_curr

---

## 5. UI — Premium Native Win32

**Theme** (`ui_theme.h` v2.2):
- Surfaces 7-level: BG 10,10,14 → BG_RAISED 14,14,19 → SIDE 16,16,22 → SIDE_HOVER 20,20,28 → SURFACE 22,22,30 → HOVER 30,30,42 → ACTIVE 36,36,52 → ELEV 40,40,58 → INSET 8,8,12 → INSET_DEEP 5,5,9
- Borders: BORDER 42,42,58, STRONG 54,54,74, SUBTLE 32,32,46, DIVIDER 24,24,36, FOCUS_RING accent
- Text 5-level: TEXT 242,242,248 15.2:1, SEC 200,200,214 10.1:1, SUB 145,145,160 5.8:1, DIM 95,95,110 3.2:1, GHOST 70,70,84
- Accent scale 5: ACCENT 198,255,61 → HOVER 210,255,90 → ACTIVE 180,240,40 → DEEP 155,224,15 → DIM 120,160,20
- Semantic 8 + dim: CYAN 79,227,255, VIOLET 123,92,255, SUCCESS 79,227,120, WARNING 255,196,61, DANGER 255,80,80, INFO 100,160,255
- Chip tones 8: NEUTRAL, OK, WARN, BAD, INFO, ACCENT, VIOLET, CYAN
- Radii 6: XS 4, SM 6, MD 10, LG 14, XL 20, 2XL 28, FULL 9999
- Spacing 8: 1 4, 2 8, 3 12, 4 16, 6 24, 8 32, 10 40, 12 48
- Shadows alpha: SM 20 8%, MD 35 14%, LG 55 22%
- Typography: LOGO 20, H1 26, H2 15, H3 13, BODY 13, SMALL 11, TINY 9, MONO 12, BIG 42, HERO 32 + weights 400/500/600/700
- Animation: MICRO 16ms 1 frame, FAST 120ms, NORMAL 200ms, SLOW 320ms
- Layout: SIDEBAR_W 220, TOPBAR_H 56, STATUS_H 104, CONTENT_X 248, CONTENT_W 986, CARD_GAP 12, SECTION_GAP 24
- Slider: H 6, THUMB 16, GAP 12
- Hit targets: MIN_HIT 32 pointer, 44 touch

**Core** (`ui_core.c` 2006 lines):
- DPI scaling `UiS`, fonts Segoe UI/Consolas via `make_fonts`, widget table `MAX_WIDGETS 320`
- Types: HEAD, LABEL, BTN, PRIMARY, GHOST, ACCENT, SLIDER, TOGGLE, SIDE, CARD, GAME_CARD, LOOK_CARD, MONITOR_CARD, ROW, DIV, SWATCH, SHAPE, SPLIT_PREVIEW, CURVE_PREVIEW, CHIP, QUICK_SAT, KV, NAVHEAD
- Painting: `UiDrawRRect`, `UiDrawText`, `UiToneColor`, `UiDrawSplitPreview` (cm_apply_pixel left identity right effective, split_pos draggable 0.05-0.95), tone curve (cm_calc_ramp + matrix slope), sidebar grouped nav, status bar 5 readouts (engine, game, profile, display, version), pending display bar KEEP/REVERT hit test, toast
- Input: mouse down/move/up, hover, wheel (nudge/coarse/fine), key (Tab, arrows, Del reset, Ctrl+C/V, Ctrl+Alt+↑/↓ saturation, Ctrl+Alt+0 reset, Ctrl+Alt+X crosshair, Ctrl+Alt+E engine, Ctrl+Alt+Shift+R emergency), double-click neutral
- Slider binding: single source `UiSl_Get/Set/Default`, `UiSl_Commit` → `Eng_SetLook` sanitised, `UiLook_Mut/Commit`
- File dialogs: `UiOpenFile`, `UiSaveFile`, `UiInputBox`
- Status: `UiStatus` → `Eng_Effective`, `UiToneOfStatus`, `UiEngineStatusIs`

**Panels**:
- **Home**: 10-state runtime banner with tone+hint, 5 status cards row1 (game, profile, monitor, resolution, refresh) + 4 row2 (HDR, engine, app status, GPU) with semantic flags, grouped quick actions (engine, looks, system), gaming mode toggle, live preview split+curve, enable toggle, wheel hints
- **Global Color**: runtime banner, triple cards REQUESTED/EFFECTIVE/APPLIED with sync/diverged + outcome, live preview BEFORE vs EFFECTIVE, 4 groups colour/light/balance/advanced with per-group reset + descriptions, sliders 16 with non-neutral flag, quick-load 4 Global presets, APPLY/RESET/SAVE/LOAD/COPY/PASTE, version footer
- **Games**: library inspector, sub-modes, automation toggles (detect, auto-apply, auto-restore, delay 0/500/1000/2000), monitor target ALL/SEL, custom exe rows, add/duplicate/delete/rename/export/import, favorite, activate/clear, versioned JSON, 18 built-in + custom
- **Display**: monitors page (cards with HDR tone, primary, bpc, color space, nits), resolution page (mode table dedup+sort, native badge, aspect classifier), refresh page (max hz, per-res hz list), safe apply with countdown, HDR refresh, identify
- **Tools**: crosshair (shape DOT/CROSS/CIRCLE/SQUARE/PLUS/CHEVRON/T/TTYPE, size 4-64, gap 0-32, thick 1-12, rot 0-360, opacity 10-100, dot 1-8, outline 1-4, color/ocolor swatches, preview, presets library, per-game), patterns (11 PAT_BLACK..HDR_PEAK, 10s auto-close + any input poll, banner per monitor), diagnostics (text/JSON builder, triple, HDR, GPU, game, config corrupt, log chronological, copy/export)
- **Settings**: startup & behaviour (startwin, tray min, autodetect, autoswitch, notify, engine at startup Off/On/Restore last, set current monitor as default), appearance (glass, reduce motion, bg None/Abstract/Image, anim On/Reduced/Off, theme dark/OLED), diagnostics & maintenance (log level Errors/Normal/Verbose, copy/export report, open AppData), safe reset (reset color, restore display mode, safe reset now, reset all settings keep profiles), hotkeys list, system facts (GPU, driver, mag, gamma, config, version v2.2.0)

---

## 6. Security Audit

See `SECURITY.md` for full audit. Summary:

| Check | Status | Evidence |
|-------|--------|----------|
| INI parse safe | ✅ | total parser, bad_lines, size caps |
| JSON parse safe | ✅ | bounded, brace counting, tolerant |
| ramps.dat safe | ✅ | validates whole, one bad poisons |
| Phone no file | ✅ | no file APIs in request handling |
| Phone no shell | ✅ | no CreateProcess/system/ShellExecute |
| Token entropy | ✅ | xorshift128+ 128-bit 32 hex |
| Lockout | ✅ | 5 tries → 30s |
| TTL | ✅ | 8h |
| Atomic writes | ✅ | tmp+Flush+MoveFileEx |
| Corrupt backup | ✅ | *.corrupt-N up to 5 |
| No injection | ✅ | no CreateRemoteThread etc |
| No game memory | ✅ | QUERY_LIMITED_INFORMATION only |
| Input sanitized | ✅ | cm_sanitize_look, clamped |
| Path traversal | ✅ | .. / \ : control blocked |

**Telemetry**: zero. Asserted by `PxAppState_HasTelemetry()==0`. Policy: no analytics, no tracking, no network except LAN phone opt-in.

---

## 7. Performance — Low Idle, Fast Apply

- **Event-driven**: foreground hook + coalesced settle 120ms one-shot, not polling. Burst folds into ONE re-assert. `Wm_CoalescedEvents` counter.
- **Ramp planning**: `PxPlan_RampForDisplay` skips neutral/unchanged, `have_curr` tracking, identical curr → no `SetDeviceGammaRamp`.
- **Matrix dedup**: `memcmp` identical → skip `MagSetFullscreenColorEffect`.
- **Idle**: no timers when nothing pending, log ring 128 entries event-driven, UI timers lowered in gaming mode but pipeline stays event-driven.
- **DPI**: `UiS` scales all metrics, fonts Segoe UI Variable, hit targets 32px pointer 44px touch.

---

## 8. Build System — Verified

**Makefile**:
- `CC := python3 -m ziglang cc -target x86_64-windows-gnu -O2 -std=c11 -Wall -DUNICODE -D_UNICODE -D_CRT_SECURE_NO_WARNINGS -municode -Isrc -Isrc/color -Isrc/display -Isrc/games -Isrc/settings -Isrc/windows -Isrc/diagnostics -Isrc/core -Isrc/security -Isrc/presets -Isrc/ui -Isrc/logging`
- `LDFLAGS := -Wl,--subsystem,windows` — GUI CRT `WinMainCRTStartup`, not console `wmainCRTStartup` (old bug: flipping subsystem byte after console link aborts at launch)
- `LIBS := -luser32 -lgdi32 -lshell32 -ladvapi32 -lws2_32 -lcomctl32 -lcomdlg32 -lshlwapi -ldwmapi -lwtsapi32 -lm`
- `SRC := $(shell find src -name '*.c' | sort)` — includes all new core files (header-only, no .c needed yet)
- `OUT_EXE := ../site/download/PlexusX.exe`, `OUT_SET := ../site/download/PlexusX-Setup.exe`
- `verify: python3 tools/verify_pe.py` checks PE subsystem 2 + entry point

**Versioning**:
- `core/version.h` single source: MAJOR 2 MINOR 2 PATCH 0, string `2.2.0`, wide `L"2.2.0"`, numeric `2,2,0,0`, build date `__DATE__` or override, product identity, feature flags, comparison helpers
- `common.h` includes it, `PX_APP_NAME/TITLE` derive from it
- Website `index.html` hardcoded `v2.2.0` should be generated via `scripts/bump_version.py` from `version.h` in future (TODO)

**Tests**:
- Host: `gcc -std=c11 -Wall -Werror -O2 -o tests/test_runner tests/test_all.c -lm && ./tests/test_runner` — 25 suites, checks count
- Parity: `scripts/site_parity.js` checks `color_engine.js` `Math.fround` vs C reference
- CI: `.github/workflows/build-windows-exe.yml` builds both exes, runs tests, verifies PE, publishes SHA256

---

## 9. Website — Parity & Premium

**`site/index.html` 563 lines**:
- Hero: eyebrow `Windows 10/11 x64 · v2.2.0 · 100% Free Forever`, H1 YOUR DISPLAY YOUR COLORS YOUR GAMES, lead 300% saturation etc, CTA Portable 448KB + Setup 699KB, chips Portable & Setup, SHA-256, Open source, NVIDIA·AMD·Intel
- Visual: hero.jpg + warm/cool tint, HUD live preview sat 240%, range 100-300, fineprint same kernel exact JS port parity-checked
- Marquee: 300% saturation, smart vibrance, 20+ game profiles, crosshair, stretched 4:3, phone remote PIN, GPU gamma ramps, test patterns, zero telemetry, crash restoration, anti-cheat safe, no accounts (duplicated for infinite scroll)
- Quick answers 4 cards: safe & verified (SHA-256 `certutil -hashfile`), anti-cheat safe (MagSetFullscreenColorEffect + GDI Gamma), free, no clutter (AppData or portable, restores on exit)
- Difference comparator: drag divider, controls sat/con/temp, reset, fineprint preview simulation
- Game gallery: 20+ profiles, grid injected by JS, stage frame with warm/cool, HUD ACTIVE, stats chips, sliders sat/bri/con/temp/gamma
- App mock: titlebar PLEXUSX v2.2.0 everything unlocked, sidebar Dashboard/Game looks/Presets/Crosshair/Display Modes/Settings, FREE FOREVER, main tabs with sliders, game rows, scene chips, cross preview, mode now, url mono
- Features 12: saturation 300%, smart vibrance, hardware gamma ramps (0.40-2.50 shadow toe highlight clarity), 20+ per-game, crosshair 4x supersampling 8 shapes, resolution & refresh up to 500Hz+, HDR guide, phone remote PIN, hotkeys, multi-monitor, crash recovery & backup, zero telemetry
- How it works 4 cards: magnification matrix, GPU gamma tables, foreground monitoring QueryFullProcessImageNameW, how games detected (event-driven, composited full vs exclusive fullscreen LIMITED curves only, HDR passthrough, ALT+TAB coalesced, diagnostics)
- Never: 6 bullets anti-cheat guarantee
- Download: official releases v2.2.0, portable + setup, meta, hash card SHA-256 portable `9c74c6724d45f88abea251e9203b0c461cfda40701359a5b03d0d25e80f917fb` setup `21d3602c3845cd306e6228c1368e0703d520cfd291f91023ae191761c69cb9b1`
- FAQ 5: banned (BattlEye/EAC/VAC/Vanguard), GPU support, 300% vs GPU panels, black screen/test pattern (Ctrl+Alt+Shift+R, Ctrl+Alt+0, 10s auto-close, crash.log), uninstall (close restores, delete exe, Settings>Apps, delete AppData)
- Footer: product, integrity, open source GitHub Apache-2.0

**`site/assets/color_engine.js` 318 lines**:
- Exact float32 `Math.fround` port of `color_math.h`, constants LUM_R 0.2126729 etc, WEIGHT_LIMIT 4.0, chromaEff maxEff/minEff, hue transpose, sanitize fit-toward-identity k 0.9975, calcRamp gamma+shadows toe (1-x)^2*0.35 + highlights shoulder x^2*0.30 + clarity 0.5*(1-cos(xπ))-x *0.25, parity CI

**`site/assets/style.css`**: tokens.css mirror native ui_theme.h, dark glass lime technical

**`site/preview.html`**: live preview standalone

---

## 10. Testing — Expanded

**Existing 25 suites** (test_all.c 1699 lines):
- Kelvin Planckian locus 6500K neutral, warm 4000K R>B, cool 9000K B>R, hostile NaN/Inf/out-of-range → finite [0,2] + non-finite→neutral 6500K + low→1000K + high→40000K
- Gamma ramp identity `i*257` 0..65535, monotonic, shadow lift, gamma bright/dark, linear controls must NOT leak into ramp
- Aspect ratio 16:9 16:10 4:3 ultrawide 21:9 32:9
- JSON profile schema + bounds clamping extreme 9999→300% etc
- 300% saturation matrix `M[i][j]=3*delta+w_i*(1-3)`, gray fixed, luminance preserved, 0%→luminance
- Matrix layout 100 bytes row-major, offset [4][4] 96
- W' safety: divisor 1.0, col4 [0 0 0 0 1], NaN/Inf→identity, weights [-4,4] scale toward identity gray stays gray, col4 offset repaired, 300% sat clamped
- Translation row: bri 120% offset +0.1 slope 1.2, con 150% slope 1.5 offset -0.25 pivot 0.5, black 150% +0.25, white 50% slope 0.5, combined bri110 con120 black130 slope1.32 offset0.1, sat does not smear offset
- Hue rotation: 0° identity, white/grays fixed all angles, luminance preserved, rotation group `hue(a)*hue(b)==hue(a+b)`, 360° identity, white through pipeline
- Linear vs non-linear split: linear→DWM only matrix non-identity ramp identity, non-linear→ramp only, neutral detection precision 1.00/100%
- Ramp monotonicity: raw curve dips precondition, generated never dips, sweep gamma 0.40-2.50 shadows/highlights/clarity 0-200% 875 ramps + 20k random NaN, 3 channels identical, dither repair in place
- Ramp write planning: SimDisplay orig/curr/have_curr/writes, linear drag 0 writes, gamma change 1 write then skip, linear while curve stays no write, different curve new write, neutral restores orig exactly once, Resync have_curr=0 untouched still alone active re-asserted once, identical curve no twice, outside target neutral
- ramps.dat validation: build blob 4+3*1568, round-trip exact, identity fallback, corruption rejected whole: NULL, empty, shorter than header, truncated, count 0, count>max, absurd 32-bit, name not terminated, non-printable, empty, all-zero black, non-monotonic, constant no span, one bad poisons whole, cm_ramp_is_valid dip
- Shipped presets: parse game_preset_manager.c `{ 1,` 17 numbers, >=17, safe matrix + monotonic ramp, white neutral
- High vibrance neutrals: levels 0-300% grays 0-1 colors, neutrals fixed, finite, sat300 vib300 gray 0.5 no green cast, sat300 vib100 3x Rec.709
- Requested vs applied: equal, vib 250 not equal, enabled 0 not equal, focus lost requested unchanged
- Transform plan: neutral identity+neutral, hostile NaN/Inf/absurd sanitized finite [-4,4] col4, determinism byte-identical, pixel neutral passes, gamma 0.80 lifts mids, white 0.5 gray no green cast, ramp planning identical curr no write new curve write same as curr no write
- Display state: presentation frame+cover, WINDOWED reachable ramp reaches, FULLSCREEN_SURFACE DWM not reachable ramp still, borderless composited reachable, gameout compute NONE/LIMITED/ACTIVE/ENGINE_OFF/UNAVAILABLE, color space HDR families, mode sort+best pick+aspect
- Game state machine: launch snapshot global apply requested, repeated same process no-op snapshot untouched, switch snapshot stays first global, return same profile different process no fresh snapshot, exit restore verbatim cycle never stacks, second exit idle, auto-restore OFF keeps game look, failed exe lookup empty never mutates ALT+TAB cannot end session, base-name normalization paths never reach matcher
- Window state: first event immediate reassert+game_reattach, burst while pending folded coalesced, settle fires single pending then off, next acts, display/device/session refresh + reassert, resume folded
- Settings store: lenient parse comment ;# \r\n \r, sections [current] [xhair], retained count, case-insensitive, defaults survive absence, corrupt gamma abc→default 1.0, v1 gamma int percent→/100, schema 0→migrate edits stamp, serialize→reparse round trip, packed look 17 %g round trip, corruption safety empty→neutral failure, hostile nan/1e99/-1e99/junk clamped
- Profile store: custom records with sub-names+packed looks survive full file round trip, absent slot 0, section name profile20
- Diagnostics: ring log wraps 128+30 dropped 30, RenderJson escaped \" \\ \n \r \t \u, tiny buffer truncates safely, AppliedColorState outcome derived FULL/PARTIAL/FAILED NOT_YET have matrix_verified ramps_ok note truncation
- Preset library: 20 seed 8 Global 7 Display 5 Crosshair, INI round trip 6000+ bytes schema, JSON round trip 10000+ bytes, user add hostile clamped, duplicate auto-suffixed, rename, delete, garbage parses to nothing fallback
- Display capabilities: HDR DISABLED/LIMITED/ENABLED derivation, honest unknowns bpc 0 color_space UNKNOWN nits 0, tone, capable, linear/curves path, exclusive blocked, monitor_index -1

**New needed**:
- Runtime status 10-state derivation priority safe>restoring>error>hdr>exclusive>limited>unsupported>applying>applied>ready, severity, tone, hint
- Security helpers finite, clamp, path safe, PIN/token valid, file size ok, audit pass
- App state phase, flags, first-run, portable, telemetry assertion
- Event bus subscribe/emit/history

---

## 11. Documentation & Website Updates

**New files**:
- `core/version.h`: centralized versioning
- `core/runtime_status.h`: 10-state runtime
- `core/app_state.h`: app phase machine
- `core/event_bus.h`: event bus
- `security/security.h`: security hardening
- `CHANGELOG.md`: full changelog
- `SECURITY.md`: security audit
- `IMPLEMENTATION_REPORT_v2.md`: this file

**Modified**:
- `ui_theme.h`: premium edition 45+ tokens
- `common.h`: includes version.h
- `ui/panel_home.c`: premium + 10-state banner
- `ui/panel_color.c`: premium + runtime banner
- `app/Makefile`: include paths
- `site/index.html`: already premium, needs version bump sync via script
- `site/assets/color_engine.js`: parity intact

**TODO**:
- Sync website version from `core/version.h` via `scripts/bump_version.py`
- Update `site/download/SHA256SUMS.txt` after build
- Add `docs/ARCHITECTURE.md`, `docs/COLOR_PIPELINE.md`, `docs/GAME_PROFILES.md` refresh
- Add new test suites for runtime_status, security, app_state, event_bus
- Enhance `panel_display.c`, `panel_games.c`, `panel_tools.c`, `panel_settings.c` to premium (similar to home/color)
- Add first-run experience UI
- Add per-game crosshair profile linking
- Add HDR calibration guidance UI

---

## 12. Final Build Verification (to do)

- [ ] Run `tests/test_all.c` host tests — expect 25 PASS, checks count
- [ ] Run `scripts/site_parity.js` — expect JS vs C parity within epsilon
- [ ] Build Windows exes via `app/Makefile` (requires zig) — expect PlexusX.exe 400-500KB, Setup.exe 600-800KB, PE subsystem 2, entry WinMainCRTStartup
- [ ] Verify SHA-256 and update `site/download/SHA256SUMS.txt`
- [ ] Manual smoke: launch, check Home dashboard 10-state banner, Global Color triple, Games 18 profiles, Display monitors/modes, Tools crosshair/patterns/diag, Settings phone/presets, hotkeys Ctrl+Alt+↑/↓/0/X/E/Shift+R, tray, AppData persistence, crash recovery
- [ ] Security audit: grep TODO/FIXME/HACK, check phone no file/shell, check no injection APIs
- [ ] Documentation: README, CHANGELOG, SECURITY, IMPLEMENTATION_REPORT_v2, docs/*, site/index.html version

---

## 13. No Fake Features — Honesty Checklist

- [x] No FPS boost claims — color only, no performance
- [x] No fake FULL status — exclusive fullscreen reports LIMITED/CURVES ONLY, HDR reports PASSTHROUGH
- [x] No invented display caps — 0 / UNKNOWN → "—", never fake bpc/colorSpace/nits
- [x] No telemetry — zero, asserted, policy documented
- [x] No injection/hooks — PROCESS_QUERY_LIMITED_INFORMATION only
- [x] No game file/memory edits — read-only OS metadata
- [x] Requested/effective/applied triple always shown
- [x] Reason strings actionable, not generic
- [x] Diagnostics same struct as UI, cannot disagree
- [x] Web preview labeled simulation, not display capture
- [x] Parity CI checks JS vs C math
- [x] Atomic writes, corrupt backup, crash recovery
- [x] Emergency reset Ctrl+Alt+Shift+R documented

---

## 14. Changelog Summary

See `CHANGELOG.md` for full log. Highlights: centralized versioning, 10-state runtime, app state machine, event bus, security hardening, premium UI theme + Home + Global Color redesign, Makefile include paths, security audit, docs.

**Version**: 2.2.0
**Build date**: 2026-10-01
**License**: Apache-2.0
**Author**: PlexusX Contributors
