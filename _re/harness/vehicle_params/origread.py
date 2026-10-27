"""Simulate the original reader 0x004d7d80 exactly (its operations in execution order from
map_read.txt, lstrcpyA copying up to the NUL with no bound) on the shipped blobs, and compare the
struct with two port variants: names copied as 16 raw bytes, or cut to 15 + NUL."""
import os
import re

D = os.path.dirname(os.path.abspath(__file__))
ops = []
for ln in open(D + r"\map_read.txt"):
    m = re.match(r"copy struct\+0x([0-9a-f]+) <- blob\+0x([0-9a-f]+) size (\d+)", ln)
    s = re.match(r"str struct\+0x([0-9a-f]+) <- blob\+0x([0-9a-f]+)", ln)
    if m:
        ops.append(("copy", int(m.group(1), 16), int(m.group(2), 16), int(m.group(3))))
    elif s:
        ops.append(("str", int(s.group(1), 16), int(s.group(2), 16), 0))


def original(blob):
    st = bytearray(0x1240 + 0x400)
    for kind, so, bo, n in ops:
        if kind == "copy":
            st[so:so + n] = blob[bo:bo + n]
        else:
            i = 0
            while True:
                c = blob[bo + i]
                st[so + i] = c
                i += 1
                if c == 0:
                    break
    return st[:0x1240], st[0x1240:]


def port(blob, raw16):
    st = bytearray(0x1240)
    for kind, so, bo, n in ops:
        if kind == "copy":
            st[so:so + n] = blob[bo:bo + n]
        elif raw16:
            # copy up to the NUL within the slot, all 16 bytes when there is none
            src = blob[bo:bo + 16]
            k = src.find(0)
            k = 16 if k < 0 else k + 1
            st[so:so + k] = src[:k]
        else:
            src = blob[bo:bo + 15]
            k = src.find(0)
            k = 15 if k < 0 else k
            st[so:so + k] = src[:k]
            st[so + k] = 0
    return st


def cstr(st, off):
    e = st.find(0, off)
    return bytes(st[off:e])


names = [so for kind, so, bo, n in ops if kind == "str"]
diff_raw = diff_cut = spill = 0
name_diff_raw = name_diff_cut = 0
for f in sorted(os.listdir(D + r"\cars")):
    blob = open(D + r"\cars\\" + f, "rb").read()
    o, tail = original(blob)
    if any(tail):
        spill += 1
    a, b = port(blob, True), port(blob, False)
    # compare all bytes except name tails after the NUL (garbage either way)
    def same(x):
        return all(x[i] == o[i] for i in range(0x1240) if not any(n <= i < n + 16 for n in names))
    diff_raw += not same(a)
    diff_cut += not same(b)
    # names as the game reads them (C strings in struct memory)
    name_diff_raw += any(cstr(a, n) != cstr(o, n) for n in names)
    name_diff_cut += any(cstr(b, n) != cstr(o, n) for n in names)
print("cars whose original read spills past the struct:", spill)
print("non-name bytes differ: raw16 %d, cut15 %d" % (diff_raw, diff_cut))
print("names (as C strings) differ: raw16 %d, cut15 %d" % (name_diff_raw, name_diff_cut))

# The simulated original structs for the C++ harness (orig/<car>.struct).
os.makedirs(D + r"\orig", exist_ok=True)
for f in sorted(os.listdir(D + r"\cars")):
    blob = open(D + r"\cars\\" + f, "rb").read()
    open(D + r"\orig\\" + f.replace(".bin", ".struct"), "wb").write(bytes(original(blob)[0]))
print("wrote", len(os.listdir(D + r"\orig")), "original structs")
