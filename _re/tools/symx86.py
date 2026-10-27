"""Tiny symbolic x86 interpreter for copy-only cdecl functions (written for the vehicle parameter
blob reader 0x004d7d80 / writer 0x004d6c50). Runs an asm.py listing with pointers tracked as
region + offset and prints every store of loaded data (dst region+off <- src region+off, size) and
every lstrcpyA(dst, src), in execution order. Handles MOV/LEA/ADD/SUB/DEC/INC/TEST/CMP/PUSH/POP,
the conditional jumps and calls to lstrcpyA through an import slot; anything else stops it.

Usage: python tools/asm.py dis 0x4d7d80 > reader.asm
       python tools/symx86.py reader.asm blob struct 0080f1bc=lstrcpyA
(the two names are the regions of the first two stack arguments; the third argument is 0;
ADDR=name maps an import slot). Used by _re/harness/vehicle_params."""
import re
import sys

LINE = re.compile(r"^([0-9a-f]{8})\s+(\S+)\s*(.*)$")


class Ptr:
    def __init__(self, region, off):
        self.region, self.off = region, off

    def __repr__(self):
        return "%s+0x%x" % (self.region, self.off)


class Neg:
    """c - (base of region): what `const - ptr` gives; adding a pointer of the region yields an int."""
    def __init__(self, region, c):
        self.region, self.c = region, c


class Tag:
    def __init__(self, region, off, size):
        self.region, self.off, self.size = region, off, size


class Sym:
    def __init__(self, name):
        self.name = name


REG32 = ["EAX", "ECX", "EDX", "EBX", "ESP", "EBP", "ESI", "EDI"]
REG8 = {"AL": "EAX", "CL": "ECX", "DL": "EDX", "BL": "EBX"}
IMPORTS = {}


def run(path, arg1, arg2, param3=0, imports=None):
    code = []
    for ln in open(path):
        m = LINE.match(ln.strip())
        if m:
            code.append((int(m.group(1), 16), m.group(2), m.group(3).split("  ;")[0].strip()))
    at = {a: i for i, (a, _, _) in enumerate(code)}
    R = {r: 0 for r in REG32}
    R8 = {}
    R["ESP"] = Ptr("stack", 0x1000)
    stack = {0x1000: "ret", 0x1004: Ptr(arg1, 0), 0x1008: Ptr(arg2, 0), 0x100C: param3}
    out = []
    flags = {"Z": False, "S": False, "C": False}

    def sgn(x):
        return x - 0x100000000 if x & 0x80000000 else x

    def add(a, b):
        if isinstance(a, Ptr) and isinstance(b, int):
            return Ptr(a.region, a.off + sgn(b))
        if isinstance(b, Ptr) and isinstance(a, int):
            return Ptr(b.region, b.off + sgn(a))
        if isinstance(a, int) and isinstance(b, int):
            return (a + b) & 0xFFFFFFFF
        if isinstance(a, Neg) and isinstance(b, Ptr) and a.region == b.region:
            return (a.c + b.off) & 0xFFFFFFFF
        if isinstance(b, Neg) and isinstance(a, Ptr) and a.region == b.region:
            return (b.c + a.off) & 0xFFFFFFFF
        if isinstance(a, Neg) and isinstance(b, int):
            return Neg(a.region, a.c + b)
        raise ValueError("add %r %r" % (a, b))

    def sub(a, b):
        if isinstance(a, Ptr) and isinstance(b, int):
            return Ptr(a.region, a.off - sgn(b))
        if isinstance(a, Ptr) and isinstance(b, Ptr) and a.region == b.region:
            return (a.off - b.off) & 0xFFFFFFFF
        if isinstance(a, int) and isinstance(b, int):
            return (a - b) & 0xFFFFFFFF
        if isinstance(a, int) and isinstance(b, Ptr):
            sa = a - 0x100000000 if a & 0x80000000 else a
            return Neg(b.region, sa - b.off)
        raise ValueError("sub %r %r" % (a, b))

    def setz(v):
        if isinstance(v, (Ptr, Neg)):
            flags["Z"] = False
            flags["S"] = False
        else:
            flags["Z"] = (v & 0xFFFFFFFF) == 0
            flags["S"] = bool(v & 0x80000000)

    def val(op):
        op = op.strip()
        if op in R:
            return R[op]
        if op in REG8:
            return R8.get(op, 0)
        if re.fullmatch(r"-?0x[0-9a-f]+|-?\d+", op):
            return int(op, 0) & 0xFFFFFFFF
        m = re.fullmatch(r"(dword|byte|word) ptr (\[.*\])|(\[.*\])", op)
        if m:
            size = {"dword": 4, "byte": 1, "word": 2}.get(m.group(1), 4)
            return load(addr(m.group(2) or m.group(3)), size)
        raise ValueError("operand " + op)

    def addr(br):
        inner = br.strip()[1:-1]
        terms = [t.strip() for t in inner.replace(" - ", " + -").split(" + ")]
        a = 0
        for t in terms:
            m = re.fullmatch(r"(\w+)\*0x([0-9a-f]+)", t)
            if m:
                rv, sc = R[m.group(1)], int(m.group(2), 16)
                a = add(a, rv if sc == 1 else (rv * sc) & 0xFFFFFFFF)
            elif t in R:
                a = add(a, R[t])
            else:
                a = add(a, int(t, 0) & 0xFFFFFFFF)
        return a

    def load(a, size):
        if isinstance(a, Ptr):
            if a.region == "stack":
                return stack.get(a.off, 0)
            return Tag(a.region, a.off, size)
        if isinstance(a, int):
            if imports and a in imports:
                return Sym(imports[a])
            return Tag("abs", a, size)
        raise ValueError("load %r" % (a,))

    def store(a, v, size):
        if isinstance(a, Ptr) and a.region == "stack":
            stack[a.off] = v
            return
        if isinstance(v, Tag):
            out.append(("copy", a, v, size))
        else:
            out.append(("const", a, v, size))

    def put(dst, v):
        dst = dst.strip()
        if dst in R:
            R[dst] = v
        elif dst in REG8:
            R8[dst] = v
        else:
            m = re.fullmatch(r"(dword|byte|word) ptr (\[.*\])|(\[.*\])", dst)
            size = {"dword": 4, "byte": 1, "word": 2}.get(m.group(1), 4)
            store(addr(m.group(2) or m.group(3)), v, size)

    i, steps = 0, 0
    while True:
        steps += 1
        if steps > 200000:
            raise RuntimeError("runaway")
        a, op, ops = code[i]
        args = [x.strip() for x in re.split(r",(?![^\[]*\])", ops)] if ops else []
        nxt = i + 1
        if op == "MOV":
            put(args[0], val(args[1]))
        elif op == "LEA":
            put(args[0], addr(args[1]))
        elif op in ("ADD", "SUB"):
            r = (add if op == "ADD" else sub)(val(args[0]), val(args[1]))
            put(args[0], r)
            setz(r)
        elif op == "DEC":
            r = sub(val(args[0]), 1)
            put(args[0], r)
            setz(r)
        elif op == "INC":
            r = add(val(args[0]), 1)
            put(args[0], r)
            setz(r)
        elif op == "XOR" and args[0] == args[1]:
            put(args[0], 0)
            setz(0)
        elif op == "TEST":
            x, y = val(args[0]), val(args[1])
            if isinstance(x, Ptr):
                setz(x)
            else:
                setz(x & y if isinstance(y, int) else x)
        elif op == "CMP":
            x, y = val(args[0]), val(args[1])
            r = sub(x, y)
            setz(r)
            flags["C"] = isinstance(x, int) and isinstance(y, int) and x < y
        elif op == "PUSH":
            R["ESP"] = sub(R["ESP"], 4)
            stack[R["ESP"].off] = val(args[0])
        elif op == "POP":
            put(args[0], stack.get(R["ESP"].off, 0))
            R["ESP"] = add(R["ESP"], 4)
        elif op == "CALL":
            f = val(args[0])
            name = f.name if isinstance(f, Sym) else str(f)
            sp = R["ESP"].off
            if name == "lstrcpyA":
                out.append(("str", stack[sp], stack[sp + 4], 0))
                R["ESP"] = add(R["ESP"], 8)
                R["EAX"] = stack[sp]
            else:
                raise ValueError("call " + name)
        elif op in ("JMP", "JNZ", "JZ", "JL", "JNC", "JC", "JGE", "JLE", "JG"):
            take = {"JMP": True, "JNZ": not flags["Z"], "JZ": flags["Z"], "JL": flags["S"],
                    "JNC": not flags["C"], "JC": flags["C"]}.get(op)
            if take is None:
                raise ValueError(op)
            if take:
                nxt = at[int(args[0], 16)]
        elif op == "RET":
            break
        elif op in ("IMUL", "SAR", "SHR"):
            raise ValueError("arith %s at %x (tyre path?)" % (op, a))
        else:
            raise ValueError("op %s at %x" % (op, a))
        i = nxt
    return out


if __name__ == "__main__":
    imp = {}
    for kv in sys.argv[4:]:
        k, v = kv.split("=")
        imp[int(k, 16)] = v
    res = run(sys.argv[1], sys.argv[2], sys.argv[3], 0, imp)
    for kind, dst, src, size in res:
        if kind == "copy":
            print("%s %s <- %s+0x%x size %d" % (kind, dst, src.region, src.off, size))
        elif kind == "str":
            print("str %s <- %s" % (dst, src))
        else:
            print("const %s <- %r size %d" % (dst, src, size))
