#!/usr/bin/env python3
"""The game's shader disk cache (DXBC files) in one file, for the PS4: <out> = shadercache.pak, read by the runtime in
place of the folder (runtime/gpu_d3d11.c pack_open: on the PS4 a look-up in a folder of 45,000 files is slow).

    pack-shadercache.py <out.pak> <folder> [<folder>...]     (a later folder's file wins over an earlier one's)

Layout (little-endian): "SSBUDXBC", u32 version 1, u32 count; count entries {u64 FNV-1a of the file's name, u64 offset,
u32 size, u32 0}, sorted by the hash; then the files' bytes, each at a multiple of 16."""
import os
import struct
import sys


def fnv1a(data):
    h = 1469598103934665603
    for b in data:
        h = ((h ^ b) * 1099511628211) & 0xFFFFFFFFFFFFFFFF
    return h


def main():
    if len(sys.argv) < 3:
        sys.exit(__doc__)
    out, folders = sys.argv[1], sys.argv[2:]
    files = {}
    for folder in folders:
        for name in os.listdir(folder):
            if name.startswith("v") and name.endswith(".dxbc"):
                files[name] = os.path.join(folder, name)
    by_hash = {}
    for name in files:
        h = fnv1a(name.encode())
        if h in by_hash:
            sys.exit(f"{name} and {by_hash[h]}: the same hash")
        by_hash[h] = name
    order = sorted(by_hash)
    sizes = {h: os.path.getsize(files[by_hash[h]]) for h in order}
    at = 16 + 24 * len(order)
    places = {}
    for h in order:
        at = (at + 15) & ~15
        places[h] = at
        at += sizes[h]
    tmp = out + ".part"
    with open(tmp, "wb") as f:
        f.write(b"SSBUDXBC" + struct.pack("<II", 1, len(order)))
        for h in order:
            f.write(struct.pack("<QQII", h, places[h], sizes[h], 0))
        for h in order:
            f.write(b"\0" * (places[h] - f.tell()))
            with open(files[by_hash[h]], "rb") as src:
                data = src.read()
            if len(data) != sizes[h]:
                sys.exit(f"{files[by_hash[h]]} changed size")
            f.write(data)
    os.replace(tmp, out)
    print(f"{out}: {len(order)} files, {at / 1048576:.0f} MiB")


if __name__ == "__main__":
    main()
