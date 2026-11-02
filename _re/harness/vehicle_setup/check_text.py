"""Checks the text build's output (harness_text.exe, TEXT_TEST; part 9b) against the missions.

    python check_text.py MISSIONS_TXT_DIR HARNESS_OUTPUT

MISSIONS_TXT_DIR holds a .txt per mission ("mission NAME"). Per mission: a textbox, a layout, a position, a render, a pages, a paged, a utf8, a control, a controls, a scroll, a check, an edit, a combo and an info line with every check met. Prints "N models, M with differences" (mutfast.py reads it).
"""
import glob
import os
import re
import sys

LINES = {
    "textbox": ("tbNew tbRuns tbVersion tbInsert tbRemove tbEdit tbCaret tbSelect tbHover tbRange tbScrollTo tbCentre "
                "tbRect tbAttach tbDepth tbFree").split(),
    "layout": ("tbStyle tbAdvance tbWidth tbUtf8 tbTruncate tbPieces tbLines tbWords tbChars tbFail "
               "tbMeasures").split(),
    "position": ("tbHeight tbStack tbAlign tbCaret tbAtX tbJustify tbSplit tbPieceAt tbOverflow "
                 "tbPosition").split(),
    "render": ("tbPlaced tbGlyphs tbBlank tbFlush tbBatch tbColour tbCentred tbCaret tbPieces tbFail tbClip "
               "tbBackground tbOutline tbUtf8 tbStep tbFonts").split(),
    "pages": ("pgNew pgArea pgPage pgRelayout pgBreak pgRender pgLines pgColour pgStop pgClip vtNew vtLines "
              "vtFirst vtStop vtFade vtScroll vtFinished vtVirtual").split(),
    "paged": ("lpNone lpLines lpWrap lpPages lpAreas lpForced lpFail lpNoWrap lpMeasures lpJoins lpBreak lpStack "
              "lpAlign lpJustify lpNext lpOverflow lpAppend lpUtf8").split(),
    "utf8": "u8Range u8Encode u8Room u8Sequences u8Cjk u8Reattach".split(),
    "control": ("ctNew ctDesc ctFind ctInput ctShow ctCreate ctEvents ctMessages ctRoutes ctCallbacks ctRemove "
                "ctRefresh ctDelete ctQueue").split(),
    "controls": ("csText csTextMsgs csTitle csLabel csPicture csPicMsgs csGroup csGroupInput csSwitch csTextButton "
                 "csTooltip csButton csButtonHeld csPaged csPages csHandled").split(),
    "scroll": ("scFind scSlider scSliderDrag scSliderMsgs scBar scBarMsgs scBarInput scBarHeld scBarArrows scSpin "
               "scSpinMsgs scSpinInput").split(),
    "check": ("ckBox ckBoxInput ckBoxMsgs ckToggle ckToggleInput ckHigh ckHighInput ckHighMsgs ckHalf ckSide ckMark "
              "ckMarkMsgs").split(),
    "edit": ("edBase edChar edUtf8 edEditing edMsgs edBox edBoxRange edList edListInput edListMsgs").split(),
    "combo": ("cbScrolled cbScrolledInput cbScrolledMsgs cbHeader cbCombo cbComboInput cbComboMsgs cbTitled cbChat "
              "cbChatMsgs").split(),
    "info": "skBase skKeys skPool skInput skMsgs ifBase ifItem ifStats ifCounts ifMsgs".split(),
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
