"""Merge per-module name lists (_re/names/*.txt) into _re/symbols.json.

Line format (blank lines and # comments ignored):
    ADDR  Qualified::Name  [| ghidra prototype]
    ADDR  g_globalName     @ type          (data symbol)

Usage: python names.py            merge all lists
       python names.py --apply    merge, then push the touched entries to Ghidra
"""
import glob
import json
import os
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def parse(path):
    out = {}
    for raw in open(path, encoding="utf-8"):
        line = raw.split("#", 1)[0].strip()
        if not line:
            continue
        addr, rest = line.split(None, 1)
        addr = addr.lower().replace("0x", "").zfill(8)
        entry = {}
        if "@" in rest:
            name, typ = (x.strip() for x in rest.split("@", 1))
            entry = {"name": name, "data": True}
            if typ:
                entry["type"] = typ
        else:
            name, _, proto = (x.strip() for x in rest.partition("|"))
            entry = {"name": name}
            if proto:
                entry["proto"] = proto
        entry["module"] = os.path.splitext(os.path.basename(path))[0]
        out[addr] = entry
    return out


def main():
    p = os.path.join(ROOT, "symbols.json")
    syms = json.load(open(p)) if os.path.exists(p) else {}
    touched = []
    # Generated lists (zz_*) first, so hand-written names win where both exist.
    paths = sorted(glob.glob(os.path.join(ROOT, "names", "*.txt")),
                   key=lambda p: (not os.path.basename(p).startswith("zz_"), p))
    merged = {}
    for path in paths:
        merged.update(parse(path))
    for addr, entry in merged.items():
        if syms.get(addr) != entry:
            touched.append(addr)
        syms[addr] = entry
    json.dump(syms, open(p, "w"), indent=1, sort_keys=True)
    print(f"{len(syms)} symbols, {len(touched)} changed")
    if "--apply" in sys.argv and touched:
        subprocess.run([sys.executable, os.path.join(ROOT, "tools", "apply_ghidra.py"), "--symbols",
                        "--only", ",".join(touched)])


if __name__ == "__main__":
    main()
