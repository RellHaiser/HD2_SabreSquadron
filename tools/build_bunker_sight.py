"""Gives the bunker's TK vz. 37 heavy machine gun (la_m_Bunkr01, the Czech6 bunker missions) the
optical sight of the Panzer III's hull MG, which the original model never got.

    python tools/build_bunker_sight.py [--src H:/HD2_Unpacked] [--out mods/tkvz37_sight]

Output: OUT/MODELS/la_m_Bunkr01.4ds (same name: the missions pick it up unchanged).

How the game shows a seat weapon's sight (C_SeatWeapon::Init 0x004c1ad0 / ShowView 0x004c6d90):
  * Init looks up the visual "VIEW<seat>" anywhere in the actor's model; SetGunner shows it to a
    local-player gunner in first-person view, and the camera module (0x0046c370) shows it at a
    "NO3RD" seat in any view.
  * la_Panzer III's hull MG (seat 1) has VIEW01 below VERT01: a black mask with a round lens
    (LA_P20.BMP + alpha LA_P20OP.BMP), and its child Zamerovac01, the graduated reticle; both just
    in front of the gun's camera CAMERAFPVGun. w_mg42Stand's VIEW00 is a plain crosshair.
  * la_m_Bunkr01 (seat 0, SEAT28 + NO3RD) has the rig HORT00 / VERT00 / BARREL00_00 and a camera
    CAMERAFPV but no VIEW00.

Why the gun showed nothing at all in first person, and a VIEW00 alone does not help: its camera sat
0.70 m in front of the elevation pivot, 0.52 m beyond the muzzle, which in all three Czech6 bunkers
is 0.6 - 0.8 m outside the bunker's sector (past the embrasure). LS3D picks the camera's sector by
its position (I3D_camera::UpdateSector) and draws the other sectors only through portals in view,
so nothing in the bunker sector - the gun, the sight - was drawn. The camera (and CAMERA3RD, at the
same spot) moves behind the pivot, beside the embrasure mount: CAMERA_SPOT. check_sectors() tests
it against the missions' sectors over the gun's range.

The sight copies VIEW01 + Zamerovac01 (and their two materials) below VERT00 as VIEW00, at the same
offset and size relative to the camera as in the Panzer (whose VERT01 is scaled 0.82, its camera
1.22: the camera is unscaled in the world, so the world offset and scale carry over). The reticle's
centre is on the camera's axis, parallel to the bore (SHDUM00, the muzzle the first-person shot
leaves along) and CAMERA_SPOT[1] - bore height above it. No zoom.
"""
import copy
import glob
import math
import os
import re
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import hd2_4ds as h                                         # noqa: E402
import render4ds                                            # noqa: E402
from build_halftrack_mg42 import graft, index, load, subtree, world_pos   # noqa: E402

BUNKER = 'la_m_Bunkr01'
DONOR = 'la_Panzer III'
DONOR_CAMERA, DONOR_VIEW = 'CAMERAFPVGun', 'VIEW01'
CAMERAS, VERT, VIEW = ('CAMERAFPV', 'CAMERA3RD'), 'VERT00', 'VIEW00'
MUZZLE = 'SHDUM00'
# the cameras' new place in VERT00's space, left of the gun like the real vz. 37's telescope: the
# embrasure mount (Cylinder06: a top plate on rods, turning with HORT00; Object02: the fixed
# cradle) stands just behind the pivot. Of a 160-spot search scored over five gun poses (lens of
# tan 0.5 on a 40 x 40 grid), this one leaves the lens centre clear and 2.4 % of the lens covered
# while staying >= 0.11 m inside the five bunker placements; spots further forward see more but
# leave the bunker.
CAMERA_SPOT = (-0.15, 0.20, -0.35)
# the gun's range (BARREL00_00: left30, right30, up10, down20), degrees
TRAVERSE, ELEVATION = (-30, 30), (-20, 10)
MIN_MARGIN = 0.10                   # metres the camera keeps inside the bunker sector


def is_identity(q):
    return all(abs(c) < 1e-5 for c in q[:3]) and abs(abs(q[3]) - 1) < 1e-5


def world_scale(m, i):
    """Uniform world scale of frame i (product of the scales up the chain; all must be uniform)."""
    s = 1.0
    while i >= 0:
        f = m['frames'][i]
        assert max(f['scale']) - min(f['scale']) < 1e-4, f['name']
        s *= f['scale'][0]
        i = f['parent'] - 1
    return s


def unrotated(m, i):
    """No rotation on frame i or any ancestor (positions then just add up, scaled)."""
    while i >= 0:
        f = m['frames'][i]
        if not is_identity(f['rot']):
            return False
        i = f['parent'] - 1
    return True


def build(src):
    bunker = load(src, BUNKER)
    donor = load(src, DONOR)
    names = {f['name'] for f in bunker['frames']}
    assert VIEW not in names, 'the model already has a sight'

    dv, dc = index(donor, DONOR_VIEW), index(donor, DONOR_CAMERA)
    vert, muzzle = index(bunker, VERT), index(bunker, MUZZLE)
    cams = [index(bunker, n) for n in CAMERAS]
    assert unrotated(donor, dv) and unrotated(donor, dc) and unrotated(bunker, vert)
    assert all(bunker['frames'][c]['parent'] - 1 == vert and unrotated(bunker, c) for c in cams)
    assert abs(world_scale(donor, dc) - 1) < 2e-3 and abs(world_scale(bunker, vert) - 1) < 1e-4

    old_camera = bunker['frames'][cams[0]]['pos']
    for c in cams:
        bunker['frames'][c]['pos'] = CAMERA_SPOT

    # the sight in the camera's (unrotated, unscaled) space, as the donor's gunner sees it
    offset = [a - b for a, b in zip(world_pos(donor, dv), world_pos(donor, dc))]
    scale = world_scale(donor, dv)

    # the two materials the copied meshes use, appended to the bunker's list
    used = sorted({g['material'] for i in subtree(donor, dv) for l in donor['frames'][i]['lods']
                   for g in l['groups'] if g['material']})
    material_map = {}
    for mid in used:
        bunker['materials'].append(copy.deepcopy(donor['materials'][mid - 1]))
        material_map[mid] = len(bunker['materials'])

    remap = graft(bunker, donor, subtree(donor, dv), vert + 1, {DONOR_VIEW: VIEW},
                  lambda mid: material_map[mid])
    view = bunker['frames'][remap[dv]]
    view['pos'] = tuple(c + o for c, o in zip(CAMERA_SPOT, offset))
    view['scale'] = (scale, scale, scale)

    names = [f['name'] for f in bunker['frames']]
    assert len(names) == len(set(names)), 'frame names must stay unique'

    # the camera looks along the muzzle's axis (its rotations add up to none), above it
    cp, mp = world_pos(bunker, cams[0]), world_pos(bunker, muzzle)
    muzzle_matrix = render4ds.world_matrices(bunker)[muzzle]
    assert all(abs(muzzle_matrix[r][c] - (r == c)) < 1e-3 for r in range(3) for c in range(3))
    return bunker, {
        'offset': offset, 'scale': scale, 'materials': material_map,
        'old_camera': old_camera, 'camera_above_bore': (cp[0] - mp[0], cp[1] - mp[1]),
        'view': [f['name'] for f in bunker['frames'][remap[dv]:]],
    }


# ---- the missions' bunker sectors -------------------------------------------------------------

def chunk_at(d, o):
    cid, size = struct.unpack_from('<HI', d, o)
    return cid, size


def gun_frames(actors):
    """(name, position, rotation (w, x, y, z), sector name) of each actors.bin model frame (record
    0x4010: 0x10 name, 0x20 position, 0x22 rotation, 0x4020 parent "sector ...", 0x2012 model)."""
    key = BUNKER.encode() + b'\0'
    for x in re.finditer(re.escape(struct.pack('<HI', 0x2012, 6 + len(key)) + key), actors):
        for start in range(x.start() - 6, max(0, x.start() - 600), -1):
            cid, size = chunk_at(actors, start)
            if cid == 0x4010 and start + size >= x.end() and size < 1000:
                break
        else:
            raise ValueError('no frame record around 0x%x' % x.start())
        rec, o = {}, start + 6
        while o < start + size:
            cid, csize = chunk_at(actors, o)
            rec[cid] = actors[o + 6:o + csize]
            o += csize
        yield (rec[0x10].rstrip(b'\0').decode('latin-1'), struct.unpack('<3f', rec[0x20]),
               struct.unpack('<4f', rec[0x22]), rec[0x4020][6:].rstrip(b'\0').decode('latin-1'))


def sector_hull(scene, name):
    """Vertices and faces of the scene's sector frame `name` (unrotated at the origin in these
    scenes; LS3DF_Github/docs/4ds_format.md "Sector and portals")."""
    key = bytes([len(name)]) + name.encode('latin-1')
    o = scene.index(key) + len(key)
    o += 1 + scene[o]                                   # the params string
    flags, env, nv, nf = struct.unpack_from('<IiII', scene, o)
    o += 16 + 32                                        # + the box
    verts = [struct.unpack_from('<3f', scene, o + 16 * i) for i in range(nv)]
    o += 16 * nv
    faces = [struct.unpack_from('<3H', scene, o + 6 * i) for i in range(nf)]
    return verts, faces


def inside_margin(p, verts, faces):
    """Distance of p inside the convex hull (negative: outside)."""
    def sub(a, b): return [x - y for x, y in zip(a, b)]
    def dot(a, b): return sum(x * y for x, y in zip(a, b))
    c = [sum(v[k] for v in verts) / len(verts) for k in range(3)]
    worst = math.inf
    for a, b, cc in faces:
        u, v = sub(verts[b], verts[a]), sub(verts[cc], verts[a])
        n = [u[1] * v[2] - u[2] * v[1], u[2] * v[0] - u[0] * v[2], u[0] * v[1] - u[1] * v[0]]
        ln = math.sqrt(dot(n, n))
        if ln < 1e-9:
            continue
        side = dot(sub(p, verts[a]), n) / ln
        worst = min(worst, side if dot(sub(c, verts[a]), n) > 0 else -side)
    return worst


def check_sectors(src, model):
    """Smallest margin of the old and the new camera inside each mission's bunker sector, over the
    gun's traverse and elevation. The actors' rotation is a yaw (w, 0, y, 0) turning +z to
    (sin a, cos a) with a = -2 atan2(y, w): the convention that puts each bunker's embrasure portal
    in front of its gun."""
    pivot = world_pos(model, index(model, VERT))
    results = []
    for actors_path in sorted(glob.glob(os.path.join(src, 'MISSIONS', '*', 'actors.bin'))):
        actors = open(actors_path, 'rb').read()
        if BUNKER.encode() + b'\0' not in actors:
            continue
        mission = os.path.dirname(actors_path)
        scene = open(os.path.join(mission, 'scene.4ds'), 'rb').read()
        for name, pos, q, sector in gun_frames(actors):
            hull = sector_hull(scene, sector)
            yaw = -2 * math.atan2(q[2], q[0])
            margins = []
            for spot in (OLD_CAMERA, CAMERA_SPOT):
                worst = math.inf
                for t in range(TRAVERSE[0], TRAVERSE[1] + 1, 5):
                    for e in range(ELEVATION[0], ELEVATION[1] + 1, 5):
                        er, tr = math.radians(e), math.radians(t)
                        y = spot[1] * math.cos(er) + spot[2] * math.sin(er)
                        z = -spot[1] * math.sin(er) + spot[2] * math.cos(er)
                        local = (pivot[0] + spot[0] * math.cos(tr) + z * math.sin(tr), pivot[1] + y,
                                 pivot[2] - spot[0] * math.sin(tr) + z * math.cos(tr))
                        p = (pos[0] + local[0] * math.cos(yaw) + local[2] * math.sin(yaw), pos[1] + local[1],
                             pos[2] - local[0] * math.sin(yaw) + local[2] * math.cos(yaw))
                        worst = min(worst, inside_margin(p, *hull))
                margins.append(worst)
            results.append((os.path.basename(mission), name, sector, margins))
    return results


OLD_CAMERA = (0.0, 0.051, 0.697)


def main():
    args = sys.argv[1:]
    src = args[args.index('--src') + 1] if '--src' in args else 'H:/HD2_Unpacked'
    out = args[args.index('--out') + 1] if '--out' in args else 'mods/tkvz37_sight'
    model, info = build(src)
    assert max(abs(a - b) for a, b in zip(info['old_camera'], OLD_CAMERA)) < 1e-3, info['old_camera']
    checks = check_sectors(src, model)
    assert checks, 'no mission uses ' + BUNKER
    for mission, name, sector, (old, new) in checks:
        print('%-15s %-13s %-17s camera inside by: old %+.2f m, new %+.2f m' % (mission, name, sector, old, new))
        assert new >= MIN_MARGIN, 'the camera leaves the bunker sector'
    data = h.dumps(model)
    assert h.dumps(h.loads(data)) == data, 'round trip'
    models = os.path.join(out, 'MODELS')
    os.makedirs(models, exist_ok=True)
    path = os.path.join(models, BUNKER + '.4ds')
    open(path, 'wb').write(data)
    print('%s: %d bytes' % (path, len(data)))
    print('%s moved from %s to %s (VERT00 space)' % (' + '.join(CAMERAS), OLD_CAMERA, CAMERA_SPOT))
    print('added %s below %s: %.3f m ahead of the camera, scale %.3f; materials %s' % (
        ' + '.join(info['view']), VERT, info['offset'][2], info['scale'],
        ', '.join('%d->%d' % kv for kv in info['materials'].items())))
    print('camera off the bore axis: x %.4f m, y %+.4f m' % info['camera_above_bore'])


if __name__ == '__main__':
    main()
