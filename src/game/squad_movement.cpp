// C_SquadController's walking (game/human_controller.h; original source 0x0052d460..0x0052e5b0,
// 0x00530ff0, 0x00532190, 0x005324d0): the pace, stepping out of the way of the leader and
// of grenades, staying on the walking network, and the map's points to attack a target from.
#include "game/human_controller.h"

#include <cstdint>
#include <cstdlib>
#include <vector>

#include "common/c_str.h"
#include "game/actor.h"
#include "game/ai_parts.h"
#include "game/checkpoints.h"
#include "game/controller_internal.h"
#include "game/game.h"
#include "game/human.h"
#include "game/squad_internal.h"
#include "game/team.h"
#include "ls3d/i3d.h"

namespace {

// The distance of the point at `offset` from a line through the origin along `dir` (-1e16 for
// a (nearly) zero `dir`).
float LineDistance(const S_vector &offset, const S_vector &dir)
{
    const float length2 = dir.Square();
    if (length2 < 1e-8f)
        return -1e16f;
    const float t = (offset.z * dir.z + offset.y * dir.y + offset.x * dir.x) / length2;
    return (offset - dir * t).Magnitude();
}

}  // namespace

void SnapToNetwork(S_vector4 &pos, bool useFrom, const S_vector4 *from)
{
    C_Checkpoints *network = GetGame()->GetCheckpoints();
    const int point = network->FindNearest(XYZ(pos), 5.0f, -1.0f);
    // (Without `from` nothing changes.)
    if (point == CHECKPOINT_NONE || !useFrom)
        return;
    const S_vector4 start = *from;
    const int startPoint = network->FindNearest(XYZ(start), 5.0f, -1.0f);
    if (startPoint == CHECKPOINT_NONE)
        return;
    if (startPoint != point) {
        // No short way (4 m) between the two points: on the edge of the start's point, toward
        // `pos`.
        std::vector<uint16_t> path;
        if (!network->FindPath(startPoint, point, &path, 0) || 4.0f < network->GetPathLength(path)) {
            const S_Checkpoint *cp = network->Get(startPoint);
            const float radius = static_cast<float>(cp->radius) * 0.0625f;
            S_vector d(pos.x - start.x, 0.0f, pos.z - start.z);
            d.Normalize();
            pos = {d.x * radius + cp->pos.x, d.y * radius + cp->pos.y, radius * d.z + cp->pos.z, start.w};
            pos.y = cp->pos.y;
        }
        return;
    }
    // The same point: within its radius, at its height.
    const S_Checkpoint *cp = network->Get(point);
    const S_vector center(cp->pos.x, 0.0f, cp->pos.z);
    pos.y = 0.0f;
    S_vector d(pos.x - center.x, 0.0f, pos.z - center.z);
    const float radius = static_cast<float>(cp->radius) * 0.0625f;
    const float distance2 = d.x * d.x + d.z * d.z + d.y * d.y;
    if (radius * radius < distance2) {
        d.Normalize();
        d *= radius;
        pos = {center.x + d.x, d.y, d.z + center.z, start.w};
    }
    pos.y = cp->pos.y;
}

void C_SquadController::UpdatePace(int /*timeDelta*/, bool ordersDone)
{
    // Near the place: slow; far: fast (with the order's own stance both slower; quiet: the
    // slowest).
    uint32_t slow = (m_flags & FLAG_2) ? MOVE_PACE_1 : MOVE_PACE_3;
    uint32_t fast = (m_flags & FLAG_2) ? MOVE_PACE_1 : MOVE_PACE_4;
    if (m_quiet) {
        slow = MOVE_PACE_1;
        fast = MOVE_PACE_1;
    }
    // Following without orders: the leader's pace (marching along), else fast.
    if (ordersDone && !(m_flags & FLAG_1)) {
        if (C_Human *leader = m_human->GetLeader()) {
            const S_vector &v = leader->GetVelocity();
            fast = MOVE_PACE_1;
            if (v.z * v.z + v.y * v.y + v.x * v.x != 0.0f)
                fast = leader->GetController()->GetMoveFlags() & MOVE_PACE_MASK;
            if (fast & MOVE_PACE_MARCH)
                fast = MOVE_PACE_MARCH;
            else if (!(m_flags & FLAG_2))
                fast = MOVE_PACE_4;
        }
    }
    uint32_t pace = m_pace;
    const bool onRoute = (m_move->flags & S_AIMoveOrder::FLAG_ROUTE) != 0;
    if (onRoute) {
        // (Following with a delay: within 1.5 m / 0.5 m; else 6 m / 3 m.)
        const S_vector4 &pos = m_human->GetPosition4();
        const float dx = pos.x - m_move->pos.x;
        const float dy = pos.y - m_move->pos.y;
        const float dz = pos.z - m_move->pos.z;
        const float distance2 = dx * dx + dy * dy + dz * dz;
        const float far2 = m_followDelay == 0 ? 36.0f : 2.25f;
        const float near2 = m_followDelay == 0 ? 9.0f : 0.25f;
        if (far2 < distance2)
            pace = fast;
        else if (distance2 < near2)
            pace = slow;
    }
    if (m_action.finished) {
        // Nothing to do on a route (not stepped aside): a new one.
        if (onRoute && !m_steppedAside) {
            if (m_flags & FLAG_1)
                m_move->flags |= S_AIMoveOrder::FLAG_REROUTE;
            if (!m_following)
                m_move->flags |= S_AIMoveOrder::FLAG_REROUTE;
        }
        // (The original also measures the leader's velocity here and drops the result.)
    }
    // The order's pace.
    if (m_orderPace != 0) {
        m_followDelay = 0;
        pace = m_orderPace;
    }
    // Out of breath: no marching; carrying a body: walking; a mine near: the slowest.
    if (m_human->m_stamina == 0.0f && pace == MOVE_PACE_MARCH)
        pace = MOVE_PACE_4;
    if (m_human->GetCarried() && (pace == MOVE_PACE_MARCH || pace == MOVE_PACE_4))
        pace = MOVE_PACE_3;
    if (m_mineNear)
        pace = MOVE_PACE_1;
    m_pace = pace;
    m_move->unknown54 = (m_move->unknown54 & ~MOVE_PACE_MASK) | pace;
}

void C_SquadController::FormationStep(const S_vector4 &target)
{
    // Walks to `target`; the leader's formation plans the member's slot from there.
    m_move->SetPos(target);
    m_steppedAside = true;
    if (!m_human->GetLeader())
        return;
    C_FormationShape *formation = m_human->GetLeader()->GetFormation();
    if (!formation)
        return;
    formation->SetSlotTarget(m_human, target);
    int slot;
    if (formation->FindSlot(m_human, &slot))
        formation->PlanSlot(slot);
}

void C_SquadController::StepAside(const S_vector4 &pos, const S_vector4 &dir, bool sideways, bool frontOnly)
{
    // A metre away from the line through `pos` along `dir` (`sideways`: a little along it
    // too; `frontOnly`: only when within 25 degrees in front of `pos`).
    if (m_move->flags & S_AIMoveOrder::FLAG_ROUTE)
        return;
    const S_vector4 &own = m_human->GetPosition4();
    S_vector away(own.x - pos.x, 0.0f, own.z - pos.z);
    away.Normalize();
    S_vector along(dir.x, 0.0f, dir.z);
    along.Normalize();
    if (frontOnly && !(0.9f <= away.y * along.y + along.x * away.x + along.z * away.z))
        return;
    S_vector side = along.Cross(away);
    side.Normalize();
    S_vector step = side.Cross(along);
    step.Normalize();
    if (sideways) {
        step.x = along.x * 0.15f + step.x;
        step.y = along.y * 0.15f + step.y;
        step.z = along.z * 0.15f + step.z;
    }
    step.Normalize();
    const S_vector4 from = own;
    S_vector4 target = {from.x + step.x, from.y + step.y, from.z + step.z, dir.w};
    if (!(m_moveFlags & MOVE_SWIMMING) && !m_human->m_body.IsWorkState5()) {
        SnapToNetwork(target, true, &from);
        FormationStep(target);
    }
}

void C_SquadController::MakeRoomForLeader()
{
    // Out of the way of the selected member walking by (1.5 m lying, 1 m crouching, else
    // 0.6 m), not while holding or walking.
    if ((m_flags & FLAG_HOLDING) || (m_move->flags & S_AIMoveOrder::FLAG_ROUTE))
        return;
    if (!g_teamManager.GetPlayerTeam()->GetSelectedMember() || m_human->m_vehicle)
        return;
    C_Human *selected = g_teamManager.GetPlayerTeam()->GetSelectedMember();
    if (selected->m_vehicle && selected->m_vehicle->GetType() == ACTOR_CAR)
        return;
    const S_vector4 &own = m_human->GetPosition4();
    const S_vector4 &other = selected->GetPosition4();
    const float dx = own.x - other.x;
    const float dy = own.y - other.y;
    const float dz = own.z - other.z;
    const float distance2 = dx * dx + dy * dy + dz * dz;
    float room2;
    switch (m_moveFlags & MOVE_STANCE_MASK) {
    case MOVE_LYING: room2 = 2.25f; break;
    case MOVE_CROUCHING: room2 = 1.0f; break;
    default: room2 = 0.36f; break;
    }
    if (!(distance2 < room2))
        return;
    // Along the way it walks (standing: its facing).
    S_vector4 dir = selected->m_velocity;
    if (dir.x * dir.x + dir.z * dir.z + dir.y * dir.y == 0.0f)
        dir = selected->GetDirection4();
    S_vector d(dir.x, dir.y, dir.z);
    d.Normalize();
    StepAside(other, {d.x, d.y, d.z, dir.w}, true, false);
}

void C_SquadController::OnGrenadeThrown(const S_vector &pos, const S_vector &dir, C_Actor *thrower)
{
    // A grenade thrown toward the member (within 30 m, the member within 20 m of its line,
    // inside the cone of twice that, at least 5 m on): it runs off to the side, the farther
    // the nearer the line.
    if (m_human == thrower || ((m_move->flags & S_AIMoveOrder::FLAG_ROUTE) && !m_steppedAside))
        return;
    const S_vector4 at = WorldPos4Of(m_human->GetFrame());
    const S_vector own(at.x, 0.0f, at.z);
    const S_vector grenade(pos.x, 0.0f, pos.z);
    S_vector along(dir.x, 0.0f, dir.z);
    along.Normalize();
    const S_vector offset(own.x - grenade.x, 0.0f, own.z - grenade.z);
    S_vector away = offset;
    away.Normalize();
    const float gx = grenade.x - own.x;
    const float gz = grenade.z - own.z;
    if (900.0f < gz * gz + gx * gx)
        return;
    if (along.z * away.z + away.x * along.x + away.y * along.y < 0.0f)
        return;
    const S_vector toGrenade = grenade - own;
    const float lateral = LineDistance(toGrenade, along);
    const float distance = offset.Magnitude();
    const S_vector across = along.Cross(S_vector(0.0f, 1.0f, 0.0f));
    const float ahead = LineDistance(toGrenade, across);
    if (lateral >= 20.0f || lateral + lateral > distance || ahead < 5.0f)
        return;
    // To the member's side of the line.
    S_vector up = along.Cross(away);
    up.Normalize();
    S_vector flee = up.Cross(along);
    flee.Normalize();
    const S_vector &frame = m_human->GetFrame()->GetWorldPos();
    const S_vector target = frame + flee * (10.0f - lateral);
    S_vector4 target4 = {target.x, target.y, target.z, 0.0f};   // (w: a stale local in the original)
    SnapToNetwork(target4, false, nullptr);
    FormationStep(target4);
}

bool C_SquadController::FindAttackPoint(S_vector4 *pos, float *radius, C_Actor *target)
{
    // The walking points named "#<the target's frame>" (the map's places to attack it from):
    // the nearest by the network from the member's point (off the network: one at random).
    const C_fstr prefix("#%s", target->GetFrame()->GetName());
    C_Checkpoints *network = GetGame()->GetCheckpoints();
    std::vector<uint16_t> points;
    network->FindByPrefix(prefix, points);
    if (points.empty())
        return false;
    int best = CHECKPOINT_NONE;
    const int start = network->FindNearest(m_human->GetFrame()->GetWorldPos(), 5.0f, -1.0f);
    if (start == CHECKPOINT_NONE) {
        best = points[static_cast<uint32_t>(std::rand()) % points.size()];
    } else {
        float bestLength = 3.4028235e+38f;
        for (size_t i = points.size(); i-- > 0;) {
            std::vector<uint16_t> path;
            if (network->FindPath(start, points[i], &path, 0)) {
                const float length = network->GetPathLength(path);
                if (length < bestLength) {
                    best = points[i];
                    bestLength = length;
                }
            }
        }
    }
    if (static_cast<int16_t>(best) == -1)
        return false;
    const S_Checkpoint *cp = network->Get(best);
    *pos = {cp->pos.x, cp->pos.y, cp->pos.z, 0.0f};     // (w: a stale local in the original)
    *radius = static_cast<float>(cp->radius) * 0.0625f;
    return true;
}

bool C_SquadController::AtSpecialPoint()
{
    // On a route at a point of a kind (types 1..4, 6) the next waypoint goes on with (1..5):
    // the alert preset and the formation wait. (The point types are not identified.)
    if (!(m_move->flags & S_AIMoveOrder::FLAG_ROUTE) || m_waypoints.empty())
        return false;
    if (m_human->m_body.IsWorkState5() || !m_currentCheckpoint)
        return false;
    const uint8_t next = m_waypoints[0]->type;
    switch (m_currentCheckpoint->type) {
    case 1: return next == 1;
    case 2: return next == 2;
    case 3: return next == 3;
    case 4: return next == 4;
    case 6: return next == 5;
    default: return false;
    }
}

void C_SquadController::StepAsideFrom(C_Actor *actor)
{
    if (!actor)
        return;
    // The view direction is the first three floats of a S_vector4; the original passes it as one.
    const S_vector4 &dir = reinterpret_cast<const S_vector4 &>(static_cast<C_Human *>(actor)->GetViewDir());
    StepAside(actor->GetPosition4(), dir, false, true);
}
