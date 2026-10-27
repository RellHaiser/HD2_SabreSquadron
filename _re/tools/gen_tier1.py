"""Tier-1 generator: mechanical, compilable C++ for every function not yet hand-ported.

Output (all inside namespace tier1):
  src/tier1/tier1_support.h   Ghidra helper semantics (CONCAT, SUB, piece access, ...)
  src/tier1/tier1_types.h     struct layouts (from _re/ghidra_types.h + fetched Ghidra layouts)
  src/tier1/tier1_decls.h     prototypes of every function and extern of every global
  src/tier1/f_XXXXXXXX.cpp    one file per original source file (XXXXXXXX = first address)

Functions listed as ported in _re/ported.txt (one address per line) are skipped:
their clean versions live in the hand-written sources.

Usage: python gen_tier1.py [--lo HEX --hi HEX]
"""
import argparse
import json
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import ghidra2cpp as g  # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PORT = os.path.dirname(ROOT)
OUT = os.path.join(PORT, "src", "tier1")
DEC = os.path.join(ROOT, "decomp")

GAME_LO, GAME_HI = 0x401000, 0x7E2000      # CRT/STL runtime above GAME_HI is not translated

# FID / import names -> standard C library names
CRT_NAMES = {
    "_strrchr": "strrchr", "_strncmp": "strncmp", "_strncat": "strncat", "_strncpy": "strncpy",
    "_strlen": "strlen", "_memset": "memset", "_malloc": "malloc", "_strcmp": "strcmp",
    "_rand": "rand", "operator_new": "tier1_operator_new", "operator_delete": "tier1_operator_delete",
    "__ftol": "tier1_ftol", "__allmul": "tier1_allmul", "__aulldiv": "tier1_aulldiv",
    "__allrem": "tier1_allrem", "__aullrem": "tier1_aullrem", "__allshl": "tier1_allshl",
    "_chkstk": "tier1_chkstk", "__CxxThrowException_8": "tier1_throw",
}


def ident(name):
    """Qualified/odd Ghidra name -> a C++ identifier usable in tier1."""
    name = CRT_NAMES.get(name, name)
    name = name.replace("::~", "__dtor_").replace("~", "dtor_")
    name = name.replace("::", "__")
    name = re.sub(r"operator\*=", "OperatorMulAssign", name)
    name = re.sub(r"operator\*", "OperatorMul", name)
    name = re.sub(r"\W", "_", name)
    return name


# ----------------------------------------------------------------------------- helpers
def load_json(path, default=None):
    return json.load(open(path, encoding="utf-8")) if os.path.exists(path) else default


def ported_set():
    p = os.path.join(ROOT, "ported.txt")
    if not os.path.exists(p):
        return set()
    return {ln.strip().lower().zfill(8) for ln in open(p) if ln.strip() and not ln.startswith("#")}


def file_boundaries(funcs):
    """Original source files start at each static initializer (members of the CRT
    initializer table: runs of code pointers in .data)."""
    import struct
    blob = open(os.path.join(ROOT, "raw", "data_00832000.bin"), "rb").read()
    starts = set()
    n = len(blob) // 4
    for i in range(n):
        v = struct.unpack_from("<I", blob, i * 4)[0]
        if GAME_LO <= v < GAME_HI and v in funcs and funcs[v].get("size", 0) <= 48:
            starts.add(v)
    return sorted(starts)


SIG_RE = re.compile(r"^(?P<ret>.*?)\s*(?P<conv>__thiscall|__stdcall|__fastcall|__cdecl)?\s*(?P<name>[\w:~<>\*=]+)\s*\((?P<params>.*)\)\s*$", re.S)


def split_header(text):
    """Return (signature line, body) of a translated function."""
    lines = text.split("\n")
    i = 0
    # (Ghidra writes its warnings as `//` lines, a few long lists as `/* */` lines.)
    while i < len(lines) and (lines[i].startswith("//") or lines[i].startswith("/*") or not lines[i].strip()):
        i += 1
    sig = []
    while i < len(lines) and lines[i] != "{":
        sig.append(lines[i].strip())
        i += 1
    return " ".join(sig), "\n".join(lines[i:])


# ----------------------------------------------------------------------------- body rewriting
PIECE_RE = re.compile(r"(\b[A-Za-z_]\w*(?:\.\w+|->\w+|\[[^\]]+\])*)\._(\d+)_(\d+)_")


def float_bits(literal):
    """Signed 32-bit pattern of a float literal (1.4013e-45 -> 1)."""
    import struct
    return struct.unpack("<i", struct.pack("<f", float(literal)))[0]


def rewrite_body(body, self_name="_this"):
    body = re.sub(r"\bthis\b", self_name, body)
    # sub-piece access  x._1_3_  -> GH_PIECE(x, 1, 3)
    body = PIECE_RE.sub(lambda m: f"GH_PIECE({m.group(1)},{m.group(2)},{m.group(3)})", body)
    # qualified names (Class::Method) -> identifiers
    body = re.sub(r"\b([A-Za-z_]\w*)::(~?[A-Za-z_]\w*(?:\*=|\*)?)", lambda m: ident(m.group(0)), body)
    # labels directly before a closing brace need a statement
    body = re.sub(r"(?m)^(\s*[A-Za-z_]\w*:)\s*\n(\s*\})", r"\1 ;\n\2", body)
    # LARGE_INTEGER halves: Ghidra names the inner struct `s`, windows.h `u`
    body = re.sub(r"\.s\.(LowPart|HighPart)\b", r".u.\1", body)
    # pointer-cast case labels (a pointer-typed switch is cast by the fixer)
    body = re.sub(r"\bcase \([^)]*\*\)(0x[0-9a-f]+|\d+):", r"case \1:", body)
    # float case labels are the integer bits of a mistyped switch (see gh_switch)
    body = re.sub(r"\bcase (-?\d+\.\d*(?:e[-+]?\d+)?|-?\d+e[-+]?\d+):", lambda m: f"case {float_bits(m.group(1))}:", body)
    # a NaN constant (NAN(x) is the unordered test)
    body = re.sub(r"\bNAN\b(?!\s*\()", "gh_nan", body)
    # null is kept as a plain 0: the decompiler compares it with integers too
    body = re.sub(r"\bnullptr\b", "0", body)
    # Ghidra comments
    body = re.sub(r"(?m)^\s*// WARNING.*\n", "", body)
    for k, v in CRT_NAMES.items():
        body = re.sub(r"\b" + re.escape(k) + r"\b", v, body)
    return body


def rewrite_signature(sig):
    m = SIG_RE.match(sig)
    if not m:
        return None
    ret = m.group("ret").strip() or "void"
    ret = re.sub(r"^\w+\s*\[10\]$", "long double", ret)     # x87 value returned in ST0
    ret = re.sub(r"^\w+\s*\[\d+\]$", "uint64_t", ret)
    # Callers sometimes consume EAX after a "void" call; every function yields a value.
    if ret == "void":
        ret = "uint32_t"
    conv = m.group("conv") or ""
    name = ident(m.group("name"))
    params = re.sub(r"\bthis\b", "_this", m.group("params")).strip()
    if params in ("", "void"):
        params = ""
    return flatten_templates(ret), conv, name, flatten_templates(params)


def flatten_templates(text):
    """Ghidra's RTTI class names (basic_streambuf<char,std::char_traits<char>_>) as `void`:
    their commas would split parameter lists."""
    while "<" in text:
        new = re.sub(r"[\w:]+<[^<>]*>", "void", text)
        if new == text:
            break
        text = new
    return text


def decl_params(params):
    """Declaration parameter list: every parameter defaulted, then `...`, so call sites
    passing more or fewer arguments than the callee's prototype still compile."""
    if not params:
        return "..."
    if params.endswith("..."):
        parts = [p.strip() for p in params[:-3].rstrip().rstrip(",").split(",") if p.strip()]
        return ", ".join(p + " = {}" for p in parts) + (", ..." if parts else "...")
    return ", ".join(p.strip() + " = {}" for p in params.split(",")) + ", ..."


def def_params(params):
    if not params:
        return "..."
    if params.endswith("..."):
        return params
    return params + ", ..."


def callconv(conv):
    # Tier-1 code is not ABI compatible with the original anyway; plain cdecl everywhere.
    return ""


ENGINE_IMPORTS = {"I3DGetDriver", "GetIGraph", "ISndGetDriver", "dbgPrintf", "dbgPrintf2", "RandFloat",
                  "GetEngineVersion", "TEV", "dtaWrite", "dtaSeek", "dtaRead", "dtaOpen", "dtaOpenWrite",
                  "dtaClose", "dtaCreate", "dtaDelete"}


CANON = {}      # address -> identifier the function is declared under
ALIASES = {}    # other identifiers a caller's (older) export may use -> address
THUNKS = set()  # lone-JMP thunks exported under their target's name


def find_thunks(funcs):
    """Ghidra shows a thunk it has no name for under its target's name, so the export
    gives both functions one name and they would be defined twice. A 5-byte function
    (JMP rel32) that a function of the same name lists as its caller is such a thunk."""
    by_name = {}
    for a, rec in funcs.items():
        by_name.setdefault(rec.get("name") or "", []).append(a)
    for a, rec in funcs.items():
        key = f"{a:08x}"
        if rec.get("size") == 5 and any(b != a and key in funcs[b].get("callers", [])
                                        for b in by_name[rec.get("name") or ""]):
            THUNKS.add(a)


def thunk_ident(a, fname):
    """<Target>Thunk, the convention for thunks named by hand (StaticInit_MeleeTrackerCreatorThunk)."""
    return fname + "Thunk" if a in THUNKS else fname


def note_aliases(a, *names):
    for n in names:
        if not n:
            continue
        for v in {ident(n), ident(g.sub_names(n))}:
            if v and not v.startswith("FUN_"):
                ALIASES[v] = a if ALIASES.get(v, a) == a else -1   # -1: ambiguous


ALIAS_TOKEN = re.compile(r"\b(?:[A-Za-z]\w*?__)?(?:thunk_)?FUN_([0-9a-f]{8})\b|\b[A-Za-z]\w*__\w+\b")


def resolve_callees(body):
    """Callers exported before a callee was renamed or moved into a class
    namespace still use the old name (FUN_x, C_auto_x__Old); use the declared one."""
    def rep(m):
        tok = m.group(0)
        if m.group(1):
            return CANON.get(int(m.group(1), 16), tok)
        if tok in DECLARED:
            return tok
        parts = tok.split("__")
        for k in range(len(parts)):
            a = ALIASES.get("__".join(parts[k:]))
            if a is not None and a != -1 and a in CANON:
                return CANON[a]
        return tok
    return ALIAS_TOKEN.sub(rep, body)


DECLARED = set()

SAME_CLASS_CALL = re.compile(r"(^|[^\w.>:])([A-Za-z_]\w*)(?=\s*\()", re.M)


def qualify_same_class(body, fname):
    """Ghidra prints calls to methods of the caller's own class without the class
    (`Attach(_this,...)` inside C_HumanWeaponPose); qualify those that are declared."""
    if "__" not in fname:
        return body
    cls = fname.split("__")[0]

    def rep(m):
        q = f"{cls}__{m.group(2)}"
        return m.group(1) + q if q in DECLARED and m.group(2) not in DECLARED else m.group(0)
    return SAME_CLASS_CALL.sub(rep, body)


def all_declarations(funcs):
    """Prototypes for every game function and every engine import thunk.
    C library and Win32 functions are left to the system headers."""
    syms = load_json(os.path.join(ROOT, "symbols.json"), {})
    out = {}
    for a, rec in funcs.items():
        key = f"{a:08x}"
        name = (syms.get(key) or {}).get("name") or rec.get("name") or ""
        if name.startswith(("Unwind@", "Catch@")) or (syms.get(key) or {}).get("crt"):
            continue
        game = GAME_LO <= a < GAME_HI
        thunk = 0x7E5290 <= a < 0x7E53C0
        if not (game or thunk or "::" in name or name in ENGINE_IMPORTS or name.startswith("FUN_")):
            continue
        d = load_json(os.path.join(DEC, key + ".json"))
        if not d or not d.get("decompiled_code"):
            continue
        sig = d["decompiled_code"].replace("\r", "")
        sig = sig[:sig.find("{")] if "{" in sig else sig
        sig = re.sub(r"(?m)^/\*.*?\*/\s*$", "", sig)
        for pat, rep in g.TYPE_MAP:
            sig = re.sub(pat, rep, sig)
        sig = " ".join(l.strip() for l in g.sub_names(sig).split("\n") if l.strip() and not l.startswith("//"))
        parts = rewrite_signature(sig)
        if parts:
            ret, conv, fname, params = parts
            fname = thunk_ident(a, fname)
            out[fname + "|" + params] = f"{ret} {fname}({decl_params(params)});"
            CANON[a] = fname
            DECLARED.add(fname)
            if a not in THUNKS:     # a thunk's exported names are its target's
                note_aliases(a, name, rec.get("name"), d.get("name"))
    return out


RETURNS_VALUE = set((load_json(os.path.join(ROOT, "tier1_fixups.json"), {}) or {}).get("returns_value", []))


def force_return(body):
    """Turn a void body into one returning 0 (callers use the EAX value)."""
    body = re.sub(r"\breturn;", "return 0;", body)
    k = body.rstrip().rfind("}")
    return body[:k] + "  return 0;\n" + body[k:]


# ----------------------------------------------------------------------------- types
BUILTIN_TYPES = {
    "void", "bool", "char", "short", "int", "long", "float", "double", "unsigned", "signed",
    "uint8_t", "uint16_t", "uint32_t", "uint64_t", "int8_t", "int16_t", "int32_t", "int64_t",
    "code", "float10", "int3", "uint3", "undefined3", "undefined5", "undefined6", "undefined7",
    "unkbyte10", "unkuint10", "size_t", "FILE", "wchar_t", "return", "else", "struct", "const",
    "uchar", "ushort", "ulong", "qword", "goto", "case", "default", "do", "while", "if", "for", "switch", "break", "continue", "sizeof",
    "new", "delete", "typedef", "static", "extern", "volatile", "register", "union", "enum", "class",
}


def is_windows_type(name):
    return (name.isupper() or re.match(r"^(_[A-Z]|tag[A-Z]|LP[A-Z]|P[A-Z]{2})", name) is not None
            or name in ("HWND__", "HINSTANCE__", "HDC__", "sockaddr", "sockaddr_in", "hostent", "fd_set",
                        "timeval", "in_addr", "WSAData"))


def spec_source():
    p = os.path.join(ROOT, "ghidra_types.h")
    return open(p).read() if os.path.exists(p) else ""


def restrict_class_names(spec_defined):
    """C_auto_XXXXXXXX structs are shown under their class_names.json name, except
    where the hand-written specs already define a (different) struct of that name."""
    g._class_names = {k: v for k, v in g.class_names().items() if v not in spec_defined}


C_KEYWORDS = {"class", "new", "delete", "this", "operator", "template", "typename", "default", "case",
              "switch", "public", "private", "protected", "virtual", "friend", "union", "struct", "enum"}


def layout_structs(spec_defined):
    """C definitions of the structures Ghidra used (ghidra_layouts.json, see
    fetch_types.py), renamed like the code and topologically ordered.
    Returns (forward names, definitions text, defined names)."""
    layouts = load_json(os.path.join(ROOT, "ghidra_layouts.json"), {}) or {}
    rename = lambda s: g.sub_names(s)  # noqa: E731  (C_auto_X -> class name)
    structs = {}
    for name, lay in layouts.items():
        cname = rename(name)
        if cname in spec_defined or is_windows_type(cname) or cname in BUILTIN_TYPES:
            continue
        # A renamed auto class can exist twice in Ghidra: the recovered layout under
        # C_auto_<vtable> and a stub under the chosen name. Keep the richer one.
        old = structs.get(cname)
        if old and (len(old["fields"]), old["size"]) >= (len(lay["fields"]), lay["size"]):
            continue
        structs[cname] = lay
    known = set(structs) | spec_defined

    def field_decl(f):
        t = rename(f["type"]).strip()
        fname = re.sub(r"\W", "_", f["name"] or f"field_{f['offset']:x}")
        if fname in C_KEYWORDS or fname[0].isdigit():
            fname = "f_" + fname
        dims = "".join(re.findall(r"\[\d+\]", t))
        base = re.sub(r"\[\d+\]", "", t).strip()
        for pat, rep in g.TYPE_MAP:
            base = re.sub(pat, rep, base)
        base = base.replace("uchar", "uint8_t") if base.startswith("uchar") else base
        stars = base.count("*")
        core = base.replace("*", "").strip()
        dep = None
        if stars == 0 and core not in BUILTIN_TYPES and core not in ("int", "char", "float", "double"):
            if core in known:
                dep = core
            else:
                return f"uint8_t {fname}[{max(f['size'], 1)}];", None, set()
        pointees = {core} if stars and core not in BUILTIN_TYPES and not is_windows_type(core) else set()
        return f"{base} {fname}{dims};", dep, pointees

    defs, deps, pointees = {}, {}, set()
    for cname, lay in structs.items():
        lines, d, pos = [], set(), 0
        vtable = cname.endswith("_vtbl")
        for f in sorted(lay["fields"], key=lambda f: f["offset"]):
            if f["offset"] < pos or f["size"] <= 0:
                continue
            if f["offset"] > pos:
                lines.append(f"    uint8_t _gap_{pos:04x}[{f['offset'] - pos}];")
            if (vtable and f["type"].strip() == "void *") or re.fullmatch(r"_func_\w+ \*", f["type"].strip()):
                f = dict(f, type="code *")       # slots are called directly
            text, dep, ptrs = field_decl(f)
            lines.append("    " + text)
            if dep and dep != cname:
                d.add(dep)
            pointees |= ptrs
            pos = f["offset"] + f["size"]
        if lay["size"] > pos:
            lines.append(f"    uint8_t _tail_{pos:04x}[{lay['size'] - pos}];")
        defs[cname] = f"struct {cname} {{\n" + "\n".join(lines) + "\n};"
        deps[cname] = {x for x in d if x in structs}

    order, state = [], {}

    def visit(n):
        if state.get(n) == 2:
            return
        if state.get(n) == 1:      # cycle by value cannot happen in valid layouts; break it
            return
        state[n] = 1
        for m in sorted(deps.get(n, ())):
            visit(m)
        state[n] = 2
        order.append(n)

    for n in sorted(defs):
        visit(n)
    pointees = {p for p in pointees if p not in known and re.match(r"^[A-Za-z_]\w*$", p)}
    text = "#pragma pack(push, 1)\n" + "\n".join(defs[n] for n in order) + "\n#pragma pack(pop)\n"
    text = callable_vtables(text)
    return sorted(structs), text, set(structs), pointees


def callable_vtables(text):
    """Vtable struct fields typed `void *` (from the specs / Ghidra) become `code *`, so
    the decompiled calls through them compile with any arguments."""
    def fix(m):
        body = re.sub(r"(?m)^(\s*)void \*\s*(\w+);", r"\1code *\2;", m.group(0))
        # typed slots (`int (*Name)(float dt);`): the decompiled calls pass `this` too
        return re.sub(r"(?m)^(\s*)[\w ]+?\s*\**\s*\(\*(\w+)\)\(.*\);", r"\1code *\2;", body)
    return re.sub(r"(?ms)^struct \w+_vtbl \{.*?^\};", fix, text)


def write_types(files, decl_texts=()):
    spec_c = callable_vtables(spec_source())
    defined = set(re.findall(r"\bstruct\s+(\w+)\s*\{", spec_c))
    # enums of the specs (typedef enum X {..} X;) must not get placeholder structs
    spec_enums = set(re.findall(r"\benum\s+(\w+)\s*\{", spec_c))
    referenced_ptr = set(re.findall(r"\bstruct\s+(\w+)\s*\*", spec_c))
    forward, layout_text, layout_defined, layout_pointees = layout_structs(defined)
    defined |= layout_defined
    used = set(layout_pointees)
    # Prototypes of hand-ported functions are declared too; their parameter types
    # (e.g. a `this` class that only those functions use) need placeholders as well.
    for text in decl_texts:
        used |= set(re.findall(r"([A-Za-z_]\w*)\s*\*+\s*\w+\s*=", text))
        used |= set(re.findall(r"^([A-Za-z_]\w*)\s*\*", text))
    for chunks in files.values():
        for text in chunks:
            used |= set(re.findall(r"\(([A-Za-z_]\w*)\s*\*+\)", text))
            used |= set(re.findall(r"(?m)^\s+([A-Za-z_]\w*)\s+\**[A-Za-z_]\w*(?:\s*\[[^\]]*\])*;", text))
            used |= set(re.findall(r"(?m)^[A-Za-z_][\w ]*?\b([A-Za-z_]\w*)\s*\*+\s*(?:__\w+\s+)?\w+\(", text))
            for params in re.findall(r"(?m)^[^\n]*\b\w+\(([^)]*)\)\s*$", text):
                used |= set(re.findall(r"([A-Za-z_]\w*)\s*\*+\s*\w+", params))
    # spec enums not (yet) in ghidra_types.h: plain integers are enough here
    import typespec
    enums = sorted(d.name for d in typespec.parse_all()[0].values()
                   if d.kind == "enum" and d.name not in defined
                   and not re.search(r"\b" + d.name + r"\b", spec_c))
    placeholders = sorted(t for t in (used | referenced_ptr)
                          if t not in defined and t not in enums and t not in spec_enums
                          and t not in BUILTIN_TYPES and not is_windows_type(t))
    with open(os.path.join(OUT, "tier1_types.h"), "w") as fh:
        fh.write("// Generated by _re/tools/gen_tier1.py - do not edit.\n#pragma once\n"
                 "#include \"tier1/tier1_support.h\"\nnamespace tier1 {\n\n")
        for t in enums:
            fh.write(f"typedef uint32_t {t};\n")
        fh.write("// Structures defined below (from Ghidra's layouts).\n")
        for t in forward:
            if t not in BUILTIN_TYPES and not is_windows_type(t) and t not in enums:
                fh.write(f"struct {t};\n")
        fh.write("// Types Ghidra knows only by name: 1-byte placeholders (pointer math is bytewise).\n")
        for t in placeholders:
            if t in forward:
                continue
            if re.fullmatch(r"_func_\w+", t):
                # Ghidra's function types of vtable slots: the slots are `code *`.
                fh.write(f"typedef code {t};\n")
            else:
                fh.write(f"struct {t} {{ uint8_t _unknown[1]; }};\n")
        fh.write("\n" + spec_c + "\n")
        fh.write("// Layouts fetched from Ghidra (_re/ghidra_layouts.json).\n" + layout_text)
        fh.write("\n} // namespace tier1\n")


# ----------------------------------------------------------------------------- main
def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--lo", default="401000")
    ap.add_argument("--hi", default=hex(GAME_HI))
    args = ap.parse_args()
    lo, hi = int(args.lo, 16), int(args.hi, 16)

    os.makedirs(OUT, exist_ok=True)
    index = load_json(os.path.join(ROOT, "index.json"), {})
    funcs = {int(a, 16): r for a, r in index.items()}
    ported = ported_set()
    starts = file_boundaries(funcs)

    restrict_class_names(set(re.findall(r"\bstruct\s+(\w+)\s*\{", spec_source())))
    find_thunks(funcs)
    decls = all_declarations(funcs)   # identifier -> declaration text
    files = {}            # file start -> [function text]
    referenced = set()
    data_syms = {v["name"]: int(k, 16) for k, v in load_json(os.path.join(ROOT, "symbols.json"), {}).items()
                 if v.get("data") and re.fullmatch(r"[A-Za-z_]\w*", v["name"])}

    for a in sorted(funcs):
        if not (lo <= a < hi):
            continue
        key = f"{a:08x}"
        if key in ported:
            continue
        rec = funcs[a]
        name = rec.get("name") or ""
        if name.startswith(("Unwind@", "Catch@")):
            continue
        d = load_json(os.path.join(DEC, key + ".json"))
        if not d or not d.get("decompiled_code"):
            continue
        text = g.translate_text(d["decompiled_code"], cosmetic=False)
        sig, body = split_header(text)
        parts = rewrite_signature(sig)
        if not parts:
            continue
        ret, conv, fname, params = parts
        fname = thunk_ident(a, fname)
        body = resolve_callees(qualify_same_class(rewrite_body(body), fname))
        if re.match(r"^\s*void\b", sig):
            body = force_return(body)
        decls[fname + "|" + params] = f"{ret} {fname}({decl_params(params)});"
        start = max([s for s in starts if s <= a], default=lo)
        files.setdefault(start, []).append(f"// 0x{key}\n{ret} {fname}({def_params(params)})\n{body}\n")
        referenced |= set(re.findall(r"\b(?:_?DAT|PTR_DAT|PTR_FUN|PTR_LAB|PTR)_[0-9a-f]{8}\b", body))
        referenced |= {t for t in re.findall(r"\b[gs]_\w+\b", body) if t in data_syms}

    # globals
    items = {int(i["address"], 16): i for i in load_json(os.path.join(ROOT, "raw", "data_items.json"), [])}
    # Globals the names files type with a structure (g_shotManager @ C_ShotManager): Ghidra's
    # decompilation accesses their members, so they need that type here as well.
    sym_types = {v["name"]: v.get("type", "") for v in load_json(os.path.join(ROOT, "symbols.json"), {}).values()
                 if v.get("data")}
    known_structs = set(re.findall(r"\bstruct\s+(\w+)\s*\{", spec_source())) | \
        set(load_json(os.path.join(ROOT, "ghidra_layouts.json"), {}) or {})
    # Arrays the names files type (g_carSurfaceOfMaterial @ int[256]): Ghidra indexes them, so they
    # are declared as arrays of their element type (a scalar, a known struct or a pointer to one).
    array_elems = {"int": "int", "uint": "uint32_t", "uint32_t": "uint32_t", "float": "float", "char": "char",
                   "uchar": "uint8_t", "uint8_t": "uint8_t", "byte": "uint8_t", "short": "short",
                   "ushort": "uint16_t", "void": "void"}

    def array_decl(sym):
        array = re.fullmatch(r"(.+?)\s*\[(\d+)\]", sym_types.get(sym, "").strip())
        base = array and re.fullmatch(r"([A-Za-z_]\w*)\s*(\**)", array.group(1).strip())
        if not base:
            return None
        name, stars = base.group(1), base.group(2)
        if name in known_structs:
            elem = f"{name} {stars}".strip()
        elif name in array_elems and (stars or name != "void"):
            elem = f"{array_elems[name]} {stars}".strip()
        else:
            return None
        return f"extern {elem} {sym}[{array.group(2)}];"

    gdecls = []
    for sym in sorted(referenced):
        addr = data_syms[sym] if sym in data_syms else int(sym.rsplit("_", 1)[1], 16)
        it = items.get(addr)
        t = (it or {}).get("type", "")
        if array_decl(sym):
            gdecls.append(array_decl(sym))
            continue
        typed = re.fullmatch(r"([A-Za-z_]\w*)\s*(\**)", sym_types.get(sym, "").strip())
        if typed and typed.group(1) in known_structs:
            ctype = f"{typed.group(1)} {typed.group(2)}".strip()
        elif sym.startswith("PTR_"):
            ctype = "uint8_t *"
        elif t in ("undefined4", "dword", "uint", "int"):
            ctype = "uint32_t"
        elif t in ("undefined2", "word", "ushort", "short"):
            ctype = "uint16_t"
        elif t in ("float",):
            ctype = "float"
        elif t in ("double",):
            ctype = "double"
        elif t.endswith("*") or t == "pointer" or t == "void *32":
            ctype = "uint8_t *"
        else:
            ctype = "uint8_t"
        gdecls.append(f"extern {ctype} {sym};")

    write_types(files, decls.values())

    with open(os.path.join(OUT, "tier1_decls.h"), "w") as fh:
        fh.write("// Generated by _re/tools/gen_tier1.py - do not edit.\n#pragma once\n"
                 "#include \"tier1/tier1_types.h\"\nnamespace tier1 {\n")
        fh.write("\n".join(gdecls) + "\n\n")
        fh.write("\n".join(decls[k] for k in sorted(decls)) + "\n}\n")

    written = set()
    for start, chunks in files.items():
        name = f"f_{start:08x}.cpp"
        written.add(name)
        with open(os.path.join(OUT, name), "w") as fh:
            fh.write("// Generated by _re/tools/gen_tier1.py - mechanical translation, not yet hand-ported.\n"
                     "#include \"tier1/tier1_decls.h\"\nnamespace tier1 {\n\n")
            fh.write("\n".join(chunks))
            fh.write("\n} // namespace tier1\n")
    # files of ranges that are hand-ported by now (or moved) must not linger
    stale = [f for f in os.listdir(OUT) if f.startswith("f_") and f.endswith(".cpp") and f not in written
             and lo <= int(f[2:10], 16) < hi]
    for f in stale:
        os.remove(os.path.join(OUT, f))
    print(f"{sum(len(c) for c in files.values())} functions in {len(files)} files, "
          f"{len(decls)} declarations, {len(gdecls)} globals, {len(stale)} stale files removed")


if __name__ == "__main__":
    main()
