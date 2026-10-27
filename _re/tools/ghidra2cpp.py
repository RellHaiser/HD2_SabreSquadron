"""Mechanical Ghidra-pseudocode -> C++ translator.

Turns one exported decompilation into a C++ function definition:
  * Ghidra scalar types -> <cstdint> names
  * string labels (s_foo_0083xxxx) -> string literals
  * FUN_/DAT_ names -> names from _re/symbols.json
  * MSVC SEH frame bookkeeping (ExceptionList, EH state locals) removed
  * inlined CRT idioms (strlen / memcpy / memset / strcpy loops) -> library calls
  * empty trailing `return;` removed, `+ -N` -> `- N`

It is deliberately conservative: every rewrite preserves the original semantics
(including the final values of the pointer/counter variables an idiom leaves
behind), so the output is a faithful starting point for hand polishing.

Library use:  from ghidra2cpp import translate; text = translate(decomp_json)
CLI:          python ghidra2cpp.py ADDR [ADDR ...]
"""
import json
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DEC = os.path.join(ROOT, "decomp")

TYPE_MAP = [
    # pointer-to-array return types ("float (*) [4] Func(...)") -> element pointer
    (r"\b(\w+)\s*\(\*\)\s*\[\d+\]", r"\1 *"),
    (r"\bundefined8\b", "uint64_t"),
    (r"\bundefined4\b", "uint32_t"),
    (r"\bundefined2\b", "uint16_t"),
    (r"\bundefined1\b", "uint8_t"),
    (r"\bundefined\b", "uint8_t"),
    (r"\bulonglong\b", "uint64_t"),
    (r"\blonglong\b", "int64_t"),
    (r"\buint\b", "uint32_t"),
    (r"\bushort\b", "uint16_t"),
    (r"\bbyte\b", "uint8_t"),
    (r"\bdword\b", "uint32_t"),
    (r"\bword\b", "uint16_t"),
    (r"\bsbyte\b", "int8_t"),
    (r"\bfloat10\b", "long double"),
]

_strings = None
_symbols = None


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


def c_escape(s):
    out = []
    for ch in s:
        o = ord(ch)
        if ch == "\\":
            out.append("\\\\")
        elif ch == '"':
            out.append('\\"')
        elif ch == "\n":
            out.append("\\n")
        elif ch == "\r":
            out.append("\\r")
        elif ch == "\t":
            out.append("\\t")
        elif o < 0x20 or 0x7F <= o < 0x100:
            out.append(f"\\x{o:02x}\"\"")
        else:
            out.append(ch)
    return "".join(out)


# String labels keep the string's characters, which need not be identifier
# characters (s_>___0083f970).
STR_RE = re.compile(r"&?(?<![\w])[su]_[^\s,;()\[\]]*?_([0-9a-f]{8})\b")
FUN_RE = re.compile(r"\b(?:thunk_)?FUN_([0-9a-f]{8})\b")
DAT_RE = re.compile(r"\b_?(?:DAT|PTR_DAT|PTR_FUN|PTR_LAB|PTR)_([0-9a-f]{8})\b")
# pointer labels named after their target: PTR_ScalarDeletingDestructor_00817408
PTR_LABEL_RE = re.compile(r"\bPTR_[^\s,;()\[\]]*?_([0-9a-f]{8})\b")
# globals only: for locals and parameters the overlap view may reinterpret bits
OVERLAP_RE = re.compile(r"(?<![\w.>])_(g_[A-Za-z]\w*)\b")


def sub_names(code):
    st, syms = strings(), symbols()

    def srep(m):
        a = int(m.group(1), 16)
        if a in st:
            return '"' + c_escape(st[a]) + '"'
        if re.fullmatch(r"&?[su]_\w*", m.group(0)):
            return m.group(0)
        # not a known string: the bytes at that address
        return f"&DAT_{a:08x}" if m.group(0).startswith("&") else f"((char *)&DAT_{a:08x})"

    def nrep(m):
        s = syms.get(m.group(1))
        return s["name"] if s else m.group(0)

    def prep(m):
        s = syms.get(m.group(1))
        return s["name"] if s else f"PTR_{m.group(1)}"

    code = STR_RE.sub(srep, code)
    code = FUN_RE.sub(nrep, code)
    code = DAT_RE.sub(nrep, code)
    code = PTR_LABEL_RE.sub(prep, code)
    # A leading '_' marks a global accessed wider than its declared type ("overlap");
    # the plain name is the same storage.
    code = OVERLAP_RE.sub(lambda m: m.group(1), code)
    names = class_names()
    if names:
        code = re.sub(r"\bC_auto_00([0-9a-f]{6})\b", lambda m: names.get("00" + m.group(1), m.group(0)), code)
    return code


_class_names = None


def class_names():
    global _class_names
    if _class_names is None:
        p = os.path.join(ROOT, "class_names.json")
        _class_names = json.load(open(p)) if os.path.exists(p) else {}
    return _class_names


# --------------------------------------------------------------------------- SEH
SEH_LINES = [
    re.compile(r"^\s*\w+ = ExceptionList;\s*$"),
    re.compile(r"^\s*ExceptionList = &?\w+;\s*$"),
    re.compile(r"^\s*\w+ = &LAB_[0-9a-f]+;\s*$"),
    re.compile(r"^\s*\w+ = &?FUN_[0-9a-f]+;\s*$"),  # handler pointer stored into the frame
    re.compile(r"^\s*\w+ = &?Unwind_[0-9a-f]+;\s*$"),
]


def strip_seh(lines):
    """Remove the MSVC C++ EH frame setup/teardown and the EH state variable."""
    text = "\n".join(lines)
    if "ExceptionList" not in text:
        return lines
    # EH frame locals: the variable that receives ExceptionList, and the handler slot.
    frame_vars = set(re.findall(r"(\w+) = ExceptionList;", text))
    frame_vars |= set(re.findall(r"(\w+) = &LAB_[0-9a-f]+;", text))
    # EH state variable: a local only ever assigned integer constants and never read.
    state_vars = set()
    for v in set(re.findall(r"\b(local_[0-9a-f]+|uStack_[0-9a-f]+)\b", text)):
        uses = re.findall(r"\b" + v + r"\b[^\n]*", text)
        if uses and all(re.fullmatch(v + r" = (?:0x[0-9a-f]+|-?\d+);", u.strip()) or
                        re.fullmatch(r"(?:undefined4|int|uint|uint32_t|int32_t) " + v + ";", u.strip())
                        for u in uses):
            state_vars.add(v)
    out = []
    for ln in lines:
        s = ln.strip()
        if any(p.match(ln) for p in SEH_LINES) and ("ExceptionList" in ln or any(re.match(r"\s*" + v + r"\b", ln) for v in frame_vars)):
            continue
        if any(re.fullmatch(v + r" = (?:0x[0-9a-f]+|-?\d+);", s) for v in state_vars):
            continue
        if any(re.fullmatch(r"[\w ]+\*? ?" + v + r"(?: \[\d+\])?;", s) for v in frame_vars | state_vars):
            continue
        out.append(ln)
    return out


# --------------------------------------------------------------------------- idioms
def _rx(pat):
    return re.compile(pat, re.M)


# MSVC inline strlen:  n = 0xffffffff; p = s; do { if (n == 0) break; n = n - 1; c = *p; p = p + 1; } while (c != '\0');
STRLEN_RE = _rx(
    r"^(?P<ind>[ \t]*)(?P<n>\w+) = 0xffffffff;\n"
    r"(?P=ind)(?P<p>\w+) = (?P<src>[^;\n]+);\n"
    r"(?P=ind)do \{\n"
    r"(?P=ind)  if \((?P=n) == 0\) break;\n"
    r"(?P=ind)  (?P=n) = (?P=n) - 1;\n"
    r"(?P=ind)  (?P<c>\w+) = \*(?P=p);\n"
    r"(?P=ind)  (?P=p) = (?P=p) \+ 1;\n"
    r"(?P=ind)\} while \((?P=c) != '\\0'\);\n")

# n = ~n  right after the strlen idiom
NOT_RE = _rx(r"^(?P<ind>[ \t]*)(?P<n>\w+) = ~(?P=n);\n")

# dword copy + byte copy loops (inline memcpy)
MEMCPY_RE = _rx(
    r"^(?P<ind>[ \t]*)for \((?P<i>\w+) = (?P<len>\w+) >> 2; (?P=i) != 0; (?P=i) = (?P=i) - 1\) \{\n"
    r"(?P=ind)  \*\((?:uint32_t|undefined4|int|uint) \*\)(?P<d>\w+) = \*\((?:uint32_t|undefined4|int|uint) \*\)(?P<s>\w+);\n"
    r"(?P=ind)  (?P=s) = (?P=s) \+ (?:4|1);\n"
    r"(?P=ind)  (?P=d) = (?P=d) \+ (?:4|1);\n"
    r"(?P=ind)\}\n"
    r"(?P=ind)for \((?P<j>\w+) = (?P=len) & 3; (?P=j) != 0; (?P=j) = (?P=j) - 1\) \{\n"
    r"(?P=ind)  \*\(?(?:\((?:char|uint8_t|undefined1|byte) \*\))?(?P=d)\)? = \*\(?(?:\((?:char|uint8_t|undefined1|byte) \*\))?(?P=s)\)?;\n"
    r"(?P=ind)  (?P=s) = (?P=s) \+ 1;\n"
    r"(?P=ind)  (?P=d) = (?P=d) \+ 1;\n"
    r"(?P=ind)\}\n")

# dword fill loop (inline memset of N dwords)
MEMSET_RE = _rx(
    r"^(?P<ind>[ \t]*)for \((?P<i>\w+) = (?P<cnt>0x[0-9a-f]+|\d+); (?P=i) != 0; (?P=i) = (?P=i) \+ -1\) \{\n"
    r"(?P=ind)  \*(?P<d>\w+) = (?P<val>0|0x0);\n"
    r"(?P=ind)  (?P=d) = (?P=d) \+ 1;\n"
    r"(?P=ind)\}\n")


STRLEN_LOOP = [
    re.compile(r"^(?P<ind>[ \t]*)do \{$"),
    re.compile(r"^[ \t]*if \((?P<n>\w+) == 0\) break;$"),
    re.compile(r"^[ \t]*(?P<n>\w+) = (?P=n) - 1;$"),
    re.compile(r"^[ \t]*(?P<c>\w+) = \*(?P<p>\w+);$"),
    re.compile(r"^[ \t]*(?P<p>\w+) = (?P=p) \+ 1;$"),
    re.compile(r"^[ \t]*\} while \((?P<c>\w+) != '\\0'\);$"),
]


def _mentions(line, var):
    return re.search(r"\b" + re.escape(var) + r"\b", line) is not None


def rewrite_strlen(lines):
    """MSVC's inline strlen (repne scasb) tolerant of interleaved scheduling.

    n = 0xffffffff; p = src; do {...} while (c != '\\0'); n = ~n;
    becomes  n = strlen(src) + 1;  (p's final value is re-materialised if still read).
    """
    i = 0
    while i + 6 <= len(lines):
        ms = [STRLEN_LOOP[k].match(lines[i + k]) for k in range(6)]
        if not all(ms):
            i += 1
            continue
        n, c, p = ms[1].group("n"), ms[3].group("c"), ms[3].group("p")
        if ms[2].group("n") != n or ms[4].group("p") != p or ms[5].group("c") != c:
            i += 1
            continue
        ind = ms[0].group("ind")
        # nearest preceding n = 0xffffffff and p = src at the same indentation
        init_n = init_p = None
        src = None
        for j in range(i - 1, max(-1, i - 12), -1):
            ln = lines[j]
            if not ln.startswith(ind) or ln.startswith(ind + " ") or ln.strip().endswith("{") or ln.strip() == "}":
                break
            if init_n is None and re.fullmatch(re.escape(ind + n) + r" = 0xffffffff;", ln):
                init_n = j
                continue
            m = re.fullmatch(re.escape(ind + p) + r" = ([^;]+);", ln)
            if init_p is None and m:
                init_p, src = j, m.group(1)
                continue
            if _mentions(ln, n) or _mentions(ln, p) or (src and _mentions(ln, src.split()[0].strip("(*&"))):
                break
        if init_n is None or init_p is None:
            i += 1
            continue
        # following n = ~n
        not_n = None
        for j in range(i + 6, min(len(lines), i + 12)):
            ln = lines[j]
            if re.fullmatch(re.escape(ind + n) + r" = ~" + re.escape(n) + ";", ln):
                not_n = j
                break
            if _mentions(ln, n) or not ln.startswith(ind) or ln.strip() in ("}", "{"):
                break
        if not_n is None:
            i += 1
            continue
        new = [f"{ind}{n} = strlen({src}) + 1;", f"{ind}{p} = {src} + {n};"]
        drop = {init_n, init_p, not_n}
        block = lines[:i] + new + lines[i + 6:]
        # indices shift: rebuild while dropping the init/not lines
        rebuilt = []
        for k, ln in enumerate(lines):
            if k in drop:
                continue
            if k == i:
                rebuilt.extend(new)
            if i <= k < i + 6:
                continue
            rebuilt.append(ln)
        del block
        lines = rebuilt
        i = 0
    return lines


def rewrite_idioms(code):
    code = "\n".join(rewrite_strlen(code.split("\n")))

    def memcpy_rep(m):
        ind, i, ln, d, s, j = (m.group(k) for k in ("ind", "i", "len", "d", "s", "j"))
        return (f"{ind}memcpy({d}, {s}, {ln});\n"
                f"{ind}{s} = {s} + {ln};\n"
                f"{ind}{d} = {d} + {ln};\n"
                f"{ind}{i} = 0;\n"
                f"{ind}{j} = 0;\n")

    code = MEMCPY_RE.sub(memcpy_rep, code)

    def memset_rep(m):
        ind, i, cnt, d = m.group("ind"), m.group("i"), m.group("cnt"), m.group("d")
        return (f"{ind}memset({d}, 0, {cnt} * sizeof(*{d}));\n"
                f"{ind}{d} = {d} + {cnt};\n"
                f"{ind}{i} = 0;\n")

    code = MEMSET_RE.sub(memset_rep, code)
    return code


# --------------------------------------------------------------------------- dead stores
def drop_dead_locals(code):
    """Remove assignments to locals that are never read, and their declarations."""
    changed = True
    while changed:
        changed = False
        names = set(re.findall(r"\b((?:[a-z]{1,3}Var\d+|local_[0-9a-f]+|[a-z]{1,4}Stack_[0-9a-f]+))\b", code))
        for v in names:
            reads = 0
            for m in re.finditer(r"\b" + v + r"\b", code):
                line_start = code.rfind("\n", 0, m.start()) + 1
                line_end = code.find("\n", m.end())
                line = code[line_start:line_end].strip()
                if re.fullmatch(v + r" = [^;]*;", line) and not re.search(r"\(", line.split("=", 1)[1]):
                    continue  # pure store without side effects
                if re.fullmatch(r"[\w ]+\*? ?" + v + r"(?: \[\d+\])?;", line):
                    continue  # declaration
                reads += 1
            if reads == 0:
                new = re.sub(r"(?m)^[ \t]*" + v + r" = [^;()]*;\n", "", code)
                new = re.sub(r"(?m)^[ \t]*[\w ]+\*? ?" + v + r"(?: \[\d+\])?;\n", "", new)
                if new != code:
                    code = new
                    changed = True
    return code


# --------------------------------------------------------------------------- cosmetics
def cosmetics(code):
    code = re.sub(r"\+ -(0x[0-9a-f]+|\d+)", r"- \1", code)
    code = re.sub(r"(?m)^(\s*)(\w+) = \2 \+ 1;$", r"\1\2++;", code)
    code = re.sub(r"(?m)^(\s*)(\w+) = \2 - 1;$", r"\1\2--;", code)
    # only for a single simple operand: `x = x * y + z` must stay as it is
    code = re.sub(r"(?m)^(\s*)(\w+) = \2 ([+\-*/|&^]) ([\w.\[\]]+|0x[0-9a-f]+);$", r"\1\2 \3= \4;", code)
    code = re.sub(r"\(\w+ \*\)0x0\b", "nullptr", code)
    # trailing bare return at the end of a void function
    code = re.sub(r"\n  return;\n\}\s*$", "\n}\n", code)
    return code


def keep_used_declarations(code, original):
    """Re-add declarations removed by the SEH pass for variables still referenced."""
    decl_lines = {}
    for ln in original.split("\n"):
        m = re.fullmatch(r"\s+[\w ]+?[\s\*]+(\w+)(?: \[[^\]]+\])*;", ln)
        if m:
            decl_lines[m.group(1)] = ln
    head, sep, body = code.partition("{\n")
    missing = [ln for v, ln in decl_lines.items()
               if re.search(r"\b" + re.escape(v) + r"\b", body) and
               not re.search(r"(?m)^\s+[\w ]+?[\s\*]+" + re.escape(v) + r"(?: \[[^\]]+\])*;", body)]
    if not missing or not sep:
        return code
    return head + sep + "\n".join(missing) + "\n" + body


def translate_text(code, cosmetic=True):
    code = code.replace("\r", "").strip("\n") + "\n"
    for pat, rep in TYPE_MAP:
        code = re.sub(pat, rep, code)
    typed = code
    lines = strip_seh(code.split("\n"))
    code = "\n".join(lines)
    code = keep_used_declarations(code, typed)
    code = rewrite_idioms(code)
    code = cosmetics(code)
    if cosmetic:
        code = drop_dead_locals(code)
    code = sub_names(code)
    code = re.sub(r"\n\s*\n", "\n", code)
    return code


def translate(d):
    a = d["address"]
    body = translate_text(d.get("decompiled_code") or "")
    return f"// 0x{a}\n{body}"


DECL_LINE = re.compile(r"^\s+[\w ]+?[\s\*]+\w+(?: \[[^\]]+\])*;$")


def dense(d):
    """Compact reading form: no local declarations, no blank lines, no WARNING lines."""
    out = []
    for ln in translate(d).split("\n"):
        if DECL_LINE.match(ln) and ln.strip().split()[0].rstrip(";") not in ("return", "break", "continue", "goto", "delete"):
            continue
        if ln.startswith("// WARNING"):
            continue
        out.append(ln)
    return "\n".join(out)


if __name__ == "__main__":
    args = sys.argv[1:]
    fn = translate
    if args and args[0] == "-d":
        fn, args = dense, args[1:]
    if args and args[0] == "--range":
        lo, hi = int(args[1], 16), int(args[2], 16)
        sel = sorted(f[:8] for f in os.listdir(DEC) if f.endswith(".json") and lo <= int(f[:8], 16) < hi)
    else:
        sel = [x.lower().replace("0x", "").zfill(8) for x in args]
    for a in sel:
        d = json.load(open(os.path.join(DEC, a + ".json"), encoding="utf-8"))
        idx = {}
        print(fn(d))
