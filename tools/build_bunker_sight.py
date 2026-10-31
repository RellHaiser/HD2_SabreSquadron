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
    CAMERAFPV but no VIEW00, hence no sight, no crosshair, no visible gun in first person.

The fix copies VIEW01 + Zamerovac01 (and their two materials) below the bunker's VERT00 as VIEW00,
at the same offset and size relative to the camera as in the Panzer (whose VERT01 is scaled 0.82,
its camera 1.22: the camera is unscaled in the world, so the world offset and scale carry over).
The reticle's centre is on the camera's axis, and the bunker's camera is on the bore axis
(SHDUM00, the muzzle the first-person shot leaves along), so the reticle is true at any range.
No zoom: the camera is not touched.

The sight needs the ported LS3DF.dll with NearPortalDistance=100 in its LS3DF.ini. The camera
sits 0.5 - 0.7 m outside the bunker's embrasure, in the outdoor sector (in all five bunker frames
of CZECH6 and CZECH6_MP_ZONE). The original engine enters the bunker sector, which holds the gun
and its sight, only through a portal in front of the camera, so it never draws them. Moving the
camera inside the bunker instead puts the bunker's concrete in front of it (tried; the user's
in-game test). LS3DF's tests/test_near_portal checks this model against the missions' sectors.
"""
import copy
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import hd2_4ds as h                                         # noqa: E402
from build_halftrack_mg42 import graft, index, load, subtree, world_pos   # noqa: E402

BUNKER = 'la_m_Bunkr01'
DONOR = 'la_Panzer III'
DONOR_CAMERA, DONOR_VIEW = 'CAMERAFPVGun', 'VIEW01'
CAMERA, VERT, VIEW = 'CAMERAFPV', 'VERT00', 'VIEW00'
MUZZLE = 'SHDUM00'


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
    vert, cam, muzzle = index(bunker, VERT), index(bunker, CAMERA), index(bunker, MUZZLE)
    assert unrotated(donor, dv) and unrotated(donor, dc) and unrotated(bunker, cam)
    assert bunker['frames'][cam]['parent'] - 1 == vert
    assert abs(world_scale(donor, dc) - 1) < 2e-3 and abs(world_scale(bunker, vert) - 1) < 1e-4

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
    view['pos'] = tuple(c + o for c, o in zip(bunker['frames'][cam]['pos'], offset))
    view['scale'] = (scale, scale, scale)

    names = [f['name'] for f in bunker['frames']]
    assert len(names) == len(set(names)), 'frame names must stay unique'

    # the reticle is true: the camera looks along the muzzle's axis from on it
    cp, mp = world_pos(bunker, cam), world_pos(bunker, muzzle)
    assert unrotated(bunker, cam)
    return bunker, {
        'offset': offset, 'scale': scale, 'materials': material_map,
        'camera_off_bore': (cp[0] - mp[0], cp[1] - mp[1]),
        'view': [f['name'] for f in bunker['frames'][remap[dv]:]],
    }


def main():
    args = sys.argv[1:]
    src = args[args.index('--src') + 1] if '--src' in args else 'H:/HD2_Unpacked'
    out = args[args.index('--out') + 1] if '--out' in args else 'mods/tkvz37_sight'
    model, info = build(src)
    data = h.dumps(model)
    assert h.dumps(h.loads(data)) == data, 'round trip'
    models = os.path.join(out, 'MODELS')
    os.makedirs(models, exist_ok=True)
    path = os.path.join(models, BUNKER + '.4ds')
    open(path, 'wb').write(data)
    print('%s: %d bytes' % (path, len(data)))
    print('added %s below %s: %.3f m ahead of %s, scale %.3f; materials %s' % (
        ' + '.join(info['view']), VERT, info['offset'][2], CAMERA, info['scale'],
        ', '.join('%d->%d' % kv for kv in info['materials'].items())))
    print('camera off the bore axis: x %.4f m, y %.4f m' % info['camera_off_bore'])


if __name__ == '__main__':
    main()
