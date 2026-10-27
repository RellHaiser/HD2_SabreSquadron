"""Mark helpers that only hand-ported code calls as ported ("absorbed").

After a subsystem is hand-ported, its STL instantiations, inline accessors and
private helpers are replaced by std:: containers / header inlines, but tier 1
would still generate them. This finds every function in [LO, HI) whose callers
are all ported (repeated to a fixpoint) and appends it to _re/ported.txt.

  python absorb.py LO HI [--dry-run]      e.g. python absorb.py 4115a0 4310f0
"""
import json
import os
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def main():
    lo, hi = int(sys.argv[1], 16), int(sys.argv[2], 16)
    dry = "--dry-run" in sys.argv
    index = json.load(open(os.path.join(ROOT, "index.json")))
    syms = json.load(open(os.path.join(ROOT, "symbols.json")))
    path = os.path.join(ROOT, "ported.txt")
    ported = {ln.strip().lower().zfill(8) for ln in open(path) if ln.strip() and not ln.startswith("#")}

    absorbed = []
    done = set(ported)
    changed = True
    while changed:
        changed = False
        for addr, e in sorted(index.items()):
            if addr in done or not lo <= int(addr, 16) < hi:
                continue
            if e.get("in_vtables") or (e.get("name") or "").startswith(("Unwind@", "Catch@")):
                continue
            callers = e.get("callers") or []
            if callers and all(c in done for c in callers):
                absorbed.append(addr)
                done.add(addr)
                changed = True

    for addr in absorbed:
        print(addr, syms.get(addr, {}).get("name", index[addr]["name"]), index[addr]["size"])
    print(f"{len(absorbed)} absorbed")
    if absorbed and not dry:
        with open(path, "a", newline="\n") as fh:   # (the file is LF; text mode would add CR)
            fh.write(f"# absorbed helpers {lo:08x}-{hi:08x}\n" + "\n".join(absorbed) + "\n")


if __name__ == "__main__":
    main()
