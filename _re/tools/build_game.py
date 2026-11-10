"""Build the game for running: every .cpp of the hand port with x87 floats and symbols, linked to an executable with a PDB and a map.

Unlike build_check.py (default SSE2, only diagnostics) this is the build that is run. Objects and outputs are in %TEMP%\\hd2_game[_checked]
(G: is synced; see the Google Drive build locks). Only sources newer than their object, or all after a header change (--all), are compiled.

Usage: python build_game.py [--checked] [--coverage] [--all] [--link-only] [--no-laa] [--copy DIR]
    --no-laa    links without large address awareness (2 GB, as the original).
    --checked   /Od /MTd /RTC1: slower, finds more (a call with the wrong arguments shows as a stack check, memory is filled, the heap is checked).
    --coverage  function-entry coverage: every source with /Gh, linked with _re/coverage/penter.cpp (built without it), which
                records each function entered in HD2_Port.cov beside the exe (_re/tools/client_coverage.py reads it). Its own
                folder (%TEMP%\\hd2_game_cov[_checked]); the code is the same build's but for the _penter call in each prologue.
    --copy DIR  copies the exe and the PDB to DIR when the link worked.
"""
import argparse
import glob
import os
import shutil
import struct
import subprocess
import sys
import tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from build_check import VCVARS, ROOT, SRC, collect

UP = os.path.dirname(ROOT)


def dll_lib(subtree, sibling, name):
    """The import library of a DLL built in this repository (ls3df/build.py, build_rw_data.py), else in a sibling clone of its repository."""
    for base in (os.path.join(ROOT, subtree), os.path.join(UP, sibling)):
        lib = os.path.join(base, "build", "msvc", name)
        if os.path.exists(lib):
            return lib
    return os.path.join(ROOT, subtree, "build", "msvc", name)


LIBS = [dll_lib("ls3df", "LS3DF", "LS3DF.lib"), dll_lib("rw_data", "RW_DATA", "rw_data.lib"),
        "kernel32.lib", "advapi32.lib", "gdi32.lib", "imagehlp.lib", "mpr.lib", "ole32.lib", "oleaut32.lib", "user32.lib", "winmm.lib", "wsock32.lib", "shell32.lib"]
COMMON = ["/nologo", "/c", "/std:c++17", "/EHsc", "/W3", "/permissive-", "/Zc:__cplusplus", "/D_CRT_SECURE_NO_WARNINGS", "/DWIN32_LEAN_AND_MEAN", "/DNOMINMAX",
          "/arch:IA32", "/Z7", "/MP", '/I"%s"' % SRC]
RELEASE = ["/O2", "/Oy-", "/MT", "/GS-"]
CHECKED = ["/Od", "/Oy-", "/MTd", "/RTC1", "/D_ITERATOR_DEBUG_LEVEL=0"]        # (The checked iterators change sizes, which the layout assertions see.)
LINK = ["/nologo", "/SUBSYSTEM:WINDOWS,5.01", "/MACHINE:X86", "/DEBUG:FULL", "/INCREMENTAL:NO", "/DYNAMICBASE:NO", "/NXCOMPAT:NO", "/SAFESEH:NO"]
# Large address aware (up to 4 GB on 64-bit Windows; the original is not): the same flag as the community LAA patchers set on the exe.
# --no-laa links without it, the original's 2 GB, to tell a crash from it apart.
LAA = "/LARGEADDRESSAWARE"
NO_LAA = "/LARGEADDRESSAWARE:NO"


def is_large_address_aware(exe):
    """IMAGE_FILE_LARGE_ADDRESS_AWARE (0x20) in the PE file header's Characteristics."""
    with open(exe, "rb") as fh:
        data = fh.read(4096)
    pe = struct.unpack_from("<I", data, 0x3C)[0]
    return bool(struct.unpack_from("<H", data, pe + 4 + 18)[0] & 0x20)


# The exe's title (string 3, SetWindowTitle) and icon. The icon is the game's: taken from the installed copy at build time, never kept in the repository.
ICON_SOURCE = r"H:\HD2_Unmodified\HD2_SabreSquadron.exe"
TITLE = "HIDDEN & DANGEROUS II"


def extract_icon(exe, out):
    """Icon 1 of the (packed) original, whose .rsrc still holds the icon and its group, as an .ico. False if there is none to read."""
    if not os.path.exists(exe):
        return False
    d = open(exe, "rb").read()
    pe = struct.unpack_from("<I", d, 0x3c)[0]
    count = struct.unpack_from("<H", d, pe + 6)[0]
    size = struct.unpack_from("<H", d, pe + 20)[0]
    va = raw = None
    for i in range(count):
        sec = pe + 24 + size + 40 * i
        if d[sec:sec + 8].rstrip(b"\0") == b".rsrc":
            va, _, raw = struct.unpack_from("<III", d, sec + 12)
    if va is None:
        return False

    def entries(off):
        named, ids = struct.unpack_from("<HH", d, raw + off + 12)
        return [struct.unpack_from("<II", d, raw + off + 16 + 8 * i) for i in range(named + ids)]

    def leaf(kind):
        for typ, t in entries(0):
            if typ == kind:
                for _, b in entries(t & 0x7fffffff):
                    for _, l in entries(b & 0x7fffffff):
                        rva, n = struct.unpack_from("<II", d, raw + l)
                        return d[raw + rva - va:raw + rva - va + n]
        return None

    image, group = leaf(3), leaf(14)
    if not image or not group or len(group) != 20:
        return False
    # (One image: the group's 14-byte entry ends with its id; the file's ends with its offset.)
    with open(out, "wb") as fh:
        fh.write(group[:6] + group[6:18] + struct.pack("<I", 22) + image)
    return True


def build_resources(out):
    """The .res, or None without the game's icon (the exe then has no title or icon, and still works)."""
    ico = os.path.join(out, "hd2.ico")
    if not extract_icon(ICON_SOURCE, ico):
        print("no icon from", ICON_SOURCE, ": the exe has no resources")
        return None
    rc = os.path.join(out, "hd2.rc")
    with open(rc, "w") as fh:
        fh.write('1 ICON "%s"\nSTRINGTABLE\nBEGIN\n    3 "%s"\nEND\n' % (ico.replace("\\", "\\\\"), TITLE))
    res = os.path.join(out, "hd2.res")
    r = run_bat(os.path.join(out, "rc.bat"), ['rc /nologo /fo "%s" "%s"' % (res, rc)])
    if r.returncode:
        print("rc failed:", (r.stdout + r.stderr)[-300:])
        return None
    return res


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
    ap.add_argument("--no-laa", action="store_true")
    ap.add_argument("--coverage", action="store_true")
    args = ap.parse_args()

    name = "hd2_game" + ("_cov" if args.coverage else "") + ("_checked" if args.checked else "")
    out = os.path.join(tempfile.gettempdir(), name)
    os.makedirs(out, exist_ok=True)
    files = collect([])
    objname = lambda f: os.path.join(out, os.path.splitext(os.path.basename(f))[0] + ".obj")
    penter = os.path.join(ROOT, "_re", "coverage", "penter.cpp")
    if args.coverage and not args.link_only:
        flags = COMMON + (CHECKED if args.checked else RELEASE)
        r = run_bat(os.path.join(out, "penter.bat"), ['cl %s /Fo"%s" "%s"' % (" ".join(flags), objname(penter), penter)])
        if r.returncode:
            print((r.stdout + r.stderr)[-3000:])
            print("compile failed (penter.cpp)")
            return 1
    if not args.link_only:
        stale = [f for f in files if args.all or not os.path.exists(objname(f)) or os.path.getmtime(f) > os.path.getmtime(objname(f))]
        if stale:
            rsp = os.path.join(out, "files.rsp")
            with open(rsp, "w") as fh:
                fh.write("\n".join('"%s"' % f for f in stale))
            flags = COMMON + (CHECKED if args.checked else RELEASE) + (["/Gh"] if args.coverage else [])
            r = run_bat(os.path.join(out, "build.bat"), ['cl %s /Fo"%s\\\\" @"%s"' % (" ".join(flags), out, rsp)])
            lines = [l for l in (r.stdout + r.stderr).splitlines() if ": error" in l or "fatal error" in l or ": warning" in l]
            for l in lines[:60]:
                print(l.replace(ROOT + "\\", ""))
            print("%d compiled, %d messages" % (len(stale), len(lines)))
            if r.returncode:
                print("compile failed")
                return 1
    known = {objname(f) for f in files + ([penter] if args.coverage else [])}
    for stale_obj in glob.glob(os.path.join(out, "*.obj")):
        if stale_obj not in known:
            os.remove(stale_obj)
    exe = os.path.join(out, "HD2_Port.exe")
    rsp = os.path.join(out, "link.rsp")
    res = build_resources(out)
    with open(rsp, "w") as fh:
        fh.write("\n".join('"%s"' % o for o in sorted(known)) + "\n")
        if res:
            fh.write('"%s"\n' % res)
        fh.write("\n".join('"%s"' % l if "\\" in l else l for l in LIBS) + "\n")
    r = run_bat(os.path.join(out, "link.bat"), ['link %s %s /STACK:%s /OUT:"%s" /MAP:"%s" @"%s"' % (" ".join(LINK), NO_LAA if args.no_laa else LAA, args.stack, exe, exe[:-4] + ".map", rsp)])
    for l in (r.stdout + r.stderr).splitlines():
        if "LNK" in l and "LNK4099" not in l:
            print(l[:220])
    print("link return code", r.returncode, exe)
    if r.returncode:
        return 1
    print("large address aware:", "yes" if is_large_address_aware(exe) else "no")
    if args.copy:
        shutil.copy2(exe, args.copy)
        shutil.copy2(exe[:-4] + ".pdb", args.copy)
        print("copied to", args.copy)
    return 0


if __name__ == "__main__":
    sys.exit(main())
