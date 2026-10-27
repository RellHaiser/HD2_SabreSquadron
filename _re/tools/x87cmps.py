"""List the x87 compares of a function in order with the C++ condition (NaN-exact) under which the
following conditional jump is taken: FCOMP + FNSTSW AX + TEST AH,m / AND EAX,m / AND AH,m + Jcc
(JP JNP JZ JNZ), worked out over the four outcomes {lt, eq, gt, unordered}.
  `a` is the value of the last FLD (wrong when the compared value stayed on the FPU stack: check
  the listing), `b` the compared operand. C++ comparisons are false for NaN, so the printed
  condition can be written as is (e.g. "!(a < b)" -> NaN takes the jump).
Usage: python tools/asm.py dis 0x4ed270 > f.asm; python tools/x87cmps.py f.asm"""
import re
import sys

C0, C2, C3 = 0x100, 0x400, 0x4000                       # in AX after FNSTSW
OUTCOME = {"lt": C0, "eq": C3, "gt": 0, "un": C0 | C2 | C3}
NAMES = {
    frozenset(["lt"]): "a < b", frozenset(["lt", "eq"]): "a <= b", frozenset(["gt"]): "a > b",
    frozenset(["gt", "eq"]): "a >= b", frozenset(["eq"]): "a == b",
    frozenset(["gt", "eq", "un"]): "!(a < b)", frozenset(["gt", "un"]): "!(a <= b)",
    frozenset(["lt", "un"]): "!(a >= b)", frozenset(["lt", "eq", "un"]): "!(a > b)",
    frozenset(["lt", "gt", "un"]): "!(a == b)", frozenset(["lt", "gt"]): "a != b (ordered)",
}


def taken(mask, jcc):
    out = set()
    for name, flags in OUTCOME.items():
        v = flags & mask
        low = (v >> 8) if mask > 0xFF or v > 0xFF else v
        bits = bin(v).count("1")
        t = {"JZ": v == 0, "JE": v == 0, "JNZ": v != 0, "JNE": v != 0, "JP": bits % 2 == 0,
             "JPE": bits % 2 == 0, "JNP": bits % 2 == 1, "JPO": bits % 2 == 1}.get(jcc)
        if t is None:
            return None
        if t:
            out.add(name)
    return NAMES.get(frozenset(out), "{%s}" % ",".join(sorted(out)))


lines = [l.rstrip() for l in open(sys.argv[1]) if re.match(r"^[0-9a-f]{8}\s", l)]
last_fld = "?"
for i, l in enumerate(lines):
    addr, rest = l[:8], l[8:].strip()
    op = rest.split(None, 1)
    if not op:
        continue
    mn, args = op[0], (op[1] if len(op) > 1 else "")
    if mn in ("FLD", "FILD"):
        last_fld = args.split("  ;")[0]
    if not (mn.startswith("FCOM") or mn.startswith("FUCOM")):
        continue
    other = args.split("  ;")[0]
    note = args.split("  ; ")[1] if "  ; " in args else ""
    mask = None
    cond = target = "?"
    for k in range(i + 1, min(i + 8, len(lines))):
        r = lines[k][8:].strip()
        m = re.match(r"TEST AH,(0x[0-9a-f]+)|AND AH,(0x[0-9a-f]+)", r)
        if m:
            mask = int(m.group(1) or m.group(2), 16) << 8
            continue
        m = re.match(r"(?:AND|TEST) EAX,(0x[0-9a-f]+)", r)
        if m:
            mask = int(m.group(1), 16)
            continue
        m = re.match(r"(J\w+) (0x[0-9a-f]+)", r)
        if m and mask is not None:
            cond = taken(mask, m.group(1)) or "%s %x" % (m.group(1), mask)
            target = m.group(2)
            break
    print("%s a=%-30s b=%-26s -> %s if %-12s %s" % (addr, last_fld, other, target, cond, note))
