"""Make the generated tier-1 sources compile (i686 g++ -fpermissive), mechanically.

Loop: generate -> compile every file (precompiled header, parallel) -> parse
diagnostics -> patch -> repeat. Patches:
  * "void value not ignored"            callee is re-declared as returning uint32_t
  * "invalid cast from type A to type B" C cast replaced by gh_bitcast<B>(...)
  * "cannot convert A to B"             explicit (B) cast inserted at the error column
  * undeclared stack0x*/LAB_*/*Ram*     declared (local buffer / extern placeholder)
Functions that still fail after the last round are replaced by a stub that keeps
the translated text inside #if 0 for later hand porting.

Usage: python fix_tier1.py [--lo HEX --hi HEX] [--rounds N] [--jobs N]
"""
import argparse
import concurrent.futures as cf
import json
import os
import re
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC = os.path.join(os.path.dirname(ROOT), "src")
T1 = os.path.join(SRC, "tier1")
FIXUPS = os.path.join(ROOT, "tier1_fixups.json")
GXX = r"C:\nim-2.2.6\dist\mingw32\bin\g++.exe"
FLAGS = ["-m32", "-std=c++17", "-fpermissive", "-w", "-fmax-errors=400", "-I" + SRC]

ERR_RE = re.compile(r"^(?P<file>[^:]+(?::\\[^:]+)?):(?P<line>\d+):(?P<col>\d+): error: (?P<msg>.*)$")


def load_fixups():
    return json.load(open(FIXUPS)) if os.path.exists(FIXUPS) else {"returns_value": []}


def save_fixups(f):
    json.dump(f, open(FIXUPS, "w"), indent=1, sort_keys=True)


def build_pch():
    r = subprocess.run([GXX, *FLAGS, "-x", "c++-header", os.path.join(T1, "tier1_decls.h"),
                        "-o", os.path.join(T1, "tier1_decls.h.gch")], capture_output=True, text=True)
    errs = [l for l in r.stderr.splitlines() if "error" in l]
    return errs


def compile_one(path):
    r = subprocess.run([GXX, *FLAGS, "-c", path, "-o", os.devnull], capture_output=True, text=True,
                       cwd=SRC)
    out = []
    for line in r.stderr.splitlines():
        m = ERR_RE.match(line.strip())
        if m:
            out.append((int(m.group("line")), int(m.group("col")), m.group("msg"), m.group("file")))
    return path, out


def function_ranges(lines):
    """[(start_line_idx, end_line_idx_exclusive, address)] for each '// 0xADDR' block."""
    marks = [(i, ln[5:13]) for i, ln in enumerate(lines) if ln.startswith("// 0x") and len(ln) >= 13]
    out = []
    for k, (i, addr) in enumerate(marks):
        end = marks[k + 1][0] if k + 1 < len(marks) else len(lines)
        out.append((i, end, addr))
    return out


def owner(ranges, idx):
    for s, e, a in ranges:
        if s <= idx < e:
            return s, e, a
    return None


def operand_end(text, i):
    """End index of the unary/postfix expression starting at text[i]."""
    n = len(text)
    while i < n and text[i] == " ":
        i += 1
    while i < n and text[i] in "*&-!~":
        i += 1
    if i < n and text[i] == "(":
        start = i
        depth = 0
        while i < n:
            if text[i] == "(":
                depth += 1
            elif text[i] == ")":
                depth -= 1
                if depth == 0:
                    i += 1
                    break
            i += 1
        # a C cast "(T *)" is a prefix: the operand continues after it
        if re.fullmatch(r"\(\s*(?:struct\s+)?[A-Za-z_][\w ]*?\s*\**\s*\)", text[start:i]):
            return operand_end(text, i)
    else:
        while i < n and (text[i].isalnum() or text[i] == "_" or text[i] == "."):
            i += 1
    # postfix: [..], ->x, .x, (args)
    while i < n:
        if text.startswith("->", i):
            i += 2
            while i < n and (text[i].isalnum() or text[i] == "_"):
                i += 1
        elif text[i] == "." and i + 1 < n and (text[i + 1].isalpha() or text[i + 1] == "_"):
            i += 1
            while i < n and (text[i].isalnum() or text[i] == "_"):
                i += 1
        elif text[i] in "[(":
            close = "]" if text[i] == "[" else ")"
            depth = 0
            open_ch = text[i]
            while i < n:
                if text[i] == open_ch:
                    depth += 1
                elif text[i] == close:
                    depth -= 1
                    if depth == 0:
                        i += 1
                        break
                i += 1
        else:
            break
    return i


def strip_aka(t):
    return re.sub(r"\s*\{aka '[^']*'\}", "", t).replace("tier1::", "").strip()


def patch_line(line, col, msg):
    """Return a patched line or None."""
    c = col - 1
    if msg.startswith("switch quantity not an integer"):
        k = line.find("switch")
        if k < 0:
            return None
        p = line.find("(", k)
        depth, end = 0, -1
        for i in range(p, len(line)):
            depth += {"(": 1, ")": -1}.get(line[i], 0)
            if depth == 0:
                end = i
                break
        if p < 0 or end < 0:
            return None
        return line[:p + 1] + "gh_switch(" + line[p + 1:end] + ")" + line[end:]
    m = re.match(r"invalid cast from type '(.+?)'(?: \{aka '[^']*'\})? to type '(.+?)'", msg)
    if m:
        to = strip_aka(m.group(2))
        # find the cast "(to)" at or before the column (spacing around '*' varies)
        base = to.rstrip("* ")
        stars = to.count("*")
        pat = r"\(\s*" + re.escape(base) + r"\s*" + r"\*\s*" * stars + r"\)"
        found = [k for k in re.finditer(pat, line) if k.start() <= c + 1] or list(re.finditer(pat, line))
        if not found:
            return None
        k = found[-1].start()
        start = found[-1].end()
        end = operand_end(line, start)
        return line[:k] + f"gh_bitcast<{to}>(" + line[start:end].strip() + ")" + line[end:]
    m = re.match(r"cannot convert '(.+?)'(?: \{aka '[^']*'\})? to '(.+?)'", msg)
    if m:
        to = strip_aka(m.group(2))
        if to.endswith(")") or "(" in to:
            return None
        end = operand_end(line, c)
        if end <= c:
            return None
        # the column may point into a member access: cast the whole postfix expression
        c = expr_start(line, c)
        frm = strip_aka(m.group(1))
        cast = f"gh_bitcast<{to}>(" if ("float" in (frm + to) or "double" in (frm + to)) else f"({to})("
        return line[:c] + cast + line[c:end] + ")" + line[end:]
    return None


def expr_start(line, c):
    """Start of the postfix expression (a.b, a->b, f(x).y, a[i].b) whose last part begins at c."""
    i = c
    while True:
        if i >= 2 and line[i - 2:i] == "->":
            k = i - 2
        elif i >= 1 and line[i - 1] == ".":
            k = i - 1
        else:
            return i
        while k > 0 and line[k - 1] == " ":
            k -= 1
        while k > 0 and line[k - 1] in ")]":
            close = line[k - 1]
            opening = "(" if close == ")" else "["
            depth, m = 0, k - 1
            while m >= 0:
                if line[m] == close:
                    depth += 1
                elif line[m] == opening:
                    depth -= 1
                    if depth == 0:
                        break
                m -= 1
            if m < 0:
                return i
            k = m
        while k > 0 and (line[k - 1].isalnum() or line[k - 1] == "_"):
            k -= 1
        i = k


def callee_at(line, col):
    """Name of the function called in `line` nearest after the column."""
    calls = [(m.start(), m.group(1)) for m in re.finditer(r"\b([A-Za-z_]\w*)\s*\(", line)]
    calls = [x for x in calls if x[1] not in ("if", "while", "switch", "for", "return", "sizeof")]
    after = [x for x in calls if x[0] >= col - 1]
    pick = after[0] if after else (calls[-1] if calls else None)
    return pick[1] if pick else None


def fallback(lines, s, e):
    """Replace the function in lines[s:e] by a stub keeping the translated text."""
    block = lines[s:e]
    try:
        brace = block.index("{")
    except ValueError:
        return lines
    header = block[1:brace]
    sig = " ".join(header)
    ret = sig.split("(")[0].rsplit(None, 1)[0] if "(" in sig else "void"
    ret = re.sub(r"\b__(thiscall|stdcall|fastcall|cdecl)\b", "", ret).strip()
    stub = [block[0] + "  (tier1 fallback: translated text kept below, needs hand porting)"] + header + ["{"]
    if ret != "void":
        stub.append("  return {};")
    stub.append("}")
    stub.append("#if 0")
    stub += block[brace:]
    stub.append("#endif")
    return lines[:s] + stub + lines[e:]


RAM_BASES = {"i": "int", "u": "uint32_t", "f": "float", "c": "char", "b": "uint8_t", "s": "short",
             "us": "uint16_t", "uc": "uint8_t", "d": "double", "l": "int", "n": "int", "w": "uint16_t",
             "dw": "uint32_t", "by": "uint8_t"}


def placeholder_decl(name):
    """extern declaration of an undeclared global: Ghidra's xRam<addr> names carry the
    type in their prefix (piRam = int *, pcRam = char *, fRam = float, ...)."""
    k = name.find("Ram")
    if k > 0 and name.endswith(name[k + 3:]) and len(name) - k == 11:
        prefix = name[:k]
        stars = len(prefix) - len(prefix.lstrip("p"))
        base = prefix.lstrip("p")
        if not base:
            base = "u"          # plain pRam: some pointer
        ctype = RAM_BASES.get(base, "uint32_t")
        return f"extern {ctype} {'*' * stars}{name};"
    return f"extern uint8_t {name};"


def apply_saved(saved, saved_locals, saved_prelude):
    """Re-apply the patches of earlier runs to freshly generated files. A patch whose
    original line no longer exists (the function was re-exported) is skipped."""
    count = 0
    for f in sorted(os.listdir(T1)):
        if not (f.startswith("f_") and f.endswith(".cpp")):
            continue
        path = os.path.join(T1, f)
        lines = open(path, encoding="utf-8").read().split("\n")
        changed = False
        for s, e, a in sorted(function_ranges(lines), key=lambda r: -r[0]):
            for old, new in saved.get(a, []):
                for i in range(s, e):
                    if lines[i] == old:
                        lines[i] = new
                        count += 1
                        changed = True
                        break
            names = saved_locals.get(a)
            if names:
                try:
                    b = lines.index("{", s, e)
                except ValueError:
                    continue
                for nm in names:
                    lines.insert(b + 1, f"  uint8_t {nm}[64];")
                changed = True
        if saved_prelude and "namespace tier1 {" in lines:
            k = lines.index("namespace tier1 {")
            body = "\n".join(lines)
            for nm in sorted(saved_prelude):
                if re.search(r"\b" + re.escape(nm) + r"\b", body):
                    lines.insert(k + 1, placeholder_decl(nm))
                    changed = True
        if changed:
            open(path, "w", encoding="utf-8").write("\n".join(lines))
    return count


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--lo", default="401000")
    ap.add_argument("--hi", default="7e2000")
    ap.add_argument("--rounds", type=int, default=5)
    ap.add_argument("--jobs", type=int, default=8)
    ap.add_argument("--no-generate", action="store_true")
    args = ap.parse_args()

    fix = load_fixups()
    saved = fix.setdefault("patches", {})        # address -> [[old line, new line], ...]
    saved_locals = fix.setdefault("locals", {})  # address -> [stack0x names]
    saved_prelude = set(fix.setdefault("prelude", []))
    regenerate = not args.no_generate
    for rnd in range(args.rounds + 1):
        if regenerate:
            subprocess.run([sys.executable, os.path.join(ROOT, "tools", "gen_tier1.py"),
                            "--lo", args.lo, "--hi", args.hi], check=True)
            regenerate = False
            n = apply_saved(saved, saved_locals, saved_prelude)
            print(f"re-applied {n} saved patches", flush=True)
        pch_errors = build_pch()
        if pch_errors:
            print("header errors:\n" + "\n".join(pch_errors[:30]))
            return
        files = sorted(os.path.join(T1, f) for f in os.listdir(T1) if f.startswith("f_") and f.endswith(".cpp"))
        with cf.ThreadPoolExecutor(args.jobs) as ex:
            results = dict(ex.map(compile_one, files))
        total = sum(len(v) for v in results.values())
        bad_files = sum(1 for v in results.values() if v)
        print(f"round {rnd}: {total} errors in {bad_files}/{len(files)} files", flush=True)
        if total == 0:
            break
        last = rnd == args.rounds
        if last:
            with open(os.path.join(ROOT, "tier1_errors.log"), "w") as log:
                for path, errs in sorted(results.items()):
                    lines = open(path, encoding="utf-8").read().split("\n")
                    for ln, col, msg, _ in errs:
                        src = lines[ln - 1].strip() if ln - 1 < len(lines) else ""
                        log.write(f"{os.path.basename(path)}:{ln}:{col}: {msg}\n    {src[:160]}\n")
        for path, errs in results.items():
            if not errs:
                continue
            lines = open(path, encoding="utf-8").read().split("\n")
            ranges = function_ranges(lines)
            failing = set()
            patched_lines = set()
            prelude = set()
            local_decls = {}
            # rightmost errors first so columns stay valid
            for ln, col, msg, _ in sorted(errs, key=lambda x: (x[0], -x[1])):
                idx = ln - 1
                if idx >= len(lines):
                    continue
                own = owner(ranges, idx)
                if not own:
                    continue
                if last:
                    failing.add(own)
                    continue
                m = re.match(r"'([\w]+)' was not declared", msg)
                if m:
                    name = m.group(1)
                    if name.startswith("stack0x"):
                        local_decls.setdefault(own, set()).add(name)
                        continue
                    if name.startswith(("LAB_", "switchD_", "caseD_")) or re.search(r"Ram[0-9a-f]{8}$", name):
                        prelude.add(name)
                        continue
                if idx in patched_lines:
                    continue
                new = patch_line(lines[idx], col, msg)
                if new is not None and new != lines[idx]:
                    saved.setdefault(own[2], []).append([lines[idx], new])
                    lines[idx] = new
                    patched_lines.add(idx)
                else:
                    failing.add(own)
            # apply local declarations
            for own, names in sorted(local_decls.items(), key=lambda x: -x[0][0]):
                s, e, a = own
                try:
                    b = lines.index("{", s, e)
                except ValueError:
                    continue
                for nm in sorted(names):
                    lines.insert(b + 1, f"  uint8_t {nm}[64];")
                saved_locals[a] = sorted(set(saved_locals.get(a, [])) | names)
            if prelude:
                k = lines.index("namespace tier1 {")
                for nm in sorted(prelude):
                    lines.insert(k + 1, placeholder_decl(nm))
                saved_prelude |= prelude
            # in the last round every function still failing becomes a stub
            if failing and last:
                fail_addrs = {a for _, _, a in failing}
                for s, e, a in sorted(function_ranges(lines), key=lambda r: -r[0]):
                    if a in fail_addrs:
                        lines = fallback(lines, s, e)
            open(path, "w", encoding="utf-8").write("\n".join(lines))
        fix["prelude"] = sorted(saved_prelude)
        save_fixups(fix)
    # final tally
    files = sorted(os.path.join(T1, f) for f in os.listdir(T1) if f.startswith("f_") and f.endswith(".cpp"))
    stubs = sum(open(f, encoding="utf-8").read().count("(tier1 fallback:") for f in files)
    print(f"done; {stubs} functions fell back to stubs")


if __name__ == "__main__":
    main()
