"""Reader / writer for Hidden & Dangerous 2 .4ds models (LS3D format version 0x29).

The layout was worked out from the shipped models (H:/HD2_Unpacked/MODELS) and is close to
Mafia's version 29: every frame stores its transform relative to its parent (1-based index,
0 = model root), standard meshes keep vertices as position / normal / uv (32 bytes).

One schema function per record serves both directions (a Stream either reads fields into the
dict or writes them from it), so a parsed model writes back byte-identical
(`python tools/hd2_4ds.py --roundtrip DIR` checks the whole corpus).

Model = {'version', 'stamp', 'materials': [...], 'frames': [...], 'tail': bytes}
Frame = {'type', 'vtype', 'rflags', 'parent', 'pos', 'rot' (x, y, z, w), 'scale', 'unk',
         'cull', 'name', 'params', ...type-specific data}
"""
import struct
import sys

FRAME_VISUAL, FRAME_SECTOR, FRAME_DUMMY, FRAME_TARGET, FRAME_JOINT, FRAME_OCCLUDER = 1, 5, 6, 7, 10, 12
(VIS_OBJECT, VIS_LITOBJECT, VIS_SINGLEMESH, VIS_SINGLEMORPH, VIS_BILLBOARD, VIS_MORPH, VIS_LENS,
 VIS_PROJECTOR, VIS_MIRROR, VIS_EMITOR, VIS_SHADOW, VIS_LANDPATCH) = range(12)

MTL_ENVMAP = 0x00080000
MTL_ALPHAMAP = 0x40000000
MTL_ANIMATED = 0x04000000


class Stream:
    def __init__(self, data=None):
        self.reading = data is not None
        self.data = data
        self.pos = 0
        self.out = bytearray()

    def _fmt(self, d, key, fmt):
        size = struct.calcsize(fmt)
        if self.reading:
            if self.pos + size > len(self.data):
                raise EOFError('read past end at 0x%x (%s)' % (self.pos, key))
            v = struct.unpack_from('<' + fmt, self.data, self.pos)
            self.pos += size
            d[key] = v[0] if len(v) == 1 else v
        else:
            v = d[key]
            self.out += struct.pack('<' + fmt, *(v if isinstance(v, (tuple, list)) else (v,)))
        return d[key]

    def u8(self, d, k): return self._fmt(d, k, 'B')
    def u16(self, d, k): return self._fmt(d, k, 'H')
    def u32(self, d, k): return self._fmt(d, k, 'I')
    def f32(self, d, k): return self._fmt(d, k, 'f')
    def vec3(self, d, k): return self._fmt(d, k, '3f')
    def vec4(self, d, k): return self._fmt(d, k, '4f')

    def raw(self, d, k, n):
        if self.reading:
            if self.pos + n > len(self.data):
                raise EOFError('read past end at 0x%x (%s)' % (self.pos, k))
            d[k] = bytes(self.data[self.pos:self.pos + n])
            self.pos += n
        else:
            assert len(d[k]) == n, (k, len(d[k]), n)
            self.out += d[k]
        return d[k]

    def str8(self, d, k):
        if self.reading:
            n = self.data[self.pos]
            d[k] = self.data[self.pos + 1:self.pos + 1 + n].decode('latin1')
            self.pos += 1 + n
        else:
            b = d[k].encode('latin1')
            assert len(b) < 256
            self.out.append(len(b))
            self.out += b
        return d[k]

    def items(self, d, k, count, fn):
        """A list of sub-records of known count."""
        if self.reading:
            d[k] = []
            for _ in range(count):
                e = {}
                fn(self, e)
                d[k].append(e)
        else:
            assert len(d[k]) == count, (k, len(d[k]), count)
            for e in d[k]:
                fn(self, e)
        return d[k]

    def count8(self, d, k, fn):
        n = len(d[k]) if not self.reading else None
        c = {'n': n}
        self.u8(c, 'n')
        return self.items(d, k, c['n'], fn)

    def count16(self, d, k, fn):
        c = {'n': None if self.reading else len(d[k])}
        self.u16(c, 'n')
        return self.items(d, k, c['n'], fn)


# ---- materials -----------------------------------------------------------------------------

def material(s, m):
    s.u32(m, 'flags')
    s.vec4(m, 'ambient')
    s.vec4(m, 'diffuse')
    s.vec4(m, 'specular')
    s.vec4(m, 'emission')
    s.f32(m, 'power')
    s.f32(m, 'opacity')
    if m['flags'] & MTL_ENVMAP:
        s.f32(m, 'env_ratio')
        s.str8(m, 'env')
    s.str8(m, 'diffuse_map')
    if m['diffuse_map'] and m['flags'] & MTL_ALPHAMAP:
        s.str8(m, 'alpha_map')
    if m['flags'] & MTL_ANIMATED:
        s.u32(m, 'anim_frames')
        s.u16(m, 'anim_unk0')
        s.u32(m, 'anim_period')
        s.u32(m, 'anim_unk1')
        s.u32(m, 'anim_unk2')


# ---- meshes --------------------------------------------------------------------------------

def face_group(s, g):
    c = {'n': None if s.reading else len(g['faces']) // 6}
    s.u16(c, 'n')
    s.raw(g, 'faces', c['n'] * 6)
    s.u16(g, 'material')


def mesh_lod(s, l):
    s.f32(l, 'distance')
    s.u32(l, 'unk')
    c = {'n': None if s.reading else len(l['vertices']) // 32}
    s.u16(c, 'n')
    s.raw(l, 'vertices', c['n'] * 32)
    s.count8(l, 'groups', face_group)


def standard_mesh(s, f):
    s.u16(f, 'instance')
    if f['instance'] == 0:
        s.count8(f, 'lods', mesh_lod)


def frame(s, f):
    s.u8(f, 'type')
    if f['type'] == FRAME_VISUAL:
        s.u8(f, 'vtype')
        s.u16(f, 'rflags')
    s.u16(f, 'parent')
    s.vec3(f, 'pos')
    s.vec4(f, 'rot')
    s.vec3(f, 'scale')
    s.u32(f, 'unk')
    s.u8(f, 'cull')
    s.str8(f, 'name')
    s.str8(f, 'params')
    t = f['type']
    if t == FRAME_DUMMY:
        s.raw(f, 'bbox', 32)
    elif t == FRAME_JOINT:
        s.u32(f, 'bone')
    elif t == FRAME_VISUAL:
        vt = f['vtype']
        if vt == VIS_OBJECT:
            standard_mesh(s, f)
        elif vt == VIS_BILLBOARD:
            standard_mesh(s, f)
            s.u32(f, 'axis')
            s.u8(f, 'ignore_camera')
        elif vt == VIS_LENS:
            s.count8(f, 'glows', lambda s, g: (s.f32(g, 'position'), s.u16(g, 'material')))
        else:
            raise NotImplementedError('visual type %d' % vt)
    else:
        raise NotImplementedError('frame type %d' % t)


def model(s, m):
    s.raw(m, 'magic', 4)
    s.u16(m, 'version')
    s.raw(m, 'stamp', 8)
    s.count16(m, 'materials', material)
    s.count16(m, 'frames', frame)
    if s.reading:
        m['tail'] = bytes(s.data[s.pos:])
        s.pos = len(s.data)
    else:
        s.out += m['tail']


def load(path):
    data = open(path, 'rb').read()
    s = Stream(data)
    m = {}
    model(s, m)
    return m


def loads(data):
    s = Stream(data)
    m = {}
    model(s, m)
    return m


def dumps(m):
    s = Stream()
    model(s, m)
    return bytes(s.out)


def save(m, path):
    open(path, 'wb').write(dumps(m))


def roundtrip(paths):
    import collections
    errors = collections.Counter()
    ok = 0
    first = {}
    for p in paths:
        data = open(p, 'rb').read()
        try:
            m = loads(data)
            if len(m['tail']) != 1:
                raise ValueError('tail %d bytes' % len(m['tail']))
            if dumps(m) != data:
                raise ValueError('mismatch')
            ok += 1
        except Exception as e:  # noqa: BLE001 -- survey tool
            key = type(e).__name__ + ': ' + str(e).split(' at ')[0]
            errors[key] += 1
            first.setdefault(key, p)
    print('ok %d / %d' % (ok, len(paths)))
    for k, n in errors.most_common():
        print('%5d  %s   e.g. %s' % (n, k, first[k]))


if __name__ == '__main__':
    import glob
    import os
    if sys.argv[1] == '--roundtrip':
        roundtrip(sorted(glob.glob(os.path.join(sys.argv[2], '*.4ds'))))
