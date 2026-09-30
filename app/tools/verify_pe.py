#!/usr/bin/env python3
"""Verify that PlexusX executables are genuine GUI-subsystem x64 images.

    verify_pe.py [--strict] FILE [FILE ...]

For every file this checks
  1. it is a PE32+ image for AMD64,
  2. the PE Subsystem is IMAGE_SUBSYSTEM_WINDOWS_GUI (2),
  3. the entry point is the *GUI* CRT start-up (WinMainCRTStartup, which stores
     __mingw_app_type = 1) and not the console one (mainCRTStartup, which stores 0).

Check 3 is what catches the classic mistake of flipping the subsystem byte of a
console-linked binary after the fact: such an image claims to be GUI but still
runs the console start-up code and aborts at launch.  The subsystem must come
from the linker (-Wl,--subsystem,windows), never from patching.

Only the standard library is used, so this runs anywhere `make` does.
Exit status: 0 = all files pass, 1 = at least one failure.
With --strict an entry stub that cannot be recognised also fails; without it a
warning is printed (a different compiler version may emit different code).
"""
import hashlib
import os
import struct
import sys

IMAGE_FILE_MACHINE_AMD64 = 0x8664
SUBSYSTEMS = {1: "NATIVE", 2: "WINDOWS_GUI", 3: "WINDOWS_CUI", 9: "WINDOWS_CE_GUI"}


class PeError(Exception):
    pass


def parse_pe(data):
    if len(data) < 0x40 or data[:2] != b"MZ":
        raise PeError("not a PE file (missing MZ header)")
    pe = struct.unpack_from("<I", data, 0x3C)[0]
    if pe + 24 > len(data) or data[pe:pe + 4] != b"PE\0\0":
        raise PeError("not a PE file (missing PE signature)")

    machine, nsections, _ts, _symptr, _nsyms, opt_size, _chars = struct.unpack_from("<HHIIIHH", data, pe + 4)
    opt = pe + 24
    magic = struct.unpack_from("<H", data, opt)[0]
    if magic != 0x20B:
        raise PeError("not a PE32+ (64-bit) image (optional header magic 0x%X)" % magic)

    entry_rva = struct.unpack_from("<I", data, opt + 16)[0]
    subsystem = struct.unpack_from("<H", data, opt + 68)[0]

    sections = []
    sec = opt + opt_size
    for i in range(nsections):
        name, vsize, vaddr, rawsize, rawptr = struct.unpack_from("<8sIIII", data, sec + i * 40)
        sections.append((vaddr, max(vsize, rawsize), rawptr, rawsize))

    return {
        "machine": machine,
        "subsystem": subsystem,
        "entry_rva": entry_rva,
        "sections": sections,
    }


def rva_to_offset(info, rva):
    for vaddr, vsize, rawptr, rawsize in info["sections"]:
        if vaddr <= rva < vaddr + vsize:
            off = rawptr + (rva - vaddr)
            return off if (rva - vaddr) < rawsize else None
    return None


def entry_app_type(data, info):
    """Returns the value the entry stub stores into __mingw_app_type (1 = GUI, 0 = console),
    or None when the stub is not recognised.

    Both CRT entry points start with
        mov rax, [rip+disp32]        48 8B 05 <disp32>     ; &__mingw_app_type
        mov dword ptr [rax], imm32   C7 00 <imm32>         ; 1 for WinMainCRTStartup, 0 for mainCRTStartup
    """
    off = rva_to_offset(info, info["entry_rva"])
    if off is None:
        return None
    window = data[off:off + 0x60]
    for i in range(len(window) - 12):
        if window[i:i + 3] == b"\x48\x8b\x05" and window[i + 7:i + 9] == b"\xc7\x00":
            imm = struct.unpack_from("<I", window, i + 9)[0]
            if imm in (0, 1):
                return imm
    return None


def verify(path, strict):
    ok = True
    try:
        with open(path, "rb") as f:
            data = f.read()
        info = parse_pe(data)
    except (OSError, PeError) as e:
        print("FAIL  %s: %s" % (path, e))
        return False

    sha = hashlib.sha256(data).hexdigest()
    sub = info["subsystem"]
    print("%s" % path)
    print("    size        : %d bytes" % len(data))
    print("    sha256      : %s" % sha)
    print("    machine     : 0x%04X (%s)" % (info["machine"], "AMD64" if info["machine"] == IMAGE_FILE_MACHINE_AMD64 else "UNEXPECTED"))
    print("    subsystem   : %d (%s)" % (sub, SUBSYSTEMS.get(sub, "?")))
    print("    entry point : RVA 0x%X" % info["entry_rva"])

    if info["machine"] != IMAGE_FILE_MACHINE_AMD64:
        print("    FAIL: machine type is not AMD64")
        ok = False
    if sub != 2:
        print("    FAIL: subsystem is %d, expected 2 (Windows GUI); link with -Wl,--subsystem,windows" % sub)
        ok = False

    app_type = entry_app_type(data, info)
    if app_type == 1:
        print("    entry stub  : WinMainCRTStartup (stores __mingw_app_type = 1, GUI start-up)  OK")
    elif app_type == 0:
        print("    FAIL: entry stub is the CONSOLE start-up (mainCRTStartup, __mingw_app_type = 0).")
        print("          A console-linked binary whose subsystem byte was patched aborts at launch;")
        print("          link with -Wl,--subsystem,windows instead of patching.")
        ok = False
    else:
        msg = "entry stub not recognised (different compiler output?)"
        if strict:
            print("    FAIL: " + msg)
            ok = False
        else:
            print("    WARN: " + msg)

    print("    result      : %s" % ("PASS" if ok else "FAIL"))
    return ok


def main(argv):
    strict = False
    files = []
    for a in argv:
        if a == "--strict":
            strict = True
        else:
            files.append(a)
    if not files:
        print(__doc__)
        return 2
    results = [verify(f, strict) for f in files]
    return 0 if all(results) else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
