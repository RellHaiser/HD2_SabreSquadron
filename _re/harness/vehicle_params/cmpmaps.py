"""Compare the reader / writer maps and print the blob layout as runs."""
import os
import re

D = os.path.dirname(os.path.abspath(__file__))


def parse(path, reader):
    pairs = set()
    for ln in open(path):
        m = re.match(r"copy (\w+)\+0x([0-9a-f]+) <- (\w+)\+0x([0-9a-f]+) size (\d+)", ln)
        s = re.match(r"str (\w+)\+0x([0-9a-f]+) <- (\w+)\+0x([0-9a-f]+)", ln)
        if m:
            d, do, s_, so, n = m.group(1), int(m.group(2), 16), m.group(3), int(m.group(4), 16), int(m.group(5))
            kind = n
        elif s:
            d, do, s_, so = s.group(1), int(s.group(2), 16), s.group(3), int(s.group(4), 16)
            kind = "str"
        else:
            continue
        blob, struct = (so, do) if reader else (do, so)
        pairs.add((blob, struct, kind))
    return pairs


r = parse(D + r"\map_read.txt", True)
w = parse(D + r"\map_write.txt", False)
print("read", len(r), "write", len(w), "same:", r == w)
print("only read:", sorted(r - w)[:10])
print("only write:", sorted(w - r)[:10])
pairs = sorted(r)
# blob coverage (strings: 16 bytes)
cov = bytearray(0x20000)
for b, s, k in pairs:
    n = 16 if k == "str" else k
    for i in range(n):
        cov[b + i] += 1
end = max(b + (16 if k == "str" else k) for b, s, k in pairs)
print("blob end 0x%x" % end)
gaps, i = [], 0
while i < end:
    if not cov[i]:
        j = i
        while j < end and not cov[j]:
            j += 1
        gaps.append((i, j - i))
        i = j
    else:
        i += 1
print("blob gaps:", [("0x%x" % a, n) for a, n in gaps])
print("overlaps:", [hex(i) for i in range(end) if cov[i] > 1][:20])
open(D + r"\params_map.txt", "w").write(
    "\n".join("%04x %04x %s" % (b, s, k) for b, s, k in sorted(pairs)))
scov = bytearray(0x20000)
for b, s, k in pairs:
    for i in range(16 if k == "str" else k):
        scov[s + i] += 1
sg, i = [], 0
while i < 0x1240:
    if not scov[i]:
        j = i
        while j < 0x1240 and not scov[j]:
            j += 1
        sg.append((i, j - i))
        i = j
    else:
        i += 1
print("struct gaps:", [("0x%x" % a, n) for a, n in sg])
