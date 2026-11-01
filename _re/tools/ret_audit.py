"""Declared stack arguments vs the code's RET n.

Ghidra gives JMP-thunk wrappers and empty virtual stubs prototypes without arguments, so the
callers' pushes vanish from the decompiles and ports drop real values (C_Human::SetMoveFlags,
C_WireCut::Cut's second argument, LoadGameTexts' language ...). This compares, for every
function, the stack bytes a declaration accounts for with the RET n of the code.

Usage: python ret_audit.py [module ...]      names/<module>.txt prototypes (default: all);
                                             entries without one use Ghidra's signature
       python ret_audit.py --slots           every typed vtable slot of the spec / auto classes
                                             (the target's names prototype, else a `vslot` line)
       python ret_audit.py --src [glob ...]  C++ declarations in src/ that carry an address
                                             comment (default src/**/*.h, src/**/*.cpp)

Tail `JMP [reg+slot]` thunks: the register chain from ECX (this) is followed through the
Ghidra member types (ghidra_layouts.json) to the vtable of the final object's class; plain
tail `JMP addr` are followed too. Prototypes count `this` out for __thiscall, the first two
integer arguments for __fastcall, structs passed by value with their Ghidra size, and a
hidden return pointer for S_vector / S_matrix / C_str ... returned by value. A __cdecl
prototype whose code pops arguments is reported (`cdecl-count-ok`: the count matches, the
convention is wrong: __stdcall, or __thiscall when ECX is read).

Known false positives (2026-10-17): zz_idioms empty_function_* / identity_*; thunks through
member chains or into vtables longer than the base class (C_Human::GetHandsItem, AddOrder ...:
verify by hand); Ghidra function chunks (PlayTableSound_part2, std_string::CopyTail);
--src: C++ default arguments, destructors listing the scalar deleting destructor, free
functions standing in for __thiscall methods (first parameter = this), merged functions
listing several addresses. A member whose address comment says "(__fastcall" was a __fastcall
function taking `this` in ECX (its first integer argument in EDX); one saying "added by the
port" has parameters the original did not (not compared).
"""
import glob
import json
import os
import re
import struct
import sys
import urllib.request
from concurrent.futures import ThreadPoolExecutor

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import ghidra_program  # noqa: E402,F401  (adds program= to plugin requests)
import typespec  # noqa: E402

RE = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ROOT = os.path.dirname(RE)
BASE = "http://127.0.0.1:8089"
rd = open(os.path.join(RE, "raw", "rdata_0080f000.bin"), "rb").read()
layouts = json.load(open(os.path.join(RE, "ghidra_layouts.json")))
syms = json.load(open(os.path.join(RE, "symbols.json")))
decls, _order = typespec.parse_all()

vt_of = {}   # class -> (vtable, slot count or None)
for k, v in json.load(open(os.path.join(RE, "class_names.json"))).items():
    vt_of.setdefault(v, (int(k, 16), None))
for d in decls.values():
    if d.vtable:
        vt_of[d.name] = (d.vtable, d.vtable_slots or None)
for k in layouts:
    m = re.match(r"C_auto_([0-9a-f]{8})$", k)
    if m:
        vt_of.setdefault(k, (int(m.group(1), 16), None))


def rdw(a):
    o = a - 0x80F000
    if 0 <= o < len(rd) - 4:
        return struct.unpack_from("<I", rd, o)[0]
    return None


_dcache = {}


def dis(a):
    if a in _dcache:
        return _dcache[a]
    try:
        d = json.load(urllib.request.urlopen(f"{BASE}/disassemble_function?address=0x{a:x}"))
        r = [i["instruction"] for i in d["instructions"]]
    except Exception:  # noqa: BLE001
        r = None
    _dcache[a] = r
    return r


def sig(a):
    try:
        d = json.load(urllib.request.urlopen(f"{BASE}/get_functions?functions=0x{a:x}&fields=signature"))
        return list(d["functions"].values())[0].get("signature")
    except Exception:  # noqa: BLE001
        return None


def rets(ins):
    r = set()
    for i in ins:
        m = re.match(r"RET(?:N)?(?: 0x([0-9a-f]+))?$", i.strip())
        if m:
            r.add(int(m.group(1) or "0", 16))
    return r


BYVAL = r"(const\s+)?(S_vector4|S_quat|S_vector|S_vector2|S_matrix|C_str|S_plane|I3D_bbox)\b"


def psize(p):
    """Stack bytes of one Ghidra-style parameter declaration."""
    p = p.strip()
    if "*" in p or "&" in p:
        return 4
    if re.match(r"(const\s+)?(S_vector4|S_quat)\b", p):
        return 16
    if re.match(r"(const\s+)?S_vector2\b", p):
        return 8
    if re.match(r"(const\s+)?S_vector\b", p):
        return 12
    if re.match(r"(const\s+)?S_matrix\b", p):
        return 64
    if re.match(r"(double|longlong|ulonglong|__int64|undefined8)\b", p):
        return 8
    t = re.sub(r"\b(const|struct)\b", "", p).split()
    if t and t[0] in layouts and t[0] != "C_str":
        return (layouts[t[0]]["size"] + 3) & ~3   # struct passed by value
    return 4


def splitargs(s):
    out, depth, cur = [], 0, ""
    for ch in s:
        if ch in "(<[":
            depth += 1
        elif ch in ")>]":
            depth -= 1
        if ch == "," and depth == 0:
            out.append(cur)
            cur = ""
        else:
            cur += ch
    if cur.strip():
        out.append(cur)
    return [x.strip() for x in out if x.strip() and x.strip() != "void"]


def argbytes(proto):
    """(stack bytes, convention, hidden return pointer) of a Ghidra prototype."""
    i, j = proto.find("("), proto.rfind(")")
    if i < 0 or j < 0:
        return None
    head = proto[:i]
    ps = splitargs(proto[i + 1:j])
    if "..." in ps:
        return None
    conv = "cdecl" if "__cdecl" in head else "fastcall" if "__fastcall" in head else \
        "thiscall" if "__thiscall" in head else "stdcall" if "__stdcall" in head else "default"
    if conv == "thiscall" and ps:
        ps = ps[1:]
    n, regs = 0, 2 if conv == "fastcall" else 0
    for p in ps:
        sz = psize(p)
        if regs and sz <= 4 and ("*" in p or not re.match(r"float\b", p)):
            regs -= 1
            continue
        n += sz
    ret = re.sub(r"\b\w+\s*$", "", head)
    for c in ("__thiscall", "__stdcall", "__cdecl", "__fastcall"):
        ret = ret.replace(c, "")
    ret = ret.strip()
    hidden = bool(re.match(BYVAL, ret)) and "*" not in ret and "&" not in ret
    return n + (4 if hidden else 0), conv, hidden


def field_ptr_type(cls, off):
    lay = layouts.get(cls)
    if not lay:
        return None, f"no layout {cls}"
    for f in lay["fields"]:
        if f["offset"] == off:
            t = f["type"]
            if t.endswith("*") and not t.endswith("* *"):
                return t[:-1].strip(), None
            return None, f"{cls}+{off:#x} is {t}"
        if f["offset"] < off < f["offset"] + f["size"]:
            return None, f"{cls}+{off:#x} inside {f['name']} ({f['type']})"
    return None, f"{cls}+{off:#x} not in layout"


def thunk_target(cls, ins):
    """Resolve a tail `JMP [reg+slot]`: (target, how) or (None, reason)."""
    m = re.match(r"JMP dword ptr \[(E[A-Z]X)(?: \+ 0x([0-9a-f]+))?\]$", ins[-1].strip())
    if not m:
        return None, None
    jreg, slot = m.group(1), int(m.group(2) or "0", 16)
    regs = {"ECX": ("obj", cls)}
    for i in ins[:-1]:
        i = i.strip()
        mm = re.match(r"MOV (E[A-Z]X),dword ptr \[(E[A-Z]X)(?: \+ 0x([0-9a-f]+))?\]$", i)
        if mm:
            dst, src, off = mm.group(1), mm.group(2), int(mm.group(3) or "0", 16)
            kind, c = regs.get(src, (None, None))
            if kind == "obj" and c:
                if off == 0:
                    regs[dst] = ("vt", c)
                else:
                    t, why = field_ptr_type(c, off)
                    regs[dst] = ("obj", t) if t else ("?", why)
            else:
                regs[dst] = ("?", f"{src} unknown")
            continue
        mm = re.match(r"MOV (E[A-Z]X),(E[A-Z]X)$", i)
        if mm:
            regs[mm.group(1)] = regs.get(mm.group(2), ("?", "unknown"))
            continue
        return None, f"slot {slot:#x}: not a plain thunk ({i})"
    kind, c = regs.get(jreg, (None, None))
    if kind != "vt":
        return None, f"slot {slot:#x}: vtable register {jreg} ({c})"
    if c not in vt_of:
        return None, f"slot {slot:#x} of {c} (no vtable known)"
    vt, ns = vt_of[c]
    if ns is not None and slot // 4 >= ns:
        return None, f"slot {slot:#x} past {c} vtable ({ns} slots)"
    t = rdw(vt + slot)
    if not t or not (0x401000 <= t < 0x80f000):
        return None, f"slot {slot:#x} of {c} vtable {vt:#x} -> {t}"
    return t, f"thunk {c}[{slot:#x}] -> {t:08x}"


def effective_rets(a, cls):
    """RET n values of the function at `a`, following tail thunks / jumps: (rets, note)."""
    ins = dis(a)
    if not ins:
        return None, "no disassembly"
    r = rets(ins)
    if r:
        return r, ""
    t, how = thunk_target(cls, ins)
    if t:
        return rets(dis(t) or []), how
    if how:
        return set(), "UNRESOLVED " + how
    m = re.match(r"JMP 0x([0-9a-f]+)$", ins[-1].strip())
    if m:
        t = int(m.group(1), 16)
        return rets(dis(t) or []), f"tail jmp -> {t:08x}"
    return set(), ""


def judge(n, conv, r):
    if conv == "cdecl":
        if r and r != {0}:
            return "cdecl-count-ok" if r == {n} else "cdecl-COUNT"
        return None
    return "COUNT" if r and r != {n} else None


def names_audit(mods):
    entries = []
    for mod in mods:
        for line in open(os.path.join(RE, "names", mod + ".txt"), encoding="utf-8"):
            line = line.split("#", 1)[0].strip()
            m = re.match(r"([0-9a-f]{8}) (\S+)(?: \| (.*))?$", line)
            if m:
                entries.append((mod, int(m.group(1), 16), m.group(2), m.group(3)))

    def work(e):
        mod, a, name, proto = e
        src = "names"
        if not proto:
            proto, src = sig(a), "ghidra"
            if not proto:
                return None
        r, note = effective_rets(a, name.split("::")[0] if "::" in name else None)
        return None if r is None else (mod, a, name, proto, src, r, note)

    with ThreadPoolExecutor(8) as ex:
        res = [x for x in ex.map(work, entries) if x]
    print(f"{len(res)} functions checked")
    for mod, a, name, proto, src, r, note in res:
        ab = argbytes(proto)
        if ab is None:
            continue
        n, conv, _ = ab
        j = "UNRESOLVED" if note.startswith("UNRESOLVED") else judge(n, conv, r)
        if j:
            print(f"{j:14} {mod:14} {a:08x} {name}: declared {n:#x} [{conv}], "
                  f"RET {sorted(hex(x) for x in r)} {note} [{src}]\n    {proto}")


def slot_audit():
    def declared_slot(d, i):
        while d is not None:
            if i in d.slot_names:
                return d.slot_names[i]
            d = decls.get(d.base) if d.base else None
        return None
    jobs = []
    for d in decls.values():
        if not d.vtable or not d.vtable_slots:
            continue
        for i in range(d.vtable_slots):
            t = rdw(d.vtable + 4 * i)
            if not t or not (0x401000 <= t < 0x80f000):
                continue
            s = syms.get(f"{t:08x}") or {}
            proto, src = s.get("proto"), "target"
            if not proto:
                ds = declared_slot(d, i)
                if ds and ds[1]:
                    proto, src = ds[1], "vslot"
            if proto and s.get("name") != "_purecall":
                jobs.append((d.name, i, t, s.get("name"), proto, src))

    def work(j):
        cls, i, t, nm, proto, src = j
        return j, effective_rets(t, nm.split("::")[0] if nm and "::" in nm else cls)
    seen = set()
    with ThreadPoolExecutor(8) as ex:
        res = list(ex.map(work, jobs))
    print(f"{len(jobs)} typed slots checked")
    for (cls, i, t, nm, proto, src), (r, note) in res:
        ab = argbytes(proto) if r is not None else None
        if not ab:
            continue
        j = "UNRESOLVED" if note.startswith("UNRESOLVED") else judge(ab[0], ab[1], r)
        if j and (t, proto) not in seen:
            seen.add((t, proto))
            print(f"{j:14} {cls}[{i * 4:#x}] -> {t:08x} {nm}: declared {ab[0]:#x} ({src}), "
                  f"RET {sorted(hex(x) for x in r)} {note}\n    {proto}")


def cpp_psize(p):
    p = re.sub(r"/\*.*?\*/", "", p).strip()
    p = re.sub(r"\s*=.*$", "", p)          # default value
    if not p or p == "void":
        return 0
    if "*" in p or "&" in p or re.search(r"\[\s*\d*\s*\]", p):
        return 4
    t = re.sub(r"\b(const|volatile|struct|class|enum|unsigned|signed)\b", "", p).split()
    t = t[0] if t else ""
    return {"S_vector4": 16, "S_quat": 16, "S_vector2": 8, "S_vector": 12, "S_matrix": 64, "S_TextRun": 8,
            "double": 8, "int64_t": 8, "uint64_t": 8, "__int64": 8}.get(t, 4)


def src_decls(globs):
    files = []
    for g in globs:
        files += [f for f in glob.glob(os.path.join(ROOT, g), recursive=True) if "tier1" not in f]
    out = []
    for f in files:
        stack, buf = [], ""
        for ln, line in enumerate(open(f, encoding="utf-8", errors="replace").read().split("\n"), 1):
            code, _, comment = line.partition("//")
            addrs = re.findall(r"0x(00[4-7][0-9a-f]{5})\b", comment)
            st = code.strip()
            buf = st if (not buf or re.search(r"[;{}]\s*$", buf)) else buf + " " + st
            if addrs and "(" in buf and not st.startswith(("if", "return", "for", "while")):
                mm = re.search(r"(~?\w+)\s*\(", buf)
                if mm:
                    i = mm.end() - 1
                    d, j = 0, i
                    while j < len(buf):
                        d += {"(": 1, ")": -1}.get(buf[j], 0)
                        if d == 0:
                            break
                        j += 1
                    if j < len(buf):
                        out.append((os.path.relpath(f, ROOT), ln, int(addrs[-1], 16), mm.group(1),
                                    buf[:mm.start()].strip(), buf[i + 1:j],
                                    bool(stack) and stack[-1] is not None, line.strip()))
            for mt in re.finditer(r"(\b(class|struct|union)\s+(?:alignas\(\d+\)\s+)?(\w+)[^;{()]*)?\{|\}", code):
                if mt.group(0) == "}":
                    if stack:
                        stack.pop()
                else:
                    stack.append(mt.group(3) if mt.group(1) else None)
            if re.search(r"[;{}]\s*$", st) or not st:
                buf = ""
    return out


def src_audit(globs):
    ds = src_decls(globs or ["src/**/*.h", "src/**/*.cpp"])
    with ThreadPoolExecutor(8) as ex:
        res = list(ex.map(lambda d: (d, effective_rets(d[2], None)), ds))
    print(f"{len(ds)} declarations checked")
    seen = set()
    for (f, ln, a, name, head, params, in_class, text), (r, note) in res:
        if not r:
            continue
        ps = splitargs(params)
        if any("..." in p for p in ps):
            continue
        n = sum(cpp_psize(p) for p in ps)
        free = "static" in head or not in_class
        ret = re.sub(r"\b(virtual|static|inline|explicit|const|friend)\b", "", head).strip()
        if re.match(r"(S_vector4|S_vector2|S_vector|S_quat|S_matrix|C_str)\b", ret) and "*" not in ret and "&" not in ret:
            n += 4
        if free and r == {0}:
            continue  # __cdecl / register arguments: nothing to compare
        # The controls' callbacks (ControlCallback): __thiscall members of `object` in the original,
        # `object` in ECX and again as the first stack argument; the port's `argument` is that one.
        if free and re.search(r"void\s*\*\s*\w+\s*,\s*void\s*\*\s*\w*\s*,\s*C_Control\s*\*", params):
            n -= 4
            if r == {n}:
                continue
        if re.search(r"inline in|scalar deleting|added by the port", text) or (name.startswith("~") and r == {4}):
            continue
        # Members marked "(__fastcall" in their address comment were __fastcall functions taking
        # `this` in ECX: their first integer argument came in EDX.
        member_fastcall = "(__fastcall" in text and (not free or head.endswith("::"))
        if free or member_fastcall:
            regs, nf = (1 if member_fastcall else 2), 0       # __fastcall
            for p in ps:
                sz = cpp_psize(p)
                if regs and sz == 4 and ("*" in p or not re.match(r"\s*(const\s+)?float\b", p)):
                    regs -= 1
                    continue
                nf += sz
            if r == {nf}:
                continue
        if r != {n} and (a, name) not in seen:
            seen.add((a, name))
            print(f"{f}:{ln} {a:08x} {name}: C++ {n:#x}{' (free)' if free else ''}, "
                  f"RET {sorted(hex(x) for x in r)} {note}\n    {text[:200]}")


if __name__ == "__main__":
    if "--slots" in sys.argv:
        slot_audit()
    elif "--src" in sys.argv:
        src_audit([a for a in sys.argv[1:] if a != "--src"])
    else:
        names_audit(sys.argv[1:] or [os.path.basename(p)[:-4]
                                     for p in sorted(glob.glob(os.path.join(RE, "names", "*.txt")))])
