"""Automatic class recovery from the decompiled code.

  1. vtables     : addresses stored into an object's first dword (`*this = &PTR_FUN_x`)
  2. ctors/dtors : functions doing that store (dtor: first store, ctor: last store)
  3. inheritance : a ctor that calls another class's ctor on `this` first
  4. members     : virtual slots + ctors/dtors + __thiscall functions called on `this`
                   from a member (propagated to a fixed point; ambiguous ones dropped)
  5. layout      : field offsets/types from `*(T *)((int)this + N)` style accesses,
                   object size from `operator_new(N)` / `_aligned_malloc(N,16)` + ctor call

Writes _re/types/90_auto.spec (classes) and _re/auto_members.json (address -> class).
Classes already described in hand-written specs (same vtable) are left alone, and
_re/class_names.json maps vtable address -> chosen class name.

Usage: python auto_classes.py
"""
import collections
import json
import os
import re
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import typespec  # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DEC = os.path.join(ROOT, "decomp")
TEXT_LO, TEXT_HI = 0x401000, 0x7E2000

# `*this = &PTR_FUN_x;` before types are applied, `this->vtable = (X_vtbl *)&PTR_FUN_x;` after.
VT_STORE = re.compile(r"(?m)^\s*(\*param_1|\*\([\w ]+\*+\)this|\*this|\*\(\w+ \*\*\*\)param_1|"
                      r"(?:this|param_1)->vtable|\*\(\w+ \*\*\)(?:this|param_1)) = "
                      r"(?:\([\w ]+\*+\))?&PTR_[\w]*?([0-9a-f]{8});")
ALLOC = re.compile(r"(\w+) = (?:\(\w+ \*\))?(?:operator_new|_aligned_malloc)\((0x[0-9a-f]+|\d+)")
CALL_ON_THIS = re.compile(r"\b(FUN_[0-9a-f]{8}|[A-Za-z_][\w:]*)\((?:\([\w ]+\*\))?(this|param_1)[,)]")
FIELD_DEREF = re.compile(r"\*\((?P<type>[\w ]+?)\s*(?P<stars>\*+)\)\(\(int\)(?P<obj>this|param_1) \+ (?P<off>0x[0-9a-f]+|\d+)\)")
FIELD_ADDR = re.compile(r"(?<![\*\w])\((?P<type>[A-Z][\w]*) \*\)\(\(int\)(?P<obj>this|param_1) \+ (?P<off>0x[0-9a-f]+|\d+)\)")
FIELD_PLAIN = re.compile(r"\*\((?P<type>[\w ]+?) \*\)(?P<obj>this|param_1)\b(?! \+)")

TYPE_SIZE = {"char": 1, "byte": 1, "undefined1": 1, "uint8_t": 1, "bool": 1, "uchar": 1,
             "short": 2, "ushort": 2, "undefined2": 2, "uint16_t": 2, "word": 2,
             "int": 4, "uint": 4, "undefined4": 4, "uint32_t": 4, "float": 4, "dword": 4, "long": 4,
             "ulong": 4, "double": 8, "longlong": 8, "ulonglong": 8, "undefined8": 8, "uint64_t": 8}
SPEC_TYPE = {"char": "int8_t", "byte": "uint8_t", "undefined1": "uint8_t", "uint8_t": "uint8_t",
             "bool": "bool", "uchar": "uint8_t", "short": "int16_t", "ushort": "uint16_t",
             "undefined2": "uint16_t", "uint16_t": "uint16_t", "word": "uint16_t", "int": "int",
             "uint": "uint32_t", "undefined4": "uint32_t", "uint32_t": "uint32_t", "float": "float",
             "dword": "uint32_t", "long": "int", "ulong": "uint32_t", "double": "double",
             "longlong": "int64_t", "ulonglong": "uint64_t", "undefined8": "uint64_t",
             "uint64_t": "uint64_t"}


def load(addr):
    p = os.path.join(DEC, addr + ".json")
    return json.load(open(p, encoding="utf-8")) if os.path.exists(p) else None


def sig_info(code):
    head = code.split("{", 1)[0]
    conv = "thiscall" if "__thiscall" in head else ("fastcall" if "__fastcall" in head else "other")
    return conv, head


def main():
    # the previous output must not count as hand-written classes
    old = os.path.join(ROOT, "types", "90_auto.spec")
    if os.path.exists(old):
        os.remove(old)
    index = json.load(open(os.path.join(ROOT, "index.json")))
    starts = {int(a, 16) for a in index}
    codes = {}
    for a in index:
        ai = int(a, 16)
        if TEXT_LO <= ai < TEXT_HI:
            d = load(a)
            if d and d.get("decompiled_code"):
                codes[a] = d["decompiled_code"].replace("\r", "")

    # ---- 1/2: vtable stores
    stores = collections.defaultdict(list)          # func -> [vt in order]
    for a, code in codes.items():
        for m in VT_STORE.finditer(code):
            stores[a].append(m.group(2))
    vtables = sorted({v for vs in stores.values() for v in vs})
    vt_set = {int(v, 16) for v in vtables}

    blobs = [(0x80F000, open(os.path.join(ROOT, "raw", "rdata_0080f000.bin"), "rb").read()),
             (0x832000, open(os.path.join(ROOT, "raw", "data_00832000.bin"), "rb").read())]

    def dw(x):
        for base, b in blobs:
            if base <= x < base + len(b) - 3:
                return struct.unpack_from("<I", b, x - base)[0]
        return 0

    slots = {}
    for v in vtables:
        x, lst = int(v, 16), []
        while True:
            t = dw(x)
            if not (TEXT_LO <= t < 0x80F000 and t in starts):
                break
            if lst and x in vt_set:
                break
            lst.append(f"{t:08x}")
            x += 4
        slots[v] = lst

    # ---- ctor / dtor classification and inheritance
    owner = {}                                     # func -> vt (class)
    role = {}
    for a, vs in stores.items():
        code = codes[a]
        returns_self = re.search(r"\breturn (this|param_1);", code) is not None
        deleting = re.search(r"& 1\) != 0", code) is not None
        if returns_self and not deleting:
            owner[a], role[a] = vs[-1], "ctor"     # ctors store their own vtable last
        else:
            owner[a], role[a] = vs[0], "dtor"      # dtors store their own vtable first
    base_of = {}
    for a, r in role.items():
        body = codes[a].split("{", 1)[1]
        for m in CALL_ON_THIS.finditer(body):
            callee = m.group(1)
            ca = callee[4:] if callee.startswith("FUN_") else None
            if ca and role.get(ca) == r and owner.get(ca) and owner[ca] != owner[a]:
                base_of.setdefault(owner[a], owner[ca])
                break

    def ancestors(v):
        seen = []
        while v in base_of and base_of[v] not in seen and base_of[v] != v:
            v = base_of[v]
            seen.append(v)
        return seen

    # ---- virtual slots: a function belongs to the base-most class whose vtable lists it
    for v, lst in slots.items():
        for f in lst:
            cur = owner.get(f)
            if cur is None:
                owner[f] = v
            elif cur != v and cur in [v] + ancestors(v):
                pass
            elif cur != v and v in ancestors(cur):
                owner[f] = v

    # ---- propagate through calls on `this`
    changed = True
    rounds = 0
    while changed and rounds < 10:
        changed = False
        rounds += 1
        votes = collections.defaultdict(set)
        for a, v in list(owner.items()):
            code = codes.get(a)
            if not code:
                continue
            conv, _ = sig_info(code)
            obj = "this" if conv == "thiscall" else ("param_1" if conv == "fastcall" else None)
            if not obj:
                continue
            for m in CALL_ON_THIS.finditer(code.split("{", 1)[1]):
                if m.group(2) != obj or not m.group(1).startswith("FUN_"):
                    continue
                ca = m.group(1)[4:]
                if ca in codes and ca not in owner and sig_info(codes[ca])[0] in ("thiscall", "fastcall"):
                    votes[ca].add(v)
        for ca, vs in votes.items():
            vs = set(vs)
            # prefer the common ancestor when several related classes call it
            if len(vs) == 1:
                owner[ca] = next(iter(vs))
                changed = True
            else:
                common = None
                for v in vs:
                    if all(v == w or v in ancestors(w) for w in vs):
                        common = v
                if common:
                    owner[ca] = common
                    changed = True

    # ---- layouts
    fields = collections.defaultdict(lambda: collections.defaultdict(collections.Counter))
    for a, v in owner.items():
        code = codes.get(a)
        if not code:
            continue
        conv, _ = sig_info(code)
        obj = "this" if conv == "thiscall" else ("param_1" if conv == "fastcall" else None)
        if not obj:
            continue
        for m in FIELD_DEREF.finditer(code):
            if m.group("obj") != obj:
                continue
            off = int(m.group("off"), 0)
            t = m.group("type").strip()
            if len(m.group("stars")) > 1:
                fields[v][off]["ptr"] += 1
            elif t in SPEC_TYPE:
                fields[v][off][SPEC_TYPE[t]] += 1
            else:
                fields[v][off]["ptr"] += 1 if t.endswith("*") else 0
        for m in FIELD_ADDR.finditer(code):
            if m.group("obj") != obj:
                continue
            t = m.group("type")
            if t in ("S_vector", "S_quat", "S_matrix", "S_plane", "S_vector4", "C_chunk"):
                fields[v][int(m.group("off"), 0)]["struct:" + t] += 1
    sizes = {}
    for a, code in codes.items():
        for m in ALLOC.finditer(code):
            var, size = m.group(1), int(m.group(2), 0)
            after = code[m.end():m.end() + 600]
            mm = re.search(r"\b(FUN_[0-9a-f]{8})\(" + re.escape(var) + r"\b", after)
            if mm and owner.get(mm.group(1)[4:]) and role.get(mm.group(1)[4:]) == "ctor":
                sizes.setdefault(owner[mm.group(1)[4:]], size)

    # ---- emit spec
    names = json.load(open(os.path.join(ROOT, "class_names.json"))) if os.path.exists(
        os.path.join(ROOT, "class_names.json")) else {}
    decls, _ = typespec.parse_all() if os.path.exists(typespec.SPEC_DIR) else ({}, [])
    manual_vt = {f"{d.vtable:08x}" for d in decls.values() if d.vtable} if decls else set()
    manual_vt |= {k for k in names if names[k] in decls}

    def cname(v):
        return names.get(v, f"C_auto_{v}")

    struct_sizes = {"S_vector": 12, "S_quat": 16, "S_matrix": 64, "S_plane": 16, "S_vector4": 16,
                    "C_chunk": 0x414}
    out = ["# Generated by _re/tools/auto_classes.py - do not edit (rename via _re/class_names.json).", ""]
    order = sorted(slots, key=lambda v: len(ancestors(v)))
    emitted = set()
    computed_size = {}
    for v in order:
        if v in manual_vt or cname(v) in decls:
            continue
        base = base_of.get(v)
        # fields of this class minus those of its ancestors
        mine = dict(fields[v])
        inherited_end = 0
        if base:
            for anc in [base] + ancestors(base):
                for off in fields[anc]:
                    mine.pop(off, None)
            inherited_end = sizes.get(base) or computed_size.get(base, 0)
        chosen = []
        for off in sorted(mine):
            if off < 4 or off < inherited_end:
                continue
            cnt = mine[off]
            t, _ = cnt.most_common(1)[0]
            if t.startswith("struct:"):
                t, sz = t[7:], struct_sizes[t[7:]]
            elif t == "ptr":
                t, sz = "void*", 4
            else:
                sz = typespec.SCALAR_SIZE.get(typespec.GH_SCALARS.get(t, t), 4)
            if chosen and chosen[-1][0] + chosen[-1][2] > off:
                continue                         # overlaps the previous field
            chosen.append((off, t, sz))
        size = sizes.get(v) or (max((o + s for o, t, s in chosen), default=4))
        size = max(size, max((o + s for o, t, s in chosen), default=4), inherited_end)
        computed_size[v] = size
        hdr = f"class {cname(v)}"
        if base and (base in emitted or base in manual_vt):
            hdr += f" : {cname(base)}"
        hdr += f" size=0x{size:x} vtable=0x{v}:{len(slots[v])}"
        out.append(hdr)
        for off, t, sz in chosen:
            out.append(f"  0x{off:03x} {t} m_{off:03x}")
        out.append("end")
        out.append("")
        emitted.add(v)

    open(os.path.join(ROOT, "types", "90_auto.spec"), "w").write("\n".join(out))
    members = {a: cname(v) for a, v in owner.items() if v not in manual_vt or True}
    json.dump({"members": members, "roles": role, "base_of": base_of, "slots": slots},
              open(os.path.join(ROOT, "auto_members.json"), "w"), indent=0)
    print(f"{len(vtables)} vtables, {len(emitted)} auto classes, {len(base_of)} inheritance links, "
          f"{len(owner)} member functions, {len(sizes)} sizes")


if __name__ == "__main__":
    main()
