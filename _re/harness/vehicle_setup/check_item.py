"""Checks the actors build's output (harness_item.exe, ITEM_TEST) against the models.

    python check_item.py MODELS_TXT_DIR HARNESS_OUTPUT

Per model: an items line with every check of ItemTest met (part 6a), an animation line with every
check of AnimationTest met (part 6b), a simple line with every check of SimpleActorsTest met
(part 7a) and an objective line with every check of ObjectiveActorsTest met (part 7b) a world line with every
check of WorldActorsTest met (part 7c), a moving line with every check of MovingActorsTest met
(part 7d) and a seat line with every check of SeatActorsTest met (part 7e). Prints
"N models, M with differences" (mutfast.py reads it).
"""
import glob
import os
import re
import sys

LINES = {
    "items": ("creator frame setup life update tick hit push contact sound message mission save teleport placement "
              "respawn debris debrisLife").split(),
    "animation": ("creator frame props copy start collisions step stop hit messages save load mission notes armour "
                  "wings restart").split(),
    "simple": ("creators clock clockMission clockSave null wire wireMission wireNet wireSave switchFrame switchState "
               "switchMission switchSave switchProps photo photoSave volume").split(),
    "objective": ("radio radioUse radioSave doc docState docNet docSave mine mineArm mineStep mineSave ts tsStep "
                  "tsSave spawn spawnNet spawnSetup spawnProps").split(),
    "world": ("explosive explosiveState explosiveNet explosiveSave ladder ladderUse ladderVolumes ladderProps pike "
              "pikeTurn pikeNet pikeSave fowl fowlBirds fowlTrigger fowlSave").split(),
    "moving": ("train trainProps trainStart trainPath trainMove trainWheels trainSounds trainSave sub subProps "
               "subStart subSeats subExit subMove subSounds subSave zone zoneProps zoneStart zoneCapture zoneState "
               "zoneSpawn").split(),
    "seat": ("fish fishProps fishStart fishTick fishUpdate fishSave fishStop bike bikeProps bikeStart bikeDrive "
             "bikeUpdate bikeSave bikeStop sw swStart swState swNet swSeats swExit swHit swTick swSave swBox").split(),
}


def main():
    models, output = sys.argv[1], sys.argv[2]
    text = open(output).read()
    blocks = {b.split()[0].replace(".4ds", ""): b for b in text.split("== ")[1:]}
    bad = 0
    total = 0
    for path in sorted(glob.glob(os.path.join(models, "*.txt"))):
        name = open(path).readline().split()[1].replace(".4ds", "")
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
    print(total, "models,", bad, "with differences")


main()
