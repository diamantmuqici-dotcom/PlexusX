# PlexusX — Game Profiles

A game profile is the complete display recipe for one title: how to recognise it,
what to show, and what to do when it starts and stops. This document describes the
model, the matching rules, the automation and the import/export format.

## 1. Model (`app/src/games/game_profile.h`)

```
Profile
├── identity      name, tag, favorite, enabled, is_custom
├── matching      exe (primary base name) + up to 4 extra exe aliases
│                 + optional full executable path
├── look          up to 12 named sub-modes, each a complete Look
├── display       optional target resolution / refresh / HDR preference,
│                 optional monitor target (-1 = all/primary)
├── automation    auto_apply, auto_restore, delay_ms, apply_display
└── history       last_activated, apply_count
```

The model is a pure header (no `<windows.h>`), so matching, duplication,
renaming and JSON round-trips are unit-tested in `tests/test_all.c`.
`game_preset_manager.c` owns the live table and its persistence.

## 2. Matching

1. The foreground executable's **base name** is extracted and normalised
   (lower-case, no path, no quotes): `C:\Games\Rust\RustClient.exe` → `rustclient.exe`.
2. Profiles are matched on the primary name, then on every alias, case-insensitively.
   A profile with `enabled = 0` is skipped entirely.
3. Aliases exist because most modern titles ship several executable names:

| title | primary executable | extra executable name PlexusX also matches |
|---|---|---|
| Fortnite | FortniteClient-Win64-Shipping.exe | FortniteLauncher.exe |
| Valorant | VALORANT-Win64-Shipping.exe | VALORANT.exe |
| Apex Legends | r5apex.exe | r5apex_dx12.exe |
| Call of Duty / Warzone | cod.exe | ModernWarfare.exe |
| Minecraft | javaw.exe | Minecraft.Windows.exe |
| GTA V | GTA5.exe | GTA5_Enhanced.exe |
| PUBG | TslGame.exe | TslGame_BE.exe |
| ARC Raiders | ArcRaiders.exe | ArcRaiders-Win64-Shipping.exe |
| THE FINALS | Discovery.exe | Discovery-Win64-Shipping.exe |

Only names that really are the same game process are listed (matching is already
case-insensitive, so `RUST.EXE` needs no alias). A user can add more names per
profile from the library inspector.

The built-in library covers 18 titles: Rust, Counter-Strike 2, Fortnite,
Valorant, Apex Legends, PUBG: BATTLEGROUNDS, Escape From Tarkov, Call of Duty:
Warzone, Overwatch 2, Rainbow Six Siege, Minecraft, Grand Theft Auto V, DayZ,
THE FINALS, Helldivers 2, Battlefield, Destiny 2 and ARC Raiders — each with
2–4 named sub-modes (Competitive, Night Ops, Visual, …).

Custom games are ordinary profiles with `is_custom = 1`; they are created,
edited, exported and deleted through the same API (`Prof_Create`,
`Prof_AddExeName`, `Prof_SetExePath`, `Prof_Delete`, …).

## 3. Automation

```
foreground change ──► detector (event-driven, no polling of the game)
                        │
                        ├─ matched profile, enabled, auto_apply
                        │     → wait delay_ms (default 0, used to let a game
                        │       finish its own startup mode changes)
                        │     → load sub-mode look as the REQUESTED look
                        │       (never stacked on the previous one)
                        │     → if apply_display: request the display mode
                        │       through the confirm-or-rollback path
                        │
                        └─ game exited / focus left
                              → auto_restore: restore the global look
```

* **No stacking.** Activating a profile replaces the requested look; it does not
  multiply it with whatever was active before.
* **No re-apply loops.** The engine applies once per revision change; detection
  events that match the currently active profile are no-ops.
* **ALT+TAB is not an exit.** Window focus loss schedules the WindowManager's
  coalesced settle check, which re-asserts the same requested look if DWM dropped
  the effect — it does not switch profiles.
* Every transition is written to the event ring and is visible on the
  Active Game page and in the diagnostics report.

## 4. Persistence and import/export

* `profiles.ini` (via `settings_store.h`) is the live store: `[profileN]` sections
  with the look packed by `px_look_pack`, plus the alias list and automation flags.
  A corrupt file is moved to `profiles.ini.corrupt-N` and rebuilt from the
  built-ins; per-field damage is clamped instead of rejected.
* JSON is the interchange format:

```json
{ "plexusx_profile": 1,
  "name": "Rust", "exe": "RustClient.exe",
  "aliases": ["Rust.exe"],
  "sub_modes": [ { "name": "Competitive", "look": { "sat": 165, "vibrance": 130, ... } } ],
  "auto_apply": 1, "auto_restore": 1, "delay_ms": 0,
  "monitor_idx": -1 }
```

* `Prof_ExportFile` / `Prof_ImportFile` move a single profile;
  `Prof_ExportLibrary` / `Prof_ImportLibrary` move the whole library.
  The reader is deliberately lenient: every numeric field is range-clamped and
  every string is bounded, so a hand-edited or hostile file can never produce a
  NaN look or an out-of-range monitor index.
* Renaming a profile marks it user-owned (`is_custom = 1`) so a later
  "restore built-ins" cannot silently overwrite the user's name.

## 5. What a profile can and cannot do

Can: choose the colour look, choose a monitor target for display changes, request
a resolution/refresh (through the confirm-and-rollback path), decide whether to
apply on launch, whether to restore the desktop look on exit, and how long to wait
first.

Cannot: touch the game process, inject anything, change game settings, or claim
that a colour change reached an exclusive-fullscreen surface when it did not —
that case is reported as `LIMITED` on the Active Game page and in diagnostics.
