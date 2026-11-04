"""Checks the text database's test (harness_texts.exe, build_texts.bat; part 9g-iii).

    python check_gametexts.py MISSIONS_TXT_DIR HARNESS_OUTPUT

The "gametexts:" line must have every check met (the models are not read). Prints "N models, M with
differences" (mutfast.py reads it).
"""
import re
import sys

CHECKS = "gtAdd gtFind gtGet gtParse gtLoad gtBom gtClear".split()


def main():
    text = open(sys.argv[2], errors="replace").read()
    lines = re.findall(r"^gametexts: (.*)$", text, re.M)
    bad = 0 if lines else 1
    for line in lines:
        got = dict(re.findall(r"(\w+) (\d)", line))
        missed = [c for c in CHECKS if got.get(c) != "1"]
        if missed:
            bad += 1
            print("('gametexts'", missed, ")")
    print("%d models, %d with differences" % (max(len(lines), 1), bad))


if __name__ == "__main__":
    main()
