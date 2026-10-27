"""Print the address-ordered string anchors per function to reveal .obj boundaries.

MSVC emits functions and their string literals in object-file order, so walking
functions by address and printing the strings each one references shows where one
source file ends and the next begins.

  python segment.py LO HI            anchors for functions in [LO, HI)
  python segment.py --gaps           list large jumps in the string-address sequence
"""
import json
import os
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def load():
    idx = json.load(open(os.path.join(ROOT, "index.json")))
    st = {int(s["address"], 16): s["value"] for s in
          json.load(open(os.path.join(ROOT, "raw", "strings_all.json")))["strings"]}
    return idx, st


def anchors(lo, hi):
    idx, st = load()
    for a in sorted(idx):
        ai = int(a, 16)
        if not (lo <= ai < hi):
            continue
        r = idx[a]
        ss = [(int(s, 16), st[int(s, 16)]) for s in r["strings"]]
        if not ss:
            continue
        txt = " | ".join(f"{sa:06x}:{v[:34]!r}" for sa, v in ss[:4])
        print(f"{a} sz={r['size']:<5} {txt}")


def gaps():
    idx, st = load()
    prev = None
    for a in sorted(idx):
        r = idx[a]
        if not r["strings"]:
            continue
        s = min(int(x, 16) for x in r["strings"])
        if prev and abs(s - prev[1]) > 0x400:
            print(f"{prev[0]} -> {a}: strings {prev[1]:06x} -> {s:06x}  {st[s][:40]!r}")
        prev = (a, s)


if __name__ == "__main__":
    if sys.argv[1] == "--gaps":
        gaps()
    else:
        anchors(int(sys.argv[1], 16), int(sys.argv[2], 16))
