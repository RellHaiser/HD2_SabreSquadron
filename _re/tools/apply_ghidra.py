"""Push the reverse-engineering state in _re/ into the Ghidra database.

Sources of truth (all hand-maintained):
  _re/types/*.spec   struct/class/enum layouts      -> import_data_types
  _re/symbols.json   functions and globals          -> rename / prototype / this-type / globals

symbols.json entry shapes (key = 8-digit hex address):
  {"name": "C_Module::C_Module", "proto": "void __thiscall C_Module(C_Module *this, int id, char *name)"}
  {"name": "g_modules", "type": "C_Module *[64]", "data": true}
  {"name": "Foo", "params": {"param_1": "count"}}

Re-running is safe: every operation is idempotent.

Usage: python apply_ghidra.py [--types] [--symbols] [--only ADDR,...] [--dry-run]
"""
import argparse
import json
import os
import re
import sys
import urllib.request

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import typespec  # noqa: E402
import os as _os, sys as _sys
_sys.path.insert(0, _os.path.dirname(_os.path.abspath(__file__)))
import ghidra_program  # noqa: E402,F401  (adds program= to plugin requests)

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BASE = "http://127.0.0.1:8089"


def post(path, body, retries=6):
    import time
    for attempt in range(retries):
        req = urllib.request.Request(BASE + path, data=json.dumps(body).encode(), method="POST",
                                     headers={"Content-Type": "application/json"})
        try:
            r = json.loads(urllib.request.urlopen(req, timeout=300).read())
        except Exception as e:  # noqa: BLE001
            r = {"error": str(e)}
        if "transaction" not in str(r.get("error", "")):
            return r
        time.sleep(1 + attempt)
    return r


def ok(r):
    return r.get("success") or r.get("status") == "success"


def spec_names(spec_file):
    import re
    path = os.path.join(typespec.SPEC_DIR, spec_file)
    return {m.group(1) for line in open(path, encoding="utf-8")
            for m in [re.match(r"\s*(?:struct|class|union|enum)\s+(\w+)", line.split("#", 1)[0])] if m}


def apply_types(dry, with_auto=False):
    """Import every spec (except the generated auto classes, whose Ghidra layouts are
    richer: members were added there by apply_members.py) plus the LS3D interfaces.
    Ghidra's import replaces every struct the source only references with an empty
    struct, so the current definitions of those are appended (import_spec.external_defs)."""
    import import_spec
    import ls3d_ghidra
    decls, order = typespec.parse_all()
    auto = set() if with_auto else spec_names("90_auto.spec")
    order = [n for n in order if n not in auto]
    # vtables of the auto classes too: only the *_vtbl struct is emitted for them
    vt = vtable_layouts(decls)
    src = typespec.emit_ghidra_c(decls, order, vt) + ls3d_ghidra.emit()
    open(os.path.join(ROOT, "ghidra_types.h"), "w").write(src)
    ext = import_spec.external_defs(src)
    head, _, rest = src.partition("\n")
    src = "\n".join([head] + ext + [rest])
    r = post("/import_data_types", {"source": src, "category": "/HD2", "dry_run": dry})
    print("types:", r.get("status"), r.get("types_added"), r.get("error") or "", (r.get("messages") or "")[:2000])
    if not dry and ok(r):
        # The slots' function types came back as _func_1, _func_2 ...: unique names, or the next
        # import replaces them (tools/name_func_types.py).
        import name_func_types
        vtables = {n: import_spec.layout(n) for n in set(re.findall(r"struct (\w+_vtbl) \{", src))}
        print("named", name_func_types.name_types({n: l for n, l in vtables.items() if l}), "slot types")


def vtable_layouts(decls):
    """class -> [(slot name, None)] built from symbols.json names of the vtable targets."""
    import struct
    syms = load_symbols()
    blobs = []
    for name, base in (("rdata", 0x80F000), ("data", 0x832000)):
        blobs.append((base, open(os.path.join(ROOT, "raw", f"{name}_{base:08x}.bin"), "rb").read()))

    def dw(a):
        for base, b in blobs:
            if base <= a < base + len(b):
                return struct.unpack_from("<I", b, a - base)[0]
        return 0

    def declared_slot(d, i):
        """(name, proto) a `vslot` line of the class or a base class gives slot i."""
        while d is not None:
            if i in d.slot_names:
                return d.slot_names[i]
            d = decls.get(d.base) if d.base else None
        return None

    def generic(nm):
        """Idiom names (empty_function_5d4c90, set_field_10_610230) say nothing about the slot."""
        import re
        return nm is None or re.match(r"^(FUN_|thunk_|LAB_)", nm) or re.search(r"_[0-9a-f]{6}$", nm)

    def slot_name(d, i, up=True, down=True):
        ds = declared_slot(d, i)
        if ds:
            return ds[0]
        s = syms.get(f"{dw(d.vtable + 4 * i):08x}")
        nm = s["name"].split("::")[-1] if s else None
        base = decls.get(d.base) if d.base else None
        if up and generic(nm) and base is not None and base.vtable and i < base.vtable_slots:
            bn = slot_name(base, i, down=False)
            if bn and not generic(bn) and bn != "_purecall":
                return bn
        if down and nm == "_purecall":
            # pure virtual: borrow the name an overriding (direct) subclass gives the slot
            for sub in decls.values():
                if sub.base == d.name and sub.vtable and i < sub.vtable_slots:
                    sn = slot_name(sub, i, up=False)
                    if sn and sn != "_purecall":
                        return sn
        return nm

    def slot_type(d, i):
        """Function pointer declaration (format string, %s = field name) from the
        prototype the names files give the slot's target, None if there is none."""
        s = syms.get(f"{dw(d.vtable + 4 * i):08x}")
        if s and s.get("proto") and s["name"] != "_purecall":
            return proto_pointer(s["proto"], decls)
        ds = declared_slot(d, i)
        return proto_pointer(ds[1], decls) if ds and ds[1] else None

    out = {}
    for d in decls.values():
        if not d.vtable:
            continue
        slots = []
        for i in range(d.vtable_slots):
            nm = slot_name(d, i)
            if nm and nm.startswith("~"):
                nm = "Destructor"
            if nm in [x[0] for x in slots]:
                nm = f"{nm}_{i:02x}"
            slots.append((nm or f"vf{i:02x}", slot_type(d, i)))
        out[d.name] = slots

    def abstract_layout(d):
        """vtable=0x0 class with spec subclasses: the slots all direct subclasses share."""
        kids = [out.get(k.name) or abstract_layout(k) for k in decls.values() if k.base == d.name]
        kids = [k for k in kids if k]
        if not kids:
            return None
        return kids[0][:min(len(k) for k in kids)]

    for d in decls.values():
        if d.vtable == 0 and d.kind == "class":
            lay = abstract_layout(d)
            if not lay and d.slot_names:
                # interface known only from `vslot` lines
                lay = [(d.slot_names[i][0], proto_pointer(d.slot_names[i][1], decls))
                       if i in d.slot_names else (f"vf{i:02x}", None)
                       for i in range(max(d.slot_names) + 1)]
            if lay:
                out[d.name] = lay
    return out


PROTO_SCALARS = {"uchar": "uint8_t", "uint": "uint32_t", "ushort": "uint16_t", "byte": "uint8_t",
                 "undefined": "uint8_t", "undefined2": "uint16_t", "undefined4": "uint32_t",
                 "dword": "uint32_t", "word": "uint16_t", "float10": "double", "code": "void"}


def proto_pointer(proto, decls):
    """'bool __thiscall Tick(C_X *this, float dt)' -> 'bool (*%s)(float dt)'.

    Ghidra's C parser takes no calling convention in a function pointer
    declarator, so the slot gets the default one (stdcall: callee pops, like
    thiscall) and leaves out the implicit `this` passed in ECX; call sites then
    decompile with the right stack arguments and return type."""
    import re
    if not proto:
        return None
    m = re.match(r"^(.*?)\s*(__thiscall|__stdcall|__cdecl|__fastcall)\s+~?[\w:]+\s*\((.*)\)\s*$", proto)
    if not m:
        return None
    ret, conv, params = m.groups()
    if conv in ("__cdecl", "__fastcall"):
        return None             # caller-cleaned / register arguments: no faithful plain form

    def ctype(t):
        t = re.sub(r"\bconst\b", "", t).strip()
        stars = t.count("*")
        base = re.sub(r"\s+", " ", t.replace("*", "")).strip()
        base = PROTO_SCALARS.get(base, base)
        if base in typespec.SCALAR_SIZE or base == "void":
            return base + (" " + "*" * stars if stars else "")
        return typespec.gh_type(base + " " + "*" * stars if stars else base, decls, {})

    plist = []
    for i, p in enumerate(params.split(",")):
        p = p.strip()
        if not p or p == "void":
            continue
        if p == "...":
            return None
        pm = re.match(r"^(.*?[\s\*])(\w+)$", p)
        if not pm:
            return None
        if conv == "__thiscall" and i == 0:
            continue            # `this`, passed in ECX
        plist.append(f"{ctype(pm.group(1))} {pm.group(2)}")
    return f"{ctype(ret)} (*%s)({', '.join(plist) if plist else 'void'})"


def vtable_length(d, decls):
    syms_vtl = load_symbols().get("__vtable_lengths__", {})
    return int(syms_vtl.get(d.name, 0)) or 1


_syms = None
_structs = None


def struct_names():
    global _structs
    if _structs is None:
        decls, _ = typespec.parse_all()
        _structs = {n for n, d in decls.items() if d.kind in ("struct", "class", "union")}
        _structs |= {"C_str", "C_str_rep", "std_string"}
    return _structs


def load_symbols():
    global _syms
    if _syms is None:
        p = os.path.join(ROOT, "symbols.json")
        _syms = json.load(open(p)) if os.path.exists(p) else {}
    return _syms


def make_thiscall(addr, dry):
    """Re-declare a function as __thiscall, keeping its stack parameters."""
    import re
    import urllib.request
    sig = json.loads(urllib.request.urlopen(f"{BASE}/get_functions?function={addr}&fields=signature",
                                            timeout=60).read()).get("signature", "")
    m = re.match(r"(.*?)\s*(__stdcall|__fastcall|__cdecl|__thiscall)\s+([^(]+)\((.*)\)$", sig)
    if not m:
        return
    ret, conv, name, params = m.groups()
    plist = [p.strip() for p in params.split(",") if p.strip() and p.strip() != "void"]
    if conv in ("__fastcall", "__thiscall") and plist:
        plist = plist[1:]          # ECX argument becomes `this`
    name = re.sub(r"\W", "_", name.split("::")[-1])
    proto = f"{ret} __thiscall {name}({', '.join(plist) if plist else 'void'})"
    post("/set_function_prototype", {"function_address": addr, "prototype": proto, "dry_run": dry})


def apply_symbols(dry, only=None):
    syms = load_symbols()
    n_ok = n_fail = 0
    for addr, s in syms.items():
        if addr.startswith("__"):
            continue
        if only and addr not in only:
            continue
        a = "0x" + addr
        results = []
        if s.get("data"):
            body = {"address": a, "name": s["name"], "strict_mode": "off", "dry_run": dry}
            if s.get("type"):
                body["type_name"] = s["type"]
            results.append(post("/set_global", body))
        elif s.get("thunk"):
            # import thunks keep their demangled name/namespace; only the prototype is fixed
            results.append(post("/set_function_prototype", {"function_address": a, "prototype": s["proto"],
                                                            "dry_run": dry}))
        else:
            full = s["name"]
            cls, _, short = full.rpartition("::")
            if cls and cls in struct_names():
                # class method: Ghidra puts it in the class namespace and types `this`
                r = post("/set_function_this_type", {"function_address": a, "this_type": cls, "dry_run": dry})
                if "no implicit 'this'" in str(r.get("error", "")):
                    make_thiscall(a, dry)
                    r = post("/set_function_this_type", {"function_address": a, "this_type": cls, "dry_run": dry})
                results.append(r)
                results.append(post("/rename_function", {"old_name": a, "new_name": short,
                                                         "strict_mode": "off", "dry_run": dry}))
            else:
                results.append(post("/rename_function", {"old_name": a, "new_name": full,
                                                         "strict_mode": "off", "dry_run": dry}))
            if s.get("proto"):
                results.append(post("/set_function_prototype", {"function_address": a, "prototype": s["proto"],
                                                                "dry_run": dry}))
            if s.get("params"):
                results.append(post("/rename_variables", {"function_address": a,
                                                          "variable_renames": s["params"], "dry_run": dry}))
        bad = [r for r in results if not ok(r)]
        if bad:
            n_fail += 1
            print(f"  {addr} {s['name']}: {[r.get('error') or r.get('message') for r in bad]}")
        else:
            n_ok += 1
    print(f"symbols: {n_ok} ok, {n_fail} with errors")


if __name__ == "__main__":
    ap = argparse.ArgumentParser()
    ap.add_argument("--types", action="store_true")
    ap.add_argument("--symbols", action="store_true")
    ap.add_argument("--only", default="")
    ap.add_argument("--dry-run", action="store_true")
    args = ap.parse_args()
    only = {x.strip().replace("0x", "").zfill(8) for x in args.only.split(",") if x.strip()} or None
    if args.types:
        apply_types(args.dry_run)
    if args.symbols:
        apply_symbols(args.dry_run, only)
