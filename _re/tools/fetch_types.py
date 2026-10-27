"""Fetch the layout of every structure in the root category of the Ghidra
program's data type manager into _re/ghidra_layouts.json.

The decompiler output refers to these structures (auto-recovered classes,
vtable structs ...); gen_tier1.py turns the layouts into C definitions for the
tier-1 type header, so the generated code compiles against exactly the types
Ghidra used.

Usage: python fetch_types.py [--workers N]
"""
import argparse
import concurrent.futures as cf
import json
import os
import urllib.parse
import urllib.request
import os as _os, sys as _sys
_sys.path.insert(0, _os.path.dirname(_os.path.abspath(__file__)))
import ghidra_program  # noqa: E402,F401  (adds program= to plugin requests)

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BASE = "http://127.0.0.1:8089"


def get(path):
    return json.load(urllib.request.urlopen(BASE + path, timeout=120))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--workers", type=int, default=6)
    args = ap.parse_args()

    types = get("/find_data_types?limit=0")["data_types"]
    names = sorted({t["name"] for t in types if t["kind"] == "struct" and t["category"] == "/"})

    def fetch(name):
        try:
            return name, get("/get_struct_layout?struct_name=" + urllib.parse.quote(name))
        except Exception as e:  # noqa: BLE001
            return name, {"error": str(e)}

    layouts = {}
    with cf.ThreadPoolExecutor(args.workers) as ex:
        for name, layout in ex.map(fetch, names):
            if "fields" in layout:
                layouts[name] = {"size": layout["size"], "fields": layout["fields"]}
    json.dump(layouts, open(os.path.join(ROOT, "ghidra_layouts.json"), "w"), indent=0)
    print(f"{len(layouts)} of {len(names)} structures")


if __name__ == "__main__":
    main()
