"""Checks the camera build's output (harness_camera.exe, CAMERA_TEST; part 8e) against the missions.

    python check_camera.py MISSIONS_TXT_DIR HARNESS_OUTPUT

MISSIONS_TXT_DIR holds a .txt per mission ("mission NAME"). Per mission: a camera and a keeper line
with every check met. Prints "N models, M with differences" (mutfast.py reads it).
"""
import glob
import os
import re
import sys

LINES = {
    "camera": ("cmMake cmGetters cmSaveLoad cmRestart cmStart cmStartNone cmSettings cmSettingsFile cmStop cmPos cmDir "
               "cmBlend cmTarget cmFov cmViewDir").split(),
    "keeper": "kpMake kpRestore kpUpdate kpCar kpSubmarine kpPlayer".split(),
    "tactical": ("tcBasics tcOn tcOff tcMove tcLookAt tcMode tcFlight tcUpdate tcMoveMode tcTurn tcDrag tcUpDown "
                 "tcZoom tcFly tcHumans").split(),
    "update": "upGate upMouse upFirst upFollow upTail upCar upScripted upDebug".split(),
}


def main():
    missions, output = sys.argv[1], sys.argv[2]
    text = open(output).read()
    blocks = {b.split()[0]: b for b in text.split("== ")[1:] if b.split()}
    bad = 0
    total = 0
    for path in sorted(glob.glob(os.path.join(missions, "*.txt"))):
        name = open(path).readline().split()[1]
        total += 1
        diffs = []
        for line, checks in LINES.items():
            m = re.search(rf"{line}: (.*)", blocks.get(name, ""))
            if not m:
                diffs.append((line, "missing"))
            elif m.group(1).strip() != " ".join(f"{c} 1" for c in checks):
                diffs += [(f"{line}.{c}", 0) for c in checks if not re.search(rf"\b{c} 1\b", m.group(1))] or [(line, 0)]
        if diffs:
            bad += 1
            print(name, diffs)
    print(f"{total} models, {bad} with differences")


main()
