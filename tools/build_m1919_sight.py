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

The fix adds a visual "FrontSight<seat>" below the barrel, modelled on photos of an M1919A4: a
bracket against the receiver's front face, standing on the jacket, with a slotted screw through its
rounded top; the sight block above it, split by a clamp slit; a thin blade in a dovetail key on top.
The blade's top edge is on the camera's axis, so the top of the post is at the centre of the
screen. The block and blade have the photo's sizes; the bracket is taller than the real one
(the model's raised rear leaf puts the line of sight 54 mm above the receiver, the real front
sight's top is about 32 mm above it). The camera must also look through the aperture's centre: w_1919Crouch's camera is 0.45 mm
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
from build_halftrack_mg42 import Mesh, cross3, index, norm3, visual_frame   # noqa: E402

TEXTURE = 'W_1919FPV.BMP'
# The leaf's own plain dark-metal patch of the texture (u0, v0, u1, v1).
UV_RECT = (216 / 256, 28 / 128, 240 / 256, 48 / 128)

# Per model. window: the aperture (upper window of the leaf) x0, x1, y0, y1 in barrel space, its
# corners checked against the mesh; receiver: the z of the receiver's front face and the y of its
# top there (the lowest point of the sloping top); jacket: the y of the barrel jacket's top just in
# front of the receiver.
GUNS = [
    {'model': 'w_1919Crouch', 'file': 'w_1919Crouch.4ds', 'barrel': 'BARREL00_00',
     'camera': 'cameraFPV', 'anim': 'w_1919Crouch_f00_00.5DS', 'sight': 'FrontSight00',
     'window': (-0.0043, 0.0035, 0.1620, 0.1723), 'receiver': (0.0351, 0.1123), 'jacket': 0.0789},
    {'model': 'la_Jeepsas', 'file': 'la_Jeepsas.ver', 'barrel': 'BARREL01_00',
     'camera': 'CAMERAFPVStatic', 'anim': 'la_Jeepsas_f01_00.5DS', 'sight': 'FrontSight01',
     'window': (-0.0027, 0.0028, 0.1481, 0.1584), 'receiver': (0.0112, 0.0984), 'jacket': 0.0700},
]
MOVE_CAMERA = 0.0010        # move a camera onto the aperture's centre when further off than this

# The M1919A4's front sight, measured on a side photo (0.275 mm a pixel, scaled by the jacket):
# a bracket bolted to the receiver's front face, standing on the jacket, its top rounded about the
# screw; above it the sight block, split by a clamp slit across it, with a thin blade in a dovetail
# key on top. Sizes in metres; z from the receiver's front face forwards, x from the bore line.
DEPTH = 0.0115              # bracket and block, front to back
BLOCK_W = 0.0100            # block, side to side
BLOCK_H = 0.0247            # block, from the screw's centre to its top
BRACKET_W = (0.0060, 0.0106)    # bracket, side to side at the bottom / at the screw
SCREW_R, SCREW_T = 0.00385, 0.0018      # slotted screw heads on both sides of the bracket
SLIT_Z, SLIT_W, SLIT_D = 0.0061, 0.0006, 0.0120     # clamp slit: z of its middle, width, depth
BLADE_Z = (0.0022, 0.0110)  # blade, front to back
BLADE_H = 0.0055            # blade, above the block (its top is on the camera's axis)
BLADE_T = 0.0015            # blade, side to side
KEY_H = 0.0008              # dovetail key under the blade, the block's width, above the block
SINK = 0.0030               # how far the bracket goes into the jacket


class UvMesh(Mesh):
    """Mesh with a planar texture mapping per vertex (along its normal's main axis) into UV_RECT."""

    def __init__(self, extent):
        super().__init__((0.0, 0.0))
        self.extent = extent        # (lo, hi) corners of the whole part, for the mapping

    def vert(self, p, n):
        k = max(range(3), key=lambda j: abs(n[j]))
        u_ax, v_ax = [j for j in range(3) if j != k]
        lo, hi = self.extent
        s = (p[u_ax] - lo[u_ax]) / (hi[u_ax] - lo[u_ax])
        t = 1 - (p[v_ax] - lo[v_ax]) / (hi[v_ax] - lo[v_ax])
        uv = (UV_RECT[0] + s * (UV_RECT[2] - UV_RECT[0]), UV_RECT[1] + t * (UV_RECT[3] - UV_RECT[1]))
        self.verts.append((tuple(p), tuple(n), uv))
        return len(self.verts) - 1

    def hexahedron(self, c):
        """A convex solid from 8 corners: c[0..3] the bottom ring, c[4..7] the top ring above them."""
        mid = tuple(sum(p[k] for p in c) / 8 for k in range(3))
        for face in ((0, 1, 2, 3), (4, 5, 6, 7), (0, 1, 5, 4), (1, 2, 6, 5), (2, 3, 7, 6), (3, 0, 4, 7)):
            ps = [c[i] for i in face]
            n = norm3(cross3([ps[1][k] - ps[0][k] for k in range(3)], [ps[2][k] - ps[0][k] for k in range(3)]))
            fc = tuple(sum(p[k] for p in ps) / 4 for k in range(3))
            if sum((fc[k] - mid[k]) * n[k] for k in range(3)) < 0:
                n = tuple(-v for v in n)
            self.quad(ps, n)

    def cylinder_x(self, cy, cz, r, x0, x1, seg=12, a0=0.0, a1=360.0, caps=True):
        """A cylinder (or an arc of one, angles from +z towards +y) along x."""
        full = a1 - a0 >= 360.0
        steps = seg if full else max(2, int(seg * (a1 - a0) / 360.0))
        ang = [math.radians(a0 + (a1 - a0) * i / steps) for i in range(steps + 1)]
        ring = [(math.sin(t), math.cos(t)) for t in ang]       # (y, z) directions
        for i in range(steps):
            (s0, c0), (s1, c1) = ring[i], ring[i + 1]
            tm = (ang[i] + ang[i + 1]) / 2
            n = (0.0, math.sin(tm), math.cos(tm))
            a = self.vert((x0, cy + r * s0, cz + r * c0), (0, s0, c0))
            b = self.vert((x1, cy + r * s0, cz + r * c0), (0, s0, c0))
            c = self.vert((x1, cy + r * s1, cz + r * c1), (0, s1, c1))
            d = self.vert((x0, cy + r * s1, cz + r * c1), (0, s1, c1))
            self.tri(a, b, c, n)
            self.tri(a, c, d, n)
        if caps:
            for x, nx in ((x0, -1.0), (x1, 1.0)):
                ctr = self.vert((x, cy, cz), (nx, 0, 0))
                ids = [self.vert((x, cy + r * s, cz + r * c), (nx, 0, 0)) for s, c in ring]
                for i in range(steps):
                    self.tri(ctr, ids[i], ids[i + 1], (nx, 0, 0))

    def lod(self, origin, material):
        vb = b''.join(struct.pack('<8f', p[0] - origin[0], p[1] - origin[1], p[2] - origin[2], *n, *uv)
                      for p, n, uv in self.verts)
        fb = b''.join(struct.pack('<3H', *f) for f in self.faces)
        assert len(self.verts) < 65536
        return {'distance': 0.0, 'unk': 0, 'vertices': vb, 'groups': [{'faces': fb, 'material': material}]}


def sight_mesh(top, jacket):
    """The front sight in its frame's space (origin: the bore line's x, the receiver's top, its
    front face). top: the blade's top edge (on the camera's axis); jacket: the jacket's top."""
    block_top = top - BLADE_H
    screw_y = block_top - BLOCK_H
    zc = DEPTH / 2
    bottom = jacket - SINK
    head = BRACKET_W[1] / 2 + SCREW_T
    mesh = UvMesh(((-head, bottom, 0.0), (head, top, DEPTH)))

    # the bracket: against the receiver's front face, tapering towards the jacket, its top
    # rounded about the screw
    wb, wt = BRACKET_W[0] / 2, BRACKET_W[1] / 2
    z0 = 0.0
    mesh.hexahedron([(-wb, bottom, z0), (wb, bottom, z0), (wb, bottom, DEPTH), (-wb, bottom, DEPTH),
                     (-wt, screw_y, z0), (wt, screw_y, z0), (wt, screw_y, DEPTH), (-wt, screw_y, DEPTH)])
    mesh.cylinder_x(screw_y, zc, DEPTH / 2, -wt, wt, seg=16, a0=0.0, a1=180.0)
    for side in (-1, 1):
        x0, x1 = sorted((side * wt, side * (wt + SCREW_T)))
        mesh.cylinder_x(screw_y, zc, SCREW_R, x0, x1, seg=12)

    # the block, split from the top by the clamp slit
    bw = BLOCK_W / 2
    slit_bottom = block_top - SLIT_D
    s0, s1 = SLIT_Z - SLIT_W / 2, SLIT_Z + SLIT_W / 2
    mesh.aabox((-bw, screw_y, 0.0), (bw, slit_bottom, DEPTH))
    mesh.aabox((-bw, slit_bottom, 0.0), (bw, block_top, s0))
    mesh.aabox((-bw, slit_bottom, s1), (bw, block_top, DEPTH))

    # the dovetail key across the block and the blade on it
    mesh.aabox((-bw, block_top - 0.001, BLADE_Z[0]), (bw, block_top + KEY_H, BLADE_Z[1]))
    mesh.aabox((-BLADE_T / 2, block_top + KEY_H - 0.0005, BLADE_Z[0]), (BLADE_T / 2, top, BLADE_Z[1]))
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

    # The sight frame: on the camera's x, at the receiver's front face and top.
    zf, ytop = gun['receiver']
    origin = (cam['pos'][0], ytop, zf)
    top = cam['pos'][1] - ytop                  # the blade's top edge on the camera's axis
    mesh = sight_mesh(top, gun['jacket'] - ytop)
    lod = mesh.lod((0.0, 0.0, 0.0), material_of(m, barrel))
    m['frames'].append(visual_frame(gun['sight'], bi + 1, origin, '', lod))

    names = [f['name'] for f in m['frames']]
    assert len(names) == len(set(names)), 'frame names must stay unique'
    data = h.dumps(m)
    assert h.dumps(h.loads(data)) == data, 'round trip'
    return m, data, anim_out, {
        'aperture': aperture, 'camera_was_off': off, 'moved': moved, 'camera': cam['pos'],
        'origin': origin, 'height': top, 'radius': origin[2] + sum(BLADE_Z) / 2 - (-0.334),
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
        png = os.path.join(out, 'preview_%s_outside.png' % gun['model'])
        r.write_png(png, outside_views(m, gun['sight']))
        print('  ' + png)


def outside_views(m, sight, size=420):
    """The sight and the receiver's front from the right (as the side photo), the front right
    and the back right, framed on the sight."""
    tris, wm = r.triangles(m)
    c = wm[index(m, sight)][3][:3]
    box = (c[0] - 0.035, c[1] - 0.045, c[2] - 0.05, c[0] + 0.035, c[1] + 0.065, c[2] + 0.06)
    near = [t for t in tris if all(min(p[k] for p in t[:3]) <= box[k + 3] and max(p[k] for p in t[:3]) >= box[k]
                                   for k in range(3))]
    imgs = [r.render(near, [], v, size, box, sight) for v in ('right', 'iso3', 'iso2')]
    return [sum((im[y] for im in imgs), []) for y in range(size)]


if __name__ == '__main__':
    main()
