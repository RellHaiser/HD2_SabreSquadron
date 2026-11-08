"""Build the dedicated server, HD2DS_Port.exe, from the game's src/ with HD2_DEDICATED defined.

It uses _re/tools/build_game.py's compiler and linker flags (that script is left as it is). The differences:
- /DHD2_DEDICATED, and objects in %TEMP%\\hd2ds_game, so the client build's objects stay.
- The engine is LS3DRF.dll (ls3drf/, the engine without sound, input or video) instead of LS3DF.dll.
- The .cpp files in CLIENT_ONLY are not built. hd2ds_sabresquadron/src/*_ds.cpp stand in for them (stubs).
- No icon or title resource.

Usage: python hd2ds_sabresquadron/build.py [--all] [--link-only] [--keep-client] [--copy DIR] [--unresolved FILE]
    --keep-client     builds the CLIENT_ONLY files too, and not the stubs (to count what the link needs without them).
    --unresolved FILE writes the unresolved symbols of a failed link to FILE, one per line.
"""
import argparse
import glob
import os
import re
import shutil
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(os.path.dirname(HERE), "_re", "tools"))
from build_check import ROOT, SRC, collect                                   # noqa: E402
from build_game import COMMON, RELEASE, LINK, LAA, dll_lib, run_bat           # noqa: E402

# The game's .cpp files with no counterpart in HD2DS_SabreSquadron.exe: none of their client functions match a server function,
# and none pairs with a changed server function between the same matched neighbours (MEDIUM: only 6334 of the client's 11400
# functions are mapped to a src/ file, so a file is listed only when several of its functions are mapped). Human poses, stains,
# vehicle sounds / lamps / effects / damage visuals and the in-game interface.
# Built although listed by that measure: files whose other functions the server's code calls (human_pose_ik.cpp's pose math,
# input_controls.cpp's g_controls / WideToAnsi / render callback, speech_slots.cpp's LoadMaterialTables).
CLIENT_ONLY = [
    "game/car_effects.cpp",
    "game/human_pose_body.cpp", "game/human_pose_dead.cpp", "game/human_pose_hold.cpp",
    "game/human_pose_ragdoll.cpp", "game/human_pose_seats.cpp", "game/human_pose_weapon.cpp",
    "game/human_stains.cpp", "game/human_weapon_pose.cpp",
    "game/interface.cpp", "game/interface_inventory.cpp", "game/interface_map.cpp", "game/interface_overlays.cpp",
    "game/interface_selectors.cpp", "game/interface_signals.cpp",
    "game/vehicle_damage.cpp", "game/vehicle_effects.cpp", "game/vehicle_lamps.cpp", "game/vehicle_sounds.cpp",
]

LIBS = [os.path.join(ROOT, "ls3drf", "build", "msvc", "LS3DRF.lib"), dll_lib("rw_data", "RW_DATA", "rw_data.lib"),
        "kernel32.lib", "advapi32.lib", "gdi32.lib", "imagehlp.lib", "mpr.lib", "ole32.lib", "oleaut32.lib", "user32.lib", "winmm.lib",
        "wsock32.lib", "shell32.lib"]
DEFINES = ["/DHD2_DEDICATED"]


def sources(keep_client):
    files = collect([])
    if keep_client:
        return files
    skip = {os.path.normcase(os.path.join(SRC, *f.split("/"))) for f in CLIENT_ONLY}
    files = [f for f in files if os.path.normcase(f) not in skip]
    return files + sorted(glob.glob(os.path.join(HERE, "src", "*.cpp")))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--all", action="store_true")
    ap.add_argument("--link-only", action="store_true")
    ap.add_argument("--keep-client", action="store_true")
    ap.add_argument("--copy")
    ap.add_argument("--unresolved")
    ap.add_argument("--stack", default="0x100000")
    args = ap.parse_args()

    out = os.path.join(tempfile.gettempdir(), "hd2ds_game")
    os.makedirs(out, exist_ok=True)
    files = sources(args.keep_client)
    objname = lambda f: os.path.join(out, os.path.splitext(os.path.basename(f))[0] + ".obj")
    names = [objname(f) for f in files]
    if len(set(names)) != len(names):
        sys.exit("two sources share an object name")
    if not args.link_only:
        stale = [f for f in files if args.all or not os.path.exists(objname(f)) or os.path.getmtime(f) > os.path.getmtime(objname(f))]
        if stale:
            rsp = os.path.join(out, "files.rsp")
            with open(rsp, "w") as fh:
                fh.write("\n".join('"%s"' % f for f in stale))
            flags = COMMON + RELEASE + DEFINES
            r = run_bat(os.path.join(out, "build.bat"), ['cl %s /Fo"%s\\\\" @"%s"' % (" ".join(flags), out, rsp)])
            lines = [l for l in (r.stdout + r.stderr).splitlines() if ": error" in l or "fatal error" in l or ": warning" in l]
            for l in lines[:60]:
                print(l.replace(ROOT + "\\", ""))
            print("%d compiled, %d messages" % (len(stale), len(lines)))
            if r.returncode:
                print("compile failed")
                return 1
    known = set(names)
    for stale_obj in glob.glob(os.path.join(out, "*.obj")):
        if stale_obj not in known:
            os.remove(stale_obj)
    exe = os.path.join(out, "HD2DS_Port.exe")
    rsp = os.path.join(out, "link.rsp")
    with open(rsp, "w") as fh:
        fh.write("\n".join('"%s"' % o for o in sorted(known)) + "\n")
        fh.write("\n".join('"%s"' % l if "\\" in l else l for l in LIBS) + "\n")
    r = run_bat(os.path.join(out, "link.bat"), ['link %s %s /STACK:%s /OUT:"%s" /MAP:"%s" @"%s"' % (
        " ".join(LINK), LAA, args.stack, exe, exe[:-4] + ".map", rsp)])
    text = (r.stdout + r.stderr).splitlines()
    # (A C++ symbol is shown as "its declaration" (its decorated name); a C one as its decorated name.)
    unresolved = sorted({m.group(1) for l in text for m in [re.search(r'LNK20(?:19|01): unresolved external symbol ("[^"]*" \(\S+\)|\S+)', l)] if m})
    others = [l for l in text if "LNK" in l and "LNK4099" not in l and "LNK2019" not in l and "LNK2001" not in l]
    for l in others[:40]:
        print(l[:220])
    print("unresolved symbols", len(unresolved))
    if args.unresolved:
        with open(args.unresolved, "w") as fh:
            fh.write("\n".join(unresolved) + "\n")
    print("link return code", r.returncode, exe)
    if r.returncode:
        return 1
    if args.copy:
        shutil.copy2(exe, args.copy)
        shutil.copy2(exe[:-4] + ".pdb", args.copy)
        print("copied to", args.copy)
    return 0


if __name__ == "__main__":
    sys.exit(main())
