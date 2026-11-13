"""Builds and runs every harness suite under _re/harness and writes a baseline report.

    python _re/harness/run_suites.py [--work DIR] [--data DIR] [--jobs N] [--list] [NAME ...]

NAME: suite names or fnmatch patterns (default: all). --work: the inputs, logs and report
(default %TEMP%\\hd2_suites). --data: the unpacked game (default H:\\HD2_Unpacked; env HD2_UNPACKED).
--mp-data: mp_setup's map lists and profiles (not part of the game data; skipped without it).
--coverage: also measure the production sources' lines, functions and branches with gcov (MinGW
suites; crash.msvc is MSVC and not measured). Each build script runs from a temporary copy whose g++
line has `--coverage -dumpdir <notes>` added; every run (each vehicle chunk apart) writes its .gcda
to its own folder through GCOV_PREFIX; gcov reads each folder, and the counts of every source under
src/ (not the tests' own files) are added up per suite and over all suites: coverage/coverage.md,
coverage/coverage.json.gz in the work folder.

Each suite ends in one status:
  pass                  built from the current sources, ran, and every check passed
  fail                  ran, and a check failed (or a checker could not read the output)
  build-failed          the executable was not produced (it is deleted first: a stale one never runs)
  timeout               a run passed its time limit (killed)
  crashed               a run exited non-zero without a parsable result
  missing-prerequisite  a compiler, game data or input set is not there (named in the report)

The report (report.json, report.md in the work folder) records the source revision, uncommitted
changes, the compilers, the data folder, and per suite: the status, the counts parsed from its
output, the time, the executable's hash and the log. Output is bounded (the tail of each run is
kept) so a runaway test cannot exhaust memory.

The manifest (SUITES below) is the inventory of the harnesses: build script, executable, inputs,
how a result is judged. Add an entry with every new build script; `--list` prints it.
"""
import argparse
import concurrent.futures
import datetime
import fnmatch
import glob
import hashlib
import json
import os
import re
import shutil
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, "..", ".."))
TEMP = os.environ.get("TEMP", os.environ.get("TMP", r"C:\Temp"))
MINGW = r"C:\nim-2.2.6\dist\mingw32\bin\g++.exe"
VCVARS = r"T:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars32.bat"
TAIL = 256 * 1024

CARS, MISSIONS = "cars", "missions"
# The equipment suite's save made with the original game (not in the repository): env EQUIPMENT_SAVE, else this.
EQUIPMENT_SAVE = os.environ.get("EQUIPMENT_SAVE", os.path.join(TEMP, "eq_saves", "original_SMCAM_quick.sav"))
# The items suite's table: the items.sav the original game read when that save was made (env ITEMS_SAV).
ITEMS_SAV = os.environ.get("ITEMS_SAV", r"G:\Hidden & Dangerous 2\Tables\items.sav")
# The original game executable, unpacked (the collision suites call its functions: collision/original_exe.h).
ORIGINAL_EXE = os.environ.get("HD2_ORIGINAL_EXE", r"H:\Other HD2 Files\HD2_SabreSquadron_unpacked.exe")
FACTORY_CHECKS = ["campaignscreens", "description", "equipmentdesc", "equipmentpanel", "equipmentscreen", "factory",
                  "gamemenu", "herostats", "loadout", "mainmenu", "menu", "menurun", "menusoldier", "mpsettings",
                  "options", "player", "replication", "screen", "storelist"]


def suite(name, bat, exe, kind, needs=(), **kw):
    return dict(name=name, bat=bat, exe=exe, kind=kind, needs=list(needs), **kw)


def V(name, bat, exe, inputs, checks, needs=(), once=False, timeout=1800):
    """A vehicle_setup build: build only, then run on an input set in chunks and check each output."""
    return suite(name, "vehicle_setup/" + bat, "vehicle_setup/" + exe, "inputs", ["mingw", "data", inputs, *needs],
                 inputs=inputs, checks=["vehicle_setup/" + c for c in checks], once=once, timeout=timeout)


def T(name, bat, exe, summary=r"(\d+) checks, (\d+) failures", needs=("mingw",), timeout=600, **kw):
    """A build script that builds and runs one test executable printing a summary line."""
    return suite(name, bat, exe, "selfrun", needs, summary=summary, timeout=timeout, **kw)


TMP = "%TEMP%"
SUITES = [
    # The vehicle harness (vehicle_setup/harness.cpp) in its builds, plus the two standalone tests there.
    V("vehicle", "build.bat", "harness.exe", CARS, ["check.py"]),
    V("vehicle.seat_weapon", "build_sw.bat", "harness_sw.exe", CARS, ["check_sw.py"]),
    V("vehicle.route", "build_route.bat", "harness_route.exe", CARS, ["check_route.py"]),
    V("vehicle.rigid", "build_rigid.bat", "harness_rigid.exe", CARS, ["check_rigid.py"]),
    V("vehicle.rigid_step", "build_rigid_step.bat", "harness_rigid_step.exe", CARS, ["check_rigid_step.py"]),
    V("vehicle.item", "build_item.bat", "harness_item.exe", CARS, ["check_item.py"]),
    V("vehicle.world", "build_world.bat", "harness_world.exe", MISSIONS, ["check_world.py"]),
    V("vehicle.camera", "build_camera.bat", "harness_camera.exe", MISSIONS, ["check_camera.py"]),
    V("vehicle.text", "build_text.bat", "harness_text.exe", MISSIONS, ["check_text.py"]),
    V("vehicle.factory", "build_factory.bat", "harness_factory.exe", MISSIONS,
      ["check_%s.py" % c for c in FACTORY_CHECKS]),
    V("vehicle.cursor", "build_cursor.bat", "harness_cursor.exe", MISSIONS, ["check_cursor.py"], once=True),
    V("vehicle.game_texts", "build_texts.bat", "harness_texts.exe", MISSIONS, ["check_gametexts.py"], once=True),
    # Builds that run their own test.
    T("net.replication", "net/build.bat", TMP + r"\net_test.exe", tree_copy="net/net_test.exe"),
    T("net.session", "net/build_session.bat", TMP + r"\netsession_test.exe", tree_copy="net/netsession_test.exe"),
    T("net.messages", "net/build_messages.bat", TMP + r"\netmessages_test.exe", tree_copy="net/netmessages_test.exe"),
    T("net.commands", "net/build_commands.bat", TMP + r"\netcommands_test.exe", tree_copy="net/netcommands_test.exe"),
    T("net.game", "net/build_game.bat", TMP + r"\netgame_test.exe", tree_copy="net/netgame_test.exe"),
    T("net.console", "net/build_console.bat", TMP + r"\console_test.exe", tree_copy="net/console_test.exe"),
    T("net.firewall", "net/build_firewall.bat", TMP + r"\firewall_test.exe", tree_copy="net/firewall_test.exe"),
    T("net.gamespy", "net/build_gamespy.bat", TMP + r"\gamespy_test.exe", tree_copy="net/gamespy_test.exe"),
    T("script_module", "script_module/build.bat", TMP + r"\script_module_test.exe",
      tree_copy="script_module/script_module_test.exe"),
    T("controls", "script_module/build_controls.bat", TMP + r"\controls_test.exe",
      tree_copy="script_module/controls_test.exe"),
    T("crash", "script_module/build_crash.bat", TMP + r"\crash_test.exe"),
    T("crash.msvc", "script_module/build_crash_msvc.bat", TMP + r"\crash_msvc\crash_test.exe", needs=("msvc",)),
    T("crowd", "script_module/build_crowd.bat", TMP + r"\crowd_test.exe"),
    T("explosion", "script_module/build_explosion.bat", TMP + r"\explosion_test.exe"),
    T("formation", "script_module/build_formation.bat", TMP + r"\formation_test.exe"),
    T("protection", "script_module/build_protection.bat", TMP + r"\protection_test.exe",
      tree_copy="script_module/protection_test.exe"),
    T("quick_messages", "script_module/build_quick_messages.bat", TMP + r"\quick_messages_test.exe"),
    T("speech", "script_module/build_speech.bat", TMP + r"\speech_test.exe", tree_copy="script_module/speech_test.exe"),
    T("stains", "script_module/build_stains.bat", TMP + r"\stains_test.exe"),
    T("shots", "shots/build.bat", TMP + r"\shots_test.exe"),
    # (Its save round trip reads equipment chunks from a quick save of the original: EQUIPMENT_SAVE above.)
    T("equipment", "equipment/build.bat", TMP + r"\equipment_test.exe", needs=("mingw", "equipment_save")),
    T("items", "items/build.bat", TMP + r"\items_test.exe", needs=("mingw", "equipment_save", "items_sav")),
    # (The human's side of the items: items_test.cpp built with HUMAN_ITEMS_TEST, game/human_items.cpp linked.)
    T("items.human", "items/build_human.bat", TMP + r"\human_items_test.exe", needs=("mingw", "equipment_save", "items_sav")),
    T("actors", "actors/build.bat", TMP + r"\actors_test.exe", needs=("mingw",)),
    # (The levels' collision files: MISSIONS_DIR, the unpacked missions; the sweeps are also compared with the
    # original executable's, HD2_ORIGINAL_EXE, and the game's DLLs, HD2_GAME_DIR: without them, with my own only.)
    T("collision", "collision/build.bat", TMP + r"\collision_test.exe", needs=("mingw", "missions")),
    # (The sphere sweeps against one shape, against the original's functions.)
    T("collision.sweepdiff", "collision/sweep_diff.bat", TMP + r"\sweep_diff.exe", needs=("mingw", "original_exe")),
    # (Physics objects on the real collision world; their steps also against the original executable's.)
    T("physics", "physics/build.bat", TMP + r"\physics_test.exe", needs=("mingw",)),
    # (The human's movement, every call against the original executable's.)
    T("human", "human/build.bat", TMP + r"\human_test.exe", needs=("mingw", "original_exe")),
    # (The movement base and the ragdoll, the same way: human/build_base.bat, HUMAN_BASE.)
    T("human.base", "human/build_base.bat", TMP + r"\human_base_test.exe", needs=("mingw", "original_exe")),
    # (The movement itself, game/human_movement.cpp: human/build_move.bat, HUMAN_MOVE.)
    T("human.move", "human/build_move.bat", TMP + r"\human_move_test.exe", needs=("mingw", "original_exe")),
    # (The animation mixer, game/human_anim_mixer.cpp: human/build_mixer.bat, HUMAN_MIXER.)
    T("human.mixer", "human/build_mixer.bat", TMP + r"\human_mixer_test.exe", needs=("mingw", "original_exe")),
    # (The animation player, game/human_anim_player.cpp through player_statics.cpp: human/build_player.bat, HUMAN_PLAYER.)
    T("human.player", "human/build_player.bat", TMP + r"\human_player_test.exe", needs=("mingw", "original_exe")),
    # (The special moves, game/human_special_moves.cpp: human/build_special.bat, HUMAN_SPECIAL.)
    T("human.special", "human/build_special.bat", TMP + r"\human_special_test.exe", needs=("mingw", "original_exe")),
    # (The idle and the interactions, game/human_idle.cpp: human/build_idle.bat, HUMAN_IDLE.)
    T("human.idle", "human/build_idle.bat", TMP + r"\human_idle_test.exe", needs=("mingw", "original_exe")),
    # (The visual, game/human_visual.cpp: human/build_visual.bat, HUMAN_VISUAL.)
    T("human.visual", "human/build_visual.bat", TMP + r"\human_visual_test.exe", needs=("mingw", "original_exe")),
    # (The lighting, game/human_lighting.cpp: human/build_lighting.bat, HUMAN_LIGHTING.)
    T("human.lighting", "human/build_lighting.bat", TMP + r"\human_lighting_test.exe", needs=("mingw", "original_exe")),
    # (The morph, game/human_morph.cpp through morph_statics.cpp: human/build_morph.bat, HUMAN_MORPH.)
    T("human.morph", "human/build_morph.bat", TMP + r"\human_morph_test.exe", needs=("mingw", "original_exe")),
    # (The work, game/human_work.cpp: human/build_work.bat, HUMAN_WORK.)
    T("human.work", "human/build_work.bat", TMP + r"\human_work_test.exe", needs=("mingw", "original_exe")),
    # (The weapon pose's skeleton and chains, game/human_pose_ik.cpp: human/build_ik.bat, HUMAN_IK.)
    T("human.ik", "human/build_ik.bat", TMP + r"\human_ik_test.exe", needs=("mingw", "original_exe")),
    # (The weapon in the hands and its holds, game/human_pose_weapon.cpp and _hold.cpp: human/build_weapon.bat, HUMAN_WEAPON.)
    T("human.weapon", "human/build_weapon.bat", TMP + r"\human_weapon_test.exe", needs=("mingw", "original_exe")),
    # (The prone body, the head and the seats, game/human_pose_body.cpp and _seats.cpp: human/build_body.bat, HUMAN_BODY.)
    T("human.body", "human/build_body.bat", TMP + r"\human_body_test.exe", needs=("mingw", "original_exe")),
    # Builds that run on the game data with their own judging.
    T("checkpoints", "checkpoints/build.bat", "checkpoints/harness.exe", summary=r"(\d+) missions, (\d+) failures",
      needs=("mingw", "data"), hardcoded_data=True),
    T("profile", "profile/build.bat", "profile/harness.exe", summary=r"^(\d+) failures", runs=6,
      needs=("mingw", "data"), hardcoded_data=True,
      restores=["profile/run/profile1.sav", "profile/run/PlayersProfiles/Tester/profile.sav"]),
    # Build only here; run by this script.
    suite("net_quantization", "net_quantization/build.bat", "net_quantization/quant_test.exe", "run",
          ["mingw"], args=[], summary=r"(\d+) cases, (\d+) failures", timeout=600),
    suite("car_tables", "car_tables/build.bat", "car_tables/harness.exe", "car_tables", ["mingw", "data"], timeout=600),
    suite("vehicle_params", "vehicle_params/build.bat", "vehicle_params/harness.exe", "vehicle_params",
          ["mingw", "data"], timeout=600, also_built=["vehicle_params/apply.exe"],
          note="ORIG_DIR (origread.py's original-reader simulation) not run; apply.exe built, not run"),
    suite("mp_setup", "mp_setup/build.bat", "mp_setup/harness.exe", "mp_setup", ["mingw", "mp_data"], timeout=900),
]


# ---------------------------------------------------------------- helpers

def expand(path):
    path = path.replace("%TEMP%", TEMP)
    return path if os.path.isabs(path) else os.path.join(HERE, path)


def sha256(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for block in iter(lambda: f.read(1 << 20), b""):
            h.update(block)
    return h.hexdigest()


def run_bounded(cmd, log, cwd=None, timeout=600, env=None):
    """Runs cmd, writing the output's tail (TAIL bytes) to log. Returns (exit code or 'timeout', tail text)."""
    with open(log, "wb") as out:
        p = subprocess.Popen(cmd, cwd=cwd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, env=env,
                             creationflags=getattr(subprocess, "CREATE_NEW_PROCESS_GROUP", 0))
        tail = bytearray()
        start = time.time()
        timed_out = False
        import threading

        def pump():
            for chunk in iter(lambda: p.stdout.read1(65536) if hasattr(p.stdout, "read1") else p.stdout.read(65536), b""):
                tail.extend(chunk)
                if len(tail) > TAIL:
                    del tail[:len(tail) - TAIL]

        t = threading.Thread(target=pump, daemon=True)
        t.start()
        while p.poll() is None:
            if time.time() - start > timeout:
                timed_out = True
                subprocess.run(["taskkill", "/T", "/F", "/PID", str(p.pid)], capture_output=True)
                break
            time.sleep(0.2)
        p.wait()
        t.join(5)
        out.write(bytes(tail))
    text = bytes(tail).decode("latin-1")
    return ("timeout" if timed_out else p.returncode), text


def bat_cmd(bat, *args):
    return ["cmd", "/d", "/c", "call", expand(bat), *args]


# ---------------------------------------------------------------- gcov

def covered(s):
    return "msvc" not in s["needs"]


def bat_for(s, ctx):
    """The build script to call: the suite's own, or with --coverage a copy beside it (its %~dp0 must
    stay the same) whose g++ line building the suite's executable has the coverage options."""
    bat = expand(s["bat"])
    cov = ctx and ctx.get("cov")
    if not cov or not covered(s):
        return bat
    if cov.get("bat"):
        return cov["bat"]
    text = open(bat, encoding="latin-1").read()
    joined = re.sub(r"\^[ \t]*\r?\n[ \t]*", " ", text)      # (continuation lines joined)
    exe = os.path.basename(s["exe"])
    lines, done = [], 0
    for line in joined.splitlines():
        m = re.match(r"(\s*)g\+\+ ", line)
        if m and exe in line:
            line = '%sg++ --coverage -dumpdir "%s\\\\" %s' % (m.group(1), cov["notes"], line[m.end():])
            done += 1
        lines.append(line)
    if not done:
        raise RuntimeError("no g++ line building %s in %s" % (exe, s["bat"]))
    path = os.path.join(os.path.dirname(bat), "zz_cov_%s.bat" % s["name"].replace(".", "_"))
    open(path, "w", encoding="latin-1", newline="\r\n").write("\n".join(lines) + "\n")
    cov["bat"] = path
    return path


def cov_env(ctx, tag):
    """With --coverage: the environment sending a run's .gcda files to its own folder."""
    cov = ctx and ctx.get("cov")
    if not cov or not cov.get("bat"):
        return None
    d = os.path.join(cov["dir"], "runs", tag)
    os.makedirs(d, exist_ok=True)
    notes = os.path.normpath(cov["notes"])
    strip = len([p for p in notes.split(os.sep) if p])
    return dict(os.environ, GCOV_PREFIX=d, GCOV_PREFIX_STRIP=str(strip))


def production_path(path, cwd):
    """A gcov source path as a repository path when it is a production source (src/, not tier1), else None."""
    full = os.path.normcase(os.path.normpath(os.path.join(cwd, path)))
    src = os.path.normcase(os.path.join(ROOT, "src")) + os.sep
    if not full.startswith(src) or (os.sep + "tier1" + os.sep) in full:
        return None
    return os.path.relpath(full, os.path.normcase(ROOT)).replace(os.sep, "/")


def gather_coverage(s, ctx):
    """gcov over every run folder of the suite; the production sources' counts added up:
    {file: {"lines": {n: count}, "functions": {name: [line, count, demangled]}, "branches": {"n:i": count}}}."""
    import gzip
    cov = ctx["cov"]
    notes = glob.glob(os.path.join(cov["notes"], "*.gcno"))
    runs = sorted(glob.glob(os.path.join(cov["dir"], "runs", "*")))
    cwd = os.path.dirname(expand(s["bat"]))
    gcov = os.path.join(os.path.dirname(MINGW), "gcov.exe")
    total = {}
    for run in runs:
        for n in notes:
            shutil.copy2(n, run)
        subprocess.run([gcov, "--json-format", "-b", "-o", run] + [os.path.basename(n) for n in notes],
                       cwd=run, capture_output=True)
        for j in glob.glob(os.path.join(run, "*.gcov.json.gz")):
            for f in json.load(gzip.open(j))["files"]:
                rel = production_path(f["file"], cwd)
                if not rel:
                    continue
                t = total.setdefault(rel, {"lines": {}, "functions": {}, "branches": {}})
                for ln in f["lines"]:
                    no = str(ln["line_number"])
                    t["lines"][no] = t["lines"].get(no, 0) + ln["count"]
                    for i, b in enumerate(x for x in ln["branches"] if not x.get("throw")):
                        key = "%s:%d" % (no, i)
                        t["branches"][key] = t["branches"].get(key, 0) + b["count"]
                for fn in f["functions"]:
                    e = t["functions"].setdefault(fn["name"], [fn["start_line"], 0, fn.get("demangled_name", "")])
                    e[1] += fn["execution_count"]
    return total


def merge_coverage(into, part):
    for rel, t in part.items():
        m = into.setdefault(rel, {"lines": {}, "functions": {}, "branches": {}})
        for k in ("lines", "branches"):
            for key, c in t[k].items():
                m[k][key] = m[k].get(key, 0) + c
        for name, (line, c, dem) in t["functions"].items():
            e = m["functions"].setdefault(name, [line, 0, dem])
            e[1] += c


def coverage_counts(t):
    lines = t["lines"].values()
    fns = [c for _, c, _ in t["functions"].values()]
    br = t["branches"].values()
    return (sum(1 for c in lines if c), len(lines), sum(1 for c in fns if c), len(fns),
            sum(1 for c in br if c), len(br))


def write_coverage(ctx, ident, per_suite, merged):
    import gzip
    d = os.path.join(ctx["work"], "coverage")
    with gzip.open(os.path.join(d, "coverage.json.gz"), "wt") as f:
        json.dump(dict(identity=ident, suites={k: sorted(v) for k, v in per_suite.items()}, files=merged), f)

    def pct(a, b):
        return "%.1f" % (100.0 * a / b) if b else "-"

    tot = [0] * 6
    rows = []
    for rel in sorted(merged):
        c = coverage_counts(merged[rel])
        tot = [a + b for a, b in zip(tot, c)]
        rows.append("| %s | %d / %d | %d / %d | %d / %d (%s %%) |" % (rel, c[0], c[1], c[2], c[3], c[4], c[5],
                                                                      pct(c[4], c[5])))
    lines = ["# Harness coverage (gcov)", "",
             "- Revision: `%s`%s" % (ident["revision"], " plus %d uncommitted changes" % len(ident["uncommitted"])
                                      if ident["uncommitted"] else ""),
             "- Suites measured: %d (%s)" % (len(per_suite), ", ".join(sorted(per_suite))),
             "- Production sources: those under src/ that a measured suite compiles (its .gcno); the suites' own "
             "files and fakes are left out. Files no suite compiles are not in this table.",
             "- Branches: gcov's branch outcomes, exception edges left out; a header's inline code counts once per "
             "line however many objects include it.", "",
             "| | Lines | Functions | Branch outcomes taken |", "|---|---:|---:|---:|",
             "| **all %d files** | %d / %d (%s %%) | %d / %d (%s %%) | %d / %d (%s %%) |" % (
                 len(merged), tot[0], tot[1], pct(tot[0], tot[1]), tot[2], tot[3], pct(tot[2], tot[3]), tot[4],
                 tot[5], pct(tot[4], tot[5])), "",
             "| Source | Lines | Functions | Branch outcomes taken |", "|---|---:|---:|---:|"] + rows
    # The client's sources no measured suite compiles: outside the table above, not 0 % inside it.
    unmeasured = []
    for dp, dirs, fns in os.walk(os.path.join(ROOT, "src")):
        dirs[:] = [x for x in dirs if x != "tier1"]
        for f in fns:
            if f.endswith(".cpp"):
                rel = os.path.relpath(os.path.join(dp, f), ROOT).replace(os.sep, "/")
                if rel not in merged:
                    n = sum(1 for _ in open(os.path.join(dp, f), encoding="utf-8", errors="replace"))
                    unmeasured.append((rel, n))
    total_cpp = len(unmeasured) + sum(1 for r in merged if r.endswith(".cpp"))
    lines[4:4] = ["- Client .cpp files no suite compiles: %d of %d (%d source lines): listed at the end. The "
                  "percentages are of the measured files only." % (len(unmeasured), total_cpp,
                                                                  sum(n for _, n in unmeasured))]
    lines += ["", "## Client sources no suite compiles", "", "| Source | Source lines |", "|---|---:|"]
    lines += ["| %s | %d |" % u for u in sorted(unmeasured, key=lambda u: -u[1])]
    open(os.path.join(d, "coverage.md"), "w").write("\n".join(lines) + "\n")
    return tot


# ---------------------------------------------------------------- inputs

def prepare_cars(work, data):
    """export_cars.py's 37 car models (and the 176 damage models, "dm4ds" beside them)."""
    base = os.path.join(work, "inputs")
    cars, dms = os.path.join(base, "cars4ds"), os.path.join(base, "dm4ds")
    stamp = os.path.join(base, "cars.done")
    if not os.path.exists(stamp):
        for d in (cars, dms):
            shutil.rmtree(d, ignore_errors=True)
        exporter = os.path.join(HERE, "vehicle_setup", "export_cars.py")
        models = os.path.join(data, "MODELS")
        for args in ([models, cars], [models, dms, "--damage"]):
            r = subprocess.run([sys.executable, exporter, *args], capture_output=True, text=True)
            if r.returncode:
                raise RuntimeError("export_cars.py %s failed:\n%s" % (" ".join(args), r.stdout + r.stderr))
        open(stamp, "w").write(datetime.datetime.now().isoformat())
    return cars


def prepare_missions(work, data):
    """The world build's mission list: every mission with a Vertanim.bin, and AFRICA2 without one."""
    d = os.path.join(work, "inputs", "missions")
    os.makedirs(d, exist_ok=True)
    names = sorted({os.path.basename(os.path.dirname(p)) for p in glob.glob(os.path.join(data, "MISSIONS", "*", "*"))
                    if os.path.basename(p).lower() == "vertanim.bin"} | {"AFRICA2"})
    for f in glob.glob(os.path.join(d, "*.txt")):
        os.remove(f)
    for n in names:
        open(os.path.join(d, n + ".txt"), "w", newline="\n").write("mission %s\n" % n)
    return d


# ---------------------------------------------------------------- suites

def judge_summary(text, pattern, runs=1):
    """Every summary line must report 0 failures, and there must be `runs` of them."""
    found = [(int(a), int(b)) for a, b in re.findall(pattern, text, re.M)] if pattern.count("(") == 2 else \
        [(None, int(b)) for b in re.findall(pattern, text, re.M)]
    counts = dict(summaries=len(found), checks=sum(a or 0 for a, _ in found), failures=sum(b for _, b in found))
    if len(found) < runs:
        return None, counts
    return counts["failures"] == 0, counts


def run_selfrun(s, ctx, log):
    code, text = run_bounded(bat_cmd(bat_for(s, ctx)), log, cwd=os.path.dirname(expand(s["bat"])), timeout=s["timeout"],
                             env=cov_env(ctx, "run"))
    if not os.path.exists(expand(s["exe"])):
        return "build-failed", {}, code
    if code == "timeout":
        return "timeout", {}, code
    ok, counts = judge_summary(text, s["summary"], s.get("runs", 1))
    if ok is None:
        return "crashed", counts, code
    return ("pass" if ok and code == 0 else "fail"), counts, code


def build(s, log, ctx=None):
    code, _ = run_bounded(bat_cmd(bat_for(s, ctx)), log, cwd=os.path.dirname(expand(s["bat"])), timeout=1800)
    return code, os.path.exists(expand(s["exe"]))


def run_inputs(s, ctx, log):
    code, built = build(s, log, ctx)
    if not built:
        return "build-failed", {}, code
    exe = expand(s["exe"])
    inputs = sorted(glob.glob(os.path.join(ctx[s["inputs"]], "*.txt")))
    if s["once"]:
        inputs = inputs[:1]
    jobs = 1 if s["once"] else min(ctx["jobs"], len(inputs))
    chunks = [inputs[i::jobs] for i in range(jobs)]
    stem = os.path.splitext(log)[0]

    def one(k):
        # Each chunk in its own folder: tests write scratch files to the current folder (mpdesc_test.txt).
        cwd = "%s.cwd%02d" % (stem, k)
        shutil.rmtree(cwd, ignore_errors=True)
        os.makedirs(cwd)
        result = run_bounded([exe, *chunks[k]], "%s.out%02d.txt" % (stem, k), cwd=cwd, timeout=s["timeout"],
                             env=cov_env(ctx, "c%02d" % k))
        shutil.rmtree(cwd, ignore_errors=True)
        return (k, *result)

    with concurrent.futures.ThreadPoolExecutor(jobs) as ex:
        results = sorted(ex.map(one, range(jobs)))
    merged = stem + ".out.txt"
    with open(merged, "w", encoding="latin-1", newline="") as f:
        for _, _, text in results:
            f.write(text)
    for k in range(jobs):
        os.remove("%s.out%02d.txt" % (stem, k))
    codes = [c for _, c, _ in results]
    counts = dict(inputs=len(inputs), chunks=jobs, checkers={})
    if "timeout" in codes:
        return "timeout", counts, codes
    status = "pass" if all(c == 0 for c in codes) else "crashed"
    with open(log, "a", encoding="latin-1") as lf:
        for c in s["checks"]:
            r = subprocess.run([sys.executable, expand(c), ctx[s["inputs"]], merged], capture_output=True, text=True,
                               cwd=os.path.dirname(expand(c)))
            lf.write("\n==== %s\n%s%s" % (c, r.stdout[-20000:], r.stderr[-5000:]))
            m = re.search(r"(\d+) models, (\d+) with differences", r.stdout)
            name = os.path.basename(c)
            if not m:
                counts["checkers"][name] = "unreadable"
                status = "fail" if status == "pass" else status
                continue
            n, bad = int(m.group(1)), int(m.group(2))
            counts["checkers"][name] = "%d of %d differ" % (bad, n)
            if bad or n != len(inputs):
                status = "fail" if status == "pass" else status
    return status, counts, codes


def run_plain(s, ctx, log):
    code, built = build(s, log, ctx)
    if not built:
        return "build-failed", {}, code
    exe = expand(s["exe"])
    code, text = run_bounded([exe, *s["args"]], log + ".run.txt", cwd=os.path.dirname(exe), timeout=s["timeout"],
                             env=cov_env(ctx, "run"))
    if code == "timeout":
        return "timeout", {}, code
    ok, counts = judge_summary(text, s["summary"])
    if ok is None:
        return "crashed", counts, code
    return ("pass" if ok and code == 0 else "fail"), counts, code


def run_car_tables(s, ctx, log):
    code, built = build(s, log, ctx)
    if not built:
        return "build-failed", {}, code
    exe = expand(s["exe"])
    scratch = os.path.join(ctx["work"], "car_tables_scratch")
    shutil.rmtree(scratch, ignore_errors=True)
    os.makedirs(scratch)
    out = log + ".run.txt"
    code, _ = run_bounded([exe, ctx["data"], scratch], out, cwd=os.path.dirname(exe), timeout=s["timeout"],
                          env=cov_env(ctx, "run"))
    if code == "timeout":
        return "timeout", {}, code
    r = subprocess.run([sys.executable, os.path.join(HERE, "car_tables", "check.py"), ctx["data"], out],
                       capture_output=True, text=True)
    with open(log, "a") as lf:
        lf.write("\n==== check.py\n" + r.stdout[-20000:] + r.stderr[-5000:])
    m = re.search(r"(\d+) lines, (\d+) differences", r.stdout)
    if not m:
        return "fail", {"checker": "unreadable"}, code
    counts = dict(lines=int(m.group(1)), differences=int(m.group(2)))
    return ("pass" if code == 0 and counts["differences"] == 0 else "fail"), counts, code


def run_vehicle_params(s, ctx, log):
    code, built = build(s, log, ctx)
    if not built or not all(os.path.exists(expand(e)) for e in s["also_built"]):
        return "build-failed", {}, code
    base = os.path.join(ctx["work"], "inputs")
    bins, props = os.path.join(base, "carbins"), os.path.join(base, "carprops")
    for d in (bins, props):
        shutil.rmtree(d, ignore_errors=True)
    env = dict(os.environ)
    r = subprocess.run([sys.executable, os.path.join(HERE, "vehicle_params", "carparams.py"), bins, props],
                       capture_output=True, text=True, cwd=os.path.join(HERE, "vehicle_params"), env=env)
    with open(log, "a") as lf:
        lf.write("\n==== carparams.py\n" + r.stdout[-20000:] + r.stderr[-5000:])
    if r.returncode or not os.path.isdir(props):
        return "fail", {"carparams.py": "failed"}, r.returncode
    exe = expand(s["exe"])
    code, text = run_bounded([exe, props, os.path.join(HERE, "vehicle_params", "params_map.txt")], log + ".run.txt",
                             cwd=os.path.dirname(exe), timeout=s["timeout"], env=cov_env(ctx, "run"))
    if code == "timeout":
        return "timeout", {}, code
    m = re.search(r"cars (\d+): repacked identical (\d+), read\+write identical (\d+), read\(write\(read\)\) same (\d+)",
                  text)
    if not m:
        return "crashed", {}, code
    cars, *same = map(int, m.groups())
    counts = dict(cars=cars, repacked=same[0], read_write=same[1], reread=same[2])
    return ("pass" if code == 0 and cars and all(x == cars for x in same) else "fail"), counts, code


def run_mp_setup(s, ctx, log):
    code, built = build(s, log, ctx)
    if not built:
        return "build-failed", {}, code
    exe = expand(s["exe"])
    code, text = run_bounded([sys.executable, os.path.join(HERE, "mp_setup", "run_tests.py"), exe, ctx["mp_data"]],
                             log + ".run.txt", cwd=os.path.join(HERE, "mp_setup"), timeout=s["timeout"],
                             env=cov_env(ctx, "run"))
    if code == "timeout":
        return "timeout", {}, code
    return ("pass" if code == 0 else "fail"), {}, code


RUNNERS = dict(selfrun=run_selfrun, inputs=run_inputs, run=run_plain, car_tables=run_car_tables,
               vehicle_params=run_vehicle_params, mp_setup=run_mp_setup)


# ---------------------------------------------------------------- main

def identity(ctx):
    def out(cmd, **kw):
        try:
            return subprocess.run(cmd, capture_output=True, text=True, **kw).stdout.strip()
        except OSError:
            return None

    gxx = out([MINGW, "--version"]) if os.path.exists(MINGW) else None
    return dict(
        date=datetime.datetime.now().isoformat(timespec="seconds"),
        revision=out(["git", "rev-parse", "HEAD"], cwd=ROOT),
        uncommitted=[l for l in (out(["git", "status", "--porcelain"], cwd=ROOT) or "").splitlines() if l],
        python=sys.version.split()[0],
        mingw=gxx.splitlines()[0] if gxx else None,
        msvc=VCVARS if os.path.exists(VCVARS) else None,
        data=ctx["data"], mp_data=ctx["mp_data"], jobs=ctx["jobs"])


def missing(s, ctx):
    have = dict(mingw=os.path.exists(MINGW), msvc=os.path.exists(VCVARS),
                data=os.path.isdir(os.path.join(ctx["data"], "MISSIONS")),
                mp_data=bool(ctx["mp_data"]) and os.path.isdir(ctx["mp_data"] or ""),
                cars=CARS in ctx, missions=MISSIONS in ctx, equipment_save=os.path.isfile(EQUIPMENT_SAVE), items_sav=os.path.isfile(ITEMS_SAV),
                original_exe=os.path.isfile(ORIGINAL_EXE))
    lost = [n for n in s["needs"] if not have.get(n, False)]
    if s.get("hardcoded_data") and os.path.normcase(os.path.normpath(ctx["data"])) != os.path.normcase(r"H:\HD2_Unpacked"):
        lost.append("data at H:\\HD2_Unpacked (hard-coded in %s)" % s["bat"])
    return lost


def write_report(ctx, ident, results):
    with open(os.path.join(ctx["work"], "report.json"), "w") as f:
        json.dump(dict(identity=ident, suites=results), f, indent=1)
    by = {}
    for r in results:
        by[r["status"]] = by.get(r["status"], 0) + 1
    lines = ["# Harness baseline", "",
             "- Revision: `%s`%s" % (ident["revision"],
                                     " plus %d uncommitted changes" % len(ident["uncommitted"]) if ident["uncommitted"] else ""),
             "- Date: %s; MinGW: %s; MSVC: %s" % (ident["date"], ident["mingw"], "yes" if ident["msvc"] else "no"),
             "- Game data: %s; mp_setup data: %s" % (ident["data"], ident["mp_data"] or "none"),
             "- Totals: " + ", ".join("%s %d" % kv for kv in sorted(by.items())), "",
             "| Suite | Status | Result | Seconds | Note |", "|---|---|---|---:|---|"]
    for r in results:
        c = r.get("counts") or {}
        if "checkers" in c:
            res = "%d inputs; " % c["inputs"] + "; ".join("%s %s" % kv for kv in c["checkers"].items())
        else:
            res = ", ".join("%s %s" % kv for kv in c.items())
        note = "; ".join(x for x in (r.get("missing"), r.get("note"), r.get("error")) if x)
        lines.append("| %s | %s | %s | %.0f | %s |" % (r["name"], r["status"], res, r.get("seconds", 0), note))
    open(os.path.join(ctx["work"], "report.md"), "w").write("\n".join(lines) + "\n")


def main():
    p = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    p.add_argument("names", nargs="*")
    p.add_argument("--work", default=os.path.join(TEMP, "hd2_suites"))
    p.add_argument("--data", default=os.environ.get("HD2_UNPACKED", r"H:\HD2_Unpacked"))
    p.add_argument("--mp-data")
    p.add_argument("--jobs", type=int, default=12)
    p.add_argument("--list", action="store_true")
    p.add_argument("--coverage", action="store_true")
    a = p.parse_args()

    chosen = [s for s in SUITES if not a.names or any(fnmatch.fnmatch(s["name"], n) for n in a.names)]
    if a.list:
        for s in chosen:
            print("%-22s %-14s %-40s needs %s" % (s["name"], s["kind"], s["bat"], ", ".join(s["needs"])))
        return 0
    if not chosen:
        sys.exit("no suite matches %s" % a.names)

    os.makedirs(os.path.join(a.work, "logs"), exist_ok=True)
    ctx = dict(work=a.work, data=a.data, mp_data=a.mp_data, jobs=a.jobs)
    ident = identity(ctx)
    if os.path.isdir(os.path.join(a.data, "MISSIONS")):
        try:
            if any(CARS in s["needs"] for s in chosen):
                ctx[CARS] = prepare_cars(a.work, a.data)
            if any(MISSIONS in s["needs"] for s in chosen):
                ctx[MISSIONS] = prepare_missions(a.work, a.data)
        except Exception as e:  # an input set that cannot be made: its suites report it
            print("input preparation failed:", e)
    results = []
    per_suite, merged = {}, {}
    if a.coverage:
        shutil.rmtree(os.path.join(a.work, "coverage"), ignore_errors=True)
        os.makedirs(os.path.join(a.work, "coverage"))
    for s in chosen:
        r = dict(name=s["name"], bat=s["bat"], note=s.get("note"))
        lost = missing(s, ctx)
        if lost:
            r.update(status="missing-prerequisite", missing="missing: " + ", ".join(lost))
        else:
            for stale in [s["exe"], *s.get("also_built", []), *( [s["tree_copy"]] if s.get("tree_copy") else [])]:
                if os.path.exists(expand(stale)):
                    os.remove(expand(stale))
            log = os.path.join(a.work, "logs", s["name"] + ".log")
            start = time.time()
            if a.coverage and covered(s):
                d = os.path.join(a.work, "coverage", s["name"])
                ctx["cov"] = dict(dir=d, notes=os.path.join(d, "notes"))
                os.makedirs(ctx["cov"]["notes"])
            # (Tracked files a suite rewrites: put back afterwards.)
            kept = {f: open(expand(f), "rb").read() for f in s.get("restores", []) if os.path.exists(expand(f))}
            try:
                status, counts, code = RUNNERS[s["kind"]](s, ctx, log)
            except Exception as e:
                status, counts, code = "crashed", {}, None
                r["error"] = "runner: %r" % e
            for f, data in kept.items():
                open(expand(f), "wb").write(data)
            if ctx.get("cov"):
                if ctx["cov"].get("bat") and os.path.exists(ctx["cov"]["bat"]):
                    os.remove(ctx["cov"]["bat"])
                try:
                    part = gather_coverage(s, ctx)
                    per_suite[s["name"]] = part
                    merge_coverage(merged, part)
                    c = [sum(x) for x in zip(*(coverage_counts(v) for v in part.values()))] or [0] * 6
                    r["coverage"] = "lines %d/%d, functions %d/%d, branches %d/%d" % tuple(c)
                except Exception as e:
                    r["coverage"] = "gcov failed: %r" % e
                ctx["cov"] = None
            r.update(status=status, counts=counts, exit=code, seconds=time.time() - start, log=log)
            if os.path.exists(expand(s["exe"])):
                r["exe_sha256"] = sha256(expand(s["exe"]))
        results.append(r)
        print("%-22s %-20s %s%s" % (r["name"], r["status"], json.dumps(r.get("counts") or r.get("missing") or ""),
                                    ("  [%s]" % r["coverage"]) if r.get("coverage") else ""), flush=True)
        write_report(ctx, ident, results)
    if a.coverage:
        tot = write_coverage(ctx, ident, per_suite, merged)
        print("coverage: lines %d/%d, functions %d/%d, branch outcomes %d/%d over %d production files" % (
            *tot, len(merged)))
        print("coverage report:", os.path.join(a.work, "coverage", "coverage.md"))
    print("report:", os.path.join(a.work, "report.md"))
    return 0 if all(r["status"] == "pass" for r in results) else 1


if __name__ == "__main__":
    sys.exit(main())
