"""The address ledger: every original game function and what became of it in the port.

    python tools/address_ledger.py [--out FILE.csv] [--list DISPOSITION] [--quiet]

One row per function of HD2_SabreSquadron.exe's game range (0x401000..0x7E2000, unwind / catch
funclets left out, as port_stats.py counts), from _re/index.json, _re/ported.txt,
_re/names/zz_idioms.txt, _re/triage.json and the sources' address comments:

  disposition   hand          ported by hand (ported.txt outside an "absorbed helpers" section)
                absorbed      a helper only ported code called, folded into its callers (absorb.py)
                idiom         a compiler / MSVC6 STL shape (stl_idioms.py), replaced by the port's std::
                empty         an empty function
                open:<bucket> not accounted for yet; the bucket is triage.py's (live, tiny, static,
                              dead, dead-eh?) or "untriaged" when triage.json has no row for it
  src           the sources (src/, not the generated src/tier1) naming the address exactly, with the
                first line: "file:line" joined by spaces. Only exact matches: no nearest-address credit.
  section       the ported.txt section header the address is listed under. Its wording matters:
                some "hand" sections list code marked done without a port ("unreferenced ... not
                ported", static-guard thunks, STL shapes), so tier 1 stops generating it
  review        the reviewed disposition from _re/ledger_review.txt, when there is one

_re/ledger_review.txt holds the reviewed rows ("ADDRESS DISPOSITION free text", # comments), for
step 2 of the coverage plan: an open row reviewed as e.g. "absorbed-by 0x00412340", "std",
"dead", "intended-change", or a hand row whose source is found elsewhere. The summary counts
reviewed rows separately; a review never changes the row's measured disposition.

Default output: _re/coverage/ledger.csv. Prints a summary (functions and original bytes per
disposition, hand / absorbed rows without an exact source address).
"""
import argparse
import collections
import csv
import json
import os
import re

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
RE = os.path.join(ROOT, "_re")
SRC = os.path.join(ROOT, "src")
GAME_LO, GAME_HI = 0x401000, 0x7E2000
SRC_DIRS = ("common", "game", "ls3d", "net", "script")


def read_ported():
    """The hand / absorbed sets as port_stats.py reads them, and each address's section header."""
    hand, absorbed, section = set(), set(), {}
    section_absorbed = False
    header = ""
    for ln in open(os.path.join(RE, "ported.txt")):
        ln = ln.strip()
        if not ln:
            continue
        if ln.startswith("#"):
            section_absorbed = ln.lower().startswith("# absorbed helpers")
            header = ln.lstrip("# ")
            continue
        a = int(ln, 16)
        (absorbed if section_absorbed else hand).add(a)
        section.setdefault(a, header)
    return hand, absorbed - hand, section


def read_idioms():
    out = set()
    for ln in open(os.path.join(RE, "names", "zz_idioms.txt")):
        m = re.match(r"([0-9a-fA-F]{8})\s", ln)
        if m:
            out.add(int(m.group(1), 16))
    return out


def read_reviews():
    path = os.path.join(RE, "ledger_review.txt")
    out = {}
    if os.path.exists(path):
        for ln in open(path, encoding="utf-8"):
            ln = ln.split("#", 1)[0].strip()
            if ln:
                a, rest = (ln.split(None, 1) + [""])[:2]
                out[int(a, 16)] = rest
    return out


def source_refs(wanted):
    """Address -> ["file:line", ...] for every exact `0x00XXXXXX` in the sources."""
    refs = collections.defaultdict(list)
    for d in SRC_DIRS:
        for root, _, files in os.walk(os.path.join(SRC, d)):
            for f in sorted(files):
                if not f.endswith((".cpp", ".h", ".inc")):
                    continue
                path = os.path.join(root, f)
                rel = os.path.relpath(path, ROOT).replace(os.sep, "/")
                with open(path, encoding="utf-8", errors="replace") as fh:
                    for no, line in enumerate(fh, 1):
                        for m in re.finditer(r"0x(00[4-7][0-9a-fA-F]{5})\b", line):
                            a = int(m.group(1), 16)
                            if a in wanted and (not refs[a] or not refs[a][-1].startswith(rel + ":")):
                                refs[a].append("%s:%d" % (rel, no))
    return refs


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--out", default=os.path.join(RE, "coverage", "ledger.csv"))
    ap.add_argument("--list", help="print the rows of one disposition (e.g. open:live, hand-nosrc)")
    ap.add_argument("--quiet", action="store_true")
    a = ap.parse_args()

    idx = {int(k, 16): r for k, r in json.load(open(os.path.join(RE, "index.json"))).items()}
    hand, absorbed, section = read_ported()
    idioms = read_idioms()
    triage = {int(t["address"], 16): t for t in json.load(open(os.path.join(RE, "triage.json")))}
    reviews = read_reviews()

    game = {k: r for k, r in idx.items() if GAME_LO <= k < GAME_HI
            and not (r.get("name") or "").startswith(("Unwind@", "Catch@"))}
    refs = source_refs(set(game))

    rows = []
    for k in sorted(game):
        r = game[k]
        name = r.get("name") or ""
        if k in hand:
            disp = "hand"
        elif k in absorbed:
            disp = "absorbed"
        elif k in idioms:
            disp = "idiom"
        elif name.startswith("empty_function"):
            disp = "empty"
        else:
            disp = "open:" + (triage[k]["bucket"] if k in triage else "untriaged")
        rows.append(dict(address="%08x" % k, name=name, size=r.get("size", 0), disposition=disp,
                         triage=triage[k]["bucket"] if k in triage else "",
                         src=" ".join(refs.get(k, [])), section=section.get(k, ""), review=reviews.get(k, "")))

    os.makedirs(os.path.dirname(a.out), exist_ok=True)
    with open(a.out, "w", newline="", encoding="utf-8") as f:
        w = csv.DictWriter(f, fieldnames=list(rows[0]), lineterminator="\n")
        w.writeheader()
        w.writerows(rows)

    if a.list:
        for row in rows:
            if row["disposition"] == a.list or (a.list == "hand-nosrc" and row["disposition"] == "hand"
                                                 and not row["src"]):
                print(row["address"], row["size"], row["name"], row["src"], row["review"])
        return

    if a.quiet:
        return
    total = [len(rows), sum(r["size"] for r in rows)]
    print("%d functions, %d original bytes (game range, funclets left out)" % tuple(total))
    by = collections.OrderedDict()
    order = ["hand", "absorbed", "idiom", "empty"]
    for r in sorted(rows, key=lambda r: (order.index(r["disposition"]) if r["disposition"] in order else 9,
                                         r["disposition"])):
        t = by.setdefault(r["disposition"], [0, 0, 0])
        t[0] += 1
        t[1] += r["size"]
        t[2] += bool(r["review"])
    for d, (n, b, rev) in by.items():
        print("  %-16s %6d (%5.2f %%) %9d bytes (%5.2f %%)  reviewed %d" % (d, n, 100 * n / total[0], b,
                                                                            100 * b / total[1], rev))
    for d in ("hand", "absorbed"):
        no = [r for r in rows if r["disposition"] == d and not r["src"]]
        print("%s rows without an exact source address: %d (%d bytes)" % (d, len(no), sum(r["size"] for r in no)))
    print("wrote", a.out)


if __name__ == "__main__":
    main()
