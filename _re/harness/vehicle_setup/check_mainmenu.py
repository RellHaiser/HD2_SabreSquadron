"""Checks the factory build's main menu line (harness_factory.exe, FACTORY_TEST; part 9h).

    python check_mainmenu.py MISSIONS_TXT_DIR HARNESS_OUTPUT

Every mission's "mainmenu:" line (one at least) must have every check met; the text build's other lines are
not checked (check_text.py's). Prints "N models, M with differences" (mutfast.py reads it).
"""
import re
import sys

CHECKS = ("mmStart mmStop mmUpdate mmDrain mmMessage mmEscape mmSelect mmBuild mmDescribe mmMainShown "
          "pfDescribe pfRefresh pfShown pfList pfDifficulty pfMessage").split()


def main():
    text = open(sys.argv[2], errors="replace").read()
    lines = re.findall(r"^mainmenu: (.*)$", text, re.M)
    bad = 0 if lines else 1
    for line in lines:
        got = dict(re.findall(r"(\w+) (\d)", line))
        missed = [c for c in CHECKS if got.get(c) != "1"]
        if missed:
            bad += 1
            print("('mainmenu'", missed, ")")
    print("%d models, %d with differences" % (max(len(lines), 1), bad))


if __name__ == "__main__":
    main()
