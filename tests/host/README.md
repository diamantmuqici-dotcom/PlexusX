# Engine host scenario tests

`tests/test_all.c` unit-tests the colour math (`app/src/color_math.h`).  These tests cover the other half of
the display pipeline: the Windows glue in **`app/src/engine.c`** that decides *when* the hardware is touched.

The real `engine.c` is compiled unchanged against a tiny fake `windows.h` (`shim/`) and mocked display,
Magnification and file APIs (`host_mock.c`), under AddressSanitizer + UBSan.  The scenarios then count
what would have reached the hardware:

* clean start, linear slider drags, curves, restore-to-original (`SetDeviceGammaRamp` call counts)
* the matrix handed to `MagSetFullscreenColorEffect` (finite, in `[-4, 4]`, column 4 == `[0 0 0 0 1]`)
* crash recovery from `ramps.dat` (valid, corrupted in seven ways, missing) and a crash *during* recovery
* `Eng_Reset` (forced), `Eng_Resync`, monitor targeting, bypass, failing drivers, NaN/Inf looks
* requested vs applied looks: `Eng_Invalidate` / `Eng_Reassert` must not clobber the user's sliders (ALT+TAB)

```sh
sh tests/host/run.sh        # needs gcc or clang (CC=clang sh tests/host/run.sh) on Linux/macOS
```

If `engine.c` starts using another Win32 function, add a declaration to `shim/windows.h` and a mock to
`host_mock.c`.
