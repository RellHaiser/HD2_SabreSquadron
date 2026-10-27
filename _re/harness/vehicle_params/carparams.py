"""Collect the cars' property blobs (chunk 0xAE24) from the shipped missions and test the
COMP run-length codec (0x004d0110) against them: decode, re-encode, compare bytes."""
import glob
import os
import struct
import sys

ROOT = r"H:\HD2_Unpacked\MISSIONS"


def chunks(data, pos, end):
    while pos + 6 <= end:
        cid, size = struct.unpack_from("<HI", data, pos)
        if size < 6 or pos + size > end:
            return
        yield cid, pos + 6, pos + size
        pos += size


def walk(data, pos, end, path, out):
    kids = list(chunks(data, pos, end))
    types = [data[a:b] for cid, a, b in kids if cid == 0xAE22]
    is_car = bool(types) and len(types[0]) >= 4 and struct.unpack_from("<I", types[0])[0] == 4
    for cid, a, b in kids:
        if cid == 0xAE24:
            if is_car:
                out.append((path, data[a:b]))
        elif b - a >= 6:
            # Try to descend: a valid sub-chunk sequence covering the payload exactly.
            p, ok = a, True
            while p < b:
                if p + 6 > b:
                    ok = False
                    break
                _, s = struct.unpack_from("<HI", data, p)
                if s < 6 or p + s > b:
                    ok = False
                    break
                p += s
            if ok and p == b:
                walk(data, a, b, path + [cid], out)


def decode(blob):
    magic, raw, total, esc = struct.unpack_from("<4sIIB", blob, 0)
    assert magic == b"COMP", magic
    out = bytearray()
    i = 13
    while i < total:
        c = blob[i]
        if c == esc:
            v, n = blob[i + 1], blob[i + 2]
            out += bytes([v]) * n
            i += 3
        else:
            out.append(c)
            i += 1
    return raw, total, bytes(out)


def encode(data):
    counts = [0] * 256
    for b in data:
        counts[b] += 1
    esc, best = 0, 0xFFFFFFFF
    for v in range(256):
        if counts[v] < best:
            esc, best = v, counts[v]
    out = bytearray()

    def flush(c, n):
        if n >= 4 or c == esc:
            out.extend((esc, c, n))
        else:
            out.extend(bytes([c]) * n)
    cur, n = data[0], 0
    for b in data:
        if b == cur:
            n += 1
            if n == 0xFF:
                out.extend((esc, cur, 0xFF))
                n = 0
        else:
            if n or cur == esc:
                flush(cur, n)
            cur, n = b, 1
    flush(cur, n)
    return struct.pack("<4sIIB", b"COMP", len(data), len(out) + 13, esc) + bytes(out)


found = []
for f in sorted(glob.glob(os.path.join(ROOT, "*", "actors.bin"))):
    data = open(f, "rb").read()
    out = []
    walk(data, 0, len(data), [], out)
    for path, payload in out:
        found.append((f, path, payload))
print(len(found), "car property chunks in", len({x[0] for x in found}), "missions")
sizes, bad, same = {}, 0, 0
for f, path, p in found:
    ver, n = struct.unpack_from("<II", p, 0)
    blob = p[8:8 + n]
    raw, total, dec = decode(blob)
    sizes[(ver, raw)] = sizes.get((ver, raw), 0) + 1
    if len(dec) != raw or total != n or len(p) != 8 + n:
        bad += 1
        print("MISMATCH", f, [hex(c) for c in path], ver, n, raw, total, len(dec), len(p))
    if encode(dec) == blob:
        same += 1
print("versions/raw sizes:", {(v, hex(r)): c for (v, r), c in sizes.items()})
print("size mismatches:", bad, " re-encoded byte-identical:", same, "/", len(found))
if len(sys.argv) > 1:
    os.makedirs(sys.argv[1], exist_ok=True)
    for i, (f, path, p) in enumerate(found):
        n = struct.unpack_from("<I", p, 4)[0]
        open(os.path.join(sys.argv[1], "car%03d_%s.bin" % (i, os.path.basename(os.path.dirname(f)))), "wb").write(
            decode(p[8:8 + n])[2])
if len(sys.argv) > 2:
    # The chunk payloads as they are (version, size, COMP blob) for the C++ harness.
    os.makedirs(sys.argv[2], exist_ok=True)
    for i, (f, path, p) in enumerate(found):
        open(os.path.join(sys.argv[2], "car%03d_%s.prop" % (i, os.path.basename(os.path.dirname(f)))), "wb").write(p)
