// The test levels' collision files (shared by the collision and physics harnesses; included inside their anonymous
// namespace, after collision.h / collision_internal.h): S_Synth and WriteSynth.
// A level of my own in the file format: 64 x 16 x 64 m in 8 m cells (two layers), triangles on the file's own
// vertex table, a water rectangle. The triangles are listed in every cell their box overlaps, sorted by their
// smallest x (v0; v2 has the largest), as the game's files are.
struct S_SynthTri {
    int frame = 0x7FFF;                                 // 0x7FFF: the file's vertex table; else a frame record
    int index[3] = {0, 0, 0};                           // vertex indices in that frame's vertices
    S_vector v[3];
    S_vector normal;                                    // into the surface (as stored)
    uint32_t flags;
    int kind;                                           // the kind it is made to be (0: any)
};
struct S_Synth {
    std::vector<std::pair<uint32_t, std::string>> records = {{0u, "nonexist"}};   // frame records: flags, name
    std::vector<S_SynthTri> tris;
    S_matrix rectMatrix;
    S_vector rectMin, rectMax;
    bool withRect = true;
    uint32_t rectFlags = COLLISION_PRIM_RECT | COLLISION_FLAG_WATER;
    // The grid (x and z may differ), the dynamic grid's cell size.
    int cellsX = 8, cellsZ = 8;
    float originX = 0.0f, originZ = 0.0f;                // the level's corner (its minimum x and z)
    float cellX = 8.0f, cellZ = 8.0f, volumeCell = 16.0f;
    // More primitives: listed in every cell (their heights widen the cells' ranges).
    std::vector<S_CollisionCapsule> capsules;
    std::vector<S_CollisionConvexPrim> convexes;        // (frame: the index of a frame record)
    std::vector<std::pair<float, float>> primHeights;   // their y ranges
};
constexpr float SYN_CELL = 8.0f;
constexpr int SYN_CELLS = 8, SYN_LAYERS = 2;

S_vector Cross(const S_vector &a, const S_vector &b) { return S_vector(a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x); }
// Orders the vertices: v0 the smallest x, v2 the largest (v1 between), unless `keep` (made with that order).
void OrderByX(S_SynthTri &t)
{
    std::sort(t.v, t.v + 3, [](const S_vector &a, const S_vector &b) { return a.x < b.x; });
}
bool g_synthQuiet;                                      // (part 2's levels: the made kinds are not checked)
S_SynthTri Tri(S_vector a, S_vector b, S_vector c, uint32_t flags, bool flip, int kind = 0)
{
    S_SynthTri t;
    t.v[0] = a;
    t.v[1] = b;
    t.v[2] = c;
    if (!kind)
        OrderByX(t);
    t.normal = Cross(t.v[1] - t.v[0], t.v[2] - t.v[0]).Normalized();
    if (flip)
        t.normal = -t.normal;
    // The kind the files store in the flags' low byte (the plane the ray test solves in: the normal's largest axis;
    // the v0-v1 edge along an axis where it is).
    const float nx = std::fabs(t.normal.x), ny = std::fabs(t.normal.y), nz = std::fabs(t.normal.z);
    const S_vector e = t.v[1] - t.v[0];
    uint32_t stored;
    if (ny >= nx && ny >= nz)
        stored = e.x == 0.0f ? TRIANGLE_KIND_XZ_EDGE_Z : e.z == 0.0f ? TRIANGLE_KIND_XZ_EDGE_X : TRIANGLE_KIND_XZ;
    else if (nx >= nz)
        stored = e.y == 0.0f ? TRIANGLE_KIND_YZ_EDGE_Z : TRIANGLE_KIND_YZ;
    else
        stored = e.x == 0.0f ? TRIANGLE_KIND_XY_EDGE_Y : TRIANGLE_KIND_XY;
    t.flags = flags | stored;
    // The sphere sweeps take a triangle's corners as wound against its stored normal (seen from its front, the way
    // a sweep into it goes: (v1 - v0) x (v2 - v0) points out of the surface); one wound with it is marked reversed.
    // (The levels' files mark none: their stored normals follow the corners' order.)
    if (!flip)
        t.flags |= COLLISION_TRIANGLE_REVERSED;
    t.kind = kind;
    if (kind && static_cast<int>(stored) != kind && !g_synthQuiet)
        std::printf("  (made kind %d, computed %u)\n", kind, stored);
    return t;
}
[[maybe_unused]] S_Synth MakeSynth(std::mt19937 &rng, int count)
{
    S_Synth s;
    std::uniform_real_distribution<float> pos(1.0f, 63.0f), py(0.5f, 15.5f), size(0.3f, 7.0f), unit(-1.0f, 1.0f);
    for (int i = 0; i < count; ++i) {
        const S_vector c(pos(rng), py(rng), pos(rng));
        const float r = size(rng);
        const S_vector a = c + S_vector(unit(rng), unit(rng), unit(rng)) * r;
        const S_vector b = c + S_vector(unit(rng), unit(rng), unit(rng)) * r;
        const S_vector d = c + S_vector(unit(rng), unit(rng), unit(rng)) * r;
        if (Cross(b - a, d - a).Magnitude() < 0.05f)
            continue;
        uint32_t flags = 0;
        if (i % 5 == 0)
            flags |= COLLISION_TRIANGLE_TWO_SIDED;
        if (i % 11 == 0)
            flags |= COLLISION_FLAG_WATER;
        S_SynthTri t = Tri(a, b, d, flags, rng() & 1);
        if (t.v[0].x < 0.0f || t.v[2].x > 64.0f || std::min({t.v[0].z, t.v[1].z, t.v[2].z}) < 0.0f || std::max({t.v[0].z, t.v[1].z, t.v[2].z}) > 64.0f ||
            std::min({t.v[0].y, t.v[1].y, t.v[2].y}) < 0.0f || std::max({t.v[0].y, t.v[1].y, t.v[2].y}) > 16.0f)
            continue;
        s.tris.push_back(t);
    }
    // The axis-aligned kinds (the edge v0-v1 along an axis), each a few times.
    for (int i = 0; i < 6; ++i) {
        const S_vector c(pos(rng), py(rng), pos(rng));
        const float r = 1.0f + i * 0.5f;
        // Facing y, v0-v1 along x (v0 smallest x, v2 largest).
        s.tris.push_back(Tri(c, c + S_vector(r, 0, 0), c + S_vector(2 * r, 0, r), 0, i & 1, TRIANGLE_KIND_XZ_EDGE_X));
        // Facing y, v0-v1 along z.
        s.tris.push_back(Tri(c, c + S_vector(0, 0, r), c + S_vector(r, 0, r * 0.5f), 0, i & 1, TRIANGLE_KIND_XZ_EDGE_Z));
        // Facing y, general.
        s.tris.push_back(Tri(c, c + S_vector(r, 0, r * 0.7f), c + S_vector(2 * r, 0, -r * 0.4f), 0, i & 1, TRIANGLE_KIND_XZ));
        // Facing x, v0-v1 along z (same height).
        s.tris.push_back(Tri(c, c + S_vector(0, 0, r), c + S_vector(0, r, r * 0.5f), 0, i & 1, TRIANGLE_KIND_YZ_EDGE_Z));
        // Facing x, general.
        s.tris.push_back(Tri(c, c + S_vector(0, r * 0.6f, r), c + S_vector(0, r, -r * 0.3f), 0, i & 1, TRIANGLE_KIND_YZ));
        // Facing z, v0-v1 along y.
        s.tris.push_back(Tri(c, c + S_vector(0, r, 0), c + S_vector(r, r * 0.5f, 0), 0, i & 1, TRIANGLE_KIND_XY_EDGE_Y));
        // Facing z, general.
        s.tris.push_back(Tri(c, c + S_vector(r, r * 0.4f, 0), c + S_vector(2 * r, -r * 0.6f, 0), 0, i & 1, TRIANGLE_KIND_XY));
    }
    // The water rectangle: 20 x 12 m at y 2, turned 30 degrees about y.
    const float a = 0.5235988f;
    S_matrix &m = s.rectMatrix;
    std::memset(&m, 0, sizeof m);
    m.m[0][0] = std::cos(a);
    m.m[0][2] = -std::sin(a);
    m.m[1][1] = 1.0f;
    m.m[2][0] = std::sin(a);
    m.m[2][2] = std::cos(a);
    m.m[3][0] = 30.0f;
    m.m[3][1] = 2.0f;
    m.m[3][2] = 30.0f;
    m.m[3][3] = 1.0f;
    s.rectMin = S_vector(-10.0f, 0.0f, -6.0f);
    s.rectMax = S_vector(10.0f, 0.0f, 6.0f);
    return s;
}
S_matrix InverseRigid(const S_matrix &m)
{
    S_matrix r;
    std::memset(&r, 0, sizeof r);
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j)
            r.m[i][j] = m.m[j][i];
    for (int j = 0; j < 3; ++j)
        r.m[3][j] = -(m.m[3][0] * r.m[0][j] + m.m[3][1] * r.m[1][j] + m.m[3][2] * r.m[2][j]);
    r.m[3][3] = 1.0f;
    return r;
}
// The file: header, the mesh block (its header, the grid lines, the triangles, the rectangle, the vertices and a
// spare one, the cells in z, y, x order).
std::string WriteSynth(const S_Synth &s, const std::string &path)
{
    std::string out;
    const auto put = [&out](const void *p, size_t n) { out.append(static_cast<const char *>(p), n); };
    const auto u32 = [&](uint32_t v) { put(&v, 4); };
    const auto f32 = [&](float v) { put(&v, 4); };
    // The frame records (the rectangle's frame reference is 0).
    int recordsSize = 0;
    for (const auto &r : s.records)
        recordsSize += 4 + static_cast<int>(r.second.size()) + 1;
    const int recordBase = 24 + 4 * static_cast<int>(s.records.size());
    u32(0x43666947);
    u32(0x10005);
    u32(static_cast<uint32_t>(recordBase + recordsSize));
    u32(static_cast<uint32_t>(s.records.size()));
    u32(0x21F);
    u32(0);
    int at = recordBase;
    for (const auto &r : s.records) {
        u32(static_cast<uint32_t>(at));
        at += 4 + static_cast<int>(r.second.size()) + 1;
    }
    for (const auto &r : s.records) {
        u32(r.first);
        put(r.second.c_str(), r.second.size() + 1);
    }
    S_CollisionMesh head;
    std::memset(&head, 0, sizeof head);
    head.minX = s.originX;
    head.minZ = s.originZ;
    head.maxX = s.originX + s.cellX * s.cellsX;
    head.maxZ = s.originZ + s.cellZ * s.cellsZ;
    head.cellSizeX = s.cellX;
    head.cellSizeZ = s.cellZ;
    head.cellsX = s.cellsX;
    head.cellsZ = s.cellsZ;
    head.volumeCellSize = s.volumeCell;
    head.minY = 0.0f;
    head.cellSizeY = SYN_CELL;
    head.cellsY = SYN_LAYERS;
    head.numTriangles = static_cast<int>(s.tris.size());
    head.numRects = s.withRect ? 1 : 0;
    head.numCapsules = static_cast<int>(s.capsules.size());
    head.numConvexes = static_cast<int>(s.convexes.size());
    head.numVertices = static_cast<int>(s.tris.size() * 3);
    put(&head, sizeof head);
    for (int i = 0; i <= s.cellsX; ++i)
        f32(s.originX + i * s.cellX);
    for (int i = 0; i <= SYN_LAYERS; ++i)
        f32(i * SYN_CELL);
    for (int i = 0; i <= s.cellsZ; ++i)
        f32(s.originZ + i * s.cellZ);
    for (int i = 0; i < 4; ++i)
        f32(0.0f);
    for (size_t i = 0; i < s.tris.size(); ++i) {
        const S_SynthTri &t = s.tris[i];
        u32(t.flags);
        for (int k = 0; k < 3; ++k)
            u32(t.frame == 0x7FFF ? (0x7FFFu << 16) | static_cast<uint32_t>(i * 3 + k)
                                  : (static_cast<uint32_t>(t.frame) << 16) | static_cast<uint32_t>(t.index[k]));
        f32(t.normal.x);
        f32(t.normal.y);
        f32(t.normal.z);
        f32(-t.normal.Dot(t.v[0]));
    }
    if (s.withRect) {
        S_CollisionRect r;
        std::memset(&r, 0, sizeof r);
        r.flags = s.rectFlags;
        r.min = s.rectMin;
        r.max = s.rectMax;
        r.matrix = s.rectMatrix;
        r.invMatrix = InverseRigid(s.rectMatrix);
        put(&r, sizeof r);
    }
    for (const S_CollisionConvexPrim &c : s.convexes)
        put(&c, sizeof c);
    for (const S_CollisionCapsule &c : s.capsules)
        put(&c, sizeof c);
    for (const S_SynthTri &t : s.tris)
        for (const S_vector &v : t.v) {
            f32(v.x);
            f32(v.y);
            f32(v.z);
            f32(1.0f);
        }
    for (int i = 0; i < 4; ++i)
        f32(0.0f);
    for (int z = 0; z < s.cellsZ; ++z)
        for (int y = 0; y < SYN_LAYERS; ++y)
            for (int x = 0; x < s.cellsX; ++x) {
                const float x0 = s.originX + x * s.cellX, z0 = s.originZ + z * s.cellZ, y0 = y * SYN_CELL;
                std::vector<int> in;
                float minY = 1e9f, maxY = -1e9f;
                for (size_t i = 0; i < s.tris.size(); ++i) {
                    const S_SynthTri &t = s.tris[i];
                    S_vector lo = t.v[0], hi = t.v[0];
                    for (const S_vector &v : t.v) {
                        lo = S_vector(std::min(lo.x, v.x), std::min(lo.y, v.y), std::min(lo.z, v.z));
                        hi = S_vector(std::max(hi.x, v.x), std::max(hi.y, v.y), std::max(hi.z, v.z));
                    }
                    if (hi.x < x0 || lo.x > x0 + s.cellX || hi.z < z0 || lo.z > z0 + s.cellZ || hi.y < y0 || lo.y > y0 + SYN_CELL)
                        continue;
                    in.push_back(static_cast<int>(i));
                    minY = std::min(minY, lo.y);
                    maxY = std::max(maxY, hi.y);
                }
                std::sort(in.begin(), in.end(), [&](int a, int b) { return s.tris[a].v[0].x < s.tris[b].v[0].x; });
                // The rectangle in the lowest layer's cells under it (all of them: its box is big).
                const bool rect = s.withRect && y == 0 && x0 < 45.0f && x0 + s.cellX > 15.0f && z0 < 45.0f && z0 + s.cellZ > 15.0f;
                if (rect) {
                    minY = std::min(minY, 2.0f - 12.0f);
                    maxY = std::max(maxY, 2.0f + 12.0f);
                }
                const size_t extra = s.capsules.size() + s.convexes.size();
                for (const auto &h : s.primHeights) {
                        minY = std::min(minY, h.first);
                        maxY = std::max(maxY, h.second);
                    }
                u32(static_cast<uint32_t>(in.size() + rect + extra));
                u32(0);
                f32(maxY);
                f32(minY);
                // (Primitives first, then the triangles: the game's files list them so, and the cell tests stop at
                // the first triangle past the segment.)
                if (rect)
                    u32(static_cast<uint32_t>(COLLISION_PRIM_RECT) << 24);
                {
                    for (size_t k = 0; k < s.convexes.size(); ++k)
                        u32((static_cast<uint32_t>(COLLISION_PRIM_CONVEX) << 24) | static_cast<uint32_t>(k * sizeof(S_CollisionConvexPrim)));
                    for (size_t k = 0; k < s.capsules.size(); ++k)
                        u32((static_cast<uint32_t>(COLLISION_PRIM_CAPSULE) << 24) | static_cast<uint32_t>(k * sizeof(S_CollisionCapsule)));
                }
                for (int i : in)
                    u32(static_cast<uint32_t>(i) * 0x20);
            }
    FILE *f = std::fopen(path.c_str(), "wb");
    std::fwrite(out.data(), 1, out.size(), f);
    std::fclose(f);
    return out;
}
// Two triangles of the quad a b c d (the stored normal into the surface: against `outward`).
void AddQuad(S_Synth &s, S_vector a, S_vector b, S_vector c, S_vector d, const S_vector &outward)
{
    const S_vector corners[2][3] = {{a, b, c}, {a, c, d}};
    for (const auto &q : corners) {
        S_SynthTri t = Tri(q[0], q[1], q[2], 0, false);
        if (t.normal.Dot(outward) > 0.0f)
            t = Tri(q[0], q[1], q[2], 0, true);
        s.tris.push_back(t);
    }
}
// A block lo..hi: its top and sides.
void AddBlock(S_Synth &s, const S_vector &lo, const S_vector &hi)
{
    AddQuad(s, S_vector(lo.x, hi.y, lo.z), S_vector(hi.x, hi.y, lo.z), S_vector(hi.x, hi.y, hi.z), S_vector(lo.x, hi.y, hi.z), S_vector(0, 1, 0));
    AddQuad(s, S_vector(lo.x, lo.y, lo.z), S_vector(lo.x, hi.y, lo.z), S_vector(lo.x, hi.y, hi.z), S_vector(lo.x, lo.y, hi.z), S_vector(-1, 0, 0));
    AddQuad(s, S_vector(hi.x, lo.y, lo.z), S_vector(hi.x, hi.y, lo.z), S_vector(hi.x, hi.y, hi.z), S_vector(hi.x, lo.y, hi.z), S_vector(1, 0, 0));
    AddQuad(s, S_vector(lo.x, lo.y, lo.z), S_vector(hi.x, lo.y, lo.z), S_vector(hi.x, hi.y, lo.z), S_vector(lo.x, hi.y, lo.z), S_vector(0, 0, -1));
    AddQuad(s, S_vector(lo.x, lo.y, hi.z), S_vector(hi.x, lo.y, hi.z), S_vector(hi.x, hi.y, hi.z), S_vector(lo.x, hi.y, hi.z), S_vector(0, 0, 1));
}
// A ramp along +x from (x0, 1, z0), `length` long and `width` wide, rising by `height`.
void AddRamp(S_Synth &s, float x0, float z0, float length, float width, float height)
{
    AddQuad(s, S_vector(x0, 1, z0), S_vector(x0 + length, 1 + height, z0), S_vector(x0 + length, 1 + height, z0 + width), S_vector(x0, 1, z0 + width),
            S_vector(-height, length, 0).Normalized());
}
