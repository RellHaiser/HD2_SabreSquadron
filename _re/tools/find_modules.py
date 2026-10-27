"""List game modules: static initializers calling a module constructor as
`ctor(&global, id, "Name")`."""
import json
import os
import re

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DEC = os.path.join(ROOT, "decomp")
PAT = re.compile(r"(FUN_[0-9a-f]+|[\w:]+)\((?:\([\w ]+\*\))?&(\w+),(\d+|0x[0-9a-f]+),(s_\w+|\"[^\"]*\")\);")
rows = []
for fn in os.listdir(DEC):
    d = json.load(open(os.path.join(DEC, fn), encoding="utf-8"))
    if d.get("size", 999) > 60:
        continue
    m = PAT.search((d.get("decompiled_code") or "").replace("\r", ""))
    if m:
        rows.append((int(m.group(3), 0), d["address"], m.group(1), m.group(2), m.group(4)))
for r in sorted(rows):
    print(*r)
