"""The engine's interfaces as the game declares them (src/ls3d) against the LS3DF port's headers, which were tested against the original DLL:
slot by slot, the bytes of arguments. A slot declared with too few arguments breaks the caller's stack at run time (the game's first crash).

Slots the game leaves as placeholders are only counted; a placeholder is never called.

Usage: python vtable_audit.py [-v]
"""
import glob
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
GAME = [os.path.join(ROOT, "src", "ls3d", n) for n in ("i3d.h", "igraph.h")]
ENGINE = sorted(glob.glob(os.path.join(os.path.dirname(ROOT), "LS3DF", "src", "**", "*.h"), recursive=True))


# The game's names (inferred from its uses) of the engine's classes.
ALIASES = {
    "ISND_source": "ISND_sound",
    "ISND_sound": "ISND_source",
    "ISND_stream": "IShow",
    "IImage": "ITexture",
    "I3D_texture": "ITexture",
    "I3D_object_visual": "I3D_object",
    "I3D_lmap_visual": "I3D_lit_object",
    "I3D_singlemesh_visual": "I3D_singlemesh",
    "I3D_lensflare_visual": "I3D_lens",
    "I3D_particle_visual": "I3D_emitor",
    "I3D_projector_visual": "I3D_projector",
    "I3D_billboard_visual": "I3D_billboard",
    "I3D_joint_override": "I3D_pose",
    "I3D_joint_override_set": "I3D_pose_set",
    "I3D_sound_listener": "ISND_3Dlistener",
    "I3D_morph": "I3D_visual_morph",
}


def strip_comments(text):
    text = re.sub(r"/\*.*?\*/", " ", text, flags=re.S)
    return re.sub(r"//[^\n]*", " ", text)


def balanced(text, start):
    """The text inside the parentheses that open at `start`, and the index after them."""
    depth = 0
    for i in range(start, len(text)):
        if text[i] == "(":
            depth += 1
        elif text[i] == ")":
            depth -= 1
            if depth == 0:
                return text[start + 1:i], i + 1
    raise ValueError("unbalanced")


def arg_bytes(params):
    params = params.strip()
    if not params or params == "void":
        return 0
    depth = 0
    parts = [""]
    for c in params:
        if c in "(<":
            depth += 1
        elif c in ")>":
            depth -= 1
        if c == "," and depth == 0:
            parts.append("")
        else:
            parts[-1] += c
    total = 0
    for p in parts:
        p = re.sub(r"=.*$", "", p).strip()
        if "*" in p or "&" in p:
            total += 4
        elif re.match(r"(const\s+)?double\b", p) or re.match(r"(const\s+)?(u?int64_t|__int64|long long)\b", p):
            total += 8
        elif re.match(r"(const\s+)?(S_vector|S_quat|S_matrix|S_vector2)\b", p):
            total += {"S_vector": 12, "S_quat": 16, "S_matrix": 64, "S_vector2": 8}[re.match(r"(?:const\s+)?(\w+)", p).group(1)]
        else:
            total += 4
    return total


def kinds(params):
    """f for a float by value, x for anything else, per 4 bytes' argument (the floats are what a wrong type would break silently)."""
    params = params.strip()
    if not params or params == "void":
        return ""
    depth = 0
    parts = [""]
    for c in params:
        if c in "(<":
            depth += 1
        elif c in ")>":
            depth -= 1
        if c == "," and depth == 0:
            parts.append("")
        else:
            parts[-1] += c
    return "".join("f" if re.match(r"(const\s+)?float\b(?!\s*[*&])", re.sub(r"=.*$", "", p).strip()) and "*" not in p and "&" not in p else "x" for p in parts)


def classes(paths):
    """{name: (base, [(name, bytes or None)])}. Only classes with at least one virtual."""
    found = {}
    for path in paths:
        text = strip_comments(open(path, encoding="utf-8", errors="replace").read())
        for m in re.finditer(r"[;{}\s](?:class|struct)\s+(?:LS3D_API\s+)?(\w+)(?:\s+final)?\s*(?::\s*(?:public\s+)?(\w+)[^{;]*)?\{", text):
            name, base = m.group(1), m.group(2)
            i = m.end()
            depth = 1
            body_start = i
            while depth and i < len(text):
                depth += {"{": 1, "}": -1}.get(text[i], 0)
                i += 1
            body = text[body_start:i]
            # Only the class's own level: nested bodies blanked.
            own = []
            depth = 0
            for c in body:
                if c == "}":
                    depth -= 1
                if depth == 0:
                    own.append(c)
                if c == "{":
                    depth += 1
            own = "".join(own)
            slots = []
            for s in re.finditer(r"I3DMETHOD\s*\(|I3D_SLOTS_4\s*\(([^)]*)\)|I3D_SLOT\s*\(\s*(\w+)\s*\)|\bvirtual\b", own):
                tok = s.group(0)
                if tok.startswith("I3DMETHOD"):
                    inner, after = balanced(own, s.end() - 1)
                    method = inner.split(",")[-1].strip()
                    result = inner.rsplit(",", 1)[0].strip()
                    j = own.index("(", after)
                    params, _ = balanced(own, j)
                    slots.append((method, arg_bytes(params), kinds(params), result == "float"))
                elif tok.startswith("I3D_SLOTS_4"):
                    slots += [("?" + a.strip(), None, "", False) for a in s.group(1).split(",")]
                elif tok.startswith("I3D_SLOT"):
                    slots.append(("?" + s.group(2), None, "", False))
                else:
                    j = own.index("(", s.end())
                    head = own[s.end():j]
                    if "__stdcall" not in head and "~" not in head:
                        method = head.split()[-1] if head.split() else "?"
                        params, _ = balanced(own, j)
                        slots.append((method + " (not stdcall)", arg_bytes(params), kinds(params), False))
                        continue
                    method = head.split()[-1] if head.split() else "?"
                    params, _ = balanced(own, j)
                    slots.append((method, arg_bytes(params), kinds(params), re.match(r"\s*float\b", head) is not None))
            if slots or (base and name not in found):
                found[name] = (base, slots)
    return found


def flat(all_classes, name):
    base, slots = all_classes[name]
    if base and base in all_classes:
        inherited = flat(all_classes, base)
        return inherited + slots
    return list(slots)


def main():
    verbose = "-v" in sys.argv
    game = classes(GAME)
    engine = classes(ENGINE)
    bad = checked = placeholders = 0
    for name in sorted(game):
        other_name = ALIASES.get(name, name)
        if other_name not in engine:
            print("%-24s not in the engine's headers (%d slots)" % (name, len(game[name][1])))
            continue
        ours, theirs = flat(game, name), flat(engine, other_name)
        if len(ours) > len(theirs):
            print("%-24s declares %d slots, the engine %d" % (name, len(ours), len(theirs)))
        for i, (method, size, ours_kinds, ours_float) in enumerate(ours):
            if size is None:
                placeholders += 1
                continue
            if i >= len(theirs):
                print("%-24s +0x%03X %s: past the engine's slots" % (name, i * 4, method))
                bad += 1
                continue
            other, other_size, other_kinds, other_float = theirs[i]
            checked += 1
            if other_size != size:
                print("%-24s +0x%03X %s %d bytes; engine %s %d bytes" % (name, i * 4, method, size, other, other_size))
                bad += 1
            elif other_float != ours_float:
                print("%-24s +0x%03X %s returns %s; engine %s returns %s" % (name, i * 4, method, "float" if ours_float else "no float", other, "float" if other_float else "no float"))
                bad += 1
            elif ours_kinds != other_kinds:
                print("%-24s +0x%03X %s arguments %s; engine %s %s (f: a float)" % (name, i * 4, method, ours_kinds, other, other_kinds))
                bad += 1
            elif verbose:
                print("%-24s +0x%03X %s = %s, %d" % (name, i * 4, method, other, size))
    print("%d slots compared, %d differ, %d placeholders" % (checked, bad, placeholders))


if __name__ == "__main__":
    main()
