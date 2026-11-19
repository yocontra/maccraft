#!/usr/bin/env python3
"""Combine a Retro68 data fork and AppleDouble into one forked file.

Retro68 writes the data fork to <App>.APPL and the resource fork plus Finder
info to %<App>.ad. Mac OS 9 needs one file with all three, so this reconstitutes
it as an HFS-style forked file on the host: the data fork is the file's bytes,
the resource fork and Finder info are the APFS extended attributes that
SheepShaver's extfs, ditto and HFS read back.

    stage_app.py <data-fork> <apple-double> <destination>

Used both by make_vm.sh (installing into the cloned boot volume) and by
reload.sh (staging a hot-swap build into the shared folder).
"""


import ctypes
import os
import struct
import subprocess
import sys

def stage(data_path, ad_path, dest):

    libc = ctypes.CDLL(None, use_errno=True)
    libc.setxattr.argtypes = [ctypes.c_char_p, ctypes.c_char_p, ctypes.c_void_p,
                              ctypes.c_size_t, ctypes.c_uint32, ctypes.c_int]
    libc.setxattr.restype = ctypes.c_int


    def set_xattr(path, name, value):
        buf = ctypes.create_string_buffer(value, len(value))
        if libc.setxattr(path.encode(), name.encode(), buf, len(value), 0, 0) != 0:
            errno = ctypes.get_errno()
            raise OSError(errno, os.strerror(errno), path)


    def read_xattr(path, name):
        raw = subprocess.run(["/usr/bin/xattr", "-px", name, path],
                             check=False, capture_output=True).stdout
        if not raw.strip():
            return None
        return bytes.fromhex(raw.decode().replace("\n", ""))


    resource = b""
    finder_info = b""

    if os.path.exists(ad_path):
        with open(ad_path, "rb") as f:
            ad = f.read()
        if len(ad) < 26 or struct.unpack(">I", ad[0:4])[0] != 0x00051607:
            sys.exit("%s is not an AppleDouble file" % ad_path)
        count = struct.unpack(">H", ad[24:26])[0]
        for i in range(count):
            entry_id, offset, length = struct.unpack(">III", ad[26 + 12 * i:38 + 12 * i])
            if entry_id == 2:
                resource = ad[offset:offset + length]
            elif entry_id == 9:
                finder_info = ad[offset:offset + length]

    # The .APPL itself may already carry the forks (native or extracted builds).
    if not resource:
        resource = read_xattr(data_path, "com.apple.ResourceFork") or b""
    if not finder_info:
        finder_info = read_xattr(data_path, "com.apple.FinderInfo") or b""

    with open(data_path, "rb") as src, open(dest, "wb") as out:
        out.write(src.read())

    if resource:
        set_xattr(dest, "com.apple.ResourceFork", resource)
    if finder_info:
        set_xattr(dest, "com.apple.FinderInfo", finder_info[:32])

    if not resource:
        sys.exit(
            "%s has no resource fork and no AppleDouble alongside it (%s).\n"
            "Rebuild so that dist/%%<App>.ad is produced." % (data_path, ad_path)
        )

    print("make_vm: staged data %d bytes, resource fork %d bytes, Finder info %d bytes"
          % (os.path.getsize(dest), len(resource), len(finder_info)), file=sys.stderr)


if __name__ == "__main__":
    if len(sys.argv) != 4:
        sys.exit("usage: stage_app.py <data-fork> <apple-double> <destination>")
    stage(*sys.argv[1:4])
