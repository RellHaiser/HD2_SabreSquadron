"""Compares the chunk 0x44D of an MPSetings.sav with the one `harness roundtrip` saved.

  python check_roundtrip.py ORIGINAL SAVED

The rotations keep only maps of the map list read, so the profile and mpmaplist.txt must belong to
the same install.
"""
import struct
import sys


def chunk_44d(path):
    data = open(path, "rb").read()
    rid, rsize = struct.unpack_from("<HI", data, 0)
    pos = 6
    while pos < rsize:
        cid, csize = struct.unpack_from("<HI", data, pos)
        if cid == 0x44D:
            return data[pos:pos + csize]
        pos += csize
    return None


def main():
    a, b = chunk_44d(sys.argv[1]), chunk_44d(sys.argv[2])
    if a is None:
        print("no 0x44D in", sys.argv[1])
        return 1
    if a == b:
        print("roundtrip: %d bytes identical" % len(a))
        return 0
    n = next((i for i in range(min(len(a), len(b))) if a[i] != b[i]), min(len(a), len(b)))
    print("roundtrip: DIFFER at %d (%d vs %d bytes): %s / %s" % (n, len(a), len(b), a[n:n + 16].hex(), b[n:n + 16].hex()))
    return 1


if __name__ == "__main__":
    sys.exit(main())
