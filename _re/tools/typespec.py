"""Type specification shared by Ghidra and the generated C++ headers.

Spec files live in _re/types/*.spec. Syntax (one declaration per block):

    struct S_vector size=0xc
      0x00 float x
      0x04 float y
      0x08 float z
    end

    class C_Module size=0xc vtable=0x8103d8 header=game/module.h
      0x04 C_str m_name
      0x08 int m_id
    end

    class C_TrackModule : C_Module size=0x2c vtable=0x80f4d0
      0x0c std::vector<S_track> m_tracks
    end

    enum E_ChunkId : uint16_t
      CHUNK_ROOT = 0x1000
    end

    alias HFILE_DTA = int          # typedef

Field types use C++ spelling. `std::vector<T>` and `C_str` are understood natively.
Offsets are absolute (from the start of the object, vtable pointer included).

This module parses specs and emits the C declarations Ghidra's CParser imports.
build_cpp.py uses the same parse result to emit C++ headers.
"""
import glob
import os
import re

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SPEC_DIR = os.path.join(ROOT, "types")


class Field:
    def __init__(self, off, ctype, name, comment=""):
        self.off, self.ctype, self.name, self.comment = off, ctype, name, comment


class Decl:
    def __init__(self, kind, name):
        self.kind = kind          # struct | class | enum | alias | union
        self.name = name
        self.base = None
        self.size = None
        self.vtable = None        # address of the vtable (int) for classes; 0 = lives in a DLL
        self.vtable_slots = 0
        self.header = None        # C++ header path relative to src/
        self.fields = []
        self.enumerators = []     # (name, value)
        self.underlying = "int"
        self.alias_of = None
        self.comment = ""
        self.verbatim = []        # extra C++ lines placed inside the class body
        self.slot_names = {}      # vtable slot index -> (name, prototype or None) from `vslot`


def parse_int(s):
    return int(s, 0)


def parse_all():
    decls = {}
    order = []
    for path in sorted(glob.glob(os.path.join(SPEC_DIR, "*.spec"))):
        cur = None
        for raw in open(path, encoding="utf-8"):
            line = raw.rstrip("\n")
            code, _, comment = line.partition("#")
            code = code.rstrip()
            if not code.strip():
                continue
            s = code.strip()
            if cur is None:
                m = re.match(r"alias\s+(\w+)\s*=\s*(.+)$", s)
                if m:
                    d = Decl("alias", m.group(1))
                    d.alias_of = m.group(2).strip()
                    d.comment = comment.strip()
                    decls[d.name] = d
                    order.append(d.name)
                    continue
                m = re.match(r"(struct|class|union)\s+(\w+)(?:\s*:\s*(\w+))?((?:\s+\w+=\S+)*)\s*$", s)
                if m:
                    cur = Decl(m.group(1), m.group(2))
                    cur.base = m.group(3)
                    for k, v in re.findall(r"(\w+)=(\S+)", m.group(4) or ""):
                        if k == "size":
                            cur.size = parse_int(v)
                        elif k == "vtable":
                            addr, _, count = v.partition(":")
                            cur.vtable = parse_int(addr)
                            cur.vtable_slots = int(count, 0) if count else 1
                        elif k == "header":
                            cur.header = v
                    cur.comment = comment.strip()
                    continue
                m = re.match(r"enum\s+(\w+)(?:\s*:\s*(\w+))?(?:\s+header=(\S+))?\s*$", s)
                if m:
                    cur = Decl("enum", m.group(1))
                    cur.underlying = m.group(2) or "int"
                    cur.header = m.group(3)
                    cur.comment = comment.strip()
                    continue
                raise SyntaxError(f"{path}: cannot parse: {line}")
            if s == "end":
                decls[cur.name] = cur
                order.append(cur.name)
                cur = None
                continue
            if cur.kind == "enum":
                m = re.match(r"(\w+)\s*=\s*(\S+)$", s)
                if not m:
                    raise SyntaxError(f"{path}: bad enumerator: {line}")
                cur.enumerators.append((m.group(1), m.group(2), comment.strip()))
                continue
            if s.startswith("|"):
                cur.verbatim.append(s[1:].rstrip())
                continue
            # `vslot 0x0e StartMission | void __thiscall StartMission(C_X *this, bool server)`:
            # names a virtual slot (index) whose target has no usable name, e.g. _purecall
            m = re.match(r"vslot\s+(0x[0-9a-fA-F]+|\d+)\s+(\w+)\s*(?:\|\s*(.+))?$", s)
            if m:
                cur.slot_names[parse_int(m.group(1))] = (m.group(2), (m.group(3) or "").strip() or None)
                continue
            m = re.match(r"(0x[0-9a-fA-F]+|\d+)\s+(.+?)\s+(\w+)((?:\[[^\]]+\])*)$", s)
            if not m:
                raise SyntaxError(f"{path}: bad field: {line}")
            ctype = m.group(2) + (m.group(4) or "")
            cur.fields.append(Field(parse_int(m.group(1)), m.group(2) if not m.group(4) else m.group(2),
                                    m.group(3) + (m.group(4) or ""), comment.strip()))
    # Hand-made corrections of generated (90_auto.spec) classes, class_bases.json:
    #   "C_Human": {"base": "C_Actor", "vtable_slots": 56}
    # A base given here only lends its vtable slot names (the generated layout already
    # holds the base's fields); vtable_slots trims a vtable that auto_classes.py ran
    # into the next table (e.g. an actor creator's).
    bases_path = os.path.join(ROOT, "class_bases.json")
    if os.path.exists(bases_path):
        import json
        for name, info in json.load(open(bases_path)).items():
            d = decls.get(name)
            if d is None:
                continue
            base = info.get("base")
            if base and not d.base and base in decls:
                d.base = base
                d.inherits_layout = False
            if info.get("vtable_slots"):
                d.vtable_slots = int(info["vtable_slots"])
    return decls, order


# --------------------------------------------------------------------------- Ghidra C emission
GH_SCALARS = {
    "int8_t": "char", "uint8_t": "unsigned char", "int16_t": "short", "uint16_t": "unsigned short",
    "int32_t": "int", "uint32_t": "unsigned int", "int64_t": "long long", "uint64_t": "unsigned long long",
    "bool": "bool", "BOOL": "int", "DWORD": "unsigned int", "HWND": "void *", "HANDLE": "void *",
    "size_t": "unsigned int", "float": "float", "double": "double", "char": "char", "int": "int",
    "unsigned": "unsigned int", "void": "void", "long": "long", "short": "short",
    "wchar_t": "unsigned short",
}

SCALAR_SIZE = {"char": 1, "unsigned char": 1, "bool": 1, "short": 2, "unsigned short": 2, "int": 4,
               "unsigned int": 4, "long": 4, "float": 4, "double": 8, "long long": 8,
               "unsigned long long": 8}


def vector_name(elem):
    return "vector_" + re.sub(r"\W+", "_", elem.replace("*", "Ptr")).strip("_")


def gh_type(ctype, decls, needed):
    """Map a C++ field type to Ghidra C. Records generated helper structs in `needed`."""
    t = ctype.strip()
    t = re.sub(r"\bconst\b\s*", "", t).strip()
    m = re.fullmatch(r"std::vector<(.+)>", t)
    if m:
        elem = m.group(1).strip()
        vn = vector_name(elem)
        needed[vn] = elem
        return f"struct {vn}"
    ptr = ""
    while t.endswith("*"):
        ptr += "*"
        t = t[:-1].strip()
    if t in GH_SCALARS:
        base = GH_SCALARS[t]
    elif t in decls and decls[t].kind == "enum":
        base = f"enum {t}"
    elif t in decls and decls[t].kind == "alias":
        base = t
    elif t == "std::string":
        base = "struct std_string"
    elif t in decls or t in ("C_str", "C_str_rep"):
        base = f"struct {t}"
    elif t.startswith("I3D_") or t.startswith("I") and t[1:2].isupper():
        base = f"struct {t}"   # engine interface, opaque
    else:
        base = f"struct {t}"
    return base + (" " + ptr if ptr else "")


def emit_ghidra_c(decls, order, vtables):
    """vtables: class name -> list of (slot method name, ghidra prototype params string or None)."""
    needed = {}
    body = []
    fwd = set()
    for name in order:
        d = decls[name]
        if d.kind == "alias":
            body.append(f"typedef {gh_type(d.alias_of, decls, needed)} {d.name};")
        elif d.kind == "enum":
            body.append(f"typedef enum {d.name} {{")
            for en, ev, _ in d.enumerators:
                body.append(f"    {en} = {ev},")
            body.append(f"}} {d.name};")
    for name in order:
        d = decls[name]
        if d.kind not in ("struct", "class", "union"):
            continue
        fwd.add(d.name)
        fields = []
        if d.vtable is not None or (d.base and has_vtable(d, decls)):
            fields.append((0, f"struct {d.name}_vtbl *", "vtable", 4))
            fwd.add(d.name + "_vtbl")
        for f in all_fields(d, decls):
            fields.append((f.off, gh_type(f.ctype, decls, needed), f.name, None))
        fields.sort(key=lambda x: x[0])
        lines = [f"struct {d.name} {{"]
        pos = 0
        for off, gt, fname, _ in fields:
            if off > pos:
                lines.append(f"    unsigned char _pad_{pos:04x}[{off - pos}];")
            arr = ""
            m = re.match(r"(\w+)(\[.+\])$", fname)
            if m:
                fname, arr = m.group(1), m.group(2)
            lines.append(f"    {gt} {fname}{arr};")
            pos = off + field_size(gt, arr, decls)
        if d.size is not None and d.size > pos:
            lines.append(f"    unsigned char _pad_{pos:04x}[{d.size - pos}];")
        lines.append("};")
        body.append("\n".join(lines))
    for cname, slots in vtables.items():
        lines = [f"struct {cname}_vtbl {{"]
        for i, (mname, params) in enumerate(slots):
            lines.append(f"    void *{mname or f'vfunc_{i:02x}'};" if params is None else
                         f"    {params.replace('%s', mname)};")
        lines.append("};")
        body.append("\n".join(lines))
    head = ["#pragma pack(push, 1)",
            "struct C_str_rep { int refCount; unsigned int length; char chars[1]; };",
            "struct C_str { struct C_str_rep *rep; };",
            "struct std_string { unsigned char alloc; unsigned char _pad[3]; char *ptr; unsigned int length; unsigned int capacity; };"]
    for vn, elem in needed.items():
        et = gh_type(elem, decls, {})
        head.append(f"struct {vn} {{ unsigned char alloc; unsigned char _pad[3]; {et} *first; {et} *last; {et} *end; }};")
    return "\n".join(head + body + ["#pragma pack(pop)"]) + "\n"


def has_vtable(d, decls):
    while d is not None:
        if d.vtable is not None:
            return True
        d = decls.get(d.base) if d.base else None
    return False


def all_fields(d, decls):
    """Fields of d including (flattened) base-class fields."""
    chain = []
    cur = d
    while cur is not None:
        chain.append(cur)
        if not getattr(cur, "inherits_layout", True):
            break           # generated layout already covers the base's fields
        cur = decls.get(cur.base) if cur.base else None
    out = []
    for c in reversed(chain):
        out.extend(c.fields)
    return out


def field_size(gt, arr, decls):
    n = 1
    for dim in re.findall(r"\[([^\]]+)\]", arr):
        n *= int(dim, 0)
    if gt.endswith("*"):
        return 4 * n
    if gt.startswith("struct vector_"):
        return 16 * n
    if gt.startswith("enum "):
        return 4 * n
    if gt in SCALAR_SIZE:
        return SCALAR_SIZE[gt] * n
    if gt.startswith("struct "):
        nm = gt[7:]
        if nm == "C_str":
            return 4 * n
        if nm == "std_string":
            return 16 * n
        if nm in decls and decls[nm].size:
            return decls[nm].size * n
    if gt in decls and decls[gt].kind == "alias":
        return field_size(gh_type(decls[gt].alias_of, decls, {}), "", decls) * n
    raise ValueError(f"unknown size for {gt}")


if __name__ == "__main__":
    ds, od = parse_all()
    print(emit_ghidra_c(ds, od, {}))
