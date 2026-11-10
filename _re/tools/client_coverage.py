"""Function-entry coverage of the client: which of the port's functions a run entered.

    python _re/tools/client_coverage.py [--exe EXE] [--out DIR] COV_FILE [COV_FILE ...]

EXE: the coverage build (default %TEMP%\\hd2_game_cov\\HD2_Port.exe; build_game.py --coverage), with its
.map beside it. COV_FILE: HD2_Port.cov files the runs wrote (several are ORed; each must be of this
exe: its header names the image's TimeDateStamp and SizeOfImage).

The functions: every function symbol ('f') of the map, public or static, whose object is a source of
src/ (not the C runtime, the DLLs' import stubs or penter.obj). Each is checked in the exe for the
`call __penter` /Gh puts first:
  instrumented     the call is there: entered or not entered is measured
  not instrumented the call is missing (naked functions, thunks the compiler made): never measured,
                   listed apart, never counted as entered
Kinds: "port" (the port's own code), "std" (std:: instantiations emitted in the port's objects),
"generated" (compiler-made: ??_G / ??_E deleting destructors, ??_R / ??__E dynamic initialisers ...),
"funclet" (exception unwind / handler funclets: never instrumented by /Gh; reached only by a throw).
Inline functions that were inlined everywhere have no symbol and are not in the count: execution of
their bodies is the callers'. Lines and branches are not measured by this build.

Writes DIR/client_coverage.json (per function) and DIR/client_coverage.md (per object), default
beside the first COV_FILE; prints the totals.
"""
import argparse
import collections
import json
import os
import re
import struct
import sys
import tempfile

ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))
SRC = os.path.join(ROOT, "src")
HEADER = struct.Struct("<8sIII12x")


def pe_info(path):
    data = open(path, "rb").read()
    pe = struct.unpack_from("<I", data, 0x3C)[0]
    stamp = struct.unpack_from("<I", data, pe + 8)[0]
    nsec = struct.unpack_from("<H", data, pe + 6)[0]
    optsize = struct.unpack_from("<H", data, pe + 20)[0]
    opt = pe + 24
    base = struct.unpack_from("<I", data, opt + 28)[0]
    size = struct.unpack_from("<I", data, opt + 56)[0]
    sections = []
    for i in range(nsec):
        s = opt + optsize + 40 * i
        vsize, va, rawsize, raw = struct.unpack_from("<IIII", data, s + 8)
        sections.append((va, max(vsize, rawsize), raw))

    def read(rva, n):
        for va, sz, raw in sections:
            if va <= rva < va + sz:
                return data[raw + rva - va: raw + rva - va + n]
        return b""
    return dict(stamp=stamp, size=size, base=base, read=read)


def source_objects():
    objs = {}
    for dp, dirs, fns in os.walk(SRC):
        dirs[:] = [d for d in dirs if d != "tier1"]
        for f in fns:
            if f.endswith(".cpp"):
                rel = os.path.relpath(os.path.join(dp, f), ROOT).replace(os.sep, "/")
                objs[os.path.splitext(f)[0].lower() + ".obj"] = rel
    return objs


def map_functions(path, base, objs):
    """(rva, symbol, object) of every function symbol of a source object; the __penter rva."""
    out, penter = {}, None
    for line in open(path, encoding="latin-1"):
        m = re.match(r"\s*[0-9a-f]{4}:[0-9a-f]{8}\s+(\S+)\s+([0-9a-f]{8})\s+(f\s+)?(?:i\s+)?(\S+)\s*$", line)
        if not m:
            continue
        sym, addr, isfunc, obj = m.group(1), int(m.group(2), 16), m.group(3), m.group(4)
        if sym == "__penter":
            penter = addr - base
        if not isfunc:
            continue
        # (A library's object is "lib:obj": the CRT's clock.obj is not src/game/clock.cpp.)
        if ":" in obj:
            continue
        obj = obj.lower()
        if obj in objs:
            out.setdefault(addr - base, (sym, obj))
    return out, penter


def kind(sym):
    if sym.startswith(("__unwindfunclet", "__ehhandler", "__catch")):
        return "funclet"
    if sym.startswith(("??_G", "??_E", "??_R", "??__E", "??__F", "??_7", "??_L", "??_M", "__", "$")) or "$" in sym[:3]:
        return "generated"
    if "@std@@" in sym or sym.startswith("??$") and "@std@" in sym:
        return "std"
    return "port"


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("cov", nargs="+")
    ap.add_argument("--exe", default=os.path.join(tempfile.gettempdir(), "hd2_game_cov", "HD2_Port.exe"))
    ap.add_argument("--out")
    a = ap.parse_args()

    pe = pe_info(a.exe)
    objs = source_objects()
    funcs, penter = map_functions(a.exe[:-4] + ".map", pe["base"], objs)
    if penter is None:
        sys.exit("no __penter in the map: not a coverage build")

    hits = bytearray(pe["size"])
    for path in a.cov:
        data = open(path, "rb").read()
        magic, stamp, size, hsize = HEADER.unpack_from(data)
        if magic.rstrip(b"\0") != b"HD2COV1" or stamp != pe["stamp"] or size != pe["size"]:
            sys.exit("%s is not of this exe (stamp %08x size %x; the exe's %08x %x)" % (path, stamp, size, pe["stamp"],
                                                                                     pe["size"]))
        for i, b in enumerate(data[hsize:hsize + size]):
            if b:
                hits[i] = 1

    rows = []
    for rva, (sym, obj) in sorted(funcs.items()):
        code = pe["read"](rva, 5)
        instrumented = len(code) == 5 and code[0] == 0xE8 and rva + 5 + struct.unpack("<i", code[1:])[0] == penter
        rows.append(dict(rva="%08x" % rva, address="%08x" % (rva + pe["base"]), symbol=sym, object=obj,
                         source=objs[obj], kind=kind(sym), instrumented=instrumented,
                         entered=bool(instrumented and hits[rva + 5])))
    starts = {rva + 5 for rva in funcs}
    stray = [i for i, b in enumerate(hits) if b and i not in starts]

    # The original addresses each function ports (port_symbols.py: the PDB's line, the address comments).
    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    import port_symbols
    psym = port_symbols.port_symbols(a.exe, pe["base"])
    for r in rows:
        p = psym.get(int(r["rva"], 16))
        r["originals"] = ["%08x" % x for x in p["addresses"]] if p else []
        r["found_by"] = p["how"] if p else None
    # Per original function of the ledger: a port function names it, and one of them was entered.
    ledger_path = os.path.join(ROOT, "_re", "coverage", "ledger.csv")
    original = {}
    for r in rows:
        if r["instrumented"]:
            for x in r["originals"]:
                original[x] = original.get(x, False) or r["entered"]
    ledger_lines = []
    if os.path.exists(ledger_path):
        import csv
        led = list(csv.DictReader(open(ledger_path, encoding="utf-8")))
        ledger_lines = ["", "## Original functions (address ledger)", "",
                        "A ledger row counts as reached when a port function whose definition or declaration names "
                        "its address was entered. Inline-only ports and unnamed helpers cannot be reached this way.", "",
                        "| Disposition | Functions | Named by a port function | Reached |", "|---|---:|---:|---:|"]
        for disp in ("hand", "absorbed"):
            sel = [x for x in led if x["disposition"] == disp]
            named = [x for x in sel if x["address"] in original]
            reached = [x for x in named if original[x["address"]]]
            ledger_lines.append("| %s | %d | %d | %d |" % (disp, len(sel), len(named), len(reached)))
            print("ledger %s: %d functions, %d named by a port function, %d reached" % (disp, len(sel), len(named),
                                                                                     len(reached)))

    out = a.out or os.path.dirname(os.path.abspath(a.cov[0]))
    os.makedirs(out, exist_ok=True)
    json.dump(dict(exe=a.exe, stamp="%08x" % pe["stamp"], cov=a.cov, stray_hits=len(stray), functions=rows),
              open(os.path.join(out, "client_coverage.json"), "w"), indent=0)

    def tally(sel):
        inst = [r for r in sel if r["instrumented"]]
        return len(sel), len(inst), sum(r["entered"] for r in inst)

    lines = ["# Client function-entry coverage", "", "- exe: `%s` (stamp %08x)" % (a.exe, pe["stamp"]),
             "- runs: " + ", ".join("`%s`" % c for c in a.cov),
             "- hits not at a known function start: %d" % len(stray), "",
             "| Kind | Functions | Instrumented | Entered | Entered % |", "|---|---:|---:|---:|---:|"]
    for k in ("port", "std", "generated", "funclet"):
        n, i, e = tally([r for r in rows if r["kind"] == k])
        lines.append("| %s | %d | %d | %d | %.1f |" % (k, n, i, e, 100 * e / i if i else 0))
    n, i, e = tally(rows)
    lines.append("| all | %d | %d | %d | %.1f |" % (n, i, e, 100 * e / i if i else 0))
    print("\n".join(lines[5:]))
    lines += ledger_lines
    lines += ["", "## Port functions by source", "", "| Source | Functions | Entered | Not instrumented |",
              "|---|---:|---:|---:|"]
    by = collections.defaultdict(list)
    for r in rows:
        if r["kind"] == "port":
            by[r["source"]].append(r)
    for src in sorted(by):
        n, i, e = tally(by[src])
        lines.append("| %s | %d | %d | %d |" % (src, i, e, n - i))
    open(os.path.join(out, "client_coverage.md"), "w").write("\n".join(lines) + "\n")
    print("wrote", os.path.join(out, "client_coverage.md"))


if __name__ == "__main__":
    main()
