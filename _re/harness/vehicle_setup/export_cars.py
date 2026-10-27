"""Exports the shipped car models (a CENTRE and a BODY frame) for the vehicle setup harness.

    python export_cars.py MODELS_DIR OUT_DIR
    python export_cars.py MODELS_DIR OUT_DIR --damage    (the cars' "DM" damage models: every
                                                          model named DM*, into OUT_DIR)

One text file per model: "model <file> <frames>", then per frame (parents first):
  f <type> <parent: 0 = the model, else 1-based> <name>
  L <16 floats: the local matrix, scale x rotation x translation (tools/render4ds.py)>
  S <scale x y z>
  P <user properties as hex, '-' when empty>
  B <8 floats: a dummy's box>                     (dummies)
  V <count> then <count> lines "x y z"            (visuals with a mesh: the first LOD's positions)
  W <lod> <count> then <count> lines "x y z"      (the further LODs' positions)
"""
import glob
import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "..", "..", "tools"))
import hd2_4ds as h  # noqa: E402
import render4ds as r  # noqa: E402


def main():
    src, out = sys.argv[1], sys.argv[2]
    damage = "--damage" in sys.argv[3:]
    os.makedirs(out, exist_ok=True)
    count = 0
    for path in sorted(glob.glob(os.path.join(src, "*.4ds"))):
        try:
            m = h.load(path)
        except Exception:
            continue
        names = [f["name"] for f in m["frames"]]
        if damage:
            if not os.path.basename(path).upper().startswith("DM"):
                continue
        elif "CENTRE" not in names or "BODY" not in names:
            continue
        lines = ["model %s %d" % (os.path.basename(path).replace(" ", "_"), len(m["frames"]))]
        for f in m["frames"]:
            lines.append("f %d %d %s" % (f["type"], f["parent"], f["name"]))
            lm = r.local_matrix(f)
            lines.append("L " + " ".join("%.9g" % v for row in lm for v in row))
            lines.append("S %.9g %.9g %.9g" % tuple(f["scale"]))
            props = f["params"].encode("latin-1") if isinstance(f["params"], str) else f["params"]
            lines.append("P " + (props.hex() if props else "-"))
            if f["type"] == 6:
                lines.append("B " + " ".join("%.9g" % v for v in struct.unpack("<8f", f["bbox"])))
            if f["type"] == 1 and f.get("lods"):
                for lod, level in enumerate(f["lods"]):
                    data = level["vertices"]
                    n = len(data) // 32
                    lines.append("V %d" % n if lod == 0 else "W %d %d" % (lod, n))
                    for i in range(n):
                        lines.append("%.9g %.9g %.9g" % struct.unpack_from("<3f", data, i * 32))
        name = os.path.splitext(os.path.basename(path))[0].replace(" ", "_")
        open(os.path.join(out, name + ".txt"), "w", newline="\n").write("\n".join(lines) + "\n")
        count += 1
    print(count, "damage models exported" if damage else "car models exported")


main()
