"""How deep frames that turn with a seat weapon dip into the hull as they turn.

    python tools/roof_contact.py MODEL.4ds SEAT FRAME[,FRAME...] [--hull telo] [--max 37] [--step 1]

Samples the frames' LOD 0 edges, turns them about HORT<seat>'s vertical axis and, for each angle,
measures how far the lowest points sit below the hull surface straight under them (rays cast
down onto the hull). A shield standing on the roof plate reads 0; positive numbers are the depth
it cuts into raised parts of the roof.
"""
import math
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import hd2_4ds as h          # noqa: E402
import render4ds as r        # noqa: E402
from build_halftrack_mg42 import ray_hit   # noqa: E402


def main():
    args = sys.argv[1:]
    path, seat, names = args[0], int(args[1]), set(args[2].split(','))

    def opt(n, d):
        return args[args.index(n) + 1] if n in args else d
    hull_names = set(opt('--hull', 'telo').split(','))
    top, step = float(opt('--max', '37')), float(opt('--step', '1'))
    m = h.load(path)
    fnames = [f['name'] for f in m['frames']]
    tris, wm = r.triangles(m)
    hort = fnames.index('HORT%02d' % seat)
    axis = (wm[hort][3][0], wm[hort][3][2])
    moving = [t[:3] for t in tris if fnames[t[4]] in names]
    hull = [t[:3] for t in tris if fnames[t[4]] in hull_names]
    # sample points along the edges near the bottom (within 5 cm of the lowest point)
    ymin = min(p[1] for t in moving for p in t)
    pts = set()
    for t in moving:
        for i in range(3):
            a, b = t[i], t[(i + 1) % 3]
            for k in range(11):
                p = tuple(a[j] + (b[j] - a[j]) * k / 10 for j in range(3))
                if p[1] < ymin + 0.05:
                    pts.add(tuple(round(c, 5) for c in p))
    pts = list(pts)
    near = [t for t in hull if max(p[1] for p in t) > ymin - 0.1]
    print('%d sample points near the bottom (y %.3f), %d hull triangles' % (len(pts), ymin, len(near)))
    worst = []
    for sign in (-1, 1):
        a = 0.0
        while a <= top + 1e-9:
            c, s = math.cos(math.radians(sign * a)), math.sin(math.radians(sign * a))
            deepest = 0.0
            for x, y, z in pts:
                dx, dz = x - axis[0], z - axis[1]
                px, pz = axis[0] + dx * c + dz * s, axis[1] - dx * s + dz * c
                d = ray_hit(near, (px, y + 0.2, pz), (0.0, -1.0, 0.0))
                if d is not None:
                    deepest = max(deepest, 0.2 - d)
            worst.append((sign * a, deepest))
            a += step
    for ang, d in sorted(worst):
        if abs(ang) % 5 == 0 or d > 0.002:
            print('traverse %+5.1f: %.1f mm into the hull' % (ang, d * 1000))
    print('deepest: %.1f mm' % (max(d for _, d in worst) * 1000))


if __name__ == '__main__':
    main()
