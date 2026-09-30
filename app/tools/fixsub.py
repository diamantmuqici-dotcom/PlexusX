#!/usr/bin/env python3
"""Post-link: switch PE subsystem CONSOLE(3) -> GUI(2) so no console window
appears, then copy to the destination path."""
import struct
import sys
import shutil

src, dst = sys.argv[1], sys.argv[2]
data = bytearray(open(src, "rb").read())
pe = struct.unpack_from("<I", data, 0x3C)[0]
if data[pe:pe + 4] != b"PE\0\0":
    sys.exit("not a PE file")
opt = pe + 24
sub = struct.unpack_from("<H", data, opt + 68)[0]
struct.pack_into("<H", data, opt + 68, 2)  # IMAGE_SUBSYSTEM_WINDOWS_GUI
open(dst, "wb").write(data)
print(f"subsystem {sub} -> 2 (GUI), wrote {dst}")
