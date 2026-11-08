"""Ghidra C definitions of the LS3D engine interfaces, generated from the hand-written
headers src/ls3d/i3d.h and src/ls3d/igraph.h.

Every interface gets a struct with a typed vtable: one `__stdcall` function typedef per
identified slot (`this` is passed on the stack, COM style), `void *SlotXX` for slots
not identified yet. Data members come from the SDK inline accessors (`Field<T>(off)`)
and from the explicitly laid out members (`// +0xNN` comments).

With these types applied, calls through engine objects decompile with their real
arguments instead of losing the pushed values.

apply_ghidra.py --types appends the output to the spec import (written to
_re/ls3d_ghidra.h as well).  Usage: python ls3d_ghidra.py   (prints the C text)
"""
import os
import re

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
HEADERS = [os.path.join(os.path.dirname(ROOT), "src", "ls3d", h) for h in ("i3d.h", "igraph.h")]

SCALARS = {
    "void": "void", "bool": "bool", "char": "char", "int": "int", "float": "float", "double": "double",
    "short": "short", "long": "long", "unsigned": "unsigned int",
    "int8_t": "char", "uint8_t": "unsigned char", "int16_t": "short", "uint16_t": "unsigned short",
    "int32_t": "int", "uint32_t": "unsigned int", "int64_t": "long long", "uint64_t": "unsigned long long",
    "size_t": "unsigned int", "uintptr_t": "unsigned int", "HRESULT": "long", "DWORD": "unsigned int", "BOOL": "int",
    "wchar_t": "unsigned short",
    "HWND": "void *", "HANDLE": "void *", "HINSTANCE": "void *",
    "I3D_ENUMPROC": "void", "I3D_LOAD_CB_PROC": "void",
    "E_I3D_FRAME_TYPE": "unsigned short",
}
ALIASES = {"PI3D_frame": "I3D_frame *", "PI3D_scene": "I3D_scene *", "PI3D_driver": "I3D_driver *"}
SIZES = {"S_matrix": 64, "S_quat": 16, "S_vector": 12, "S_vector4": 16, "S_vector2": 8, "I3D_bbox": 32,
         "unsigned short": 2, "short": 2, "char": 1, "unsigned char": 1, "bool": 1, "double": 8}

# Names of S_FrameData members identified since the header was written.
FRAME_DATA = """struct S_FrameData {
    struct C_Actor *actor;
    struct C_Script *script;
    unsigned int unknown08;
    unsigned int unknown0C;
    struct S_PhysicsHandle *physicsObject;
    struct C_VersionMaster *versionMaster;
    struct C_VersionKeeper *versionKeeper;
    unsigned int unknown1C;
};"""


def gh(t, known):
    """C++ type -> Ghidra C type text; records referenced struct names in `known`."""
    t = re.sub(r"\b(const|class|struct|enum)\b", "", t).strip()
    t = re.sub(r"\s+", " ", t)
    for a, b in ALIASES.items():
        t = re.sub(r"\b" + a + r"\b", b, t)
    ptr = t.count("*") + t.count("&")
    base = t.replace("*", "").replace("&", "").strip()
    if base in SCALARS:
        b = SCALARS[base]
        if b == "void *":
            b, ptr = "void", ptr + 1
    elif base.startswith("E_"):
        b = "int"
    else:
        known.add(base)
        b = "struct " + base
    return b + (" " + "*" * ptr if ptr else "")


def parse_params(text, known):
    text = text.strip()
    if not text:
        return []
    out = []
    for p in split_top(text):
        p = re.sub(r"=.*$", "", p).strip()
        m = re.match(r"(.*?)(\w+)$", p)
        ptype, pname = (m.group(1), m.group(2)) if m and m.group(1).strip() else (p, "")
        out.append(f"{gh(ptype, known)} {pname}".strip())
    return out


def split_top(text):
    parts, depth, cur = [], 0, ""
    for ch in text:
        if ch in "(<":
            depth += 1
        elif ch in ")>":
            depth -= 1
        if ch == "," and depth == 0:
            parts.append(cur)
            cur = ""
        else:
            cur += ch
    parts.append(cur)
    return [p for p in parts if p.strip()]


def parse_classes():
    classes = {}
    order = []
    for path in HEADERS:
        text = open(path, encoding="utf-8").read()
        for m in re.finditer(r"(?ms)^class (\w+)(?: : public (\w+))? \{\n(.*?)^\};", text):
            name, base, body = m.group(1), m.group(2), m.group(3)
            body_nc = re.sub(r"//[^\n]*", "", body)
            slots = []
            for s in re.finditer(r"I3DMETHOD\(\s*([^,]+?)\s*,\s*(\w+)\s*\)\s*\((.*?)\)\s*=\s*0\s*;"
                                 r"|I3D_SLOTS_4\(([^)]*)\)|I3D_SLOT\((\w+)\)", body_nc, re.S):
                if s.group(2):
                    slots.append((s.group(2), s.group(1), s.group(3)))
                elif s.group(4):
                    slots += [(None, x.strip(), None) for x in s.group(4).split(",")]
                else:
                    slots.append((None, s.group(5), None))
            fields = {}
            for f in re.finditer(r"(\w+)\(\)\s*const\s*\{\s*return Field<(.+?)>\((0x[0-9A-Fa-f]+)\)(\[\w+\])?", body):
                acc, ftype, off = f.group(1), f.group(2), int(f.group(3), 16)
                nm = re.sub(r"^(Get|Num)", lambda k: "num" if k.group(1) == "Num" else "", acc)
                nm = "m_" + nm[:1].lower() + nm[1:]
                if f.group(4):
                    nm += "s"
                fields.setdefault(off, (ftype, nm))
            for f in re.finditer(r"Field<(.+?)>\((0x[0-9A-Fa-f]+)\)", body):
                off = int(f.group(2), 16)
                fields.setdefault(off, (f.group(1), f"m_{off:x}"))
            for f in re.finditer(r"(?m)^\s+([\w:<> ]+?\s*\**)\s*(m_\w+)(\[(0x[0-9A-Fa-f]+|\d+)\])?;\s*//\s*\+0x([0-9A-Fa-f]+)", body):
                fields[int(f.group(5), 16)] = (f.group(1).strip(), f.group(2))
            classes[name] = {"base": base, "slots": slots, "fields": fields}
            order.append(name)
    return classes, order


def all_slots(classes, name):
    c = classes[name]
    base = all_slots(classes, c["base"]) if c["base"] in classes else []
    return base + [(name, s) for s in c["slots"]]


def all_fields(classes, name):
    c = classes[name]
    out = dict(all_fields(classes, c["base"])) if c["base"] in classes else {}
    out.update(c["fields"])
    return out


_spec_sizes = None


def field_size(t):
    """Size of a member type. Structs come from the specs (types/*.spec); assuming 4
    bytes for an embedded struct would misplace every later member."""
    global _spec_sizes
    if t.endswith("*"):
        return 4
    name = t.replace("struct ", "").strip()
    if name in SIZES:
        return SIZES[name]
    if _spec_sizes is None:
        import sys
        sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
        import typespec
        _spec_sizes = {n: d.size for n, d in typespec.parse_all()[0].items() if d.size}
    if name in _spec_sizes:
        return _spec_sizes[name]
    if name.startswith(("I3D_", "S_")) and name not in ("I3D_frame",):
        raise ValueError(f"ls3d_ghidra: unknown size of embedded struct {name}")
    return 4


def emit():
    classes, order = parse_classes()
    known = set()
    typedefs, structs = [], []
    for name in order:
        vt = []
        for owner, (mname, ret, params) in all_slots(classes, name):
            if mname is None:
                vt.append(f"    void *Slot{ret.upper()};")
                continue
            # Inline function pointer: the decompiler does not look through a typedef
            # of a function type when typing an indirect call.
            ps = [f"struct {owner} *self"] + parse_params(params, known)
            vt.append(f"    {gh(ret, known)} (__stdcall *{mname})({', '.join(ps)});")
        lines = [f"struct {name} {{", f"    struct {name}_vtbl *vtable;"]
        pos = 4
        for off, (ftype, fname) in sorted(all_fields(classes, name).items()):
            if off < pos:
                continue
            if off > pos:
                lines.append(f"    unsigned char _pad_{pos:04x}[{off - pos}];")
            t = gh(ftype, known)
            lines.append(f"    {t} {fname};" if not t.endswith("*") else f"    {t}{fname};")
            pos = off + field_size(t)
        lines.append("};")
        structs.append(f"struct {name}_vtbl {{\n" + "\n".join(vt) + "\n};\n" + "\n".join(lines))
    fwd = sorted((known | set(order) | {f"{n}_vtbl" for n in order}) - {"S_FrameData", "I3D_bbox"})
    return ("// LS3D engine interfaces (generated by _re/tools/ls3d_ghidra.py from src/ls3d/*.h)\n"
            + "\n".join(f"struct {n};" for n in fwd) + "\n" + FRAME_DATA + "\n"
            + "\n".join(typedefs) + "\n" + "\n".join(structs) + "\n")


if __name__ == "__main__":
    out = emit()
    open(os.path.join(ROOT, "ls3d_ghidra.h"), "w").write(out)
    print(out)
