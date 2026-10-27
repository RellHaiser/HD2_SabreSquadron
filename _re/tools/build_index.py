"""Build a queryable index over the exported Ghidra data.

Inputs : _re/raw/*.json, _re/raw/*.bin, _re/decomp/*.json
Outputs: _re/index.json  - per-function metadata (no code)
         _re/vtables.json - vtable start -> [function addresses]

Per function we record: name, size, line count, callers, callees, the strings
it references, the vtables it references (ctor/dtor evidence) and the globals
it touches.
"""
import json
import re
import os
import struct

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
RAW = os.path.join(ROOT, "raw")
DEC = os.path.join(ROOT, "decomp")

RDATA_BASE = 0x80F000
DATA_BASE = 0x832000


def load_sections():
    secs = []
    for fn in os.listdir(RAW):
        if fn.endswith(".bin"):
            base = int(fn.split("_")[1][:8], 16)
            secs.append((base, open(os.path.join(RAW, fn), "rb").read()))
    return secs


def main():
    funcs_list = json.load(open(os.path.join(RAW, "functions.json")))["functions"]
    all_starts = {int(f["address"], 16): f for f in funcs_list}
    strings = {int(s["address"], 16): s["value"] for s in json.load(open(os.path.join(RAW, "strings_all.json")))["strings"]}

    # ---- vtables: runs of >=2 consecutive pointers to function starts in .rdata/.data
    vtables = {}
    for base, blob in load_sections():
        if base not in (RDATA_BASE, DATA_BASE):
            continue
        n = len(blob) // 4
        i = 0
        while i < n:
            v = struct.unpack_from("<I", blob, i * 4)[0]
            if v in all_starts and not all_starts[v]["name"].startswith(("Unwind@", "Catch@")):
                j = i
                run = []
                while j < n:
                    w = struct.unpack_from("<I", blob, j * 4)[0]
                    if w in all_starts:
                        run.append(w)
                        j += 1
                    else:
                        break
                if len(run) >= 2:
                    vtables[base + i * 4] = run
                i = j
            else:
                i += 1

    vt_starts = set(vtables)
    index, codes, sigs = {}, {}, {}
    for fn in os.listdir(DEC):
        if not fn.endswith(".json"):
            continue
        d = json.load(open(os.path.join(DEC, fn), encoding="utf-8"))
        a = int(d["address"], 16)
        code = d.get("decompiled_code") or ""
        codes[a] = code
        sigs[a] = d.get("signature") or ""
        refs = [int(r.split("<")[0], 16) for r in d.get("refs", [])]
        srefs = sorted({r for r in refs if r in strings})
        vrefs = sorted({r for r in refs if r in vt_starts})
        grefs = sorted({r for r in refs if 0x80F000 <= r < 0x98E000 and r not in strings and r not in vt_starts})
        index[a] = {
            "name": d.get("name"),
            "size": d.get("size", 0),
            "lines": code.count("\n"),
            "chars": len(code),
            "callers": sorted({int(c["address"], 16) for c in d.get("callers", []) if ":" not in c["address"]}),
            "callees": sorted({int(c["address"], 16) for c in d.get("callees", []) if ":" not in c["address"]}),
            "imports": sorted({c["name"] for c in d.get("callees", []) if ":" in c["address"]}),
            "strings": srefs,
            "vtables": vrefs,
            "globals": grefs,
        }

    # The API caps callee lists at 50 entries: add the calls visible in the code text
    # and rebuild callers as the inverse relation.
    by_name, plain_count = {}, {}
    for a, rec in index.items():
        n = rec["name"] or ""
        if n.startswith(("FUN_", "Unwind@", "Catch@")):
            continue
        plain_count[n] = plain_count.get(n, 0) + 1
        m = re.search(r"\(\s*(\w+)\s*\*\s*this\b", sigs.get(a, ""))
        if m:
            by_name[m.group(1) + "::" + n] = a
    for a, rec in index.items():
        n = rec["name"] or ""
        if plain_count.get(n) == 1:
            by_name.setdefault(n, a)
    call_rx = re.compile(r"\b(?:(?:[A-Za-z_]\w*::)?FUN_([0-9a-f]{8})|([A-Za-z_]\w*(?:::~?\w+)?))\s*\(")
    for a, rec in index.items():
        code = codes.get(a, "")
        found = set(rec["callees"])
        for m in call_rx.finditer(code):
            if m.group(1):
                t = int(m.group(1), 16)
            else:
                t = by_name.get(m.group(2))
            if t is not None and t in index and t != a:
                found.add(t)
        rec["callees"] = sorted(found)
    inverse = {}
    for a, rec in index.items():
        for c in rec["callees"]:
            inverse.setdefault(c, set()).add(a)
    for a, rec in index.items():
        rec["callers"] = sorted(set(rec["callers"]) | inverse.get(a, set()))

    # functions only reachable through vtables
    vt_member = {}
    for vt, members in vtables.items():
        for k, m in enumerate(members):
            vt_member.setdefault(m, []).append([vt, k])
    for a, rec in index.items():
        rec["in_vtables"] = vt_member.get(a, [])

    out = {f"{a:08x}": rec for a, rec in sorted(index.items())}
    for rec in out.values():
        for k in ("callers", "callees", "strings", "vtables", "globals"):
            rec[k] = [f"{x:08x}" for x in rec[k]]
        rec["in_vtables"] = [[f"{v:08x}", k] for v, k in rec["in_vtables"]]
    json.dump(out, open(os.path.join(ROOT, "index.json"), "w"), indent=0)
    json.dump({f"{k:08x}": [f"{x:08x}" for x in v] for k, v in sorted(vtables.items())},
              open(os.path.join(ROOT, "vtables.json"), "w"), indent=0)
    tot = sum(r["chars"] for r in out.values())
    print(f"{len(out)} functions, {len(vtables)} vtables, {tot / 1e6:.1f} MB decompiled text")


if __name__ == "__main__":
    main()
