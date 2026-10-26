// C_Car: the checkpoints the car marks for the walkers' paths (original 0x004db060 MarkCheckpoints,
// 0x004dab60 ClearMarks, 0x004dac80 MarkAhead, 0x004dafe0 ClearAhead, 0x004dab20
// IsCheckpointMarked): those around the car get CHECKPOINT_FLAG_CAR_0200, those ahead of a moving
// car in single player CHECKPOINT_FLAG_UNDER_CAR. GetFootprint (0x004dac00) is the box the
// meleeTracker's paths go round (game/melee_tracker_route.cpp).
#include "game/car.h"

#include <cmath>
#include <cstdint>
#include <vector>

#include "game/car_internal.h"
#include "game/checkpoints.h"
#include "game/game.h"
#include "ls3d/i3d.h"

namespace {

C_CheckpointNetwork *Checkpoints()
{
    return GetGame()->GetCheckpoints();
}

// S_vector::Normalize from a square the caller summed (the last bit follows its order).
void NormalizeBy(S_vector &v, float square)
{
    if (std::fabs(square - 1.0f) < 1e-8f)
        return;
    if (square >= 1e-8f)
        v *= 1.0f / std::sqrt(square);
    else if (v.x != 0.0f)
        v.x = v.x >= 0.0f ? 1.0f : -1.0f;
    else if (v.z != 0.0f)
        v.z = v.z >= 0.0f ? 1.0f : -1.0f;
    else
        v.y = v.y >= 0.0f ? 1.0f : -1.0f;
}

}  // namespace

bool C_Car::IsCheckpointMarked(int checkpoint) const    // 0x004dab20
{
    for (size_t i = m_marks.size(); i-- > 0;) {
        if (static_cast<uint16_t>(checkpoint) == m_marks[i].checkpoint)
            return true;
    }
    return false;
}

void C_Car::GetFootprint(I3D_frame **frame, float *min, float *max) const   // 0x004dac00
{
    min[0] = m_vehicle.m_bodyMin.x - 0.5f;
    min[1] = m_vehicle.m_bodyMin.z - 0.5f;
    max[0] = m_vehicle.m_bodyMax.x + 0.5f;
    max[1] = m_vehicle.m_bodyMax.z + 0.5f;
    *frame = m_vehicle.m_body;
}

void C_Car::MarkCheckpoints()                           // 0x004db060
{
    C_CheckpointNetwork *network = Checkpoints();
    ClearMarks();
    const int nearest = network->FindNearest(GetFrame()->GetWorldPos(), 5.0f, -1.0f);
    if (nearest != CHECKPOINT_NONE) {
        // Those within 15 m of the nearest (itself among them), and the nearest again (Quirk kept:
        // it is marked twice).
        std::vector<uint16_t> points;
        network->GetNeighbours(nearest, 15.0f, &points);
        points.push_back(static_cast<uint16_t>(nearest));
        // (The original also moves each point into the body's space and does nothing with it.)
        for (size_t i = points.size(); i-- > 0;) {
            m_marks.push_back({points[i], true});
            network->Get(m_marks.back().checkpoint)->flags |= CHECKPOINT_FLAG_CAR_0200;
        }
    }
    MarkAhead();
}

void C_Car::ClearMarks()                                // 0x004dab60
{
    ClearAhead();
    C_CheckpointNetwork *network = Checkpoints();
    for (size_t i = m_marks.size(); i-- > 0;) {
        S_Checkpoint *point = network->Get(m_marks[i].checkpoint);
        if (m_marks[i].flagged) {
            point->flags &= ~CHECKPOINT_FLAG_CAR_0200;
        } else if (point->unknown1A & 0xE0) {
            point->unknown1A -= 0x20;                   // (no such mark in this exe)
        }
    }
    m_marks.clear();
}

void C_Car::MarkAhead()                                 // 0x004dac80
{
    if (!IsLocalGame())
        return;
    ClearAhead();
    if (m_vehicle.m_speed < 0.1f)
        return;
    C_CheckpointNetwork *network = Checkpoints();
    // (Without a point within 5 m: the neighbours of CHECKPOINT_NONE, none.)
    const int nearest = network->FindNearest(GetFrame()->GetWorldPos(), 5.0f, -1.0f);
    std::vector<uint16_t> points;
    network->GetNeighbours(nearest, 16.0f, &points);
    // The 10 m ahead along the velocity.
    S_vector dir(m_vehicle.m_velocity.x, m_vehicle.m_velocity.y, m_vehicle.m_velocity.z);
    NormalizeBy(dir, (dir.z * dir.z + dir.y * dir.y) + dir.x * dir.x);
    const S_vector from = GetFrame()->GetWorldPos();
    const S_vector to = dir * 10.0f + GetFrame()->GetWorldPos();
    for (size_t i = points.size(); i-- > 0;) {
        S_Checkpoint *point = network->Get(points[i]);
        float t;
        if (SegmentDistance2(point->pos, from, to, &t) < 4.0f) {
            point->flags |= CHECKPOINT_FLAG_UNDER_CAR;
            m_marksAhead.push_back(points[i]);
        }
    }
}

void C_Car::ClearAhead()                                // 0x004dafe0
{
    if (!IsLocalGame())
        return;
    C_CheckpointNetwork *network = Checkpoints();
    for (size_t i = m_marksAhead.size(); i-- > 0;)
        network->Get(m_marksAhead[i])->flags &= ~CHECKPOINT_FLAG_UNDER_CAR;
    m_marksAhead.clear();
}
