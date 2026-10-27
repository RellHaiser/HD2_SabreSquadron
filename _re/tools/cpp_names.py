"""Draft names-file lines (ADDR Class::Name | prototype) from hand-ported C++ headers.

Each declaration with an address comment becomes one line; the prototype is a Ghidra-style
C signature (explicit `this`, references as pointers, primitive types mapped to Ghidra's;
unknown struct types become void*). The LAST address in the comment is taken (as ret_audit
does), so cross-reference comments need care. Prints to stdout: review before pasting into
_re/names/<module>.txt (ctors/dtors, scalar-deleting dtors, __fastcall helpers and duplicate
addresses need a hand look; duplicates are reported on stderr).

Usage: python cpp_names.py HEADER [HEADER...] [--range LO-HI] [--extra ADDR,...]
       (headers relative to the project root, e.g. src/game/melee_tracker_ai.h)"""
import json
import os
import re
import sys

RE = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ROOT = os.path.dirname(RE)

layouts = json.load(open(os.path.join(RE, "ghidra_layouts.json")))
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import typespec  # noqa: E402
known_structs = set(layouts) | {n for n, d in typespec.parse_all()[0].items() if d.kind in ("struct", "class")}
PRIM = {"void": "void", "bool": "bool", "int": "int", "float": "float", "char": "char",
        "uint32_t": "uint", "int32_t": "int", "uint16_t": "ushort", "int16_t": "short",
        "uint8_t": "uchar", "int8_t": "char", "double": "double", "E_HitType": "int",
        "E_HumanRole": "int", "DWORD": "uint", "unsigned": "uint"}
LO, HI = 0x401000, 0x80f000
EXTRA = set()
HEADERS = []


def gtype(t):
    """C++ type text -> Ghidra type text."""
    t = re.sub(r"\b(const|volatile|struct|class|enum|virtual|static|inline|explicit)\b", "", t).strip()
    ref = t.endswith("&")
    t = t.rstrip("&").strip()
    stars = t.count("*") + (1 if ref else 0)
    base = t.replace("*", "").strip()
    if base in PRIM:
        g = PRIM[base]
    elif base in known_structs:
        g = base
    else:
        g = "void"
        if stars == 0:
            g = "int"           # by-value unknown: 4 bytes
    return g + (" " + "*" * stars if stars else "")


def split_params(s):
    out, d, cur = [], 0, ""
    for ch in s:
        if ch in "(<[":
            d += 1
        elif ch in ")>]":
            d -= 1
        if ch == "," and d == 0:
            out.append(cur)
            cur = ""
        else:
            cur += ch
    if cur.strip():
        out.append(cur)
    return [p.strip() for p in out]


def gparam(p, i):
    p = re.sub(r"/\*\s*(\w+)\s*\*/", r"\1", p)          # /*name*/ -> name
    p = re.sub(r"\s*=.*$", "", p).strip()
    if not p or p == "void":
        return None
    m = re.match(r"(.*?)(\b\w+)\s*(\[\s*\w*\s*\])?$", p)
    if m and m.group(1).strip() and m.group(1).strip() not in ("const",):
        typ, name, arr = m.group(1), m.group(2), m.group(3)
        if arr:
            typ += "*"
    else:
        typ, name = p, f"param_{i + 1}"
    return f"{gtype(typ)} {name}"


def decls():
    out = []
    for h in HEADERS:
        stack, buf = [], ""
        for ln, line in enumerate(open(os.path.join(ROOT, h), encoding="utf-8").read().split("\n"), 1):
            code, _, comment = line.partition("//")
            addrs = [int(a, 16) for a in re.findall(r"0x(00[4-7][0-9a-f]{5})\b", comment)]
            st = code.strip()
            buf = st if (not buf or re.search(r"[;{}]\s*$", buf)) else buf + " " + st
            if addrs and "(" in buf:
                mm = re.search(r"(~?\w+)\s*\(", buf)
                if mm:
                    i = mm.end() - 1
                    d, j = 0, i
                    while j < len(buf):
                        d += {"(": 1, ")": -1}.get(buf[j], 0)
                        if d == 0:
                            break
                        j += 1
                    cls = next((c for c in reversed(stack) if c), None)
                    out.append(dict(file=h, line=ln, addr=addrs[-1], name=mm.group(1),
                                    head=buf[:mm.start()].strip(), params=buf[i + 1:j], cls=cls,
                                    text=line.strip()))
            for mt in re.finditer(r"(\b(class|struct|union)\s+(?:alignas\(\d+\)\s+)?(\w+)[^;{()]*)?\{|\}", code):
                if mt.group(0) == "}":
                    if stack:
                        stack.pop()
                else:
                    stack.append(mt.group(3) if mt.group(1) else None)
            if re.search(r"[;{}]\s*$", st) or not st:
                buf = ""
    return out


def main():
    global LO, HI, EXTRA
    args = sys.argv[1:]
    while args:
        a = args.pop(0)
        if a == "--range":
            lo, hi = args.pop(0).split("-")
            LO, HI = int(lo, 16), int(hi, 16)
        elif a == "--extra":
            EXTRA = {int(x, 16) for x in args.pop(0).split(",")}
        else:
            HEADERS.append(a)
    if not HEADERS:
        raise SystemExit(__doc__)
    seen = {}
    for d in decls():
        a = d["addr"]
        if not (LO <= a < HI or a in EXTRA):
            continue
        head = d["head"]
        static = "static" in head
        stdcall = "__stdcall" in head
        ret = re.sub(r"\b(virtual|static|inline|explicit|__stdcall|override|const)\b", "", head).strip()
        name, cls = d["name"], d["cls"]
        if name == cls:                                   # constructor
            ret = f"{cls} *"
        elif name.startswith("~"):
            ret = "void"
        gret = gtype(ret) if ret else "void"
        params = [x for x in (gparam(p, i) for i, p in enumerate(split_params(d["params"]))) if x]
        qual = f"{cls}::{name}" if cls else name
        if cls and not static:
            conv = "__thiscall"
            params = [f"{cls} * this"] + params
        else:
            conv = "__stdcall" if stdcall else "__cdecl"
        proto = f"{gret} {conv} {name}({', '.join(params) if params else 'void'})"
        line = f"{a:08x} {qual} | {proto}"
        if a in seen:
            print(f"# DUP {a:08x}: {seen[a]}  vs  {qual}", file=sys.stderr)
            continue
        seen[a] = qual
        print(line)


main()
