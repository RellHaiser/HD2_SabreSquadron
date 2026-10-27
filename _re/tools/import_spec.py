"""Push the declarations of ONE spec file to Ghidra, without touching anything else.

apply_ghidra.py --types re-imports every spec, which would overwrite the (richer) Ghidra-side
layouts of the auto classes. Ghidra's /import_data_types also REPLACES every struct the source
references but does not define with an empty 1-byte struct, so this tool appends definitions
of every such external struct copied from Ghidra's current layout (recursively).

Usage: python import_spec.py 30_items.spec [--dry-run] [--show]
       python import_spec.py --structs A_vtbl,B_vtbl [--dry-run]   (generated definitions only;
       not for 90_auto.spec classes, whose re-import wipes the members added in Ghidra)
"""
import json
import os
import re
import sys
import urllib.parse
import urllib.request

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import apply_ghidra as ag  # noqa: E402
import typespec  # noqa: E402
import os as _os, sys as _sys
_sys.path.insert(0, _os.path.dirname(_os.path.abspath(__file__)))
import ghidra_program  # noqa: E402,F401  (adds program= to plugin requests)

GH_TYPES = {"uchar": "unsigned char", "uint": "unsigned int", "ushort": "unsigned short", "byte": "unsigned char",
            "undefined": "unsigned char", "undefined1": "unsigned char", "undefined2": "unsigned short",
            "undefined4": "unsigned int", "undefined8": "unsigned long long", "dword": "unsigned int",
            "word": "unsigned short", "ulong": "unsigned long", "longlong": "long long",
            "ulonglong": "unsigned long long", "sbyte": "char", "pointer": "void *"}
SCALARS = set(GH_TYPES.values()) | {"char", "short", "int", "long", "float", "double", "bool", "void",
                                    "long long", "unsigned long long"}


def layout(name):
    q = urllib.parse.urlencode({"struct_name": name})
    try:
        r = json.load(urllib.request.urlopen(f"{ag.BASE}/get_struct_layout?{q}", timeout=60))
    except Exception:  # noqa: BLE001
        return None
    return r if "fields" in r else None


_ENUMS = None
ENUMS_USED = set()
SIZED_INTS = {1: "unsigned char", 2: "unsigned short", 4: "unsigned int", 8: "unsigned long long"}


def spec_enums():
    """Spec enum name -> Ghidra C definition."""
    global _ENUMS
    if _ENUMS is None:
        decls, _ = typespec.parse_all()
        src = typespec.emit_ghidra_c(decls, [n for n, d in decls.items() if d.kind == "enum"], {})
        _ENUMS = {m.group(1): m.group(0) for m in re.finditer(r"(?s)typedef enum (\w+) \{.*?\} \1;", src)}
    return _ENUMS


def c_field(gtype, name, need, by_value, size=None):
    """Ghidra field type string -> C declaration. Referenced struct names go to `need`,
    those embedded by value (which must be defined first) also to `by_value`.
    Enums are NOT structs: an enum referenced as `struct E` is imported as an empty struct and
    shifts every later field, so spec enums are recorded in ENUMS_USED (their definitions
    must be emitted) and other by-value non-struct types become plain integers of `size`."""
    m = re.match(r"^(.*?)((?:\[\d+\])*)$", gtype)
    base, dims = m.group(1).strip(), m.group(2)
    ptr = "*" * base.count("*")
    base = base.replace("*", "").strip()
    ctype = GH_TYPES.get(base, base)
    if ctype in spec_enums():
        ENUMS_USED.add(ctype)
        return f"    {ctype} {ptr}{name}{dims};"
    if re.fullmatch(r"_func_\w+", ctype):
        print(f"warning: {name}: function type {ctype} copied as void*", file=sys.stderr)
        return f"    void *{name}{dims};"
    if ctype not in SCALARS and not ptr and size and not dims and layout(ctype) is None and size in SIZED_INTS:
        return f"    {SIZED_INTS[size]} {name};"
    if ctype not in SCALARS:
        need.add(ctype)
        if not ptr:
            by_value.add(ctype)
        ctype = f"struct {ctype}"
    return f"    {ctype} {ptr}{name}{dims};"


_GENERATED = None
_HELPERS = None


def generated_defs():
    """struct name -> definition text as the generators emit it (every spec except the auto
    classes, their vtables, and the LS3D interfaces from i3d.h/igraph.h)."""
    global _GENERATED, _HELPERS
    if _GENERATED is None:
        import ls3d_ghidra
        decls, order = typespec.parse_all()
        auto = ag.spec_names("90_auto.spec")
        src = typespec.emit_ghidra_c(decls, [n for n in order if n not in auto], ag.vtable_layouts(decls))
        src += ls3d_ghidra.emit()
        _GENERATED = {m.group(1): m.group(0) for m in re.finditer(r"(?ms)^struct (\w+) \{\n.*?^\};", src)}
        _HELPERS = {m.group(1): m.group(0) for m in re.finditer(r"(?m)^struct (\w+) \{[^\n]*\};$", src)}
    return _GENERATED


def generated_helpers():
    """The one-line helper structs (vector_X, C_str, std_string) of the generated definitions."""
    generated_defs()
    return _HELPERS


_AUTO_VTABLES = None


def auto_vtable_defs():
    """Generated vtable structs of the auto classes (90_auto.spec). Only `--structs` uses them:
    naming a slot's target renames the slot in the generated (tier 1) vtable, and Ghidra's copy
    must follow, or decompiled calls through `vfNN` no longer match."""
    global _AUTO_VTABLES
    if _AUTO_VTABLES is None:
        decls, order = typespec.parse_all()
        auto = ag.spec_names("90_auto.spec")
        src = typespec.emit_ghidra_c(decls, [n for n in order if n in auto], ag.vtable_layouts(decls))
        _AUTO_VTABLES = {m.group(1): m.group(0) for m in re.finditer(r"(?ms)^struct (\w+_vtbl) \{\n.*?^\};", src)}
    return _AUTO_VTABLES


def vtable_def(name, need):
    """Generated definition of a vtable struct (typed function pointers); Ghidra's layout only
    names the anonymous `_func_N` function types, which C cannot refer to, and copying them
    as `struct _func_N *` turns every slot into a pointer to an empty struct."""
    text = generated_defs().get(name)
    if text is None:
        return None
    need |= set(re.findall(r"struct (\w+)", text)) - {name}
    words = set(re.findall(r"\b(E_\w+)\b", text))
    ENUMS_USED.update(w for w in words if w in spec_enums())
    return text


def external_defs(src):
    """Definitions (dependency-ordered) of every struct `src` references but does not define."""
    defined = set(re.findall(r"struct (\w+) \{", src)) | set(re.findall(r"typedef enum \w+ \{.*?\} (\w+);", src, re.S))
    todo = set(re.findall(r"struct (\w+)\b", src)) - defined
    defs, deps, done = {}, {}, set()
    while todo:
        name = todo.pop()
        if name in done or name in defined:
            continue
        done.add(name)
        if name.endswith("_vtbl"):
            need = set()
            text = vtable_def(name, need)
            if text is not None:
                defs[name], deps[name] = text, set()
                todo |= need - done - defined
                continue
        lay = layout(name)
        if not lay or not lay["fields"]:
            continue            # unknown or already empty: an empty replacement changes nothing
        need, by_value = set(), set()
        body = [c_field(f["type"], f["name"], need, by_value, f.get("size")) for f in lay["fields"]]
        defs[name] = f"struct {name} {{\n" + "\n".join(body) + "\n};"
        deps[name] = by_value
        todo |= need - done - defined
    out, emitted = [], set(defined)
    while defs:
        ready = [n for n in sorted(defs) if deps[n] <= emitted | (deps[n] - set(defs))]
        if not ready:
            raise SystemExit(f"cyclic by-value structs: {sorted(defs)}")
        for n in ready:
            out.append(defs.pop(n))
            emitted.add(n)
    return out


def main():
    dry = "--dry-run" in sys.argv
    if "--structs" in sys.argv[2:]:
        # `X.spec --structs ...` used to import the whole spec (90_auto.spec: wipes members).
        raise SystemExit("--structs must be the first argument (it takes no spec file)")
    if sys.argv[1] == "--structs":
        # Re-import generated definitions of the named structs (e.g. damaged vtables).
        names = sys.argv[2].split(",")
        defs = []
        for n in names:
            text = vtable_def(n, set()) or auto_vtable_defs().get(n)
            if text is None:
                raise SystemExit(f"no generated definition of {n}")
            defs.append(text)
        # The one-line helpers (vector_X ...) the definitions use: a whole-spec import emits
        # them, but here a missing one would be copied from Ghidra or become an empty struct
        # (2026-10-18: C_MpMap's two new vectors turned into 1-byte placeholders).
        body = "\n".join(defs)
        used = [h for n, h in generated_helpers().items() if re.search(rf"\bstruct {n}\b", body)]
        src = "#pragma pack(push, 1)\n" + "\n".join(used + defs) + "\n"
    else:
        spec = sys.argv[1]
        decls, order = typespec.parse_all()
        path = os.path.join(typespec.SPEC_DIR, os.path.basename(spec))
        mine = [m.group(1) for line in open(path, encoding="utf-8")
                for m in [re.match(r"\s*(?:struct|class|union|enum)\s+(\w+)", line.split("#", 1)[0])] if m]
        vt = {k: v for k, v in ag.vtable_layouts(decls).items() if k in mine}
        src = typespec.emit_ghidra_c(decls, [n for n in order if n in mine], vt)
    # emitter output: pragma, one-line helper structs (C_str, std_string, vector_*), then the spec
    lines = [l for l in src.splitlines() if not l.startswith("#pragma")]
    helpers = [l for l in lines if re.match(r"struct \w+ \{.*\};$", l)]
    body = [l for l in lines if l not in helpers]
    ext = external_defs(src)
    enums = [spec_enums()[n] for n in sorted(ENUMS_USED) if not re.search(rf"typedef enum {n} \{{", src)]
    src = "\n".join(["#pragma pack(push, 1)"] + helpers + enums + ext + body + ["#pragma pack(pop)"]) + "\n"
    if "--show" in sys.argv:
        print(src)
    r = ag.post("/import_data_types", {"source": src, "category": "/", "dry_run": dry})
    print({k: v for k, v in r.items() if k != "messages"})
    print((r.get("messages") or "")[:3000])
    if not dry and ag.ok(r):
        # The parse named the slots' function types _func_1, _func_2 ... again: they replaced
        # the types of those names, and the next import would replace these. Unique names.
        import name_func_types
        vtables = {n: layout(n) for n in set(re.findall(r"struct (\w+_vtbl) \{", src))}
        print(f"named {name_func_types.name_types({n: l for n, l in vtables.items() if l})} slot types")
    if not dry:
        write_types_header()


def write_types_header():
    """Regenerates _re/ghidra_types.h (all specs as C), which gen_tier1.py takes the spec
    types from; only `apply_ghidra.py --types` (a full import) wrote it before."""
    import ls3d_ghidra
    decls, order = typespec.parse_all()
    auto = ag.spec_names("90_auto.spec")
    order = [n for n in order if n not in auto]
    src = typespec.emit_ghidra_c(decls, order, ag.vtable_layouts(decls)) + ls3d_ghidra.emit()
    open(os.path.join(ag.ROOT, "ghidra_types.h"), "w").write(src)


if __name__ == "__main__":
    main()
