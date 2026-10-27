"""Create Ghidra functions for callbacks that are only referenced as code labels.

Callbacks passed by address (`EnumFrames(&LAB_00410b40, ...)`, atexit handlers ...)
are often never turned into functions, so they are missing from the export. This
scans the exported decompilation for `&LAB_xxxxxxxx` arguments whose target lies
outside every known function body and creates functions there.

Usage: python create_callback_functions.py [--dry-run]
Afterwards export the new functions (export_decomp.py) and rebuild the index.
"""
import bisect
import glob
import json
import os
import re
import sys
import urllib.request
import os as _os, sys as _sys
_sys.path.insert(0, _os.path.dirname(_os.path.abspath(__file__)))
import ghidra_program  # noqa: E402,F401  (adds program= to plugin requests)

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BASE = "http://127.0.0.1:8089"
TEXT_LO, TEXT_HI = 0x401000, 0x7E2000
LABEL_ARG = re.compile(r"[(,]\s*(?:\(\w+ \*\))?&LAB_([0-9a-f]{8})\b")


def main():
    dry = "--dry-run" in sys.argv
    bodies = []
    targets = set()
    for path in glob.glob(os.path.join(ROOT, "decomp", "*.json")):
        d = json.load(open(path, encoding="utf-8"))
        start = int(d["address"], 16)
        end = int(d.get("body_end") or d["address"], 16)
        bodies.append((start, end))
        for m in LABEL_ARG.finditer(d.get("decompiled_code") or ""):
            targets.add(int(m.group(1), 16))
    bodies.sort()
    starts = [b[0] for b in bodies]

    def inside(a):
        i = bisect.bisect_right(starts, a) - 1
        return i >= 0 and bodies[i][0] <= a <= bodies[i][1]

    new = sorted(a for a in targets if TEXT_LO <= a < TEXT_HI and not inside(a))
    print(f"{len(targets)} label arguments, {len(new)} outside functions")
    created = []
    for a in new:
        if dry:
            print(f"  would create {a:08x}")
            continue
        req = urllib.request.Request(BASE + "/create_function", data=json.dumps({"address": f"0x{a:08x}"}).encode(),
                                     headers={"Content-Type": "application/json"})
        try:
            r = json.loads(urllib.request.urlopen(req, timeout=120).read())
        except Exception as e:  # noqa: BLE001
            r = {"error": str(e)}
        ok = not r.get("error")
        print(f"  {a:08x}: {'ok' if ok else r.get('error')}")
        if ok:
            created.append(f"{a:08x}")
    if created:
        print("export with: python export_decomp.py --only " + ",".join(created))


if __name__ == "__main__":
    main()
