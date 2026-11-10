"""Builds IJoy.dll (x86) with MSVC.

Usage: python build.py [dll] [compare] [--debug]   (default: dll)
Output: build/IJoy.dll (+ .lib, .pdb), build/compare.exe

Only MSVC: the interfaces are MSVC vtables (one virtual-destructor slot); a g++ build would put
two destructor slots there and not be a drop-in replacement.
"""
import os
import re
import shutil
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.abspath(__file__))
SOURCES = sorted(os.path.join(ROOT, "src", f) for f in os.listdir(os.path.join(ROOT, "src")) if f.endswith(".cpp"))
VSWHERE = r"C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe"
# Objects, PDB and import libraries go to %TEMP%: a cloud-synced project folder (Google Drive here)
# locks freshly written files while it uploads them, which breaks cl/link.
WORK = os.path.join(tempfile.gettempdir(), "ijoy_build")


def find_vcvars():
    if os.path.exists(VSWHERE):
        r = subprocess.run([VSWHERE, "-latest", "-products", "*", "-requires",
                            "Microsoft.VisualStudio.Component.VC.Tools.x86.x64", "-property",
                            "installationPath"], capture_output=True, text=True)
        for path in r.stdout.splitlines():
            bat = os.path.join(path.strip(), r"VC\Auxiliary\Build\vcvars32.bat")
            if path.strip() and os.path.exists(bat):
                return bat
    return None


def find_dlltool():
    for name in ("i686-w64-mingw32-dlltool", "dlltool"):
        path = shutil.which(name)
        if path:
            return path
    path = r"C:\nim-2.2.6\dist\mingw32\bin\dlltool.exe"
    return path if os.path.exists(path) else None


def dinput_import_lib():
    """DINPUT.dll's import library (DirectInputCreateEx): not in today's Windows SDK."""
    dlltool = find_dlltool()
    if not dlltool:
        print("dinput.lib: MinGW dlltool not found")
        return None
    os.makedirs(WORK, exist_ok=True)
    lib = os.path.join(WORK, "dinput.lib")
    r = subprocess.run([dlltool, "--kill-at", "-m", "i386", "-d", os.path.join(ROOT, "lib", "dinput.def"),
                        "-l", lib], capture_output=True, text=True)
    if r.returncode != 0:
        print((r.stdout + r.stderr).strip())
        return None
    return lib


def run_cl(vcvars, args):
    r = subprocess.run(f'"{vcvars}" >nul 2>nul && cl {args}', shell=True, capture_output=True, text=True)
    lines = [l for l in (r.stdout + r.stderr).splitlines()
             if l.strip() and not re.match(r"^\w+\.cpp$", l.strip()) and "Creating library" not in l
             and l.strip() != "Generating Code..."]
    if lines:
        print("\n".join(lines))
    return r.returncode == 0


def build_dll(vcvars, debug):
    dinput = dinput_import_lib()
    if not dinput:
        return False
    work = os.path.join(WORK, "dll")
    os.makedirs(work, exist_ok=True)
    dll = os.path.join(work, "IJoy.dll")
    flags = ["/nologo", "/LD", "/EHsc", "/std:c++17", "/W4", "/permissive-", "/MT" + ("d" if debug else ""),
             "/DIJOY_EXPORTS", "/Zi", "/Od" if debug else "/O2", "/DNDEBUG" if not debug else "/D_DEBUG",
             f'/I"{os.path.join(ROOT, "include")}"', f'/Fo"{work}\\\\"', f'/Fd"{work}\\\\"', f'/Fe"{dll}"']
    srcs = " ".join(f'"{s}"' for s in SOURCES)
    link = (f'/link /DEF:"{os.path.join(ROOT, "ijoy.def")}" /DEBUG /INCREMENTAL:NO '
            f'"{dinput}" dxguid.lib user32.lib')
    ok = run_cl(vcvars, f'{" ".join(flags)} {srcs} {link}')
    if ok:
        out = os.path.join(ROOT, "build")
        os.makedirs(out, exist_ok=True)
        for name in ("IJoy.dll", "IJoy.lib", "IJoy.pdb"):
            shutil.copyfile(os.path.join(work, name), os.path.join(out, name))
    print("dll:", "ok" if ok else "FAILED")
    return ok


def build_compare(vcvars):
    """tests/compare.cpp: the differential test against the original DLL."""
    work = os.path.join(WORK, "compare")
    os.makedirs(work, exist_ok=True)
    exe = os.path.join(work, "compare.exe")
    args = (f'/nologo /EHsc /std:c++17 /W4 /O2 /D_CRT_SECURE_NO_WARNINGS /I"{os.path.join(ROOT, "include")}" '
            f'"{os.path.join(ROOT, "tests", "compare.cpp")}" /Fo"{work}\\\\" /Fe"{exe}" /link dxguid.lib')
    ok = run_cl(vcvars, args)
    if ok:
        shutil.copyfile(exe, os.path.join(ROOT, "build", "compare.exe"))
    print("compare:", "ok" if ok else "FAILED")
    return ok


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    debug = "--debug" in sys.argv
    targets = args or ["dll"]
    vcvars = find_vcvars()
    if not vcvars:
        print("Visual Studio with the C++ x86 tools not found")
        sys.exit(1)
    ok = True
    if "dll" in targets:
        ok &= build_dll(vcvars, debug)
    if "compare" in targets:
        ok &= build_compare(vcvars)
    sys.exit(0 if ok else 1)


if __name__ == "__main__":
    main()
