"""Checks the seat weapon build's output (harness_sw.exe, REAL_SEAT_WEAPON) against the models.

    python check_sw.py MODELS_TXT_DIR HARNESS_OUTPUT

Per model: every check of SeatWeaponTest met, and as many weapons set up as the model's frames
describe (a seat <nn> with a "SEAT<nn>" dummy, "HORT<nn>*" and "VERT<nn>*" dummies, a
"BARREL<nn>_00*" visual with an "itemid" property, and a "BARREL<nn>_<k>*" visual holding a
"SHDUM*" dummy), the other seats failing. Prints "N models, M with differences" (mutfast.py reads it).
"""
import fnmatch
import glob
import os
import re
import sys

VIS, DUMMY = 1, 6
CHECKS = ("ctor init props barrels rest sounds again accessors gunner shell save load stop restart release failed "
          "messages update turn view controls fire shots reload aim").split()


def load(path):
    frames = []
    for line in open(path):
        if line.startswith("f "):
            _, t, parent, name = line.rstrip("\n").split(" ", 3)
            frames.append({"type": int(t), "parent": int(parent), "name": name, "props": ""})
        elif line.startswith("P ") and frames:
            h = line[2:].strip()
            frames[-1]["props"] = "" if h == "-" else bytes.fromhex(h).decode("latin1")
    return frames


def descendants(frames, index):
    out, todo = [], [index]
    while todo:
        p = todo.pop()
        for i, f in enumerate(frames):
            if f["parent"] == p + 1:
                out.append(i)
                todo.append(i)
    return out


def expected(frames):
    """(seats, weapons) the frames describe."""
    def find(mask, kind, among=None):
        for i in (range(len(frames)) if among is None else among):
            if frames[i]["type"] == kind and fnmatch.fnmatchcase(frames[i]["name"].upper(), mask.upper()):
                return i
        return None

    seats = sorted(int(f["name"][4:6]) for f in frames
                   if f["type"] == DUMMY and re.fullmatch(r"SEAT\d\d", f["name"]))
    weapons = 0
    for s in seats:
        hort, vert = find(f"HORT{s:02d}*", DUMMY), find(f"VERT{s:02d}*", DUMMY)
        barrel0 = find(f"BARREL{s:02d}_00*", VIS)
        props = frames[barrel0]["props"].lower() if barrel0 is not None else ""
        if hort is None or vert is None or "itemid" not in props:
            continue
        kept = 0
        for k in range(100):
            b = find(f"BARREL{s:02d}_{k:02d}*", VIS)
            if b is None:
                break
            kept += find("SHDUM*", DUMMY, descendants(frames, b)) is not None
        weapons += kept > 0
    return len(seats), weapons


def main():
    models, output = sys.argv[1], sys.argv[2]
    text = open(output).read()
    blocks = {b.split()[0].replace(".4ds", ""): b for b in text.split("== ")[1:]}
    bad = 0
    total = 0
    for path in sorted(glob.glob(os.path.join(models, "*.txt"))):
        name = open(path).readline().split()[1].replace(".4ds", "")
        total += 1
        seats, weapons = expected(load(path))
        block = blocks.get(name, "")
        m = re.search(r"seat weapon: weapons (\d+) failed (\d+) (.*)", block)
        want = " ".join(f"{c} 1" for c in CHECKS)
        diffs = []
        if not m:
            diffs.append(("seat weapon", "missing"))
        else:
            if (int(m.group(1)), int(m.group(1)) + int(m.group(2))) != (weapons, seats):
                diffs.append(("weapons", (weapons, seats), (int(m.group(1)), int(m.group(1)) + int(m.group(2)))))
            if m.group(3).strip() != want:
                diffs += [(c, 0) for c in CHECKS if not re.search(rf"\b{c} 1\b", m.group(3))] or [("checks", 0)]
        if diffs:
            bad += 1
            print(name, diffs)
    print(total, "models,", bad, "with differences")


main()
