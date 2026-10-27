"""Checks the setup harness's output against counts taken straight from the exported models.

    python check.py MODELS_TXT_DIR HARNESS_OUTPUT

Per model: exhausts, special volumes, doors ("DOOR<digit>" visuals) and their windows, seats and
their doors, wheels / axles, strut frames, body collision boxes and points, removed frames; the
damage parts by type, the lamps by kind (sides from the world x), shared projectors, the lamps'
parts, and that every object Setup made is released again.
"""
import fnmatch
import glob
import math
import os
import re
import sys

VIS, DUMMY = 1, 6


def load(path):
    frames = []
    for line in open(path):
        if line.startswith("f "):
            _, t, parent, name = line.rstrip("\n").split(" ", 3)
            frames.append({"type": int(t), "parent": int(parent), "name": name, "kids": [], "props": ""})
        elif line.startswith("L "):
            v = [float(x) for x in line.split()[1:]]
            frames[-1]["local"] = [v[0:4], v[4:8], v[8:12], v[12:16]]
        elif line.startswith("P "):
            h = line[2:].strip()
            frames[-1]["props"] = bytes.fromhex(h).decode("latin-1") if h != "-" else ""
    for i, f in enumerate(frames):
        if f["parent"]:
            frames[f["parent"] - 1]["kids"].append(i)
        lm = f["local"]
        if f["parent"]:
            pm = frames[f["parent"] - 1]["world"]
            f["world"] = [[sum(lm[r][k] * pm[k][c] for k in range(4)) for c in range(4)] for r in range(4)]
        else:
            f["world"] = lm
    return frames


def part_type(props):
    """AddVehiclePart's classification (no "+DEFORM" groups in the shipped cars)."""
    if len(props) <= 11:
        return 0
    i = props.find("[")
    if i < 0:
        return 0
    j = props.find("]", i + 1)
    if j < 0:
        return 0
    lst = props[i + 1:j]
    n = len(lst)
    if n < 10:
        return 0
    for word, t, need in (("DEFORM BODY", 1, 11), ("DEFORM BUMPER", 2, 13), ("DEFORM LIGHT", 5, 12),
                          ("DEFORM SPZ", 7, 10), ("DEFORM MIRROR", 4, 13), ("DEFORM WING", 6, 11),
                          ("DEFORM DOOR", 8, 11), ("DEFORM ROOF", 9, 11), ("DEFORM STEPNEY", 10, 14),
                          ("DEFORM GLASS", 3, 12)):
        if n >= need and lst.startswith(word):
            return t
    if lst.startswith("+DEFORM"):
        raise SystemExit("a +DEFORM group: extend check.py")
    return 0


def match(frames, pattern, types):
    return [i for i, f in enumerate(frames) if f["type"] in types and fnmatch.fnmatch(f["name"].upper(), pattern)]


def subtree(frames, i):
    out = []
    for k in frames[i]["kids"]:
        out.append(k)
        out += subtree(frames, k)
    return out


def expect(frames):
    e = {}
    e["exhausts"] = len(match(frames, "EXHAUST*", (VIS, DUMMY)))
    e["specials"] = len(match(frames, "SPECCOLL*", (VIS, DUMMY)))
    doors = [i for i in match(frames, "DOOR*", (VIS,)) if frames[i]["name"][4:5].isdigit()]
    e["doors"] = len(doors)
    e["windows"] = sum(1 for d in doors if any(frames[k]["type"] == VIS and fnmatch.fnmatch(
        frames[k]["name"].upper(), "WINDOW*") for k in subtree(frames, d)))
    seats = match(frames, "SEAT*", (DUMMY,))
    e["seats"] = len(seats)
    e["seat_doors"] = min(len(seats), len(doors))
    wheels = match(frames, "WH*", (VIS, DUMMY))
    e["wheels"] = len(wheels)
    axles = len(wheels) // 2
    axis = match(frames, "AXIS*", (VIS,))

    def num(name):
        m = re.match(r"[+-]?\d+", name[5:])
        return int(m.group(0)) if m else 0
    whole = [i for i in axis if frames[i]["name"][4:5] == "0" and num(frames[i]["name"]) < axles]
    side = [i for i in axis if i not in whole and frames[i]["name"][4:5] in "LR" and num(frames[i]["name"]) < axles]
    e["struts"] = (len(whole), len(side))
    boxes = match(frames, "BODYCOL*", (DUMMY,))
    e["boxes"] = len(boxes)
    e["points"] = sum(1 for b in boxes for k in frames[b]["kids"]
                      if frames[k]["type"] == DUMMY and not frames[k]["name"].upper().startswith("BODYCOL"))
    removed = e["exhausts"] + e["specials"] + e["boxes"] + e["points"]
    removed += len(match(frames, "BALLAST*", (DUMMY,)))
    for exact in ("HEAD", "CENTRE", "SMOKE_MOTOR"):
        removed += 1 if any(f["type"] == DUMMY and f["name"].upper() == exact for f in frames) else 0
    e["removed"] = removed

    parts = {}
    for i, f in enumerate(frames):
        if f["type"] == VIS:
            t = part_type(f["props"])
            if t:
                parts[i] = t
    e["parts"] = tuple(sum(1 for t in parts.values() if t == k) for k in range(1, 11))
    e["glass"] = sum(1 for t in parts.values() if t == 3)
    lamps = []
    for prefix, kind, sided in (("LIGHTRE", 7, False), ("LIGHTIN", 1, True), ("LIGHTBI", 3, True),
                                ("LIGHTLI", 5, True), ("LIGHTBR", 8, False), ("LIGHTBA", 9, False)):
        for i in match(frames, prefix + "*", (VIS, DUMMY)):
            k = kind + 1 if sided and frames[i]["world"][3][0] > 0 else kind
            lamps.append((i, k))
    e["lamps"] = tuple(sum(1 for _, k in lamps if k == n) for n in range(1, 10))
    if any(frames[i]["props"].count("[") for i, _ in lamps):
        raise SystemExit("a lamp with its own projector: extend check.py")
    projectors = 0
    lamp_frames = [i for i, _ in lamps]
    for p in match(frames, "PROJECTOR*", (DUMMY,)):
        below = [k for k in subtree(frames, p) if frames[k]["type"] in (VIS, DUMMY)
                 and fnmatch.fnmatch(frames[k]["name"].upper(), "LIGHT*")]
        if len(below) == 2 and all(k in lamp_frames for k in below) and below[0] != below[1] \
                and frames[p]["props"].count(",") >= 2:
            projectors += 1
    e["projectors"] = projectors
    e["shared"] = 2 * projectors
    linked = 0
    for i, _ in lamps:
        if parts.get(i) == 5:
            linked += 1
            continue
        a = frames[i]["parent"]
        while a:
            if (a - 1) in parts:
                linked += 1
                break
            a = frames[a - 1]["parent"]
    e["with_part"] = linked
    e["released"] = True
    e["strut_geometry"] = strut_geometry(frames, whole, side)
    e["reset"] = 2 * projectors
    return e


def props_vector(props):
    """ReadPropsVector: up to three numbers after the first '[' (sscanf), in cm."""
    v = [0.0, 0.0, 0.0]
    i = props.find("[")
    if i < 0 or len(props) <= 2:
        return v
    for n, tok in enumerate(props[i + 1:].replace("]", " ").split()[:3]):
        m = re.match(r"[+-]?(\d+\.?\d*|\.\d+)([eE][+-]?\d+)?", tok)
        if not m:
            break
        v[n] = float(m.group(0)) * 0.01
    return v


def strut_geometry(frames, whole, side):
    """UpdateStruts at setup, recomputed from the frames: per strut (by name) its place, z scale
    and direction (None: its wheel is missing and the port leaves it)."""
    def wheel(letter, axle):
        for i, f in enumerate(frames):
            n = f["name"].upper()
            if f["type"] in (VIS, DUMMY) and n.startswith("WH") and n[2:3] == letter:
                m = re.match(r"[+-]?\d+", n[3:])
                if (int(m.group(0)) if m else 0) == axle:
                    return i
        return None

    def wheel_pos(i):
        p = frames[i]["local"][3][:3] + [1.0]
        h = frames[frames[i]["parent"] - 1]["local"]
        return [sum(p[k] * h[k][c] for k in range(4)) for c in range(3)]

    def unit(v):
        sq = sum(x * x for x in v)
        if abs(sq - 1.0) < 1e-8:
            return v
        if sq >= 1e-8:
            s = 1.0 / sq ** 0.5
            return [x * s for x in v]
        return None                                     # not in the shipped models

    def num(name):
        m = re.match(r"[+-]?\d+", name[5:])
        return int(m.group(0)) if m else 0

    out = {}
    for s in side:
        f = frames[s]
        o = props_vector(f["props"])
        letter = f["name"][4].upper()
        if letter == "R":
            o[0] = -o[0]
        w = wheel(letter, num(f["name"]))
        if w is None:
            out[f["name"]] = None
            continue
        p = wheel_pos(w)
        d = [o[k] + p[k] - f["local"][3][k] for k in range(3)]
        out[f["name"]] = (tuple(f["local"][3][:3]), sum(x * x for x in d) ** 0.5, tuple(unit(d)))
    for s in whole:
        f = frames[s]
        o = props_vector(f["props"])
        left, right = wheel("L", num(f["name"])), wheel("R", num(f["name"]))
        if left is None or right is None:
            out[f["name"]] = None
            continue
        pr, pl = wheel_pos(right), wheel_pos(left)
        start = [-o[0] + pr[0], o[1] + pr[1], o[2] + pr[2]]
        end = [o[k] + pl[k] for k in range(3)]
        hl = frames[frames[left]["parent"] - 1]["local"][3]
        hr = frames[frames[right]["parent"] - 1]["local"][3]
        d = [hl[k] - hr[k] for k in range(3)]
        out[f["name"]] = (tuple(start), sum((end[k] - start[k]) ** 2 for k in range(3)) ** 0.5, tuple(unit(d)))
    return out


def close(a, b, eps=2e-3):
    if isinstance(a, (tuple, list)):
        return len(a) == len(b) and all(close(x, y, eps) for x, y in zip(a, b))
    return abs(a - b) <= eps * max(1.0, abs(a))


def struts_match(want, got):
    if set(want) != set(got):
        return False
    for name, w in want.items():
        pos, scale, d = got[name]
        if w is None:
            if d != (0.0, 0.0, 0.0):
                return False
            continue
        if not (close(pos, w[0]) and close(scale[2], w[1]) and close(scale[:2], (1.0, 1.0)) and close(d, w[2])):
            return False
    return True


def parse(text):
    r = {}
    for block in text.split("== ")[1:]:
        name = block.split()[0].replace(".4ds", "")
        g = {}
        m = re.search(r"exhausts=(\d+) specials=(\d+)", block)
        g["exhausts"], g["specials"] = int(m.group(1)), int(m.group(2))
        m = re.search(r"doors=(\d+):([^\n]*)", block)
        g["doors"] = int(m.group(1))
        g["windows"] = m.group(2).count("+w")
        m = re.search(r"seats=(\d+):([^\n]*)", block)
        g["seats"] = int(m.group(1))
        g["seat_doors"] = len(re.findall(r"\((?!-\))", m.group(2)))
        g["wheels"] = int(re.search(r"wheels=(\d+)", block).group(1))
        m = re.search(r"struts axle=(\d+) side=(\d+)", block)
        g["struts"] = (int(m.group(1)), int(m.group(2)))
        m = re.search(r"bodycol boxes=(\d+) \(B \d+\) points=(\d+)", block)
        g["boxes"], g["points"] = int(m.group(1)), int(m.group(2))
        g["removed"] = int(re.search(r"removed frames=(\d+)", block).group(1))
        g["ok"] = "setup=1" in block.split("\n")[0]
        m = re.search(r"parts=\d+:((?: \d+){10}) grouped=\d+ glass-own-material=(\d+) meshes made=(\d+)", block)
        g["parts"] = tuple(int(x) for x in m.group(1).split())
        g["glass"] = int(m.group(2))
        m = re.search(r"lamps=\d+:((?: \d+){9}) shared=(\d+) with-part=(\d+) projectors=(\d+)", block)
        g["lamps"] = tuple(int(x) for x in m.group(1).split())
        g["shared"], g["with_part"], g["projectors"] = int(m.group(2)), int(m.group(3)), int(m.group(4))
        m = re.search(r"made frames \d+ \(live (\d+)\), images \d+ \(live (\d+)\), materials (\d+)/(\d+)", block)
        g["released"] = m.group(1) == "0" and m.group(2) == "0" and m.group(3) == m.group(4)
        g["strut_geometry"] = {}
        for m in re.finditer(r"strut (\S+) pos (\S+) (\S+) (\S+) scale (\S+) (\S+) (\S+) dir (\S+) (\S+) (\S+)", block):
            v = [float(x) for x in m.groups()[1:]]
            g["strut_geometry"][m.group(1)] = (tuple(v[0:3]), tuple(v[3:6]), tuple(v[6:9]))
        m = re.search(r"reset: unlinked (\d+) \(expected (\d+)\) effects stopped (\d+) engine sounds stopped (\d+) "
                      r"volume calls (\d+)", block)
        # The projectors' (no shipped lamp has its own); nothing else touched, the sounds stopped once.
        g["reset"] = int(m.group(1)) if m.group(3) == "0" and m.group(4) == "1" and m.group(5) == "0" else -1
        r[name] = g
    return r


def farthest(lo, hi):
    """The farthest of the box's corners from the origin."""
    return max(sum(c * c for c in (x, y, z)) ** 0.5 for x in (lo[0], hi[0]) for y in (lo[1], hi[1])
               for z in (lo[2], hi[2]))


def floats(text):
    return [float(x) for x in re.findall(r"-?\d+\.\d+(?:e[+-]?\d+)?", text)]


def check_start(block):
    """Start's volumes, turning circle, axle middles and the seat load, recomputed from the
    harness's own printout of their inputs. Returns a list of what differs."""
    bad = []
    lines = block.split("\n")
    get = lambda prefix: [l for l in lines if l.startswith(prefix)]
    box = floats(get("start body box")[0])
    lo, hi = box[0:3], box[3:6]
    body = floats(get("start body volume")[0])
    want = [farthest(lo, hi) + 0.1] + [c + 0.1 for c in lo] + [c - 0.1 for c in hi]
    if not close(tuple(body[:7]), tuple(want)):
        bad.append(("body volume", want, body))
    low = [lo[0], lo[1] - 0.5, lo[2]]
    second = floats(get("start second volume")[0])
    want = [farthest(low, hi) + 0.1] + low + hi
    if not close(tuple(second[:7]), tuple(want)):
        bad.append(("second volume", want, second))
    corners = [floats(l) for l in get("start special corner")]
    specials = [floats(l) for l in get("start special volume")]
    if len(specials) * 2 != len(corners):
        bad.append(("special volumes", len(corners) // 2, len(specials)))
    for k, vol in enumerate(specials):
        c0, c1 = corners[2 * k], corners[2 * k + 1]
        want = [farthest(c0, c1) + 0.1] + c0 + c1
        if not close(tuple(vol), tuple(want)):
            bad.append(("special volume", k, want, vol))

    turning = get("start turning")[0]
    t6e8, t6ec, steer = floats(turning)[:3]
    f74 = int(re.search(r"f74 ([0-9A-F]+)", turning).group(1), 16)
    wheels = {}
    for l in get("start wheel"):
        m = re.match(r"start wheel (\d+) model (\S+) (\S+) (\S+) flags ([0-9A-F]+) skids (\d+)", l)
        wheels[int(m.group(1))] = ([float(m.group(k)) for k in (2, 3, 4)], int(m.group(5), 16), int(m.group(6)))
    n = int(re.search(r"wheels=(\d+)", block).group(1))
    if not f74 & 2 and n >= 2 and 0 in wheels and n - 2 in wheels:
        track = 0.0
        for i in range(1, n):
            if i in wheels and i - 1 in wheels:
                track = max(track, wheels[i][0][0] - wheels[i - 1][0][0])
        front, back = wheels[0], wheels[n - 2]
        a1 = steer if front[1] & 1 else 1.5707964
        a2 = steer if back[1] & 1 else 1.5707964
        reach = math.sin(a2) * math.sin(a1) * (front[0][2] - back[0][2]) / math.sin(math.pi - (a1 + a2))
        want = (track + reach) * 1.2
        if not close((t6e8, t6ec), (want, want)):
            bad.append(("turning", want, t6e8, t6ec))
    if n > 1 and all(i in wheels for i in (0, 1, n - 2, n - 1)):
        mid = lambda a, b: [(b[k] - a[k]) * 0.5 + a[k] for k in range(3)]
        axles = floats(get("start axles")[0])
        want = mid(wheels[0][0], wheels[1][0]) + mid(wheels[n - 2][0], wheels[n - 1][0])
        if not close(tuple(axles), tuple(want)):
            bad.append(("axles", want, axles))
    if any(w[2] != 50 for w in wheels.values()):
        bad.append(("skid marks per wheel", [w[2] for w in wheels.values()]))

    com_line = get("start com")[0]
    com, com1e0 = floats(com_line)[0:3], floats(com_line)[3:6]
    inv = [float(x) for x in re.search(r"inv \((\S+) (\S+) (\S+)\)", com_line).groups()]
    inv220 = [float(x) for x in re.search(r"inv220 \((\S+) (\S+) (\S+)\)", com_line).groups()]
    mass = float(re.search(r"mass (\S+)", com_line).group(1))
    if not close(tuple(com1e0), tuple(com)) or not close(tuple(inv220), tuple(inv)):
        bad.append(("unloaded centre of mass", com, com1e0, inv, inv220))
    load = get("load seat")
    if load:
        seat = floats(load[0])[0:3]
        got1e0 = floats(load[0])[3:6]
        got220 = [float(x) for x in re.search(r"inv220 \((\S+) (\S+) (\S+)\)", load[0]).groups()]
        share = 80.0 / (80.0 + mass)
        shift = [(seat[k] - com[k]) * share for k in range(3)]
        want1e0 = [com[k] + shift[k] for k in range(3)]
        d2 = sum(s * s for s in shift)
        want220 = [1.0 / (1.0 / inv[k] + d2 * 80.0) for k in range(3)]
        if not close(tuple(got1e0), tuple(want1e0)) or not close(tuple(got220), tuple(want220), 1e-3):
            bad.append(("seat load", want1e0, got1e0, want220, got220))
    # The drive test (the real drivetrain, solver and wheel contacts over a flat ground, Setup's
    # default block):
    # every gear up in order at full throttle, the revs within the limit, a stop on the brake (Step's
    # standstill, under 0.5 m/s) and then reverse (the automatic box); the body upright, its height
    # steady, barely sliding or turning under power (on locked brakes a car may spin: la_panther),
    # the wheels casting their rays (the ground found every step). A
    # three-wheeler (la_BmwR75) on the default block leans onto its tipping line and rolls over in
    # the toy world: only that it runs.
    drive, body = get("drive:"), get("drive body:")
    wheels_line = get("ballasts=")
    real_wheels = 4
    if wheels_line:
        real_wheels = len(re.findall(r" WH\w+\(r[^)]*\)", wheels_line[0])) - wheels_line[0].count(" dummy")
    if drive and body:
        m = re.match(r"drive: gears (-?\d+)\.\.(-?\d+) of (\d+) downshifts (\d+) max rpm (\S+) limit (\S+) top (\S+) m/s "
                     r"fuel used (\S+) stop step (-?\d+) reverse step (-?\d+) back (\S+) m/s", drive[0])
        low, high, count, down = (int(m.group(k)) for k in (1, 2, 3, 4))
        rpm, limit, top, fuel = (float(m.group(k)) for k in (5, 6, 7, 8))
        stop, reverse, back = int(m.group(9)), int(m.group(10)), float(m.group(11))
        b = re.match(r"drive body: upright min (\S+) side max (\S+) m/s turn max (\S+) deg \(accelerating (\S+) m/s (\S+) deg\) "
                     r"height (\S+)\.\.(\S+) rays (\d+) collisions (\d+) steps (\d+)", body[0])
        upright, side, turn, side_acc, turn_acc, low_y, high_y = (float(b.group(k)) for k in range(1, 8))
        rays, steps = int(b.group(8)), int(b.group(10))
        if real_wheels >= 4:
            if not (low == 1 and high == count and down == 0 and rpm <= limit + 0.1 and top > 20.0 and fuel > 0.0
                    and 480 <= stop <= reverse and back < -1.0):
                bad.append(("drive", drive[0]))
            # Under power a car drifts (the drive is split by the wheels' grip: an off-centre load or
            # a wheel a little larger or wider pulls; up to 16° over the 40 s, 1.2 km: la_krupp; the
            # Opel's 6° drops to 1° with its mass centred, DRIVE_SYMMETRIC=1); a wrong sign spins it.
            if not (upright > 0.95 and side_acc < 0.5 and turn_acc < 20.0 and -0.3 < low_y and high_y < 0.3
                    and rays >= steps):
                bad.append(("drive body", body[0]))
        elif rays < steps:
            bad.append(("drive body", body[0]))
    else:
        bad.append(("drive", "missing"))
    # The body collision (the real CollideBody in the stub world). On the flat ground no body point
    # meets anything (the boxes stay clear of it; impulses would sound). Into the wall: met at speed
    # (over 5 m/s), stopped within a second (under a third of that), the points' spheres at most
    # 0.5 m in (a car's 0.1 m; tracked vehicles climb the wall under the harness's strong drive,
    # la_krupp's sphere boxes let it creep in and the late push-out bring it back), standing at
    # the wall for most of the rest, dents and impact sounds, the own volumes ignored and freed
    # in pairs. The three-wheeler as above: only that it runs.
    collide, crash = get("drive collide:"), get("crash:")
    if collide and crash:
        c = re.match(r"drive collide: box tests (\d+) touched (\d+) sphere tests (\d+) impact sounds (\d+) "
                     r"ignores (\d+)/(\d+)", collide[0])
        k = re.match(r"crash: first contact step (-?\d+) impact (\S+) m/s peak (\S+) m/s past wall max (\S+) "
                     r"\(points (\S+)\) upright min (\S+) stopped steps (\d+) of (\d+) collisions (\d+) box tests (\d+) "
                     r"touched (\d+) sphere tests (\d+) deforms (\d+) \(part hits (\d+)\) impact sounds (\d+) ignores (\d+)/(\d+)",
                     crash[0])
        first, impact, points = int(k.group(1)), float(k.group(2)), float(k.group(5))
        stopped, after = int(k.group(7)), int(k.group(8))
        hits, sounds = int(k.group(14)), int(k.group(15))
        tracked = re.search(r"crash stop: .* tracked 1", block) is not None
        if c.group(5) != c.group(6) or k.group(16) != k.group(17):
            bad.append(("collide ignores", collide[0], crash[0]))
        if real_wheels >= 4:
            if int(c.group(4)) != 0:
                bad.append(("drive collide", collide[0]))
            if not (first >= 0 and impact > 5.0 and points < 0.5 and stopped * 2 > after and (hits > 0 or tracked)
                    and sounds > 0):
                bad.append(("crash", crash[0]))
        stop = get("crash stop:")
        if real_wheels >= 4 and stop:
            s = re.match(r"crash stop: (\S+) m/s a second after the first contact, tracked (\d)", stop[0])
            sphere_boxes = re.search(r"bodycol boxes=\d+ \(B 0\)", block) is not None
            if not abs(float(s.group(1))) < impact / 3.0 or (
                    s.group(2) == "0" and not sphere_boxes and not points < 0.1):
                bad.append(("crash stop", stop[0], crash[0]))
        elif real_wheels >= 4:
            bad.append(("crash stop", "missing"))
    else:
        bad.append(("crash", "missing"))
    # The lamps (the real UpdateLamps): per switch setting exactly the lamps of the kinds it lights
    # lit (billboard, projected and spot light), each placed and aimed at its lamp or PROJECTOR
    # (also with the headlights relabelled as reversing lights and flashers, "*"); the left
    # indicator blinking 0.4 s on, 0.3 s off from off (50 ms updates); a flasher alternating
    # every 0.35 s (its period 0.3 s passed at 50 ms steps); all dark after Stop.
    scenes = get("lamps ")
    for l in scenes:
        m = re.match(r"lamps \w+\*?: lit (\d+) expected (\d+) wrong (\d+) misplaced (\d+)", l)
        if m and not (m.group(1) == m.group(2) and m.group(3) == "0" and m.group(4) == "0"):
            bad.append(("lamps", l))
        m = re.match(r"lamps blink: (\S*) flash: (\S*)", l)
        if m and m.group(1) not in ("", ".....########......########......#######"):
            bad.append(("lamps blink", l))
        m = re.match(r"lamps flash\*: (\S*)", l)
        if m and m.group(1) not in ("", "......#######.......#######.......######"):
            bad.append(("lamps flash", l))
    if len([l for l in scenes if re.match(r"lamps \w+\*?:", l)]) < 7:
        bad.append(("lamps", "missing"))
    stop = get("stop:")
    if not stop or "lamp objects on 0 " not in stop[0]:
        bad.append(("stop lamps", stop))
    # The sounds: every slot released and unlinked with the lamps' lights (ReleaseSounds).
    m = stop and re.search(r"sounds left (\d+) unlinked (\d+) \(expected (\d+)\)", stop[0])
    if not m or m.group(1) != "0" or m.group(2) != m.group(3):
        bad.append(("stop sounds", stop))
    # The sounds' loading (SoundTest): every slot as the original sets it up and in its order, the
    # rolling sounds skipped without a circumference, kept, changed, emptied and missing names, two
    # sectors, ReleaseSounds.
    m = get("sounds load:")
    if m != ["sounds load: slots 81 of 81 order 1 rolling skipped 2 others 79 kept 1 changed 1 emptied 1 missing 1/1"]:
        bad.append(("sounds", m))
    m = get("sounds sectors:")
    m = m and re.match(r"sounds sectors: added (\d+) removed \1 of \1 none 1 released (\d+) of \2 empty 1 flags 2 ambient 1$", m[0])
    if not m:
        bad.append(("sounds", get("sounds sectors:")))
    # The engine sounds: every checked layer, idle and single loop as worked out apart, the
    # layers alone in the single loop's mode, the volume scale put back after every call.
    m = get("engine sounds:")
    m = m and re.match(r"engine sounds: layers (\d+) of \1 idle (\d+) of \2 single (\d+) of \3 untouched 1 "
                       r"volume kept (\d+) of \4 switch 1 off 1$", m[0])
    if not m or min(int(m.group(k)) for k in (1, 2, 3, 4)) == 0:
        bad.append(("engine sounds", get("engine sounds:")))
    if get("engine start:") != ["engine start: started 1 waiting 1 running 1 short 1 wrapped 1 blocked 1 group 1 1 "
                                "silent 1 stopped 1 1 1 not started 1"]:
        bad.append(("engine start", get("engine start:")))
    m = get("engine one-offs:")
    m = m and re.match(r"engine one-offs: fail 1 gear (\d+) of \1 second ([1-9]\d*) no detail 1 blocked 1 "
                       r"loops stopped 1 sounds stopped 1$", m[0])
    if not m:
        bad.append(("engine one-offs", get("engine one-offs:")))
    # The running sounds (rolling, rim, squeal) as worked out apart; the one-offs.
    m = get("running sounds:")
    m = m and re.match(r"running sounds: rolling (\d+) of \1 rim (\d+) of \2 untouched 1 rim off 1 squeal (\d+) of \3 "
                       r"squeal sounds (\d+) of \4$", m[0])
    if not m or min(int(m.group(k)) for k in (1, 2, 3, 4)) == 0:
        bad.append(("running sounds", get("running sounds:")))
    if get("running one-offs:") != ["running one-offs: handbrake 1 loops 1 burst 1"]:
        bad.append(("running one-offs", get("running one-offs:")))
    m = get("hit sounds:")
    m = m and re.match(r"hit sounds: impact (\d+) of \1 rules 1 bump 1 door 1$", m[0])
    if not m or m.group(1) == "0":
        bad.append(("hit sounds", get("hit sounds:")))
    # The car (part 2a): the creator, the constructor, SetFrame, the properties, CopyFrom, the parts'
    # release, the list, the timer.
    if get("car:") != ["car: named 1 made 1 frame 1 refused 1 properties 1 copy 1 released 1 list 1 timer 1"]:
        bad.append(("car", get("car:")))
    # The seats (part 2b): the ways out, seating, leaving and getting out, the camera dummies, the
    # properties, the seat weapons' slots, the network's bookkeeping; as many as Setup found (the
    # report's seats=, compared with the model's SEAT dummies above).
    count = re.search(r"seats=(\d+):", block)
    if not count or get("seats:") != [f"seats: {count.group(1)} entry 1 blocked 1 occupy 1 leave 1 exit 1 camera 1 "
                                      "props 1 slots 1 net 1"]:
        bad.append(("seats", get("seats:")))
    # The car's controls and updates (part 2c): the controls against the setters, IsDrivable and the
    # environment flags, the engine's noise, the water, the detail, Update, Tick, the checkpoint
    # marks, the timer and the messages.
    if get("car update:") != ["car update: controls 1 state 1 water 1 detail 1 update 1 tick 1 marks 1 timer 1"]:
        bad.append(("car update", get("car update:")))
    # The vehicle's callbacks (part 2d-i): every branch as the original's, the messages read back.
    if get("callbacks:") != ["callbacks: response 1 impulse 1 touch 1 firm 1 impact 1 part off 1 wheel off 1 glass 1 "
                             "body ground 1 wheel ground 1 messages 1"]:
        bad.append(("callbacks", get("callbacks:")))
    # The car's contacts, hits, wreck, charges, ray and crew queries (part 2d-ii).
    if get("car hits:") != ["car hits: messages 1 simulated 1 collision 1 tyre 1 hit 1 wreck 1 charges 1 ray 1 queries 1"]:
        bad.append(("car hits", get("car hits:")))
    # The car's network side (part 2f-i): the messages, the server's and a client's, the seat
    # weapons', the item store, the replication, the state handlers, a client's wreck.
    if get("car net:") != ["car net: messages 1 server 1 client 1 weapons 1 items 1 replication 1 state 1 destroyed 1"]:
        bad.append(("car net", get("car net:")))
    # The damage parts' hits and effects (part 2g-ii).
    if get("car parts hits:") != ["car parts hits: place 1 material 1 hit 1 zone 1 explosive 1 seats 1 effects 1 wreck 1 car 1"]:
        bad.append(("car parts hits", get("car parts hits:")))
    # The car's damage parts (part 2g-i).
    if get("car parts:") != ["car parts: setup 1 props 1 reset 1 tick 1 save 1 load 1 quirks 1 names 1 none 1 release 1"]:
        bad.append(("car parts", get("car parts:")))
    # The car's saved game and a joining client's state (part 2f-ii).
    if get("car save:") != ["car save: save 1 load 1 quirks 1 state 1 read 1"]:
        bad.append(("car save", get("car save:")))
    # The car's helpers (part 1e).
    m = get("car helpers:")
    m = m and re.match(r"car helpers: camera 1 contact (\d+) of \1 beams (\d+) of \2 read 1 still 1 callback 1$", m[0])
    if not m or m.group(1) == "0" or m.group(2) == "0":
        bad.append(("car helpers", get("car helpers:")))
    # Update (the real one after each step): the doors opened and shut in the steps their speed
    # takes, with a sound each way, each frame turned by its angle, the free end out; the windows (none shipped) hidden at the bottom,
    # shown at the top; every wheel holder turned in by the toe at steering 0; the track strips
    # on their wheels and scrolled by the engine's speed; every drive step done; the measured wheel
    # rolling the distance travelled; sliding wheels laying skid marks; the bumps' cycles in range
    # and about 1.5 / 16.5 of the time bumped.
    doors, windows, toe, tracks, drive, bumps = (get(p) for p in ("doors:", "windows:", "toe:", "tracks:",
                                                                  "drive update:", "bumps:"))
    m = doors and re.match(r"doors: (\d+) open (\d+) in (\d+) steps shut (\d+) in (\d+) steps sounds (\d+)/(\d+) "
                           r"misturned (\d+) expected steps (\d+)/(\d+) outward (\d+)", doors[0])
    if not m:
        bad.append(("doors", doors))
    else:
        n = m.group(1)
        if not (m.group(2) == m.group(4) == m.group(6) == m.group(7) == m.group(11) == n and m.group(8) == "0" and
                m.group(3) == m.group(9) and m.group(5) == m.group(10)):
            bad.append(("doors", doors[0]))
    # (0.5 m at 0.3 m/s: 21 steps of 83 ms each way; the doors' own if any, else two made for the
    # test; the first stopped by its part's state 1.)
    m = windows and re.match(r"windows: (\d+) down in (\d+) steps hidden (\d+) up in (\d+) steps shown (\d+) "
                             r"stopped (\d+)", windows[0])
    has_doors = doors and not doors[0].startswith("doors: 0 ")
    if not m or not (m.group(1) == m.group(3) == m.group(5) and
                     (m.group(1) != "0" and m.group(2) == m.group(4) == "21" and m.group(6) == "1"
                      if has_doors else m.group(1) == "0")):
        bad.append(("windows", windows))
    m = toe and re.match(r"toe: \S+ rad wheels (\d+) off (\d+) in (\d+) out (\d+)", toe[0])
    if not m or not (int(m.group(1)) > 0 and m.group(2) == "0" and m.group(3) == m.group(1) and m.group(4) == "0"):
        bad.append(("toe", toe))
    tracked = re.search(r"tracks L=(\d+) R=(\d+)", block)
    m = tracks and re.match(r"tracks: vertices (\d+) off (\d+) strips (\d+) scrolled (\d+) refreshes (\d+) "
                            r"scroll error (\S+)", tracks[0])
    if not m or not tracked:
        bad.append(("tracks", tracks))
    elif int(tracked.group(1)) + int(tracked.group(2)) > 0:
        if not (int(m.group(1)) > 0 and m.group(2) == "0" and int(m.group(3)) > 0 and m.group(4) == m.group(3)
                and int(m.group(5)) > 0 and float(m.group(6)) < 0.05):
            bad.append(("tracks", tracks[0]))
    elif m.group(1) != "0" or m.group(5) != "0":
        bad.append(("tracks", tracks[0]))
    m = drive and re.match(r"drive update: done (\d+) idle (\d+) lost (\d+) spin wheel (-?\d+) rolled (\S+) m "
                           r"travelled (\S+) m skid marks (\d+) \(segments on (\d+)\)", drive[0])
    if not m:
        bad.append(("drive update", drive))
    else:
        rolled, travelled = float(m.group(5)), float(m.group(6))
        if m.group(1) != "780" or (
                real_wheels >= 4 and not (travelled > 1.0 and abs(rolled - travelled) < 0.06 * travelled + 0.2
                                          and int(m.group(7)) > 0 and int(m.group(8)) > 0)):
            bad.append(("drive update", drive[0]))
    m = bumps and re.match(r"bumps: samples (\d+) cycles (\d+) out of range (\d+) bumped share (\S+)", bumps[0])
    if not m or m.group(3) != "0" or (real_wheels >= 4 and not (int(m.group(2)) > 0 and
                                                               0.03 < float(m.group(4)) < 0.2)):
        bad.append(("bumps", bumps))
    # The effects (the real ones, the pool's emitters faked): a wheel's dust rolling, switched in
    # place to the skid (locked fast, spinning) and back (locked slowly), stopped slowly and freed,
    # taken again as it is (no copy), none in mission 8 or without VEHICLE_SIM_EFFECTS (the light
    # 0.5); every exhaust's started right and
    # stopped; the engine smoke, started again on the same emitter, its second, stopped; two body
    # contacts' right, none for a third, switched in place, stopped; the drive's dust and
    # exhausts; all freed after
    # Stop.
    wheel = get("effects wheel:")
    if not wheel or wheel[0] != "effects wheel: rolling 1 locked 1 spinning 1 slow locked 1 stopped 1 freed in 2 " \
                                "free 1 retaken 1 mission 8 1 off 1":
        bad.append(("effects wheel", wheel))
    exhausts = int(re.search(r"exhausts=(\d+)", block).group(1))
    exhaust = get("effects exhaust:")
    if not exhaust or exhaust[0] != "effects exhaust: %d started %d right %d stopped %d" % ((exhausts,) * 4):
        bad.append(("effects exhaust", exhaust))
    smoke = get("effects smoke:")
    if not smoke or smoke[0] != "effects smoke: first 1 again on the same 1 second 1 stopped 1":
        bad.append(("effects smoke", smoke))
    contact = get("effects contact:")
    if not contact or contact[0] != "effects contact: right 2 third none 1 switched 2 one left 1 off 1":
        bad.append(("effects contact", contact))
    m = get("drive effects:")
    m = m and re.match(r"drive effects: dust (\d+) skid (\d+) exhaust (\d+) \(emitter steps\) most taken (\d+) of 30", m[0])
    if not m or (real_wheels >= 4 and not (int(m.group(1)) > 0 and (int(m.group(3)) > 0) == (exhausts > 0))):
        bad.append(("drive effects", get("drive effects:")))
    # The skid marks (the real LaySkidMark): straight on, the 0.3 m segments end to end along the
    # path from the first contact, the corners flat; round a 3 m arc, the segments along it, each
    # joined at its corner's pivot, the corners shaped by the turn's gap; 16 m wrapping the
    # ring; ends past 5 m, off the
    # ground, on bare ground and water; turned back, a new mark.
    skids = [get(p) for p in ("skids straight:", "skids arc:", "skids end:")]
    if not get("skids: none"):
        m = skids[0] and re.match(r"skids straight: closed (\d+) right (\d+) contiguous (\d+) along (\d+) corners flat (\d+) of (\d+)",
                                  skids[0][0])
        if not m or not (int(m.group(1)) >= 9 and m.group(1) == m.group(2) == m.group(3) == m.group(4) and
                         m.group(5) == m.group(6)):
            bad.append(("skids straight", skids[0]))
        m = skids[1] and re.match(r"skids arc: closed (\d+) right (\d+) joined (\d+) turning (\d+) corners (\d+) "
                                  r"shaped (\d+) of (\d+)", skids[1][0])
        if not m or not (int(m.group(1)) >= 12 and m.group(1) == m.group(2) and
                         m.group(3) == m.group(4) == m.group(5) == m.group(6) == m.group(7)):
            bad.append(("skids arc", skids[1]))
        ring = get("skids ring:")
        m = ring and re.match(r"skids ring: (\d+) wrapped (\d) at (\d+) on (\d+)", ring[0])
        if not m or not (m.group(2) == "1" and m.group(4) == m.group(1)):
            bad.append(("skids ring", ring))
        if not skids[2] or skids[2][0] != "skids end: far 1 back 1 off ground 1 no marks 1 water 1":
            bad.append(("skids end", skids[2]))
        # (4 cm leaves a new segment, 6 cm stretches it, 38 cm closes it and stretches the next.)
        stretch = get("skids stretch:")
        if not stretch or stretch[0] != "skids stretch: still 1 stretched 1 closed 1":
            bad.append(("skids stretch", stretch))
    # The real Deform (DeformTest): a dent of every vertex in reach by one of the ten factors (several
    # used), nothing else moved, the original kept once (two levels, the rest dropped), the hit
    # callback; again within maxDent of it; repaired back; the least impulse; wear (health, once per
    # step, gone, glass, doors, bumpers, wheels, lamps, light parts); a synthetic group.
    if not get("deform: no parts"):
        m = get("deform dent:")
        m = m and re.match(r"deform dent: levels (\d) of (\d+) reach (\d+) of (\d+) moved (\d+) clamped 0 factors (\d+) "
                           r"off 0 other parts (\d+) out 0 originals (\d+) kept 1 refs 1 levels (\d) dropped (\d+) "
                           r"same 1 marked 1 hit 1 bits 1 health 1 refreshes 1$", m[0])
        if not m or not (int(m.group(3)) > 0 and m.group(3) == m.group(5) and
                         int(m.group(6)) >= min(int(m.group(3)), 4) and int(m.group(8)) == int(m.group(7)) + 1 and
                         m.group(9) == m.group(1) and int(m.group(10)) == max(0, int(m.group(2)) - 2)):
            bad.append(("deform dent", get("deform dent:")))
        m = get("deform again:")
        m = m and re.match(r"deform again: originals 1 refs 1 within (\d+) of (\d+) at the limit (\d+) grown (\d+)$", m[0])
        if not m or m.group(1) != m.group(2) or int(m.group(3)) == 0 or m.group(4) != m.group(2):
            bad.append(("deform again", get("deform again:")))
        m = get("deform repair:")
        m = m and re.match(r"deform repair: restored (\d+) of (\d+) originals 0 marked 0$", m[0])
        if not m or m.group(1) != m.group(2):
            bad.append(("deform repair", get("deform repair:")))
        fixed = {"deform strength:": ["deform strength: weak untouched 1 forced dented 1"],
                 "deform wear:": [r"deform wear: first 1 once 1 second 1 no wear 1 gone 1 lamps (\d+)/\1"],
                 "deform glass:": ["deform glass: cracked 1 shattered 1 silent 1", "deform glass: none"],
                 "deform door:": [r"deform door: windows ([1-9]\d*) broken \1 moved [01] gone 1", "deform door: none"],
                 "deform bumper:": ["deform bumper: passed 1", "deform bumper: none"],
                 "deform wheel:": ["deform wheel: weak 1 hit 1 once 1 far 1 lost 1"],
                 "deform lamps:": [r"deform lamps: hit (\d+) right \1 weak \1 out \1 shared (\d+) sides \2 quirk \2"],
                 "deform originals:": ["deform originals: users 1 by model 1 back 1 left 0"],
                 "deform light parts:": [r"deform light parts: (\d+) right \1"],
                 "deform group:": ["deform group: whole 1 half 1 third 1 last 1 gone 1 body 1 attached 1 kept 1 dent 1"]}
        for prefix, patterns in fixed.items():
            line = get(prefix)
            if not line or not any(re.fullmatch(p, line[0]) for p in patterns):
                bad.append((prefix[:-1], line))
    # The saved state (StateTest): the block's size and layout (the fixed part's fields at the
    # original's offsets), each record as saved; refused for another version; loaded back: every
    # field, lamp, dented part (the vertices in 2 cm steps), seat and wheel; a car on its roof back on
    # its wheels; the part keys; the packed places.
    save, load = get("state save:"), get("state load:")
    m = save and re.match(r"state save: saved 1 size (\d+) layout 0 header 0 lamps (\d+) off 0 parts (\d+) "
                          r"\(dented (\d+)\) off 0 seats (\d+) off 0 wheels (\d+) off 0$", save[0])
    k = load and re.match(r"state load: refused 1 loaded 1 fields (\d+) of \1 derived 1 lamps (\d+) parts (\d+) "
                          r"originals (\d+) seats (\d+) wheels (\d+) refreshed 1 reused (\d+)$", load[0])
    if not m or not k or m.group(3) != m.group(4) or k.group(2) != m.group(2) or k.group(3) != m.group(4) or \
            k.group(4) != m.group(4) or k.group(5) != m.group(5) or k.group(6) != m.group(6) or \
            k.group(7) != m.group(4):
        bad.append(("state", save, load))
    # Replace (LoadState's step after Reset): the motion back, the wheels placed, the skid marks in
    # the scene, the lamp objects in the model's parent, the controls released.
    r = get("state replace:")
    r = r and re.match(r"state replace: motion 1 wheels (\d+) of \1 skid frames (\d+) of \2 lamps (\d+) of \3 "
                       r"controls 1$", r[0])
    if not r:
        bad.append(("state replace", get("state replace:")))
    if get("state roof:") != ["state roof: rolled 1"] or get("state keys:") != ["state keys: 1 found 1 packed 20 of 20"]:
        bad.append(("state", get("state roof:"), get("state keys:")))
    # The car's collisions (HitTest): AddForce with each flag value; ApplyCarHit's motion (as
    # ApplyImpulse's and the arithmetic's), its wear and impact callback, its dent (as Deform's).
    hits = {"hit force:": "hit force: 4 of 4",
            "hit car:": "hit car: same 1 motion 1 direction 1 callbacks 1",
            "hit wear:": "hit wear: wear 1 impact 1 args 1 tiny 1",
            "hit dent:": "hit dent: no parts" if get("deform: no parts") else "hit dent: same 1 repaired 1"}
    for prefix, expected in hits.items():
        if get(prefix) != [expected]:
            bad.append(("hit", get(prefix)))
    # Restart after a teleport (the drive's state scrambled, the car moved and turned): no mismatch,
    # the spin checked on every real wheel when repaired (none unrepaired: their wheels stay out of
    # the solver and keep their turn), every lamp object in the model's parent, the volumes in; from
    # 10 m/s the car coasts on along its new heading (0.9..1 of the speed, under 8 % sideways).
    restarts = get("restart speed")
    ok = len(restarts) == 3
    for line, (speed, repair) in zip(restarts, (("0.0", "0"), ("-6.0", "1"), ("10.0", "1"))):
        m = re.match(r"restart speed (\S+) repair (\d): place 0 motion 0 controls 0 wheels 0 contacts 0 holders 0 "
                     r"repair 0 spin 0 of (\d+) lamps (\d+) of (\d+) volumes 1 coast (\S+) side (\S+)$", line)
        ok = ok and bool(m) and m.group(1) == speed and m.group(2) == repair and m.group(4) == m.group(5) and \
            int(m.group(3)) == (real_wheels if repair == "1" else 0) and \
            (speed != "10.0" or (0.9 < float(m.group(6)) < 1.0 and float(m.group(7)) < 0.08))
    if not ok:
        bad.append(("restart", restarts))
    m = get("stop effects:")
    m = m and re.match(r"stop effects: let go (\d+) freed in (\d+) ticks active 0", m[0])
    if not m or int(m.group(2)) > 5:
        bad.append(("stop effects", get("stop effects:")))
    return bad


def c_atoi(text):
    """C's atoi: leading white space, a sign, the digits (0 without any)."""
    m = re.match(r"[ \t\n\v\f\r]*([+-]?\d+)", text)
    return int(m.group(1)) if m else 0


def check_mission(frames, block):
    """Part 2e: the mission's checks, and each seat as StartMission reads its dummy: a bit per
    character of a "SEAT<digits>" property (up to a comma or space), the text after "name" (-1
    without), a weapon for flag 0x100, a camera keeper for a "camera3RD*" dummy under it unless
    flag 0x20 or 0x100."""
    bad = []
    line = re.search(r"^mission: .*$", block, re.M)
    if not line or line.group(0) != "mission: start 1 vehicle 1 seats 1 wreck 1 restore 1 restart 1 stop 1 net 1":
        bad.append(("mission", line and line.group(0)))
    seats = re.search(r"^mission seats:(.*)$", block, re.M)
    got = dict(item.split("=", 1) for item in seats.group(1).split()) if seats else None
    want = {}
    for i in match(frames, "SEAT*", (DUMMY,)):
        props = frames[i]["props"].split("\0", 1)[0]
        flags = 0
        if props.startswith("SEAT"):
            for c in props[4:]:
                if c in ", ":
                    break
                flags |= 1 << ((ord(c) - 48) & 31)
        at = props.find("name")
        text = c_atoi(props[at + 4:]) if at >= 0 else -1
        camera = any(frames[k]["type"] == DUMMY and fnmatch.fnmatch(frames[k]["name"].upper(), "CAMERA3RD*")
                     for k in subtree(frames, i))
        want[frames[i]["name"]] = (f"{flags:x}/{text}/{1 if flags & 0x100 else 0}/"
                                   f"{1 if camera and not flags & 0x120 else 0}")
    if got != want:
        bad.append(("mission seats", want, got))
    return bad


def check_parts(models, name, frames, block):
    """Part 2g-i: the damage model ("DM" + the model, exported next to the models as dm4ds) as
    Gather reads it: "HOR" / "VERT" frames turn, "D_ZONE" zones, "EXPL_D" explosive parts, the seats'
    "SEAT<nn>" visuals, the "particle" dummy's PFIRE (else 0x60), PSMOKE (else 0x5F) and SID."""
    path = os.path.join(os.path.dirname(os.path.abspath(models)), "dm4ds", "DM" + name + ".txt")
    dm = load(path) if os.path.exists(path) else []
    zones = explosives = turrets = 0
    for f in dm:
        if "HOR" in f["name"] or "VERT" in f["name"]:
            turrets += 1
        elif "D_ZONE" in f["name"]:
            zones += 1
        elif "EXPL_D" in f["name"]:
            explosives += 1
    seats = len(match(frames, "SEAT*", (DUMMY,)))
    boxes = sum(1 for s in range(seats) if any(f["type"] == VIS and f["name"].upper() == "SEAT%02d" % s for f in dm))
    particle = next((f for f in dm if f["type"] == DUMMY and f["name"].upper() == "PARTICLE"), None)

    def prop(key):
        if particle is None or key not in particle["props"]:
            return -1
        return c_atoi(particle["props"][particle["props"].find(key) + len(key):])
    fire = prop("PFIRE")
    smoke = prop("PSMOKE")
    want = (f"car parts model: dm {1 if dm else 0} zones {zones} explosives {explosives} turrets {turrets} "
            f"hitboxes {boxes} fire {0x60 if fire == -1 else fire} smoke {0x5F if smoke == -1 else smoke} "
            f"sound {prop('SID')}")
    line = re.search(r"^car parts model: .*$", block, re.M)
    return [] if line and line.group(0) == want else [("car parts model", want, line and line.group(0))]


def main():
    models, output = sys.argv[1], sys.argv[2]
    text = open(output).read()
    got = parse(text)
    # The car effects' pool (effects_car.def's 22 effects, 19 on hits.tga): 30 emitters on effect 1,
    # its texture, named, its properties, a key per effect of the texture, its own selected, free.
    pool = re.search(r"effects pool: .*", text)
    if not ("effects init: 1" in text and pool and pool.group(0) ==
            "effects pool: definitions 22 emitters 30 on effect 1 30 keys 30 (19 each) selected 30 properties 30 "
            "named 30 idle 30"):
        print("effects pool", pool and pool.group(0))
        return
    start_bad, blocks = {}, {}
    for block in text.split("== ")[1:]:
        start_bad[block.split()[0].replace(".4ds", "")] = check_start(block)
        blocks[block.split()[0].replace(".4ds", "")] = block
    bad = 0
    for path in sorted(glob.glob(os.path.join(models, "*.txt"))):
        name = open(path).readline().split()[1].replace(".4ds", "")
        frames = load(path)
        e = expect(frames)
        g = got.get(name)
        if g is None:
            print(name, "missing from the output")
            bad += 1
            continue
        diffs = [(k, e[k], g[k]) for k in e if k != "strut_geometry" and e[k] != g[k]]
        if not struts_match(e["strut_geometry"], g["strut_geometry"]):
            diffs.append(("strut_geometry", e["strut_geometry"], g["strut_geometry"]))
        diffs += start_bad.get(name, [("start", "missing")])
        diffs += check_mission(frames, blocks.get(name, ""))
        diffs += check_parts(models, name, frames, blocks.get(name, ""))
        if not g["ok"]:
            diffs.append(("setup", True, False))
        if diffs:
            bad += 1
            print(name, diffs)
    print(len(got), "models,", bad, "with differences")


main()
