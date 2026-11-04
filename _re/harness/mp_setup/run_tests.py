"""Runs every check of the multiplayer set-up's harness; exit code 0 when all pass.

  python run_tests.py EXE DATA [--quiet]

DATA holds the map lists (<install>.txt: the install's own mpmaplist.txt, its loose file else its
archives'; the profiles whose rotations name maps a list lacks get them added: see PROFILES) and the
edge case of make_edge.py (DATA/edge). Every MPSetings.sav of PROFILES is loaded and saved again
(chunk 0x44D byte-identical); every list is read and compared with check_dump's own reading.
"""
import os
import shutil
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import check_dump  # noqa: E402
import check_roundtrip  # noqa: E402

# (profile, the map list it was saved with)
PROFILES = [
    ("H:/HD2_CMP2.4.8/PlayersProfiles/=RpR=RellHaiser/MPSetings.sav", "HD2_CMP2.4.8.txt"),
    ("H:/HD2_CMP2.4.8_NWMItemMod/PlayersProfiles/=RpR=RellHaiser/MPSetings.sav", "HD2_CMP2.4.8_NWMItemMod.txt"),
    ("H:/HD2_CurrentPlay/PlayersProfiles/=RpR=RellHaiser/MPSetings.sav", "HD2_CurrentPlay.txt"),
    ("H:/HD2_ItemMod_NWM/PlayersProfiles/=RpR=RellHaiser/MPSetings.sav", "HD2_ItemMod_NWM.txt"),
    ("H:/HD2_ItemMod_NWM/PlayersProfiles/Test/MPSetings.sav", "HD2_ItemMod_NWM_test.txt"),
    ("H:/HD2_Max_TextureSoundMod/PlayersProfiles/RellHaiser/MPSetings.sav", "HD2_Max_TextureSoundMod.txt"),
    ("H:/HD2_MGTest/PlayersProfiles/=RpR=RellHaiser/MPSetings.sav", "HD2_MGTest.txt"),
    ("H:/HD2_NWM_PAM/PlayersProfiles/=RpR=RellHaiser/MPSetings.sav", "HD2_NWM_PAM.txt"),
    ("H:/HD2_NWM_PAM/PlayersProfiles/Blank/MPSetings.sav", "HD2_NWM_PAM.txt"),
    ("H:/HD2_NWM_PAM/PlayersProfiles/Test/MPSetings.sav", "HD2_NWM_PAM.txt"),
    ("H:/HD2_NWM_w_HiPower/PlayersProfiles/Test/MPSetings.sav", "HD2_NWM_w_HiPower.txt"),
    ("H:/HD2_PackedCMP/PlayersProfiles/Test/MPSetings.sav", "HD2_PackedCMP.txt"),
    ("H:/HD2_RtCW/PlayersProfiles/=RpR=RellHaiser/MPSetings.sav", "HD2_RtCW.txt"),
    ("H:/HD2_Thorir/PlayersProfiles/=RpR=RellHaiser/MPSetings.sav", "HD2_Thorir.txt"),
]
DUMPS = ["ss.txt", "HD2_CMP2.4.8.txt", "HD2_ItemMod_NWM.txt", "HD2_PackedCMP.txt", "HD2_RtCW.txt", "HD2_Thorir.txt"]


def run(exe, cwd, *args):
    try:
        return subprocess.run([exe, *args], cwd=cwd, capture_output=True, text=True, timeout=30,
                              encoding="latin-1").stdout
    except subprocess.TimeoutExpired:
        return None


def main():
    exe, data = os.path.abspath(sys.argv[1]), sys.argv[2]
    quiet = "--quiet" in sys.argv
    work = os.path.join(os.path.dirname(exe), "run")
    os.makedirs(work, exist_ok=True)
    failed = []
    for profile, maplist in PROFILES:
        shutil.copy(os.path.join(data, maplist), os.path.join(work, "mpmaplist.txt"))
        out = os.path.join(work, "out.sav")
        if os.path.exists(out):
            os.remove(out)
        if run(exe, work, "roundtrip", profile, out) is None or not os.path.exists(out) or \
                check_roundtrip.chunk_44d(profile) != check_roundtrip.chunk_44d(out):
            failed.append("roundtrip " + profile)
    for maplist in DUMPS:
        shutil.copy(os.path.join(data, maplist), os.path.join(work, "mpmaplist.txt"))
        got = run(exe, work, "dump")
        want = check_dump.reference(open(os.path.join(data, maplist), encoding="latin-1").read())
        if got is None or [l for l in got.splitlines() if l.startswith(("type", "map", "  ", "rules", "rotation"))] != want or \
                not got.startswith("loaded 1 name 'H&D 2 SERVER' password '' port 11001 options 0 0 0 0 0\n"):
            failed.append("dump " + maplist)
    # The elements that end the list (make_edge.py's term*.txt and the counts of the first type).
    edge_dir = os.path.join(data, "edge")
    counts = open(os.path.join(edge_dir, "terms.txt")).read().splitlines()
    for k, want in enumerate(counts):
        shutil.copy(os.path.join(edge_dir, "term%d.txt" % k), os.path.join(work, "mpmaplist.txt"))
        got = run(exe, work, "dump")
        if got is None or want not in got.splitlines():
            failed.append("term%d" % k)
    # No list: nothing read, no defaults; only GameData\mpmaplist.txt: read; another root: no maps, the defaults.
    os.remove(os.path.join(work, "mpmaplist.txt"))
    os.makedirs(os.path.join(work, "GameData"), exist_ok=True)
    gamedata = os.path.join(work, "GameData", "mpmaplist.txt")
    if os.path.exists(gamedata):
        os.remove(gamedata)
    got = run(exe, work, "dump")
    if got is None or not got.startswith("loaded 0 name '' password '' port 0 options 0 0 0 0 0\ntype 0: 0 maps\n"):
        failed.append("no list")
    shutil.copy(os.path.join(edge_dir, "term0.txt"), gamedata)
    got = run(exe, work, "dump")
    if got is None or "type 0: 1 maps" not in got.splitlines():
        failed.append("GameData list")
    os.remove(gamedata)
    open(os.path.join(work, "mpmaplist.txt"), "w").write('<LIST><GAMESTYLE type="deathmatch"><MAP name="A" dir="a"/></GAMESTYLE></LIST>')
    got = run(exe, work, "dump")
    if got is None or not got.startswith("loaded 1 name 'H&D 2 SERVER' password '' port 11001 options 0 0 0 0 0\ntype 0: 0 maps\n"):
        failed.append("other root")
    for name in ("edge", "edge2"):
        shutil.copy(os.path.join(data, "edge", "mpmaplist.txt"), os.path.join(work, "mpmaplist.txt"))
        got = run(exe, work, "roundtrip", os.path.join(data, "edge", name + ".sav"), "out.sav")
        want = open(os.path.join(data, "edge", name + ".txt")).read().splitlines()
        if got is None or got.splitlines() != want:
            failed.append(name)
    total = len(PROFILES) + len(DUMPS) + len(counts) + 3 + 2
    if not quiet or failed:
        print("%d of %d checks pass%s" % (total - len(failed), total, "" if not failed else ": " + ", ".join(failed)))
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
