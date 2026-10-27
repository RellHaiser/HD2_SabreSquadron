"""Mutations of src/game/vehicle_skid_marks.cpp for ../mutfast.py: each should make the harness fail."""
import os

ROOT = r"G:\Programming\HD2 Source Port\C++"
K = os.path.join(ROOT, r"src\game\vehicle_skid_marks.cpp")

MUTATIONS = [
    (K, "lift", "normal.y * 0.05f", "normal.y * 0.06f"),
    (K, "min stretch", "if (length < 0.05f)", "if (length < 0.15f)"),
    (K, "max stretch", "if (length > 5.0f) {", "if (length > 7.0f) {"),
    (K, "direction ignored", "+ velocity.x * w->skidDir.x < 0.0f) {", "+ velocity.x * w->skidDir.x < -100.0f) {"),
    (K, "segment longer", "if (length <= 0.3f) {", "if (length <= 0.4f) {"),
    (K, "step short", "const S_vector step(dir.x * 0.3f, dir.y * 0.3f, dir.z * 0.3f);",
     "const S_vector step(dir.x * 0.25f, dir.y * 0.25f, dir.z * 0.25f);"),
    (K, "segment scale", "mark.frame0->SetScale(S_vector(w->width, 1.0f, 0.3f));",
     "mark.frame0->SetScale(S_vector(w->width, 1.0f, 0.29f));"),
    (K, "pivot swapped", "if (!((acrossDir.x * dir.x + dir.y * acrossDir.y) + dir.z * acrossDir.z < 0.0f)) {",
     "if (((acrossDir.x * dir.x + dir.y * acrossDir.y) + dir.z * acrossDir.z < 0.0f)) {"),
    (K, "start offset sign", "start = S_vector(w->skidLeft.x - side.x * half,", "start = S_vector(w->skidLeft.x + side.x * half,"),
    (K, "corner half width", "SetScale(S_vector(gapHalf + gapHalf, 1.0f, cornerLength));",
     "SetScale(S_vector(gapHalf, 1.0f, cornerLength));"),
    (K, "corner length", "std::sqrt(acrossLength * acrossLength - gapHalf * gapHalf)", "acrossLength"),
    (K, "corner aim", "cornerDir = S_vector(-bisector.x, -bisector.y, -bisector.z);", "cornerDir = bisector;"),
    (K, "fresh ignored", "        if (!mark.fresh) {", "        if (true) {"),
    (K, "ring not wrapped", "const uint32_t next = current + 1 < count ? current + 1 : 0;",
     "const uint32_t next = current + 1 < count ? current + 1 : count - 1;"),
    (K, "water ignored", "(w->ground->flags & COLLISION_FLAG_WATER) ||", ""),
    (K, "following not active", "        following.active = true;\n", ""),
    (K, "new mark not fresh", "            mark.fresh = true;\n", ""),
    (K, "edges not moved", "w->skidLeft = S_vector(start.x + left.x, start.y + left.y, start.z + left.z);",
     "w->skidLeft = left;"),
    (K, "no surface check", "!g_carSurfaces[g_carSurfaceOfMaterial[w->surface]].skidMarks ||", ""),
]

if __name__ == "__main__":
    # Each pattern must occur once in its file.
    for path, name, a, b in MUTATIONS:
        n = open(path, encoding="utf-8", newline="").read().count(a)
        if n != 1:
            print(f"{name}: pattern found {n} times")
    print(len(MUTATIONS), "mutations")
