"""Build the game for running: every .cpp of the hand port with x87 floats and symbols, linked to an executable with a PDB and a map.

Unlike build_check.py (default SSE2, only diagnostics) this is the build that is run. Objects and outputs are in %TEMP%\\hd2_game[_checked]
(G: is synced; see the Google Drive build locks). Only sources newer than their object, or all after a header change (--all), are compiled.

Usage: python build_game.py [--checked] [--all] [--link-only] [--copy DIR]
    --checked   /Od /MTd /RTC1: slower, finds more (a call with the wrong arguments shows as a stack check, memory is filled, the heap is checked).
    --copy DIR  copies the exe and the PDB to DIR when the link worked.
"""
import argparse
import glob
import os
import shutil
import subprocess
import sys
import tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from build_check import VCVARS, ROOT, SRC, collect

UP = os.path.dirname(ROOT)
LIBS = [os.path.join(UP, r"LS3DF\build\msvc\LS3DF.lib"), os.path.join(UP, r"RW_DATA\build\msvc\rw_data.lib"),
        "kernel32.lib", "advapi32.lib", "gdi32.lib", "imagehlp.lib", "mpr.lib", "ole32.lib", "oleaut32.lib", "user32.lib", "winmm.lib", "wsock32.lib", "shell32.lib"]
COMMON = ["/nologo", "/c", "/std:c++17", "/EHsc", "/W3", "/permissive-", "/Zc:__cplusplus", "/D_CRT_SECURE_NO_WARNINGS", "/DWIN32_LEAN_AND_MEAN", "/DNOMINMAX",
          "/arch:IA32", "/Z7", "/MP", '/I"%s"' % SRC]
RELEASE = ["/O2", "/Oy-", "/MT", "/GS-"]
CHECKED = ["/Od", "/Oy-", "/MTd", "/RTC1", "/D_ITERATOR_DEBUG_LEVEL=0"]        # (The checked iterators change sizes, which the layout assertions see.)
LINK = ["/nologo", "/SUBSYSTEM:WINDOWS", "/MACHINE:X86", "/DEBUG:FULL", "/INCREMENTAL:NO", "/DYNAMICBASE:NO", "/NXCOMPAT:NO", "/LARGEADDRESSAWARE:NO", "/SAFESEH:NO"]


def run_bat(path, lines):
    with open(path, "w") as fh:
        fh.write("@echo off\n")
        fh.write('call "%s" >nul\n' % VCVARS)
        fh.write("\n".join(lines) + "\n")
    return subprocess.run([path], capture_output=True, text=True, errors="replace")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--checked", action="store_true")
    ap.add_argument("--all", action="store_true")
    ap.add_argument("--link-only", action="store_true")
    ap.add_argument("--copy")
    ap.add_argument("--stack", default="0x100000")
    args = ap.parse_args()

    out = os.path.join(tempfile.gettempdir(), "hd2_game_checked" if args.checked else "hd2_game")
    os.makedirs(out, exist_ok=True)
    files = collect([])
    objname = lambda f: os.path.join(out, os.path.splitext(os.path.basename(f))[0] + ".obj")
    if not args.link_only:
        stale = [f for f in files if args.all or not os.path.exists(objname(f)) or os.path.getmtime(f) > os.path.getmtime(objname(f))]
        if stale:
            rsp = os.path.join(out, "files.rsp")
            with open(rsp, "w") as fh:
                fh.write("\n".join('"%s"' % f for f in stale))
            flags = COMMON + (CHECKED if args.checked else RELEASE)
            r = run_bat(os.path.join(out, "build.bat"), ['cl %s /Fo"%s\\\\" @"%s"' % (" ".join(flags), out, rsp)])
            lines = [l for l in (r.stdout + r.stderr).splitlines() if ": error" in l or "fatal error" in l or ": warning" in l]
            for l in lines[:60]:
                print(l.replace(ROOT + "\\", ""))
            print("%d compiled, %d messages" % (len(stale), len(lines)))
            if r.returncode:
                print("compile failed")
                return 1
    known = {objname(f) for f in files}
    for stale_obj in glob.glob(os.path.join(out, "*.obj")):
        if stale_obj not in known:
            os.remove(stale_obj)
    exe = os.path.join(out, "HD2_Port.exe")
    rsp = os.path.join(out, "link.rsp")
    with open(rsp, "w") as fh:
        fh.write("\n".join('"%s"' % o for o in sorted(known)) + "\n")
        fh.write("\n".join('"%s"' % l if "\\" in l else l for l in LIBS) + "\n")
    r = run_bat(os.path.join(out, "link.bat"), ['link %s /STACK:%s /OUT:"%s" /MAP:"%s" @"%s"' % (" ".join(LINK), args.stack, exe, exe[:-4] + ".map", rsp)])
    for l in (r.stdout + r.stderr).splitlines():
        if "LNK" in l and "LNK4099" not in l:
            print(l[:220])
    print("link return code", r.returncode, exe)
    if r.returncode:
        return 1
    if args.copy:
        shutil.copy2(exe, args.copy)
        shutil.copy2(exe[:-4] + ".pdb", args.copy)
        print("copied to", args.copy)
    return 0


if __name__ == "__main__":
    sys.exit(main())
