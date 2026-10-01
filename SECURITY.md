# PlexusX Security Policy & Audit

## Summary
PlexusX is a **user-mode, anti-cheat safe** display utility. It operates exclusively through legitimate Windows APIs (`MagSetFullscreenColorEffect`, `SetDeviceGammaRamp`, `EnumDisplaySettings`, `DXGI 1.6`) and never injects code, hooks games, or touches game memory/files.

## Trust Boundaries

### 1. File Parsing (INI, JSON, ramps.dat)
- **Threat**: corrupt file → crash, code exec, silent data loss, propagation
- **Mitigations**:
  - `PxIni` parser is total: every line length-checked, malformed counted (`bad_lines`), skipped, duplicates FIRST wins
  - Size caps: config 192KB, presets 256KB, JSON 256KB, ramps.dat 4 + 8*1568 bytes
  - Corrupt non-empty file → moved aside as `*.corrupt-N` (up to 5), never deleted, defaults loaded
  - Atomic writes: tmp file + `FlushFileBuffers` + `MoveFileExW(REPLACE_EXISTING|WRITE_THROUGH)` — crash never truncates
  - `ramps.dat` validation: count, name printable + terminated, ramp monotonic + non-degenerate span + non-zero. One bad record poisons whole file → identity fallback
  - Numeric parsing: `strtod`/`strtol` output finite-checked (`v==v && v<=1e30 && v>=-1e30`), range-clamped, missing → default. `cm_sanitize_look` total
  - Path traversal blocked: rejects `..`, absolute `/\`, `:`, control chars `<0x20` in all user-supplied paths
- **Verified by**: `test_settings_store`, `test_profile_store`, `test_ramps_file_validation`, `test_preset_library`

### 2. Network (LAN Phone Control, port 8777)
- **Threat**: unauthorized LAN access, brute force, token leak, file/shell access
- **Mitigations**:
  - Binds LAN interface only, never internet
  - 4-digit PIN 1000-9999 single-use, trades for 32-hex-char token (128-bit) via `xorshift128+` RNG seeded `QueryPerformanceCounter` + `GetTickCount`
  - 5 wrong attempts → 30s lockout
  - Max 8 sessions LRU eviction, TTL 8h idle
  - `Cache-Control: no-store`, CORS headers
  - Tiny HTTP parser: method + path + query only, no file serving, no shell, no `..`
  - Command surface limited to: saturation, vibrance, brightness, contrast, gamma, temperature, game profile selection, engine on/off, crosshair toggle. Every value clamped to UI safe ranges
  - No file/shell access, no display-mode changes via phone
- **Verified by**: manual audit of `phone.c` (633 lines), `security.h` `px_sec_pin_valid`, `px_sec_token_valid`

### 3. Process (Game Detection)
- **Threat**: anti-cheat flag, game memory access, injection
- **Mitigations**:
  - `OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION)` only — read-only OS metadata
  - `QueryFullProcessImageNameW` — base name only, paths stripped, lowercased
  - `GetForegroundWindow` + window rect vs monitor rect + `WS_CAPTION` check for presentation classification (`WINDOWED`, `COMPOSITED_COVER`, `FULLSCREEN_SURFACE`)
  - Event-driven via `SetWinEventHook(EVENT_SYSTEM_FOREGROUND)`, not polling
  - No DLL injection, no hooks into game processes, no memory read/write, no file modification
  - Alias table covers launchers (FortniteLauncher, VALORANT.exe, r5apex_dx12, ModernWarfare, Minecraft.Windows, GTA5_Enhanced, TslGame_BE, ArcRaiders shipping, Discovery shipping)
- **Verified by**: `test_game_state_machine`, architecture review

### 4. Persistence
- **Threat**: crash leaves display in bad state, data loss, corruption propagation
- **Mitigations**:
  - `ramps.dat` backup of original ramps captured at `PxPipe_Init`, repaired via `cm_ramp_make_monotonic`, validated via `cm_ramp_is_valid`
  - Dirty flag `dirty.flg` — start restores from `ramps.dat` or identity if dirty
  - `display.pending` crash-safe file for confirm-or-rollback: writes pending before `ChangeDisplaySettingsExW`, arms 15s countdown, `Modes_RecoverPendingFromDisk` on start
  - `crash.log` written on unhandled exception after restoring display
  - Emergency safe reset `Ctrl+Alt+Shift+R`: closes patterns, bypasses engine, `Eng_Reset()`, restores ramps
- **Verified by**: `test_ramp_write_planning`, `test_ramps_file_validation`

### 5. Color Pipeline
- **Threat**: NaN/Inf/overflow → black screen, driver rejection, DWM crash
- **Mitigations**:
  - `MagColorEffect` is `float[5][5]` 100 bytes row-major, W' column `[0 0 0 0 1]` enforced. Old `float[4][5]` 80-byte bug would read 20 bytes stack garbage
  - Translation lives only in row 4 `m[4][0..2]`, never column 4 (old bug blanked screen)
  - `cm_sanitize`: NaN/Inf → identity, 3x3 block scaled toward identity (gray stays gray), translation independently clamped to `[-4,4]`, W' repaired to `[0 0 0 0 1]`
  - `WEIGHT_LIMIT 4.0`, `chromaEff` `maxEff/minEff`, hue matrix transpose for row vectors, `fit-toward-identity` k `0.9975`
  - Ramp: `gamma+shadows toe (1-x)^2*0.35 + highlights shoulder x^2*0.30 + clarity 0.5*(1-cos(xπ))-x *0.25`, monotonic enforced
  - 300% saturation: `M[i][j] = 3*delta + w_i*(1-3)` with Rec.709 `LUM_R 0.2126729` etc, luminance preserved, gray fixed point
- **Verified by**: `test_matrix_layout`, `test_w_divisor_safety`, `test_translation_row`, `test_saturation_matrix`, `test_hue_rotation`, `test_high_vibrance_neutrals`, `test_ramp_monotonicity`, `test_shipped_presets`

## Security Audit Checklist (current)

| Check | Status | Evidence |
|-------|--------|----------|
| INI parse safe | ✅ | `settings_store.h` total parser, `bad_lines`, size caps |
| JSON parse safe | ✅ | `game_profile.h` `PxJson_*` bounded, `preset_store.h` brace counting, tolerant |
| ramps.dat safe | ✅ | `color_math.h` `cm_parse_ramps_blob` validates whole |
| Phone no file access | ✅ | `phone.c` no file APIs in request handling |
| Phone no shell | ✅ | `phone.c` no `CreateProcess`, `system`, `ShellExecute` in remote path |
| Token entropy | ✅ | `xorshift128+` 128-bit, 32 hex chars |
| Lockout | ✅ | 5 tries → 30s |
| TTL | ✅ | 8h |
| Atomic writes | ✅ | tmp + `FlushFileBuffers` + `MoveFileExW` |
| Corrupt backup | ✅ | `*.corrupt-N` up to 5, never delete |
| No injection | ✅ | no `CreateRemoteThread`, `WriteProcessMemory`, `SetWindowsHookEx` into games |
| No game memory | ✅ | only `PROCESS_QUERY_LIMITED_INFORMATION` |
| Input sanitized | ✅ | `cm_sanitize_look`, `PxPreset_Sanitize`, finite checks |
| Path traversal blocked | ✅ | `security.h` `px_sec_path_is_safe` |

## Reporting
Report security issues via GitHub Issues (private) or email to maintainers. No bounty program, but all reports are triaged within 7 days.

## Telemetry
**Zero**. No analytics, no tracking, no network calls except LAN phone control (port 8777, local network only, opt-in). All settings stored locally in `%APPDATA%\PlexusX` or portable folder. Policy asserted by `PxAppState_HasTelemetry() == 0`.

## Dependencies
- **Windows**: `user32`, `gdi32`, `shell32`, `advapi32`, `ws2_32`, `comctl32`, `comdlg32`, `shlwapi`, `dwmapi`, `wtsapi32` — all system, no third-party
- **Build**: `zig cc` cross-compiler, no runtime deps
- **Website**: `color_engine.js` exact `Math.fround` port of `color_math.h`, parity-checked in CI via `scripts/site_parity.js`
