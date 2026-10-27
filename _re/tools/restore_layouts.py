"""Restore struct layouts in Ghidra from a snapshot written by fetch_types.py.

Needed after an import replaced structs it only referenced with empty 1-byte
structs (see import_spec.py). Fields are re-created at their snapshot offsets;
referenced structs that are neither restored nor defined get their current
Ghidra definition appended (so they are not emptied in turn).

Usage: python restore_layouts.py SNAPSHOT.json NAME [NAME...] [--dry-run] [--show]
       python restore_layouts.py SNAPSHOT.json --damaged DAMAGE.json [--dry-run]
"""
import json
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import apply_ghidra as ag  # noqa: E402
import import_spec as isp  # noqa: E402
import os as _os, sys as _sys
_sys.path.insert(0, _os.path.dirname(_os.path.abspath(__file__)))
import ghidra_program  # noqa: E402,F401  (adds program= to plugin requests)


def struct_def(name, lay, need, by_value):
    lines, pos = [], 0
    for f in sorted(lay["fields"], key=lambda f: f["offset"]):
        if f["offset"] < pos or f["size"] <= 0:
            continue
        if f["offset"] > pos:
            lines.append(f"    unsigned char _gap_{pos:04x}[{f['offset'] - pos}];")
        fname = re.sub(r"\W", "_", f["name"] or f"field_{f['offset']:x}")
        lines.append(isp.c_field(f["type"], fname, need, by_value, f["size"]))
        pos = f["offset"] + f["size"]
    if lay["size"] > pos:
        lines.append(f"    unsigned char _tail_{pos:04x}[{lay['size'] - pos}];")
    return f"struct {name} {{\n" + "\n".join(lines) + "\n};"


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    snap = json.load(open(args[0], encoding="utf-8"))
    if "--damaged" in sys.argv:
        names = [d[0] for d in json.load(open(args[1], encoding="utf-8")) if d[0] in snap]
    else:
        names = args[1:]
    defs, deps, need_all = {}, {}, set()
    for n in names:
        need, by_value = set(), set()
        defs[n] = struct_def(n, snap[n], need, by_value)
        deps[n] = by_value
        need_all |= need
    src_defs = "\n".join(defs.values())
    # Referenced structs outside the restored set keep their current definition.
    ext = isp.external_defs(src_defs + "\n" + "\n".join(f"struct {x} *_ref;" for x in sorted(need_all - set(defs))))
    out, emitted = [], set()
    order = dict(defs)
    while order:
        ready = [n for n in sorted(order) if deps[n] <= emitted | (deps[n] - set(order))]
        if not ready:
            raise SystemExit(f"cyclic by-value structs: {sorted(order)}")
        for n in ready:
            out.append(order.pop(n))
            emitted.add(n)
    enums = [isp.spec_enums()[n] for n in sorted(isp.ENUMS_USED)]
    src = "\n".join(["#pragma pack(push, 1)"] + enums + ext + out + ["#pragma pack(pop)"]) + "\n"
    if "--show" in sys.argv:
        print(src)
    r = ag.post("/import_data_types", {"source": src, "category": "/", "dry_run": "--dry-run" in sys.argv})
    print(len(names), "structs;", {k: v for k, v in r.items() if k != "messages"})
    print((r.get("messages") or "")[:2000])


if __name__ == "__main__":
    main()
