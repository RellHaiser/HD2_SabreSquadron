"""Rename an identifier everywhere in the hand-written sources and name lists.

  python rename.py OLD NEW [OLD NEW ...]

Word-boundary replacement in src/**/*.h|cpp (except src/tier1) and _re/names/*.txt.
Qualified names (A::B) are matched literally. Use this when a placeholder such as
fn_4d6900 or C_Class_8111e0 gets its real name.
"""
import glob
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC = os.path.join(os.path.dirname(ROOT), "src")


def files():
    for p in glob.glob(os.path.join(SRC, "**", "*.*"), recursive=True):
        if os.sep + "tier1" + os.sep in p or not p.endswith((".h", ".cpp")):
            continue
        yield p
    yield from glob.glob(os.path.join(ROOT, "names", "*.txt"))


def main():
    args = sys.argv[1:]
    pairs = list(zip(args[0::2], args[1::2]))
    for path in files():
        text = open(path, encoding="utf-8").read()
        new = text
        for old, rep in pairs:
            new = re.sub(r"(?<![\w:])" + re.escape(old) + r"(?!\w)", rep, new)
        if new != text:
            open(path, "w", encoding="utf-8", newline="\n").write(new)   # (text mode would write CRLF)
            print("updated", os.path.relpath(path, os.path.dirname(ROOT)))


if __name__ == "__main__":
    main()
