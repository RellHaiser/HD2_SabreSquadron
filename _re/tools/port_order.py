"""Rank source files for bottom-up porting: few calls into unported, unnamed game
code first. Writes _re/port_order.txt."""
import json
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import gen_tier1  # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def main():
    index = json.load(open(os.path.join(ROOT, "index.json")))
    funcs = {int(a, 16): r for a, r in index.items()}
    starts = gen_tier1.file_boundaries(funcs)
    ported = gen_tier1.ported_set()
    syms = json.load(open(os.path.join(ROOT, "symbols.json")))
    file_of = {}
    for a in funcs:
        if gen_tier1.GAME_LO <= a < gen_tier1.GAME_HI:
            file_of[a] = max([s for s in starts if s <= a], default=gen_tier1.GAME_LO)
    files = {}
    for a, f in file_of.items():
        if (funcs[a]["name"] or "").startswith(("Unwind@", "Catch@")):
            continue
        files.setdefault(f, []).append(a)
    rows = []
    for f, fs in files.items():
        if all(f"{a:08x}" in ported for a in fs):
            continue
        ext = set()
        for a in fs:
            for c in funcs[a]["callees"]:
                ci = int(c, 16)
                if ci in file_of and file_of[ci] != f and c not in ported and c not in syms:
                    ext.add(c)
        size = sum(funcs[a]["size"] for a in fs)
        rows.append((len(ext), size, f, len(fs)))
    rows.sort()
    with open(os.path.join(ROOT, "port_order.txt"), "w") as fh:
        for ext, size, f, n in rows:
            fh.write(f"{f:08x} ext={ext:<4} bytes={size:<7} n={n}\n")
    print(f"{len(rows)} unported files")


if __name__ == "__main__":
    main()
