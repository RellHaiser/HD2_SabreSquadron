"""Annotates a function's disassembly with stack slots (for AND ESP,-16 frames, whose decompile is garbled).

    python _re/tools/slots.py 0x00628f80 [--strings]

Each line of `asm.py dis` gets the depth below the frame's base (the ESP after the frame's SUB ESP) and every
[ESP+n] its slot: sK = n - depth (the same K names the same local all through the function). PUSH/POP and
ADD/SUB ESP move the depth; a direct CALL pops its callee's RET n (read from the callee's own disassembly,
cached); an indirect CALL (COM / virtual: stdcall) pops what was pushed since the last call. --strings prints
the C strings of PUSHed addresses in 0x0080_0000 .. 0x0090_0000 (Ghidra's HTTP read_memory).
After an unconditional jump or a return, the next line takes the depth recorded by the first jump to it.
Recount by hand where it matters.
"""
import json
import os
import re
import subprocess
import sys
import urllib.request

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
ASM = os.path.join(ROOT, "_re", "tools", "asm.py")
_ret_cache = {}


def dis(address):
    out = subprocess.run([sys.executable, ASM, "dis", address], capture_output=True, text=True).stdout
    return [l for l in out.splitlines() if re.match(r"^[0-9a-f]{8}\s", l)]


def ret_size(target):
    if target not in _ret_cache:
        size = 0
        for l in dis(target):
            m = re.search(r"\bRET(?:\s+0x([0-9a-f]+))?\s*$", l)
            if m:
                size = int(m.group(1), 16) if m.group(1) else 0
                break
        _ret_cache[target] = size
    return _ret_cache[target]


def c_string(address):
    try:
        url = "http://127.0.0.1:8089/read_memory?address=0x%x&length=64&program=HD2_SabreSquadron_unpacked.exe" % address
        raw = urllib.request.urlopen(url, timeout=5).read().decode()
        try:
            data = bytes.fromhex(json.loads(raw)["hex"])
        except Exception:
            data = bytes.fromhex(raw.strip())
        text = data.split(b"\0", 1)[0]
        if text and all(32 <= c < 127 or c in (9, 10) for c in text):
            return text.decode()
    except Exception:
        pass
    return None


def main():
    address = sys.argv[1]
    strings = "--strings" in sys.argv
    lines = dis(address)
    depth = None          # below the base; None until the frame is set up
    since_call = 0        # bytes pushed since the last call
    seen_sub = False
    at_target = {}        # a jump's target: the depth at the jump
    after_jump = False    # the line before was an unconditional jump / a return
    for l in lines:
        addr, ins = l[:8], l[8:].strip()
        note = ""
        if depth is not None and after_jump and int(addr, 16) in at_target:
            depth = at_target[int(addr, 16)]
        after_jump = False
        if depth is None:
            if re.match(r"SUB ESP,0x[0-9a-f]+", ins):
                depth = 0
                seen_sub = True
                print("%s  %-48s ; base" % (addr, ins))
                continue
            print("%s  %s" % (addr, ins))
            continue
        for m in re.finditer(r"\[ESP \+ 0x([0-9a-f]+)\]|\[ESP\]", ins):
            n = int(m.group(1), 16) if m.group(1) else 0
            note += " s%d" % (n - depth)
        if strings:
            m = re.match(r"PUSH 0x([0-9a-f]+)$", ins)
            if m and 0x800000 <= int(m.group(1), 16) < 0x900000:
                t = c_string(int(m.group(1), 16))
                if t is not None:
                    note += ' "%s"' % t
        print("%s  [%4d] %-48s ;%s" % (addr, depth, ins, note))
        m = re.match(r"J[A-Z]+ 0x([0-9a-f]+)", ins)
        if m:
            at_target.setdefault(int(m.group(1), 16), depth)
            after_jump = ins.startswith("JMP ")
        elif ins.startswith("RET"):
            after_jump = True
        if ins.startswith("PUSH "):
            depth += 4
            since_call += 4
        elif ins.startswith("POP "):
            depth -= 4
        elif re.match(r"SUB ESP,0x[0-9a-f]+", ins):
            depth += int(ins.split(",")[1], 16)
        elif re.match(r"ADD ESP,0x[0-9a-f]+", ins):
            depth -= int(ins.split(",")[1], 16)
        elif ins.startswith("CALL "):
            m = re.match(r"CALL 0x([0-9a-f]+)", ins)
            if m:
                depth -= ret_size("0x" + m.group(1))
            else:
                depth -= since_call
            since_call = 0
    if not seen_sub:
        print("(no SUB ESP frame found)")


if __name__ == "__main__":
    main()
