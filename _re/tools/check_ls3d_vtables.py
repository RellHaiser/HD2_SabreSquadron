"""List LS3D vtables whose Ghidra copy no longer matches src/ls3d/*.h.

Naming a slot in i3d.h / igraph.h changes the generated vtable (ls3d_ghidra.emit()), and tier 1
takes that generated struct, while Ghidra's copy keeps the old `SlotNN` name until it is
re-imported. Decompiles exported in between then call `vtable->SlotNN` and fail to compile.
Run after `fetch_types.py`; it prints the stale vtables and the import command that repairs them
(then re-export the callers: decompiles still mentioning the old slot names).

  python check_ls3d_vtables.py
"""
import json
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import ls3d_ghidra  # noqa: E402

ROOT = os.path.dirname(HERE)


def main():
    src = ls3d_ghidra.emit()
    generated = {m.group(1): m.group(0) for m in re.finditer(r"(?ms)^struct (\w+) \{\n.*?^\};", src)}
    data = json.load(open(os.path.join(ROOT, "ghidra_layouts.json")))
    if isinstance(data, dict) and "structures" in data:
        data = data["structures"]
    layouts = data if isinstance(data, dict) else {s["name"]: s for s in data}
    stale = []
    for name, text in generated.items():
        if not name.endswith("_vtbl") or name not in layouts:
            continue
        slots = []
        for line in text.splitlines()[1:-1]:
            m = re.search(r"\(\s*__stdcall\s*\*\s*(\w+)\)|\*\s*(\w+);\s*$|\b(\w+);\s*$", line)
            if m:
                slots.append(next(g for g in m.groups() if g))
        fields = layouts[name].get("fields") or layouts[name].get("members") or []
        have = [f["name"] for f in fields]
        diff = [(h, g) for h, g in zip(have, slots) if h != g]
        if diff or len(have) != len(slots):
            stale.append(name)
            print(f"{name}: " + ", ".join(f"{h} -> {g}" for h, g in diff[:8]))
    if stale:
        print("\npython tools/import_spec.py --structs " + ",".join(stale))
    else:
        print("all LS3D vtables match")


if __name__ == "__main__":
    main()
