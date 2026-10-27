"""Mutations of src/game/vehicle_collide.cpp for ../mutfast.py: each should make the harness fail."""
import os

ROOT = r"G:\Programming\HD2 Source Port\C++"
SRC = os.path.join(ROOT, r"src\game\vehicle_collide.cpp")

MUTATIONS = [
    ("approach sign", "if (!(DotXZY(velocity, normal) < 0.0f))", "if (!(DotXZY(velocity, normal) > 0.0f))"),
    ("bounce sign", "* -(m_unknown228C ? m_unknown6F8 : ground->bounce)", "* (m_unknown228C ? m_unknown6F8 : ground->bounce)"),
    ("impulse turn sign", "        m_unknown210 -= turnFor(arm.Cross(impulse));\n        const float size",
     "        m_unknown210 += turnFor(arm.Cross(impulse));\n        const float size"),
    ("contact velocity", "XYZ(m_velocity) + arm.Cross(m_unknown210)", "XYZ(m_velocity) - arm.Cross(m_unknown210)"),
    ("move reversed", "        SetXYZ(point->move, XYZ(point->move) - XYZ(point->start));",
     "        SetXYZ(point->move, XYZ(point->start) - XYZ(point->move));"),
    ("box world pose", "touched = g_collisionWorld.TestBoxSweep(local, m_previousModelMatrix, world,",
     "touched = g_collisionWorld.TestBoxSweep(local, m_previousModelMatrix, m_previousModelMatrix,"),
    ("no push", "        out = normal * depth;", "        out = normal * 0.0f;"),
    ("friction turn sign", "m_unknown210 += turnFor(force.Cross(arm));", "m_unknown210 -= turnFor(force.Cross(arm));"),
    ("friction along", "S_vector along = slide.Cross(normal).Cross(normal);", "S_vector along = normal.Cross(slide.Cross(normal));"),
    ("off parts", "if (box->parent && !box->parent->IsOn())", "if (box->parent && box->parent->IsOn())"),
    ("no ignore clear", "        g_collisionWorld.SetIgnoredVolume(nullptr, 0);\n        if (!touched)",
     "        if (!touched)"),
    ("resistance mass", "float resistance = DotXZY(lever, normal) + 1.0f / m_currentMass;",
     "float resistance = DotXZY(lever, normal) + 0.1f / m_currentMass;"),
]

# (name, pattern, replacement): in SRC.
MUTATIONS = [m if len(m) == 4 else (SRC,) + m for m in MUTATIONS]

if __name__ == "__main__":
    # Each pattern must occur once in its file.
    for path, name, a, b in MUTATIONS:
        n = open(path, encoding="utf-8", newline="").read().count(a)
        if n != 1:
            print(f"{name}: pattern found {n} times")
    print(len(MUTATIONS), "mutations")
