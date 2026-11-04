"""Checks the factory build's campaign screens line (harness_factory.exe, FACTORY_TEST; part 9g).

    python check_campaignscreens.py MISSIONS_TXT_DIR HARNESS_OUTPUT

Every mission's "campaignscreens:" line (one at least) must have every check met; the text build's other
lines are not checked (check_text.py's). Prints "N models, M with differences" (mutfast.py reads it).
"""
import re
import sys

CHECKS = ("oeTable oeDescribe oeBuild oeInput oeSlots "
          "smStart smMessage smConsole smPassword smBuild smTick smJoin smNotify "
          "vsCredits vsMovie vsFit vsUpdate vsScroll vsRender vsInput vsMovieState "
          "dbMedal dbResults dbStore dbStart dbMessage dbBuild "
          "dbDiary dbDiaryPage dbWon dbScreen dbPanel dbObjectives dbResult dbShow dbSkills dbRank dbMedals dbScroll sqSlot sqOneSoldier sqStart sqSlots sqSync sqMessage sqSelect sqDead sqFill sqSelection sqShown sqSquadScreen sqBuild").split()


def main():
    text = open(sys.argv[2], errors="replace").read()
    lines = re.findall(r"^campaignscreens: (.*)$", text, re.M)
    bad = 0 if lines else 1
    for line in lines:
        got = dict(re.findall(r"(\w+) (\d)", line))
        missed = [c for c in CHECKS if got.get(c) != "1"]
        if missed:
            bad += 1
            print("('campaignscreens'", missed, ")")
    print("%d models, %d with differences" % (max(len(lines), 1), bad))


if __name__ == "__main__":
    main()
