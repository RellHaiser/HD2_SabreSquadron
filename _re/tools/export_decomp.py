"""Bulk-export Ghidra decompilation for every relevant function to disk.

Talks directly to the GhidraMCP plugin HTTP server (default 127.0.0.1:8089) so the
decompiled text never has to pass through an interactive session.

Output: _re/decomp/<address>.json, one file per function. Resumable: functions
that already have a file are skipped unless --force is given.

Usage: python export_decomp.py [--workers N] [--force] [--only ADDR,ADDR,...]
"""
import argparse
import concurrent.futures as cf
import json
import os
import sys
import time
import urllib.parse
import urllib.request
import os as _os, sys as _sys
_sys.path.insert(0, _os.path.dirname(_os.path.abspath(__file__)))
import ghidra_program  # noqa: E402,F401  (adds program= to plugin requests)

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
RAW = os.path.join(ROOT, "raw")
OUT = os.path.join(ROOT, "decomp")
BASE = "http://127.0.0.1:8089"
FIELDS = "signature,decompiled_code,callees,callers,refs,body_end,plate_comment"

TEXT_LO, TEXT_HI = 0x401000, 0x80EFFF


def is_relevant(f):
    a = int(f["address"], 16)
    if not (TEXT_LO <= a <= TEXT_HI):
        return False  # .psbf is the copy-protection wrapper, not game code
    n = f["name"]
    if n.startswith(("Unwind@", "Catch@")):
        return False  # compiler-generated EH funclets
    return True


def fetch_batch(addrs):
    q = urllib.parse.urlencode({"functions": ",".join("0x" + a for a in addrs), "fields": FIELDS})
    for attempt in range(4):
        try:
            raw = urllib.request.urlopen(BASE + "/get_functions?" + q, timeout=600).read()
            return json.loads(raw)
        except Exception as e:  # noqa: BLE001 - retry any transport error
            err = e
            time.sleep(2 + attempt * 3)
    raise RuntimeError(f"batch failed {addrs[0]}..: {err}")


def save_batch(addrs):
    d = fetch_batch(addrs)
    funcs = d.get("functions", {})
    saved = 0
    for key, val in funcs.items():
        a = (val.get("address") or key.replace("0x", "")).lower().zfill(8)
        timed_out = val.get("decompile_failed") and "timeout" in (val.get("decompile_error") or "")
        if (val.get("truncation") or {}).get("decompiled_code") or timed_out:
            # get_functions caps the text at 120000 chars and gives up on big functions
            # (process: timeout, e.g. the script VM 0x00416e10); force_decompile does neither
            raw = urllib.request.urlopen(BASE + "/force_decompile?address=0x" + a, timeout=600).read()
            full = json.loads(raw).get("decompiled")
            if full:
                val["decompiled_code"] = full
                val.setdefault("truncation", {})["decompiled_code"] = False
                if timed_out:
                    val["decompile_failed"] = False
                    val.pop("decompile_error", None)
        with open(os.path.join(OUT, a + ".json"), "w", encoding="utf-8") as fh:
            json.dump(val, fh)
        saved += 1
    return saved


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--workers", type=int, default=4)
    ap.add_argument("--batch", type=int, default=10)
    ap.add_argument("--force", action="store_true")
    ap.add_argument("--only", default="")
    args = ap.parse_args()

    os.makedirs(OUT, exist_ok=True)
    if args.only:
        todo = [a.strip().replace("0x", "").lower().zfill(8) for a in args.only.split(",") if a.strip()]
    else:
        fs = json.load(open(os.path.join(RAW, "functions.json")))["functions"]
        todo = [f["address"].lower() for f in fs if is_relevant(f)]
    if not args.force:
        have = {n[:-5] for n in os.listdir(OUT) if n.endswith(".json")}
        todo = [a for a in todo if a not in have]
    print(f"{len(todo)} functions to export", flush=True)

    batches = [todo[i:i + args.batch] for i in range(0, len(todo), args.batch)]
    done = 0
    t0 = time.time()
    with cf.ThreadPoolExecutor(max_workers=args.workers) as ex:
        futs = {ex.submit(save_batch, b): b for b in batches}
        for fut in cf.as_completed(futs):
            try:
                done += fut.result()
            except Exception as e:  # noqa: BLE001
                print("ERROR", e, file=sys.stderr, flush=True)
            if done and done % 200 < args.batch:
                rate = done / (time.time() - t0)
                print(f"{done}/{len(todo)}  {rate:.1f}/s  eta {(len(todo) - done) / max(rate, 0.01) / 60:.1f} min", flush=True)
    print(f"done {done} in {(time.time() - t0) / 60:.1f} min", flush=True)


if __name__ == "__main__":
    main()
