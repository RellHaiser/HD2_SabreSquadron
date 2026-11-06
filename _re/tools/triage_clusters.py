"""Second step of the triage: run after triage.py. Buckets its rows (dead, dead-eh?, static, tiny, live), marks what the hand-written source mentions, prints the clusters by address."""
import json, os, re, glob, bisect, collections, sys
os.chdir(r"G:\Programming\HD2 Source Port\C++")
sys.path.insert(0, "_re/tools")
rows = json.load(open("_re/triage.json"))
idx = {int(a, 16): r for a, r in json.load(open("_re/index.json")).items()}

# Addresses named in the hand-written source -> its file.
src_of = {}
named = collections.Counter()
text_all = {}
for f in glob.glob("src/**/*", recursive=True):
    if "tier1" in f or not f.endswith((".cpp", ".h")):
        continue
    t = open(f, encoding="utf-8", errors="replace").read()
    text_all[f] = t
    for m in re.findall(r"0x00([4-7][0-9a-f]{5})", t):
        src_of.setdefault(int(m, 16) + 0, os.path.relpath(f, "src").replace("\\", "/"))
blob = "\n".join(text_all.values())
fun_refs = set(int(x, 16) for x in re.findall(r"FUN_([0-9a-f]{8})", blob))
words = set(re.findall(r"[A-Za-z_][A-Za-z0-9_]*", blob))

ported_addrs = sorted(src_of)
def nearest_src(a):
    i = bisect.bisect_left(ported_addrs, a)
    c = []
    for j in (i - 1, i):
        if 0 <= j < len(ported_addrs):
            c.append((abs(ported_addrs[j] - a), src_of[ported_addrs[j]]))
    return min(c)[1] if c else "?"

def bucket(r):
    n = r["name"]
    a = int(r["address"], 16)
    if r["how"] == "dead":
        return "dead-eh?" if r["loose_refs"] else "dead"
    if n.startswith(("StaticInit", "StaticDestroy", "$E", "atexit")):
        return "static"
    if r["size"] < 16:
        return "tiny"
    return "live"

needs = 0
for r in rows:
    a = int(r["address"], 16)
    short = r["name"].split("::")[-1]
    r["bucket"] = bucket(r)
    r["src_uses"] = bool(a in fun_refs or a in src_of or (not r["name"].startswith("FUN_") and short in words and len(short) > 5))
    r["near"] = nearest_src(a)
json.dump(rows, open("_re/triage.json", "w"), indent=0)

c = collections.defaultdict(lambda: [0, 0])
for r in rows:
    c[r["bucket"]][0] += 1
    c[r["bucket"]][1] += r["size"]
print({k: tuple(v) for k, v in c.items()})
live = [r for r in rows if r["bucket"] == "live"]
print("live: named %d, FUN %d" % (sum(not r["name"].startswith("FUN_") for r in live), sum(r["name"].startswith("FUN_") for r in live)))
print("live and mentioned in hand source: %d functions %d bytes" % (sum(r["src_uses"] for r in live), sum(r["size"] for r in live if r["src_uses"])))
print("live, callers all ported (no todo caller): %d, %d bytes" % (sum(r["todo_callers"] == 0 for r in live), sum(r["size"] for r in live if r["todo_callers"] == 0)))

# Clusters: runs of live todo with gaps of at most 0x800.
live.sort(key=lambda r: r["address"])
clusters = []
for r in live:
    a = int(r["address"], 16)
    if clusters and a - clusters[-1]["end"] <= 0x800:
        cl = clusters[-1]
    else:
        cl = {"start": a, "end": a, "rows": []}
        clusters.append(cl)
    cl["rows"].append(r)
    cl["end"] = a + r["size"]
clusters.sort(key=lambda c: -sum(r["size"] for r in c["rows"]))
print("clusters", len(clusters))
for cl in clusters[:26]:
    rs = cl["rows"]
    b = sum(r["size"] for r in rs)
    near = collections.Counter(r["near"] for r in rs).most_common(1)[0][0]
    named_ = [r["name"] for r in sorted(rs, key=lambda r: -r["size"]) if not r["name"].startswith("FUN_")][:4]
    strs = []
    for r in rs:
        strs += [str(s)[:28] for s in r["strings"]]
    print("%06x-%06x %3d fn %6d B src %6d B near %-24s %s" % (cl["start"], cl["end"], len(rs), b, sum(r["size"] for r in rs if r["src_uses"]), near[:24], [n[:22] for n in named_]))
dead = [r for r in rows if r["bucket"] == "dead"]
dead.sort(key=lambda r: r["address"])
dc = []
for r in dead:
    a = int(r["address"], 16)
    if dc and a - dc[-1]["end"] <= 0x800:
        cl = dc[-1]
    else:
        cl = {"start": a, "end": a, "rows": []}
        dc.append(cl)
    cl["rows"].append(r)
    cl["end"] = a + r["size"]
dc.sort(key=lambda c: -sum(r["size"] for r in c["rows"]))
print("dead clusters")
for cl in dc[:14]:
    rs = cl["rows"]
    named_ = [r["name"] for r in sorted(rs, key=lambda r: -r["size"]) if not r["name"].startswith("FUN_")][:4]
    print("%06x-%06x %3d fn %6d B near %-28s %s" % (cl["start"], cl["end"], len(rs), sum(r["size"] for r in rs), rs[0]["near"][:28], [n[:24] for n in named_]))
