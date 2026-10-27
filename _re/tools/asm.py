"""Read a function's disassembly from Ghidra: plain, its control flow, or a symbolic x87 trace.

The decompile of big state machines and x87 math is not reliable (lost jumps that merge states,
jump tables shown as one path, float sums printed with the wrong grouping). Check them here.

Usage: python asm.py dis  ADDR [FROM TO]   the instructions (CALL targets and float / double
                                          constants annotated)
       python asm.py flow ADDR [FROM TO]   only compares, jumps, calls, returns and stores of
                                          immediates (the state machine's skeleton)
       python asm.py x87  ADDR [FROM TO]   a symbolic x87 stack: every store / compare with the
                                          expression it uses, sums grouped as the code adds them
                                          (linear sweep: jumps are not followed)

Compare + jump pairs (FCOMP a, b; FNSTSW AX):  TEST AH,5 / JP  jumps when !(a < b);
TEST AH,5 / JNP when a < b;  AND EAX,0x4100 / JNZ when a <= b or unordered, JZ when a > b;
TEST AH,0x44 / JNP when a == b;  TEST AH,0x41 / JP when !(a <= b) (unordered included).
"""
import json
import os
import re
import struct
import sys
import urllib.request

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import ghidra_program  # noqa: E402,F401  (adds program= to plugin requests)

BASE = "http://127.0.0.1:8089"
RE = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def get(path):
    return json.loads(urllib.request.urlopen(BASE + path, timeout=120).read().decode())


_SYMS = None


def symbol(addr):
    """The callee's name from index.json (every function; rebuilt by build_index.py)."""
    global _SYMS
    if _SYMS is None:
        _SYMS = {}
        for f in ("symbols.json", "index.json"):
            try:
                _SYMS.update(json.load(open(os.path.join(RE, f))))
            except OSError:
                pass
    e = _SYMS.get(f"{addr:08x}")
    return e.get("name") if isinstance(e, dict) else None


_MEM = {}


def constant(addr, kind):
    key = (addr, kind)
    if key not in _MEM:
        n = 8 if kind == "double" else 4
        try:
            h = get(f"/read_memory?address=0x{addr:x}&length={n}")["hex"]
            raw = bytes.fromhex(h)
            _MEM[key] = struct.unpack("<d" if n == 8 else "<f", raw)[0]
        except Exception:  # noqa: BLE001
            _MEM[key] = None
    return _MEM[key]


def instructions(addr, lo=0, hi=0xFFFFFFFF):
    r = get(f"/disassemble_function?address=0x{addr:x}")
    out = []
    for i in r.get("instructions", []):
        a = int(i["address"], 16)
        if not lo <= a <= hi:
            continue
        text = i["instruction"]
        note = ""
        m = re.search(r"CALL 0x([0-9a-f]+)", text)
        if m:
            note = symbol(int(m.group(1), 16)) or ""
        m = re.search(r"(float|double) ptr \[0x([0-9a-f]+)\]", text)
        if m:
            v = constant(int(m.group(2), 16), m.group(1))
            if v is not None:
                note = f"[{v:g}]"
        out.append((a, text, note))
    return out


def show(ins, only_flow=False):
    for a, text, note in ins:
        op = text.split()[0]
        if only_flow and not (op in ("CMP", "TEST", "CALL", "RET", "FCOMP", "FCOM", "FCOMPP", "FNSTSW")
                              or op.startswith("J") or op in ("AND", "OR") and "EAX" in text
                              or re.match(r"MOV (dword|byte) ptr \[.*\],0x", text)):
            continue
        print(f"{a:08x}  {text}" + (f"  ; {note}" if note else ""))


def x87(ins):
    st = []

    def g(i):
        while len(st) <= i:
            st.append("?")
        return st[i]

    def put(i, v):
        g(i)
        st[i] = v

    def wrap(e):
        atom = re.fullmatch(r"[\w.-]+|d?\[[^\]]*\]|\w+\(.*\)", e) and not re.search(r"\) . \w", e)
        return e if atom else "(" + e + ")"

    def opnd(s, note):
        s = s.replace("float ptr ", "").replace("dword ptr ", "").replace("double ptr ", "d")
        if note.startswith("[") and re.search(r"\[0x[0-9a-f]+\]", s):
            return note[1:-1]
        return s

    def sti(s):
        m = re.match(r"ST(\d)?", s)
        return int(m.group(1) or 0)

    for a, text, note in ins:
        parts = text.split(None, 1)
        op = parts[0]
        args = [p.strip() for p in parts[1].split(",")] if len(parts) > 1 else []
        tag = f"{a:08x}"
        if op in ("FLD", "FILD"):
            st.insert(0, g(sti(args[0])) if args[0].startswith("ST") else ("int" if op == "FILD" else "") + opnd(args[0], note))
        elif op in ("FLDZ", "FLD1"):
            st.insert(0, "0" if op == "FLDZ" else "1")
        elif op in ("FIADD", "FISUB", "FIMUL", "FIDIV", "FISUBR", "FIDIVR"):
            sym = {"FIADD": "+", "FISUB": "-", "FIMUL": "*", "FIDIV": "/", "FISUBR": "-r", "FIDIVR": "/r"}[op]
            srcv = "int" + opnd(args[0], note)
            d = g(0)
            if sym in ("-r", "/r"):
                put(0, wrap(srcv) + " " + sym[0] + " " + wrap(d))
            else:
                put(0, wrap(d) + " " + sym + " " + wrap(srcv))
        elif op.rstrip("P") in ("FADD", "FSUB", "FMUL", "FDIV", "FSUBR", "FDIVR"):
            base = op[:-1] if op.endswith("P") else op
            sym = {"FADD": "+", "FSUB": "-", "FMUL": "*", "FDIV": "/", "FSUBR": "-r", "FDIVR": "/r"}[base]
            pop = op.endswith("P")
            src = None
            if not args:
                dst, src, pop = 1, 0, True
            elif len(args) == 1 and not args[0].startswith("ST"):
                dst, srcv = 0, opnd(args[0], note)
            elif len(args) == 1 and pop:
                dst, src = sti(args[0]), 0
            elif len(args) == 1:
                dst, src = 0, sti(args[0])
            else:
                dst, src = sti(args[0]), sti(args[1])
            if src is not None:
                srcv = g(src)
            d = g(dst)
            if sym == "-r":
                v = wrap(srcv) + " - " + wrap(d)
            elif sym == "/r":
                v = wrap(srcv) + " / " + wrap(d)
            else:
                v = wrap(d) + " " + sym + " " + wrap(srcv)
            put(dst, v)
            if pop:
                st.pop(0)
        elif op in ("FST", "FSTP", "FIST", "FISTP"):
            if args[0].startswith("ST"):
                put(sti(args[0]), g(0))
            else:
                print(tag, op, opnd(args[0], note), "=", g(0))
            if op.endswith("P"):
                st.pop(0)
        elif op.startswith("FCOM") or op.startswith("FUCOM"):
            other = g(1) if op.endswith("PP") or not args else opnd(args[0], note)
            print(tag, op, g(0), "<?>", other)
            for _ in range(2 if op.endswith("PP") else 1 if op.endswith("P") else 0):
                if st:
                    st.pop(0)
        elif op == "FXCH":
            i = sti(args[0]) if args else 1
            x, y = g(0), g(i)
            put(0, y)
            put(i, x)
        elif op in ("FCHS", "FABS", "FSQRT"):
            put(0, op[1:].lower() + "(" + g(0) + ")")
        elif op == "CALL":
            if "ftol" in note:
                print(tag, "ftol", g(0))
                if st:
                    st.pop(0)
            elif re.search(r"Rand|Square|Magnitude|Length|Dot", note):
                st.insert(0, note + "()")
        elif op.startswith("F") and op not in ("FNSTSW", "FWAIT", "FNCLEX", "FLDCW", "FNSTCW"):
            print(tag, "??", text)


def main():
    if len(sys.argv) < 3 or sys.argv[1] not in ("dis", "flow", "x87"):
        sys.exit(__doc__)
    addr = int(sys.argv[2], 16)
    lo = int(sys.argv[3], 16) if len(sys.argv) > 3 else 0
    hi = int(sys.argv[4], 16) if len(sys.argv) > 4 else 0xFFFFFFFF
    ins = instructions(addr, lo, hi)
    if sys.argv[1] == "x87":
        x87(ins)
    else:
        show(ins, sys.argv[1] == "flow")


if __name__ == "__main__":
    main()
