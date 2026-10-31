"""The decorator grid's Follow (0x0047eb80), from the asm (not the C++ port): the world cells refilled
for a sequence of Follow calls. Floats as numpy-free 32-bit via struct."""
import math
import struct
import sys


def f(v):
    return struct.unpack("f", struct.pack("f", v))[0]


def trunc(v):
    return int(v)  # __ftol truncates toward 0


def half(n):
    return int(n / 2)  # signed / 2 truncating


class G:
    def __init__(s, size, cs):
        s.sx = s.sz = size
        s.cs = f(cs)
        s.ox = s.oz = -1
        s.shx = s.shz = 0
        s.subx = s.subz = 0
        s.out = []

    def refill(s, z, x):
        bx = s.ox - half(s.sx) + s.shx
        bz = s.oz - half(s.sz) + s.shz
        s.out.append((bx + x, bz + z))

    def col(s, x):
        for z in range(s.sz):
            s.refill(z, x)

    def row(s, z):
        for x in range(s.sx):
            s.refill(z, x)

    def all(s):
        s.shx = s.shz = 0
        for x in range(s.sx):
            s.col(x)

    def shiftx(s, back):
        if not back:
            s.shx += 1
            if s.shx >= s.sx:
                s.shx = 0
                s.ox += s.sx
        else:
            s.shx -= 1
            if s.shx < 0:
                s.shx = s.sx - 1
                s.ox -= s.sx

    def shiftz(s, back):
        if not back:
            s.shz += 1
            if s.shz >= s.sz:
                s.shz = 0
                s.oz += s.sz
        else:
            s.shz -= 1
            if s.shz < 0:
                s.shz = s.sz - 1
                s.oz -= s.sz

    def follow(s, x, z):
        x, z = f(x), f(z)
        inv = f(1.0 / s.cs)
        cx = trunc(f(x * inv))
        cz = trunc(f(z * inv))
        if x < 0:
            cx -= 1
        if z < 0:
            cz -= 1
        dx = cx - s.ox - s.shx
        dz = cz - s.shz - s.oz
        ax, az = abs(dx), abs(dz)
        px = f(x - f(cx * s.cs))
        pz = f(z - f(cz * s.cs))
        if ax >= half(s.sx) or az >= half(s.sz):
            s.subx = trunc(f(f(px * inv) * s.sz))
            s.subz = trunc(f(f(pz * inv) * s.sx))
            s.ox, s.oz = cx, cz
            s.all()
            return
        if s.ox + s.shx == cx:
            n = trunc(f(f(px * inv) * s.sz))
            if n > s.subx:
                for i in range(s.subx + 1, n + 1):
                    s.refill(i, s.sx - 1)
                s.subx = n
            elif n < s.subx:
                for i in range(n + 1, s.subx + 1):
                    s.refill(i, 0)
                s.subx = n
        if s.shz + s.oz == cz:
            n = trunc(f(f(pz / s.cs) * s.sx))
            if n > s.subz:
                for i in range(s.subz + 1, n + 1):
                    s.refill(s.sz - 1, i)
                s.subz = n
            elif n < s.subz:
                for i in range(n + 1, s.subz + 1):
                    s.refill(0, i)
                s.subz = n
        if s.shx + s.ox != cx:
            if dx > 0:
                for i in range(s.subx + 1, s.sz):
                    s.refill(i, s.sx - 1)
            elif dx < 0:
                for i in range(0, s.subx + 1):
                    s.refill(i, 0)
            if ax:
                if dx > 0:
                    n = ax
                    while True:
                        n -= 1
                        s.shiftx(False)
                        if n == 0:
                            break
                        s.col(s.sx - 1)
                    s.refill(s.sz - 1, s.subz)
                    if s.subz > 0:
                        s.refill(s.sz - 1, s.subz - 1)
                elif dx < 0:
                    n = ax
                    while n:
                        n -= 1
                        s.shiftx(True)
                        if n:
                            s.col(0)
                        if s.subz < s.sx - 1:
                            s.refill(0, s.subz + 1)
                n = trunc(f(f(px / s.cs) * s.sz))
                if dx > 0:
                    for i in range(0, n + 1):
                        s.refill(i, s.sx - 1)
                    s.subx = n
                if dx < 0:
                    for i in range(n + 1, s.sz):
                        s.refill(i, 0)
                    s.subx = n
        if s.shz + s.oz != cz:
            if dz > 0:
                for i in range(s.subz + 1, s.sx):
                    s.refill(s.sz - 1, i)
            elif dz < 0:
                for i in range(0, s.subz + 1):
                    s.refill(0, i)
            if az:
                if dz > 0:
                    n = az
                    while True:
                        n -= 1
                        s.shiftz(False)
                        if n == 0:
                            break
                        s.row(s.sz - 1)
                    s.refill(s.subx, s.sx - 1)
                    if s.subx > 0:
                        s.refill(s.subx - 1, s.sx - 1)
                elif dz < 0:
                    n = az
                    while n:
                        n -= 1
                        s.shiftz(True)
                        if n:
                            s.row(0)
                        if s.subx < s.sz - 1:
                            s.refill(s.subx + 1, 0)
                n = trunc(f(f(pz / s.cs) * s.sx))
                if dz > 0:
                    for i in range(0, n + 1):
                        s.refill(s.sz - 1, i)
                    s.subz = n
                if dz < 0:
                    for i in range(n + 1, s.sx):
                        s.refill(0, i)
                    s.subz = n


def run(size, steps):
    g = G(size, 1.0)
    for st in steps:
        if st[0] == "set":
            setattr(g, st[1], st[2])
            continue
        g.out = []
        g.follow(st[0], st[1])
        print(f"Follow({st[0]}, {st[1]}): {g.out} ox {g.ox} oz {g.oz} shx {g.shx} shz {g.shz} subx {g.subx} subz {g.subz}")


if __name__ == "__main__":
    run(int(sys.argv[1]) if len(sys.argv) > 1 else 4, eval(sys.argv[2]))
