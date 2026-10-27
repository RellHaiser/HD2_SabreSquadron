"""Checks the route build's output (harness_route.exe, ROUTE_TEST) against the models.

    python check_route.py MODELS_TXT_DIR HARNESS_OUTPUT

Per model: a route line with every check of RouteTest met (each model has a BODY frame). Prints
"N models, M with differences" (mutfast.py reads it).
"""
import glob
import os
import re
import sys

CHECKS = "unchanged step detour swap end legs fail cars tilt".split()


def main():
    models, output = sys.argv[1], sys.argv[2]
    text = open(output).read()
    blocks = {b.split()[0].replace(".4ds", ""): b for b in text.split("== ")[1:]}
    bad = 0
    total = 0
    for path in sorted(glob.glob(os.path.join(models, "*.txt"))):
        name = open(path).readline().split()[1].replace(".4ds", "")
        total += 1
        m = re.search(r"route: (.*)", blocks.get(name, ""))
        diffs = []
        if not m:
            diffs.append(("route", "missing"))
        elif m.group(1).strip() != " ".join(f"{c} 1" for c in CHECKS):
            diffs += [(c, 0) for c in CHECKS if not re.search(rf"\b{c} 1\b", m.group(1))] or [("checks", 0)]
        if diffs:
            bad += 1
            print(name, diffs)
    print(total, "models,", bad, "with differences")


main()
