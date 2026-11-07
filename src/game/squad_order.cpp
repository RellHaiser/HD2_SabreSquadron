// The orders of a squad member's queue (game/squad_order.h; original source 0x0058c540..
// 0x0058d7e0) and their pool (0x005825c0..0x00582640, 0x00583980).
#include "game/squad_order.h"

#include <malloc.h>
#include <cstdlib>
#include <cstring>
#include <vector>

#include "common/object_pool.h"
#include "game/actor.h"
#include "game/checkpoints.h"
#include "game/collision.h"
#include "game/controller_internal.h"
#include "game/game.h"
#include "game/human.h"
#include "game/human_controller.h"
#include "game/interface.h"
#include "game/pending.h"
#include "game/world.h"
#include "ls3d/i3d.h"

namespace {

// The order's collision volume: a box of 0.4 x 1.6 x 0.4 m standing on the marker, which
// the command map picks.
constexpr float ORDER_VOLUME_HALF_WIDTH = 0.2f;
constexpr float ORDER_VOLUME_HEIGHT = 1.6f;
// A path search that fails counts as this long (m).
constexpr float NO_PATH_LENGTH = 1e6f;
// The path is made again when its ends moved more than 0.2 m.
constexpr float PATH_MOVED2 = 0.04f;

// The orders' pool (in the AI pools at 0x008ab158 +0x18 in the original; the body commands'
// at +0x00 is not used: nothing allocates from it).
C_ObjectPool<S_HumanOrder> g_orderPool{16};

S_vector4 FramePos4(const I3D_frame *frame)
{
    const S_matrix &m = frame->GetMatrix();
    return {m.m[3][0], m.m[3][1], m.m[3][2], m.m[3][3]};
}

void FreePath(S_Vector4Array &path)
{
    _aligned_free(path.data);
    path.data = nullptr;
    path.count = 0;
    path.capacity = 0;
}

// Appends `point` to the growable array (grown by `growBy` when full).
void AppendPoint(S_Vector4Array &path, const S_vector4 &point)
{
    if (path.count >= path.capacity) {
        path.capacity += path.growBy;
        S_vector4 *data = static_cast<S_vector4 *>(_aligned_malloc(sizeof(S_vector4) * path.capacity, 16));
        for (int i = 0; i < path.count; ++i)
            data[i] = path.data[i];
        _aligned_free(path.data);
        path.data = data;
    }
    path.data[path.count++] = point;
}

// The path's length from `from` to point `to` (NO_PATH_LENGTH when there is none; 0 when it is
// the start).
float PathLength(C_CheckpointNetwork *network, int from, int to, std::vector<uint16_t> &path)
{
    if (from == to)
        return 0.0f;
    if (!network->FindPath(from, to, &path, 0))
        return NO_PATH_LENGTH;
    return network->GetPathLength(path);
}

}  // namespace

// ------------------------------------------------------------------------- the pool

S_HumanOrder *AllocHumanOrder(C_Human *human, int kind, int arg, const S_vector4 &pos, const S_vector4 &data,
                              uint32_t pace, uint32_t stance, uint32_t mode, C_Actor *actor, int unknown70)
{
    S_HumanOrder *order = g_orderPool.Get();
    order->Init(human, kind, arg, pos, data, pace, stance, mode, actor, unknown70);
    return order;
}

void ReleaseHumanOrder(S_HumanOrder *order)
{
    // (The original also takes back body commands, type 1, into their pool; none comes
    // from it.) Fix: the original releases a null order's path.
    if (!order || order->type != 4)
        return;
    order->Release();
    g_orderPool.Put(order);
}

// ------------------------------------------------------------------------- the order

S_HumanOrder::~S_HumanOrder()
{
    if (marker)
        RemoveMarker();
    _aligned_free(path.data);
}

void S_HumanOrder::Init(C_Human *human_, int kind_, int arg_, const S_vector4 &pos_, const S_vector4 &data_,
                        uint32_t pace, uint32_t stance, uint32_t mode, C_Actor *actor_, int unknown70_)
{
    kind = kind_;
    human = human_;
    arg = arg_;
    data = data_;
    pos = pos_;
    actor = actor_;
    unknown70 = unknown70_;
    flags = pace | stance | mode;
    // (The w of both is a stale local in the original.)
    pathFrom = {100000.0f, 100000.0f, 100000.0f, 0.0f};
    pathTo = {100000.0f, 100000.0f, 100000.0f, 0.0f};
    started = false;
    done = false;
    selected = false;
    if (marker)
        RemoveMarker();
    marker = GetGame()->GetInterface()->AcquireOrderMarker(this);
    marker->SetData(reinterpret_cast<S_FrameData *>(this));
    marker->SetPos(XYZ(pos));
    marker->LinkTo(GetGame()->GetScene()->GetDynamicRoot(), 1);
    marker->Update();
    // A box standing on the marker, for the map's picking.
    volume = S_CollisionVolume();
    volume.flags = ORDER_VOLUME_SHAPE;
    volume.center = XYZ(pos);
    volume.centerW = pos.w;
    volume.bbox.min = S_vector(-ORDER_VOLUME_HALF_WIDTH, 0.0f, -ORDER_VOLUME_HALF_WIDTH);
    volume.bbox.max = S_vector(ORDER_VOLUME_HALF_WIDTH, ORDER_VOLUME_HEIGHT, ORDER_VOLUME_HALF_WIDTH);
    volume.radius = ORDER_VOLUME_HEIGHT;
    volume.matrix = &marker->GetMatrix();
    invMatrix.Inverse(marker->GetMatrix());
    volume.invMatrix = &invMatrix;
    volume.frame = marker;
    volume.type = ORDER_VOLUME_TYPE;
    g_collisionWorld.AddVolume(&volume);
}

void S_HumanOrder::InitPlain(C_Human *human_, const S_vector4 &pos_, const S_vector4 &data_, uint32_t pace,
                             uint32_t stance, uint32_t mode)
{
    human = human_;
    data = data_;
    pos = pos_;
    flags = pace | stance | mode;
    started = false;
    done = false;
    selected = false;
    if (marker)
        RemoveMarker();
    marker = nullptr;
}

void S_HumanOrder::Release()
{
    RemoveMarker();
    if (path.count != 0)
        FreePath(path);
}

void S_HumanOrder::RemoveMarker()
{
    if (!marker)
        return;
    g_collisionWorld.RemoveVolume(&volume);
    marker->SetData(nullptr);
    GetGame()->GetInterface()->ReleaseOrderMarker(marker);
    marker = nullptr;
    volume = S_CollisionVolume();
}

void S_HumanOrder::CopyFrom(const S_HumanOrder &other)
{
    if (XYZ(other.data).Square() >= 1e-16f)
        data = other.data;
    if (XYZ(other.pos).Square() >= 1e-16f)
        pos = other.pos;
    if (other.flags != 0)
        flags = other.flags;
    GetGame()->GetInterface()->UpdateOrderMarker(this, marker);
}

S_vector4 S_HumanOrder::GetActorPoint(const C_Actor *target) const
{
    const I3D_frame *frame = target->GetFrame();
    S_vector4 point = FramePos4(frame);
    switch (target->GetType()) {
    case ACTOR_MINE:
    case ACTOR_ITEM:
    case ACTOR_ANIMATION:
        break;
    case ACTOR_HUMAN: {
        // The body's frame, 0.2 m up (w: a stale local in the original).
        const S_vector &body = static_cast<const C_Human *>(target)->GetBodyFrame()->GetWorldPos();
        point = {body.x, body.y + 0.2f, body.z, point.w};
        break;
    }
    case ACTOR_CAR:
    case ACTOR_SUBMARINE:
    case ACTOR_STATIC_WEAPON:
        // (w: a stale local in the original.)
        point.y += 1.0f;
        break;
    case ACTOR_DOOR:
    case ACTOR_RADIO:
    case ACTOR_LADDER:
    case ACTOR_SWITCH:
    case ACTOR_PHOTO_PLACE:
    case ACTOR_EXPLOSIVE_PLACE:
    case ACTOR_PIKE:
    case ACTOR_WIRE_CUT:
        // The path's last point more than 1 m from the actor (from the end; none: the actor).
        for (int i = path.count; i-- > 0;) {
            if ((XYZ(path.data[i]) - XYZ(point)).Square() > 1.0f) {
                point = path.data[i];
                break;
            }
        }
        break;
    case ACTOR_MELEE_TRACKER: {
        const S_vector &body = static_cast<const C_MeleeTracker *>(target)->GetBodyFrame()->GetWorldPos();
        point = {body.x, body.y + 0.2f, body.z, point.w};
        break;
    }
    default:
        // The path's end. (Fix: the original reads before the points of an empty path.)
        if (path.count > 0)
            point = path.data[path.count - 1];
        break;
    }
    return point;
}

void S_HumanOrder::UpdateMarker()
{
    S_vector4 point;
    if (actor) {
        point = GetActorPoint(actor);
    } else if (kind == 4 || kind == HUMAN_ORDER_WATCH || kind == HUMAN_ORDER_6 || kind == HUMAN_ORDER_7) {
        if (!human->GetVehicle()) {
            // Ahead of the human along the order's direction (lying: 2 m, else 1.2 m).
            const float ahead = (human->GetStanceFlags() & MOVE_LYING) ? 2.0f : 1.2f;
            point = {ahead * data.x + pos.x, ahead * data.y + pos.y, ahead * data.z + pos.z, 0.0f};
        } else {
            const S_vector &body = human->GetBodyFrame()->GetWorldPos();
            point = {body.x, body.y + 0.2f, body.z, 0.0f};
        }
    } else {
        point = pos;
    }
    marker->SetPos(XYZ(point));
    marker->Update();
    // (The original writes stale registers to the volume's centre.)
    volume.center = XYZ(point);
    volume.centerW = point.w;
    invMatrix.Inverse(marker->GetMatrix());
    volume.invMatrix = &invMatrix;
    g_collisionWorld.MoveVolume(&volume);
}

void S_HumanOrder::UpdatePath(const S_vector4 &from)
{
    if (kind == 4 || kind == HUMAN_ORDER_WATCH || kind == HUMAN_ORDER_6 || kind == HUMAN_ORDER_7) {
        // The path is the start and the marker.
        FreePath(path);
        path.capacity = path.growBy;
        path.data = static_cast<S_vector4 *>(_aligned_malloc(sizeof(S_vector4) * path.capacity, 16));
        AppendPoint(path, from);
        AppendPoint(path, FramePos4(marker));
        UpdateMarker();
        return;
    }
    if (path.count != 0) {
        // Nothing moved: the path stays.
        if ((XYZ(pathFrom) - XYZ(from)).Square() <= PATH_MOVED2) {
            const S_vector to = actor ? actor->GetFrame()->GetWorldPos() : XYZ(pos);
            if ((XYZ(pathTo) - to).Square() <= PATH_MOVED2)
                return;
        }
        FreePath(path);
    }
    C_Checkpoints *network = GetGame()->GetCheckpoints();
    pathFrom = from;
    const int start = network->FindNearest(XYZ(pathFrom), 5.0f, -1.0f);
    int goal = CHECKPOINT_NONE;
    bool goalFound = false;
    if (!actor) {
        pathTo = pos;
    } else if (actor->GetType() == ACTOR_DOOR) {
        // The nearer side of the door.
        std::vector<uint16_t> sides;
        network->GetActorPoints(actor, sides);
        if (sides.size() != 2) {
            pathTo = FramePos4(actor->GetFrame());
        } else {
            std::vector<uint16_t> path;
            const float toFirst = PathLength(network, start, sides[0], path);
            const float toSecond = PathLength(network, start, sides[1], path);
            goal = toSecond <= toFirst ? sides[1] : sides[0];
            // (w: a stale local in the original.)
            pathTo = ToVector4(network->Get(goal)->pos, 0.0f);
            goalFound = true;
        }
    } else if (actor->GetType() == ACTOR_STATIC_WEAPON) {
        goal = network->FindActorPoint(actor);
        if (goal == CHECKPOINT_NONE)
            pathTo = FramePos4(actor->GetFrame());
        else
            pathTo = ToVector4(network->Get(goal)->pos, 0.0f);   // (w: a stale local in the original)
        goalFound = true;
    } else if (actor->GetType() == ACTOR_WIRE_CUT) {
        // The nearer of the fence's two points.
        const C_WireCut *wire = static_cast<const C_WireCut *>(actor);
        std::vector<uint16_t> path;
        const int first = wire->GetPoint(0);
        const int second = wire->GetPoint(1);
        const float toFirst = PathLength(network, start, first, path);
        const float toSecond = PathLength(network, start, second, path);
        goal = toSecond <= toFirst ? second : first;
        pathTo = ToVector4(network->Get(goal)->pos, 0.0f);   // (w: a stale local in the original)
        goalFound = true;
    } else {
        pathTo = FramePos4(actor->GetFrame());
    }
    if (!goalFound || static_cast<int16_t>(goal) == -1)
        goal = network->FindNearest(XYZ(pathTo), 5.0f, -1.0f);
    std::vector<uint16_t> points;
    network->FindPath(start, goal, &points, 0);
    BuildPathPoints(pathFrom, pathTo, points, path);
    // Without a direction the order faces along the path's last step. (Fix: the original reads
    // before the points of an empty path.)
    if (XYZ(data).Square() < 1e-16f && path.count > 0) {
        // (The original subtracts the last point from itself: a zero direction, normalized to
        // +y.)
        const S_vector4 &last = path.data[path.count - 1];
        S_vector dir = XYZ(last) - XYZ(last);
        dir.Normalize();
        data = {dir.x, dir.y, dir.z, data.w};
    }
    UpdateMarker();
}
