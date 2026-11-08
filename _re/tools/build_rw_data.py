"""Build rw_data.dll and its import library from the rw_data/ subtree (RellHaiser/rw_data has no build script).

Output: rw_data/build/msvc/rw_data.dll, .lib, .pdb (ignored by git); objects in %TEMP%\\rw_data_build. The flags are the game's
release ones (/O2 /MT, x87 floats); rw_data.def pins the original's 26 exports and ordinals. The subtree itself is not edited.

Usage: python build_rw_data.py
"""
import glob
import os
import subprocess
import sys
import tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from build_check import VCVARS, ROOT

RW = os.path.join(ROOT, "rw_data")
OUT = os.path.join(RW, "build", "msvc")
OBJ = os.path.join(tempfile.gettempdir(), "rw_data_build")
CL = ["/nologo", "/c", "/std:c++17", "/EHsc", "/W3", "/O2", "/MT", "/arch:IA32", "/Z7", "/MP", "/DRW_DATA_BUILD", "/D_CRT_SECURE_NO_WARNINGS",
      '/I"%s"' % os.path.join(RW, "include"), '/Fo"%s\\\\"' % OBJ]
LINK = ["/nologo", "/DLL", "/MACHINE:X86", "/DEBUG", '/DEF:"%s"' % os.path.join(RW, "rw_data.def")]
LIBS = ["kernel32.lib", "user32.lib", "advapi32.lib"]


def main():
    os.makedirs(OUT, exist_ok=True)
    os.makedirs(OBJ, exist_ok=True)
    srcs = sorted(glob.glob(os.path.join(RW, "src", "*.cpp")))
    for o in glob.glob(os.path.join(OBJ, "*.obj")):
        os.remove(o)
    dll = os.path.join(OUT, "rw_data.dll")
    bat = os.path.join(OBJ, "build.bat")
    with open(bat, "w") as fh:
        fh.write("@echo off\n")
        fh.write('call "%s" >nul\n' % VCVARS)
        fh.write("cl %s %s || exit /b 1\n" % (" ".join(CL), " ".join('"%s"' % s for s in srcs)))
        fh.write('link %s /OUT:"%s" "%s\\*.obj" %s\n' % (" ".join(LINK), dll, OBJ, " ".join(LIBS)))
    r = subprocess.run([bat], capture_output=True, text=True, errors="replace")
    for l in (r.stdout + r.stderr).splitlines():
        s = l.strip()
        # (dta_internal.h defines the macros the command line also sets: C4005 and its note are dropped; vcvars' vswhere complaint too.)
        if s and not s.endswith(".cpp") and "C4005" not in s and "previously declared" not in s and "vswhere" not in s and "operable program" not in s \
                and "Creating library" not in s and s != "Generating Code...":
            print(s[:220])
    print("rw_data:", "ok ->" if r.returncode == 0 else "failed", dll)
    return r.returncode


if __name__ == "__main__":
    sys.exit(main())
