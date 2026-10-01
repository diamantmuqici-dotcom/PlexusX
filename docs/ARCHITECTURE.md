# PlexusX — Architecture

PlexusX is a **display-control application**: it shapes the colour and the display
mode of the Windows desktop through documented, user-mode APIs only. It does not
inject into games, does not read or write game memory, does not hook processes and
does not modify game files. Everything it does is visible to, and reversible by,
the Windows display stack.

This document is the module map: who owns what, and where each kind of state
lives. If two files ever appear to own the same value, that is a bug.

## 1. Layers

```
  ┌───────────────────────────────────────────────────────────────────────┐
  │ presentation        app/src/ui/*.c            (Win32 GDI shell)       │
  │   ui_core.c  panels  …  one widget table, one dispatcher (Ui_Exec)    │
  ├───────────────────────────────────────────────────────────────────────┤
  │ orchestration       app/src/color/color_engine.c        (Eng_*)      │
  │                     app/src/games/game_preset_manager.c (Prof_*)      │
  │                     app/src/display/display_manager.c   (Modes_/Dm_)  │
  │                     app/src/windows/window_manager.c    (Wm_*)        │
  ├───────────────────────────────────────────────────────────────────────┤
  │ state (pure, host-testable headers)                                   │
  │   color_state.h            REQUESTED   (the user's look)              │
  │   color_runtime_state.h    EFFECTIVE   (what the output path can do)  │
  │   applied_color_state.h    APPLIED     (what the hardware confirmed)  │
  │   game_profile.h / game_state.h / game_display_state.h                │
  │   display_state.h          monitors, modes, HDR, presentation, GPU    │
  │   window_state.h           focus / ALT+TAB / reassert policy          │
  │   preset_store.h           unified preset library (4 payload kinds)   │
  │   settings_store.h         INI core: load, validate, migrate, atomic  │
  ├───────────────────────────────────────────────────────────────────────┤
  │ math (shared with the browser preview and the test suites)            │
  │   color/look.h + color/color_math.h                                   │
  ├───────────────────────────────────────────────────────────────────────┤
  │ platform glue       color/color_pipeline.c   (DWM matrix + GPU LUT)   │
  │                     games/game_detector.c    (foreground facts)       │
  │                     crosshair.c, tools.c, phone.c                     │
  └───────────────────────────────────────────────────────────────────────┘
```

`app/src/common.h` is the single interface header: types, ids and the prototypes
of every module. Each function has exactly one declaration site — the module
header that owns it — and `common.h` includes those headers last.

## 2. The three colour states

| state | header | written by | meaning |
|---|---|---|---|
| REQUESTED | `color_state.h` | sliders, presets, phone, hotkeys, profile activation | the look the user asked for |
| EFFECTIVE | `color_runtime_state.h` | derived, never stored | what the current output path can actually deliver (HDR / exclusive fullscreen / missing API) |
| APPLIED | `applied_color_state.h` | `color_pipeline.c` result | what the hardware confirmed, with an outcome of `FULL`, `PARTIAL`, `FAILED` or `NOT_YET` |

There is **one** requested-state instance in the process (`g_cs` inside
`color_engine.c`); everything writes through `Eng_SetLook` / `Eng_Apply`, which
sanitise and bump a revision counter. A failed apply never rewrites the request,
so the UI can always show the truth: requested ≠ applied is a state, not an error
to hide. See `docs/COLOR_PIPELINE.md`.

## 3. UI modules

The Win32 shell is split by product area, not by widget type:

| file | pages |
|---|---|
| `ui/ui_core.c` | DPI, fonts, widget table, painting, input, navigation, status bar, `Ui_Exec` (the only dispatcher), modal helpers, file dialogs |
| `ui/panel_home.c` | HOME: live cards, quick actions, preview |
| `ui/panel_color.c` | DISPLAY ▸ Global Color |
| `ui/panel_display.c` | DISPLAY ▸ Monitors, Resolution, Refresh Rate |
| `ui/panel_games.c` | GAMES ▸ Game Profiles, Active Game, Custom Games |
| `ui/panel_tools.c` | TOOLS ▸ Crosshair, Test Patterns, Diagnostics |
| `ui/panel_settings.c` | REMOTE ▸ Phone Control, SETTINGS, preset library |

Rules the panels follow:

* a panel **builds widgets** and **reads state**; it never keeps a second copy of
  a value and never talks to the display directly for anything else than the
  documented action helpers it is allowed to call (`Modes_ApplySafe` etc.);
* a widget whose value is unknown is simply not created — there are no
  decorative buttons and no invented readouts;
* every panel is rebuilt from live state, so a page can never show a stale value.

`Ui_Exec(id)` is the single action entry point, used by mouse input, the tray
menu, hotkeys and the phone remote.

## 4. Event flow (games, focus, display)

```
WinEvent hook (games/game_detector.c) ──► WM_APP_FOREGROUND (posted to the UI thread)
                                            │
                       Prof_NotifyForeground│(match exe → profile → sub-mode)
                                            ▼
                               ColorState requested (no stacking)
                                            │
                                   Eng_Apply ─┴─► pipeline (DWM matrix + GPU LUT)
                                            │
                                   AppliedColorState + event ring (diagnostics)

WM_DISPLAYCHANGE / WM_ACTIVATEAPP / device reset → WindowManager (Wm_*)
     invalidate cache → reassert the REQUESTED look → one coalesced settle check
```

The UI thread owns all state changes; the phone server posts messages
(`WM_APP_LOOK`, `WM_APP_PROFILE`) instead of touching the engine from its own
thread.

## 5. Persistence

| file | owner | contents |
|---|---|---|
| `config.ini` | `settings_store.c` | look, UI prefs, hotkey-adjacent settings, engine policy |
| `profiles.ini` | `game_preset_manager.c` | the game library (per-profile looks, aliases, automation) |
| `presets.ini` | `preset_store.c` | the unified preset library (Global / Game / Display / Crosshair) |
| `ramps.dat`, `dirty.flg` | `color_pipeline.c` | crash recovery: original GPU ramps + "an apply was in flight" marker |
| `display.pending` | `display_manager.c` | the mode a confirm-or-rollback change is leaving, so a crash during the countdown reverts on the next start |

All four are versioned. Corrupt input is moved aside (`.corrupt-N`) and defaults
are rebuilt; a partially valid file is clamped field by field, never rejected
wholesale.

## 6. Build, test, ship

```
cd app && make          # zig cc → x86_64-windows-gnu, GUI subsystem, PE verified
gcc tests/test_all.c …  # 25 suites: math, store, presets, profiles, capabilities, diagnostics
sh tests/host/run.sh    # the real engine against a mocked Win32 stack (ASan/UBSan)
node scripts/site_parity.js   # browser preview must match the C math bit-for-bit
```

`site/` is not a mock-up of the application: it is a preview that imports the
same math through `site/assets/color_engine.js`, and the parity test above is
what keeps the two honest. See `.github/workflows/build-windows-exe.yml`.
