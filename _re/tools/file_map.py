"""One line per original source file (static-initializer boundaries): range,
function count, code bytes, classes and a few string hints. Writes _re/file_map.txt."""
import json
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import gen_tier1  # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def main():
    index = json.load(open(os.path.join(ROOT, "index.json")))
    funcs = {int(a, 16): r for a, r in index.items()}
    starts = gen_tier1.file_boundaries(funcs)
    st = {int(s["address"], 16): s["value"] for s in
          json.load(open(os.path.join(ROOT, "raw", "strings_all.json")))["strings"]}
    members = json.load(open(os.path.join(ROOT, "auto_members.json")))["members"]
    ported = gen_tier1.ported_set()
    groups = {}
    for a in sorted(funcs):
        if not (gen_tier1.GAME_LO <= a < gen_tier1.GAME_HI):
            continue
        if funcs[a]["name"] and funcs[a]["name"].startswith(("Unwind@", "Catch@")):
            continue
        s = max([x for x in starts if x <= a], default=gen_tier1.GAME_LO)
        groups.setdefault(s, []).append(a)
    lines = []
    for s, fs in sorted(groups.items()):
        size = sum(funcs[a]["size"] for a in fs)
        done = sum(1 for a in fs if f"{a:08x}" in ported)
        classes = sorted({members.get(f"{a:08x}") for a in fs} - {None})
        strs = []
        for a in fs:
            for x in funcs[a]["strings"]:
                v = st.get(int(x, 16), "")
                if len(v) > 4 and v not in strs:
                    strs.append(v)
        hint = " | ".join(v[:28] for v in strs[:5])
        lines.append(f"{s:08x}-{fs[-1]:08x} n={len(fs):<4} bytes={size:<6} ported={done:<3} "
                     f"cls={','.join(c.replace('C_auto_00', '@') for c in classes[:4])} :: {hint}")
    open(os.path.join(ROOT, "file_map.txt"), "w", encoding="utf-8").write("\n".join(lines))
    print(f"{len(lines)} files")


if __name__ == "__main__":
    main()
