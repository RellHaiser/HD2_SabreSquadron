"""The port's functions and the original addresses they implement, from the PDB and the address comments.

    python _re/tools/port_symbols.py [--exe EXE] [--out FILE.json] [--show N]

For every function symbol of the port's own objects in EXE's map (default the coverage build,
%TEMP%\\hd2_game_cov\\HD2_Port.exe; any build_game.py build works) the PDB gives the source file and
line of its first instruction (dbghelp, offline: nothing runs). The original address(es) it ports are
taken from the `0x00XXXXXX` comments, looked for in this order, the first that has any:

  definition   the function's head line (walking back from the PDB line to the line naming the
               function followed by "(") and the comment lines right above it
  declaration  a line of a header naming "Name(" inside the class's body (class / struct Class ...),
               or a free function's declaration line, with an address comment on it
  body         (only when neither has one) none: the function is left without an address

Only addresses in the game range (0x00401000..0x007e2000) count. A function can name several (a
merged or absorbed helper's own comment counts too: "also 0x..."). Inline functions inlined
everywhere have no symbol. The result maps rva -> {symbol, name, file, line, how, addresses}.
"""
import argparse
import collections
import ctypes
import ctypes.wintypes as W
import json
import os
import re
import sys
import tempfile

ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))
SRC = os.path.join(ROOT, "src")
ADDR = re.compile(r"0x(00[4-7][0-9a-fA-F]{5})\b")
GAME_LO, GAME_HI = 0x401000, 0x7E2000


class IMAGEHLP_LINE64(ctypes.Structure):
    _fields_ = [("SizeOfStruct", W.DWORD), ("Key", ctypes.c_void_p), ("LineNumber", W.DWORD),
                ("FileName", ctypes.c_char_p), ("Address", ctypes.c_uint64)]


class Pdb:
    def __init__(self, exe, base):
        self.dbg = ctypes.WinDLL("dbghelp.dll")
        self.h = W.HANDLE(0x4844)
        self.dbg.SymSetOptions(0x2 | 0x10)          # SYMOPT_UNDNAME, SYMOPT_LOAD_LINES
        if not self.dbg.SymInitialize(self.h, None, False):
            raise OSError("SymInitialize failed")
        self.dbg.SymLoadModuleEx.restype = ctypes.c_uint64
        if not self.dbg.SymLoadModuleEx(self.h, None, exe.encode(), None, ctypes.c_uint64(base), 0, None, 0):
            raise OSError("SymLoadModuleEx failed for %s" % exe)

    def line(self, address):
        ln = IMAGEHLP_LINE64()
        ln.SizeOfStruct = ctypes.sizeof(IMAGEHLP_LINE64)
        disp = W.DWORD()
        if not self.dbg.SymGetLineFromAddr64(self.h, ctypes.c_uint64(address), ctypes.byref(disp), ctypes.byref(ln)):
            return None, None
        return ln.FileName.decode("latin-1"), ln.LineNumber

    def undecorate(self, sym):
        buf = ctypes.create_string_buffer(2048)
        n = self.dbg.UnDecorateSymbolName(sym.encode("latin-1"), buf, 2048, 0x1000)   # UNDNAME_NAME_ONLY
        return buf.value.decode("latin-1") if n else sym


def game_addresses(text):
    return [a for a in (int(m, 16) for m in ADDR.findall(text)) if GAME_LO <= a < GAME_HI]


class Sources:
    def __init__(self):
        self.cache = {}
        self.headers = None

    def lines(self, path):
        path = os.path.normcase(os.path.normpath(path))
        if path not in self.cache:
            try:
                self.cache[path] = open(path, encoding="utf-8", errors="replace").read().splitlines()
            except OSError:
                self.cache[path] = []
        return self.cache[path]

    def class_bodies(self):
        """Class -> [(header, first line, last line)] of every class / struct body in src (not tier1)."""
        if self.headers is None:
            self.headers = collections.defaultdict(list)
            for dp, dirs, fns in os.walk(SRC):
                dirs[:] = [d for d in dirs if d != "tier1"]
                for f in fns:
                    if not f.endswith(".h"):
                        continue
                    path = os.path.join(dp, f)
                    lines = self.lines(path)
                    for i, l in enumerate(lines):
                        m = re.match(r"\s*(?:class|struct)\s+([^;]*)$", l)
                        if not m:
                            continue
                        # (alignas(16), an export macro, `final`: the name is the last word before ':' / '{'.)
                        rest = re.sub(r"alignas\s*\([^)]*\)", " ", m.group(1)).split("{")[0]
                        rest = re.split(r"(?<!:):(?!:)", rest)[0]
                        words = [w for w in re.findall(r"\w+", rest) if w != "final"]
                        if not words:
                            continue
                        cname = words[-1]
                        depth, start = 0, None
                        for j in range(i, len(lines)):
                            depth += lines[j].count("{") - lines[j].count("}")
                            if start is None and "{" in lines[j]:
                                start = j
                            if start is not None and depth <= 0:
                                self.headers[cname].append((path, i, j))
                                break
        return self.headers


def find(pdb, sources, rva, base, sym):
    file, line = pdb.line(base + rva)
    full = pdb.undecorate(sym)
    # (operator names, destructors: the last part after the class.)
    parts = full.split("::")
    name = parts[-1]
    cls = parts[-2] if len(parts) > 1 else None
    out = dict(symbol=sym, name=full, file=None, line=line, how=None, addresses=[])
    if not file:
        return out
    if not os.path.normcase(os.path.abspath(file)).startswith(os.path.normcase(SRC) + os.sep):
        out["file"] = file          # (a library header's code: the STL's, the SDK's)
        return out
    out["file"] = os.path.relpath(file, ROOT).replace(os.sep, "/")
    lines = sources.lines(file)
    if not lines:
        return out
    key = re.escape(name) + r"\s*\("
    # The definition's head: back from the first instruction's line to the line naming the function.
    head = None
    for i in range(min(line, len(lines)) - 1, max(-1, line - 40), -1):
        if re.search(key, lines[i]):
            head = i
            break
    if head is not None:
        found = game_addresses(lines[head])
        j = head - 1
        while not found and j >= 0 and lines[j].strip().startswith("//"):
            found = game_addresses(lines[j])
            j -= 1
        if found:
            out.update(how="definition", addresses=sorted(set(found)))
            return out
    # The declaration: a header line naming the function in its class's body (or a free function's).
    bodies = sources.class_bodies().get(cls, []) if cls else []
    candidates = []
    for path, a, b in bodies:
        hl = sources.lines(path)
        for k in range(a, b + 1):
            if re.search(r"(?<![\w~])" + key if not name.startswith("~") else key, hl[k]):
                # (A declaration over several lines: its address may be on a later one, up to the ';'.)
                e = k
                while e < min(b, k + 6) and ";" not in hl[e].split("//")[0]:
                    e += 1
                candidates += game_addresses(" ".join(hl[k:e + 1]))
    if not cls:
        stem = os.path.splitext(file)[0] + ".h"
        for k, l in enumerate(sources.lines(stem)):
            if re.search(r"(?<!\w)" + key, l):
                candidates += game_addresses(l)
    if candidates:
        out.update(how="declaration", addresses=sorted(set(candidates)))
    return out


def map_functions(exe):
    """rva -> decorated symbol of every function of the port's own objects (as client_coverage.py)."""
    objs = set()
    for dp, dirs, fns in os.walk(SRC):
        dirs[:] = [d for d in dirs if d != "tier1"]
        objs |= {os.path.splitext(f)[0].lower() + ".obj" for f in fns if f.endswith(".cpp")}
    out = {}
    for l in open(exe[:-4] + ".map", encoding="latin-1"):
        m = re.match(r"\s*[0-9a-f]{4}:[0-9a-f]{8}\s+(\S+)\s+([0-9a-f]{8})\s+f\s+(?:i\s+)?(\S+)\s*$", l)
        if m and ":" not in m.group(3) and m.group(3).lower() in objs:
            out.setdefault(int(m.group(2), 16) - 0x400000, m.group(1))
    return out


def port_symbols(exe, base=0x400000):
    pdb = Pdb(exe, base)
    sources = Sources()
    return {rva: find(pdb, sources, rva, base, sym) for rva, sym in sorted(map_functions(exe).items())}


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--exe", default=os.path.join(tempfile.gettempdir(), "hd2_game_cov", "HD2_Port.exe"))
    ap.add_argument("--out")
    ap.add_argument("--show", type=int, default=0, help="print N functions without an address")
    a = ap.parse_args()
    table = port_symbols(a.exe)
    hows = collections.Counter(v["how"] or "none" for v in table.values())
    print("%d functions: %s" % (len(table), ", ".join("%s %d" % kv for kv in hows.most_common())))
    addrs = collections.Counter(x for v in table.values() for x in v["addresses"])
    print("%d distinct original addresses named" % len(addrs))
    if a.out:
        json.dump({"%08x" % k: v for k, v in table.items()}, open(a.out, "w"), indent=0)
    shown = 0
    for v in table.values():
        if not v["how"] and shown < a.show and not v["symbol"].startswith(("??_", "??__", "?__")) \
                and "std@@" not in v["symbol"]:
            print("  %s  %s:%s" % (v["name"], v["file"], v["line"]))
            shown += 1


if __name__ == "__main__":
    sys.exit(main())
