"""Works out the car tables harness's output from the game's files and compares it line by line.

python check.py <game folder> <harness output>
"""
import struct
import sys


def f32(x):
    """x rounded to a float (the port's float arithmetic)."""
    return struct.unpack("<f", struct.pack("<f", x))[0]


def bits(x):
    return "%08X" % struct.unpack("<I", struct.pack("<f", x))[0]


THOUSAND, DEGREE = f32(1000.0), f32(0.017453292)


def tyres_lines(label, types):
    """types: [(id, name, entries or None)], entries [(surface, [12 floats])]."""
    out = [f"{label}: types {len(types)}"]
    for t, (tid, name, entries) in enumerate(types):
        out.append(f"type {t} id {tid} name '{name}' entries {-1 if entries is None else len(entries)}")
        for e, (surface, v) in enumerate(entries or []):
            vals = [f32(v[0] * THOUSAND), f32(v[1] * DEGREE), f32(v[2] * DEGREE)] + v[3:8] + \
                   [f32(v[8] * THOUSAND)] + v[9:12]
            out.append(f"entry {t} {e} surface {surface} " + " ".join(bits(x) for x in vals))
    return out


def parse_tyres(data):
    o = 0
    count = struct.unpack_from("<i", data, 4)[0]
    o = 8
    types = [(0, "develop", None)]
    for _ in range(count):
        o += 4
        name = data[o:o + 32].split(b"\0")[0].decode("latin-1")
        o += 32
        tid, n = struct.unpack_from("<ii", data, o)
        o += 8
        entries = []
        for _ in range(n):
            surface, size = struct.unpack_from("<iI", data, o)
            o += 8
            raw = data[o:o + size] + b"\0" * max(0, 48 - size)
            o += size
            entries.append((surface, list(struct.unpack_from("<12f", raw, 0))))
        types.append((tid, name, entries))
    return types


def synthetic_types():
    """The harness's synthetic file: 'empty' (no entries: an empty table), 'short' (two floats), 'full'."""
    def entry(floats, e):
        v = [f32((k + 1) * 0.5 + e) for k in range(floats)]
        return (30 + e, (v + [0.0] * 12)[:12])
    return [(0, "develop", None), (7, "empty", []), (100, "short", [entry(2, 0)]),
            (101, "full", [entry(13, 0), entry(13, 1)])]


HARD = {0x30, 0x33, 0x34, 0x35, 0x36, 0x37, 0x40, 0x41, 0x5F}


def surface_line(i, fr, bo, e0, e4, e8, ec, f0, dust, skid, contact):
    fr = 0.0 if fr < 0 else 1.0 if fr > 1 else fr
    bo = 1.0 if bo < 1 else 2.0 if bo > 2 else bo
    u04 = e0 if e4 > e0 else e4
    u0c = e8 if ec > e8 else ec
    f0 = 0.0 if f0 < 0 else f32(0.2) if f0 > f32(0.2) else f0
    hard = int(i in HARD)
    return (f"surface {i} {bits(u04)} {bits(e0)} {bits(u0c)} {bits(e8)} {bits(f0)} {bits(fr)} {bits(bo)} "
            f"{dust} {skid} {contact} {hard} {hard}")


def surfaces_lines(label, rows, map_before):
    out = [f"{label}: surfaces {len(rows)}"] + [surface_line(*r) for r in rows]
    table = [0] * 255 + [map_before]                    # (the last entry is left as it was)
    for k, r in enumerate(rows):
        table[r[0]] = k
    out.append(f"{label} map: " + " ".join(str(x) for x in table))
    return out


def main():
    game, output = sys.argv[1], sys.argv[2]
    tyres = open(game + r"\tables\tyres.bin", "rb").read()
    materials = open(game + r"\tables\hdmaterials.def", "rb").read()
    real = parse_tyres(tyres)
    want = ["load real: 1 open 0"] + tyres_lines("real", real)
    want += ["load again: 1", f"again types {len(real)} fresh tables 1"]
    want += ["load missing: 0 open 0"] + tyres_lines("missing", [(0, "develop", None)])
    want += ["load version 2: 0 open 0"] + tyres_lines("version 2", [(0, "develop", None)])
    want += ["load synthetic: 1 open 0"] + tyres_lines("synthetic", synthetic_types())
    want += ["freed: types 0"]
    want += ["init none: 1"]
    want += surfaces_lines("none", [(0, 0.35, 1.0, 100.0, 100.0, 100.0, 100.0, 0.0, 0, 0, 0)], 77)
    # (the defaults: 100 for all four, so the minimums keep them)
    rows = []
    for i in range(len(materials) // 256):
        fr, bo, e0, e4, e8, ec, f0 = struct.unpack_from("<7f", materials, i * 256 + 0xD8)
        dust, skid, contact = struct.unpack_from("<3i", materials, i * 256 + 0xF4)
        rows.append((i, fr, bo, e0, e4, e8, ec, f0, dust, skid, contact))
    want += ["init real: 1"] + surfaces_lines("real", rows, 77)
    rows = []
    for i in range(100):
        rows.append((i, (i % 7 - 3) * 0.5, (i % 5) * 0.75, float(i % 4), float(i * 3 % 5), float(i % 3),
                     float(i * 2 % 4), (i % 6 - 2) * 0.125, i, i + 1, -i))
    want += ["init synthetic: 1"] + surfaces_lines("synthetic", rows, 77)
    want += ["cleared: surfaces 0"]

    got = open(output, encoding="latin-1").read().splitlines()
    bad = [(k, w, g) for k, (w, g) in enumerate(zip(want, got)) if w != g]
    if len(want) != len(got):
        bad.append((min(len(want), len(got)), f"{len(want)} lines", f"{len(got)} lines"))
    for k, w, g in bad[:10]:
        print(f"line {k}:\n  want {w[:200]}\n  got  {g[:200]}")
    print(f"{len(want)} lines, {len(bad)} differences")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
