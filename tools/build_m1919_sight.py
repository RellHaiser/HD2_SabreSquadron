"""Gives the Browning M1919 its missing front sight, on the stationary gun (w_1919Crouch) and on the
SAS jeep's gun (la_Jeepsas, seat 1).

    python tools/build_m1919_sight.py [--src H:/HD2_Unpacked] [--out mods/m1919_front_sight]

Output: OUT/MODELS/w_1919Crouch.4ds, la_Jeepsas.4ds, la_Jeepsas_f01_00.5DS (same names: the
missions pick them up unchanged), OUT/preview_*.png (the view from the gunner's camera).

The models: the gun's mesh is the seat weapon's barrel visual BARREL<seat>_00 (W_1919FPV.BMP). It
has the rear sight leaf flipped up (two windows; the upper one is the aperture) but nothing on the
receiver's front, where the M1919A4's front sight stands on the trunnion block. The first-person
camera (cameraFPV / CAMERAFPVStatic) is a child of the barrel looking along +Z, unrotated at rest;
the barrel's FPV animation <model>_f<seat>_00.5DS moves it with the barrel (recoil) and returns it
to the same rest place.

The fix adds a visual "FrontSight<seat>" below the barrel: a base on the receiver's front, a stem and
a blade whose top edge is on the camera's axis, so the top of the post is at the centre of the
screen. The camera must also look through the aperture's centre: w_1919Crouch's camera is 0.45 mm
from it (left as it is), la_Jeepsas's 2.5 mm above it (a quarter of the window), so that camera is
moved onto the aperture's centre in the model and in the animation's position keys.

The game loads la_Jeepsas.4ds from Patch.dta, whose copy differs from models.dta's (same frames,
other vertex data); H:/HD2_Unpacked has it as MODELS/la_Jeepsas.ver (byte-identical, checked).
w_1919Crouch and both .5DS files are only in models.dta; SabreSquadron.dta ships none of them.
"""
import math
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import hd2_4ds as h                                         # noqa: E402
import hd2_5ds as a5                                        # noqa: E402
import render4ds as r                                       # noqa: E402
from build_halftrack_mg42 import Mesh, index, visual_frame   # noqa: E402

TEXTURE = 'W_1919FPV.BMP'
# The leaf's own plain dark-metal patch of the texture (u0, v0, u1, v1).
UV_RECT = (216 / 256, 28 / 128, 240 / 256, 48 / 128)

# Per model. window: the aperture (upper window of the leaf) x0, x1, y0, y1 in barrel space, its
# corners checked against the mesh; front: the receiver's front block z0, z1 and its top (the
# lowest point of its sloping top) where the sight's base sits.
GUNS = [
    {'model': 'w_1919Crouch', 'file': 'w_1919Crouch.4ds', 'barrel': 'BARREL00_00',
     'camera': 'cameraFPV', 'anim': 'w_1919Crouch_f00_00.5DS', 'sight': 'FrontSight00',
     'window': (-0.0043, 0.0035, 0.1620, 0.1723), 'front': (0.0020, 0.0351, 0.1123)},
    {'model': 'la_Jeepsas', 'file': 'la_Jeepsas.ver', 'barrel': 'BARREL01_00',
     'camera': 'CAMERAFPVStatic', 'anim': 'la_Jeepsas_f01_00.5DS', 'sight': 'FrontSight01',
     'window': (-0.0027, 0.0028, 0.1481, 0.1584), 'front': (-0.0218, 0.0112, 0.0984)},
]
MOVE_CAMERA = 0.0010        # move a camera onto the aperture's centre when further off than this

# The sight in its frame's space (origin: centred on the camera's x, on the receiver's top, in the
# middle of its front block): half widths x, y range, half lengths z. The blade's top is y = top.
BASE = (0.0110, -0.0040, 0.0080, 0.0120)
STEM = (0.0045, 0.0060, None, 0.0060)       # up to the blade
BLADE = (0.0016, None, None, 0.0050)        # BLADE_H below the top, up to the top
BLADE_H = 0.0160


class UvMesh(Mesh):
    """Mesh with a planar texture mapping per face into UV_RECT."""

    def __init__(self, extent):
        super().__init__((0.0, 0.0))
        self.extent = extent        # (lo, hi) corners of the whole part, for the mapping

    def quad(self, ps, n):
        k = max(range(3), key=lambda j: abs(n[j]))
        u_ax, v_ax = [j for j in range(3) if j != k]
        lo, hi = self.extent
        ids = []
        for p in ps:
            s = (p[u_ax] - lo[u_ax]) / (hi[u_ax] - lo[u_ax])
            t = 1 - (p[v_ax] - lo[v_ax]) / (hi[v_ax] - lo[v_ax])
            uv = (UV_RECT[0] + s * (UV_RECT[2] - UV_RECT[0]), UV_RECT[1] + t * (UV_RECT[3] - UV_RECT[1]))
            self.verts.append((tuple(p), tuple(n), uv))
            ids.append(len(self.verts) - 1)
        self.tri(ids[0], ids[1], ids[2], n)
        self.tri(ids[0], ids[2], ids[3], n)

    def lod(self, origin, material):
        vb = b''.join(struct.pack('<8f', p[0] - origin[0], p[1] - origin[1], p[2] - origin[2], *n, *uv)
                      for p, n, uv in self.verts)
        fb = b''.join(struct.pack('<3H', *f) for f in self.faces)
        assert len(self.verts) < 65536
        return {'distance': 0.0, 'unk': 0, 'vertices': vb, 'groups': [{'faces': fb, 'material': material}]}


def sight_mesh(top):
    """The base, stem and blade in the sight frame's space; `top` = the blade's top edge."""
    boxes = [
        ((-BASE[0], BASE[1], -BASE[3]), (BASE[0], BASE[2], BASE[3])),
        ((-STEM[0], STEM[1], -STEM[3]), (STEM[0], top - BLADE_H + 0.002, STEM[3])),
        ((-BLADE[0], top - BLADE_H, -BLADE[3]), (BLADE[0], top, BLADE[3])),
    ]
    lo = tuple(min(b[0][k] for b in boxes) for k in range(3))
    hi = tuple(max(b[1][k] for b in boxes) for k in range(3))
    mesh = UvMesh((lo, hi))
    for blo, bhi in boxes:
        mesh.aabox(blo, bhi)
    return mesh


def mesh_points(f):
    vb = f['lods'][0]['vertices']
    return [struct.unpack_from('<3f', vb, k * 32) for k in range(len(vb) // 32)]


def material_of(m, barrel):
    ids = [g['material'] for g in barrel['lods'][0]['groups']
           if m['materials'][g['material'] - 1]['diffuse_map'].upper() == TEXTURE]
    assert len(ids) == 1, ids
    return ids[0]


def check_window(barrel, window):
    """The aperture's corners are vertices of the leaf (within 0.25 mm)."""
    pts = mesh_points(barrel)
    x0, x1, y0, y1 = window
    for x in (x0, x1):
        for y in (y0, y1):
            assert any(abs(p[0] - x) < 2.5e-4 and abs(p[1] - y) < 2.5e-4 and -0.34 < p[2] < -0.33
                       for p in pts), ('no leaf vertex at', x, y)


def build(src, gun):
    path = os.path.join(src, 'MODELS', gun['file'])
    m = h.load(path)
    assert not any(f['name'] == gun['sight'] for f in m['frames']), 'the model already has a sight'
    bi, ci = index(m, gun['barrel']), index(m, gun['camera'])
    barrel, cam = m['frames'][bi], m['frames'][ci]
    assert cam['parent'] - 1 == bi
    assert all(abs(c) < 1e-6 for c in cam['rot'][:3]) and all(abs(s - 1) < 1e-6 for s in cam['scale'])
    check_window(barrel, gun['window'])
    x0, x1, y0, y1 = gun['window']
    aperture = ((x0 + x1) / 2, (y0 + y1) / 2)

    anim_path = os.path.join(src, 'MODELS', gun['anim'])
    anim = a5.load(anim_path)
    keys = a5.positions(anim, gun['camera'])
    assert all(max(abs(k[j] - cam['pos'][j]) for j in range(3)) < 1e-3 for k in keys), \
        'the animation keeps the camera at its rest place'

    off = (cam['pos'][0] - aperture[0], cam['pos'][1] - aperture[1])
    anim_out = None
    if math.hypot(*off) > MOVE_CAMERA:
        delta = (-off[0], -off[1], 0.0)
        cam['pos'] = tuple(cam['pos'][j] + delta[j] for j in range(3))
        anim_out = a5.shift_positions(anim, gun['camera'], delta)
        moved = delta
    else:
        moved = None

    # The sight frame: centred on the camera's x, on the receiver's front block.
    z0, z1, ytop = gun['front']
    origin = (cam['pos'][0], ytop, (z0 + z1) / 2)
    top = cam['pos'][1] - ytop                  # the blade's top edge on the camera's axis
    mesh = sight_mesh(top)
    lod = mesh.lod((0.0, 0.0, 0.0), material_of(m, barrel))
    m['frames'].append(visual_frame(gun['sight'], bi + 1, origin, '', lod))

    names = [f['name'] for f in m['frames']]
    assert len(names) == len(set(names)), 'frame names must stay unique'
    data = h.dumps(m)
    assert h.dumps(h.loads(data)) == data, 'round trip'
    return m, data, anim_out, {
        'aperture': aperture, 'camera_was_off': off, 'moved': moved, 'camera': cam['pos'],
        'origin': origin, 'height': top, 'radius': origin[2] - (-0.334),
        'verts': len(mesh.verts), 'faces': len(mesh.faces)}


# ---- preview: the view from the camera ---------------------------------------------------------

def camera_view(m, camera, size=(640, 480), fov_y=50.0, zoom=1.0):
    """Perspective render from the frame `camera` along its +Z (a crosshair marks the centre)."""
    tris, wm = r.triangles(m)
    ci = index(m, camera)
    cm = wm[ci]
    eye = cm[3][:3]
    axes = [r.norm(cm[k][:3]) for k in range(3)]
    W, H = size
    f = H / 2 / math.tan(math.radians(fov_y) / 2) * zoom
    img = [[(150, 170, 190)] * W for _ in range(H)]
    zb = [[1e30] * W for _ in range(H)]
    sight = {i for i, fr in enumerate(m['frames']) if fr['name'].startswith('FrontSight')}
    light = r.norm((0.4, 0.8, -0.45))

    def proj(p):
        d = [p[k] - eye[k] for k in range(3)]
        v = [sum(d[k] * axes[j][k] for k in range(3)) for j in range(3)]
        return v
    for ta, tb, tc, mat, fi in tris:
        vs = [proj(p) for p in (ta, tb, tc)]
        if min(v[2] for v in vs) < 0.03:
            continue
        n = r.norm(r.cross([vs[1][k] - vs[0][k] for k in range(3)], [vs[2][k] - vs[0][k] for k in range(3)]))
        lam = abs(sum(n[k] * light[k] for k in range(3)))
        base = (200, 60, 40) if fi in sight else (70, 72, 66)
        col = tuple(int(min(255, c * (0.45 + 0.7 * lam))) for c in base)
        s = [(W / 2 + f * v[0] / v[2], H / 2 - f * v[1] / v[2], v[2]) for v in vs]
        x0 = max(0, int(min(p[0] for p in s))); x1 = min(W - 1, int(max(p[0] for p in s)) + 1)
        y0 = max(0, int(min(p[1] for p in s))); y1 = min(H - 1, int(max(p[1] for p in s)) + 1)
        den = (s[1][1] - s[2][1]) * (s[0][0] - s[2][0]) + (s[2][0] - s[1][0]) * (s[0][1] - s[2][1])
        if abs(den) < 1e-12:
            continue
        for y in range(y0, y1 + 1):
            for x in range(x0, x1 + 1):
                px, py = x + 0.5, y + 0.5
                l1 = ((s[1][1] - s[2][1]) * (px - s[2][0]) + (s[2][0] - s[1][0]) * (py - s[2][1])) / den
                l2 = ((s[2][1] - s[0][1]) * (px - s[2][0]) + (s[0][0] - s[2][0]) * (py - s[2][1])) / den
                l3 = 1 - l1 - l2
                if min(l1, l2, l3) < -1e-6:
                    continue
                # perspective-correct depth: interpolate 1/z
                iz = l1 / s[0][2] + l2 / s[1][2] + l3 / s[2][2]
                z = 1 / iz
                if z < zb[y][x]:
                    zb[y][x] = z
                    img[y][x] = col
    cx, cy = W // 2, H // 2
    for k in range(-40, 41):
        if abs(k) > 3:
            img[cy][cx + k] = (0, 230, 255)
            img[cy + k][cx] = (0, 230, 255)
    return img


def main():
    args = sys.argv[1:]
    src = args[args.index('--src') + 1] if '--src' in args else 'H:/HD2_Unpacked'
    out = args[args.index('--out') + 1] if '--out' in args else 'mods/m1919_front_sight'
    models = os.path.join(out, 'MODELS')
    os.makedirs(models, exist_ok=True)
    for gun in GUNS:
        m, data, anim, info = build(src, gun)
        path = os.path.join(models, gun['model'] + '.4ds')
        open(path, 'wb').write(data)
        print('%s: %d bytes' % (path, len(data)))
        if anim:
            apath = os.path.join(models, gun['anim'])
            open(apath, 'wb').write(anim)
            print('%s: %d bytes' % (apath, len(anim)))
        print('  aperture centre x %.5f y %.5f; camera was off it by x %+.2f mm y %+.2f mm%s' % (
            info['aperture'][0], info['aperture'][1], info['camera_was_off'][0] * 1000,
            info['camera_was_off'][1] * 1000, ', moved onto it' if info['moved'] else ', left'))
        print('  %s at (%.4f, %.4f, %.4f), %.1f mm tall to the camera axis y %.4f; sight radius %.0f mm; '
              '%d verts, %d faces' % (gun['sight'], *info['origin'], info['height'] * 1000, info['camera'][1],
                                     info['radius'] * 1000, info['verts'], info['faces']))
        for name, zoom in (('', 1.0), ('_zoom', 4.0)):
            png = os.path.join(out, 'preview_%s%s.png' % (gun['model'], name))
            r.write_png(png, camera_view(m, gun['camera'], zoom=zoom))
            print('  ' + png)


if __name__ == '__main__':
    main()
