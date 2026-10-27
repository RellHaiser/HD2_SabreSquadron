"""Reads / rewrites the object-name table of Hidden & Dangerous 2 .5ds animations (version 0x7A).

Layout: "5DS\\0", u16 version, 8-byte stamp, u32 data size (file size - 18); the data block starts
at 18: u16 object count, u16 frame count, then per object (u32 name offset, u32 key data offset),
both relative to the data block. The key data comes first, the NUL-terminated object names last.
An animation binds to a model's frames by these names, so moving an animation to differently
named frames only needs the name table rewritten.

An object's key data: u32 flags, then per track present (in this order) u16 key count, u16 frame
numbers, padding, the values (offsets relative to the data block):
    flag 4  rotation keys: quaternions (x, y, z, w), 16 bytes, aligned to 16
    flag 2  position keys: vectors, 12 bytes, aligned to 4 (local to the frame's parent)
    flag 8  scale keys: vectors, 12 bytes, aligned to 4
    flag 0x20  not decoded (only on root objects "a" in the shipped files)
(checked on the 42,879 objects of the shipped files without flag 0x20: each ends where the
next begins).

    python tools/hd2_5ds.py FILE.5ds                     # list objects and their tracks
"""
import struct
import sys

DATA = 18


def load(path):
    return parse(open(path, 'rb').read())


def parse(d):
    assert d[:4] == b'5DS\x00', 'not a 5ds file'
    version, = struct.unpack_from('<H', d, 4)
    size, = struct.unpack_from('<I', d, 14)
    assert size == len(d) - DATA, 'size field %d, file %d' % (size, len(d))
    count, frames = struct.unpack_from('<HH', d, DATA)
    objs = []
    for i in range(count):
        noff, doff = struct.unpack_from('<II', d, DATA + 4 + i * 8)
        e = d.index(b'\x00', DATA + noff)
        objs.append({'name': d[DATA + noff:e].decode('latin1'), 'name_off': noff, 'data_off': doff})
    names_start = min(o['name_off'] for o in objs)
    assert all(o['data_off'] < names_start for o in objs), 'key data after the names'
    return {'raw': d, 'version': version, 'frames': frames, 'objects': objs, 'names_start': names_start}


TRACKS = ((4, 'rot', 16, 16), (2, 'pos', 12, 4), (8, 'scale', 12, 4))


def _align(p, a):
    return (p + a - 1) // a * a


def tracks(anim, obj):
    """{'rot'|'pos'|'scale': (frames, value offset in the file)} of object `obj` (a dict of
    anim['objects'])."""
    d = anim['raw']
    flags, = struct.unpack_from('<I', d, DATA + obj['data_off'])
    if flags & ~0x0E:
        raise NotImplementedError('track flags 0x%x' % flags)
    p = obj['data_off'] + 4
    out = {}
    for bit, name, size, al in TRACKS:
        if flags & bit:
            n, = struct.unpack_from('<H', d, DATA + p)
            frames = struct.unpack_from('<%dH' % n, d, DATA + p + 2)
            p = _align(p + 2 + 2 * n, al)
            out[name] = (frames, DATA + p)
            p += size * n
    return out


def positions(anim, name):
    obj = next(o for o in anim['objects'] if o['name'] == name)
    frames, at = tracks(anim, obj)['pos']
    return [struct.unpack_from('<3f', anim['raw'], at + 12 * k) for k in range(len(frames))]


def shift_positions(anim, name, delta):
    """Returns the file bytes with every position key of object `name` moved by delta (x, y, z)."""
    obj = next(o for o in anim['objects'] if o['name'] == name)
    frames, at = tracks(anim, obj)['pos']
    d = bytearray(anim['raw'])
    for k in range(len(frames)):
        v = struct.unpack_from('<3f', d, at + 12 * k)
        struct.pack_into('<3f', d, at + 12 * k, *(v[i] + delta[i] for i in range(3)))
    return bytes(d)


def scale_motion(anim, name, scale):
    """Returns the file bytes with object `name`'s position keys pulled towards its first key:
    first + (key - first) * scale (0: the frame stays put, 1: unchanged)."""
    obj = next(o for o in anim['objects'] if o['name'] == name)
    frames, at = tracks(anim, obj)['pos']
    d = bytearray(anim['raw'])
    first = struct.unpack_from('<3f', d, at)
    for k in range(len(frames)):
        v = struct.unpack_from('<3f', d, at + 12 * k)
        struct.pack_into('<3f', d, at + 12 * k, *(first[i] + (v[i] - first[i]) * scale for i in range(3)))
    return bytes(d)


def rename(anim, mapping):
    """Returns the file bytes with object names replaced via mapping {old: new}."""
    d = anim['raw']
    head = bytearray(d[:DATA + anim['names_start']])
    names = b''
    for i, o in enumerate(anim['objects']):
        n = mapping.get(o['name'], o['name']).encode('latin1')
        struct.pack_into('<I', head, DATA + 4 + i * 8, anim['names_start'] + len(names))
        names += n + b'\x00'
    out = bytes(head) + names
    out = out[:14] + struct.pack('<I', len(out) - DATA) + out[18:]
    return out


if __name__ == '__main__':
    a = load(sys.argv[1])
    print('version 0x%x, %d frames' % (a['version'], a['frames']))
    for o in a['objects']:
        try:
            t = tracks(a, o)
            desc = ', '.join('%s %d keys' % (k, len(v[0])) for k, v in t.items())
        except NotImplementedError as e:
            desc = str(e)
        print('  %-20s data at 0x%x  %s' % (o['name'], o['data_off'], desc))
