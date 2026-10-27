"""Port progress by function count and code size (from _re/index.json and _re/ported.txt)."""
import json
import os
import re

RE = r"G:\Programming\HD2 Source Port\C++\_re"
GAME_LO, GAME_HI = 0x401000, 0x7E2000

idx = {int(a, 16): r for a, r in json.load(open(os.path.join(RE, "index.json"))).items()}

# ported.txt: sections; "# absorbed helpers" ones are helpers marked by absorb.py.
hand, absorbed = set(), set()
section_absorbed = False
for ln in open(os.path.join(RE, "ported.txt")):
    ln = ln.strip()
    if not ln:
        continue
    if ln.startswith("#"):
        section_absorbed = ln.lower().startswith("# absorbed helpers")
        continue
    a = int(ln, 16)
    (absorbed if section_absorbed else hand).add(a)
absorbed -= hand

idioms = set()
for ln in open(os.path.join(RE, "names", "zz_idioms.txt")):
    m = re.match(r"([0-9a-fA-F]{8})\s", ln)
    if m:
        idioms.add(int(m.group(1), 16))

def kind(a, r):
    name = r.get("name") or ""
    if name.startswith(("Unwind@", "Catch@")):
        return "eh"
    if not (GAME_LO <= a < GAME_HI):
        return "lib"
    return "game"

rows = {}
for a, r in idx.items():
    k = kind(a, r)
    if a in hand:
        s = "hand"
    elif a in absorbed:
        s = "absorbed"
    elif a in idioms:
        s = "idiom"
    elif (r.get("name") or "").startswith("empty_function"):
        s = "empty"
    else:
        s = "todo"
    t = rows.setdefault((k, s), [0, 0])
    t[0] += 1
    t[1] += r.get("size", 0)

def get(k, s):
    return rows.get((k, s), [0, 0])

print("all functions:", len(idx), "bytes:", sum(r.get("size", 0) for r in idx.values()))
for k in ("game", "lib", "eh"):
    tot = [sum(v[i] for (kk, _), v in rows.items() if kk == k) for i in (0, 1)]
    print(f"\n[{k}] {tot[0]} functions, {tot[1]} bytes")
    for s in ("hand", "absorbed", "idiom", "empty", "todo"):
        n, b = get(k, s)
        if n:
            print(f"  {s:9} {n:6} ({100*n/tot[0]:5.1f} %)  {b:9} bytes ({100*b/tot[1]:5.1f} %)")

# Game code: done = hand + absorbed; remaining real work = todo (excluding idioms / empty).
g = {s: get("game", s) for s in ("hand", "absorbed", "idiom", "empty", "todo")}
tot = [sum(v[i] for v in g.values()) for i in (0, 1)]
done = [g["hand"][i] + g["absorbed"][i] for i in (0, 1)]
print(f"\ngame code done (hand + absorbed): {done[0]} / {tot[0]} functions ({100*done[0]/tot[0]:.1f} %), "
      f"{done[1]} / {tot[1]} bytes ({100*done[1]/tot[1]:.1f} %)")
# The largest remaining functions and named ones.
todo = sorted(((r.get("size", 0), a, r.get("name")) for a, r in idx.items()
               if kind(a, r) == "game" and a not in hand and a not in absorbed and a not in idioms
               and not (r.get("name") or "").startswith("empty_function")), reverse=True)
named = sum(1 for _, _, n in todo if n and not n.startswith("FUN_"))
print(f"remaining game functions: {len(todo)} ({named} named, {len(todo) - named} FUN_)")
# Size buckets of what is left.
for lo, hi in ((0, 64), (64, 256), (256, 1024), (1024, 4096), (4096, 1 << 30)):
    sel = [s for s, _, _ in todo if lo <= s < hi]
    print(f"  {lo:5}..{hi if hi < 1 << 30 else '':>5}: {len(sel):5} functions {sum(sel):8} bytes")
