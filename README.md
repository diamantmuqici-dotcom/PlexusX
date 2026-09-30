# ChromaX

**Three times the colour. Every game. €0.**

A free, open, portable monitor-colour engine for Windows 10/11 — saturation up to
**300%**, scene presets, per-game auto-switching, a crosshair designer, resolution
& refresh control and LAN phone control. One 260 KB exe. No account, no card, no
premium tier, no telemetry.

> Changes what your monitor shows. Never touches the game: no memory reads, no file
> edits, no DLL injection, no driver, no admin.

## Repository layout

| Path        | What it is                                                        |
|-------------|-------------------------------------------------------------------|
| `app/`      | The Windows app — plain C, Win32, hand-drawn dark UI.              |
| `site/`     | The marketing site (static HTML/CSS/JS) + the downloadable exe.    |
| `app/Makefile` | Cross-compiles the exe with `zig cc` (`x86_64-windows-gnu`).   |
| `LICENSE`   | Apache-2.0.                                                       |

## Building the exe

Requires Python 3 and the `ziglang` PyPI package (zig bundles the mingw headers and
linker — no Visual Studio needed):

```bash
pip install ziglang
cd app && make          # writes site/download/ChromaX.exe
```

The linker emits a CONSOLE-subsystem PE; `tools/fixsub.py` flips it to GUI so no
console window appears at runtime.

### What the app actually does

* **Colour matrix** — saturation / brightness / contrast / temperature / hue are
  composed into one 4×5 matrix and applied through `magnification.dll`
  (`MagSetFullscreenColorEffect`), the same OS layer the Windows Magnifier colour
  filter uses. It rides over the whole screen, including games.
* **GPU gamma** — a second stage writes per-monitor gamma ramps with
  `SetDeviceGammaRamp`. Originals are saved to `%APPDATA%\ChromaX\ramps.dat`
  *before* anything changes; a dirty flag makes the next launch restore them even
  after a crash.
* **Foreground watch** — 1 s timer reads the front-most process
  (`QueryFullProcessImageNameW`) and swaps the look per profile.
* **Crosshair** — click-through layered window (`UpdateLayeredWindow`, 4×
  supersampled for anti-aliasing), six shapes, live colours.
* **Modes** — `EnumDisplaySettingsW` / `ChangeDisplaySettingsExW`, including
  stretched 4:3 presets; Windows shows its own "keep changes?" prompt.
* **Phone control** — a tiny winsock HTTP server on port 8777 (only while the
  toggle is on) serving a dark control page to your LAN.
* **Hotkeys** — `Ctrl+Alt+↑↓` saturation, `0` reset, `X` crosshair, `E` look.

## Running the site

```bash
python3 -m http.server 8080 --directory site
```

Everything is static — no build step, no framework. The before/after slider and the
game gallery are browser-side previews of the exact maths the app runs.

## Verifying the download

```bash
certutil -hashfile ChromaX.exe SHA256     # Windows
sha256sum ChromaX.exe                     # anywhere else
```

The hash is printed on the site's download card and in `site/download/`.

## Why no premium?

Because everything works. The feature split other tools hide behind accounts and
cards is simply not present in this codebase — there is no licence check to bypass
and no server to call.
