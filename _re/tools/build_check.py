"""Compile-check the port with MSVC (x86, C++17): every .cpp under src/ -> .obj.

Usage: python build_check.py [file-or-dir ...]   (default: all of src/)
Objects go to the session scratch dir; only diagnostics are printed.
Without MSVC (no vcvars32.bat) the files are syntax-checked with a 32-bit g++ instead.
"""
import os
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
SRC = os.path.join(ROOT, "src")
VSWHERE = r"C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe"


def find_vcvars():
    """vcvars32.bat of the newest Visual Studio with the C++ tools (VS 2026 lives on T:)."""
    if os.path.exists(VSWHERE):
        r = subprocess.run([VSWHERE, "-latest", "-products", "*", "-requires",
                            "Microsoft.VisualStudio.Component.VC.Tools.x86.x64", "-property",
                            "installationPath"], capture_output=True, text=True)
        for path in r.stdout.splitlines():
            bat = os.path.join(path.strip(), r"VC\Auxiliary\Build\vcvars32.bat")
            if path.strip() and os.path.exists(bat):
                return bat
    return None


def has_windows_sdk(vcvars):
    """The C runtime headers (stdio.h) and windows.h come from the Windows SDK, a separate
    Visual Studio component; without it every file fails on its first include."""
    r = subprocess.run(f'"{vcvars}" >nul 2>nul && set WindowsSdkDir', shell=True,
                       capture_output=True, text=True)
    return "WindowsSdkDir=" in r.stdout and r.stdout.split("WindowsSdkDir=", 1)[1].strip() != ""


VCVARS = find_vcvars()
FLAGS = ["/nologo", "/c", "/std:c++17", "/EHsc", "/W3", "/permissive-", "/Zc:__cplusplus",
         "/D_CRT_SECURE_NO_WARNINGS", "/DWIN32_LEAN_AND_MEAN", "/DNOMINMAX", f"/I\"{SRC}\""]


def collect(args):
    files = []
    targets = args or [SRC]
    for t in targets:
        t = os.path.abspath(t)
        if os.path.isdir(t):
            for dp, dirs, fns in os.walk(t):
                dirs[:] = [d for d in dirs if d != "tier1"]   # tier-1 sources are built with g++
                files += [os.path.join(dp, f) for f in fns if f.endswith(".cpp")]
        else:
            files.append(t)
    return sorted(files)


GXX_FLAGS = ["-std=gnu++17", "-m32", "-fsyntax-only", "-Wall", "-Wno-unknown-pragmas", "-Wno-comment",
             "-Wno-invalid-offsetof", "-D_CRT_SECURE_NO_WARNINGS", "-DWIN32_LEAN_AND_MEAN", "-DNOMINMAX",
             f"-I{SRC}"]


def check_gxx(files):
    import shutil
    from concurrent.futures import ThreadPoolExecutor
    gxx = shutil.which("g++")
    if not gxx:
        sys.exit("neither MSVC nor g++ found")

    def run(f):
        r = subprocess.run([gxx] + GXX_FLAGS + [f], capture_output=True, text=True)
        return r.stdout + r.stderr

    with ThreadPoolExecutor(max_workers=8) as pool:
        outputs = list(pool.map(run, files))
    lines = [l for out in outputs for l in out.splitlines() if l.strip()]
    errors = [l for l in lines if ": error:" in l or "fatal error:" in l]
    warnings = [l for l in lines if ": warning:" in l]
    for l in lines:
        print(l.replace(ROOT + "\\", "").replace(ROOT + "/", ""))
    print(f"\n{len(files)} files (g++ syntax check), {len(errors)} errors, {len(warnings)} warnings")
    sys.exit(1 if errors else 0)


def main():
    files = collect(sys.argv[1:])
    if not VCVARS:
        check_gxx(files)
    if not has_windows_sdk(VCVARS):
        print("MSVC found but no Windows SDK (add it in the Visual Studio Installer); using g++\n")
        check_gxx(files)
    out = os.path.join(tempfile.gettempdir(), "hd2_build_check")
    os.makedirs(out, exist_ok=True)
    rsp = os.path.join(out, "files.rsp")
    with open(rsp, "w") as fh:
        fh.write("\n".join(f'"{f}"' for f in files))
    bat = os.path.join(out, "build.bat")
    with open(bat, "w") as fh:
        fh.write("@echo off\n")
        fh.write(f'call "{VCVARS}" >nul\n')
        fh.write(f'cl {" ".join(FLAGS)} /Fo"{out}\\\\" @"{rsp}"\n')
    r = subprocess.run([bat], capture_output=True, text=True)
    # (vcvars32 calls vswhere, which is not on the PATH: its two-line complaint is dropped.)
    noise = ("Compiling...", "Generating Code...", "operable program or batch file.")
    lines = [l for l in (r.stdout + r.stderr).splitlines()
             if l.strip() and not l.strip().endswith(".cpp") and "vswhere" not in l
             and l.strip() not in noise]
    errors = [l for l in lines if ": error" in l or "fatal error" in l]
    warnings = [l for l in lines if ": warning" in l]
    for l in lines:
        print(l.replace(ROOT + "\\", ""))
    print(f"\n{len(files)} files, {len(errors)} errors, {len(warnings)} warnings")
    sys.exit(1 if errors else 0)


if __name__ == "__main__":
    main()
