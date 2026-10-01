# PlexusX — Colour Pipeline

This is the contract behind every colour control in the application: what is
requested, what the output path can carry, what the hardware confirmed, and what
the UI is therefore allowed to claim.

## 1. Requested → Effective → Applied

```
   sliders / presets / phone / hotkeys / profile activation
                     │
                     ▼
             ColorState.requested          (look.h, sanitised + clamped)
                     │  revision++
                     ▼
   effective derivation (color_runtime_state.h)  ← output facts:
                     │                              magnification available?
                     │                              ramps accepted?
                     │                              HDR active?  exclusive fullscreen?
                     │                              engine enabled? apply in flight?
                     ▼
              PxTransformPlan                  (color_transform.h)
                     │
                     ▼
        ColorPipeline (color_pipeline.c)
          · DWM magnification colour matrix  — linear stages
          · SetDeviceGammaRamp               — non-linear tone curves
                     │
                     ▼
            AppliedColorState                  (outcome FULL / PARTIAL / FAILED)
```

* **Never a second truth.** `Eng_SetLook` is the only write path to the requested
  state; it sanitises (`cm_sanitize_look`: finite, in-range, NaN-safe) and bumps
  the revision only when something actually changed.
* **A failed apply never rewrites the request.** The user's sliders stay where
  they were put; the status simply reports `FAILED` / `LIMITED`.
* **Redundancy is skipped by revision.** `PxPlan_Compute` + the pipeline cache
  compare the new plan against the applied one, so dragging a slider back to the
  value the hardware already has performs no writes. Re-asserting an unchanged
  look after ALT+TAB costs a readback, not a write storm.

## 2. The two paths, and why there are two

| path | API | carries | cannot carry |
|---|---|---|---|
| linear | `MagSetFullscreenColorEffect` (Windows Magnification API, DWM) | saturation, vibrance, brightness, contrast, temperature, tint, R/G/B gain, hue, black/white point | any tone curve that needs a per-channel LUT |
| non-linear | `SetDeviceGammaRamp` (GPU gamma LUT, scanout side) | gamma, shadows, highlights, clarity, black/white roll-off | content inside an exclusive-fullscreen surface that bypasses DWM |

The two are complementary, and both are legitimate user-mode APIs. The pipeline
sends what each half can carry and reports the result honestly. If only one half
is usable, the status is `LIMITED`, never `ACTIVE`.

## 3. The six statuses

| status | when |
|---|---|
| `DISABLED` | the user bypassed the engine — output is identity |
| `APPLYING` | an edit is in flight (slider drag) or a re-assert is pending |
| `ACTIVE` | engine enabled, apply confirmed `FULL`, requested == applied, and a linear adjustment is not blocked by the output path |
| `LIMITED` | the path can only carry part of the look (exclusive fullscreen → curves only; partial hardware result) |
| `FAILED` | the apply failed; the display is untouched |
| `PASSTHROUGH` | the output is HDR: Windows owns the tone mapping, PlexusX writes nothing to it |

`ACTIVE` is deliberately the strictest state: it requires a matching revision, a
`FULL` outcome **and** a usable path. Every status carries a one-line, actionable
reason string that the UI shows verbatim (status bar, cards, diagnostics, phone).

## 4. HDR

HDR is derived from what the OS reports (`DXGI_OUTPUT_DESC1` via
`display/display_manager.c`), never guessed:

| state | meaning | what PlexusX does |
|---|---|---|
| `HDR DISABLED` | SDR output | the full pipeline applies |
| `HDR ENABLED` | the output runs an HDR transfer function | writes nothing — `PASSTHROUGH` |
| `HDR LIMITED` | the panel is HDR-capable but Windows is in SDR | the full pipeline applies |
| `HDR PASSTHROUGH` | the OS tone-mapper owns the output | writes nothing |

The rule is simple and absolute: **when an output is HDR, the engine reports
`PASSTHROUGH` and the effective look is identity on that output.** No fake HDR
support, no "HDR enhancement" claim; the limitation is explained in the UI
(`display_capabilities.h` holds the per-state text).

## 5. Exclusive fullscreen

A fullscreen surface can bypass DWM composition. The foreground window's
presentation mode is classified from real window geometry
(`PX_PRES_WINDOWED` / `PX_PRES_COMPOSITED_COVER` / `PX_PRES_FULLSCREEN_SURFACE`):

* windowed and borderless windows are composited → the linear matrix reaches them;
* an exclusive fullscreen surface is not → the effective look is the **curves-only
  look** (`PxEff_CurvesOnlyLook`), the status is `LIMITED`, and the reason says so.

That is a limitation of the platform, not a bug, and the UI states it instead of
pretending the chroma part landed.

## 6. Safety

* **Clamping.** Every value is clamped in the UI, again in `cm_sanitize_look`, and
  the matrix itself is bounded (`[-4, 4]`, finite, column 4 == `[0 0 0 0 1]`) by
  `cm_sanitize` before it is handed to DWM.
* **Crash recovery.** Before the first write, the pipeline stores the original
  ramps in `ramps.dat` and raises `dirty.flg`; a crash while the flag is up means
  the next start restores the originals instead of the crashed state.
* **Emergency reset.** `Ctrl+Alt+Shift+R` (and the tray entry, and the Settings
  page) closes test patterns, bypasses the engine and restores the original ramps.
  It never deletes profiles or presets.
* **A display mode survives a dead process.** The confirm-or-rollback path
  writes the mode it is leaving to `display.pending` before the change reaches
  the driver and removes it on confirm/rollback; if PlexusX is killed during the
  countdown, the next start restores that mode and says so.
* **Never a silent success.** The matrix is read back with
  `MagGetFullscreenColorEffect` when the OS supports it; if DWM dropped the effect
  (the classic ALT+TAB symptom) the outcome is `PARTIAL`, not `FULL`.

## 7. Where the math lives

`color/look.h` is the parameter block; `color/color_math.h` is the kernel:

* `cm_build_effect` assembles the 5×5 matrix from the linear stages;
* `cm_calc_ramp` builds the monotonic 3×256 LUT from the tone curves;
* `cm_apply_pixel` applies both to an sRGB pixel — the UI preview, the unit tests
  and `site/assets/color_engine.js` all call the same math, which is what the
  parity test (`node scripts/site_parity.js --golden`) enforces.

Because the preview runs the same kernel as the pipeline, a preview can never
advertise something the display will not do — and when the status is
`PASSTHROUGH` or `DISABLED`, the preview's "after" half shows identity, because
that is what the display really gets.
