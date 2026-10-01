# PlexusX 2.2 — Implementation Report

**Scope delivered:** PlexusX is now a production-shaped Windows display-control
application: a native Win32/GDI application that detects games, resolves a
profile, computes the *effective* colour state the output path can actually
carry, applies it through legitimate user-mode display APIs, verifies what was
applied, and reports the result — never claiming more than the hardware
confirmed.

---

## 1. Systems changed

### Colour engine (`app/src/color/`)
| Piece | Role |
|---|---|
| `color_math.h` | The single colour kernel: matrix assembly, gamma LUT, per-pixel preview. Shared by the app, the unit tests and the web parity test. |
| `color_engine.{h,c}` | Controller: requested state, revision/generation counters, apply/verify, invalidation, resync, target monitor, backup/restore, diagnostics log ring. |
| `color_pipeline.{h,c}` | The only code that touches the display: DWM magnification matrix + per-display gamma ramps, plan diffing, crash-safe `ramps.dat` / `dirty.flg`. |
| `color_runtime_state.h` (new) | Effective-state derivation and the six statuses with reasons. |
| `color_state.h`, `color_transform.h`, `applied_color_state.h`, `look.h` | Requested state, transform plan, applied-state record, parameter block. |

### Display (`app/src/display/`)
* `display_manager.c`: mode/monitor enumeration, DXGI 1.6 colour facts, HDR
  derivation, confirm-or-rollback apply, and **new** crash-safe
  `display.pending` recovery so an unconfirmed mode change cannot outlive the
  session that made it.
* `display_capabilities.h` (new) + `display_state.h`: pure classification of the
  presentation path (windowed / borderless / exclusive surface), HDR verdicts and
  mode picking — unit-tested on the host.

### Games (`app/src/games/`)
* `game_profile.{h,c}` (new): the profile model and its algebra — executable
  normalisation, alias matching, duplication, renaming, clamped JSON
  import/export. Free of `<windows.h>` so it is testable.
* `game_preset_manager.c`: the live 18-title library, detection, delayed
  auto-apply, desktop restore, persistence.
* `game_state.h`, `game_display_state.h`: the launch/switch/exit state machine and
  the game-output truth (FULL / LIMITED / PASSTHROUGH).

### UI (`app/src/ui/` — split out of the 2016-line `ui.c`, which is deleted)
`ui_core.c` (shell: DPI, fonts, widget table, painting, input, grouped sidebar,
status strip, `Ui_Exec`, file/input dialogs) plus
`panel_home.c`, `panel_color.c`, `panel_display.c`, `panel_games.c`,
`panel_tools.c`, `panel_settings.c`. Layout follows the brief: HOME · DISPLAY
(Global Color / Monitors / Resolution / Refresh Rate) · GAMES (Game Profiles /
Active Game / Custom Games) · TOOLS (Crosshair / Test Patterns / Diagnostics) ·
REMOTE (Phone Control) · SETTINGS, with a bottom status strip carrying COLOR
ENGINE, CURRENT GAME, CURRENT PROFILE, DISPLAY and VERSION.

### Presets, diagnostics, settings, remote
* `presets/preset_store.{h,c}` (new): one versioned library for Global / Game /
  Display / Crosshair presets (INI + JSON), seeded once with 20 presets, with
  CRUD, rename, duplicate, export/import and corruption fallback.
* `diagnostics/diagnostics_report.h` (new): the single report builder feeding
  screen, clipboard and exported file, plus the ring event log.
* `settings/settings_store.{h,c}`: lenient INI store with v1 migration and
  per-field clamping.
* `phone.c`: LAN remote hardened (single-use PIN → 128-bit session token, bad-try
  lockout, idle expiry, bounded sessions, tokened endpoints only) — colour,
  profile and engine commands, no shell, no filesystem.

---

## 2. Files

**Created:** `app/src/ui/` (7 files), `app/src/color/color_runtime_state.h`,
`app/src/display/display_capabilities.h`, `app/src/games/game_profile.{h,c}`,
`app/src/presets/preset_store.{h,c}`, `app/src/diagnostics/diagnostics_report.h`,
`docs/ARCHITECTURE.md`, `docs/COLOR_PIPELINE.md`, `docs/GAME_PROFILES.md`,
this report.
**Deleted:** `app/src/ui.c` (monolith).
**Modified:** `common.h`, `color_engine.{h,c}`, `display_manager.c`,
`game_preset_manager.c`, `main.c`, `phone.c`, `tools.c`, `settings_store.h`,
`app/Makefile`, `README.md`, `.github/workflows/build-windows-exe.yml`,
`tests/*`, `site/download/*` (binaries + `SHA256SUMS.txt`).

**Deliberately *not* created:** `color_controller.*`, `color_apply.*`,
`game_runtime.*`. `color_engine.c` is the controller, `color_pipeline.c` is the
applier and `game_preset_manager.c` + `game_state.h` + `game_display_state.h` are
the game runtime; splitting them further would have duplicated a state machine
that already works rather than adding value.

---

## 3. Notable fixes

* Status honesty: `ACTIVE` now requires an enabled engine, a matching revision and
  a verified `FULL` outcome; HDR outputs report `PASSTHROUGH` with an identity
  effective look; exclusive fullscreen reports `LIMITED` with a reason instead of
  pretending the chroma half landed.
* Redundant applies: revision counters + plan diffing mean identical state is
  never rewritten (slider return, ALT+TAB re-assert, repeated profile activation).
* Crash safety: `dirty.flg` / `ramps.dat` restore on start; **new**
  `display.pending` restores a display mode left unconfirmed by a killed session.
* Look-edit contract: copies flow through `Eng_SetLook`, so no-op edits do not
  re-apply and the authoritative look is never mutated in place.
* Minimize-to-tray, engine startup policy and phone auto-start now honour the
  saved configuration; delay buttons drive the real switching delay.
* Many correctness bugs found by tests/build: `color_runtime_state.h` historic
  derivation bug, missing `game_profile` implementations and a `Profile` typedef
  collision, an implicit `eng_apply_done`, uninitialised `PxIni` in
  `PxPre_Parse`, `#endif` without `#if`, `wchar_t`-to-`%s` diagnostic warnings,
  `PAT_COUNT` misuse, host-harness linker gaps.

---

## 4. Verification (all re-run on the final tree)

| Gate | Result |
|---|---|
| `cd app && make clean && make` (zig `cc`, `-Wall`, x64 GUI PE) | clean — 0 errors, 0 warnings |
| `python3 app/tools/verify_pe.py` | both binaries: x64, subsystem 2 (GUI), GUI entry point — **PASS** |
| `gcc -Wall -Wextra -Werror … tests/test_all.c` | **25 suites / 227,986 checks — PASS** |
| `sh tests/host/run.sh` (real engine + pipeline, ASan + UBSan) | **21 scenarios / 144 checks — PASS** (incl. HDR `PASSTHROUGH`, exclusive `LIMITED`) |
| `node scripts/site_parity.js --golden /tmp/parity.json` | **15 looks, 1947 values, max |C−JS| = 1.0e+0 ≤ tol 3e-4 (ramps exact)** |
| `cmp /tmp/parity.json site/parity.json` | byte-identical |
| SHA-256 | `PlexusX.exe 0eaa9624…f998`, `PlexusX-Setup.exe a88f46f8…7861` |

---

## 5. Remaining limitations (stated, not hidden)

* **Exclusive fullscreen.** A true exclusive swap chain bypasses DWM, so only the
  gamma-LUT half reaches it; the app reports `LIMITED` and says why. No user-mode
  API can do better without touching the game process, which PlexusX will not do.
* **HDR.** When an output is in an HDR space the engine passes through (Windows
  owns the tone mapping) and reports `PASSTHROUGH`. HDR is enumerated and
  explained, never faked; toggling HDR itself opens the Windows settings page
  because there is no supported user-mode API to switch it programmatically.
* **Per-monitor colour.** The DWM matrix is global; per-monitor targeting applies
  to gamma-ramp writes and display-mode changes, not to an independent matrix per
  monitor.
* **Detection** is foreground-executable based (aliases cover launcher/shipping
  variants). A game whose foreground window belongs to a launcher must be matched
  through its executable list.
* Binaries are unsigned (checksums only), the UI is English-only, and the event
  log is a fixed 128-entry ring.
* UI drawing code is compiled by the Windows toolchain in CI but has no
  host-side rendering test; the pure logic behind every panel is unit-tested.
