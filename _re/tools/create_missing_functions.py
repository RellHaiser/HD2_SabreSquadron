"""Create Ghidra functions for code pointers found in .rdata/.data tables (vtables,
callback tables) that Ghidra never turned into functions.

A target is skipped if it lies inside an existing function body (switch jump tables).
Bodies are fetched cheaply (no decompilation) for every known function.

Usage: python create_missing_functions.py [--dry-run]
"""
import bisect
import json
import os
import struct
import sys
import time
import urllib.parse
import urllib.request
import os as _os, sys as _sys
_sys.path.insert(0, _os.path.dirname(_os.path.abspath(__file__)))
import ghidra_program  # noqa: E402,F401  (adds program= to plugin requests)

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
RAW = os.path.join(ROOT, "raw")
BASE = "http://127.0.0.1:8089"
TEXT_LO, TEXT_HI = 0x401000, 0x80F000


def get(path):
    return json.loads(urllib.request.urlopen(BASE + path, timeout=600).read())


def post(path, body):
    req = urllib.request.Request(BASE + path, data=json.dumps(body).encode(), method="POST",
                                 headers={"Content-Type": "application/json"})
    return json.loads(urllib.request.urlopen(req, timeout=120).read())


def code_pointer_targets():
    out = set()
    for name, base in (("rdata", 0x80F000), ("data", 0x832000)):
        blob = open(os.path.join(RAW, f"{name}_{base:08x}.bin"), "rb").read()
        n = len(blob) // 4
        i = 0
        while i < n:
            j = i
            while j < n and TEXT_LO <= struct.unpack_from("<I", blob, j * 4)[0] < TEXT_HI:
                j += 1
            if j - i >= 3:
                out.update(struct.unpack_from("<I", blob, k * 4)[0] for k in range(i, j))
            i = max(j, i + 1)
    return out


def function_bodies():
    fs = get("/find_functions?limit=0")["functions"]
    addrs = [f["address"] for f in fs if TEXT_LO <= int(f["address"], 16) < TEXT_HI]
    bodies = []
    for i in range(0, len(addrs), 20):
        chunk = addrs[i:i + 20]
        q = urllib.parse.urlencode({"functions": ",".join("0x" + a for a in chunk), "fields": "body_end"})
        d = get("/get_functions?" + q)
        for v in d["functions"].values():
            if "address" in v and "body_end" in v:
                bodies.append((int(v["address"], 16), int(v["body_end"], 16)))
    bodies.sort()
    return bodies


def main():
    dry = "--dry-run" in sys.argv
    targets = code_pointer_targets()
    bodies = function_bodies()
    starts = [b[0] for b in bodies]
    todo = []
    for t in sorted(targets):
        i = bisect.bisect_right(starts, t) - 1
        if i >= 0 and bodies[i][0] == t:
            continue  # already a function
        if i >= 0 and bodies[i][0] < t <= bodies[i][1]:
            continue  # inside a body: jump-table entry
        todo.append(t)
    print(f"{len(targets)} code pointers, {len(todo)} need functions", flush=True)
    made, failed = [], []
    for t in todo:
        r = {}
        for attempt in range(6):
            try:
                r = post("/create_function", {"address": f"0x{t:08x}", "dry_run": dry})
            except Exception as e:  # noqa: BLE001
                r = {"error": str(e)}
            if "transaction" not in str(r.get("error", "")):
                break
            time.sleep(1 + attempt)
        (made if r.get("success") else failed).append((t, r.get("message") or r.get("error")))
    print(f"created {len(made)}, failed {len(failed)}")
    json.dump({"created": [f"{t:08x}" for t, _ in made], "failed": [[f"{t:08x}", m] for t, m in failed]},
              open(os.path.join(RAW, "created_functions.json"), "w"), indent=0)
    for t, m in failed[:20]:
        print(f"  fail {t:08x}: {m}")


if __name__ == "__main__":
    main()
