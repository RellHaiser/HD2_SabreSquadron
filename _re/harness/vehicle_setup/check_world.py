"""Checks the world modules build's output (harness_world.exe, WORLD_TEST; part 8) against the missions.

    python check_world.py MISSIONS_TXT_DIR HARNESS_OUTPUT

MISSIONS_TXT_DIR holds a .txt per mission ("mission NAME"). Per mission: a vanim, an lmaps and a
tshift line with every check met. Prints "N models, M with differences" (mutfast.py reads it).
"""
import glob
import os
import re
import sys

LINES = {
    "vanim": "vaLoad vaCut vaDone vaFind vaPlayers vaBin vaBlend vaReset vaUpdate vaPing vaTick vaStart vaStop vaRemove".split(),
    "lmaps": "lmLoad lmFind lmSet lmGroup lmSave lmLoadGame lmRestart lmFree".split(),
    "tshift": "tsFind tsAdd tsRemove tsFree".split(),
    "sight": "sgRegister sgRelation sgParts sgTestNow sgCanSee sgUpdate sgTiming sgTracker sgTick sgStates sgRemove sgModule".split(),
    "geffects": ("gfModule gfQuad gfFade gfModelFx gfHierarchy gfFades gfHumans gfStorm gfWater gfUnderwater gfRain gfTeamRain "
                 "gfCamera gfMission").split(),
    "geffects2": ("gfShadow gfShadowName gfShadowFind gfFace gfFaceFind gfUniform gfUniformFile gfGlass gfShardSplit "
                  "gfShardPhysics").split(),
    "birds": ("bdMake bdDelete bdSit bdHop bdFlyAway bdTakeOff bdClimb bdGlide bdModule bdMovie bdMovieFile "
              "bdMovieUpdate bdEffects").split(),
    "decorators": ("dcMake dcLoad dcVersions dcLoadEnd dcLight dcStart dcGrid dcOn dcClear dcFree dcCover "
                   "dcModule").split(),
    "decorators2": ("dmSlope dmLight dmTake dmPool dmPrepare dmCell dmRefill dmRefillSeed dmFollow dmFollowMove dmShift "
                    "dmBend dmBendSpring dmBendUpdate dmBends dmHuman dmCar dmUpdate dmSway").split(),
    "volumetrics": ("vlCone vlConeLoad vlBuild vlBuildCone vlTexture vlRender vlRenderCone vlShaft vlShaftLoad vlRefresh "
                    "vlDraw vlFade vlList vlPrune vlListRender vlSort vlClouds vlCloudDraw vlModule").split(),
}


def main():
    missions, output = sys.argv[1], sys.argv[2]
    text = open(output).read()
    blocks = {b.split()[0]: b for b in text.split("== ")[1:] if b.split()}
    bad = 0
    total = 0
    for path in sorted(glob.glob(os.path.join(missions, "*.txt"))):
        name = open(path).readline().split()[1]
        total += 1
        diffs = []
        for line, checks in LINES.items():
            m = re.search(rf"{line}: (.*)", blocks.get(name, ""))
            if not m:
                diffs.append((line, "missing"))
            elif m.group(1).strip() != " ".join(f"{c} 1" for c in checks):
                diffs += [(f"{line}.{c}", 0) for c in checks if not re.search(rf"\b{c} 1\b", m.group(1))] or [(line, 0)]
        if diffs:
            bad += 1
            print(name, diffs)
    print(f"{total} models, {bad} with differences")


main()
