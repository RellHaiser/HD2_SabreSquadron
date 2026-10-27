"""Checks a seat weapon against fixed geometry over its traverse / elevation range.

    python tools/gun_clearance.py MODEL.4ds SEAT OBSTACLE[,OBSTACLE...] [--step 1]
                                  [--traverse-with FRAME,...] [--fixed FRAME,...] [--skip FRAME,...] [--pivot-dz DZ] [--lift DY] [--elev E,...]

Turns every visual below HORT<seat> (the gun, its belt, sights) about the pivot -- elevation about
VERT's x axis, then traverse about HORT's y axis, as the seat weapon does -- and tests its LOD 0
triangles for intersection with the obstacles' LOD 0 triangles (e.g. the gun shield "Loft01").
Prints, for elevations from the barrel's down to up limit, the largest clear traverse each way.

What-ifs: --traverse-with moves frames with the traverse only (a shield fixed to the mount);
OBSTACLE "*" means every other visual of the model; --pivot-dz moves the traverse axis along z
(e.g. behind the shield) while the gun still elevates about VERT; --lift raises the carried frames.
"""
import math
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import hd2_4ds as h          # noqa: E402
import render4ds as r        # noqa: E402


def sub(a, b): return (a[0] - b[0], a[1] - b[1], a[2] - b[2])
def dot(a, b): return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]
def cross(a, b): return (a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0])


def seg_tri(p, q, a, b, c):
    """Segment pq crosses triangle abc (Moller-Trumbore on the segment)."""
    e1, e2 = sub(b, a), sub(c, a)
    d = sub(q, p)
    hv = cross(d, e2)
    det = dot(e1, hv)
    if abs(det) < 1e-12:
        return False
    inv = 1.0 / det
    s = sub(p, a)
    u = dot(s, hv) * inv
    if u < 0.0 or u > 1.0:
        return False
    qv = cross(s, e1)
    v = dot(d, qv) * inv
    if v < 0.0 or u + v > 1.0:
        return False
    t = dot(e2, qv) * inv
    return 0.0 <= t <= 1.0


def tri_tri(t1, t2):
    for i in range(3):
        if seg_tri(t1[i], t1[(i + 1) % 3], *t2) or seg_tri(t2[i], t2[(i + 1) % 3], *t1):
            return True
    return False


def bbox(t):
    return (min(p[0] for p in t), min(p[1] for p in t), min(p[2] for p in t),
            max(p[0] for p in t), max(p[1] for p in t), max(p[2] for p in t))


def overlap(a, b):
    return all(a[k] <= b[k + 3] and b[k] <= a[k + 3] for k in range(3))


def main():
    path, seat, obstacles = sys.argv[1], int(sys.argv[2]), sys.argv[3].split(',')
    step = float(sys.argv[sys.argv.index('--step') + 1]) if '--step' in sys.argv else 1.0
    m = h.load(path)
    names = [f['name'] for f in m['frames']]
    hort = names.index('HORT%02d' % seat)
    vert = names.index('VERT%02d' % seat)
    barrel = m['frames'][names.index('BARREL%02d_00' % seat)]
    lim = {k: float(re.search(k + r'(\d+)', barrel['params']).group(1)) for k in ('up', 'down', 'left', 'right')}

    def below(root):
        out = set()
        for i in range(len(m['frames'])):
            j = i
            while m['frames'][j]['parent']:
                j = m['frames'][j]['parent'] - 1
                if j == root:
                    out.add(i)
                    break
        return out
    elevating = below(vert)                          # the gun: elevation and traverse
    with_names = set(sys.argv[sys.argv.index('--traverse-with') + 1].split(',')) if '--traverse-with' in sys.argv else set()
    # --fixed: frames below HORT kept still, as obstacles (the gun against a shield that turns with
    # it: only the elevation moves the one against the other, so look at the traverse-0 column)
    fixed = set(sys.argv[sys.argv.index('--fixed') + 1].split(',')) if '--fixed' in sys.argv else set()
    # --skip: frames left out altogether (neither moving nor obstacle)
    skip = {i for i, n in enumerate(names) if n in (set(sys.argv[sys.argv.index('--skip') + 1].split(','))
                                                     if '--skip' in sys.argv else set())}
    elevating -= skip
    traversing = (below(hort) - elevating - {vert} - {i for i, n in enumerate(names) if n in fixed}) | \
        {i for i, n in enumerate(names) if n in with_names}
    traversing -= skip
    moving_frames = elevating | traversing | skip
    dz = float(sys.argv[sys.argv.index('--pivot-dz') + 1]) if '--pivot-dz' in sys.argv else 0.0
    tris, wm = r.triangles(m)
    pivot = tuple(wm[vert][3][k] for k in range(3))                          # elevation
    trav_pivot = (wm[hort][3][0], wm[hort][3][1], wm[hort][3][2] + dz)    # traverse
    gun = [t[:3] for t in tris if t[4] in elevating]
    lift = float(sys.argv[sys.argv.index('--lift') + 1]) if '--lift' in sys.argv else 0.0   # raise --traverse-with frames
    carried = [[(p[0], p[1] + (lift if names[t[4]] in with_names else 0.0), p[2]) for p in t[:3]]
               for t in tris if t[4] in traversing]
    # only obstacles the moving parts can reach: within their distance of the traverse axis and
    # their height range (elevation adds at most the gun's length times the sine of the limit)
    moving_pts = [p for t in gun + carried for p in t]
    reach = max(math.hypot(p[0] - trav_pivot[0], p[2] - trav_pivot[2]) for p in moving_pts) + 0.01
    gun_len = max((math.dist(p, pivot) for t in gun for p in t), default=0.0)
    ylo = min(p[1] for p in moving_pts) - gun_len * math.sin(math.radians(max(lim['up'], lim['down']))) - 0.01
    yhi = max(p[1] for p in moving_pts) + gun_len * math.sin(math.radians(max(lim['up'], lim['down']))) + 0.01

    def reachable(t):
        if max(p[1] for p in t[:3]) < ylo or min(p[1] for p in t[:3]) > yhi:
            return False
        return min(math.hypot(p[0] - trav_pivot[0], p[2] - trav_pivot[2]) for p in t[:3]) <= reach or \
            math.hypot(sum(p[0] for p in t[:3]) / 3 - trav_pivot[0], sum(p[2] for p in t[:3]) / 3 - trav_pivot[2]) <= reach + 0.5
    picked = [t for t in tris if t[4] not in moving_frames and (obstacles == ['*'] or names[t[4]] in obstacles)
              and reachable(t)]
    obst = [t[:3] for t in picked]
    obst_owner = [names[t[4]] for t in picked]
    obst_boxes = [bbox(t) for t in obst]
    print('elevation pivot', tuple(round(c, 3) for c in pivot), 'traverse axis', tuple(round(c, 3) for c in trav_pivot),
          '| %d gun + %d traversing triangles, %d obstacle triangles' % (len(gun), len(carried), len(obst)))
    print('limits in the params: up %g down %g left %g right %g' % (lim['up'], lim['down'], lim['left'], lim['right']))

    def rot(p, o, axis, deg):
        c, s = math.cos(math.radians(deg)), math.sin(math.radians(deg))
        x, y, z = sub(p, o)
        if axis == 'x':      # elevation: the muzzle (+z) goes up for deg > 0
            y, z = y * c + z * s, -y * s + z * c
        else:                # traverse: the muzzle goes to +x for deg > 0
            x, z = x * c + z * s, -x * s + z * c
        return (x + o[0], y + o[1], z + o[2])

    def turned(t, trav, elev):
        return [rot(rot(p, pivot, 'x', elev), trav_pivot, 'y', trav) for p in t]

    def moving(trav, elev):
        for t in gun:
            yield turned(t, trav, elev)
        for t in carried:
            yield [rot(p, trav_pivot, 'y', trav) for p in t]

    def hits(trav, elev, skip=frozenset()):
        out = set()
        for tt in moving(trav, elev):
            bb = bbox(tt)
            for j, (ob, o) in enumerate(zip(obst_boxes, obst)):
                if j not in skip and j not in out and overlap(bb, ob) and tri_tri(tt, o):
                    out.add(j)
        return out

    # contacts already there at rest (the shield sitting on the roof) are not clipping
    resting = frozenset(hits(0.0, 0.0))
    if resting:
        print('touching at rest (ignored):', sorted({obst_owner[j] for j in resting}))
    blocker = {}

    def clear(trav, elev):
        found = hits(trav, elev, resting)
        if found:
            blocker[(trav, elev)] = sorted({obst_owner[j] for j in found})
        return not found

    elevs = sorted({-lim['down'], -lim['down'] / 2, 0.0, lim['up'] / 2, lim['up']})
    if '--elev' in sys.argv:
        elevs = [float(e) for e in sys.argv[sys.argv.index('--elev') + 1].split(',')]
    for elev in elevs:
        row = []
        for sign in (-1, 1):
            a = 0.0
            if not clear(0.0, elev):
                row.append('blocked at 0 by %s' % ','.join(blocker[(0.0, elev)]))
                continue
            while a + step <= 45 and clear(sign * (a + step), elev):
                a += step
            by = blocker.get((sign * (a + step), elev))
            row.append('%s %4.1f%s' % ('-x' if sign < 0 else '+x', a, ' (then %s)' % ','.join(by) if by else ''))
        print('elevation %+5.1f: clear to %s' % (elev, ', '.join(row)))


if __name__ == '__main__':
    main()
