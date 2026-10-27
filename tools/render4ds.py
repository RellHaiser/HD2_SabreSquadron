"""Renders a .4ds model to a PNG (pure Python z-buffer, flat shading, one color per material).

    python tools/render4ds.py MODEL.4ds OUT.png [--views side,top,front,iso] [--size 480]
                              [--mark NAME,NAME...] [--hide NAME,NAME...] [--lod 0]
                              [--box x0,y0,z0,x1,y1,z1] [--pose SEAT:TRAVERSE:ELEVATION]

Frame transforms follow LS3D: row vectors, world = scale * rotation * translation * parent,
the stored quaternion (x, y, z, w) taken as its conjugate in D3DX terms (cameras pitch down,
the halftrack's bench seats face inwards). +X right, +Y up, +Z forward.
Marked frames (dummies) are drawn as dots with their names. --pose turns a seat weapon (its
HORT<seat> / VERT<seat> frames) by degrees: traverse > 0 points the muzzle to +x, elevation > 0 up.
"""
import math
import struct
import sys
import zlib
import os

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import hd2_4ds as h


def quat_matrix(q):
    x, y, z, w = q
    x, y, z = -x, -y, -z
    return [[1 - 2 * (y * y + z * z), 2 * (x * y + z * w), 2 * (x * z - y * w)],
            [2 * (x * y - z * w), 1 - 2 * (x * x + z * z), 2 * (y * z + x * w)],
            [2 * (x * z + y * w), 2 * (y * z - x * w), 1 - 2 * (x * x + y * y)]]


def local_matrix(f):
    r = quat_matrix(f['rot'])
    sx, sy, sz = f['scale']
    m = [[r[0][j] * sx for j in range(3)] + [0.0],
         [r[1][j] * sy for j in range(3)] + [0.0],
         [r[2][j] * sz for j in range(3)] + [0.0],
         list(f['pos']) + [1.0]]
    return m


def mul(a, b):
    return [[sum(a[i][k] * b[k][j] for k in range(4)) for j in range(4)] for i in range(4)]


def xform(v, m):
    x, y, z = v
    return (x * m[0][0] + y * m[1][0] + z * m[2][0] + m[3][0],
            x * m[0][1] + y * m[1][1] + z * m[2][1] + m[3][1],
            x * m[0][2] + y * m[1][2] + z * m[2][2] + m[3][2])


IDENT = [[1.0, 0, 0, 0], [0, 1.0, 0, 0], [0, 0, 1.0, 0], [0, 0, 0, 1.0]]


def world_matrices(m):
    out = []
    for f in m['frames']:
        lm = local_matrix(f)
        out.append(mul(lm, out[f['parent'] - 1]) if f['parent'] else lm)
    return out


def triangles(m, lod=0, hide=(), only=None):
    """World-space triangles [(a, b, c, material, frame index)] of every visual frame."""
    wm = world_matrices(m)
    frames = m['frames']
    hidden = set()
    for i, f in enumerate(frames):
        if f['name'] in hide or (f['parent'] and (f['parent'] - 1) in hidden):
            hidden.add(i)
    tris = []
    for i, f in enumerate(frames):
        if f['type'] != h.FRAME_VISUAL or i in hidden or 'lods' not in f and not f.get('instance'):
            continue
        if only is not None and i not in only:
            continue
        src = frames[f['instance'] - 1] if f.get('instance') else f
        if 'lods' not in src or not src['lods']:
            continue
        l = src['lods'][min(lod, len(src['lods']) - 1)]
        vb = l['vertices']
        verts = [xform(struct.unpack_from('<3f', vb, k * 32), wm[i]) for k in range(len(vb) // 32)]
        for g in l['groups']:
            fb = g['faces']
            for k in range(0, len(fb), 6):
                a, b, c = struct.unpack_from('<3H', fb, k)
                tris.append((verts[a], verts[b], verts[c], g['material'], i))
    return tris, wm


def cross(a, b):
    return (a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0])


def norm(v):
    l = math.sqrt(sum(c * c for c in v)) or 1.0
    return tuple(c / l for c in v)


def look(eye_dir, up=(0.0, 1.0, 0.0)):
    """A view from direction eye_dir (pointing from the model to the camera), D3D LookAtLH."""
    z = norm(tuple(-c for c in eye_dir))
    x = norm(cross(up, z))
    y = cross(z, x)
    return lambda p: (p[0] * x[0] + p[1] * x[1] + p[2] * x[2],
                      p[0] * y[0] + p[1] * y[1] + p[2] * y[2],
                      p[0] * z[0] + p[1] * z[1] + p[2] * z[2])


def orbit(yaw, pitch):
    """Camera direction: yaw 0 = in front (+Z), 90 = on the left (-X); pitch up from level."""
    cy, sy = math.cos(math.radians(yaw)), math.sin(math.radians(yaw))
    cp, sp = math.cos(math.radians(pitch)), math.sin(math.radians(pitch))
    return look((-sy * cp, sp, cy * cp))


VIEWS = {
    'side': look((-1.0, 0.0, 0.0)),            # from the left, front to the left
    'right': look((1.0, 0.0, 0.0)),
    'top': look((0.0, 1.0, 0.0), up=(0.0, 0.0, 1.0)),   # front up
    'front': look((0.0, 0.0, 1.0)),
    'back': look((0.0, 0.0, -1.0)),
    'iso': orbit(40, 30),       # front left, above
    'iso2': orbit(220, 35),     # back right, above
    'iso3': orbit(-40, 30),     # front right, above
    'iso4': orbit(140, 35),     # back left, above
}


def parse_view(v):
    """'yaw:pitch' or a named view."""
    if ':' in v:
        y, p = v.split(':')
        return orbit(float(y), float(p))
    return VIEWS[v]


def color(mat):
    hsh = (mat * 2654435761) & 0xffffffff
    return (90 + (hsh & 0x7f), 90 + ((hsh >> 8) & 0x7f), 90 + ((hsh >> 16) & 0x7f))


FONT = {}  # tiny 3x5 font for labels
_F = """0:111101101101111 1:010110010010111 2:111001111100111 3:111001111001111 4:101101111001001
5:111100111001111 6:111100111101111 7:111001001001001 8:111101111101111 9:111101111001111
A:010101111101101 B:110101110101110 C:111100100100111 D:110101101101110 E:111100110100111
F:111100110100100 G:111100101101111 H:101101111101101 I:111010010010111 J:001001001101111
K:101101110101101 L:100100100100111 M:101111111101101 N:111101101101101 O:111101101101111
P:111101111100100 Q:111101101111001 R:111101110101101 S:111100111001111 T:111010010010010
U:101101101101111 V:101101101101010 W:101101111111101 X:101101010101101 Y:101101010010010
Z:111001010100111 _:000000000000111 -:000000111000000"""
for tok in _F.split():
    k, bits = tok.split(':')
    FONT[k] = bits


def render(tris, marks, view, size, box=None, title=''):
    proj = parse_view(view)
    pts = [proj(p) for t in tris for p in t[:3]] + [proj(p) for _, p in marks]
    if box:
        corners = [(x, y, z) for x in (box[0], box[3]) for y in (box[1], box[4]) for z in (box[2], box[5])]
        pts = [proj(c) for c in corners]
    if not pts:
        return [[(255, 255, 255)] * size for _ in range(size)]
    minx = min(p[0] for p in pts); maxx = max(p[0] for p in pts)
    miny = min(p[1] for p in pts); maxy = max(p[1] for p in pts)
    span = max(maxx - minx, maxy - miny) * 1.08 or 1.0
    cx, cy = (minx + maxx) / 2, (miny + maxy) / 2
    sc = size / span
    W = H = size
    img = [[(255, 255, 255)] * W for _ in range(H)]
    zb = [[1e30] * W for _ in range(H)]
    light = (0.4, 0.8, -0.45)
    ll = math.sqrt(sum(c * c for c in light))
    light = tuple(c / ll for c in light)
    for a, b, c, mat, fi in tris:
        pa, pb, pc = proj(a), proj(b), proj(c)
        # shade by the view-space normal
        ux, uy, uz = pb[0] - pa[0], pb[1] - pa[1], pb[2] - pa[2]
        vx, vy, vz = pc[0] - pa[0], pc[1] - pa[1], pc[2] - pa[2]
        nx, ny, nz = uy * vz - uz * vy, uz * vx - ux * vz, ux * vy - uy * vx
        nl = math.sqrt(nx * nx + ny * ny + nz * nz) or 1.0
        lam = abs(nx * light[0] + ny * light[1] + nz * light[2]) / nl
        base = color(mat)
        col = tuple(int(min(255, ch * (0.35 + 0.75 * lam))) for ch in base)
        sa = ((pa[0] - cx) * sc + W / 2, H / 2 - (pa[1] - cy) * sc, pa[2])
        sb = ((pb[0] - cx) * sc + W / 2, H / 2 - (pb[1] - cy) * sc, pb[2])
        sc2 = ((pc[0] - cx) * sc + W / 2, H / 2 - (pc[1] - cy) * sc, pc[2])
        x0 = max(0, int(min(sa[0], sb[0], sc2[0]))); x1 = min(W - 1, int(max(sa[0], sb[0], sc2[0])) + 1)
        y0 = max(0, int(min(sa[1], sb[1], sc2[1]))); y1 = min(H - 1, int(max(sa[1], sb[1], sc2[1])) + 1)
        den = (sb[1] - sc2[1]) * (sa[0] - sc2[0]) + (sc2[0] - sb[0]) * (sa[1] - sc2[1])
        if abs(den) < 1e-12:
            continue
        for y in range(y0, y1 + 1):
            row = img[y]; zrow = zb[y]
            for x in range(x0, x1 + 1):
                px, py = x + 0.5, y + 0.5
                l1 = ((sb[1] - sc2[1]) * (px - sc2[0]) + (sc2[0] - sb[0]) * (py - sc2[1])) / den
                l2 = ((sc2[1] - sa[1]) * (px - sc2[0]) + (sa[0] - sc2[0]) * (py - sc2[1])) / den
                l3 = 1 - l1 - l2
                if l1 < -1e-6 or l2 < -1e-6 or l3 < -1e-6:
                    continue
                z = l1 * sa[2] + l2 * sb[2] + l3 * sc2[2]
                if z < zrow[x]:
                    zrow[x] = z
                    row[x] = col
    for name, p in marks:
        pp = proj(p)
        x = int((pp[0] - cx) * sc + W / 2); y = int(H / 2 - (pp[1] - cy) * sc)
        for dy in range(-2, 3):
            for dx in range(-2, 3):
                if 0 <= x + dx < W and 0 <= y + dy < H:
                    img[y + dy][x + dx] = (220, 0, 0)
        text(img, name.upper(), x + 4, y - 7, (200, 0, 0))
    text(img, (view + ' ' + title).upper(), 3, 3, (0, 0, 160))
    # scale bar: 1 m
    text(img, '1M', 3, H - 16, (0, 0, 0))
    for x in range(3, min(W, 3 + int(sc))):
        img[H - 6][x] = (0, 0, 0)
    return img


def text(img, s, x, y, col):
    H, W = len(img), len(img[0])
    for ch in s:
        bits = FONT.get(ch)
        if bits:
            for r in range(5):
                for c in range(3):
                    if bits[r * 3 + c] == '1':
                        for yy in range(2):
                            for xx in range(2):
                                X, Y = x + c * 2 + xx, y + r * 2 + yy
                                if 0 <= X < W and 0 <= Y < H:
                                    img[Y][X] = col
        x += 8


def write_png(path, rows):
    H, W = len(rows), len(rows[0])
    raw = b''.join(b'\x00' + bytes(v for px in row for v in px) for row in rows)

    def chunk(t, d):
        c = struct.pack('>I', len(d)) + t + d
        return c + struct.pack('>I', zlib.crc32(t + d) & 0xffffffff)
    png = b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', W, H, 8, 2, 0, 0, 0))
    png += chunk(b'IDAT', zlib.compress(raw, 6)) + chunk(b'IEND', b'')
    open(path, 'wb').write(png)


def axis_quat(axis, deg, comp, sign):
    """The stored quaternion turning about `axis` (0 x, 1 y) by |deg| so that the +z direction
    gets component `comp` of sign `sign`."""
    a = math.radians(abs(deg)) / 2
    for s in (1, -1):
        q = [0.0, 0.0, 0.0, math.cos(a)]
        q[axis] = s * math.sin(a)
        if deg == 0 or (quat_matrix(q)[2][comp] > 0) == (sign > 0):
            return tuple(q)
    raise AssertionError


def pose_seat_weapon(m, seat, trav, elev):
    names = [f['name'] for f in m['frames']]
    m['frames'][names.index('HORT%02d' % seat)]['rot'] = axis_quat(1, trav, 0, trav)
    m['frames'][names.index('VERT%02d' % seat)]['rot'] = axis_quat(0, elev, 1, elev)


def main():
    args = sys.argv[1:]
    src, out = args[0], args[1]

    def opt(name, default=None):
        return args[args.index(name) + 1] if name in args else default
    views = opt('--views', 'side,top,front,iso').split(',')
    size = int(opt('--size', '480'))
    lod = int(opt('--lod', '0'))
    hide = set(filter(None, opt('--hide', '').split(',')))
    markn = list(filter(None, opt('--mark', '').split(',')))
    box = [float(v) for v in opt('--box').split(',')] if opt('--box') else None
    m = h.load(src)
    if opt('--pose'):
        seat, trav, elev = opt('--pose').split(':')
        pose_seat_weapon(m, int(seat), float(trav), float(elev))
    tris, wm = triangles(m, lod, hide)
    if box:
        tris = [t for t in tris if all(box[k] <= sum(p[k] for p in t[:3]) / 3 <= box[k + 3] for k in range(3))]
    marks = []
    for i, f in enumerate(m['frames']):
        if any(f['name'] == n or (n.endswith('*') and f['name'].startswith(n[:-1])) for n in markn):
            marks.append((f['name'], (wm[i][3][0], wm[i][3][1], wm[i][3][2])))
    imgs = [render(tris, marks, v, size, box, os.path.basename(src)) for v in views]
    cols = 2 if len(imgs) > 1 else 1
    rows_n = (len(imgs) + cols - 1) // cols
    canvas = [[(255, 255, 255)] * (size * cols) for _ in range(size * rows_n)]
    for k, im in enumerate(imgs):
        ox, oy = (k % cols) * size, (k // cols) * size
        for y in range(size):
            canvas[oy + y][ox:ox + size] = im[y]
        for y in range(size):
            canvas[oy + y][ox] = (180, 180, 180)
    write_png(out, canvas)


if __name__ == '__main__':
    main()
