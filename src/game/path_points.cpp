// The points of an order's path on the command map (part 11b; 0x007ce930): the walking network's points that the straight line
// misses, then a smooth curve through them, ten points to each stretch.
#include <cmath>
#include <vector>

#include "game/checkpoints.h"
#include "game/game.h"
#include "game/pending.h"

namespace {

constexpr int CURVE_STEPS = 10;
constexpr float STEP = 0.1f;
constexpr float STEP2 = 0.01f;
constexpr float STEP3 = 0.001f;
constexpr float RADIUS_UNIT = 0.0625f;                  // S_Checkpoint::radius: 1/16 m

// (0x00581f20.) (DS 0x00587bd0)
void AddPoint(S_Vector4Array &points, const S_vector4 &point)
{
    if (points.count >= points.capacity) {
        points.capacity += points.growBy;
        auto *grown = static_cast<S_vector4 *>(_aligned_malloc(static_cast<size_t>(points.capacity) * sizeof(S_vector4), 16));
        for (int i = 0; i < points.count; ++i)
            grown[i] = points.data[i];
        _aligned_free(points.data);
        points.data = grown;
    }
    points.data[points.count++] = point;
}

S_vector XYZ(const S_vector4 &v) { return S_vector(v.x, v.y, v.z); }
S_vector4 WithW(const S_vector &v, float w)
{
    S_vector4 out;
    out.x = v.x;
    out.y = v.y;
    out.z = v.z;
    out.w = w;
    return out;
}

// What the path must go through: the start, the points it would otherwise cut past (each at its radius from the network's
// point, on the side the line runs), the end. (The w of the points between: stale locals in the original; 0 here.)
std::vector<S_vector4> PointsToPass(C_CheckpointNetwork *network, const S_vector4 &from, const S_vector4 &to, const std::vector<uint16_t> &path)
{
    std::vector<S_vector4> out;
    out.push_back(from);
    for (size_t i = 0; i < path.size(); ++i) {
        const S_Checkpoint *point = network->Get(path[i]);
        const S_vector center = point->pos;
        const float radius = static_cast<float>(point->radius) * RADIUS_UNIT;
        const float radius2 = radius * radius;
        const S_vector4 &before = out.back();
        const S_vector after = i + 1 == path.size() ? XYZ(to) : S_vector(network->Get(path[i + 1])->pos);
        const float prevX = before.x - center.x, prevZ = before.z - center.z;
        const float nextX = after.x - center.x, nextZ = after.z - center.z;
        const float lineX = nextX - prevX, lineZ = nextZ - prevZ;
        // Its point must lie between the two, each outside the radius.
        if (!std::signbit(lineX * prevX + lineZ * prevZ) || prevZ * prevZ + prevX * prevX < radius2 ||
            std::signbit(lineX * nextX + lineZ * nextZ) || nextZ * nextZ + nextX * nextX < radius2)
            continue;
        const S_vector side = S_vector(lineZ, 0.0f, -lineX).NormalizedGame();
        const float distance = prevX * side.x + prevZ * side.z;
        if (std::fabs(distance) < radius)
            continue;
        const float offset = std::signbit(distance) ? -radius : radius;
        out.push_back(WithW(S_vector(center.x + side.x * offset, center.y + side.y * offset, center.z + side.z * offset), 0.0f));
    }
    out.push_back(to);
    return out;
}

}  // namespace

void BuildPathPoints(const S_vector4 &from, const S_vector4 &to, const std::vector<uint16_t> &path, S_Vector4Array &points)
{
    _aligned_free(points.data);
    points.data = nullptr;
    points.count = 0;
    points.capacity = 0;
    C_CheckpointNetwork *network = GetGame()->GetCheckpoints();
    const std::vector<S_vector4> pass = PointsToPass(network, from, to, path);
    const int count = static_cast<int>(pass.size());

    std::vector<S_vector> tangents;
    for (int i = 0; i < count; ++i) {
        S_vector t;
        if (i == 0) {
            t = (XYZ(pass[1]) - XYZ(pass[0])) * 0.5f;
        } else if (i == count - 1) {
            t = (XYZ(pass[i]) - XYZ(pass[i - 1])) * 0.5f;
        } else {
            const S_vector in = XYZ(pass[i]) - XYZ(pass[i - 1]);
            const S_vector out = XYZ(pass[i + 1]) - XYZ(pass[i]);
            t = (in + out) * 0.5f;
        }
        tangents.push_back(t);
    }

    // A cubic through each stretch, by forward differences.
    for (int i = 0; i + 1 < count; ++i) {
        const S_vector p0 = XYZ(pass[i]), p1 = XYZ(pass[i + 1]);
        const S_vector &m0 = tangents[static_cast<size_t>(i)], &m1 = tangents[static_cast<size_t>(i + 1)];
        const S_vector a = (p0 + p0) - (p1 + p1) + m0 + m1;
        const S_vector b = p0 * -3.0f + p1 * 3.0f - m0 * 2.0f - m1;
        S_vector d1 = a * STEP3 + b * STEP2 + m0 * STEP;
        S_vector d2 = a * 6.0f * STEP3 + b * 2.0f * STEP2;
        const S_vector d3 = a * 6.0f * STEP3;
        S_vector point = p0;
        for (int step = 0; step < CURVE_STEPS; ++step) {
            AddPoint(points, WithW(point, pass[static_cast<size_t>(i)].w));
            point += d1;
            d1 += d2;
            d2 += d3;
        }
    }
    AddPoint(points, to);
}
