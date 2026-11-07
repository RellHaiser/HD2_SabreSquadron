// The shape of a formation and its members' places (game/team.h; 0x005942d0..0x0059bb60). The shape's slots come from a table by type, scaled and turned by the
// shape's matrix; each tick the slots are placed around the leader as it goes, kept apart and fitted onto the checkpoint network; the members ask where theirs is.
// Dead, not here: 0x00594210, 0x00595420, 0x00597b50, 0x0059ab30.
#include <cmath>
#include <cstring>

#include "common/chunk.h"
#include "game/actor.h"
#include "game/actors_module.h"
#include "game/car.h"
#include "game/checkpoints.h"
#include "game/game.h"
#include "game/human.h"
#include "game/human_controller.h"
#include "game/submarine.h"
#include "game/team.h"
#include "ls3d/i3d.h"

#include "game/formation_tables.inc"

namespace formation {

const float SLOT_APART = 1.2f;

enum : uint16_t {
    CK_SHAPE = 0x5BE,
    CK_MATRIX = 1000,
    CK_DIR = 2000,
    CK_SCALE = 3000,
    CK_CHANGED = 4000,
    CK_SCRIPTED = 5000,
    CK_DOOR_POINT = 6000,
    CK_DOOR_STATE = 7000,
    CK_COUNT = 8000,
    CK_LEADER = 9000,
    CK_TYPE = 10000,
    CK_MODE = 11000,
    CK_SLOTS = 12000,
    CK_SLOT = 0x1E61,
    CK_SLOT_BASE = 1000,
    CK_SLOT_SCALED = 2000,
    CK_SLOT_TARGET = 3000,
    CK_SLOT_CURRENT = 4000,
    CK_SLOT_DIR = 5000,
    CK_SLOT_BASE_DIR = 6000,
    CK_SLOT_SCALED_DIR = 7000,
    CK_SLOT_MEMBER = 8000,
    CK_SLOT_UNKNOWN = 9000,
    CK_SLOT_DISTANCE = 10000,
    CK_SLOT_SEAT = 11000,
    CK_SLOT_VEHICLE = 0x2CEC,
    CK_SLOT_ASIDE = 12000,
    CK_SLOT_DIRTY = 13000,
    CK_SLOT_PATH_FROM = 14000,
    CK_SLOT_CHECKPOINT = 15000,
    CK_SLOT_PATH = 16000,
};

S_vector Xyz(const S_vector4 &value) { return S_vector(value.x, value.y, value.z); }
void SetXyz(S_vector4 &value, const S_vector &v)
{
    value.x = v.x;
    value.y = v.y;
    value.z = v.z;
}
S_vector Sub(const S_vector &a, const S_vector &b) { return S_vector(a.x - b.x, a.y - b.y, a.z - b.z); }
float Sq(const S_vector &a) { return a.x * a.x + a.y * a.y + a.z * a.z; }

// As the game's copies do it.
float LengthOfSq(float sq)
{
    uint32_t bits;
    std::memcpy(&bits, &sq, 4);
    if (static_cast<int32_t>(bits) < 0x322BCC77)
        return 0.0f;
    if (std::fabs(sq - 1.0f) < 1e-8f)
        return 1.0f;
    return std::sqrt(sq);
}

S_vector Unit(S_vector v)
{
    const float sq = Sq(v);
    if (std::fabs(sq - 1.0f) >= 1e-8f) {
        if (sq >= 1e-8f) {
            const float k = 1.0f / std::sqrt(sq);
            v = S_vector(v.x * k, v.y * k, v.z * k);
        } else if (v.x != 0.0f) {
            v.x = v.x >= 0.0f ? 1.0f : -1.0f;
        } else if (v.z != 0.0f) {
            v.z = v.z >= 0.0f ? 1.0f : -1.0f;
        } else {
            v.y = v.y >= 0.0f ? 1.0f : -1.0f;
        }
    }
    return v;
}

bool IsVehicleType(const C_Actor *actor)
{
    if (!actor)
        return false;
    const int type = actor->GetType();
    return type == ACTOR_CAR || type == ACTOR_SUBMARINE || type == 0x15 || type == 0x22;
}

// (0x00594a80; thiscall on the slot in the original)
void WriteSlot(C_chunk &chunk, const S_FormationSlot &slot)
{
    chunk.Begin(CK_SLOT);
    chunk.WriteChunk(CK_SLOT_BASE, &slot.basePos, 12);
    chunk.WriteChunk(CK_SLOT_SCALED, &slot.scaledPos, 12);
    chunk.WriteChunk(CK_SLOT_TARGET, &slot.target, 12);
    chunk.WriteChunk(CK_SLOT_CURRENT, &slot.current, 12);
    chunk.WriteChunk(CK_SLOT_DIR, &slot.dir, 12);
    chunk.WriteChunk(CK_SLOT_BASE_DIR, &slot.baseDir, 12);
    chunk.WriteChunk(CK_SLOT_SCALED_DIR, &slot.scaledDir, 12);
    if (slot.member) {
        const int id = slot.member->GetNetId();
        chunk.WriteChunk(CK_SLOT_MEMBER, &id, 4);
    }
    if (slot.unknown74) {
        const int id = slot.unknown74->GetNetId();
        chunk.WriteChunk(CK_SLOT_UNKNOWN, &id, 4);
    }
    chunk.WriteChunk(CK_SLOT_DISTANCE, &slot.distance, 4);
    chunk.WriteChunk(CK_SLOT_SEAT, &slot.seat, 4);
    chunk.WriteChunk(CK_SLOT_VEHICLE, &slot.vehicleId, 4);
    chunk.WriteChunk(CK_SLOT_ASIDE, &slot.pushedAside, 1);
    chunk.WriteChunk(CK_SLOT_DIRTY, &slot.dirty, 1);
    chunk.WriteChunk(CK_SLOT_PATH_FROM, &slot.pathFrom, 2);
    chunk.WriteChunk(CK_SLOT_CHECKPOINT, &slot.checkpoint, 2);
    if (!slot.path.empty()) {
        const int count = static_cast<int>(slot.path.size());
        chunk.WriteChunk(CK_SLOT_PATH, &count, 4);
        // Kept: the points follow the chunk, not in it.
        for (const uint16_t point : slot.path)
            chunk.Write(&point, 2);
    }
    chunk.Ascend();
}

// (0x00594dd0; thiscall on the slot in the original)
void ReadSlot(C_chunk &chunk, S_FormationSlot &slot, C_Human *leader)
{
    slot.member = nullptr;
    slot.unknown74 = nullptr;
    slot.path.clear();
    uint16_t id;
    if (!chunk.Descend(id))
        return;
    while (chunk.HasMore()) {
        if (!chunk.Descend(id))
            break;
        int count = 0;
        switch (id) {
        case CK_SLOT_BASE: chunk.Read(&slot.basePos, 12); break;
        case CK_SLOT_SCALED: chunk.Read(&slot.scaledPos, 12); break;
        case CK_SLOT_TARGET: chunk.Read(&slot.target, 12); break;
        case CK_SLOT_CURRENT: chunk.Read(&slot.current, 12); break;
        case CK_SLOT_DIR: chunk.Read(&slot.dir, 12); break;
        case CK_SLOT_BASE_DIR: chunk.Read(&slot.baseDir, 12); break;
        case CK_SLOT_SCALED_DIR: chunk.Read(&slot.scaledDir, 12); break;
        case CK_SLOT_MEMBER: {
            int actor = 0;
            chunk.Read(&actor, 4);
            slot.member = static_cast<C_Human *>(g_actorsModule.GetActorById(actor));
            if (slot.member && leader && slot.member != leader)
                slot.member->SetLeader(leader);
            break;
        }
        case CK_SLOT_UNKNOWN: {
            int actor = 0;
            chunk.Read(&actor, 4);
            slot.unknown74 = g_actorsModule.GetActorById(actor);
            break;
        }
        case CK_SLOT_DISTANCE: chunk.Read(&slot.distance, 4); break;
        case CK_SLOT_SEAT: chunk.Read(&slot.seat, 4); break;
        case CK_SLOT_VEHICLE: chunk.Read(&slot.vehicleId, 4); break;
        case CK_SLOT_ASIDE: chunk.Read(&slot.pushedAside, 1); break;
        case CK_SLOT_DIRTY: chunk.Read(&slot.dirty, 1); break;
        case CK_SLOT_PATH_FROM: chunk.Read(&slot.pathFrom, 2); break;
        case CK_SLOT_CHECKPOINT: chunk.Read(&slot.checkpoint, 2); break;
        case CK_SLOT_PATH:
            chunk.Read(&count, 4);
            chunk.Ascend();
            for (; count > 0; --count) {
                uint16_t point = 0;
                chunk.Read(&point, 2);
                slot.path.push_back(point);
            }
            continue;
        default:
            break;
        }
        chunk.Ascend();
    }
    chunk.Ascend();
}

}  // namespace formation

using namespace formation;

C_FormationShape::C_FormationShape()
{
    m_matrix.Identity();
    SetType(FORMATION_FOLLOW);
    SetDir(S_vector(0.0f, 0.0f, 1.0f));
    SetScale(S_vector(2.5f, 2.5f, 2.5f));
    m_changed = false;
    m_scripted = false;
}

C_FormationShape::~C_FormationShape()
{
    SetType(FORMATION_NONE);
}

void C_FormationShape::SetType(E_FormationType type)
{
    m_changed = true;
    if (type == FORMATION_NONE) {
        if (m_slots) {
            delete[] m_slots;
            m_slots = nullptr;
            m_type = FORMATION_NONE;
        }
        return;
    }
    if (!m_slots)
        m_slots = new S_FormationSlot[SLOT_COUNT];
    if (type == FORMATION_NEXT) {
        int next = m_type + 1;
        if (next > 10)
            next = 1;
        m_type = static_cast<E_FormationType>(next);
    } else {
        m_type = type;
    }
    for (int i = SLOT_COUNT - 1; i >= 0; --i) {
        const float *const row = FORMATION_TABLE[m_type][i];
        S_FormationSlot &slot = m_slots[i];
        slot.basePos = {row[0], row[1], row[2], 0.0f};
        const S_vector dir = Unit(S_vector(row[3], row[4], row[5]));
        slot.baseDir = {dir.x, dir.y, dir.z, 0.0f};
    }
    ApplyMatrix();
}

void C_FormationShape::SetDir(const S_vector &dir)
{
    SetXyz(m_dir, dir);
    SetScale(Xyz(m_scale));
}

void C_FormationShape::SetScale(const S_vector &scale)
{
    SetXyz(m_scale, scale);
    m_matrix.SetDir(Xyz(m_dir));
    for (int axis = 0; axis < 3; ++axis) {
        const float k = axis == 0 ? scale.x : axis == 1 ? scale.y : scale.z;
        m_matrix.m[axis][0] *= k;
        m_matrix.m[axis][1] *= k;
        m_matrix.m[axis][2] *= k;
    }
    ApplyMatrix();
}

void C_FormationShape::ApplyMatrix()
{
    if (!m_slots)
        return;
    for (int i = SLOT_COUNT - 1; i >= 0; --i) {
        S_FormationSlot &slot = m_slots[i];
        const S_vector scaled = Xyz(slot.basePos) * m_matrix;
        SetXyz(slot.scaledPos, scaled);
        SetXyz(slot.scaledDir, Unit(Xyz(slot.baseDir) * m_matrix));
        // (Kept: from the first slot's scaled place, as it was until its own turn comes last.)
        slot.distance = LengthOfSq(Sq(Sub(scaled, Xyz(m_slots[0].scaledPos))));
    }
}

bool C_FormationShape::FindSlot(const C_Human *member, int *slot) const
{
    for (int i = SLOT_COUNT - 1; i >= 0; --i) {
        if (m_slots[i].member == member) {
            if (slot)
                *slot = i;
            return true;
        }
    }
    return false;
}

bool C_FormationShape::AddMember(C_Human *member)
{
    if (FindSlot(member, nullptr))
        return false;
    for (int i = 0; i < SLOT_COUNT; ++i) {
        S_FormationSlot &slot = m_slots[i];
        if (slot.member)
            continue;
        slot.member = member;
        SetXyz(slot.target, member->GetFrame()->GetWorldPos());
        slot.target.w = member->GetFrame()->GetMatrix().m[3][3];
        const S_matrix &world = member->GetFrame()->GetMatrix();
        SetXyz(slot.dir, Unit(S_vector(world.m[2][0], world.m[2][1], world.m[2][2])));
        slot.current = slot.target;
        slot.dirty = true;
        ++m_memberCount;
        return true;
    }
    return false;
}

bool C_FormationShape::RemoveMember(C_Human *member)
{
    if (!FindSlot(member, nullptr))
        return false;
    if (member == m_leader)
        SetLeader(nullptr);
    for (int i = 0; i < SLOT_COUNT; ++i) {
        if (m_slots[i].member != member)
            continue;
        m_slots[i].member = nullptr;
        if (m_leader && i < SLOT_COUNT - 1) {
            for (int j = i; j < SLOT_COUNT - 1; ++j) {
                if (m_slots[i].member == m_leader || m_slots[j].member == m_leader) {
                    // (Fix: not past the slots; the original reads beyond the last.)
                    if (m_slots[j + 1].member == m_leader && j + 2 < SLOT_COUNT)
                        m_slots[j].member = m_slots[j + 2].member;
                } else {
                    m_slots[j].member = m_slots[j + 1].member;
                }
            }
        }
        m_slots[SLOT_COUNT - 1].member = nullptr;
        --m_memberCount;
        return true;
    }
    return false;
}

bool C_FormationShape::SetLeader(C_Human *leader)
{
    if (leader == m_leader)
        return true;
    SetDoorPoint(0xFFFF);
    if (leader && !FindSlot(leader, nullptr) && !AddMember(leader))
        return false;
    m_leader = leader;
    if (!leader)
        return true;
    // The first slot is the leader's.
    C_Human *const displaced = m_slots[0].member;
    m_slots[0].member = leader;
    for (int i = SLOT_COUNT - 1; i >= 0; --i) {
        if (m_slots[i].member == m_leader) {
            m_slots[i].member = displaced;
            m_slots[0].member = m_leader;
            break;
        }
    }
    ApplyMatrix();
    const S_matrix &world = leader->GetFrame()->GetMatrix();
    S_matrix placed = world;
    placed.SetDir(Unit(S_vector(world.m[2][0], world.m[2][1], world.m[2][2])));
    const S_vector pos = leader->GetFrame()->GetWorldPos();
    placed.m[3][0] = pos.x;
    placed.m[3][1] = pos.y;
    placed.m[3][2] = pos.z;
    for (int i = SLOT_COUNT - 1; i >= 0; --i)
        SetXyz(m_slots[i].target, Xyz(m_slots[i].scaledPos) * placed);
    SetXyz(m_slots[0].target, pos);
    m_slots[0].target.w = world.m[3][3];
    m_slots[0].checkpoint = 0xFFFF;
    FitOnNetwork(0);
    return true;
}

void C_FormationShape::OnLeaderDied()
{
    if (!m_slots)
        return;
    for (int i = SLOT_COUNT - 1; i >= 0; --i) {
        C_Human *const member = m_slots[i].member;
        if (!member || member == m_leader || member->GetControllerType() != HUMAN_CONTROLLER_SQUAD)
            continue;
        C_SquadController *const controller = static_cast<C_SquadController *>(member->GetController());
        if (!controller)
            continue;
        const int order = controller->GetCombatOrder();
        if (order == 0xC || order == 0xE)
            controller->SetCombatOrder(0x10);
        else if (order == 0xD || order == 0xF)
            controller->SetCombatOrder(0x11);
    }
}

bool C_FormationShape::GetSlotTarget(const C_Human *member, S_vector4 *pos, uint16_t *checkpoint)
{
    if (!member || !m_leader || !m_slots)
        return false;
    int index = 0;
    if (!FindSlot(member, &index))
        return false;
    S_FormationSlot &slot = m_slots[index];
    *pos = slot.target;
    if (checkpoint)
        *checkpoint = slot.checkpoint;
    if (slot.dirty) {
        slot.dirty = false;
        return true;
    }
    return false;
}

bool C_FormationShape::GetSlotDir(const C_Human *member, S_vector4 *dir)
{
    if (!m_leader || !m_slots)
        return false;
    int index = 0;
    if (!FindSlot(member, &index))
        return false;
    *dir = m_slots[index].dir;
    return true;
}

int C_FormationShape::GetSlotSeat(const C_Human *member, int *vehicleId)
{
    if (!m_leader || !m_slots)
        return 0;
    int index = 0;
    if (!FindSlot(member, &index))
        return index;
    *vehicleId = m_slots[index].vehicleId;
    return m_slots[index].seat;
}

void C_FormationShape::InvalidateSlot(const C_Human *member)
{
    if (!m_leader || !m_slots)
        return;
    int index = 0;
    if (FindSlot(member, &index))
        m_slots[index].dirty = true;
}

void C_FormationShape::SetSlotTarget(const C_Human *member, const S_vector4 &pos)
{
    int index = 0;
    if (!FindSlot(member, &index))
        return;
    m_slots[index].target = pos;
    m_slots[index].checkpoint = 0xFFFF;
    m_slots[index].dirty = true;
}

bool C_FormationShape::FitOnNetwork(int index)
{
    S_FormationSlot &slot = m_slots[index];
    C_Checkpoints *const network = GetGame()->GetCheckpoints();
    int found = network->FindNearest(Xyz(slot.target), 5.0f, -1.0f);
    slot.checkpoint = static_cast<uint16_t>(found);
    if (found == CHECKPOINT_NONE) {
        found = network->FindNearestType15(Xyz(slot.target), 5.0f, -1.0f);
        slot.checkpoint = static_cast<uint16_t>(found);
        if (found == CHECKPOINT_NONE)
            return false;
    }
    const S_Checkpoint *const point = network->Get(slot.checkpoint);
    const float radius = static_cast<float>(point->radius) * 0.0625f;
    slot.target.y = 0.0f;
    const S_vector off(slot.target.x - point->pos.x, 0.0f, slot.target.z - point->pos.z);
    if (radius * radius < Sq(off)) {
        const S_vector dir = Unit(off);
        slot.target.x = dir.x * radius + point->pos.x;
        slot.target.z = dir.z * radius + point->pos.z;
    }
    slot.target.y = point->pos.y;
    slot.dirty = true;
    if (m_leader && point->type == CHECKPOINT_TYPE_15) {
        const float above = point->pos.y + 0.5f;
        const float own = m_leader->GetPosition().y;
        slot.target.y = own <= above ? above : own;
    }
    return true;
}

void C_FormationShape::KeepApart()
{
    for (int i = SLOT_COUNT - 1; i >= 0; --i) {
        S_FormationSlot &slot = m_slots[i];
        if (slot.member == m_leader) {
            FitOnNetwork(i);
        } else if (slot.member && !slot.pushedAside) {
            for (int j = SLOT_COUNT - 1; j >= 0; --j) {
                const S_FormationSlot &other = m_slots[j];
                if (other.member == slot.member || !other.member)
                    continue;
                const S_vector off = Sub(Xyz(slot.target), Xyz(other.target));
                const float sq = Sq(off);
                if (LengthOfSq(sq) < SLOT_APART) {
                    const S_vector dir = Unit(off);
                    slot.target.x = dir.x * SLOT_APART + other.target.x;
                    slot.target.y = dir.y * SLOT_APART + other.target.y;
                    slot.target.z = dir.z * SLOT_APART + other.target.z;
                    slot.dirty = true;
                    slot.pushedAside = true;
                    break;
                }
            }
            FitOnNetwork(i);
        }
    }
}

void C_FormationShape::PushAside(int index, float distance)
{
    if (!m_leader)
        return;
    S_FormationSlot &slot = m_slots[index];
    if (m_leader == slot.member)
        return;
    const S_vector leaderVel = Xyz(m_leader->m_velocity);
    const S_vector memberVel = Xyz(slot.member->m_velocity);
    S_vector leaderDir;
    if (LengthOfSq(Sq(memberVel)) <= 0.0f || LengthOfSq(Sq(leaderVel)) <= 0.0f) {
        leaderDir = Unit(leaderVel);
    } else {
        leaderDir = Unit(leaderVel);
        const S_vector memberDir = Unit(memberVel);
        // Going the same way: it can stay.
        if (0.1f < leaderDir.x * memberDir.x + leaderDir.y * memberDir.y + leaderDir.z * memberDir.z)
            return;
    }
    const S_vector r = Sub(Xyz(slot.target), m_leader->GetFrame()->GetWorldPos());
    const S_vector across(leaderVel.y * r.z - leaderVel.z * r.y, leaderVel.z * r.x - r.z * leaderVel.x, r.y * leaderVel.x - leaderVel.y * r.x);
    const S_vector away = Unit(S_vector(leaderVel.z * across.y - leaderVel.y * across.z, across.z * leaderVel.x - leaderVel.z * across.x, leaderVel.y * across.x - across.y * leaderVel.x));
    const S_vector dir = Unit(S_vector(leaderDir.x * 0.15f + away.x, leaderDir.y * 0.15f + away.y, leaderDir.z * 0.15f + away.z));
    slot.target.x += dir.x * distance;
    slot.target.y += dir.y * distance;
    slot.target.z += dir.z * distance;
    slot.dirty = true;
    slot.pushedAside = true;
}

void C_FormationShape::PlanSlot(int index)
{
    if (index == 0)
        return;
    S_FormationSlot &slot = m_slots[index];
    if (!slot.member)
        return;
    S_FormationSlot &first = m_slots[0];
    if (slot.checkpoint == 0xFFFF || first.checkpoint == 0xFFFF)
        return;
    C_Checkpoints *const network = GetGame()->GetCheckpoints();
    if (slot.pathFrom == 0xFFFF || first.pathFrom == 0xFFFF || slot.pathFrom != slot.checkpoint || first.pathFrom != first.checkpoint) {
        slot.pathFrom = slot.checkpoint;
        first.pathFrom = first.checkpoint;
        slot.path.clear();
        if (!network->FindPath(first.checkpoint, slot.checkpoint, &slot.path, 0x200))
            return;
    }
    // As far along the way from the first slot as the shape says.
    float remaining = slot.distance;
    const int count = static_cast<int>(slot.path.size());
    if (count >= 2) {
        remaining -= LengthOfSq(Sq(Sub(Xyz(first.target), network->Get(slot.path[1])->pos)));
        if (remaining < 0.0f)
            return;
    }
    if (!(0.0f <= remaining))
        return;
    for (int i = 2; i < count - 1; ++i) {
        const S_vector point = network->Get(slot.path[i])->pos;
        const float left = remaining - LengthOfSq(Sq(Sub(point, network->Get(slot.path[i - 1])->pos)));
        if (left < 0.0f) {
            const S_vector toward = i == count - 2 ? Sub(Xyz(slot.target), point) : Sub(network->Get(slot.path[i + 1])->pos, point);
            const S_vector dir = Unit(toward);
            slot.target.x = point.x + dir.x * remaining;
            slot.target.y = point.y + dir.y * remaining;
            slot.target.z = point.z + dir.z * remaining;
            slot.dirty = true;
            return;
        }
        remaining = left;
    }
}

void C_FormationShape::BoardVehicle(C_Actor *vehicle)
{
    if (!vehicle) {
        for (int i = 0; i < SLOT_COUNT; ++i)
            m_slots[i].vehicleId = -1;
        C_Actor *const own = m_leader->GetVehicle();
        if (own->GetType() == ACTOR_CAR) {
            BoardVehicle(own);
        } else if (own->GetType() == ACTOR_SUBMARINE) {
            BoardVehicle(own);
            const std::vector<C_Actor *> &submarines = GetSubmarines();
            for (size_t i = submarines.size(); i-- != 0;) {
                if (submarines[i] != own)
                    BoardVehicle(submarines[i]);
            }
        }
        return;
    }
    const bool isCar = vehicle->GetType() == ACTOR_CAR;
    const auto seatPlace = [&](int seat, S_vector4 *pos, S_vector4 *dir) {
        if (isCar)
            return static_cast<C_Car *>(vehicle)->GetSeatEntry(seat, pos, dir);
        if (vehicle->GetType() == ACTOR_SUBMARINE)
            return static_cast<C_Submarine *>(vehicle)->FindExit(seat, pos, dir, false) && static_cast<C_Submarine *>(vehicle)->FindExit(seat, pos, dir, true);
        return false;
    };
    std::vector<S_vector4> places, dirs;
    for (int seat = vehicle->GetSeatCount() - 1; seat >= 0; --seat) {
        S_vector4 pos = {}, dir = {};
        if (seatPlace(seat, &pos, &dir)) {
            places.push_back(pos);
            dirs.push_back(dir);
        }
    }
    if (places.empty())
        return;
    int seat = 0;
    const int seats = vehicle->GetSeatCount();
    for (int i = 0; i < SLOT_COUNT; ++i) {
        S_FormationSlot &slot = m_slots[i];
        if (slot.vehicleId != -1 || !slot.member || slot.member == m_leader)
            continue;
        while (seat < seats && vehicle->GetSeatOccupant(seat))
            ++seat;
        if (seat == seats) {
            // Full: the rest wait at the last free seat's (or the last seat's) place.
            int chosen = seats - 1;
            for (int last = vehicle->GetSeatCount(); last > 0; --last) {
                if (!vehicle->GetSeatOccupant(last - 1)) {
                    chosen = last - 1;
                    break;
                }
            }
            S_vector4 pos = {}, dir = {};
            if (isCar) {
                if (!static_cast<C_Car *>(vehicle)->GetSeatEntry(chosen, &pos, &dir))
                    return;
            } else if (vehicle->GetType() == ACTOR_SUBMARINE && seatPlace(chosen, &pos, &dir)) {
                return;                                 // (kept: a submarine with a place gives up here)
            }
            for (int j = i; j < SLOT_COUNT; ++j) {
                m_slots[j].target = pos;
                m_slots[j].dir = dir;
                m_slots[j].current = pos;
                m_slots[j].dirty = true;
                FitOnNetwork(j);
            }
            return;
        }
        if (IsVehicleType(slot.member->GetVehicle()))
            continue;
        size_t nearest = 0;
        float best = 3.4028235e38f;
        const S_vector at = slot.member->GetPosition();
        for (size_t k = places.size(); k-- != 0;) {
            const float sq = Sq(Sub(Xyz(places[k]), at));
            if (sq < best) {
                best = sq;
                nearest = k;
            }
        }
        slot.target = places[nearest];
        slot.dir = dirs[nearest];
        slot.current = slot.target;
        slot.seat = seat;
        slot.vehicleId = vehicle->GetNetId();
        slot.dirty = true;
        FitOnNetwork(i);
        ++seat;
    }
}

void C_FormationShape::LineUpAtDoor()
{
    C_Checkpoints *const network = GetGame()->GetCheckpoints();
    const S_Checkpoint *const point = network->Get(m_doorPoint);
    const S_Checkpoint *door = nullptr;
    for (int i = (point->flags & CHECKPOINT_LINK_COUNT) - 1; i >= 0; --i) {
        const S_Checkpoint *const other = network->Get(point->links[i].target);
        if (other->type == 1 || other->type == 2) {
            door = other;
            break;
        }
    }
    if (!door)                                          // (fix: the original reads through it)
        return;
    const S_vector a = point->pos, b = door->pos;
    const S_vector d = Unit(Sub(b, a));
    const S_vector side(d.y * 0.0f - d.z, d.z * 0.0f - d.x * 0.0f, d.x - d.y * 0.0f);
    for (int i = SLOT_COUNT - 1; i >= 1; --i) {
        S_FormationSlot &slot = m_slots[i];
        if (!slot.member || slot.member == m_leader)
            continue;
        if (i == 1) {
            const S_vector base = m_doorState < 0 ? a : b;
            SetXyz(slot.target, S_vector(side.x + base.x, side.y + base.y, side.z + base.z));
            SetXyz(slot.dir, Unit(m_doorState < 0 ? S_vector(-d.x, -d.y, -d.z) : d));
        } else if (i == 2) {
            const S_vector base = m_doorState < 1 ? a : b;
            SetXyz(slot.target, S_vector(base.x - side.x, base.y - side.y, base.z - side.z));
            SetXyz(slot.dir, Unit(m_doorState < 1 ? S_vector(-d.x, -d.y, -d.z) : d));
        } else if (i == 3) {
            if (m_doorState < 2)
                SetXyz(slot.target, S_vector((a.x - d.x) + side.x, (a.y - d.y) + side.y, (a.z - d.z) + side.z));
            else
                SetXyz(slot.target, S_vector(d.x + b.x, d.y + b.y, d.z + b.z));
            SetXyz(slot.dir, Unit(d));
        }
        slot.dirty = true;
    }
    KeepApart();
    if (m_doorState != -1) {
        // When the next is in its place, the one after goes through.
        for (int i = SLOT_COUNT - 1; i >= 0; --i) {
            S_FormationSlot &slot = m_slots[i];
            if (slot.member && slot.member != m_leader && m_doorState == i - 1) {
                const S_vector velocity = Xyz(slot.member->m_velocity);
                const S_vector off = Sub(slot.member->GetPosition(), Xyz(slot.target));
                if (Sq(velocity) < 0.01f && Sq(off) < 0.5f)
                    ++m_doorState;
                return;
            }
        }
        return;
    }
    m_doorState = 0;
    for (int i = SLOT_COUNT - 1; i >= 0; --i) {
        S_FormationSlot &slot = m_slots[i];
        if (!slot.member || slot.member == m_leader)
            continue;
        const S_vector velocity = Xyz(slot.member->m_velocity);
        const S_vector off = Sub(slot.member->GetPosition(), Xyz(slot.target));
        if (0.01f < Sq(velocity) || 0.5f <= Sq(off)) {
            m_doorState = -1;
            return;
        }
    }
}

void C_FormationShape::MoveSlots(int timeDelta)
{
    for (int i = 0; i < SLOT_COUNT; ++i) {
        S_FormationSlot &slot = m_slots[i];
        const S_vector off(slot.target.x - slot.current.x, 0.0f, slot.target.z - slot.current.z);
        const float sq = off.x * off.x + off.z * off.z;
        const float length = LengthOfSq(sq);
        const S_vector dir = Unit(off);
        const float step = static_cast<float>(timeDelta) * 0.004f;
        float move = length <= step ? length : step;
        if (5.0f < length)
            move = length - 5.0f;
        slot.current.x += dir.x * move;
        slot.current.y += move * dir.y;
        slot.current.z += dir.z * move;
    }
}

void C_FormationShape::FollowLeader()
{
    const S_vector leaderPos = m_leader->GetFrame()->GetWorldPos();
    S_vector dir = Xyz(m_leader->m_velocity);
    if (LengthOfSq(dir.x * dir.x + dir.z * dir.z) == 0.0f) {
        if (!m_changed)
            return;
        dir = Xyz(m_lastDir);
        dir.y = 0.0f;
        if (LengthOfSq(dir.x * dir.x + dir.z * dir.z) == 0.0f) {
            const S_vector view = m_leader->GetViewDir();
            dir.x = view.x;
            dir.z = view.z;
        }
    }
    dir.y = 0.0f;
    dir = Unit(dir);
    S_matrix placed = m_leader->GetFrame()->GetMatrix();
    placed.SetDir(dir);
    m_lastDir = {dir.x, dir.y, dir.z, 0.0f};
    placed.m[3][0] = leaderPos.x;
    placed.m[3][1] = leaderPos.y;
    placed.m[3][2] = leaderPos.z;

    C_Checkpoints *const network = GetGame()->GetCheckpoints();
    for (int i = SLOT_COUNT - 1; i >= 0; --i) {
        S_FormationSlot &slot = m_slots[i];
        if (!slot.member || !slot.member->IsAlive())
            continue;
        S_vector place = Xyz(slot.scaledPos) * placed;
        SetXyz(slot.dir, Unit(Sub(Xyz(slot.scaledDir) * placed, leaderPos)));
        if (!slot.member->IsLocalPlayer()) {
            // Round a corner: no farther along the walking points than straight away from the leader.
            const float straight = LengthOfSq(Sq(Sub(place, leaderPos)));
            const int from = network->FindNearest(leaderPos, 5.0f, -1.0f);
            const int to = network->FindNearest(place, 5.0f, -1.0f);
            std::vector<uint16_t> path;
            if (from != CHECKPOINT_NONE && to != CHECKPOINT_NONE && network->FindPath(from, to, &path, 0)) {
                if (!path.empty() && path[0] == from)
                    path.erase(path.begin());
                S_vector previous = leaderPos;
                float along = 0.0f;
                for (size_t k = 0; k < path.size(); ++k) {
                    const S_Checkpoint *const point = network->Get(path[k]);
                    if (!point)
                        break;
                    along += LengthOfSq(Sq(Sub(previous, point->pos)));
                    const float radius = static_cast<float>(point->radius) * 0.0625f;
                    if (straight <= radius + along) {
                        if (radius < LengthOfSq(Sq(Sub(point->pos, place)))) {
                            const S_vector back = Unit(Sub(leaderPos, point->pos));
                            const float by = along - radius <= straight ? along - straight : radius;
                            place = S_vector(back.x * by + point->pos.x, back.y * by + point->pos.y, back.z * by + point->pos.z);
                        }
                        break;
                    }
                    previous = point->pos;
                }
            }
        }
        const S_vector was = Xyz(slot.target);
        const float wasFar = LengthOfSq(Sq(Sub(was, leaderPos)));
        slot.pushedAside = false;
        if (wasFar < SLOT_APART && slot.member) {
            const S_vector ahead(dir.x * 0.001f + leaderPos.x, dir.y * 0.001f + leaderPos.y, dir.z * 0.001f + leaderPos.z);
            if (LengthOfSq(Sq(Sub(was, ahead))) < wasFar)
                PushAside(i, SLOT_APART);
        }
        if (m_changed) {
            SetXyz(slot.target, place);
            slot.dirty = true;
        } else {
            const S_vector moved(dir.x * 0.01f + (was.x - place.x), dir.y * 0.01f + (was.y - place.y), dir.z * 0.01f + (was.z - place.z));
            if (std::sqrt(Sq(moved)) < wasFar) {
                SetXyz(slot.target, place);
                slot.dirty = true;
            }
        }
        if (Sq(Xyz(m_leader->m_velocity)) != 0.0f && !m_scripted)
            PlanSlot(i);
    }
    KeepApart();
    m_changed = false;
}

void C_FormationShape::Tick(int timeDelta)
{
    if (!m_leader || !m_slots)
        return;
    m_mode = m_doorPoint == 0xFFFF ? 1 : 2;
    MoveSlots(timeDelta);
    if (m_mode == 1) {
        const C_Actor *const vehicle = m_leader->GetVehicle();
        if (IsVehicleType(vehicle) && (vehicle->GetType() == ACTOR_CAR || vehicle->GetType() == ACTOR_SUBMARINE)) {
            BoardVehicle(nullptr);
            return;
        }
        if (m_leader->GetBodyClimbState() == 0)
            FollowLeader();
    } else if (m_mode == 2) {
        LineUpAtDoor();
    }
}

bool C_FormationShape::SaveGame(C_chunk &chunk)
{
    chunk.Begin(CK_SHAPE);
    chunk.WriteChunk(CK_MATRIX, &m_matrix, 0x40);
    chunk.WriteChunk(CK_DIR, &m_dir, 12);
    chunk.WriteChunk(CK_SCALE, &m_scale, 12);
    chunk.WriteChunk(CK_CHANGED, &m_changed, 1);
    chunk.WriteChunk(CK_SCRIPTED, &m_scripted, 1);
    chunk.WriteChunk(CK_DOOR_POINT, &m_doorPoint, 2);
    chunk.WriteChunk(CK_DOOR_STATE, &m_doorState, 4);
    chunk.WriteChunk(CK_COUNT, &m_memberCount, 4);
    if (m_leader) {
        const int id = m_leader->GetNetId();
        chunk.WriteChunk(CK_LEADER, &id, 4);
    }
    chunk.WriteChunk(CK_TYPE, &m_type, 4);
    chunk.WriteChunk(CK_MODE, &m_mode, 4);
    if (m_slots) {
        chunk.Begin(CK_SLOTS);
        for (int i = 0; i < SLOT_COUNT; ++i)
            WriteSlot(chunk, m_slots[i]);
        chunk.Ascend();
    }
    chunk.Ascend();
    return true;
}

bool C_FormationShape::LoadGame(C_chunk &chunk)
{
    m_leader = nullptr;
    uint16_t id;
    if (!chunk.Descend(id))
        return true;
    while (chunk.HasMore()) {
        if (!chunk.Descend(id))
            break;
        switch (id) {
        case CK_MATRIX: chunk.Read(&m_matrix, 0x40); break;
        case CK_DIR: chunk.Read(&m_dir, 12); break;
        case CK_SCALE: chunk.Read(&m_scale, 12); break;
        case CK_CHANGED: chunk.Read(&m_changed, 1); break;
        case CK_SCRIPTED: chunk.Read(&m_scripted, 1); break;
        case CK_DOOR_POINT: chunk.Read(&m_doorPoint, 2); break;
        case CK_DOOR_STATE: chunk.Read(&m_doorState, 4); break;
        case CK_COUNT: chunk.Read(&m_memberCount, 4); break;
        case CK_LEADER: {
            int actor = 0;
            chunk.Read(&actor, 4);
            m_leader = static_cast<C_Human *>(g_actorsModule.GetActorById(actor));
            break;
        }
        case CK_TYPE: chunk.Read(&m_type, 4); break;
        case CK_MODE: chunk.Read(&m_mode, 4); break;
        case CK_SLOTS:
            if (!m_slots)                               // (fix: a formation of no slots loading some)
                m_slots = new S_FormationSlot[SLOT_COUNT];
            for (int i = 0; i < SLOT_COUNT; ++i)
                ReadSlot(chunk, m_slots[i], m_leader);
            break;
        default:
            break;
        }
        chunk.Ascend();
    }
    chunk.Ascend();
    return true;
}
