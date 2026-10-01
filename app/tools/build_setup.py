#!/usr/bin/env python3
"""Build PlexusX-Setup.exe by embedding PlexusX.exe as payload.

The installer is linked as a GUI program directly (-Wl,--subsystem,windows), so the linker selects the
GUI CRT start-up.  The subsystem byte of a console-linked binary must never be patched afterwards: the
console start-up code would then abort inside a GUI-flagged image.
"""
import os
import shutil
import subprocess
import sys

def main():
    exe_path = sys.argv[1] if len(sys.argv) > 1 else "../site/download/PlexusX.exe"
    out_setup = sys.argv[2] if len(sys.argv) > 2 else "../site/download/PlexusX-Setup.exe"

    if not os.path.exists(exe_path):
        sys.exit(f"Source executable {exe_path} not found")

    with open(exe_path, "rb") as f:
        exe_bytes = f.read()

    os.makedirs("build", exist_ok=True)
    payload_c = "build/payload.c"
    print(f"Generating payload ({len(exe_bytes)} bytes)...")
    with open(payload_c, "w") as f:
        f.write("#include <stddef.h>\n")
        f.write("const unsigned int g_payload_exe_len = %d;\n" % len(exe_bytes))
        f.write("const unsigned char g_payload_exe[] = {\n")
        # Write chunks of bytes
        for i in range(0, len(exe_bytes), 16):
            chunk = exe_bytes[i:i+16]
            f.write("  " + ", ".join(f"0x{b:02x}" for b in chunk) + ",\n")
        f.write("};\n")

    print("Compiling Setup executable...")
    cmd = [
        "python3", "-m", "ziglang", "cc",
        "-target", "x86_64-windows-gnu",
        "-O2", "-std=c11", "-Wall",
        "-DUNICODE", "-D_UNICODE", "-D_CRT_SECURE_NO_WARNINGS", "-municode",
        "-Wl,--subsystem,windows",
        "-o", "build/PlexusX-Setup.exe",
        "tools/setup.c", "build/payload.c",
        "-luser32", "-lshell32", "-ladvapi32", "-lole32", "-lshlwapi"
    ]
    subprocess.check_call(cmd)

    # Plain copy: the linked image is shipped exactly as the linker produced it.
    shutil.copyfile("build/PlexusX-Setup.exe", out_setup)
    print(f"Successfully generated {out_setup} ({os.path.getsize(out_setup)} bytes)")

if __name__ == "__main__":
    main()
