"""mutrun.py MUTATE_FILE WORK [JOBS] [LABEL...]: mutation runner for a harness with a build_*.bat. Env MUT_HARNESS: its directory
under _re/harness (default net); MUT_BAT: the .bat (default build.bat); MUT_TEST: the test source (default the bat's first
local .cpp); MUT_PASS: the regex of a passing run's output, bytes (default: "NAME: N checks, 0 failures").

Bounded: a test's output is read up to OUTPUT_LIMIT and it is killed beyond that (a mutant that prints for ever once ran the
machine out of memory through capture_output), or after TIMEOUT. Results print as they finish. The unmutated sources are
compiled once. Env MUT_DEFS: extra -D options, space separated (-DGAMESPY_BROWSER_ONLY skips the slower earlier tests)."""
import concurrent.futures, importlib.util, os, queue, re, shutil, subprocess, sys, threading

ROOT = r"G:\Programming\HD2 Source Port\C++"
MUT, WORK = sys.argv[1], sys.argv[2]
JOBS = int(sys.argv[3]) if len(sys.argv) > 3 else 6
want = set(sys.argv[4:])
OUTPUT_LIMIT = 256 * 1024
TIMEOUT = 40
spec = importlib.util.spec_from_file_location("m", MUT)
mod = importlib.util.module_from_spec(spec)
spec.loader.exec_module(mod)
GXX = r"C:\nim-2.2.6\dist\mingw32\bin\g++.exe"
env = dict(os.environ, PATH=r"C:\nim-2.2.6\dist\mingw32\bin;" + os.environ["PATH"])
HARNESS = ROOT + "\\_re\\harness\\" + os.environ.get("MUT_HARNESS", "net")
bat = open(HARNESS + "\\" + os.environ.get("MUT_BAT", "build.bat"), encoding="utf-8").read()
SRCS = re.findall(r"%SRC%\\([\w\\]+\.cpp)", next(l for l in bat.splitlines() if l.startswith("g++")))
LINE = next(l for l in bat.splitlines() if l.startswith("g++"))
LOCALS = list(dict.fromkeys(re.findall(r"(?<![\\\w%])(\w+\.cpp)", LINE)))
PASS = os.environ.get("MUT_PASS", r"\w+: \d+ checks, 0 failures").encode()
EXTRA = os.environ.get("MUT_DEFS", "").split()
FLAGS = ["-std=c++17", "-faligned-new=8", "-O1", "-msse2", "-mfpmath=sse", "-DLS3D_API=", "-DRW_DATA_API="] + EXTRA


def compile_one(source, obj, d, port):
    cmd = [GXX] + FLAGS + ["-DSB_TEST_PORT=%d" % (port - 1), "-DGCD_TEST_PORT=%d" % port, "-I" + d + r"\src", "-I" + d + r"\h",
                           "-c", source, "-o", obj]
    r = subprocess.run(cmd, capture_output=True, text=True, env=env)
    if r.returncode != 0:
        compile_one.last_error = r.stderr[:1500]
    return r.returncode == 0


def tree(n, port):
    d = "%s\\w%s" % (WORK, n)
    if os.path.exists(d):
        shutil.rmtree(d)
    # (Not tier 1: the copies filled C: once.)
    shutil.copytree(ROOT + r"\src", d + r"\src", ignore=shutil.ignore_patterns("tier1", "*.gch", "*.obj"))
    shutil.copytree(HARNESS, d + r"\h", ignore=shutil.ignore_patterns("*.exe"))
    objs = {}
    for s in SRCS:
        objs[s] = d + "\\" + s.replace("\\", "_") + ".o"
        if not compile_one(d + "\\src\\" + s, objs[s], d, port):
            raise SystemExit("base compile failed: " + s + "\n" + compile_one.last_error)
    tests = []
    for i, local in enumerate(LOCALS):
        tests.append(d + "\\test%d.o" % i)
        if not compile_one(d + "\\h\\" + local, tests[-1], d, port):
            raise SystemExit("test compile failed: " + local + "\n" + compile_one.last_error)
    return d, objs, tests


def run_exe(exe):
    """"pass", "fail", or "timeout"; never holds more than the tail of the output."""
    p = subprocess.Popen([exe], stdout=subprocess.PIPE, stderr=subprocess.STDOUT, env=env)
    tail = [b""]
    over = [False]

    def reader():
        total = 0
        while True:
            chunk = p.stdout.read(65536)
            if not chunk:
                return
            total += len(chunk)
            tail[0] = (tail[0] + chunk)[-4096:]
            if total > OUTPUT_LIMIT:
                over[0] = True
                p.kill()
                return

    t = threading.Thread(target=reader, daemon=True)
    t.start()
    try:
        p.wait(timeout=TIMEOUT)
    except subprocess.TimeoutExpired:
        p.kill()
        p.wait()
        t.join(5)
        return "timeout"
    t.join(5)
    if over[0]:
        return "fail"
    return "pass" if re.search(PASS, tail[0]) else "fail"


def run(d, objs, test, port, rebuilt=None):
    exe = d + r"\t.exe"
    use = dict(objs)
    if rebuilt:
        source, name = rebuilt
        obj = d + r"\mutant.o"
        if not compile_one(source, obj, d, port):
            return "build failed"
        use[name] = obj
    cmd = [GXX] + list(use.values()) + list(test) + ["-lws2_32", "-limm32", "-o", exe]
    linked = subprocess.run(cmd, capture_output=True, text=True, env=env)
    if linked.returncode != 0:
        if not rebuilt:
            print(linked.stderr[-1500:], flush=True)
        return "build failed"
    return run_exe(exe)


muts = [m for m in mod.MUTATIONS if not want or m[1] in want]
os.makedirs(WORK, exist_ok=True)
BASE_PORT = 30000 + os.getpid() % 15000
base = tree("base", BASE_PORT)
print("baseline:", run(*base, BASE_PORT), flush=True)
work = queue.Queue()
for m in muts:
    work.put(m)
lock = threading.Lock()


def worker(n):
    port = BASE_PORT + 2 * (n + 1)
    d, objs, test = tree(n, port)
    while True:
        try:
            path, label, pattern, replacement = work.get_nowait()
        except queue.Empty:
            return
        name = os.path.relpath(path, ROOT + r"\src").replace("/", "\\")
        copy = d + "\\src\\" + name
        original = open(copy, encoding="utf-8", newline="").read()
        if original.count(pattern) != 1:
            result = "pattern found %d" % original.count(pattern)
        else:
            open(copy, "w", encoding="utf-8", newline="").write(original.replace(pattern, replacement))
            try:
                r = run(d, objs, test, port, (copy, name))
            finally:
                open(copy, "w", encoding="utf-8", newline="").write(original)
            result = "killed" if r in ("fail", "timeout") else r if r == "build failed" else "SURVIVED"
        with lock:
            print(label + ":", result, flush=True)


with concurrent.futures.ThreadPoolExecutor(JOBS) as ex:
    for f in [ex.submit(worker, j) for j in range(JOBS)]:
        f.result()
