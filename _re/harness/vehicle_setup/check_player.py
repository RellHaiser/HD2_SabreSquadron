"""Checks the factory build's lines of the in-game menu's player screen (harness_factory.exe,
FACTORY_TEST; part 9e-xiii): the "gamemenu:", "screens:" (the description popup) and
"equipmentdesc:" (the slot 0x60's callback) lines, by their checkers.

    python check_player.py MISSIONS_TXT_DIR HARNESS_OUTPUT

Every mission's line of each (one at least) must have every check met. Prints "N models, M with
differences" (mutfast.py reads it).
"""
import re
import sys

import check_equipmentdesc
import check_gamemenu
import check_screen

LINES = (("gamemenu", check_gamemenu.CHECKS), ("screens", check_screen.CHECKS),
         ("equipmentdesc", check_equipmentdesc.CHECKS))


def main():
    text = open(sys.argv[2], errors="replace").read()
    count, bad = 1, 0
    for name, checks in LINES:
        lines = re.findall(r"^%s: (.*)$" % name, text, re.M)
        if not lines:
            bad += 1
            print("(%r missing)" % name)
        for line in lines:
            got = dict(re.findall(r"(\w+) (\d)", line))
            missed = [c for c in checks if got.get(c) != "1"]
            if missed:
                bad += 1
                print("(%r" % name, missed, ")")
        count = max(count, len(lines))
    print("%d models, %d with differences" % (count, bad))


if __name__ == "__main__":
    main()
