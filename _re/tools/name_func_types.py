"""Give the anonymous function types of vtable slots (/functions/_func_N) unique names.

Ghidra's C parser gives the function type of every inline function-pointer field the name
`_func_N`, numbering from 1 in each parse, and /import_data_types REPLACES existing types of
the same name. So every partial import (import_spec.py) rewrote the slot signatures of all
vtables imported earlier whose types carried the same numbers (2026-10-18: C_GameState's
GetState had become an ISND_stream method taking two arguments, C_Actor's slots had the
signatures of whatever was imported last). Naming each type after its slot
(`_func_<struct>__<field>`) keeps the next parse's `_func_N` from replacing it.

import_spec.py calls name_types() after every import; run this tool after any other import
that creates vtables (apply_ghidra.py --types).

Usage: python name_func_types.py [STRUCT ...] [--dry-run]
       (default: every struct of ghidra_layouts.json; run fetch_types.py first)
"""
import json
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import apply_ghidra as ag  # noqa: E402
import ghidra_program  # noqa: E402,F401  (adds program= to plugin requests)

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ANON = re.compile(r"(_func_\d+) \*$")


def unique_name(struct, field):
    return re.sub(r"\W", "_", f"_func_{struct}__{field}")


def name_types(layouts, dry=False, log=print):
    """layouts: struct name -> {"fields": [{"name", "type"}...]}. Renames every anonymous
    `_func_N` a field points to; returns the number renamed."""
    owners = {}
    for struct, lay in layouts.items():
        for f in lay.get("fields", []):
            m = ANON.match(f["type"].strip())
            if m:
                owners.setdefault(m.group(1), []).append((struct, f["name"]))
    renamed = 0
    for anon, refs in sorted(owners.items(), key=lambda kv: int(kv[0][6:])):
        if len(refs) > 1:
            log(f"  {anon} is shared by {refs[:4]}: named after the first")
        target = unique_name(*refs[0])
        r = ag.post("/rename_data_type", {"old_name": anon, "new_name": target, "dry_run": dry})
        if not ag.ok(r) and "exist" in str(r.get("error", "")).lower():
            # A re-imported slot: the type of the earlier import is no longer used.
            d = ag.post("/delete_data_type", {"type_name": target, "dry_run": dry})
            if not ag.ok(d):
                ag.post("/rename_data_type", {"old_name": target, "new_name": target + "__old", "dry_run": dry})
            r = ag.post("/rename_data_type", {"old_name": anon, "new_name": target, "dry_run": dry})
        if ag.ok(r):
            renamed += 1
        else:
            log(f"  {anon} -> {target}: {r.get('error') or r}")
    return renamed


def main():
    dry = "--dry-run" in sys.argv
    only = [a for a in sys.argv[1:] if not a.startswith("--")]
    layouts = json.load(open(os.path.join(ROOT, "ghidra_layouts.json")))
    if only:
        layouts = {n: layouts[n] for n in only if n in layouts}
    print(f"renamed {name_types(layouts, dry)} function types")


if __name__ == "__main__":
    main()
