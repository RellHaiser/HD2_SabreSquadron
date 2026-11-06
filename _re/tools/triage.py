"""Triage of the game functions not ported yet: which the game can reach, how, and which subsystem they belong to.

Writes _re/triage.json (one row per function) and prints a summary. Reachability is worked out on the ORIGINAL program. The
edges come from the decompile export AND a byte scan of .text (rel32 calls and jumps, absolute addresses), so an edge the
export misses still counts; a chance byte match can only make a function look live, the safe direction.

  called    reachable from WinMain and the CRT's static initializers by code alone
  virtual   reachable only through a table (a vtable, a callback list) that live code references
  orphan    reachable only through a function pointer in data whose table no code references directly (a struct's field,
            say: its base address is what the code holds). Taken as live: it cannot be ruled out
  dead      none of those: only referenced by other dead code, or not at all

The byte scan needs .text: fetched from Ghidra once into --cache (default: %TEMP%\\hd2_text.bin).
Usage: python triage.py [--cache FILE]
"""
import argparse
import bisect
import json
import os
import re
import struct
import sys
import urllib.request

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
GAME_LO, GAME_HI = 0x401000, 0x7E2000
TEXT_LO, TEXT_END = 0x401000, 0x80F000
PROGRAM = "HD2_SabreSquadron_unpacked.exe"


def fetch_text(cache):
    if os.path.exists(cache) and os.path.getsize(cache) == TEXT_END - TEXT_LO:
        return open(cache, "rb").read()
    out = bytearray()
    step = 0x8000
    for a in range(TEXT_LO, TEXT_END, step):
        n = min(step, TEXT_END - a)
        url = "http://127.0.0.1:8089/read_memory?address=0x%x&length=%d&program=%s" % (a, n, PROGRAM)
        d = json.load(urllib.request.urlopen(url))
        out += bytes.fromhex(d["hex"].replace(" ", ""))
    assert len(out) == TEXT_END - TEXT_LO
    open(cache, "wb").write(out)
    return bytes(out)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--cache", default=os.path.join(os.environ.get("TEMP", "."), "hd2_text.bin"))
    args = ap.parse_args()
    os.chdir(ROOT)
    idx = {int(a, 16): r for a, r in json.load(open("index.json")).items()}
    starts = set(idx)

    hand, absorbed = set(), set()
    section = False
    for ln in open("ported.txt"):
        ln = ln.strip()
        if not ln:
            continue
        if ln.startswith("#"):
            section = ln.lower().startswith("# absorbed helpers")
            continue
        (absorbed if section else hand).add(int(ln, 16))
    done = hand | absorbed
    idioms = set()
    for ln in open(os.path.join("names", "zz_idioms.txt")):
        m = re.match(r"([0-9a-fA-F]{8})\s", ln)
        if m:
            idioms.add(int(m.group(1), 16))

    def is_game(a):
        name = idx[a].get("name") or ""
        return GAME_LO <= a < GAME_HI and not name.startswith(("Unwind@", "Catch@"))

    def is_todo(a):
        return (is_game(a) and a not in done and a not in idioms and
                not (idx[a].get("name") or "").startswith("empty_function"))

    todo = {a for a in idx if is_todo(a)}

    # ---- the data: pointers to functions; runs of them are tables (vtables when Ghidra says so). ----
    data_blobs = [(0x80F000, open("raw/rdata_0080f000.bin", "rb").read()), (0x832000, open("raw/data_00832000.bin", "rb").read())]
    data_ptrs = {}                                      # function -> data addresses holding it
    for base, blob in data_blobs:
        for off in range(0, len(blob) - 3, 4):
            v = struct.unpack_from("<I", blob, off)[0]
            if v in starts:
                data_ptrs.setdefault(v, []).append(base + off)

    # ---- the code's edges (from the decompile export, which records every reference; index for the rest). ----
    code_edges = {a: set() for a in idx}
    code_data_refs = {a: set() for a in idx}
    for a, r in idx.items():
        for c in r.get("callees") or []:
            c = int(c, 16)
            if c in starts:
                code_edges[a].add(c)
        path = os.path.join("decomp", "%08x.json" % a)
        if os.path.exists(path):
            d = json.load(open(path, encoding="utf-8"))
            for ref in d.get("refs") or []:
                m = re.match(r"0x([0-9a-fA-F]+)", str(ref))
                if not m:
                    continue
                v = int(m.group(1), 16)
                if v in starts and v != a:
                    code_edges[a].add(v)
                elif 0x80F000 <= v < 0x98E000:
                    code_data_refs[a].add(v)

    word_at = {}
    for base, blob in data_blobs:
        for off in range(0, len(blob) - 3, 4):
            word_at[base + off] = struct.unpack_from("<I", blob, off)[0]
    table_words = {w for w, v in word_at.items() if v in starts}

    # ---- the byte scan: every rel32 and absolute reference in .text, given to the function it lies in ----
    text = fetch_text(args.cache)
    ordered = sorted(idx)

    def owner(addr):
        i = bisect.bisect_right(ordered, addr) - 1
        if i < 0:
            return None
        a = ordered[i]
        return a if addr < a + max(idx[a].get("size", 0), 1) else None

    loose = {}                                          # target -> count of references from outside any known function
    byte_refs = {}                                      # target -> referencing addresses
    unpack = struct.Struct("<I").unpack_from
    unpacks = struct.Struct("<i").unpack_from
    size = len(text)
    for i in range(size - 4):
        v = unpack(text, i)[0]
        target = None
        if v in starts or v in table_words:
            target = v
        hits = [target] if target is not None else []
        b0 = text[i]
        if b0 in (0xE8, 0xE9) and i + 5 <= size:
            t = (TEXT_LO + i + 5 + unpacks(text, i + 1)[0]) & 0xFFFFFFFF
            if t in starts:
                hits.append(t)
        elif b0 == 0x0F and 0x80 <= text[i + 1] <= 0x8F and i + 6 <= size:
            t = (TEXT_LO + i + 6 + unpacks(text, i + 2)[0]) & 0xFFFFFFFF
            if t in starts:
                hits.append(t)
        for t in hits:
            where = TEXT_LO + i
            o = owner(where)
            if t in starts:
                byte_refs.setdefault(t, []).append(where)
            if o is None:
                loose[t] = loose.get(t, 0) + 1
            elif t in starts:
                if t != o:
                    code_edges[o].add(t)
            else:
                code_data_refs[o].add(t)

    # A table's slots from a referenced word on, to the next word code references or the run's end.
    referenced_words = {v for refs in code_data_refs.values() for v in refs if v in table_words}

    def table_slots(start):
        out = [word_at[start]]
        at = start + 4
        while at in table_words and at not in referenced_words:
            out.append(word_at[at])
            at += 4
        return out

    table_of = {v: table_slots(v) for v in referenced_words}

    # ---- the entry points: WinMain, the CRT's static initializers (the table that starts .data) ----
    roots = {a for a, r in idx.items() if (r.get("name") or "") == "WinMain"}
    if not roots:
        sys.exit("no WinMain")
    crt_table = []
    at = 0x832004
    while at in table_words:
        crt_table.append(word_at[at])
        at += 4
    roots |= set(crt_table)

    def closure(seed, use_tables):
        seen, work = set(), list(seed)
        while work:
            a = work.pop()
            if a in seen or a not in idx:
                continue
            seen.add(a)
            work.extend(code_edges[a])
            if use_tables:
                for v in code_data_refs[a]:
                    work.extend(table_of.get(v, ()))
        return seen

    called = closure(roots, False)
    virtual = closure(roots, True)
    # Pointers in data outside any table that code references, and not reached from one that is.
    covered = set()
    for v in referenced_words:
        at = v
        covered.add(at)
        at += 4
        while at in table_words and at not in referenced_words:
            covered.add(at)
            at += 4
    crt_words = {0x832004 + 4 * i for i in range(len(crt_table))}
    orphan_seeds = {word_at[w] for w in table_words - covered - crt_words}
    table = closure(roots | orphan_seeds, True)

    rows = []
    for a in sorted(todo):
        r = idx[a]
        how = "called" if a in called else "virtual" if a in virtual else "orphan" if a in table else "dead"
        refs = byte_refs.get(a, [])
        owners = sorted({owner(h) for h in refs}, key=lambda x: (x is None, x or 0))
        live_callers = [c for c in code_edges if a in code_edges[c] and c in table]
        todo_callers = [c for c in live_callers if c in todo]
        rows.append({
            "address": "%08x" % a, "name": r.get("name") or "", "size": r.get("size", 0), "how": how,
            "byte_refs": len(refs), "byte_ref_owners": ["%08x" % o if o is not None else "loose" for o in owners][:6],
            "data_refs": len(data_ptrs.get(a, [])), "live_callers": len(live_callers), "todo_callers": len(todo_callers),
            "loose_refs": loose.get(a, 0), "strings": (r.get("strings") or [])[:6], "in_vtables": (r.get("in_vtables") or [])[:3],
        })
    json.dump(rows, open("triage.json", "w"), indent=0)

    def line(label, sel):
        print("  %-30s %5d functions %8d bytes" % (label, len(sel), sum(x["size"] for x in sel)))

    print("entry points:", ", ".join("%08x %s" % (a, idx[a].get("name")) for a in sorted(roots)))
    print("todo: %d functions, %d bytes" % (len(rows), sum(x["size"] for x in rows)))
    line("called", [x for x in rows if x["how"] == "called"])
    line("virtual (a table live code uses)", [x for x in rows if x["how"] == "virtual"])
    line("orphan pointer (taken as live)", [x for x in rows if x["how"] == "orphan"])
    line("dead, referenced by loose bytes", [x for x in rows if x["how"] == "dead" and x["loose_refs"]])
    line("dead, by dead code only", [x for x in rows if x["how"] == "dead" and x["byte_refs"] and not x["loose_refs"]])
    line("dead, no references at all", [x for x in rows if x["how"] == "dead" and not x["byte_refs"]])
    in_idx = done & set(idx)
    print("sanity: ported functions the analysis calls dead: %d of %d" % (len(in_idx - table), len(in_idx)))
    json.dump(sorted("%08x" % a for a in in_idx - table), open(os.path.join(os.environ.get("TEMP", "."), "hd2_done_dead.json"), "w"))


if __name__ == "__main__":
    main()
