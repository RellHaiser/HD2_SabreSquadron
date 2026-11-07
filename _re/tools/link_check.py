"""Trial link of the hand port with MSVC: every object build_check.py left, against the engine's and the system's import libraries.

The linker's list of unresolved symbols is the list of what the hand port still needs. Tier 1 is not linked (it is another world: g++, its own types,
namespace tier1). Output: _re/link_unresolved.txt (symbol, undecorated name, which objects want it). No executable is kept.

Usage: python build_check.py && python link_check.py
"""
import collections
import glob
import os
import re
import subprocess
import sys
import tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from build_check import VCVARS, ROOT

OBJ = os.path.join(tempfile.gettempdir(), "hd2_build_check")
UP = os.path.dirname(ROOT)
LIBS = [os.path.join(UP, r"LS3DF\build\msvc\LS3DF.lib"), os.path.join(UP, r"RW_DATA\build\msvc\rw_data.lib"), os.path.join(UP, r"IJOY\build\IJoy.lib"),
        "kernel32.lib", "advapi32.lib", "gdi32.lib", "imagehlp.lib", "mpr.lib", "ole32.lib", "oleaut32.lib", "user32.lib", "winmm.lib", "wsock32.lib", "shell32.lib"]
OUT = os.path.join(ROOT, "_re", "link_unresolved.txt")

UNRESOLVED = re.compile(r"^(?P<obj>\S+?\.obj) : error LNK2019: unresolved external symbol (?P<rest>.*)$|^(?P<obj2>\S+?\.obj) : error LNK2001: unresolved external symbol (?P<rest2>.*)$")
DUPLICATE = re.compile(r"error LNK(2005|1169)")


def main():
    objs = sorted(glob.glob(os.path.join(OBJ, "*.obj")))
    rsp = os.path.join(OBJ, "link.rsp")
    with open(rsp, "w") as fh:
        fh.write("\n".join('"%s"' % o for o in objs) + "\n")
        fh.write("\n".join('"%s"' % l if "\\" in l else l for l in LIBS) + "\n")
    bat = os.path.join(OBJ, "link.bat")
    exe = os.path.join(OBJ, "trial.exe")
    with open(bat, "w") as fh:
        fh.write("@echo off\n")
        fh.write('call "%s" >nul\n' % VCVARS)
        fh.write('link /nologo /SUBSYSTEM:WINDOWS /MACHINE:X86 /OUT:"%s" @"%s"\n' % (exe, rsp))
    r = subprocess.run([bat], capture_output=True, text=True, errors="replace")
    want = collections.defaultdict(set)
    names = {}
    others = []
    for line in (r.stdout + r.stderr).splitlines():
        m = re.match(r"^(\S+?\.obj) : error LNK20(?:19|01): unresolved external symbol (.*)$", line)
        if m:
            rest = m.group(2)
            # '"name" (decorated) referenced in function ...' or 'decorated'
            d = re.match(r'"(.*?)" \((\S+)\)', rest)
            if d:
                names[d.group(2)] = d.group(1)
                want[d.group(2)].add(m.group(1)[:-4])
            else:
                sym = rest.split(" ")[0]
                names[sym] = sym
                want[sym].add(m.group(1)[:-4])
        elif "LNK" in line and "LNK4006" not in line and "LNK1120" not in line and "LNK4088" not in line:
            others.append(line)
    with open(OUT, "w", newline="\n") as fh:
        for sym in sorted(want, key=lambda s: names[s]):
            fh.write("%s\t%s\t%s\n" % (names[sym], sym, " ".join(sorted(want[sym]))))
    print("%d objects; %d unresolved symbols; output %s" % (len(objs), len(want), os.path.relpath(OUT, ROOT)))
    for line in others[:40]:
        print(line[:200])
    print("%d other linker messages" % len(others))
    print("return code", r.returncode, "exe" if os.path.exists(exe) else "no exe")


if __name__ == "__main__":
    main()
