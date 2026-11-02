"""Checks the cursor harness's output (harness_cursor.exe, part 9c-vi-a).

    python check_cursor.py MODELS_DIR HARNESS_OUTPUT

The harness runs once whatever the models; its "cursor:" line must have every check met. Prints
"1 models, M with differences" (mutfast.py reads it).
"""
import re
import sys

CHECKS = "cuMake cuUpdate cuClamp cuClicks cuQuery cuPick".split()


def main():
    text = open(sys.argv[2], errors="replace").read()
    m = re.search(r"^cursor: (.*)$", text, re.M)
    bad = not m
    if m:
        got = dict(re.findall(r"(\w+) (\d)", m.group(1)))
        bad = any(got.get(c) != "1" for c in CHECKS)
        if bad:
            print("('cursor'", [c for c in CHECKS if got.get(c) != "1"], ")")
    print("1 models, %d with differences" % (1 if bad else 0))


if __name__ == "__main__":
    main()
