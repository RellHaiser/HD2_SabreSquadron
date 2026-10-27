"""Recognise tiny compiler-generated helper functions (VC6 STL template instantiations,
inline accessors) by the shape of their decompiled body and give them generic names.

  python stl_idioms.py            write _re/names/zz_idioms.txt (merged by names.py)

Names carry the address suffix because each template instantiation is a separate
function: e.g. vector_size_p_0041fb80 (std::vector<T*>::size), vector_at_p_..., etc.
Entries already named in another names/*.txt file are skipped.
"""
import json
import os
import re

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DEC = os.path.join(ROOT, "decomp")

P = r"param_1"
PATTERNS = [
    # std::vector<T> size(): (last - first) >> log2(sizeof(T))
    ("vector_size", re.compile(
        r"^\{ if \(\*\(int \*\)\(param_1 \+ 4\) == 0\) \{ return 0; \} return "
        r"(?:\(\w+\))?\(?\*\(int \*\)\(param_1 \+ 8\) - \*\(int \*\)\(param_1 \+ 4\)\)? (?:>> (\d+)|/ (\d+)); \}$")),
    # std::vector<T> operator[]: first + i * sizeof(T)
    ("vector_at", re.compile(
        r"^\{ return \*\(int \*\)\(\(int\)this \+ 4\) \+ param_1(?: \* (\w+))?; \}$")),
    # std::vector<T> _Tidy(): delete storage, null the three pointers
    ("vector_tidy", re.compile(
        r"^\{ operator_delete\(\*\(void \*\*\)\(param_1 \+ 4\)\); \*\(undefined4 \*\)\(param_1 \+ 4\) = 0; "
        r"\*\(undefined4 \*\)\(param_1 \+ 8\) = 0; \*\(undefined4 \*\)\(param_1 \+ 0xc\) = 0; return; \}$")),
    # std::vector<T> clear() for trivially destructible T: last = first
    ("vector_clear", re.compile(
        r"^\{ \*\(undefined4 \*\)\(param_1 \+ 8\) = \*\(undefined4 \*\)\(param_1 \+ 4\); return; \}$")),
    # std::_Destroy range on trivially destructible elements / empty inline
    ("empty_function", re.compile(r"^\{ return; \}$")),
    # std::_Construct(p, v) for 4-byte POD
    ("construct_copy4", re.compile(
        r"^\{ if \(param_1 != \(undefined4 \*\)0x0\) \{ \*param_1 = \*param_2; \} return; \}$")),
    # std::uninitialized_copy for 4-byte POD
    ("uninitialized_copy4", re.compile(
        r"^\{ for \(; param_1 != param_2; param_1 = param_1 \+ 1\) \{ if \(param_3 != \(undefined4 \*\)0x0\) "
        r"\{ \*param_3 = \*param_1; \} param_3 = param_3 \+ 1; \} return; \}$")),
    # std::uninitialized_fill_n for 4-byte POD
    ("uninitialized_fill_n4", re.compile(
        r"^\{ for \(; param_2 != 0; param_2 = param_2 \+ -1\) \{ if \(param_1 != \(undefined4 \*\)0x0\) "
        r"\{ \*param_1 = \*param_3; \} param_1 = param_1 \+ 1; \} return; \}$")),
    # std::copy for 4-byte POD
    ("copy4", re.compile(
        r"^\{ for \(; param_1 != param_2; param_1 = param_1 \+ 1\) \{ \*param_3 = \*param_1; "
        r"param_3 = param_3 \+ 1; \} return; \}$")),
    # trivial getters
    ("get_field", re.compile(r"^\{ return \*\((?:undefined\d|int|uint|float|short|ushort|char|byte|undefined) \*\)"
                             r"\(param_1 \+ (0x[0-9a-f]+|\d+)\); \}$")),
    ("get_first_field", re.compile(r"^\{ return \*param_1; \}$")),
    ("field_address", re.compile(r"^\{ return param_1 \+ (0x[0-9a-f]+|\d+); \}$")),
    ("set_field", re.compile(r"^\{ \*\((?:undefined\d|int|uint|float|short|char|byte) \*\)\(\(int\)this \+ "
                             r"(0x[0-9a-f]+|\d+)\) = param_1; return; \}$")),
    ("identity", re.compile(r"^\{ return param_1; \}$")),
]

SIZES = {"2": 4, "3": 8, "4": 16, "5": 32}


def body_of(code):
    lines = [x.strip() for x in code.split("\n")]
    # drop the signature line and local declarations
    out = []
    started = False
    for ln in lines:
        if not started:
            if ln == "{":
                started = True
                out.append("{")
            continue
        if not ln or ln.startswith("//"):
            continue
        if re.match(r"^[\w ]+[\s\*]+\w+(?: \[\w+\])?;$", ln) and not ln.startswith("return"):
            continue
        out.append(ln)
    return " ".join(out)


def main():
    taken = set()
    for p in os.listdir(os.path.join(ROOT, "names")):
        if p.endswith(".txt") and p != "zz_idioms.txt":
            for raw in open(os.path.join(ROOT, "names", p), encoding="utf-8"):
                line = raw.split("#", 1)[0].strip()
                if line:
                    taken.add(line.split()[0].lower().zfill(8))
    out = []
    counts = {}
    for fn in sorted(os.listdir(DEC)):
        a = fn[:8]
        if a in taken:
            continue
        d = json.load(open(os.path.join(DEC, fn), encoding="utf-8"))
        if not (d.get("name") or "").startswith("FUN_"):
            continue
        code = d.get("decompiled_code") or ""
        if code.count("\n") > 16:
            continue
        body = body_of(code)
        for kind, rx in PATTERNS:
            m = rx.match(body)
            if not m:
                continue
            name = kind
            if kind == "vector_size":
                shift = m.group(1)
                name += "_e%d" % SIZES.get(shift, 0) if shift else "_e%s" % m.group(2)
            elif kind == "vector_at":
                name += "_e%s" % (int(m.group(1), 0) if m.group(1) else 1)
            elif kind in ("get_field", "field_address", "set_field"):
                name += "_%x" % int(m.group(1), 0)
            out.append(f"{a} {name}_{a[2:]}")
            counts[kind] = counts.get(kind, 0) + 1
            break
    path = os.path.join(ROOT, "names", "zz_idioms.txt")
    with open(path, "w", encoding="utf-8") as f:
        f.write("# GENERATED by tools/stl_idioms.py — compiler-generated helpers named by body shape.\n")
        f.write("\n".join(out) + "\n")
    print(len(out), counts)


if __name__ == "__main__":
    main()
