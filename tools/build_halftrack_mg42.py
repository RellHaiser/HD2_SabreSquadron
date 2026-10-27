"""Builds the Sd.Kfz. 251 halftrack with a working MG 42 in the gun shield at the front of the troop
compartment, plus the companion models the game loads by prefix, in two camouflages (VARIANTS):
    la_hakl_MG   from la_hakl_Af (Africa), with the shield of la_hakl_Ev moved over
    la_hakl_EM   from la_hakl_Ev (Europe), which has the shield already

    python tools/build_halftrack_mg42.py [--src H:/HD2_Unpacked] [--out mods/halftrack_mg42] [Af] [Ev]

Outputs per variant NAME (OUT/MODELS):
    NAME.4ds              the vehicle: the base + the shield, its left front bench seat SEAT03 made
                          the gunner's, carrying the MG 42 rig of w_mg42Stand (HORT03 / VERT03 /
                          BARREL03_00); still six seats (MAX_SEATS)
    NAME_f03_00.4ds       first-person gun of seat 3, barrel 0 ("%s_f%02d_%02d", seat weapon
    NAME_f03_00.5DS       loader 0x004c1ad0): w_mg42Stand_f00_00 renamed to the seat-3 frames
    DMNAME.4ds            damage model ("DM%s", 0x004e0930): the base's, the SEAT03 hit box
                          fitted to the standing gunner
    cNAME.4ds             burnt wreck ("C%s", C_Car activation 0x004d16a0): the base's + the gun
                          (+ the shield where the base's wreck lacks it)
    sNAME.4ds             shadow ("s%s", 0x004894e0): sla_hakl_Ev (the hull plus the shield)
and OUT/MISSIONS/<m>/actors.bin for the missions with car actors of the base model, their halftracks
using NAME (same-length name, patched in place) to try the vehicle in game.

How the game reads the rig:
  * seat params "SEAT<digits>": every digit d sets seat flag 1 << d (C_Car activation 0x004d16a0).
    0x100 makes a seat weapon. On a static weapon (w_mg42Stand "SEAT28") the gunner gets the blended
    "(mg42_stand" pose, but that pose is only applied to ACTOR_STATIC_WEAPON gunners
    (C_HumanWeaponPose::SetSeat 0x007ae190 / UpdateStaticWeapon). In a car the hands go onto the
    gun's grips "LWS" / "RWS" (IK, UpdateSeat) only when the seat also has flag 2 (sits; the jeep's
    gunner "SEAT18"). So the gunner seat is "SEAT128": 2 (hands on the grips) + 4 (the body plays
    the standing anim, checked first when the occupant gets in) + 0x100 (the weapon).
  * a seat weapon needs HORT<seat>* and VERT<seat>* frames and BARREL<seat>_<n>* frames; the first
    barrel's params give itemid / up / down / left / right (degrees) / hs / vs (turn speeds).
  * the MG 42 frames below HORT are copied unchanged from w_mg42Stand, so the grips, the camera frames
    and the first-person animation (keys local to VERT) stay valid; only the seat moves: the gunner
    stands upright GRIP_AHEAD_OF_FEET behind the pistol grip (RWS).
"""
import copy
import math
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import hd2_4ds as h      # noqa: E402
import hd2_5ds as h5     # noqa: E402

# Names keep the length of the base's name, so mission files can be patched in place.
VARIANTS = {
    'Af': {
        'name': 'la_hakl_MG', 'base': 'la_hakl_Af',
        'shield_from': 'la_hakl_Ev',     # la_hakl_Af has no shield
        'floor': 0.494,                  # the troop compartment's floor (measured on "telo")
        'dm_body': 'BODY00',
        'wreck_shield': 'C2E_HS_B3.BMP', # the burnt texture for the shield added to the wreck
        'missions': ['AFRICA4', 'AFRICA2_MP_ZONE', 'Co_Libye3'],
    },
    'Ev': {
        'name': 'la_hakl_EM', 'base': 'la_hakl_Ev',
        'shield_from': None,
        'floor': 0.498,
        'dm_body': 'BODY',
        'wreck_shield': None,            # cla_hakl_Ev has the shield
        'missions': ['ALPS1', 'Burgundy2', 'CZECH5', 'Co_Burgundy2', 'NORMANDY2'],
    },
}
SHADOW = 'sla_hakl_Ev'                   # = sla_hakl_Af + the shield; the hulls match within mm
# The gunner takes over SEAT03, the left front bench (that passenger sat level with the gunner).
# A vehicle cannot offer more than six seats: the roll menu (C_RollMenu::Activate 0x004b2830)
# encodes "get in at seat N" as action 8 + N and handles 8..13 only, so a seventh seat's action
# 14 is HUMAN_COMMAND_PICK_LOCK (0x0E): the soldier picks the vehicle's lock instead.
SEAT = 3
MAX_SEATS = 6
SEAT_PARAMS = 'SEAT128,name7101'    # name7101: the gunner's seat name (jeep, static MG 42)
# Item 33: MG 42. The shield turns with the gun (it hangs below HORT), so the traverse is not limited
# by the plate; 37 degrees each way is where tools/gun_clearance.py finds shield, mount and gun
# first touching the cab's vision blocks.
BARREL_PARAMS = 'itemid33,up15,down10,left37,right37,matid12,hs180,vs180'
# The mount (as on the real Sd.Kfz. 251 shield mount): a post rising from a turntable on the roof
# behind the apex of the shield, a yoke on the post and a cradle in the yoke holding the gun at the
# rear of its barrel jacket, the whole jacket in front of the shield; a strut from the post braces
# the right wing. Traverse (HORT: post, yoke, strut, shield) and elevation (VERT: cradle, gun) both
# turn about the yoke's trunnions on the post.
AXIS_BEHIND_PLATE = 0.04         # the post (both axes) on the slot line, 4 cm behind the plate
BARREL_AXIS_Y = 1.886            # world height of the barrel's axis: through the middle of the slot
GUN_AT_PLATE = 0.30              # the gun's point (its frame z, muzzle at 0.87) at the plate: just in front
                                 # of the rear sight (0.23..0.29), the jacket (0.35..0.65) and muzzle ahead
BARREL_AXIS_ABOVE_ORIGIN = 0.092 # the jacket's axis above the gun frame's origin
SHIELD_LIFT = 0.0                # (2.5 cm in the build before: the user preferred no gap at the roof)
RECOIL_SCALE = 0.0               # share of the static MG 42's recoil slide (7.3 cm) kept in the
                                 # first-person animation: 0, the cradle holds the gun (in game the
                                 # full slide moved the gun back and forth through the mount)
GRIP_ABOVE_FEET = 1.30           # the pistol grip (RWS) at chest height of the standing gunner
GRIP_AHEAD_OF_FEET = 0.38        # and this far in front of him
GUN_RIGHT_OF_BODY = 0.083        # the gun at the right shoulder (w_mg42Stand: HORT00 0.083 right of SEAT00)
WRECK_GUN_DROOP = 12.0           # degrees the wreck's gun hangs down
WRECK_GUN_YAW = 0.0               # (the shield and the mount stay facing forward)


# ---- helpers ----------------------------------------------------------------------------------

def load(src, name):
    return h.load(os.path.join(src, 'MODELS', name + '.4ds'))


def index(m, name):
    for i, f in enumerate(m['frames']):
        if f['name'] == name:
            return i
    raise KeyError(name)


def subtree(m, root):
    """Frame indices of root and its descendants, in file order."""
    keep = {root}
    for i in range(root + 1, len(m['frames'])):
        if m['frames'][i]['parent'] - 1 in keep:
            keep.add(i)
    return sorted(keep)


def quat_matrix(q):
    """Rotation of a frame's stored quaternion (x, y, z, w), row-vector convention (see render4ds)."""
    x, y, z, w = q
    x, y, z = -x, -y, -z
    return [[1 - 2 * (y * y + z * z), 2 * (x * y + z * w), 2 * (x * z - y * w)],
            [2 * (x * y - z * w), 1 - 2 * (x * x + z * z), 2 * (y * z + x * w)],
            [2 * (x * z + y * w), 2 * (y * z - x * w), 1 - 2 * (x * x + y * y)]]


def rotate(v, q):
    r = quat_matrix(q)
    return tuple(sum(v[k] * r[k][j] for k in range(3)) for j in range(3))


def quat_mul(a, b):
    """Quaternion product a*b, (x, y, z, w)."""
    ax, ay, az, aw = a
    bx, by, bz, bw = b
    return (aw * bx + ax * bw + ay * bz - az * by,
            aw * by - ax * bz + ay * bw + az * bx,
            aw * bz + ax * by - ay * bx + az * bw,
            aw * bw - ax * bx - ay * by - az * bz)


def world_pos(m, i):
    """World position of frame i (rotations and scales of the ancestors included)."""
    f = m['frames'][i]
    p = f['pos']
    while f['parent']:
        par = m['frames'][f['parent'] - 1]
        p = rotate(tuple(p[k] * par['scale'][k] for k in range(3)), par['rot'])
        p = tuple(p[k] + par['pos'][k] for k in range(3))
        f = par
    return p


def assert_plain(m, i):
    """The frame and its ancestors have no rotation and unit scale (positions simply add up)."""
    f = m['frames'][i]
    while True:
        assert all(abs(c) < 1e-6 for c in f['rot'][:3]) and abs(abs(f['rot'][3]) - 1) < 1e-6, f['name']
        if not f['parent']:
            return
        f = m['frames'][f['parent'] - 1]
        assert all(abs(s - 1) < 1e-6 for s in f['scale']), f['name']


def mesh_points(f, lod=0):
    vb = f['lods'][lod]['vertices']
    return [struct.unpack_from('<3f', vb, k * 32) for k in range(len(vb) // 32)]


def graft(dst, src, indices, parent_of_root, rename, material):
    """Appends copies of src frames `indices` (a subtree in file order) to dst.

    parent_of_root: dst parent (1-based, 0 = root) of frames whose parent is outside `indices`.
    rename: {old name: new name}; material: function old src material id -> dst id (0 stays 0).
    Returns {src index: dst index}.
    """
    remap = {}
    for i in indices:
        f = copy.deepcopy(src['frames'][i])
        sp = f['parent'] - 1
        f['parent'] = remap[sp] + 1 if sp in remap else parent_of_root
        f['name'] = rename.get(f['name'], f['name'])
        if f.get('instance'):
            f['instance'] = remap[f['instance'] - 1] + 1
        for l in f.get('lods', []):
            for g in l['groups']:
                g['material'] = material(g['material']) if g['material'] else 0
        remap[i] = len(dst['frames'])
        dst['frames'].append(f)
    return remap


def move_to_end(m, i):
    """Moves frame i (no children, no instances of it) to the end of the frame list."""
    frames = m['frames']
    assert not any(f['parent'] == i + 1 or f.get('instance') == i + 1 for f in frames)
    f = frames.pop(i)

    def fix(ref):
        return ref - 1 if ref > i + 1 else ref
    for g in frames:
        g['parent'] = fix(g['parent'])
        if g.get('instance'):
            g['instance'] = fix(g['instance'])
    f['parent'] = fix(f['parent'])
    frames.append(f)


# ---- new geometry -----------------------------------------------------------------------------

class Mesh:
    """Triangles for a standard mesh LOD: vertices (position, normal, uv), counter-checked winding
    (the shipped meshes: cross(b - a, c - a) points along the vertex normals)."""

    def __init__(self, uv):
        self.verts = []
        self.faces = []
        self.uv = uv

    def vert(self, p, n):
        self.verts.append((tuple(p), tuple(n)))
        return len(self.verts) - 1

    def tri(self, a, b, c, n):
        pa, pb, pc = (self.verts[i][0] for i in (a, b, c))
        u = [pb[k] - pa[k] for k in range(3)]
        w = [pc[k] - pa[k] for k in range(3)]
        cr = (u[1] * w[2] - u[2] * w[1], u[2] * w[0] - u[0] * w[2], u[0] * w[1] - u[1] * w[0])
        if sum(cr[k] * n[k] for k in range(3)) < 0:
            b, c = c, b
        self.faces.append((a, b, c))

    def quad(self, ps, n):
        """A flat quad (4 corners in order) facing n."""
        ids = [self.vert(p, n) for p in ps]
        self.tri(ids[0], ids[1], ids[2], n)
        self.tri(ids[0], ids[2], ids[3], n)

    def box(self, center, axes, half):
        """A box: center, three orthonormal axes, half sizes along them."""
        for k in range(3):
            for s in (-1.0, 1.0):
                n = tuple(axes[k][j] * s for j in range(3))
                o = [(k + 1) % 3, (k + 2) % 3]
                c = [center[j] + n[j] * half[k] for j in range(3)]
                corners = []
                for a, b in ((-1, -1), (1, -1), (1, 1), (-1, 1)):
                    corners.append(tuple(c[j] + axes[o[0]][j] * half[o[0]] * a + axes[o[1]][j] * half[o[1]] * b
                                         for j in range(3)))
                self.quad(corners, n)

    def aabox(self, lo, hi):
        self.box(tuple((lo[k] + hi[k]) / 2 for k in range(3)), ((1, 0, 0), (0, 1, 0), (0, 0, 1)),
                 tuple((hi[k] - lo[k]) / 2 for k in range(3)))

    def bar(self, p0, p1, half_w, half_h):
        """A square bar from p0 to p1."""
        d = [p1[k] - p0[k] for k in range(3)]
        length = math.sqrt(sum(c * c for c in d))
        ax = [c / length for c in d]
        ref = (0.0, 1.0, 0.0) if abs(ax[1]) < 0.9 else (1.0, 0.0, 0.0)
        side = norm3(cross3(ref, ax))
        up = cross3(ax, side)
        self.box(tuple((p0[k] + p1[k]) / 2 for k in range(3)), (side, up, tuple(ax)), (half_w, half_h, length / 2))

    def cylinder_y(self, cx, cz, r, y0, y1, seg=16, caps=True):
        ring = [(math.cos(2 * math.pi * i / seg), math.sin(2 * math.pi * i / seg)) for i in range(seg)]
        for i in range(seg):
            (c0, s0), (c1, s1) = ring[i], ring[(i + 1) % seg]
            cm, sm = math.cos(2 * math.pi * (i + 0.5) / seg), math.sin(2 * math.pi * (i + 0.5) / seg)
            a = self.vert((cx + r * c0, y0, cz + r * s0), (c0, 0, s0))
            b = self.vert((cx + r * c1, y0, cz + r * s1), (c1, 0, s1))
            c = self.vert((cx + r * c1, y1, cz + r * s1), (c1, 0, s1))
            d = self.vert((cx + r * c0, y1, cz + r * s0), (c0, 0, s0))
            self.tri(a, b, c, (cm, 0, sm))
            self.tri(a, c, d, (cm, 0, sm))
        if caps:
            for y, ny in ((y1, 1.0), (y0, -1.0)):
                ctr = self.vert((cx, y, cz), (0, ny, 0))
                ids = [self.vert((cx + r * c, y, cz + r * s), (0, ny, 0)) for c, s in ring]
                for i in range(seg):
                    self.tri(ctr, ids[i], ids[(i + 1) % seg], (0, ny, 0))

    def lod(self, origin, material):
        vb = b''.join(struct.pack('<8f', p[0] - origin[0], p[1] - origin[1], p[2] - origin[2], *n, *self.uv)
                      for p, n in self.verts)
        fb = b''.join(struct.pack('<3H', *f) for f in self.faces)
        assert len(self.verts) < 65536
        return {'distance': 0.0, 'unk': 0, 'vertices': vb, 'groups': [{'faces': fb, 'material': material}]}


def cross3(a, b):
    return (a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0])


def norm3(v):
    length = math.sqrt(sum(c * c for c in v))
    return tuple(c / length for c in v)


def visual_frame(name, parent, pos, params, lod):
    return {'type': h.FRAME_VISUAL, 'vtype': h.VIS_OBJECT, 'rflags': 0x2a00, 'parent': parent,
            'pos': tuple(pos), 'rot': (-0.0, -0.0, -0.0, 1.0), 'scale': (1.0, 1.0, 1.0), 'unk': 0,
            'cull': 0x09, 'name': name, 'params': params, 'instance': 0, 'lods': [lod]}


def world_triangles(m, names):
    """World triangles (LOD 0) of the named frames."""
    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    import render4ds as r
    tris, _ = r.triangles(m)
    wanted = {i for i, f in enumerate(m['frames']) if f['name'] in names}
    return [t[:3] for t in tris if t[4] in wanted]


def ray_hit(tris, origin, d):
    """Distance along unit direction d to the nearest triangle, or None."""
    best = None
    for a, b, c in tris:
        e1 = [b[k] - a[k] for k in range(3)]
        e2 = [c[k] - a[k] for k in range(3)]
        pv = cross3(d, e2)
        det = sum(e1[k] * pv[k] for k in range(3))
        if abs(det) < 1e-12:
            continue
        s = [origin[k] - a[k] for k in range(3)]
        u = sum(s[k] * pv[k] for k in range(3)) / det
        if u < 0 or u > 1:
            continue
        qv = cross3(s, e1)
        v = sum(d[k] * qv[k] for k in range(3)) / det
        if v < 0 or u + v > 1:
            continue
        t = sum(e2[k] * qv[k] for k in range(3)) / det
        if t > 0 and (best is None or t < best):
            best = t
    return best


def seat_numbers(m):
    return sorted(int(f['name'][4:]) for f in m['frames']
                  if f['name'].startswith('SEAT') and f['name'][4:].isdigit())


def check_names_unique(m, what):
    seen = {}
    for f in m['frames']:
        seen[f['name']] = seen.get(f['name'], 0) + 1
    dup = [n for n, c in seen.items() if c > 1]
    assert not dup, '%s: duplicate frame names %s' % (what, dup)


def roundtrip(m, what):
    data = h.dumps(m)
    assert h.dumps(h.loads(data)) == data, what
    return data


# ---- the vehicle ------------------------------------------------------------------------------

def shield_geometry(ev, af):
    """The Ev shield (Loft01) moved onto the Af hull; returns (world origin, slot centre x, front
    face z at a height y, as a function)."""
    li = index(ev, 'Loft01')
    assert_plain(ev, li)
    # the hulls of the two variants differ by a few millimetres
    hull_shift = tuple(world_pos(af, index(af, 'telo'))[k] - world_pos(ev, index(ev, 'telo'))[k] for k in range(3))
    origin_ev = world_pos(ev, li)
    s = ev['frames'][li]['scale']
    pts = [tuple(origin_ev[k] + p[k] * s[k] + hull_shift[k] for k in range(3)) for p in mesh_points(ev['frames'][li])]
    origin = tuple(origin_ev[k] + hull_shift[k] for k in range(3))
    # the slot: the vertical edges of the notch are the points at the slot's shoulders and top,
    # nearest the centre line
    inner = [p for p in pts if abs(p[0] + 0.02) < 0.07 and p[1] > 1.84]
    xs = sorted(set(round(p[0], 4) for p in inner))
    slot_x = (min(xs) + max(xs)) / 2
    # the front face of the plate along the slot edge: z of the frontmost points at the shoulder
    # and at the top, interpolated in height
    edge = [p for p in inner if abs(p[0] - min(xs)) < 1e-3]
    low = max((p for p in edge if p[1] < 1.9), key=lambda p: p[2])
    high = max((p for p in edge if p[1] > 1.9), key=lambda p: p[2])

    def face_z(y):
        t = (y - low[1]) / (high[1] - low[1])
        return low[2] + t * (high[2] - low[2])
    return origin, slot_x, face_z, (min(xs), max(xs), low[1], high[1])


def build_vehicle(src, v):
    base = load(src, v['base'])
    mg = load(src, 'w_mg42Stand')
    out = copy.deepcopy(base)
    body = index(out, 'BODY')
    assert_plain(out, body)
    body_w = world_pos(out, body)

    # 1. the shield; moved over from another variant with this one's version of its texture
    if v['shield_from']:
        donor = load(src, v['shield_from'])
        shield_origin, slot_x, face_z, slot = shield_geometry(donor, base)
        donor_mtl = donor['materials']
        by_map = {m['diffuse_map'].upper(): i + 1 for i, m in enumerate(out['materials'])}

        def shield_material(mid):
            name = donor_mtl[mid - 1]['diffuse_map'].upper().replace('_EV.BMP', '.BMP')
            return by_map[name]
        li = index(donor, 'Loft01')
        remap = graft(out, donor, [li], body + 1, {}, shield_material)
        shield = out['frames'][remap[li]]
        shield['pos'] = tuple(shield_origin[k] - body_w[k] for k in range(3))
    else:
        shield_origin, slot_x, face_z, slot = shield_geometry(base, base)

    # 2. the gun through the slot, the trunnions on the post behind the plate; the gunner stands
    # behind the pistol grip (RWS)
    hort_src = index(mg, 'HORT00')
    barrel_src = index(mg, 'BARREL00_00')
    barrel_axis = BARREL_AXIS_Y
    plate_z = face_z(barrel_axis)
    pivot = (slot_x, barrel_axis, plate_z - AXIS_BEHIND_PLATE)        # HORT = VERT: the trunnions
    barrel_w = (slot_x, barrel_axis - BARREL_AXIS_ABOVE_ORIGIN, plate_z - GUN_AT_PLATE)
    barrel_local = tuple(barrel_w[k] - pivot[k] for k in range(3))   # below VERT
    rws_local = mg['frames'][index(mg, 'RWS')]['pos']               # below BARREL (no rotation)
    assert_plain(mg, barrel_src)
    grip = tuple(barrel_w[k] + rws_local[k] for k in range(3))
    seat_w = (pivot[0] - GUN_RIGHT_OF_BODY, grip[1] - GRIP_ABOVE_FEET, grip[2] - GRIP_AHEAD_OF_FEET)
    assert abs(seat_w[1] - v['floor']) < 0.03, 'the gunner would not stand on the floor: %.3f' % seat_w[1]
    # the bench seat becomes the gunner's (a dummy with no children; it keeps its place in the file)
    seat_i = index(out, 'SEAT%02d' % SEAT)
    seat = out['frames'][seat_i]
    assert seat['type'] == h.FRAME_DUMMY and seat['parent'] == body + 1
    assert not any(f['parent'] == seat_i + 1 for f in out['frames'])
    seat.update(pos=tuple(seat_w[k] - body_w[k] for k in range(3)), rot=(-0.0, -0.0, -0.0, 1.0),
                scale=(1.0, 1.0, 1.0), params=SEAT_PARAMS)
    assert seat_numbers(out) == list(range(MAX_SEATS)), seat_numbers(out)

    # 3. the MG 42 rig (HORT00 and below) with the seat number and unique camera names
    mtl_base = len(out['materials'])
    out['materials'] += copy.deepcopy(mg['materials'])
    rename = rig_names()
    remap = graft(out, mg, subtree(mg, hort_src), seat_i + 1, rename, lambda mid: mid + mtl_base)
    # both axes at the trunnions (HORT and VERT together); the gun sits further forward below VERT
    # than in w_mg42Stand (whose VERT is the bipod point), so the barrel's first-person position
    # keys (local to VERT) move by the same amount (build_fpv)
    hort_w = pivot
    vert_src = index(mg, 'VERT00')
    assert mg['frames'][vert_src]['parent'] == hort_src + 1 and mg['frames'][barrel_src]['parent'] == vert_src + 1
    assert max(abs(c) for c in mg['frames'][vert_src]['pos']) < 1e-6
    out['frames'][remap[hort_src]]['pos'] = tuple(hort_w[k] - seat_w[k] for k in range(3))
    out['frames'][remap[vert_src]]['pos'] = (0.0, 0.0, 0.0)
    out['frames'][remap[barrel_src]]['pos'] = barrel_local
    out['frames'][remap[barrel_src]]['params'] = BARREL_PARAMS
    barrel_shift = tuple(barrel_local[k] - mg['frames'][barrel_src]['pos'][k] for k in range(3))
    hort_i = index(out, 'HORT%02d' % SEAT)

    # 4. the shield turns with the gun (traverse only; the gun elevates inside its slot): it hangs
    # below HORT, moved to the end of the file so that its parent comes first, raised SHIELD_LIFT
    # so that its lower edge clears the roof plate it sat on
    li = index(out, 'Loft01')
    assert_plain(out, li)
    shield_w = world_pos(out, li)
    move_to_end(out, li)
    hort_i = index(out, 'HORT%02d' % SEAT)
    shield = out['frames'][-1]
    shield['parent'] = hort_i + 1
    shield['pos'] = (shield_w[0] - hort_w[0], shield_w[1] + SHIELD_LIFT - hort_w[1], shield_w[2] - hort_w[2])
    shield_mtl = shield['lods'][0]['groups'][0]['material']

    # 5. the mount, painted like the shield (its texture's middle)
    build_mount(out, pivot, barrel_w, shield_mtl, hort_i, index(out, 'VERT%02d' % SEAT))
    check_names_unique(out, v['name'])

    # the copied rig ends where it was meant to
    for name, want in (('HORT%02d' % SEAT, hort_w), ('VERT%02d' % SEAT, pivot), ('RWS', grip),
                       ('Loft01', (shield_w[0], shield_w[1] + SHIELD_LIFT, shield_w[2]))):
        got = world_pos(out, index(out, name))
        assert max(abs(got[k] - want[k]) for k in range(3)) < 1e-5, (name, got, want)
    info = {'slot': slot, 'slot_x': slot_x, 'pivot': pivot, 'hort': hort_w, 'seat': seat_w, 'grip': grip,
            'lws': world_pos(out, index(out, 'LWS')), 'shield_face_z': face_z(barrel_axis),
            'barrel_local': barrel_local, 'barrel_shift': barrel_shift,
            'muzzle_ahead': barrel_w[2] + 0.872 - plate_z}
    return out, info


MOUNT = 'MG_MOUNT'      # turns with the traverse (post, yoke, strut)
CRADLE = 'MG_CRADLE'    # tilts with the gun


def build_mount(out, pivot, barrel_w, material, hort_i, vert_i):
    """The shield mount from the photos of the real one: a turntable on the roof below the post, the
    post behind the apex of the shield up to a yoke whose arms carry the trunnions, a bracket from
    the post to the apex, a strut from the post to the right wing with a clamp; the cradle around
    the rear of the barrel jacket hangs in the yoke and tilts with the gun."""
    x0, ya, zp = pivot
    hull = world_triangles(out, {'telo'})
    shield = world_triangles(out, {'Loft01'})
    down = ray_hit(hull, (x0, ya, zp), (0.0, -1.0, 0.0))
    roof = ya - down
    # where the plate's back is: rays forwards (+z) from behind
    apex_y = roof + 0.09
    apex_z = zp + ray_hit(shield, (x0, apex_y, zp), (0.0, 0.0, 1.0))
    wing_x, wing_y = x0 + 0.20, ya - 0.08
    wing_z = zp - 0.5 + ray_hit(shield, (wing_x, wing_y, zp - 0.5), (0.0, 0.0, 1.0))
    yoke_bottom = ya - 0.075
    xs = [p[0] for t in shield for p in t]
    us = mean_uv(out, 'Loft01')

    m = Mesh(us)
    m.cylinder_y(x0, zp, 0.075, roof, roof + 0.012, seg=20)                    # turntable
    m.cylinder_y(x0, zp, 0.022, roof + 0.012, yoke_bottom, seg=12)             # post
    m.aabox((x0 - 0.057, yoke_bottom, zp - 0.022), (x0 + 0.057, yoke_bottom + 0.016, zp + 0.022))  # yoke
    for s in (-1, 1):
        m.aabox((x0 + s * 0.047 - 0.005, yoke_bottom, zp - 0.02), (x0 + s * 0.047 + 0.005, ya + 0.03, zp + 0.02))
        lo, hi = sorted((x0 + s * 0.035, x0 + s * 0.042))
        m.aabox((lo, ya - 0.012, zp - 0.012), (hi, ya + 0.012, zp + 0.012))   # trunnion boss
    m.aabox((x0 - 0.012, apex_y - 0.008, zp + 0.02), (x0 + 0.012, apex_y + 0.008, apex_z - 0.004))   # apex bracket
    strut_end = (wing_x, wing_y, wing_z - 0.012)
    m.bar((x0 + 0.018, roof + 0.07, zp), strut_end, 0.007, 0.007)            # strut
    m.aabox((wing_x - 0.02, wing_y - 0.016, wing_z - 0.02), (wing_x + 0.02, wing_y + 0.016, wing_z - 0.004))   # clamp
    out['frames'].append(visual_frame(MOUNT, hort_i + 1, (0.0, 0.0, 0.0), 'matid12', m.lod(pivot, material)))

    c = Mesh(us)
    z0, z1 = barrel_w[2] + 0.205, barrel_w[2] + 0.295                         # the jacket's rear
    for s in (-1, 1):
        lo, hi = sorted((x0 + s * 0.026, x0 + s * 0.034))
        c.aabox((lo, ya - 0.045, z0), (hi, ya + 0.025, z1))                   # side plates
    c.aabox((x0 - 0.034, ya - 0.045, z0), (x0 + 0.034, ya - 0.035, z1))       # bottom
    assert z0 < zp < z1, 'the trunnions are not inside the cradle'
    out['frames'].append(visual_frame(CRADLE, vert_i + 1, (0.0, 0.0, 0.0), 'matid12', c.lod(pivot, material)))
    assert min(xs) < wing_x < max(xs)


def mean_uv(m, name):
    f = m['frames'][index(m, name)]
    vb = f['lods'][0]['vertices']
    uv = [struct.unpack_from('<2f', vb, k * 32 + 24) for k in range(len(vb) // 32)]
    return (sum(u for u, _ in uv) / len(uv), sum(v for _, v in uv) / len(uv))


def rig_names():
    return {
        'HORT00': 'HORT%02d' % SEAT,
        'VERT00': 'VERT%02d' % SEAT,
        'BARREL00_00': 'BARREL%02d_00' % SEAT,
        'SHDUM00': 'SHDUM%02d' % SEAT,
        'VIEW00': 'VIEW%02d' % SEAT,
        'SEAT00': 'SEAT%02d' % SEAT,
        # the halftrack's driver seat already has CAMERAFPV / CAMERA3RD; the jeep's gun names its
        # camera "CAMERAFPVStatic" the same way (lookups are "cameraFPV*" / "camera3RD*" below the seat)
        'CAMERAFPV': 'CAMERAFPVStatic',
        'CAMERA3RD': 'CAMERA3RDStatic',
    }


# ---- first-person gun -------------------------------------------------------------------------

def build_fpv(src, vehicle):
    """The first-person model mirrors the vehicle's path BODY -> SEAT06 -> HORT06 -> ... (like
    la_Jeepsas_f01_00), the animation gets the renamed frames."""
    fp = load(src, 'w_mg42Stand_f00_00')
    out = copy.deepcopy(fp)
    out['frames'] = []
    vb = index(vehicle, 'BODY')
    out['frames'].append(copy.deepcopy(vehicle['frames'][vb]))
    out['frames'][0]['parent'] = 0
    vs = index(vehicle, 'SEAT%02d' % SEAT)
    seat = copy.deepcopy(vehicle['frames'][vs])
    seat['parent'] = 1
    out['frames'].append(seat)
    hort = index(fp, 'HORT00')
    remap = graft(out, fp, subtree(fp, hort), 2, rig_names(), lambda mid: mid)
    # the same pivot offset and gun params as the vehicle
    out['frames'][remap[hort]]['pos'] = vehicle['frames'][index(vehicle, 'HORT%02d' % SEAT)]['pos']
    out['frames'][remap[index(fp, 'VERT00')]]['pos'] = vehicle['frames'][index(vehicle, 'VERT%02d' % SEAT)]['pos']
    fb = remap[index(fp, 'BARREL00_00')]
    src_barrel = out['frames'][fb]['pos']
    new_barrel = vehicle['frames'][index(vehicle, 'BARREL%02d_00' % SEAT)]['pos']
    out['frames'][fb]['pos'] = new_barrel
    out['frames'][fb]['params'] = BARREL_PARAMS
    check_names_unique(out, 'fpv')
    anim = h5.load(os.path.join(src, 'MODELS', 'w_mg42Stand_f00_00.5DS'))
    # the barrel's position keys are local to VERT: they move with the barrel's new offset
    shift = tuple(new_barrel[k] - src_barrel[k] for k in range(3))
    rest = h5.positions(anim, 'BARREL00_00')[0]
    assert max(abs(rest[k] - src_barrel[k]) for k in range(3)) < 1e-3, (rest, src_barrel)
    anim = h5.parse(h5.shift_positions(anim, 'BARREL00_00', shift))
    # the cradle holds the gun: the static gun's 7.3 cm slide back on each shot is scaled by
    # RECOIL_SCALE (the 1 degree muzzle kick, the camera shake and the belt stay)
    anim = h5.parse(h5.scale_motion(anim, 'BARREL00_00', RECOIL_SCALE))
    names = rig_names()
    anim_out = h5.rename(anim, names)
    # every animated object must exist in the vehicle (the animation binds by name)
    vnames = {f['name'] for f in vehicle['frames']}
    for o in h5.parse(anim_out)['objects']:
        assert o['name'] in vnames, o['name']
    assert out['stamp'] == anim['raw'][6:14], 'the 4ds / 5ds pair keeps one stamp'
    return out, anim_out


# ---- damage model -----------------------------------------------------------------------------

def build_damage(src, v, info):
    dm = load(src, 'DM' + v['base'])
    out = copy.deepcopy(dm)
    body = index(out, v['dm_body'])
    assert_plain(out, body)
    body_w = world_pos(out, body)
    # the bench passenger's box becomes the gunner's
    pi = index(out, 'SEAT%02d' % SEAT)
    proto = out['frames'][pi]
    assert proto['parent'] == body + 1 and all(abs(c) < 1e-6 for c in proto['rot'][:3])
    pts = mesh_points(proto)
    lo = [min(p[k] for p in pts) for k in range(3)]
    hi = [max(p[k] for p in pts) for k in range(3)]
    # the standing gunner: from where the passengers' boxes start to above his head, from his
    # heels to his hands on the grips
    proto_bottom = world_pos(out, pi)[1] + lo[1] * proto['scale'][1]
    sx, sy, sz = info['seat']
    want_lo = (sx - 0.27, round(proto_bottom, 3), sz - 0.25)
    want_hi = (sx + 0.27, sy + 1.85, info['lws'][2] + 0.05)
    scale = tuple((want_hi[k] - want_lo[k]) / (hi[k] - lo[k]) for k in range(3))
    pos_w = tuple(want_lo[k] - lo[k] * scale[k] for k in range(3))
    proto.update(scale=scale, pos=tuple(pos_w[k] - body_w[k] for k in range(3)))
    check_names_unique(out, 'DM')
    return out, (want_lo, want_hi)


# ---- wreck ------------------------------------------------------------------------------------

def build_wreck(src, v, vehicle, info):
    cl = load(src, 'c' + v['base'])
    mg = load(src, 'w_mg42Stand')
    out = copy.deepcopy(cl)
    by_map = {m['diffuse_map'].upper(): i + 1 for i, m in enumerate(out['materials'])}
    burnt = by_map['CLA_CRASH01.BMP']
    if v['wreck_shield']:
        # shield: a root frame at its world place (the wreck has no BODY)
        burnt_plate = by_map[v['wreck_shield']]
        donor = load(src, v['shield_from'])
        li = index(donor, 'Loft01')
        remap = graft(out, donor, [li], 0, {}, lambda mid: burnt_plate)
        vs = index(vehicle, 'Loft01')
        sw = world_pos(vehicle, vs)
        out['frames'][remap[li]]['pos'] = (sw[0], sw[1] - SHIELD_LIFT, sw[2])   # resting on the roof
    else:
        index(out, 'Loft01')            # the base's wreck has its own burnt shield
    # the gun without its detailed LOD, burnt, drooping over the shield
    bi = index(mg, 'BARREL00_00')
    remap = graft(out, mg, [bi], 0, {'BARREL00_00': 'mg42wreck'}, lambda mid: burnt)
    gun = out['frames'][remap[bi]]
    gun['lods'] = gun['lods'][1:]
    gun['params'] = 'matid12'
    droop = math.radians(WRECK_GUN_DROOP)
    yaw = math.radians(WRECK_GUN_YAW)
    q_pitch = (math.sin(droop / 2), 0.0, 0.0, math.cos(droop / 2))
    q_yaw = (0.0, math.sin(yaw / 2), 0.0, math.cos(yaw / 2))
    q = quat_mul(q_pitch, q_yaw)
    # the stored quaternion must bring the muzzle (+Z) down
    if rotate((0.0, 0.0, 1.0), q)[1] > 0:
        q = quat_mul((-q_pitch[0], 0.0, 0.0, q_pitch[3]), q_yaw)
    assert rotate((0.0, 0.0, 1.0), q)[1] < 0
    offset = info['barrel_local']                     # VERT -> BARREL
    pivot = info['pivot']
    o = rotate(offset, q)
    gun['rot'] = q
    gun['pos'] = tuple(pivot[k] + o[k] for k in range(3))
    # the mount, burnt; the cradle hangs down with the gun
    remap = graft(out, vehicle, [index(vehicle, MOUNT), index(vehicle, CRADLE)], 0, {}, lambda mid: burnt)
    for i in remap.values():
        out['frames'][i]['pos'] = pivot
    out['frames'][remap[index(vehicle, CRADLE)]]['rot'] = q
    check_names_unique(out, 'wreck')
    return out


# ---- mission patch ----------------------------------------------------------------------------

def patch_mission(src, v, mission):
    path = os.path.join(src, 'MISSIONS', mission, 'actors.bin')
    d = bytearray(open(path, 'rb').read())
    old = v['base'].encode() + b'\x00'
    new = v['name'].encode() + b'\x00'
    assert len(old) == len(new)
    hits = 0
    p = 0
    while True:
        p = d.find(old, p)
        if p < 0:
            break
        # only model-name chunks (id 0x2012: u16 id, u32 size incl. the 6-byte header)
        cid, size = struct.unpack_from('<HI', d, p - 6)
        if cid == 0x2012 and size == 6 + len(old):
            d[p:p + len(new)] = new
            hits += 1
        p += 1
    assert hits, 'no %s actor in %s' % (v['base'], mission)
    return bytes(d), hits


# ---- main -------------------------------------------------------------------------------------

def build(src, out, v):
    name = v['name']
    models = os.path.join(out, 'MODELS')
    os.makedirs(models, exist_ok=True)
    vehicle, info = build_vehicle(src, v)
    fpv, anim = build_fpv(src, vehicle)
    dm, box = build_damage(src, v, info)
    wreck = build_wreck(src, v, vehicle, info)
    shadow = load(src, SHADOW)

    files = {
        name + '.4ds': roundtrip(vehicle, name),
        '%s_f%02d_00.4ds' % (name, SEAT): roundtrip(fpv, 'fpv'),
        '%s_f%02d_00.5DS' % (name, SEAT): anim,
        'DM' + name + '.4ds': roundtrip(dm, 'DM'),
        'c' + name + '.4ds': roundtrip(wreck, 'wreck'),
        's' + name + '.4ds': roundtrip(shadow, 'shadow'),
    }
    print('== %s (from %s)' % (name, v['base']))
    for fname, data in files.items():
        open(os.path.join(models, fname), 'wb').write(data)
        print('%-26s %7d bytes' % (fname, len(data)))
    for mission_name in v['missions']:
        mission, hits = patch_mission(src, v, mission_name)
        mdir = os.path.join(out, 'MISSIONS', mission_name)
        os.makedirs(mdir, exist_ok=True)
        open(os.path.join(mdir, 'actors.bin'), 'wb').write(mission)
        print('MISSIONS/%s/actors.bin: %d halftrack actor(s) now use %s' % (mission_name, hits, name))
    print('shield slot x %.3f..%.3f, y %.3f..%.3f; front face z %.3f at the barrel' % (
        info['slot'][0], info['slot'][1], info['slot'][2], info['slot'][3], info['shield_face_z']))
    print('traverse axis (HORT%02d, gun + shield) %s, elevation pivot (VERT%02d) %s, gunner (SEAT%02d) %s' % (
        SEAT, tuple(round(c, 3) for c in info['hort']), SEAT, tuple(round(c, 3) for c in info['pivot']),
        SEAT, tuple(round(c, 3) for c in info['seat'])))
    print('gunner hit box %s .. %s' % (tuple(round(c, 3) for c in box[0]), tuple(round(c, 3) for c in box[1])))


def main():
    args = sys.argv[1:]
    src = args[args.index('--src') + 1] if '--src' in args else 'H:/HD2_Unpacked'
    out = args[args.index('--out') + 1] if '--out' in args else 'mods/halftrack_mg42'
    picked = [a for a in args if a in VARIANTS] or list(VARIANTS)
    for key in picked:
        build(src, out, VARIANTS[key])


if __name__ == '__main__':
    main()
