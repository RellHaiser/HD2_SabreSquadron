"""Mutation tests of the multiplayer set-up's port with this harness.

  python mutate.py MUTATIONS.py DATA WORK [-j N]

MUTATIONS.py defines MUTATIONS = [(path, name, pattern, replacement), ...] (the pattern occurs once in
the file at path). The other sources are compiled once into WORK; each mutation recompiles only its file,
links, and runs run_tests.py on DATA. Prints the survivors (and mutations that do not build).
"""
import concurrent.futures
import os
import runpy
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
SRC = os.path.normpath(os.path.join(HERE, "..", "..", "..", "src"))
GPP = r"C:\nim-2.2.6\dist\mingw32\bin\g++.exe"
ENV = dict(os.environ, PATH=r"C:\nim-2.2.6\dist\mingw32\bin;" + os.environ["PATH"])
FLAGS = ["-std=c++17", "-O1", "-I" + os.path.join(HERE, "stub"), "-I" + SRC]
SOURCES = [os.path.join(HERE, "harness.cpp"), os.path.join(SRC, "common", "c_str.cpp"),
           os.path.join(SRC, "common", "chunk.cpp"), os.path.join(SRC, "game", "xml_node.cpp"),
           os.path.join(SRC, "game", "mp_game_setup.cpp")]


def compile_(src, obj):
    return subprocess.run([GPP, *FLAGS, "-c", src, "-o", obj], env=ENV, capture_output=True).returncode == 0


def main():
    mutations = runpy.run_path(sys.argv[1])["MUTATIONS"]
    data, work = sys.argv[2], sys.argv[3]
    jobs = int(sys.argv[sys.argv.index("-j") + 1]) if "-j" in sys.argv else 4
    os.makedirs(work, exist_ok=True)
    objs = {}
    for src in SOURCES:
        obj = os.path.join(work, os.path.basename(src) + ".o")
        if not compile_(src, obj):
            print("base build failed:", src)
            return 1
        objs[os.path.normcase(os.path.abspath(src))] = obj
    texts = {}

    def one(arg):
        index, (path, name, pattern, replacement) = arg
        key = os.path.normcase(os.path.abspath(path))
        text = texts.setdefault(key, open(path, encoding="utf-8", newline="").read())
        if text.count(pattern) != 1:
            return name, "pattern"
        d = os.path.join(work, "m%d" % index)
        os.makedirs(d, exist_ok=True)
        src = os.path.join(d, os.path.basename(path))
        open(src, "w", encoding="utf-8", newline="").write(text.replace(pattern, replacement))
        obj = os.path.join(d, "mut.o")
        if not compile_(src, obj):
            return name, "build"
        exe = os.path.join(d, "harness.exe")
        link = [o for k, o in objs.items() if k != key] + [obj]
        if subprocess.run([GPP, *link, "-o", exe], env=ENV, capture_output=True).returncode != 0:
            return name, "build"
        r = subprocess.run([sys.executable, os.path.join(HERE, "run_tests.py"), exe, data, "--quiet"],
                           capture_output=True, text=True)
        return name, "killed" if r.returncode != 0 else "survived"

    results = []
    with concurrent.futures.ThreadPoolExecutor(jobs) as pool:
        for name, result in pool.map(one, enumerate(mutations)):
            results.append((name, result))
    killed = sum(1 for _, r in results if r == "killed")
    print("%d mutations: %d killed" % (len(results), killed))
    for name, r in results:
        if r != "killed":
            print("  %s: %s" % (r, name))
    return 0


if __name__ == "__main__":
    sys.exit(main())
