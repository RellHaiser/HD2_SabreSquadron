"""Fast mutation testing of the ported vehicle code against this harness.

    python mutfast.py MUTATIONS.py --models DIR [--work DIR] [--jobs N] [--timeout S] [--from NAME]
                      [--baseline] [--build BAT --check PY] [NAME ...]

MUTATIONS.py defines MUTATIONS, a list of (path, name, pattern, replacement): a source file in the
repository, the mutation's name, a text that occurs once in that file and its replacement. Each
mutation should make the harness fail (check.py finds models with differences, or the harness
crashes).

The runner works on a copy of src/ and the harness (the work folder, by default
%TEMP%/mutfast/<the name of MUTATIONS.py>), so the real sources stay free to edit meanwhile: every
object is built once, each mutation recompiles only its file (all of them for a header) and links,
and the models (the .txt files export_cars.py writes into DIR) run in parallel chunks. --timeout:
seconds per chunk (a mutation can make the harness loop). --from: start at that mutation; NAMEs:
only those. --build / --check: another build of the harness and its checker (the seat weapon's:
build_sw.bat, check_sw.py; the build's -D and -Wl, options are taken from its g++ line). Prints a line per mutation: the harness's exit code, how many models check.py fails and
the kinds of failure ("?" when check.py could not read the output, e.g. after a crash).
"""
import argparse
import concurrent.futures
import glob
import importlib.util
import os
import re
import shutil
import subprocess
import sys
import tempfile
import time

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, r"..\..\.."))
GXX = r"C:\nim-2.2.6\dist\mingw32\bin\g++.exe"
ENV = dict(os.environ, PATH=os.path.dirname(GXX) + os.pathsep + os.environ["PATH"])


class Tree:
    """The work folder: the copied sources, their objects, the harness built from them."""

    def __init__(self, work, build="build.bat", checker="check.py"):
        self.build, self.checker = build, checker
        self.at(work)

    def at(self, work):
        self.work = work
        self.harness = os.path.join(work, r"_re\harness\vehicle_setup")
        self.obj = os.path.join(work, "obj")
        self.exe = os.path.join(work, "mut.exe")
        self.flags = ["-std=c++17", "-O1", "-Wall", "-Wextra", "-Wno-invalid-offsetof", "-msse2", "-mfpmath=sse",
                      "-DLS3D_API=", "-DRW_DATA_API=", "-I" + os.path.join(work, r"_re\harness\vehicle_setup\stub"),
                      "-I" + os.path.join(work, r"_re\harness\vehicle_params\stub"),
                      "-I" + os.path.join(work, "src")]

    def copy(self):
        for _ in range(20):             # (a virus scanner may still hold the last run's files)
            if not os.path.exists(self.work):
                break
            shutil.rmtree(self.work, ignore_errors=True)
            time.sleep(0.5)
        if os.path.exists(self.work):   # (still held: a fresh folder next to it)
            self.at(f"{self.work}_{int(time.time())}")
        shutil.copytree(os.path.join(ROOT, "src"), os.path.join(self.work, "src"))
        for d in (r"_re\harness\vehicle_setup", r"_re\harness\vehicle_params\stub"):
            shutil.copytree(os.path.join(ROOT, d), os.path.join(self.work, d),
                            ignore=shutil.ignore_patterns("*.exe", "*.txt", "*.o"))
        os.makedirs(self.obj)

    def sources(self):
        """The harness's sources as the build's .bat lists them (in the copy); its -D and -f options join the flags."""
        bat = open(os.path.join(HERE, self.build)).read()
        line = next(l for l in bat.splitlines() if l.startswith("g++"))
        self.flags += [t for t in line.split() if t.startswith(("-D", "-f")) and t not in self.flags]
        self.link_flags = [t for t in line.split() if t.startswith("-Wl,")]   # (part 7d's --wrap)
        return [os.path.normpath(os.path.join(self.harness, t)) for t in line.split() if t.endswith(".cpp")]

    def obj_of(self, src):
        return os.path.join(self.obj, re.sub(r"[\\/:]", "_", os.path.relpath(src, self.work))[:-4] + ".o")

    def compile(self, srcs):
        """Builds the objects; the first failure's output (or None)."""
        def one(src):
            r = subprocess.run([GXX] + self.flags + ["-c", src, "-o", self.obj_of(src)], capture_output=True,
                               text=True, env=ENV)
            return r.returncode != 0 or re.search(r"\berror\b", r.stdout + r.stderr), r.stdout + r.stderr

        with concurrent.futures.ThreadPoolExecutor(16) as ex:
            return next((text for bad, text in ex.map(one, srcs) if bad), None)

    def link(self, srcs):
        r = subprocess.run([GXX] + [self.obj_of(s) for s in srcs] + getattr(self, "link_flags", []) + ["-lwinmm", "-o", self.exe], capture_output=True,
                           text=True, env=ENV)
        return None if r.returncode == 0 else r.stdout + r.stderr

    def run(self, models, jobs, timeout):
        """The models in `jobs` parallel chunks, the output in model order: (exit code, output file)."""
        chunks = [c for c in (models[i::jobs] for i in range(jobs)) if c]

        def one(k):
            out = os.path.join(self.work, f"out_{k}.txt")
            with open(out, "w") as f:
                try:
                    code = subprocess.run([self.exe] + sorted(chunks[k]), stdout=f, stderr=subprocess.STDOUT,
                                          cwd=self.harness, timeout=timeout).returncode
                except subprocess.TimeoutExpired:
                    code = "timeout"
            return k, code, out

        with concurrent.futures.ThreadPoolExecutor(len(chunks)) as ex:
            results = sorted(ex.map(one, range(len(chunks))))
        merged = os.path.join(self.work, "out.txt")
        with open(merged, "w") as f:
            for _, _, out in results:
                f.write(open(out).read())
                os.remove(out)
        return next((c for _, c, _ in results if c != 0), 0), merged

    def check(self, models_dir, out):
        c = subprocess.run([sys.executable, os.path.join(self.harness, self.checker), models_dir, out],
                           capture_output=True, text=True, cwd=self.harness)
        m = re.search(r"(\d+) models, (\d+) with differences", c.stdout)
        kinds = sorted(set(re.findall(r"\('([a-z ]+)'", c.stdout)))
        return (m.group(2) if m else "?"), (m.group(1) if m else "?"), kinds


def main():
    p = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    p.add_argument("mutations")
    p.add_argument("names", nargs="*")
    p.add_argument("--models", required=True)
    p.add_argument("--work")
    p.add_argument("--jobs", type=int, default=12)
    p.add_argument("--timeout", type=int, default=120)
    p.add_argument("--from", dest="start")
    p.add_argument("--baseline", action="store_true")
    p.add_argument("--build", default="build.bat")
    p.add_argument("--check", default="check.py")
    a = p.parse_args()

    spec = importlib.util.spec_from_file_location("mutations", a.mutations)
    mod = importlib.util.module_from_spec(spec)
    argv, sys.argv = sys.argv, [a.mutations, "--check"]     # (older lists run their own main(): only the check)
    spec.loader.exec_module(mod)
    sys.argv = argv
    name = os.path.splitext(os.path.basename(a.mutations))[0]
    tree = Tree(a.work or os.path.join(tempfile.gettempdir(), "mutfast", name), a.build, a.check)
    tree.copy()
    srcs = tree.sources()
    failed = tree.compile(srcs) or tree.link(srcs)
    if failed:
        print(f"the unmutated build failed\n{failed[-1500:]}", flush=True)
        return
    models = sorted(glob.glob(os.path.join(a.models, "*.txt")))
    if a.baseline:
        code, out = tree.run(models, a.jobs, a.timeout)
        fails, total, kinds = tree.check(a.models, out)
        print(f"baseline: exit {code} -> {fails} of {total} models fail {kinds}", flush=True)

    start = a.start
    for path, label, pattern, replacement in mod.MUTATIONS:
        if start is not None:
            if label != start:
                continue
            start = None
        if a.names and label not in a.names:
            continue
        copy = os.path.join(tree.work, os.path.relpath(path, ROOT))
        original = open(copy, encoding="utf-8", newline="").read()
        if original.count(pattern) != 1:
            print(f"{label}: pattern found {original.count(pattern)} times", flush=True)
            continue
        todo = [copy] if copy.endswith(".cpp") else srcs
        open(copy, "w", encoding="utf-8", newline="").write(original.replace(pattern, replacement))
        try:
            failed = tree.compile(todo) or tree.link(srcs)
            if failed:
                print(f"{label}: build failed\n{failed[-1500:]}", flush=True)
                continue
            code, out = tree.run(models, a.jobs, a.timeout)
            fails, total, kinds = tree.check(a.models, out)
            print(f"{label}: exit {code} -> {fails} of {total} models fail {kinds}", flush=True)
        finally:
            open(copy, "w", encoding="utf-8", newline="").write(original)
            tree.compile(todo)          # (the unmutated objects back for the next one)


main()
