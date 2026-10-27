"""Disassemble one case of a jump-table switch.

  python casedis.py TABLE COUNT CASE [MAXBYTES]
  e.g. python casedis.py 41cbe0 119 14      (script VM case 14)

Prints the instructions from the case's target address up to the next case target
(in address order) or MAXBYTES (default 1200).
"""
import json
import struct
import sys
import urllib.request
import os as _os, sys as _sys
_sys.path.insert(0, _os.path.dirname(_os.path.abspath(__file__)))
import ghidra_program  # noqa: E402,F401  (adds program= to plugin requests)

BASE = "http://127.0.0.1:8089"


def mem(addr, n):
    d = json.load(urllib.request.urlopen(f"{BASE}/read_memory?address=0x{addr:x}&length={n}"))
    return bytes(d["data"])


def main():
    table, count, case = int(sys.argv[1], 16), int(sys.argv[2]), int(sys.argv[3], 0)
    maxbytes = int(sys.argv[4]) if len(sys.argv) > 4 else 1200
    targets = list(struct.unpack(f"<{count}I", mem(table, count * 4)))
    start = targets[case]
    later = sorted(t for t in set(targets) if t > start)
    end = min(later[0] if later else start + maxbytes, start + maxbytes)
    d = json.load(urllib.request.urlopen(f"{BASE}/disassemble_bytes?start_address=0x{start:x}&length={end - start}"))
    for i in d.get("instructions", []):
        print(i["address"], i["mnemonic"], i["operands"])


if __name__ == "__main__":
    main()
