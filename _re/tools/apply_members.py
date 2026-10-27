"""Assign auto-recovered member functions to their classes in Ghidra (types `this`).

Reads _re/auto_members.json (from auto_classes.py). Functions that already have a
hand-chosen name in symbols.json are skipped (apply_ghidra.py handles those).

Usage: python apply_members.py [--jobs N] [--classes A,B,...]
"""
import argparse
import concurrent.futures as cf
import json
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import apply_ghidra as ag  # noqa: E402
import os as _os, sys as _sys
_sys.path.insert(0, _os.path.dirname(_os.path.abspath(__file__)))
import ghidra_program  # noqa: E402,F401  (adds program= to plugin requests)

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def assign(item):
    addr, cls = item
    a = "0x" + addr
    r = ag.post("/set_function_this_type", {"function_address": a, "this_type": cls})
    if "no implicit 'this'" in str(r.get("error", "")):
        ag.make_thiscall(a, False)
        r = ag.post("/set_function_this_type", {"function_address": a, "this_type": cls})
    return addr, ag.ok(r), r.get("error")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--jobs", type=int, default=4)
    ap.add_argument("--classes", default="", help="only members of these classes")
    args = ap.parse_args()
    classes = {c for c in args.classes.split(",") if c}
    members = json.load(open(os.path.join(ROOT, "auto_members.json")))["members"]
    syms = ag.load_symbols()
    todo = [(a, c) for a, c in members.items() if a not in syms and (not classes or c in classes)]
    ok = bad = 0
    errors = {}
    with cf.ThreadPoolExecutor(args.jobs) as ex:
        for addr, good, err in ex.map(assign, todo):
            if good:
                ok += 1
            else:
                bad += 1
                errors[addr] = err
    json.dump(errors, open(os.path.join(ROOT, "apply_members_errors.json"), "w"), indent=0)
    print(f"{ok} assigned, {bad} failed")


if __name__ == "__main__":
    main()
