"""Exports IJoy.dll's Ghidra state into _re/: functions (decompile, signature, callers,
callees), exports, imports, namespaces, strings, segments.

Usage: python export_all.py [--only-list] [--threads N]
"""
import argparse
import json
import os
import sys
from concurrent.futures import ThreadPoolExecutor

sys.path.insert(0, os.path.dirname(__file__))
import gh  # noqa: E402

RE = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def save(name, obj):
    with open(os.path.join(RE, name), "w", encoding="utf-8", newline="\n") as f:
        json.dump(obj, f, indent=1)


def fetch_batch(addrs):
    r = gh.get("get_functions", functions=",".join("0x" + a for a in addrs),
               fields="signature,decompiled_code,callees,callers,refs", timeout=900)
    if isinstance(r, str):
        raise RuntimeError(r[:300])
    return r


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--only-list", action="store_true")
    ap.add_argument("--threads", type=int, default=6)
    a = ap.parse_args()

    funcs = gh.get("find_functions", limit=0)["functions"]
    save("functions.json", funcs)
    for kind in ("segments", "exports", "imports", "namespaces"):
        save(kind + ".json", gh.items(kind))
    strings = gh.get("list_strings", limit=0)
    save("strings.json", strings)
    print(len(funcs), "functions")
    if a.only_list:
        return

    addrs = [f["address"] for f in funcs if not f.get("is_external")]
    batches = [addrs[i:i + 20] for i in range(0, len(addrs), 20)]
    out = {}
    with ThreadPoolExecutor(a.threads) as ex:
        for n, r in enumerate(ex.map(fetch_batch, batches)):
            res = r.get("functions", r.get("results", r))
            if isinstance(res, dict):
                res = list(res.values())
            for fn in res:
                key = fn.get("address") or fn.get("entry")
                out[key] = fn
            if n % 10 == 0:
                print(n * 20, "/", len(addrs), flush=True)
    save("decomp.json", out)
    print("decompiled", len(out))


if __name__ == "__main__":
    main()
