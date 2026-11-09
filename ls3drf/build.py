"""Builds the LS3DRF.dll port (x86, MSVC).

Usage: python build.py [dll] [check] [test] [run] [--debug] [--only SUBSTR] [--ops N] [--seeds 1,2]
  dll    (default) compile everything and link build/msvc/LS3DRF.dll with ls3drf.def
  check  compile every translation unit, no link (fast syntax/type check while porting)
  test   build tests/*.cpp into build/<name>.exe
  run    run the built tests with their standard arguments (--ops, default 20000, each of --seeds,
         default 1; the originals from %HD2_DIR%), one line each; full output in %TEMP%\\ls3drf_build\\logs
Intermediate files go to %TEMP%\\ls3drf_build: a synced source folder (Google Drive, OneDrive) locks
freshly written files while it uploads them, which breaks cl and link.
Needs Visual Studio with the C++ x86 tools (found with vswhere) and MinGW's dlltool (%DLLTOOL%, PATH
or a common install) for the import libraries of lib/*.def.
"""
import os
import re
import shutil
import subprocess
import sys
import tempfile
from concurrent.futures import ThreadPoolExecutor

ROOT = os.path.dirname(os.path.abspath(__file__))
SRC = os.path.join(ROOT, "src")
VSWHERE = r"C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe"
WORK = os.path.join(tempfile.gettempdir(), "ls3drf_build")
LIBS = ["kernel32.lib", "user32.lib", "gdi32.lib", "ole32.lib", "winmm.lib", "advapi32.lib"]
# Import libraries made from lib/*.def (rw_data): MSVC's lib.exe cannot keep the leading underscore
# of decorated __stdcall export names ("_dtaOpen@8"), MinGW dlltool can.
IMPORT_DEFS = ["rw_data"]
# Where to look for it: %DLLTOOL%, PATH, then common 32-bit MinGW installs (MSYS2, Nim's own).
DLLTOOL_PATHS = [r"C:\msys64\mingw32\bin\dlltool.exe", r"C:\mingw32\bin\dlltool.exe",
                 r"C:\nim-2.2.6\dist\mingw32\bin\dlltool.exe"]


def sources(only=None):
    out = []
    for d, _, files in os.walk(SRC):
        for f in files:
            if f.endswith(".cpp"):
                p = os.path.join(d, f)
                if not only or only in p.replace("\\", "/"):
                    out.append(p)
    return sorted(out)


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


_env = None


def msvc_env():
    """The environment vcvars32.bat sets up (computed once)."""
    global _env
    if _env is None:
        bat = find_vcvars()
        if not bat:
            sys.exit("msvc: Visual Studio with the C++ x86 tools not found")
        r = subprocess.run(f'"{bat}" >nul 2>nul && set', shell=True, capture_output=True, text=True)
        env = dict(line.split("=", 1) for line in r.stdout.splitlines() if "=" in line)
        path = env.get("PATH") or env.get("Path", "")
        for tool in ("cl", "link"):
            env["_" + tool.upper()] = shutil.which(tool, path=path) or tool
        _env = env                      # complete before other threads can see it
    return _env


def tool(name):
    return msvc_env()["_" + name.upper()]


def cflags(debug):
    return ["/nologo", "/c", "/EHsc", "/std:c++17", "/W4", "/permissive-", "/MT" + ("d" if debug else ""),
            "/D_CRT_SECURE_NO_WARNINGS", "/DWIN32_LEAN_AND_MEAN", "/DNOMINMAX", "/DLS3D_API=", "/Zi",
            "/Od" if debug else "/O2", "/DNDEBUG" if not debug else "/D_DEBUG",
            "/wd4100",                      # unreferenced parameters (interface methods)
            "/I" + SRC, "/I" + os.path.join(ROOT, "include")]


# The device seams (docs/render_device.md, sound_device.md, input_device.md, video_device.md): only a
# backend and the API declarations may name the API's interfaces or include them; the rest of the
# engine names no device API. (The Direct3D 8, sound, video and input backends and the render device
# are cut from this build: their entries now hold for the whole tree. The tests' Direct3D 8
# declarations live in tests/d3d8.)
SEAMS = [
    ("the Direct3D 8 backend", ("dx/",),
     re.compile(r'#include "dx/d3d8\.h"|#include "gfx/d3d8/|\bIDirect3D\w*8\b|\bg_pD3D\b|\bg_pDevice\b')),
    ("the DirectSound backend", ("dx/", "audio/dsound/"),
     re.compile(r'#include <dsound\.h>|#include "audio/dsound/|\bIDirectSound\w*|\bIKsPropertySet\b|'
                r'\bLPDIRECTSOUND\w*|\bDirectSound(Create8|Enumerate\w*)\b')),
    ("the DirectInput backend", ("dx/", "input/dinput/"),
     re.compile(r'#include <dinput\.h>|#include "input/dinput/|\bIDirectInput\w*|\bLPDIRECTINPUT\w*|'
                r'\bDirectInput8Create\b')),
    # Bink and DirectShow (with the DirectShow base classes, src/dshow, part of the video backend).
    ("the Windows video backend", ("dx/", "external/", "dshow/", "video/win32/"),
     re.compile(r'#include <dshow\.h>|#include "external/bink\.h"|#include "dshow/|#include "video/win32/|'
                r'\bBink[A-Z]\w*\s*\(|\b(IGraphBuilder|IMediaControl|IMediaSeeking|IBasicAudio|IMediaEvent|'
                r'CTextureRenderer)\b')),
    # The window, its messages, the cursor, time, threads, locks and events (docs/platform.md). (The
    # DirectShow base classes keep theirs: they belong to the video backend. The window and the
    # threads are cut from this build.)
    ("the Win32 platform", ("platform/win32/", "dshow/"),
     re.compile(r'#include <mmsystem\.h>|\b(timeGetTime|GetTickCount|Sleep|CreateWindowExA|RegisterClassExA|'
                r'DefWindowProcA|PeekMessageA|DispatchMessageA|TranslateMessage|IsDialogMessageA|WaitMessage|'
                r'SetWindowTextA|SetFocus|UpdateWindow|GetWindowRect|GetSystemMetrics|ClipCursor|SetCursorPos|'
                r'SystemParametersInfoA|PostQuitMessage|GetForegroundWindow|IsChild|LoadMenuA|LoadCursorA|'
                r'GetStockObject|GetDesktopWindow|VkKeyScanA|MapVirtualKeyA|CreateThread|SuspendThread|'
                r'ResumeThread|TerminateThread|SetThreadPriority|WaitForSingleObject|InitializeCriticalSection|'
                r'DeleteCriticalSection|EnterCriticalSection|LeaveCriticalSection|CreateEventA|CloseHandle|'
                r'MsgWaitForMultipleObjects)\s*\(')),
]


# The LS3DF commit whose changes this port has taken (README, "Source and LS3DF").
LS3DF_CARRIED = "d8ced1c"


def check_ls3df():
    """In HD2_SabreSquadron: False when ls3df/ changed, since LS3DF_CARRIED, a file this port has."""
    repo = os.path.dirname(ROOT)
    if not os.path.isdir(os.path.join(repo, "ls3df", "src")):
        return True                                     # (the standalone repository)
    r = subprocess.run(["git", "-C", repo, "diff", "--name-only", LS3DF_CARRIED + "^{tree}", "HEAD:ls3df",
                        "--", "src", "tests", "lib"], capture_output=True, text=True)
    if r.returncode:
        print("ls3df: cannot compare with %s: %s" % (LS3DF_CARRIED, r.stderr.strip()))
        return False
    changed = r.stdout.split()
    here = [f for f in changed if os.path.exists(os.path.join(ROOT, f))]
    if changed and not here:
        print("ls3df: %d file(s) changed since %s, none of them in this port" % (len(changed), LS3DF_CARRIED))
    if here:
        print("ls3df: changed since %s and not carried over (README, \"Source and LS3DF\"):" % LS3DF_CARRIED)
        for f in here:
            print("  " + f)
    return not here


def check_seams():
    """Lines outside the backends that use what only a backend may (printed); True when none."""
    bad = []
    for d, _, files in os.walk(SRC):
        for f in files:
            path = os.path.join(d, f)
            rel = os.path.relpath(path, SRC).replace("\\", "/")
            if not f.endswith((".cpp", ".h")):
                continue
            lines = list(enumerate(open(path, encoding="utf-8", errors="replace"), 1))
            for name, allowed, pattern in SEAMS:
                if rel.startswith(allowed):
                    continue
                for n, line in lines:
                    if pattern.search(line.split("//", 1)[0]):   # (comments may name them)
                        bad.append("%s(%d): uses %s: %s" % (rel, n, name, line.strip()))
    for line in bad:
        print(line)
    return not bad


def compile_one(src, debug):
    rel = os.path.relpath(src, SRC)
    obj = os.path.join(WORK, "obj", re.sub(r"[\\/]", "_", rel)[:-4] + ".obj")
    pdb = os.path.join(WORK, "obj", "ls3drf.pdb")
    cmd = [tool("cl"), *cflags(debug), "/FS", "/Fd" + pdb, "/Fo" + obj, src]
    r = subprocess.run(cmd, capture_output=True, text=True, env=msvc_env())
    text = "\n".join(l for l in (r.stdout + r.stderr).splitlines()
                     if l.strip() and l.strip() != os.path.basename(src))
    return src, obj, r.returncode == 0, text


def compile_all(debug, only=None):
    os.makedirs(os.path.join(WORK, "obj"), exist_ok=True)
    srcs = sources(only)
    ok = True
    objs = []
    with ThreadPoolExecutor(max(2, os.cpu_count() or 4)) as ex:
        for src, obj, good, text in ex.map(lambda s: compile_one(s, debug), srcs):
            if text:
                print(text)
            ok &= good
            objs.append(obj)
    print("compiled %d files: %s" % (len(srcs), "ok" if ok else "ERRORS"))
    return ok, objs


def import_libs():
    """WORK/<name>.lib for each lib/<name>.def (remade when the .def is newer)."""
    dlltool = (os.environ.get("DLLTOOL") or shutil.which("dlltool")
               or next((p for p in DLLTOOL_PATHS if os.path.exists(p)), None))
    out = []
    for name in IMPORT_DEFS:
        src = os.path.join(ROOT, "lib", name + ".def")
        lib = os.path.join(WORK, name + ".lib")
        if not os.path.exists(lib) or os.path.getmtime(lib) < os.path.getmtime(src):
            if not dlltool:
                sys.exit("import libraries: MinGW dlltool not found (set DLLTOOL to its path)")
            os.makedirs(WORK, exist_ok=True)
            subprocess.run([dlltool, "-m", "i386", "-d", src, "-l", lib], check=True)
        out.append(lib)
    return out


def link(objs, debug):
    dll = os.path.join(WORK, "LS3DRF.dll")
    cmd = [tool("link"), "/nologo", "/DLL", "/DEBUG", "/INCREMENTAL:NO", "/MACHINE:X86",
           "/DEF:" + os.path.join(ROOT, "ls3drf.def"), "/OUT:" + dll, *objs, *LIBS, *import_libs()]
    r = subprocess.run(cmd, capture_output=True, text=True, env=msvc_env())
    text = "\n".join(l for l in (r.stdout + r.stderr).splitlines() if l.strip() and "Creating library" not in l)
    if text:
        print(text)
    if r.returncode != 0:
        print("link: FAILED")
        return False
    out = os.path.join(ROOT, "build", "msvc")
    os.makedirs(out, exist_ok=True)
    for name in ("LS3DRF.dll", "LS3DRF.lib", "LS3DRF.pdb"):
        if os.path.exists(os.path.join(WORK, name)):
            shutil.copyfile(os.path.join(WORK, name), os.path.join(out, name))
    print("link: ok ->", os.path.join(out, "LS3DRF.dll"))
    return True


def test_directives(path):
    """`// test-sources: src/igraph/*.cpp ...` lines of a test: port sources it links."""
    import glob
    srcs, libs = [], []
    for line in open(path, encoding="utf-8", errors="replace"):
        m = re.match(r"\s*//\s*test-(sources|libs):\s*(.*)", line)
        if m:
            for pat in m.group(2).split():
                found = sorted(glob.glob(os.path.join(ROOT, pat)))
                (srcs if m.group(1) == "sources" else libs).extend(found or [pat])
    return srcs, libs


def build_tests(debug, only=None):
    ok = True
    tdir = os.path.join(ROOT, "tests")
    out = os.path.join(ROOT, "build")
    os.makedirs(out, exist_ok=True)
    compiled = {}                       # a port source compiled for one test serves the others
    for f in sorted(os.listdir(tdir)) if os.path.isdir(tdir) else []:
        if not f.endswith(".cpp") or (only and only not in f):
            continue
        exe = os.path.join(WORK, f[:-4] + ".exe")
        flags = [x for x in cflags(debug) if x != "/c"]
        extra, extra_libs = test_directives(os.path.join(tdir, f))
        objs = []
        if extra:
            # Port sources compiled for the test (same flags as the DLL).
            os.makedirs(os.path.join(WORK, "obj"), exist_ok=True)
            with ThreadPoolExecutor(max(2, os.cpu_count() or 4)) as ex:
                for src, obj, good, text in ex.map(lambda s: compiled.get(s) or compile_one(s, debug), extra):
                    if text:
                        print(text)
                    ok &= good
                    objs.append(obj)
                    compiled[src] = (src, obj, good, "")
        cmd = [tool("cl"), *flags, "/Fo" + WORK + "\\", "/Fd" + os.path.join(WORK, f[:-4] + ".pdb"), "/Fe" + exe,
               os.path.join(tdir, f), *objs, "/link", "/INCREMENTAL:NO", *LIBS, *import_libs(), *extra_libs]
        r = subprocess.run(cmd, capture_output=True, text=True, env=msvc_env())
        text = "\n".join(l for l in (r.stdout + r.stderr).splitlines() if l.strip() and l.strip() != f)
        if text:
            print(text)
        if r.returncode == 0:
            shutil.copyfile(exe, os.path.join(out, f[:-4] + ".exe"))
        ok &= r.returncode == 0
        print("test %s: %s" % (f, "ok" if r.returncode == 0 else "FAILED"))
    return ok


# Arguments of the tests that take no operation count and seed (after the original's path, "" = found
# by %HD2_DIR% or PATH). compare_math's count is the rounds; without it "--pc24" is read as 0 rounds.
RUN_ARGS = {"compare_math": ["{ops}", "--pc24"], "compare_dxerr8": []}


def run_tests(only=None, ops="20000", seeds=("1",)):
    tdir = os.path.join(ROOT, "tests")
    logs = os.path.join(WORK, "logs")
    os.makedirs(logs, exist_ok=True)
    env = dict(os.environ)
    if env.get("HD2_DIR"):
        # The originals import rw_data.dll and others from the game folder.
        env["PATH"] = env["HD2_DIR"] + os.pathsep + env.get("PATH", "")
    ok = True
    for f in sorted(os.listdir(tdir)) if os.path.isdir(tdir) else []:
        name = f[:-4]
        if not f.endswith(".cpp") or (only and only not in f):
            continue
        exe = os.path.join(ROOT, "build", name + ".exe")
        if not os.path.isfile(exe):
            print("run %s: not built" % name)
            ok = False
            continue
        runs = [[a.format(ops=ops) for a in RUN_ARGS[name]]] if name in RUN_ARGS else [[ops, s] for s in seeds]
        for args in runs:
            r = subprocess.run([exe, "", *args], capture_output=True, text=True, errors="replace", env=env)
            out = r.stdout + r.stderr
            with open(os.path.join(logs, "-".join([name, *args]) + ".txt"), "w", encoding="utf-8") as log:
                log.write(out)
            last = ([l for l in out.splitlines() if l.strip()] or ["(no output)"])[-1]
            print("run %s: %s%s" % (" ".join([name, *args]), last, "" if r.returncode == 0 else " (exit %d)" % r.returncode))
            ok &= r.returncode == 0
    return ok


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    debug = "--debug" in sys.argv

    def option(name, default):
        if name not in sys.argv:
            return default
        value = sys.argv[sys.argv.index(name) + 1]
        if value in args:
            args.remove(value)
        return value

    only = option("--only", None)
    ops = option("--ops", "20000")
    seeds = option("--seeds", "1").split(",")
    targets = args or ["dll"]
    ok = True
    if "check" in targets or "dll" in targets:
        ok &= check_seams()
    if "check" in targets:
        ok &= check_ls3df()
        ok &= compile_all(debug, only)[0]
    if "dll" in targets:
        good, objs = compile_all(debug)
        ok &= good and link(objs, debug)
    if "test" in targets:
        ok &= build_tests(debug, only)
    if "run" in targets and ok:
        ok &= run_tests(only, ops, seeds)
    sys.exit(0 if ok else 1)


if __name__ == "__main__":
    main()
