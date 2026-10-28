"""Coverage map of the three ports (the exe, LS3DF.dll, rw_data.dll) by subsystem and state.

States: verified (ported and covered by a differential test or a harness), ported (by hand, not
tested), pending (not hand-ported; tier 1 stands in), excluded (C runtime, STL, compiler-generated,
unreferenced). Writes the HD2 Port Coverage dashboard's data files: coverage.json, formats.json,
binaries.json (replace them in the dashboard's Sources panel), and prints a summary.

    python tools/coverage_map.py [--out DIR]     (default _re/coverage)

Hand-kept tables below: EXE_TESTED / LS3DF_MODULES (which sources a test covers), FORMATS (the
data files' weights and states). Update them as tests and formats land.
"""
import argparse
import bisect
import collections
import json
import os
import re

BASE = r"G:\Programming\HD2 Source Port"
EXE_RE = os.path.join(BASE, "C++", "_re")
EXE_SRC = os.path.join(BASE, "C++", "src")
LS3DF_RE = os.path.join(BASE, "LS3DF", "_re")
LS3DF_SRC = os.path.join(BASE, "LS3DF_Github", "src")
RW_README = os.path.join(BASE, "RW_DATA", "README.md")
GAME_LO, GAME_HI = 0x401000, 0x7E2000

EXE, LS3DF, RW = "HD2_SabreSquadron.exe", "LS3DF.dll", "rw_data.dll"
V, P, N, X = "verified", "ported", "pending", "excluded"
STATES = (V, P, N, X)

# ---------------------------------------------------------------- the exe

# Source file (src/<dir>/<stem>) -> subsystem; the first match wins.
EXE_SUBSYSTEMS = [
    ("AI & controllers", r"game/human_controller$"),
    ("Humans", r"game/(human|soldier|surface_marks)"),
    ("Vehicles", r"game/(vehicle|car|seat_weapon)"),
    ("AI & controllers", r"game/(ai_|squad_|enemy_|sniper_|scripted_controller|checkpoint|controller_|player_(controller|weapons|net|internal)|remote_controller|team)"),
    ("Actors & objects", r"game/(actor|actors_module|animation_actor|item_actor|debris|clock|null_actor|wire_cut|photo_place|switch_actor|mine|radio|document_place|ts_explosion|spawn_point|versions|melee_tracker|frame_box_volume|"
                         r"explosive_place|ladder|pike|fowl_generator|mp_zone|submarine|train|static_weapon|bike|fish|door|supply_box|barrel)"),
    ("Weapons & items", r"game/(shots|items|item_|heavy_fire_support)"),
    ("Physics & collision", r"game/(physics|collision|rigid_body|world)"),
    ("Interface & HUD", r"game/(interface|first_person_view|text_box)"),
    ("Scripting", r"script/"),
    ("Profile & campaign", r"game/(profile|player_profile|campaign|key_bindings)"),
    ("Sound & effects", r"game/(sound_|particles)"),
    ("Network", r"net/"),
    ("App & core", r"(common/|ls3d/|game/(app|game|module|console|resource_cache|track|perf_timer|pending))"),
]
# Source files a harness runs (_re/harness, _re/PROGRESS.md).
EXE_TESTED = re.compile(r"game/(vehicle|car|seat_weapon|melee_tracker_route|rigid_body|debris|item_actor|animation_actor|"
                        r"clock|null_actor|wire_cut|photo_place|switch_actor|mine|radio|document_place|ts_explosion|spawn_point|"
                        r"explosive_place|ladder|pike|fowl_generator|mp_zone|submarine|train|track|static_weapon|bike|fish|"
                        r"door|supply_box|barrel|"
                        r"checkpoint|profile|player_profile|key_bindings|campaign|soldier)|common/(config_file|os_files)")
EXE_TEST_NOTES = {
    "Vehicles": "vehicle_params harness (205 shipped car blocks re-pack byte-identical), vehicle_setup harness on the 37 shipped car models, car_tables harness; mutation-tested",
    "Actors & objects": "vehicle_setup harness builds for debris, item, animation, simple, objective, world, moving, seat and prop actors; mutation-tested",
    "Profile & campaign": "profile harness round trips (HumanStore.def, profile.sav, Options.bin, game.cfg, key bindings); Gamedata .gdt re-saved byte-identical",
    "AI & controllers": "checkpoints harness: every mission's check2.bin (73)",
    "Physics & collision": "rigid body builds of the vehicle harness; mutation-tested",
    "App & core": "profile harness (config files, file wrappers)",
}
# Unported code: subsystem from its file_map.txt range's classes and strings, then address bands.
EXE_RANGE_RULES = [
    ("Network", r"server|Server|kicked|CD Key|maxclients|ADMIN|TEAM>|voicechat|C_NetMessage|C_CommandProcessor|challenge"),
    ("Actors & objects", r"C_(Pike|FowlGenerator|Train|Bike|Fish|AirAttack|Ladder|MPZone|Junkers88|SupplyBox|Debris|Mine|Door|StaticWeapon|Barrel|Radio|PhotoPlace|ExplosivePlace|DocumentPlace|TSExplosion|SpawnPoint|Switch|WireCut|AnimationActor|ItemActor)"),
    ("Mission & world modules", r"GEffects|C_VertexAnimModule|LMapsGrp|LightMapGroups|decorations\.bin|decorator_sl|volumetrics|meshplayer|scene2\.bin|tree\.klz|check\.bin|C_LoaderScreen|C_CameraModule|C_SightModule|clouder|lensflare"),
]
EXE_BANDS = [
    (0x614000, 0x697000, "Menus & front end"),
    (0x720000, 0x748000, "Network"),
]


def nearest(sorted_list, a, lo=None, hi=None):
    i = bisect.bisect_left(sorted_list, a)
    c = [sorted_list[j] for j in (i - 1, i) if 0 <= j < len(sorted_list)]
    if lo is not None:
        c = [b for b in c if lo <= b <= hi]
    return min(c, key=lambda b: abs(b - a)) if c else None


def exe_rows():
    idx = {int(a, 16): r for a, r in json.load(open(os.path.join(EXE_RE, "index.json"))).items()}

    # ported.txt as port_stats.py reads it.
    hand, absorbed = set(), set()
    section_absorbed = False
    for ln in open(os.path.join(EXE_RE, "ported.txt")):
        ln = ln.strip()
        if not ln:
            continue
        if ln.startswith("#"):
            section_absorbed = ln.lower().startswith("# absorbed helpers")
            continue
        (absorbed if section_absorbed else hand).add(int(ln, 16))
    absorbed -= hand
    idioms = set()
    for ln in open(os.path.join(EXE_RE, "names", "zz_idioms.txt")):
        m = re.match(r"([0-9a-fA-F]{8})\s", ln)
        if m:
            idioms.add(int(m.group(1), 16))

    # Address -> source file, from the `// 0x00XXXXXX` comments (a .cpp before its header).
    addr_file = {}
    for d in ("common", "ls3d", "net", "script", "game"):
        for root, _, fs in os.walk(os.path.join(EXE_SRC, d)):
            for f in sorted(fs, key=lambda n: (not n.endswith(".cpp"), n)):
                rel = d + "/" + re.sub(r"\.(h|cpp|inc)$", "", f)
                text = open(os.path.join(root, f), encoding="utf-8", errors="replace").read()
                for m in re.finditer(r"0x(00[4-7][0-9a-fA-F]{5})\b", text):
                    addr_file.setdefault(int(m.group(1), 16), rel)

    def file_subsystem(rel):
        for name, pat in EXE_SUBSYSTEMS:
            if re.match(pat, rel):
                return name
        return None

    ranges = []
    for ln in open(os.path.join(EXE_RE, "file_map.txt")):
        m = re.match(r"([0-9a-f]{8})-([0-9a-f]{8}).*?cls=(\S*) :: (.*)", ln)
        if m:
            ranges.append((int(m.group(1), 16), int(m.group(2), 16), m.group(3) + " " + m.group(4)))
    range_lo = [r[0] for r in ranges]

    def range_of(a):
        i = bisect.bisect_right(range_lo, a) - 1
        return i if i >= 0 and ranges[i][0] <= a <= ranges[i][1] else None

    known = {a: file_subsystem(f) for a, f in addr_file.items()
             if GAME_LO <= a < GAME_HI and a in idx and file_subsystem(f)}
    known_sorted = sorted(known)
    file_sorted = sorted(a for a in addr_file if GAME_LO <= a < GAME_HI)

    def subsystem(a):
        if a in known:
            return known[a]
        ri = range_of(a)
        if ri is not None:
            for name, pat in EXE_RANGE_RULES:
                if re.search(pat, ranges[ri][2]):
                    return name
        for lo, hi, name in EXE_BANDS:
            if lo <= a < hi:
                return name
        if ri is not None:
            b = nearest(known_sorted, a, ranges[ri][0], ranges[ri][1])
            if b is not None:
                return known[b]
        return known[nearest(known_sorted, a)]

    out = collections.defaultdict(lambda: [0, 0])
    for a, r in idx.items():
        name = r.get("name") or ""
        if not (GAME_LO <= a < GAME_HI) or name.startswith(("Unwind@", "Catch@")):
            group, state = "C runtime & library", X
        elif a in hand or a in absorbed:
            # A ported function without an address comment of its own goes with the nearest one's file.
            f = addr_file.get(a) or addr_file[nearest(file_sorted, a)]
            group, state = file_subsystem(f) or subsystem(a), V if EXE_TESTED.match(f) else P
        elif a in idioms or name.startswith("empty_function"):
            group, state = "STL & compiler idioms", X
        else:
            group, state = subsystem(a), N
        out[group, state][0] += 1
        out[group, state][1] += r.get("size", 0)

    rows = []
    for (group, state), (n, b) in out.items():
        if state == V:
            ev = EXE_TEST_NOTES[group]
        elif state == P:
            ev = "Hand-ported; compile-checked file by file and read against the assembly; no linked build yet"
        elif state == N:
            ev = "Not hand-ported; the generated tier-1 translation stands in"
        elif group == "C runtime & library":
            ev = "MSVC runtime and libraries outside the game's code range, and exception-unwind funclets"
        else:
            ev = "MSVC 6 STL instantiations and empty functions (stl_idioms.py); the port uses the standard library"
        rows.append(row(EXE, group, state, n, b, ev))
    return rows

# ---------------------------------------------------------------- LS3DF.dll

# Source file -> module and the test that compares it with the original (None: not compared).
LS3DF_MODULES = [
    ("Textures & images", r"igraph/resize_image", "compare_resize"),
    ("Scene graph", r"i3d/(frame|visual|user|dummy|joint|volume|target|camera|light|sector|portal|occluder|area|model|anim|driver_frames)\.", "compare_frame"),
    ("Materials", r"i3d/material", "compare_material"),
    ("Meshes & buffers", r"i3d/(mesh|ib_manager|vb_manager|driver_factories)|core/multiset", "compare_mesh, compare_buffers"),
    ("Collision", r"i3d/collision_", "compare_collision"),
    ("Lightmaps", r"i3d/lightmap_build", "compare_lightmap"),
    ("Lightmaps", r"i3d/lightmap\.", None),
    ("Loaders", r"i3d/load", "compare_loader"),
    ("Weather", r"i3d/scene_weather", "compare_weather"),
    ("Scene", r"i3d/scene\.", None),
    ("Renderer", r"i3d/(render|portal_render|proj_geometry|driver_draw|driver_core|driver\.|i3d\.)", None),
    ("Visuals", r"i3d/(object|litobject|singlemesh|morph|billboard|lens|projector|mirror|emitter|shadow|sound)\.", None),
    ("IGraph input", r"igraph/igraph_input", "compare_input"),
    ("IGraph device & window", r"igraph/(igraph\.|igraph_state|igraph_image)", None),
    ("Textures & images", r"igraph/(itexture|image_load|image_convert|texture_stream)", "compare_texture"),
    ("DXT encoder", r"igraph/(s3tc|texture_compress)", "compare_s3tc"),
    ("Video (IShow)", r"igraph/ishow|video/", "compare_show"),
    ("DirectShow base", r"dshow/", "compare_show"),
    ("Sound (ISND)", r"isnd/", "compare_isnd"),
    ("Math", r"math/", "compare_math"),
    ("DXGetErrorString8", r"dx/dxerr8", "compare_dxerr8"),
    ("Engine core", r"core/", None),
]
LS3DF_RESIZE = "10070160"  # IGraph::ResizeImage: igraph_image.cpp's wrapper, body in resize_image.cpp


def ls3df_rows():
    idx = json.load(open(os.path.join(LS3DF_RE, "index.json")))
    addr_files = {}
    for root, _, fs in os.walk(LS3DF_SRC):
        for f in fs:
            p = os.path.join(root, f)
            rel = os.path.relpath(p, LS3DF_SRC).replace(os.sep, "/")
            for m in re.finditer(r"0x(10[0-9a-fA-F]{6})\b", open(p, encoding="utf-8", errors="replace").read()):
                addr_files.setdefault(m.group(1).lower(), set()).add(rel)

    def home(a):
        if a == LS3DF_RESIZE:
            return "igraph/resize_image.cpp"
        files = sorted(addr_files[a])
        return ([f for f in files if f.endswith(".cpp")] or files)[0]

    def module(f):
        for name, pat, test in LS3DF_MODULES:
            if re.match(pat, f):
                return name, test
        raise KeyError(f"LS3DF_MODULES has no module for {f}")

    hits = sorted((a for a in idx if a in addr_files), key=lambda a: int(a, 16))
    hit_values = [int(a, 16) for a in hits]
    out = collections.defaultdict(lambda: [0, 0])
    tests = collections.defaultdict(set)
    for a, r in idx.items():
        if r["region"] in ("crt", "import"):
            group, state = "C runtime", X
        elif r["region"] == "eh":
            group, state = "Compiler-generated (EH)", X
        elif a not in addr_files and not r["callers"] and not r.get("vslots"):
            group, state = "Unreferenced", X
        else:
            # Templates and inlined helpers without an address comment go with the nearest ported function.
            f = home(a if a in addr_files else hits[hit_values.index(nearest(hit_values, int(a, 16)))])
            group, test = module(f)
            state = V if test else P
            if test:
                tests[group].update(t.strip() for t in test.split(","))
        out[group, state][0] += 1
        out[group, state][1] += r.get("size", 0)

    excluded_notes = {
        "C runtime": "MSVC 6 C runtime and import thunks; the port links the current CRT",
        "Compiler-generated (EH)": "Exception-unwind funclets",
        "Unreferenced": "No callers and no virtual-table slot",
    }
    rows = []
    for (group, state), (n, b) in out.items():
        if state == V:
            ev = "Differential test against the original DLL: " + ", ".join(sorted(tests[group]))
        elif state == P:
            ev = "Ported; part of the drop-in DLL that ran the game; not compared with the original"
        else:
            ev = excluded_notes[group]
        rows.append(row(LS3DF, group, state, n, b, ev))
    return rows

# ---------------------------------------------------------------- rw_data.dll


def rw_rows():
    # The README's function map lists the ported functions; its note counts the rest.
    text = open(RW_README, encoding="utf-8").read()
    mapped = len(re.findall(r"^\| `0x1000[0-9a-f]{4}` \|", text, re.M))
    vectors = int(re.search(r"(\d+) `std::vector` instantiations", text).group(1))
    inits = int(re.search(r"the (\d+) static initialisers", text).group(1))
    test = "compare.cpp against the original: all 11 archives, 64,569 files, 6.4 GB, about 3.35 million checks, 0 failures"
    return [
        row(RW, "Archive API & codecs", V, mapped, None, test),
        row(RW, "Static initialisers", V, inits, None, test),
        row(RW, "std::vector instantiations", X, vectors, None, "Replaced by the standard library"),
    ]

# ---------------------------------------------------------------- binaries and formats

BINARIES = [
    {"binary": EXE, "port": "C++ (HD2_SabreSquadron)", "status": "Hand port, compiles file by file; not linked yet, generated tier-1 code fills the gaps"},
    {"binary": LS3DF, "port": "LS3DF_Github", "status": "Drop-in DLL; ran the menus, two co-op missions, music and movies"},
    {"binary": RW, "port": "RW_DATA_Github", "status": "Drop-in DLL; every function of the original's own code verified"},
]

# (group, item, weight, state, evidence). Weights are by hand: how much of the game rests on each.
FORMATS = [
    (".dta archives", "Mount & lookup", 3, V, "compare.cpp: all 11 archives (ISD0 and ISD1), 64,569 files read whole and with random seeks"),
    (".dta archives", "Keys & cipher", 2, V, "compare.cpp: the two-step key and GetKey's masked values match the original"),
    (".dta archives", "LZSS blocks", 2, V, "compare.cpp: codec against the original's code (3,000 random rounds) and every compressed file"),
    (".dta archives", "WAV blocks", 2, V, "compare.cpp: 16-bit DPCM decoder against the original's code and every compressed WAV"),
    (".dta archives", "Writing", 1, V, "compare.cpp: every dtaOpenWrite flag combination, .tmp write-through, dtaBin2Text"),
    (".dta archives", "File logs", 1, V, "compare.cpp: file logs, including the slot-reuse quirk"),
    (".dta archives", "FQL", 1, V, "compare.cpp: recordings compared byte for byte, then played back"),
    ("Assets", ".4ds models", 4, V, "compare_loader: all 5,946 models, object graphs compared member by member"),
    ("Assets", "Scenes & maps", 3, V, "compare_loader: 244 mission files (82 scenes, maps, loading screens)"),
    ("Assets", ".6ds shadows", 1, V, "compare_loader: all 10 shadow files"),
    ("Assets", ".5ds anims", 2, P, "Loader ported (OpenAnimationFile, animation sets); the game's .5ds files are not in compare_loader's list"),
    ("Assets", "BMP/TGA/DDS", 2, V, "compare_texture: 6,000 random cases"),
    ("Assets", "DXT", 1, V, "compare_s3tc: 23,000 images, 373,000 blocks"),
    ("Assets", "Ogg music", 1, V, "compare_show: the game's music"),
    ("Assets", "Bink/AVI", 1, V, "compare_show: the game's movies, 24 AVI playbacks"),
    ("Mission files", "actors.bin", 3, P, "Car blocks verified (205 of 205 re-pack byte-identical); the other actor types ported, not checked against the files"),
    ("Mission files", "Scripts", 3, P, "Compiler, expressions and interpreter ported; scripting reference written; not run over the shipped scripts"),
    ("Mission files", "check2.bin", 2, V, "checkpoints harness: loads all 73 missions' networks"),
    ("Mission files", "scene2.bin", 2, N, "Mission loader not ported"),
    ("Mission files", "items.dat", 1, P, "Load and save ported"),
    ("Mission files", "effects.bin", 1, P, "Static emitters ported (v1/v2)"),
    ("Mission files", "tracks.dat", 1, P, "Track module ported"),
    ("Mission files", "tree.klz", 1, N, "Not ported"),
    ("Mission files", "decorations", 1, N, "decorations.bin and decorator_sl.bin: not ported"),
    ("Mission files", "volumetrics", 1, N, "volumetrics.bin: not ported"),
    ("Mission files", "meshplayer", 1, N, "meshplayer.bin: not ported"),
    ("Mission files", "Vertanim", 1, N, "Vertanim.def / .bin: vertex animation module not ported"),
    ("Tables", "items.sav", 2, P, "Item definitions ported (10 component classes)"),
    ("Tables", "HumanStore.def", 1, V, "The shipped roster (65 soldiers) loads and writes back byte-identical"),
    ("Tables", "tyres.bin, materials", 1, V, "car_tables harness on the shipped tyres.bin and hdmaterials.def"),
    ("Tables", "anim.sav", 1, P, "Human movement anim tables ported"),
    ("Tables", "effects.def", 1, P, "Particle definitions ported"),
    ("Tables", "gamecam.set", 1, N, "Camera module not ported"),
    ("Tables", "Dialogs.bin", 1, N, "Text tables not ported"),
    ("Saves & profiles", "Gamedata .gdt", 1, V, "Campaign files load and re-save byte-identical"),
    ("Saves & profiles", "profile.sav, game.cfg", 2, V, "profile harness: save/load round trips of profile.sav, Options.bin, game.cfg and the profile's parts"),
    ("Saves & profiles", "Saved games", 3, P, "Most actors' chunks ported (vehicles' verified in the harness); some modules still write placeholders"),
]


def row(binary, group, state, n, b, evidence):
    return {"binary": binary, "group": group, "state": state, "functions": n, "bytes": b, "evidence": evidence}


def pct(a, b):
    return f"{100 * a / b:5.1f} %" if b else "    -"


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--out", default=os.path.join(EXE_RE, "coverage"), help="folder for the three JSON files")
    args = ap.parse_args()

    rows = exe_rows() + ls3df_rows() + rw_rows()
    order = {s: i for i, s in enumerate(STATES)}
    rows.sort(key=lambda r: ([b["binary"] for b in BINARIES].index(r["binary"]), r["group"], order[r["state"]]))
    formats = [{"group": g, "item": i, "weight": w, "state": s, "evidence": e} for g, i, w, s, e in FORMATS]

    os.makedirs(args.out, exist_ok=True)
    for name, data in (("coverage.json", rows), ("formats.json", formats), ("binaries.json", BINARIES)):
        json.dump(data, open(os.path.join(args.out, name), "w"), indent=1)

    # Summary: shares of the functions that need porting (all but excluded), as the dashboard shows them.
    total = collections.Counter()
    for b in BINARIES:
        c = collections.Counter()
        for r in rows:
            if r["binary"] == b["binary"]:
                c[r["state"]] += r["functions"]
        total.update(c)
        to_port = c[V] + c[P] + c[N]
        print(f"{b['binary']:22} {sum(c.values()):6} functions  ported {pct(c[V] + c[P], to_port)}"
              f"  verified {pct(c[V], to_port)}  ({c[V]} verified, {c[P]} ported, {c[N]} pending, {c[X]} excluded)")
    to_port = total[V] + total[P] + total[N]
    all_functions = sum(total.values())
    print(f"{'all':22} {all_functions:6} functions  ported {pct(total[V] + total[P], to_port)}"
          f"  verified {pct(total[V], to_port)}  (over all functions: {pct(total[V] + total[P], all_functions)})")
    weight = collections.Counter()
    for f in formats:
        weight[f["state"]] += f["weight"]
    w = sum(weight.values())
    print(f"file formats: {pct(weight[V] + weight[P] / 2, w)} weighted"
          f"  ({pct(weight[V], w)} verified, {pct(weight[P], w)} ported)")
    print("wrote", ", ".join(os.path.join(args.out, n) for n in ("coverage.json", "formats.json", "binaries.json")))


if __name__ == "__main__":
    main()
