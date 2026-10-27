"""Give auto-recovered classes their class_names.json names in Ghidra.

auto_classes.py first created each class as C_auto_<vtable> and members were
typed with it, so the recovered field layout lives there. Naming a class later
created a second, partial struct under the chosen name. This moves the partial
one aside (<name>__stub, same for <name>_vtbl) and renames C_auto_<vtable> (and
its _vtbl) to the chosen name, keeping every use of the recovered layout.
Afterwards retype `this` of the classes' members (apply_members.py --classes,
apply_ghidra.py --symbols --only) so nothing still points at a stub.

Usage: python rename_auto_structs.py [--dry-run]   (prints the renamed classes)
"""
import argparse
import json
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import apply_ghidra as ag  # noqa: E402
import os as _os, sys as _sys
_sys.path.insert(0, _os.path.dirname(_os.path.abspath(__file__)))
import ghidra_program  # noqa: E402,F401  (adds program= to plugin requests)

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def candidates():
    layouts = json.load(open(os.path.join(ROOT, "ghidra_layouts.json")))
    names = json.load(open(os.path.join(ROOT, "class_names.json")))
    auto = ag.spec_names("90_auto.spec")
    out = []
    for vt, name in names.items():
        old = f"C_auto_{vt}"
        # An empty placeholder of the old name (recreated by a stale reference) must not
        # replace the real, already renamed class.
        if old in layouts and not layouts[old]["fields"] and layouts[old]["size"] <= 1:
            print(f"  skipped empty placeholder {old}")
            continue
        if name in auto and old in layouts:
            out.append((old, name, name in layouts, old + "_vtbl" in layouts, name + "_vtbl" in layouts))
    return out


def rename(old, new, dry):
    r = ag.post("/rename_data_type", {"old_name": old, "new_name": new, "dry_run": dry})
    if not ag.ok(r):
        print(f"  {old} -> {new}: {r.get('error') or r}")
    return ag.ok(r)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--dry-run", action="store_true")
    args = ap.parse_args()
    done = []
    for old, name, has_stub, has_old_vt, has_stub_vt in candidates():
        if has_stub and not rename(name, name + "__stub", args.dry_run):
            continue
        if has_stub_vt:
            rename(name + "_vtbl", name + "_vtbl__stub", args.dry_run)
        if rename(old, name, args.dry_run):
            done.append(name)
        if has_old_vt:
            rename(old + "_vtbl", name + "_vtbl", args.dry_run)
    print(f"renamed {len(done)} classes")
    print(",".join(done))
    if done and not args.dry_run:
        # Member functions still have the old name as an orphaned `this` type.
        print(f"NEXT: python apply_members.py --classes {','.join(done)}")


if __name__ == "__main__":
    main()
