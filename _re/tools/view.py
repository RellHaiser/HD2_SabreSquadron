"""Compact viewer for exported decompilation.

  python view.py ADDR [ADDR ...]       show functions
  python view.py --range LO HI         show every exported function in [LO, HI)
  python view.py --brief LO HI         one line per function: addr size callers strings

Compaction: CRLF -> LF, local declarations folded onto one line, Ghidra string
labels (s_foo_00839xxx) replaced by the literal, and FUN_/DAT_ names replaced by
names from _re/symbols.json when known.
"""
import json
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DEC = os.path.join(ROOT, "decomp")

_strings = None
_symbols = None
_index = None


def strings():
    global _strings
    if _strings is None:
        _strings = {int(s["address"], 16): s["value"] for s in
                    json.load(open(os.path.join(ROOT, "raw", "strings_all.json")))["strings"]}
    return _strings


def symbols():
    global _symbols
    if _symbols is None:
        p = os.path.join(ROOT, "symbols.json")
        _symbols = json.load(open(p)) if os.path.exists(p) else {}
    return _symbols


def index():
    global _index
    if _index is None:
        _index = json.load(open(os.path.join(ROOT, "index.json")))
    return _index


STR_RE = re.compile(r"\b(?:s|u)_[A-Za-z0-9_]*?_([0-9a-f]{8})\b")
FUN_RE = re.compile(r"\b(?:thunk_)?FUN_([0-9a-f]{8})\b")
DAT_RE = re.compile(r"\b_?(?:DAT|PTR_DAT|PTR_FUN|PTR_LAB|PTR)_([0-9a-f]{8})\b")
DECL_RE = re.compile(r"^\s{2}[A-Za-z_][\w \*]*?\s\*?[A-Za-z_]\w*(?: \[\d+\])?;\s*$")


def c_escape(s):
    return s.replace("\\", "\\\\").replace("\"", "\\\"").replace("\n", "\\n").replace("\r", "\\r").replace("\t", "\\t")


def compact(code):
    syms = symbols()
    st = strings()
    code = code.replace("\r", "")

    def srep(m):
        a = int(m.group(1), 16)
        if a in st:
            return '"' + c_escape(st[a]) + '"'
        return m.group(0)

    def frep(m):
        k = m.group(1)
        s = syms.get(k)
        return s["name"] if s else m.group(0)

    def drep(m):
        k = m.group(1)
        s = syms.get(k)
        return s["name"] if s else m.group(0)

    code = STR_RE.sub(srep, code)
    code = FUN_RE.sub(frep, code)
    code = DAT_RE.sub(drep, code)
    out, decls = [], []
    lines = code.split("\n")
    state = "head"  # head -> decls (just after the opening brace) -> body
    for ln in lines:
        if ln.strip() == "":
            continue
        if state == "head":
            out.append(ln)
            if ln == "{":
                state = "decls"
            continue
        if state == "decls":
            if DECL_RE.match(ln) and not ln.strip().startswith("return"):
                decls.append(ln.strip().rstrip(";"))
                continue
            if decls:
                out.append("  // locals: " + "; ".join(decls))
            state = "body"
        out.append(ln)
    return "\n".join(out).strip()


def show(addr):
    a = addr.lower().replace("0x", "").zfill(8)
    p = os.path.join(DEC, a + ".json")
    if not os.path.exists(p):
        print(f"// {a}: not exported")
        return
    d = json.load(open(p, encoding="utf-8"))
    idx = index().get(a, {})
    syms = symbols()
    callers = [syms.get(c, {}).get("name", c) for c in idx.get("callers", [])]
    vt = idx.get("in_vtables", [])
    hdr = f"// @{a} size={d.get('size')} callers={','.join(callers[:8])}{'...' if len(callers) > 8 else ''}"
    if vt:
        hdr += " vt=" + ",".join(f"{v}[{k}]" for v, k in vt[:4])
    print(hdr)
    print(compact(d.get("decompiled_code") or ""))
    print()


def brief(lo, hi):
    st = strings()
    syms = symbols()
    for a, r in index().items():
        ai = int(a, 16)
        if lo <= ai < hi:
            ss = [st.get(int(s, 16), "")[:40] for s in r["strings"][:3]]
            nm = syms.get(a, {}).get("name", r["name"])
            print(f"{a} {nm:24} sz={r['size']:<5} in={len(r['callers']):<3} out={len(r['callees']):<3}"
                  f"{' vt' if r['in_vtables'] else ''}{' VTREF' if r['vtables'] else ''} {ss if ss else ''}")


if __name__ == "__main__":
    args = sys.argv[1:]
    if args and args[0] == "--range":
        lo, hi = int(args[1], 16), int(args[2], 16)
        for a in sorted(index()):
            if lo <= int(a, 16) < hi:
                show(a)
    elif args and args[0] == "--brief":
        brief(int(args[1], 16), int(args[2], 16))
    else:
        for x in args:
            show(x)
