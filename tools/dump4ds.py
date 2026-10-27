"""Prints a .4ds model's materials and frame tree: python tools/dump4ds.py MODEL.4ds [--mtl]"""
import sys
import os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import hd2_4ds as h

TYPES = {1: 'VIS', 5: 'SECTOR', 6: 'DUMMY', 7: 'TARGET', 10: 'JOINT', 12: 'OCCL'}
VT = {0: 'obj', 1: 'lit', 2: 'skin', 3: 'smorph', 4: 'bboard', 5: 'morph', 6: 'lens', 7: 'proj', 8: 'mirror'}

m = h.load(sys.argv[1])
if '--mtl' in sys.argv:
    for i, t in enumerate(m['materials']):
        print('mtl %2d %08x %s %s' % (i + 1, t['flags'], t['diffuse_map'], t.get('alpha_map', '')))
kids = {}
for i, f in enumerate(m['frames']):
    kids.setdefault(f['parent'], []).append(i)


def info(f):
    t = TYPES.get(f['type'], f['type'])
    if f['type'] == 1:
        t += '/' + VT.get(f['vtype'], str(f['vtype'])) + ' rf=%04x' % f['rflags']
        if f.get('instance'):
            t += ' inst=%d' % f['instance']
        elif 'lods' in f:
            t += ' lods=' + ','.join('%dv/%s' % (len(l['vertices']) // 32, '+'.join(str(g['material']) for g in l['groups'])) for l in f['lods'])
    return t


def walk(p, depth):
    for i in kids.get(p, []):
        f = m['frames'][i]
        print('%3d %s%-22s %-28s pos=(%.3f %.3f %.3f) rot=(%.3f %.3f %.3f %.3f) scl=(%.2f %.2f %.2f) cull=%02x unk=%x %r' % (
            i + 1, '  ' * depth, f['name'], info(f), *f['pos'], *f['rot'], *f['scale'], f['cull'], f['unk'], f['params']))
        walk(i + 1, depth + 1)


walk(0, 0)
print('tail', m['tail'].hex())
