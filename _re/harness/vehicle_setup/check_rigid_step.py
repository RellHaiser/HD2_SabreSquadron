"""Checks the rigid body's step build's output (harness_rigid_step.exe, RIGID_STEP_TEST) against the models.

    python check_rigid_step.py MODELS_TXT_DIR HARNESS_OUTPUT

Per model: a rigid step line with every check of RigidStepTest met. Prints
"N models, M with differences" (mutfast.py reads it).
"""
import glob
import os
import re
import sys

CHECKS = "filter collide integrate box boxRays boxContact water actors wheel drop".split()


def main():
    models, output = sys.argv[1], sys.argv[2]
    text = open(output).read()
    blocks = {b.split()[0].replace(".4ds", ""): b for b in text.split("== ")[1:]}
    bad = 0
    total = 0
    for path in sorted(glob.glob(os.path.join(models, "*.txt"))):
        name = open(path).readline().split()[1].replace(".4ds", "")
        total += 1
        m = re.search(r"rigid step: (.*)", blocks.get(name, ""))
        diffs = []
        if not m:
            diffs.append(("rigid step", "missing"))
        elif m.group(1).strip() != " ".join(f"{c} 1" for c in CHECKS):
            diffs += [(c, 0) for c in CHECKS if not re.search(rf"\b{c} 1\b", m.group(1))] or [("checks", 0)]
        if diffs:
            bad += 1
            print(name, diffs)
    print(total, "models,", bad, "with differences")


main()
