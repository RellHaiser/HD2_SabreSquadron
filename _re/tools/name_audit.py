"""Names in the port's source against the game's files (H:\\HD2_Unpacked): literals given to the model and animation caches, and any literal whose file
exists only with a "#" or "!" in front (the crate's animations were written without theirs). Names built at run time or from tables in data are not seen.
Known: shlaif01.i3d and shlaif02.i3d are the original's own strings (0x0083c008, 0x0083c018).

Usage (from the repository's root): python _re/tools/name_audit.py
"""
import glob
import os
import re
import sys
roots = [r"H:\HD2_Unpacked\MODELS", r"H:\HD2_Unpacked\MAPS", r"H:\HD2_Unpacked\MAPS_C", r"H:\HD2_Unpacked\MAPS_U"]
models, anims = set(), set()
for r in roots:
    if not os.path.isdir(r):
        continue
    for f in os.listdir(r):
        n, e = os.path.splitext(f)
        (models if e.lower() in (".4ds", ".i3d") else anims if e.lower() == ".5ds" else set()).add(n.lower())
print(len(models), "models", len(anims), "animations")
call = re.compile(r'(g_animationCache|g_modelCache|g_versionModelCache|g_shadowCache)\s*\.\s*(?:Get|OpenInto|Load)\s*\(([^;]*?)\)\s*[;)<!=]')
lit = re.compile(r'"([^"\\]+)"')
bad = 0
seen = 0
for f in glob.glob("src/**/*.cpp", recursive=True):
    if "tier1" in f:
        continue
    text = open(f, encoding="utf-8", errors="replace").read()
    for m in call.finditer(text):
        for name in lit.findall(m.group(2)):
            seen += 1
            pool = anims if m.group(1) == "g_animationCache" else models
            if name.lower() not in pool:
                bad += 1
                line = text.count("\n", 0, m.start()) + 1
                print("MISSING %s:%d %s %r" % (f, line, m.group(1), name))
print(seen, "direct literals,", bad, "missing")
# Name-like tables: a literal that is not a file, whose '#' or '!' form is.
near = 0
for f in glob.glob("src/**/*.cpp", recursive=True):
    if "tier1" in f:
        continue
    text = re.sub(r"//[^\n]*", "", open(f, encoding="utf-8", errors="replace").read())
    for m in re.finditer(r'"([A-Za-z_][\w ]{2,})"', text):
        name = m.group(1).lower()
        for pre in "#!":
            if pre + name in anims or pre + name in models:
                if name not in anims and name not in models:
                    near += 1
                    line = text.count("\n", 0, m.start()) + 1
                    print("PREFIX %s:%d %r exists only as %r" % (f, line, m.group(1), pre + name))
print(near, "literals whose file has a prefix")
