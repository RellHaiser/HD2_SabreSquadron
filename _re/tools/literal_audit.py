"""String literals of the port against the strings of the original executable (through the Ghidra plugin, port 8089).

A literal the original does not have is often the port's own (a comment-like message, a fix), but a misread name fails silently at run time: the game looks a
frame or a file up and carries on without it. Reported only when the original has a near twin: the same text but for case, '_' for ' ', or a '#' or '!' prefix.

Usage (repository root, Ghidra running): python _re/tools/literal_audit.py
"""
import glob
import json
import re
import urllib.request

PROGRAM = "HD2_SabreSquadron_unpacked.exe"
LITERAL = re.compile(r'"((?:[^"\\\n]|\\.)*)"')


def original_strings():
    found = set()
    offset = 0
    while True:
        url = "http://127.0.0.1:8089/list_strings?program=%s&limit=2000&offset=%d" % (PROGRAM, offset)
        with urllib.request.urlopen(url, timeout=120) as response:
            data = json.load(response)
        items = data.get("strings") or data.get("matches") or []
        if not items:
            return found
        found.update(item["value"] for item in items)
        offset += len(items)


def key(text):
    return re.sub(r"[ _]", "", text.lstrip("#!")).lower()


def main():
    original = original_strings()
    twins = {}
    for text in original:
        twins.setdefault(key(text), set()).add(text)
    print(len(original), "strings in the original")
    checked = reported = 0
    for path in sorted(glob.glob("src/**/*.cpp", recursive=True) + glob.glob("src/**/*.h", recursive=True)):
        if "tier1" in path:
            continue
        for number, line in enumerate(open(path, encoding="utf-8", errors="replace"), 1):
            line = line.split("//")[0] if '"' not in line.split("//")[0][-1:] else line
            for literal in LITERAL.findall(line):
                if len(literal) < 3 or "\\" in literal or "%" in literal or literal in original:
                    continue
                checked += 1
                near = twins.get(key(literal))
                if near:
                    reported += 1
                    print("%s:%d %r: the original has %s" % (path, number, literal, sorted(near)))
    print(checked, "literals not in the original,", reported, "with a near twin")


if __name__ == "__main__":
    main()
