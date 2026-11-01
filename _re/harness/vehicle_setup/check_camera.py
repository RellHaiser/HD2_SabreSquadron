"""Checks the camera build's output (harness_camera.exe, CAMERA_TEST; part 8e) against the missions.

    python check_camera.py MISSIONS_TXT_DIR HARNESS_OUTPUT

MISSIONS_TXT_DIR holds a .txt per mission ("mission NAME"). Per mission: a camera and a keeper line
with every check met. Prints "N models, M with differences" (mutfast.py reads it).
"""
import glob
import os
import re
import sys

LINES = {
    "camera": ("cmMake cmGetters cmSaveLoad cmRestart cmStart cmStartNone cmSettings cmSettingsFile cmStop cmPos cmDir "
               "cmBlend cmTarget cmFov cmViewDir").split(),
    "keeper": "kpMake kpRestore kpUpdate kpCar kpSubmarine kpPlayer".split(),
    "tactical": ("tcBasics tcOn tcOff tcMove tcLookAt tcMode tcFlight tcUpdate tcMoveMode tcTurn tcDrag tcUpDown "
                 "tcZoom tcFly tcHumans").split(),
    "update": "upGate upMouse upFirst upFollow upTail upCar upScripted upDebug".split(),
    "place": ("plNull plTurn plPlain plSweep plBlend mvShake mvOffsets mvTarget mvAim mvTurret mvFade svFoot svSeat "
              "svChange smModes").split(),
    "loader": ("lsQuiet lsOpenFail lsScene lsTexts lsMpTexts lsProgress lsDraw lsClose lsWait "
               "lsPlayers").split(),
    "mission": "msBegin msScene msObjects msCheckpoints msLoad msFail msModules".split(),
    "scene2": ("s2File s2Camera s2Settings s2List s2TShift s2Actors s2States s2Shadows s2Groups s2B820 s2Music "
               "s2Progress s2Fail").split(),
    "objects": ("moName moCreate moPlace moParent moModel moCamera moLight moLightOld moBox moSound moMesh "
                "moVisual moLightmaps moProjector moB110 moSector moFail").split(),
    "fonts": ("foSets foLanguages foJapanese foConfig foVersion foShared foMissing foTable foQuads foGrow foFlush "
              "foMap").split(),
    "xml": ("xmTree xmText xmComments xmAttributes xmBad xmWalk xmFile cfLanguage cfSet cfFonts "
            "cfValues").split(),
    "gdi": ("gdInit gdCells gdFail gdGlyph gdTexture gdReuse gdEvict gdOrder gdSelect gdClose").split(),
    "styles": "stFirst stDefaults stCreate stRelease".split(),
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
