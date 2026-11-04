"""Checks `harness dump` against an independent reading of the same mpmaplist.txt.

  python check_dump.py MPMAPLIST DUMP

The reference walks the elements in document order (comments skipped) and applies the rules of
C_MpGameSetup::LoadMapList (0x006189f0) as read from the asm: the GAMESTYLE types, the MAP fields
(" UNKNOWN " without a name or dir), OBJECTIVE, AIRCRAFTATACK, BALANCED, ALLOWEDITEMS 0.1 (its ITEMs),
ALLOWEDPRESETS (the element's text), ZONEGRAPH; any other element ends the list.
"""
import re
import sys

TAG = re.compile(r'<(/?)([^\s/>]+)((?:\s+[^\s=]+\s*=\s*"[^"]*")*)\s*(/?)>', re.S)
ATTR = re.compile(r'([^\s=]+)\s*=\s*"([^"]*)"')
DEFAULT_RULES = [
    [32, 20, 0, 3, 10, 5, 1, 0, 0, 0, 5, 100, 1, 1, 1, 1, 0, 3, -1, 1, 0, 1],
    [32, 0, 30, 3, 10, 30, 1, 1, 1, 0, 5, 100, 1, 1, 1, 1, 0, 3, -1, 1, 0, 1],
    [32, 0, 5, 3, 10, 0, 0, 1, 1, 0, 5, 100, 1, 1, 1, 1, 0, 3, -1, 1, 0, 1],
    [6, 0, 0, 3, 10, 5, 1, 1, 0, 0, 5, 100, 1, 1, 1, 1, 0, 3, 30, 1, 0, 1],
]
STYLES = {"deathmatch": 0, "teamplay": 1, "hd2multiplayer": 2, "cooperative": 3}


def elements(text):
    text = re.sub(r"<!--.*?-->", "", text, flags=re.S)
    out, stack = [], []
    pos = 0
    for m in TAG.finditer(text):
        closing, name, attrs, empty = m.groups()
        if closing:
            el = stack.pop()
            if not el["children"]:
                el["text"] = text[el["end"]:m.start()]
            continue
        el = {"name": name, "attrs": dict(ATTR.findall(attrs)), "children": [], "text": "", "end": m.end()}
        if stack:
            stack[-1]["children"].append(el)
        out.append(el)
        if not empty:
            stack.append(el)
    return out


def atoi(s):
    m = re.match(r"\s*([+-]?\d+)", s or "")
    return int(m.group(1)) if m else 0


def reference(text):
    els = elements(text)
    lines = []
    if not els or els[0]["name"] != "MAP_LIST":
        return lines
    groups = [[] for _ in range(4)]
    group = None
    m = None
    i = 1
    while i < len(els):
        e = els[i]
        a = e["attrs"]
        n = e["name"]
        if n == "GAMESTYLE":
            if a.get("type") not in STYLES:
                break
            group = groups[STYLES[a["type"]]]
        elif n == "MAP":
            if group is None:
                break
            if "name" in a and "dir" in a:
                m = {"name": a["name"], "dir": a["dir"], "front": 1 if a.get("mode", "").upper() == "FRONTLINE" else 0}
            else:
                m = {"name": " UNKNOWN ", "dir": " UNKNOWN ", "front": 0}
            m.update(axis=-1, allied=-1, baxis=-1, ballied=-1, obj=[], items=[], presets="",
                     desc=atoi(a["desc"]) if "desc" in a else -1)
            group.append(m)
        elif n == "OBJECTIVE":
            g = lambda k: atoi(a[k]) if k in a else -1
            vip = a.get("type", "").upper() == "VIP" if "type" in a else False
            side = a.get("side")
            team = 1 if side is None or side.upper() == "ALLIED" else 0 if side.upper() == "AXIS" else -1
            m["obj"].append((g("axis_text"), g("allied_text"), g("value"), 1 if vip else -1, team,
                             g("vip_uniform_id") if vip else -1))
        elif n == "AIRCRAFTATACK":
            if "allied" in a:
                m["allied"] = atoi(a["allied"])
            if "axis" in a:
                m["axis"] = atoi(a["axis"])
        elif n == "BALANCED":
            if "allied" not in a or "axis" not in a:
                break
            m["ballied"], m["baxis"] = atoi(a["allied"]), atoi(a["axis"])
        elif n == "ALLOWEDITEMS":
            v = float(a["version"]) if "version" in a else 0.0
            assert abs(v - 0.1) < 1e-6, "only version 0.1 is modelled"
            j = i + 1
            while j < len(els) and els[j] in all_descendants(e):
                c = els[j]
                if c["name"] == "ITEM":
                    g = lambda k: atoi(c["attrs"][k]) if k in c["attrs"] else -1
                    m["items"].append((g("item_id"), g("item_num"), g("ammo_num")))
                j += 1
            i = j
            continue
        elif n == "ALLOWEDPRESETS":
            m["presets"] = e["text"]
        elif n != "ZONEGRAPH":
            break
        i += 1
    for t, maps in enumerate(groups):
        lines.append("type %d: %d maps" % (t, len(maps)))
        for m in maps:
            lines.append("map '%s' '%s' 0 %d %d %d %d %d %d" % (m["name"], m["dir"], m["axis"], m["allied"],
                                                              m["baxis"], m["ballied"], m["front"], m["desc"]))
            lines += ["  objective %d %d %d %d %d %d" % o for o in m["obj"]]
            lines += ["  item %d %d %d" % it for it in m["items"]]
            lines.append("  presets '%s'" % m["presets"])
    # The defaults after the list: the rules (0x006189f0), no rotation.
    for g, rule in enumerate(DEFAULT_RULES):
        lines.append("rules %d: %s" % (g, " ".join(str(v) for v in rule)))
    lines += ["rotation %d type %d:" % (r, t) for r in range(5) for t in range(4)]
    return lines


def all_descendants(e):
    out = []
    for c in e["children"]:
        out.append(c)
        out += all_descendants(c)
    return out


def main():
    text = open(sys.argv[1], encoding="latin-1").read()
    want = reference(text)
    got = [l for l in open(sys.argv[2], encoding="latin-1").read().splitlines()
           if l.startswith(("type", "map", "  ", "rules", "rotation"))]
    if got == want:
        print("dump: %d lines match (%d maps)" % (len(want), sum(1 for l in want if l.startswith("map"))))
        return 0
    for k, (g, w) in enumerate(zip(got, want)):
        if g != w:
            print("line %d: got %r want %r" % (k, g, w))
            break
    print("dump: MISMATCH (%d vs %d lines)" % (len(got), len(want)))
    return 1


if __name__ == "__main__":
    sys.exit(main())
