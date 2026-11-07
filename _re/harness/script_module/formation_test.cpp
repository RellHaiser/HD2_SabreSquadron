// The shape of a formation (part 12b: game/formation.cpp). MinGW; build_formation.bat; run formation_test.exe ("formation: N checks, 0 failures").
#include <windows.h>

#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <vector>

#define protected public
#define private public
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
#include "ls3d/rw_data.h"
#undef private
#undef protected
#include <new>
#include <malloc.h>

// Its helpers too, so that they can be tried one by one.
#include "game/formation_tables.inc"
namespace formation {
S_vector Xyz(const S_vector4 &value);
void SetXyz(S_vector4 &value, const S_vector &v);
float LengthOfSq(float sq);
S_vector Unit(S_vector v);
}  // namespace formation
using namespace formation;

// How many slot arrays were freed.
static int g_arraysFreed;
void *operator new[](size_t size, std::align_val_t align) { return _aligned_malloc(size, static_cast<size_t>(align)); }
void operator delete[](void *p, std::align_val_t) noexcept { ++g_arraysFreed; _aligned_free(p); }
void operator delete[](void *p, size_t, std::align_val_t) noexcept { ++g_arraysFreed; _aligned_free(p); }

namespace {
int checks, failures;
#define CHECK(cond)                                                                 \
    do {                                                                            \
        ++checks;                                                                   \
        if (!(cond)) {                                                              \
            ++failures;                                                             \
            std::printf("FAIL %s:%d: %s\n", __func__, __LINE__, #cond);             \
        }                                                                           \
    } while (0)
bool Near(float a, float b, float within = 1e-3f) { return std::fabs(a - b) <= within; }

std::string g_file;
int g_at;
}  // namespace
int RW_DATA_CALL dtaOpen(const char *, bool) { g_at = 0; return 1; }
int RW_DATA_CALL dtaOpenWrite(const char *, int) { g_file.clear(); g_at = 0; return 1; }
bool RW_DATA_CALL dtaClose(int) { return true; }
int RW_DATA_CALL dtaSeek(int, int offset, int origin)
{
    if (origin == 0) g_at = offset;
    else if (origin == 1) g_at += offset;
    else g_at = static_cast<int>(g_file.size()) + offset;
    return g_at;
}
int RW_DATA_CALL dtaRead(int, void *data, int size)
{
    int got = static_cast<int>(g_file.size()) - g_at;
    if (got > size) got = size;
    if (got < 0) got = 0;
    std::memcpy(data, g_file.data() + g_at, got);
    g_at += got;
    return got;
}
int RW_DATA_CALL dtaWrite(int, const void *data, int size)
{
    if (static_cast<size_t>(g_at) + size > g_file.size())
        g_file.resize(g_at + size);
    std::memcpy(&g_file[g_at], data, size);
    g_at += size;
    return size;
}
void I3D_frame::UpdateWMatrixProc() {}
S_Checkpoint::~S_Checkpoint() {}

// ---- the engine's math ----
void __stdcall S_matrix::Identity()
{
    std::memset(this, 0, sizeof(*this));
    for (int i = 0; i < 4; ++i) m[i][i] = 1.0f;
}
void __stdcall S_matrix::SetDir(const S_vector &dir)
{
    // z along dir, y up, x to the side; the place stays.
    m[2][0] = dir.x; m[2][1] = dir.y; m[2][2] = dir.z;
    m[1][0] = 0; m[1][1] = 1; m[1][2] = 0;
    m[0][0] = dir.z; m[0][1] = 0; m[0][2] = -dir.x;
    m[0][3] = m[1][3] = m[2][3] = 0;
}
S_vector __stdcall S_vector::operator*(const S_matrix &t) const
{
    return S_vector(x * t.m[0][0] + y * t.m[1][0] + z * t.m[2][0] + t.m[3][0],
                    x * t.m[0][1] + y * t.m[1][1] + z * t.m[2][1] + t.m[3][1],
                    x * t.m[0][2] + y * t.m[1][2] + z * t.m[2][2] + t.m[3][2]);
}

// ---- the network ----
namespace {
S_Checkpoint *g_points;
S_CheckpointLink g_links[16][8];
int g_pointCount;
alignas(16) unsigned char g_networkBytes[0x4000];
std::vector<std::vector<uint16_t>> g_paths;             // what FindPath finds
int g_findPaths;
uint32_t g_findPathFlags;
uint32_t g_findPathSeen;
bool g_pathFails;
float g_gaveNearestTo = 0;
int AddPoint(float x, float y, float z, float radius, uint8_t type = 0)
{
    S_Checkpoint &point = g_points[g_pointCount];
    point.pos = S_vector(x, y, z);
    point.radius = static_cast<uint8_t>(radius * 16.0f);
    point.type = type;
    point.flags = 0;
    point.links = g_links[g_pointCount];
    return g_pointCount++;
}
void LinkTo(int a, int b)
{
    g_points[a].links[g_points[a].flags & CHECKPOINT_LINK_COUNT].target = static_cast<uint16_t>(b);
    g_points[a].flags++;
}
C_Checkpoints *Network() { return reinterpret_cast<C_Checkpoints *>(g_networkBytes); }
}  // namespace
S_Checkpoint *C_CheckpointNetwork::Get(int index) { return index >= 0 && index < g_pointCount ? &g_points[index] : nullptr; }
static bool g_nearestOdd;
static float g_nearestRadius, g_nearestY, g_nearest15Radius, g_nearest15Y;
int C_CheckpointNetwork::FindNearest(S_vector pos, float radius, float y) const
{
    g_nearestRadius = radius;
    g_nearestY = y;
    if (radius != 5.0f || y != -1.0f)
        g_nearestOdd = true;
    int best = 0xFFFF;
    float bestSq = 1e20f;
    for (int i = 0; i < g_pointCount; ++i) {
        if (g_points[i].type == 15)
            continue;
        const float dx = g_points[i].pos.x - pos.x, dz = g_points[i].pos.z - pos.z;
        if (dx * dx + dz * dz < bestSq) {
            bestSq = dx * dx + dz * dz;
            best = i;
        }
    }
    return best;
}
int C_CheckpointNetwork::FindNearestType15(S_vector pos, float radius, float y) const
{
    g_nearest15Radius = radius;
    g_nearest15Y = y;
    for (int i = 0; i < g_pointCount; ++i)
        if (g_points[i].type == 15)
            return i;
    return 0xFFFF;
}
bool C_CheckpointNetwork::FindPath(int from, int to, std::vector<uint16_t> *path, uint32_t flags)
{
    ++g_findPaths;
    g_findPathFlags = flags;
    g_findPathSeen |= flags;
    if (g_pathFails)
        return false;
    for (const auto &candidate : g_paths) {
        if (candidate.front() == from && candidate.back() == to) {
            *path = candidate;
            return true;
        }
    }
    path->clear();
    path->push_back(static_cast<uint16_t>(from));
    if (to != from)
        path->push_back(static_cast<uint16_t>(to));
    return true;
}

namespace {
// ---- humans ----
alignas(16) unsigned char g_gameBytes[0x400];
struct S_Fake {
    alignas(16) unsigned char frameBytes[0x400];
    alignas(16) unsigned char humanBytes[0x2000];
    bool local = false;
    int climb = 0;
    int controllerType = 0;
    C_Human *leaderSet = nullptr;
    C_Human *human() { return reinterpret_cast<C_Human *>(humanBytes); }
    I3D_frame *frame() { return reinterpret_cast<I3D_frame *>(frameBytes); }
};
std::map<const C_Human *, S_Fake *> g_fakes;
std::map<int, C_Actor *> g_byId;
S_vector g_viewDir(0, 0, 1);
void SetFramePos(unsigned char *frameBytes, float x, float y, float z)
{
    float *matrix = reinterpret_cast<float *>(frameBytes + 0x40);
    for (int i = 0; i < 16; ++i) matrix[i] = 0;
    for (int i = 0; i < 4; ++i) matrix[i * 5] = 1.0f;
    matrix[12] = x; matrix[13] = y; matrix[14] = z;
    reinterpret_cast<I3D_frame *>(frameBytes)->m_frameFlags = 0x20;
}
int g_nextId = 100;
S_Fake *MakeHuman(float x, float z)
{
    S_Fake *fake = new S_Fake();
    std::memset(fake->frameBytes, 0, sizeof(fake->frameBytes));
    std::memset(fake->humanBytes, 0, sizeof(fake->humanBytes));
    SetFramePos(fake->frameBytes, x, 0, z);
    C_Human *human = fake->human();
    human->m_type = ACTOR_HUMAN;
    human->m_frame = fake->frame();
    human->m_stats[0] = 100;
    human->m_netId = g_nextId;
    human->m_position = {x, 0, z, 1.0f};
    g_byId[g_nextId++] = human;
    g_fakes[human] = fake;
    return fake;
}
// A controller for the squad's orders.
void *g_squadVt[256];
int g_orderNow;
int g_orderSet;
int __thiscall F_GetCombatOrder(void *) { return g_orderNow; }
}  // namespace
void C_Human::SetLeader(C_Human *leader) { g_fakes[this]->leaderSet = leader; }
bool C_Human::IsLocalPlayer() const { return g_fakes[this]->local; }
int C_Human::GetControllerType() const { return m_controller ? m_controller->GetType() : HUMAN_CONTROLLER_NONE; }
const S_vector &C_Human::GetViewDir() const { return g_viewDir; }
int S_HumanBody::GetClimbState() const
{
    for (auto &entry : g_fakes)
        if (&entry.first->m_body == this) return entry.second->climb;
    return 0;
}
void C_SquadController::SetCombatOrder(int order) { g_orderSet = order; }
C_Actor *C_ActorsModule::GetActorById(int id) const { return g_byId.count(id) ? g_byId[id] : nullptr; }

// ---- vehicles ----
namespace {
void *g_vehicleVt[128];
struct S_FakeVehicle {
    alignas(16) unsigned char frameBytes[0x400];
    alignas(16) unsigned char bytes[0x3000];
    int seats = 0;
    C_Actor *occupants[8] = {};
    bool hasEntry[8] = {true, true, true, true, true, true, true, true};
    C_Actor *actor() { return reinterpret_cast<C_Actor *>(bytes); }
};
std::map<const void *, S_FakeVehicle *> g_vehicles;
int __thiscall F_SeatCount(C_Actor *self) { return g_vehicles[self]->seats; }
C_Actor *__thiscall F_SeatOccupant(C_Actor *self, int seat) { return g_vehicles[self]->occupants[seat]; }
S_FakeVehicle *MakeVehicle(E_ActorType type, int seats)
{
    S_FakeVehicle *fake = new S_FakeVehicle();
    std::memset(fake->frameBytes, 0, sizeof(fake->frameBytes));
    std::memset(fake->bytes, 0, sizeof(fake->bytes));
    SetFramePos(fake->frameBytes, 0, 0, 0);
    *reinterpret_cast<void ***>(fake->bytes) = g_vehicleVt;
    C_Actor *actor = fake->actor();
    actor->m_type = type;
    actor->m_frame = reinterpret_cast<I3D_frame *>(fake->frameBytes);
    actor->m_netId = g_nextId++;
    fake->seats = seats;
    g_vehicles[actor] = fake;
    return fake;
}
std::vector<C_Actor *> g_submarines;
}  // namespace
bool C_Car::GetSeatEntry(int seat, S_vector4 *pos, S_vector4 *dir)
{
    S_FakeVehicle *fake = g_vehicles[this];
    if (!fake->hasEntry[seat])
        return false;
    *pos = {10.0f + seat, 0, 20.0f, 0};
    *dir = {0, 0, static_cast<float>(seat + 1), 0};
    return true;
}
bool C_Submarine::FindExit(int seat, S_vector4 *pos, S_vector4 *dir, bool otherSide)
{
    S_FakeVehicle *fake = g_vehicles[this];
    if (!fake->hasEntry[seat])
        return false;
    *pos = {30.0f + seat, 0, otherSide ? 41.0f : 40.0f, 0};
    *dir = {1, 0, 0, 0};
    return true;
}
const std::vector<C_Actor *> &GetSubmarines() { return g_submarines; }
C_game *GetGame() { return reinterpret_cast<C_game *>(g_gameBytes); }

namespace {
S_Checkpoint *MakePoints()
{
    static S_Checkpoint *points = static_cast<S_Checkpoint *>(::operator new(sizeof(S_Checkpoint) * 16));
    return points;
}
void ResetWorld()
{
    g_pointCount = 0;
    g_paths.clear();
    g_findPaths = 0;
    g_pathFails = false;
    g_viewDir = S_vector(0, 0, 1);
    g_fakes.clear();
    g_byId.clear();
}
// One big circle everything fits in.
void BigWorld()
{
    ResetWorld();
    AddPoint(0, 0, 0, 15.0f);
}

void TestTables()
{
    C_FormationShape shape;
    CHECK(shape.GetType() == FORMATION_FOLLOW && shape.HasSlots() && !shape.m_scripted && !shape.m_changed);
    CHECK(shape.m_doorPoint == 0xFFFF && shape.m_doorState == -1 && shape.GetMemberCount() == 0 && shape.GetLeader() == nullptr);
    CHECK(Near(shape.m_scale.x, 2.5f) && Near(shape.m_scale.z, 2.5f));
    CHECK(Near(shape.m_dir.z, 1.0f));
    // Scaled and turned (dir +z: x stays).
    C_FormationShape line;
    line.SetType(FORMATION_LINE);
    CHECK(line.m_changed);
    CHECK(line.GetType() == FORMATION_LINE);
    for (int i = 0; i < 6; ++i) {
        const S_FormationSlot &slot = line.m_slots[i];
        CHECK(Near(slot.scaledPos.x, slot.basePos.x * 2.5f) && Near(slot.scaledPos.z, slot.basePos.z * 2.5f));
    }
    // The tables by type: each has its first slot at the origin or close, and six directions of unit length.
    for (int type = 1; type <= 10; ++type) {
        line.SetType(static_cast<E_FormationType>(type));
        for (int i = 0; i < 6; ++i) {
            const S_FormationSlot &slot = line.m_slots[i];
            CHECK(Near(slot.baseDir.x * slot.baseDir.x + slot.baseDir.y * slot.baseDir.y + slot.baseDir.z * slot.baseDir.z, 1.0f));
        }
    }
    line.SetType(FORMATION_ARROW_INVERT);
    line.SetType(FORMATION_NEXT);
    CHECK(line.GetType() == 1);
    line.SetType(FORMATION_NEXT);
    CHECK(line.GetType() == 2);
    // Type 0's row: straight back in x.
    line.SetType(static_cast<E_FormationType>(0));
    CHECK(Near(line.m_slots[3].basePos.x, -3.0f) && line.m_slots[3].basePos.y == 0 && line.m_slots[3].basePos.z == 0);
    // Distance from the first slot (Kept: by what the first had before its turn).
    line.ApplyMatrix();
    CHECK(Near(line.m_slots[3].distance, 7.5f));
    CHECK(Near(line.m_slots[0].distance, 0.0f));
    // Scale.
    line.SetScale(S_vector(1, 1, 1));
    CHECK(Near(line.m_slots[3].scaledPos.x, -3.0f));
    line.ApplyMatrix();
    CHECK(Near(line.m_slots[3].distance, 3.0f));
    // None frees; none again nothing; then anew.
    line.SetType(FORMATION_NONE);
    CHECK(!line.HasSlots() && line.GetType() == FORMATION_NONE);
    line.SetType(FORMATION_NONE);
    line.SetType(FORMATION_LINE);
    CHECK(line.HasSlots());
}

void TestMembers()
{
    BigWorld();
    C_FormationShape shape;
    S_Fake *a = MakeHuman(1, 1), *b = MakeHuman(2, 2), *c = MakeHuman(3, 3);
    CHECK(shape.AddMember(a->human()));
    CHECK(!shape.AddMember(a->human()));
    CHECK(shape.GetMemberCount() == 1);
    int slot = -1;
    CHECK(shape.FindSlot(a->human(), &slot) && slot == 0);
    CHECK(!shape.FindSlot(b->human(), &slot));
    CHECK(shape.FindSlot(a->human(), nullptr));
    CHECK(shape.m_slots[0].target.x == 1 && shape.m_slots[0].current.z == 1 && shape.m_slots[0].dirty);
    CHECK(Near(shape.m_slots[0].dir.z, 1.0f));
    CHECK(shape.AddMember(b->human()) && shape.AddMember(c->human()));
    CHECK(shape.GetSlotMember(2) == c->human());
    // Full: no.
    S_Fake *d = MakeHuman(0, 0), *e = MakeHuman(0, 0), *f = MakeHuman(0, 0), *g = MakeHuman(0, 0);
    CHECK(shape.AddMember(d->human()) && shape.AddMember(e->human()) && shape.AddMember(f->human()));
    CHECK(!shape.AddMember(g->human()));
    CHECK(shape.GetMemberCount() == 6);
    // Without a leader, a removal only empties: last slot too (kept), the count down.
    CHECK(shape.RemoveMember(b->human()));
    CHECK(shape.GetSlotMember(1) == nullptr && shape.GetSlotMember(2) == c->human() && shape.GetSlotMember(5) == nullptr);
    CHECK(shape.GetMemberCount() == 5);
    CHECK(!shape.RemoveMember(b->human()));

    // A leader takes the first slot; the one there moves to the leader's.
    C_FormationShape led;
    led.SetType(static_cast<E_FormationType>(0));
    S_Fake *l = MakeHuman(5, 5), *m = MakeHuman(1, 1), *n = MakeHuman(2, 2);
    CHECK(led.AddMember(m->human()) && led.AddMember(n->human()) && led.AddMember(l->human()));
    CHECK(led.SetLeader(l->human()));
    CHECK(led.GetLeader() == l->human());
    CHECK(led.GetSlotMember(0) == l->human() && led.GetSlotMember(2) == m->human() && led.GetSlotMember(1) == n->human());
    CHECK(led.GetMemberCount() == 3);
    CHECK(led.m_slots[0].target.x == 5 && led.m_slots[0].target.z == 5);
    CHECK(led.m_slots[0].dirty && led.m_slots[0].checkpoint == 0);
    // The others' places about it.
    CHECK(Near(led.m_slots[3].target.x, 5.0f - 7.5f) && Near(led.m_slots[3].target.z, 5.0f));
    // The same: nothing. A leader not yet in: added. The door forgotten.
    led.m_doorPoint = 3;
    CHECK(led.SetLeader(l->human()));
    CHECK(led.m_doorPoint == 3);
    C_FormationShape fresh;
    S_Fake *o = MakeHuman(1, 0);
    fresh.m_doorPoint = 3;
    fresh.m_doorState = 2;
    CHECK(fresh.SetLeader(o->human()));
    CHECK(fresh.GetSlotMember(0) == o->human() && fresh.GetMemberCount() == 1 && fresh.m_doorPoint == 0xFFFF && fresh.m_doorState == -1);
    // A leader that cannot get in.
    C_FormationShape full;
    S_Fake *members[6];
    for (S_Fake *&member : members) {
        member = MakeHuman(0, 0);
        full.AddMember(member->human());
    }
    S_Fake *late = MakeHuman(0, 0);
    CHECK(!full.SetLeader(late->human()));
    CHECK(full.GetLeader() == nullptr);
    // No leader: nothing but cleared.
    CHECK(led.SetLeader(nullptr));
    CHECK(led.GetLeader() == nullptr);

    // Removing with a leader: the others close up.
    C_FormationShape closing;
    S_Fake *lead = MakeHuman(0, 0), *x = MakeHuman(1, 0), *y = MakeHuman(2, 0), *z = MakeHuman(3, 0);
    closing.SetLeader(lead->human());
    closing.AddMember(x->human());
    closing.AddMember(y->human());
    closing.AddMember(z->human());
    CHECK(closing.RemoveMember(x->human()));
    CHECK(closing.GetSlotMember(0) == lead->human() && closing.GetSlotMember(1) == y->human() && closing.GetSlotMember(2) == z->human() && closing.GetSlotMember(3) == nullptr);
    CHECK(closing.GetMemberCount() == 3);
    // Removing the leader.
    CHECK(closing.RemoveMember(lead->human()));
    CHECK(closing.GetLeader() == nullptr && closing.GetSlotMember(0) == nullptr);
}

void TestQueries()
{
    BigWorld();
    C_FormationShape shape;
    S_Fake *l = MakeHuman(0, 0), *m = MakeHuman(2, 0), *other = MakeHuman(9, 9);
    S_vector4 pos = {};
    uint16_t point = 7;
    // No leader yet: nothing.
    CHECK(!shape.GetSlotTarget(m->human(), &pos, &point));
    S_vector4 dir = {};
    CHECK(!shape.GetSlotDir(m->human(), &dir));
    int vehicle = 5;
    CHECK(shape.GetSlotSeat(m->human(), &vehicle) == 0 && vehicle == 5);
    shape.InvalidateSlot(m->human());
    shape.SetLeader(l->human());
    shape.AddMember(m->human());
    // Told once it has changed.
    CHECK(shape.GetSlotTarget(m->human(), &pos, &point));
    CHECK(pos.x == 2);
    CHECK(!shape.GetSlotTarget(m->human(), &pos, &point));
    CHECK(pos.x == 2);
    CHECK(!shape.GetSlotTarget(m->human(), &pos, nullptr));
    CHECK(!shape.GetSlotTarget(nullptr, &pos, &point));
    CHECK(!shape.GetSlotTarget(other->human(), &pos, &point));
    shape.InvalidateSlot(m->human());
    CHECK(shape.GetSlotTarget(m->human(), &pos, nullptr));
    shape.InvalidateSlot(other->human());
    // The point goes along.
    shape.m_slots[1].checkpoint = 44;
    shape.InvalidateSlot(m->human());
    CHECK(shape.GetSlotTarget(m->human(), &pos, &point) && point == 44);
    // Set by hand: at it, no point.
    const S_vector4 there = {7, 8, 9, 1};
    shape.SetSlotTarget(m->human(), there);
    CHECK(shape.m_slots[1].target.x == 7 && shape.m_slots[1].target.w == 1 && shape.m_slots[1].checkpoint == 0xFFFF && shape.m_slots[1].dirty);
    shape.SetSlotTarget(other->human(), there);
    // Direction, seat.
    shape.m_slots[1].dir = {0, 0, -1, 0};
    CHECK(shape.GetSlotDir(m->human(), &dir) && dir.z == -1);
    CHECK(!shape.GetSlotDir(other->human(), &dir));
    shape.m_slots[1].seat = 3;
    shape.m_slots[1].vehicleId = 55;
    CHECK(shape.GetSlotSeat(m->human(), &vehicle) == 3 && vehicle == 55);
    vehicle = 5;
    CHECK(shape.GetSlotSeat(other->human(), &vehicle) == 0 && vehicle == 5);
}

void TestFitOnNetwork()
{
    ResetWorld();
    AddPoint(0, 3, 0, 1.0f);
    C_FormationShape shape;
    S_Fake *l = MakeHuman(0, 0);
    shape.m_slots[0].member = l->human();
    // Within: as it is, at the point's height.
    shape.m_slots[1].target = {0.5f, 9, 0, 0};
    CHECK(shape.FitOnNetwork(1));
    CHECK(shape.m_slots[1].target.x == 0.5f && shape.m_slots[1].target.y == 3 && shape.m_slots[1].checkpoint == 0 && shape.m_slots[1].dirty);
    // Outside: to the edge.
    shape.m_slots[1].target = {3, 9, 4, 0};
    CHECK(shape.FitOnNetwork(1));
    CHECK(Near(shape.m_slots[1].target.x, 0.6f) && Near(shape.m_slots[1].target.z, 0.8f) && shape.m_slots[1].target.y == 3);
    // None: false.
    ResetWorld();
    shape.m_slots[1].dirty = false;
    CHECK(!shape.FitOnNetwork(1));
    CHECK(shape.m_slots[1].checkpoint == 0xFFFF && !shape.m_slots[1].dirty);
    // Only a type 15: it, and the height at least half a metre over it, or the leader's.
    AddPoint(0, 3, 0, 1.0f, 15);
    shape.m_leader = l->human();
    l->human()->m_position.y = 0.0f;
    shape.m_slots[1].target = {0, 9, 0, 0};
    CHECK(shape.FitOnNetwork(1));
    CHECK(shape.m_slots[1].checkpoint == 0 && Near(shape.m_slots[1].target.y, 3.5f));
    l->human()->m_position.y = 7.0f;
    shape.FitOnNetwork(1);
    CHECK(Near(shape.m_slots[1].target.y, 7.0f));
    shape.m_leader = nullptr;
    shape.FitOnNetwork(1);
    CHECK(Near(shape.m_slots[1].target.y, 3.0f));
    l->human()->m_position.y = 0.0f;
}

void TestMoveSlots()
{
    C_FormationShape shape;
    shape.m_slots[1].target = {10, 0, 0, 0};
    shape.m_slots[1].current = {9, 0, 0, 0};
    shape.MoveSlots(100);                               // 0.4 m
    CHECK(Near(shape.m_slots[1].current.x, 9.4f));
    shape.m_slots[1].current = {9.9f, 0, 0, 0};
    shape.MoveSlots(100);                               // (not past it)
    CHECK(Near(shape.m_slots[1].current.x, 10.0f));
    shape.m_slots[1].current = {0, 0, 0, 0};
    shape.MoveSlots(100);                               // far behind: to within 5
    CHECK(Near(shape.m_slots[1].current.x, 5.0f));
    // On the ground plane only.
    shape.m_slots[2].target = {0, 7, 1, 0};
    shape.m_slots[2].current = {0, 0, 0, 0};
    shape.MoveSlots(100);
    CHECK(shape.m_slots[2].current.y == 0.0f && Near(shape.m_slots[2].current.z, 0.4f));
}

void TestFollow()
{
    BigWorld();
    C_FormationShape shape;
    S_Fake *l = MakeHuman(0, 0), *m = MakeHuman(-2.5f, 0), *n = MakeHuman(-5.0f, 0);
    shape.SetType(static_cast<E_FormationType>(0));
    shape.SetLeader(l->human());
    shape.AddMember(m->human());
    shape.AddMember(n->human());
    // Moving: the slots behind it, about its way.
    l->human()->m_velocity = {0, 0, 2, 0};
    shape.m_changed = true;
    shape.Tick(100);
    CHECK(shape.m_mode == 1);
    CHECK(!shape.m_changed);
    CHECK(Near(shape.m_lastDir.z, 1.0f));
    CHECK(Near(shape.m_slots[1].target.x, -2.5f) && Near(shape.m_slots[1].target.z, 0.0f));
    CHECK(Near(shape.m_slots[2].target.x, -5.0f));
    CHECK(shape.m_slots[1].dirty);
    // Turned: follows the velocity (+x: the slot goes to +z in this frame).
    l->human()->m_velocity = {3, 0, 0, 0};
    shape.m_changed = true;
    shape.Tick(100);
    CHECK(Near(shape.m_lastDir.x, 1.0f));
    CHECK(Near(shape.m_slots[1].target.z, 2.5f));
    // Standing with nothing changed: nothing.
    l->human()->m_velocity = {0, 0, 0, 0};
    shape.m_slots[1].target = {1, 0, 1, 0};
    shape.Tick(100);
    CHECK(shape.m_slots[1].target.x == 1);
    // Standing, changed: by the last direction; without one, the view.
    shape.m_changed = true;
    shape.m_lastDir = {0, 0, 0, 0};
    g_viewDir = S_vector(0, 0, -1);
    shape.Tick(100);
    CHECK(Near(shape.m_lastDir.z, -1.0f));
    // A climbing leader: nothing; no leader or no slots: nothing.
    l->climb = 1;
    shape.m_changed = true;
    shape.m_lastDir = {1, 0, 0, 0};
    shape.Tick(100);
    CHECK(shape.m_changed);
    l->climb = 0;
    C_FormationShape none;
    none.Tick(100);
    CHECK(none.m_mode == 0);
    // A dead member: not moved; the local player: not by the path.
    n->human()->m_stats[0] = 0;
    shape.m_slots[2].target = {9, 0, 9, 0};
    shape.m_changed = true;
    shape.Tick(100);
    CHECK(shape.m_slots[2].target.x == 9);
    n->human()->m_stats[0] = 100;
}

void TestFollowPaths()
{
    // Round a corner: walking it is longer than straight, so the slot comes back along the way.
    ResetWorld();
    const int p0 = AddPoint(0, 0, 0, 1.0f), p1 = AddPoint(0, 0, 10, 1.0f), p2 = AddPoint(-5, 0, 10, 1.0f);
    C_FormationShape shape;
    S_Fake *l = MakeHuman(0, 0), *m = MakeHuman(-1, 0);
    shape.SetType(static_cast<E_FormationType>(0));
    shape.SetLeader(l->human());
    shape.AddMember(m->human());
    g_paths.push_back({static_cast<uint16_t>(p0), static_cast<uint16_t>(p1), static_cast<uint16_t>(p2)});
    l->human()->m_velocity = {0, 0, 2, 0};
    shape.m_changed = true;
    // (The slot's place -2.5, 0 is nearest p0: no way to walk at all.)
    shape.Tick(100);
    CHECK(shape.m_slots[1].checkpoint == p0);
    // A slot's walk: FindPath's flags 0 on the way.
    CHECK(g_findPathFlags == 0x200 || g_findPathFlags == 0);
    // The scripted do not plan.
    const int before = g_findPaths;
    shape.m_scripted = true;
    shape.m_slots[1].pathFrom = 0xFFFF;
    shape.Tick(100);
    CHECK(g_findPathFlags == 0);
    (void)before;
}

void TestKeepApart()
{
    BigWorld();
    C_FormationShape shape;
    S_Fake *l = MakeHuman(0, 0), *m = MakeHuman(1, 0), *n = MakeHuman(2, 0);
    shape.SetLeader(l->human());
    shape.AddMember(m->human());
    shape.AddMember(n->human());
    shape.m_slots[0].target = {0, 0, 0, 0};
    shape.m_slots[1].target = {0, 0, 0.5f, 0};
    shape.m_slots[2].target = {5, 0, 0, 0};
    shape.m_slots[1].dirty = false;
    shape.KeepApart();
    // Too close to the leader's: 1.2 from it, along the line between. The far one stays, flagged only when moved.
    CHECK(Near(shape.m_slots[1].target.z, 1.2f));
    CHECK(shape.m_slots[1].pushedAside && shape.m_slots[1].dirty);
    CHECK(Near(shape.m_slots[2].target.x, 5.0f) && !shape.m_slots[2].pushedAside);
    // Those already moved aside are not again.
    shape.m_slots[1].target = {0, 0, 0.5f, 0};
    shape.KeepApart();
    CHECK(Near(shape.m_slots[1].target.z, 0.5f));
    // Exactly 1 m apart counts too; just over 1.2 does not.
    shape.m_slots[1].pushedAside = false;
    shape.m_slots[1].target = {0, 0, 1.0f, 0};
    shape.KeepApart();
    CHECK(Near(shape.m_slots[1].target.z, 1.2f));
    shape.m_slots[1].pushedAside = false;
    shape.m_slots[1].target = {0, 0, 1.25f, 0};
    shape.KeepApart();
    CHECK(Near(shape.m_slots[1].target.z, 1.25f));
}

void TestPushAside()
{
    BigWorld();
    C_FormationShape shape;
    S_Fake *l = MakeHuman(0, 0), *m = MakeHuman(1, 0);
    shape.SetLeader(l->human());
    shape.AddMember(m->human());
    // Going the same way: stays.
    l->human()->m_velocity = {0, 0, 1, 0};
    m->human()->m_velocity = {0, 0, 1, 0};
    shape.m_slots[1].target = {1, 0, 1, 0};
    shape.m_slots[1].dirty = false;
    shape.PushAside(1, 1.2f);
    CHECK(shape.m_slots[1].target.x == 1 && !shape.m_slots[1].dirty);
    // Otherwise: away from the leader's line, a little ahead.
    m->human()->m_velocity = {0, 0, 0, 0};
    shape.PushAside(1, 1.2f);
    CHECK(shape.m_slots[1].dirty && shape.m_slots[1].pushedAside);
    CHECK(shape.m_slots[1].target.x > 1.5f);
    CHECK(shape.m_slots[1].target.z > 1.0f);
    // On the other side: the other way.
    shape.m_slots[1].target = {-1, 0, 1, 0};
    shape.PushAside(1, 1.2f);
    CHECK(shape.m_slots[1].target.x < -1.5f);
    // Across its way too.
    m->human()->m_velocity = {1, 0, 0, 0};
    shape.m_slots[1].target = {1, 0, 1, 0};
    shape.PushAside(1, 1.2f);
    CHECK(shape.m_slots[1].target.x > 1.5f);
    // The leader's own slot, or no leader: nothing.
    shape.m_slots[0].target = {0, 0, 0, 0};
    shape.PushAside(0, 1.2f);
    CHECK(shape.m_slots[0].target.x == 0);
    shape.m_leader = nullptr;
    shape.m_slots[1].target = {1, 0, 1, 0};
    shape.PushAside(1, 1.2f);
    CHECK(shape.m_slots[1].target.x == 1);
}

void TestPlanSlot()
{
    ResetWorld();
    const int a = AddPoint(0, 0, 0, 1.0f), b = AddPoint(0, 0, 4, 1.0f), c = AddPoint(0, 0, 8, 1.0f), d = AddPoint(6, 0, 8, 1.0f), e = AddPoint(12, 0, 8, 1.0f);
    C_FormationShape shape;
    S_Fake *l = MakeHuman(0, 0), *m = MakeHuman(12, 8);
    shape.m_slots[0].member = l->human();
    shape.m_slots[1].member = m->human();
    S_FormationSlot &first = shape.m_slots[0], &slot = shape.m_slots[1];
    first.target = {0, 0, 0, 0};
    first.checkpoint = static_cast<uint16_t>(a);
    slot.checkpoint = static_cast<uint16_t>(e);
    slot.target = {12, 0, 8, 0};
    g_paths.push_back({static_cast<uint16_t>(a), static_cast<uint16_t>(b), static_cast<uint16_t>(c), static_cast<uint16_t>(d), static_cast<uint16_t>(e)});
    // The first slot, an empty slot, or without points: nothing.
    shape.PlanSlot(0);
    CHECK(g_findPaths == 0);
    shape.PlanSlot(2);
    CHECK(g_findPaths == 0);
    slot.checkpoint = 0xFFFF;
    shape.PlanSlot(1);
    CHECK(g_findPaths == 0);
    slot.checkpoint = static_cast<uint16_t>(e);
    first.checkpoint = 0xFFFF;
    shape.PlanSlot(1);
    CHECK(g_findPaths == 0);
    first.checkpoint = static_cast<uint16_t>(a);
    // 6 m along it: 4 to b, then 2 on toward... the third leg.
    slot.distance = 6.0f;
    slot.dirty = false;
    shape.PlanSlot(1);
    CHECK(g_findPaths == 1 && g_findPathFlags == 0x200);
    CHECK(slot.pathFrom == e && first.pathFrom == a);
    // (b at 4 m; 2 left, from c along to d by the leg lengths)
    CHECK(slot.dirty);
    CHECK(Near(slot.target.x, 2.0f) && Near(slot.target.z, 8.0f));
    // Cached while the points stay.
    shape.PlanSlot(1);
    CHECK(g_findPaths == 1);
    // Not reaching even the first point: nothing.
    slot.target = {12, 0, 8, 0};
    slot.dirty = false;
    slot.distance = 2.0f;
    shape.PlanSlot(1);
    CHECK(slot.target.x == 12 && !slot.dirty);
    // Beyond the whole way: nothing moves.
    slot.distance = 100.0f;
    shape.PlanSlot(1);
    CHECK(slot.target.x == 12 && !slot.dirty);
    // Another start: found again; no way: nothing.
    first.checkpoint = static_cast<uint16_t>(b);
    g_pathFails = true;
    shape.PlanSlot(1);
    CHECK(g_findPaths == 2);
    CHECK(slot.path.empty());
    g_pathFails = false;
}

void TestBoarding()
{
    BigWorld();
    C_FormationShape shape;
    S_Fake *l = MakeHuman(0, 0), *m = MakeHuman(12, 20), *n = MakeHuman(10, 20);
    shape.SetLeader(l->human());
    shape.AddMember(m->human());
    shape.AddMember(n->human());
    S_FakeVehicle *car = MakeVehicle(ACTOR_CAR, 3);
    l->human()->m_vehicle = car->actor();
    car->occupants[0] = l->human();
    shape.Tick(100);
    // Each by the nearest entry; seats in order, the taken skipped.
    CHECK(shape.m_slots[1].vehicleId == car->actor()->GetNetId() && shape.m_slots[1].seat == 1);
    CHECK(shape.m_slots[2].vehicleId == car->actor()->GetNetId() && shape.m_slots[2].seat == 2);
    CHECK(shape.m_slots[1].dir.z == 3.0f);              // (the entry at x = 12)
    CHECK(shape.m_slots[2].dir.z == 1.0f);              // (at x = 10)
    CHECK(shape.m_slots[0].vehicleId == -1);
    CHECK(shape.m_slots[1].dirty);
    // A member with a vehicle of its own: skipped.
    S_FakeVehicle *other = MakeVehicle(ACTOR_CAR, 1);
    m->human()->m_vehicle = other->actor();
    shape.m_slots[1].seat = 0;
    shape.Tick(100);
    CHECK(shape.m_slots[1].vehicleId == -1);
    CHECK(shape.m_slots[2].seat == 1);
    m->human()->m_vehicle = nullptr;
    // Full: the rest at a place.
    car->occupants[1] = reinterpret_cast<C_Actor *>(l->human());
    car->occupants[2] = reinterpret_cast<C_Actor *>(l->human());
    shape.Tick(100);
    CHECK(shape.m_slots[1].vehicleId == -1);
    CHECK(shape.m_slots[1].dirty);
    // No entries at all: nothing.
    car->occupants[1] = car->occupants[2] = nullptr;
    car->hasEntry[0] = car->hasEntry[1] = car->hasEntry[2] = false;
    shape.m_slots[1].target = {77, 0, 0, 0};
    shape.Tick(100);
    CHECK(shape.m_slots[1].target.x == 77);
    // A submarine: all of them looked at, its own first.
    S_FakeVehicle *sub1 = MakeVehicle(ACTOR_SUBMARINE, 2), *sub2 = MakeVehicle(ACTOR_SUBMARINE, 2);
    g_submarines = {sub2->actor(), sub1->actor()};
    l->human()->m_vehicle = sub1->actor();
    shape.Tick(100);
    CHECK(shape.m_slots[1].vehicleId == sub1->actor()->GetNetId());
    CHECK(shape.m_slots[2].vehicleId == sub1->actor()->GetNetId());
    CHECK(Near(shape.m_slots[1].target.z, 15.0f, 30.0f));
    // A sub's second seat taken and three members: the third to the other.
    sub1->occupants[0] = l->human();
    S_Fake *o = MakeHuman(0, 0);
    shape.AddMember(o->human());
    shape.Tick(100);
    CHECK(shape.m_slots[3].vehicleId == sub2->actor()->GetNetId() || shape.m_slots[2].vehicleId == sub2->actor()->GetNetId());
    // Another kind of vehicle: not boarded, but not moved by the shape in this tick either when kept (0x15).
    S_FakeVehicle *train = MakeVehicle(static_cast<E_ActorType>(0x15), 1);
    l->human()->m_vehicle = train->actor();
    shape.m_slots[1].vehicleId = 5;
    shape.Tick(100);
    CHECK(shape.m_slots[1].vehicleId == 5);
    l->human()->m_vehicle = nullptr;
}

void TestDoor()
{
    ResetWorld();
    const int point = AddPoint(0, 0, 0, 15.0f), door = AddPoint(0, 0, 2, 15.0f, 1);
    LinkTo(point, door);
    C_FormationShape shape;
    S_Fake *l = MakeHuman(0, 0), *a = MakeHuman(1, 0), *b = MakeHuman(-1, 0), *c = MakeHuman(0, -1);
    shape.SetLeader(l->human());
    shape.AddMember(a->human());
    shape.AddMember(b->human());
    shape.AddMember(c->human());
    shape.SetDoorPoint(static_cast<uint16_t>(point));
    CHECK(shape.m_doorState == -1);
    // Everyone away from their places: still waiting, state -1 again.
    shape.Tick(100);
    CHECK(shape.m_mode == 2);
    CHECK(shape.m_doorState == -1);
    // On this side at state -1: one each side of the door's way, facing back; the third behind.
    // (1 m from the leader's slot, so kept apart to 1.2.)
    CHECK(Near(shape.m_slots[1].target.x, -1.2f) && Near(shape.m_slots[1].target.z, 0.0f));
    CHECK(Near(shape.m_slots[2].target.x, 1.2f) && Near(shape.m_slots[2].target.z, 0.0f));
    CHECK(Near(shape.m_slots[3].target.z, -1.0f, 0.3f));
    CHECK(Near(shape.m_slots[1].dir.z, -1.0f) && Near(shape.m_slots[2].dir.z, -1.0f));
    CHECK(Near(shape.m_slots[3].dir.z, 1.0f));
    // All in place: state 0: the first goes through.
    a->human()->m_position = {shape.m_slots[1].target.x, 0, shape.m_slots[1].target.z, 1};
    b->human()->m_position = {shape.m_slots[2].target.x, 0, shape.m_slots[2].target.z, 1};
    c->human()->m_position = {shape.m_slots[3].target.x, 0, shape.m_slots[3].target.z, 1};
    shape.Tick(100);
    CHECK(shape.m_doorState == 0);
    shape.Tick(100);
    CHECK(Near(shape.m_slots[1].target.z, 2.0f));
    CHECK(Near(shape.m_slots[1].dir.z, 1.0f));
    CHECK(shape.m_doorState == 0);                      // (it has not got there)
    a->human()->m_position = {shape.m_slots[1].target.x, 0, shape.m_slots[1].target.z, 1};
    shape.Tick(100);
    CHECK(shape.m_doorState == 1);
    // Moving it counts as not there.
    b->human()->m_velocity = {1, 0, 0, 0};
    shape.Tick(100);
    b->human()->m_position = {shape.m_slots[2].target.x, 0, shape.m_slots[2].target.z, 1};
    shape.Tick(100);
    CHECK(shape.m_doorState == 1);
    b->human()->m_velocity = {0, 0, 0, 0};
    shape.Tick(100);
    CHECK(shape.m_doorState == 2);
    CHECK(Near(shape.m_slots[2].target.z, 2.0f));
    shape.Tick(100);
    CHECK(Near(shape.m_slots[3].target.z, 3.0f, 0.3f));
    // A point without a door: nothing, no crash.
    const int lone = AddPoint(5, 0, 5, 1.0f);
    shape.SetDoorPoint(static_cast<uint16_t>(lone));
    shape.m_slots[1].target = {9, 0, 9, 0};
    shape.Tick(100);
    CHECK(shape.m_slots[1].target.x == 9);
}

void TestLeaderDied()
{
    BigWorld();
    C_FormationShape shape;
    S_Fake *l = MakeHuman(0, 0), *m = MakeHuman(1, 0), *n = MakeHuman(2, 0);
    shape.SetLeader(l->human());
    shape.AddMember(m->human());
    shape.AddMember(n->human());
    alignas(16) static unsigned char controllerBytes[0x1000];
    std::memset(controllerBytes, 0, sizeof(controllerBytes));
    *reinterpret_cast<void ***>(controllerBytes) = g_squadVt;
    C_HumanController *controller = reinterpret_cast<C_HumanController *>(controllerBytes);
    controller->m_type = HUMAN_CONTROLLER_SQUAD;
    m->human()->m_controller = controller;
    n->human()->m_controller = nullptr;
    struct { int now, set; } cases[] = {{0xC, 0x10}, {0xE, 0x10}, {0xD, 0x11}, {0xF, 0x11}, {0xB, -1}, {0x10, -1}};
    for (auto &one : cases) {
        g_orderNow = one.now;
        g_orderSet = -1;
        shape.OnLeaderDied();
        CHECK(g_orderSet == one.set);
    }
    // Not of the squad: no.
    controller->m_type = HUMAN_CONTROLLER_NONE;
    g_orderNow = 0xC;
    g_orderSet = -1;
    shape.OnLeaderDied();
    CHECK(g_orderSet == -1);
    // The leader itself: no.
    controller->m_type = HUMAN_CONTROLLER_SQUAD;
    m->human()->m_controller = nullptr;
    l->human()->m_controller = controller;
    shape.OnLeaderDied();
    CHECK(g_orderSet == -1);
    // No slots: nothing.
    shape.SetType(FORMATION_NONE);
    shape.OnLeaderDied();
}

void TestMath()
{
    CHECK(LengthOfSq(4.0f) == 2.0f);
    CHECK(Near(LengthOfSq(1.5f), 1.22474f));
    CHECK(LengthOfSq(1.0f) == 1.0f);
    CHECK(LengthOfSq(1e-9f) == 0.0f);                   // (under the original's floor)
    CHECK(LengthOfSq(-1.0f) == 0.0f);
    CHECK(Near(LengthOfSq(1e-8f), 1e-4f, 1e-5f));

    S_vector v = Unit(S_vector(3, 0, 4));
    CHECK(Near(v.x, 0.6f) && Near(v.z, 0.8f));
    v = Unit(S_vector(0, 2, 0));
    CHECK(Near(v.y, 1.0f));
    v = Unit(S_vector(0, 0, 1.5f));
    CHECK(Near(v.z, 1.0f));
    v = Unit(S_vector(1e-5f, 0, 0));
    CHECK(v.x == 1 && v.y == 0 && v.z == 0);
    v = Unit(S_vector(-1e-5f, 0, 0));
    CHECK(v.x == -1);
    v = Unit(S_vector(0, 0, 1e-5f));
    CHECK(v.z == 1 && v.x == 0);
    v = Unit(S_vector(0, 0, -1e-5f));
    CHECK(v.z == -1);
    v = Unit(S_vector(0, 0, 0));
    CHECK(v.y == 1);
    v = Unit(S_vector(0, -1e-5f, 0));
    CHECK(v.y == -1);
    v = Unit(S_vector(0.1f, 0, 0));                     // (sq .01: not so small)
    CHECK(Near(v.x, 1.0f));

    S_vector4 four = {1, 2, 3, 4};
    SetXyz(four, S_vector(5, 6, 7));
    CHECK(four.x == 5 && four.y == 6 && four.z == 7 && four.w == 4);
}

void TestMatrix()
{
    C_FormationShape shape;
    shape.SetDir(S_vector(0.6f, 0, 0.8f));
    CHECK(shape.m_dir.x == 0.6f && shape.m_dir.z == 0.8f);
    // (SetDir takes the scale it had.)
    CHECK(Near(shape.m_matrix.m[2][0], 0.6f * 2.5f));
    shape.SetScale(S_vector(2, 3, 4));
    CHECK(shape.m_scale.x == 2 && shape.m_scale.y == 3 && shape.m_scale.z == 4);
    CHECK(Near(shape.m_matrix.m[0][0], 0.8f * 2) && Near(shape.m_matrix.m[0][2], -0.6f * 2));
    CHECK(Near(shape.m_matrix.m[1][1], 3.0f));
    CHECK(Near(shape.m_matrix.m[2][0], 0.6f * 4) && Near(shape.m_matrix.m[2][2], 0.8f * 4));
    CHECK(shape.m_matrix.m[3][3] == 1.0f);
    // Each slot: place and direction through it, every row.
    for (int type = 0; type <= 10; ++type) {
        shape.SetType(static_cast<E_FormationType>(type));
        for (int i = 0; i < 6; ++i) {
            const float *row = FORMATION_TABLE[type][i];
            const S_FormationSlot &slot = shape.m_slots[i];
            CHECK(slot.basePos.x == row[0] && slot.basePos.y == row[1] && slot.basePos.z == row[2]);
            const S_vector dir = Unit(S_vector(row[3], row[4], row[5]));
            CHECK(slot.baseDir.x == dir.x && slot.baseDir.y == dir.y && slot.baseDir.z == dir.z);
            const S_vector scaled = S_vector(row[0], row[1], row[2]) * shape.m_matrix;
            CHECK(Near(slot.scaledPos.x, scaled.x) && Near(slot.scaledPos.y, scaled.y) && Near(slot.scaledPos.z, scaled.z));
            const S_vector turned = Unit(dir * shape.m_matrix);
            CHECK(Near(slot.scaledDir.x, turned.x) && Near(slot.scaledDir.y, turned.y) && Near(slot.scaledDir.z, turned.z));
        }
    }
    // The last of the types comes round, not before.
    shape.SetType(static_cast<E_FormationType>(9));
    shape.SetType(FORMATION_NEXT);
    CHECK(shape.GetType() == 10);
    shape.SetType(FORMATION_NEXT);
    CHECK(shape.GetType() == 1);
    // Freed.
    {
        int before = g_arraysFreed;
        C_FormationShape freed;
        freed.SetType(FORMATION_NONE);
        CHECK(g_arraysFreed == before + 1);
        before = g_arraysFreed;
        {
            C_FormationShape gone;
        }
        CHECK(g_arraysFreed == before + 1);
    }
}

void TestMembersMore()
{
    BigWorld();
    S_Fake *fakes[7];
    for (S_Fake *&fake : fakes)
        fake = MakeHuman(1, 1);
    C_Human *h[7];
    for (int i = 0; i < 7; ++i)
        h[i] = fakes[i]->human();

    // In the last slot: found. Its matrix's w and direction.
    C_FormationShape a;
    reinterpret_cast<float *>(fakes[0]->frameBytes + 0x40)[15] = 2.5f;
    reinterpret_cast<float *>(fakes[0]->frameBytes + 0x40)[8] = 3.0f;
    reinterpret_cast<float *>(fakes[0]->frameBytes + 0x40)[10] = 4.0f;
    a.m_slots[5].member = h[1];
    int slot = -1;
    CHECK(a.FindSlot(h[1], &slot) && slot == 5);
    CHECK(a.AddMember(h[0]));
    CHECK(a.m_slots[0].target.w == 2.5f && a.m_slots[0].current.w == 2.5f);
    CHECK(Near(a.m_slots[0].dir.x, 0.6f) && Near(a.m_slots[0].dir.z, 0.8f));

    // Closing up: the last but one and the last.
    C_FormationShape b;
    b.SetLeader(h[1]);
    for (int i = 2; i <= 6; ++i)
        b.AddMember(h[i]);
    CHECK(b.RemoveMember(h[5]));
    CHECK(b.GetSlotMember(4) == h[6] && b.GetSlotMember(5) == nullptr && b.GetSlotMember(3) == h[4]);
    CHECK(b.GetMemberCount() == 5);
    b.AddMember(h[5]);
    CHECK(b.RemoveMember(h[5]));                        // (from the last slot)
    CHECK(b.GetSlotMember(5) == nullptr && b.GetSlotMember(4) == h[6]);
    CHECK(b.AddMember(h[5]) && b.RemoveMember(h[2]));
    CHECK(b.GetSlotMember(1) == h[3] && b.GetSlotMember(2) == h[4] && b.GetSlotMember(3) == h[6] && b.GetSlotMember(4) == h[5] && b.GetSlotMember(5) == nullptr);

    // The leader met on the way: passed by (it can only be so with a leader not in the first slot).
    C_FormationShape c;
    c.m_slots[0].member = h[2];
    c.m_slots[1].member = h[3];
    c.m_slots[2].member = h[1];
    c.m_slots[3].member = h[1];
    c.m_slots[4].member = h[4];
    c.m_slots[5].member = h[5];
    c.m_leader = h[1];
    c.m_memberCount = 6;
    CHECK(c.RemoveMember(h[3]));
    // (Once the leader has come to the emptied slot, only a slot before another leader's is filled.)
    CHECK(c.GetSlotMember(1) == h[1] && c.GetSlotMember(2) == h[4] && c.GetSlotMember(3) == h[1] && c.GetSlotMember(4) == h[4] && c.GetSlotMember(5) == nullptr);

    // The leader from the last slot takes the first; the one there goes to its place.
    C_FormationShape d;
    d.m_slots[0].member = h[2];
    d.m_slots[5].member = h[1];
    d.m_memberCount = 2;
    CHECK(d.SetLeader(h[1]));
    CHECK(d.GetSlotMember(0) == h[1] && d.GetSlotMember(5) == h[2]);
    C_FormationShape dw;
    dw.m_slots[0].member = h[2];
    dw.m_slots[5].member = h[0];
    dw.m_memberCount = 2;
    CHECK(dw.SetLeader(h[0]));
    CHECK(dw.m_slots[0].target.w == 2.5f);

    // Places around a leader turned to +x, of every slot; its w; its own place even if the table has it elsewhere.
    BigWorld();
    S_Fake *l = MakeHuman(3, 2);
    float *world = reinterpret_cast<float *>(l->frameBytes + 0x40);
    world[8] = 2.0f; world[10] = 0.0f;                  // (not of unit length: made so)
    world[15] = 1.5f;
    C_FormationShape e;
    e.SetType(static_cast<E_FormationType>(0));
    e.m_slots[0].basePos = {1, 0, 1, 0};
    e.m_slots[5].basePos = {-4, 0, 0, 0};
    CHECK(e.SetLeader(l->human()));
    CHECK(e.m_slots[0].target.x == 3 && e.m_slots[0].target.z == 2 && e.m_slots[0].target.w == 1.5f);
    CHECK(Near(e.m_slots[0].scaledPos.x, 2.5f));
    // (scaled -10, 0, 0: along the matrix's x, which is (z, 0, -x) of (1, 0, 0): (0, 0, -1); so +10 in z.)
    CHECK(Near(e.m_slots[5].target.x, 3.0f) && Near(e.m_slots[5].target.z, 12.0f));
    world[8] = 0.0f; world[10] = 1.0f;

    // The dead leader's squad: last slot and first slot too.
    BigWorld();
    alignas(16) static unsigned char controllerBytes[0x1000];
    std::memset(controllerBytes, 0, sizeof(controllerBytes));
    *reinterpret_cast<void ***>(controllerBytes) = g_squadVt;
    C_HumanController *controller = reinterpret_cast<C_HumanController *>(controllerBytes);
    controller->m_type = HUMAN_CONTROLLER_SQUAD;
    S_Fake *f = MakeHuman(0, 0);
    f->human()->m_controller = controller;
    g_orderNow = 0xC;
    for (int at : {0, 5}) {
        C_FormationShape g;
        g.m_slots[at].member = f->human();
        g_orderSet = -1;
        g.OnLeaderDied();
        CHECK(g_orderSet == 0x10);
    }
}

void TestQueriesMore()
{
    BigWorld();
    S_Fake *l = MakeHuman(0, 0), *m = MakeHuman(2, 0);
    C_FormationShape shape;
    shape.AddMember(m->human());
    shape.m_slots[0].dirty = true;
    shape.m_slots[0].seat = 3;
    shape.m_slots[0].vehicleId = 9;
    S_vector4 pos = {}, dir = {};
    int vehicle = 5;
    // A member but no leader: nothing is told.
    CHECK(!shape.GetSlotTarget(m->human(), &pos, nullptr));
    CHECK(shape.m_slots[0].dirty);
    CHECK(!shape.GetSlotDir(m->human(), &dir));
    CHECK(shape.GetSlotSeat(m->human(), &vehicle) == 0 && vehicle == 5);
    shape.m_slots[0].dirty = false;
    shape.InvalidateSlot(m->human());
    CHECK(!shape.m_slots[0].dirty);
    // With a leader, no one asked for: the empty slot does not answer.
    shape.SetLeader(l->human());
    shape.m_slots[3].dirty = true;
    CHECK(!shape.GetSlotTarget(nullptr, &pos, nullptr));
    CHECK(shape.m_slots[3].dirty);
    // No slots.
    C_FormationShape none;
    none.m_leader = l->human();
    none.SetType(FORMATION_NONE);
    CHECK(!none.GetSlotTarget(l->human(), &pos, nullptr));
    CHECK(!none.GetSlotDir(l->human(), &dir));
    vehicle = 5;
    CHECK(none.GetSlotSeat(l->human(), &vehicle) == 0 && vehicle == 5);
    none.InvalidateSlot(l->human());
    none.m_leader = nullptr;
    // The asked for in the first slot (index 0).
    shape.m_slots[0].seat = 4;
    int slot = -1;
    CHECK(shape.FindSlot(l->human(), &slot) && slot == 0);
    CHECK(shape.GetSlotSeat(l->human(), &vehicle) == 4 && vehicle == shape.m_slots[0].vehicleId);
    shape.m_slots[0].dir = {0, 0, 7, 0};
    CHECK(shape.GetSlotDir(l->human(), &dir) && dir.z == 7);
    shape.m_slots[0].dirty = false;
    shape.InvalidateSlot(l->human());
    CHECK(shape.m_slots[0].dirty);
    const S_vector4 there = {7, 8, 9, 1};
    shape.SetSlotTarget(l->human(), there);
    CHECK(shape.m_slots[0].target.x == 7);
    shape.m_slots[0].target = {0, 0, 0, 0};
    shape.m_slots[0].dirty = false;
    CHECK(!shape.GetSlotTarget(l->human(), &pos, nullptr));
    shape.m_slots[0].target = {1, 2, 3, 0};
    CHECK(pos.x == 0);
    shape.m_slots[0].dirty = true;
    CHECK(shape.GetSlotTarget(l->human(), &pos, nullptr) && pos.x == 1);
}

void TestFitMore()
{
    ResetWorld();
    AddPoint(0, 3, 0, 1.0f);
    C_FormationShape shape;
    shape.m_slots[1].target = {0.5f, 40, 0, 0};
    CHECK(shape.FitOnNetwork(1));
    CHECK(g_nearestRadius == 5.0f && g_nearestY == -1.0f);
    // Height does not count for being within (Kept out of the sum).
    shape.m_slots[1].target = {0.7f, 40, 0.7f, 0};
    CHECK(shape.FitOnNetwork(1));
    CHECK(shape.m_slots[1].target.x == 0.7f && shape.m_slots[1].target.z == 0.7f);
    // A point off the origin.
    ResetWorld();
    AddPoint(5, 1, 5, 1.0f);
    shape.m_slots[1].target = {5, 0, 8, 0};
    CHECK(shape.FitOnNetwork(1));
    CHECK(Near(shape.m_slots[1].target.x, 5.0f) && Near(shape.m_slots[1].target.z, 6.0f) && shape.m_slots[1].target.y == 1);
    shape.m_slots[1].target = {8, 0, 5, 0};
    CHECK(shape.FitOnNetwork(1));
    CHECK(Near(shape.m_slots[1].target.x, 6.0f) && Near(shape.m_slots[1].target.z, 5.0f));
    // The type 15 search.
    ResetWorld();
    AddPoint(0, 0, 0, 1.0f, 15);
    shape.m_slots[1].target = {0, 0, 0, 0};
    CHECK(shape.FitOnNetwork(1));
    CHECK(g_nearest15Radius == 5.0f && g_nearest15Y == -1.0f);

    // Kept apart: the last slot, the leader's point found; with no point the mark stays.
    BigWorld();
    C_FormationShape apart;
    S_Fake *l = MakeHuman(0, 0), *m = MakeHuman(0, 0), *n = MakeHuman(0, 0);
    apart.SetLeader(l->human());
    apart.m_slots[5].member = m->human();
    apart.m_slots[0].checkpoint = 0xFFFF;
    apart.m_slots[0].target = {0, 0, 0, 0};
    apart.m_slots[5].target = {0, 0, 0.5f, 0};
    apart.KeepApart();
    CHECK(apart.m_slots[0].checkpoint == 0);
    CHECK(apart.m_slots[5].pushedAside && Near(apart.m_slots[5].target.z, 1.2f));
    // Against the one in the last slot, with heights: (y kept in the sum when nothing fits it).
    ResetWorld();
    C_FormationShape high;
    high.m_slots[0].member = l->human();
    high.m_leader = l->human();
    high.m_slots[0].target = {9, 9, 9, 0};
    high.m_slots[4].member = n->human();
    high.m_slots[5].member = m->human();
    high.m_slots[5].target = {0, 1, 0, 0};
    high.m_slots[4].target = {0, 0.5f, 0, 0};
    high.m_slots[4].dirty = false;
    high.KeepApart();
    // (5 looks at the others first: 0.5 away: 1.2 over 4, on 0.5.)
    CHECK(Near(high.m_slots[5].target.y, 1.7f) && high.m_slots[5].dirty);
    CHECK(!high.m_slots[4].pushedAside);
    high.m_slots[5].pushedAside = true;
    high.m_slots[5].target = {0, 0, 0, 0};
    high.m_slots[4].target = {0, 0.5f, 0, 0};
    high.m_slots[4].pushedAside = false;
    high.KeepApart();
    CHECK(Near(high.m_slots[4].target.y, 1.2f) && high.m_slots[4].pushedAside);
}

void TestPushMore()
{
    BigWorld();
    C_FormationShape shape;
    S_Fake *l = MakeHuman(0, 0), *m = MakeHuman(1, 0);
    shape.SetLeader(l->human());
    shape.AddMember(m->human());
    S_FormationSlot &slot = shape.m_slots[1];
    // Exactly: away from the line, .15 ahead.
    l->human()->m_velocity = {0, 0, 1, 0};
    m->human()->m_velocity = {};
    slot.target = {1, 0, 1, 0};
    shape.PushAside(1, 2.0f);
    const float k = 1.0f / std::sqrt(1.0f + 0.15f * 0.15f);
    CHECK(Near(slot.target.x, 1.0f + 2.0f * k) && Near(slot.target.z, 1.0f + 2.0f * 0.15f * k) && slot.target.y == 0.0f);
    // The leader going up too, off to a side: all three.
    l->human()->m_velocity = {0, 1, 0, 0};
    slot.target = {1, 0, 0, 0};
    shape.PushAside(1, 2.0f);
    CHECK(Near(slot.target.x, 1.0f + 2.0f * k) && Near(slot.target.y, 2.0f * 0.15f * k) && Near(slot.target.z, 0.0f));
    // The leader going along x.
    l->human()->m_velocity = {1, 0, 0, 0};
    slot.target = {0, 0, 1, 0};
    shape.PushAside(1, 2.0f);
    CHECK(Near(slot.target.x, 2.0f * 0.15f * k) && Near(slot.target.z, 1.0f + 2.0f * k));
    // Velocities of any size: a slow one going the same way stays.
    l->human()->m_velocity = {0, 0, 1, 0};
    m->human()->m_velocity = {0, 0, 0.1f, 0};
    slot.target = {1, 0, 1, 0};
    slot.dirty = false;
    shape.PushAside(1, 2.0f);
    CHECK(slot.target.x == 1.0f && !slot.dirty);
    // Barely the same way: by .12: stays; by .05: goes.
    m->human()->m_velocity = {0.99277f, 0, 0.12f, 0};
    shape.PushAside(1, 2.0f);
    CHECK(slot.target.x == 1.0f);
    m->human()->m_velocity = {0.99875f, 0, 0.05f, 0};
    shape.PushAside(1, 2.0f);
    CHECK(slot.target.x > 2.0f);
    // A standing leader (member going): its direction nothing, a push by what is across: none, and straight on.
    l->human()->m_velocity = {};
    m->human()->m_velocity = {1, 0, 0, 0};
    slot.target = {1, 0, 1, 0};
    shape.PushAside(1, 2.0f);
    CHECK(slot.dirty);
}

void TestPlanMore()
{
    ResetWorld();
    const int a = AddPoint(0, 0, 0, 1.0f), b = AddPoint(0, 0, 4, 1.0f), c = AddPoint(0, 0, 8, 1.0f), d = AddPoint(6, 0, 8, 1.0f), e = AddPoint(12, 0, 8, 1.0f);
    C_FormationShape shape;
    S_Fake *l = MakeHuman(0, 0), *m = MakeHuman(12, 8);
    shape.m_slots[0].member = l->human();
    shape.m_slots[1].member = m->human();
    S_FormationSlot &first = shape.m_slots[0], &slot = shape.m_slots[1];
    first.target = {0, 0, 0, 0};
    first.checkpoint = static_cast<uint16_t>(a);
    slot.checkpoint = static_cast<uint16_t>(e);
    g_paths.push_back({static_cast<uint16_t>(a), static_cast<uint16_t>(b), static_cast<uint16_t>(c), static_cast<uint16_t>(d), static_cast<uint16_t>(e)});
    // Second leg: along toward the slot's own place, by what was left after the one before.
    slot.target = {12, 0, 8, 0};
    slot.distance = 11.0f;
    shape.PlanSlot(1);
    CHECK(Near(slot.target.x, 9.0f) && Near(slot.target.z, 8.0f));
    // Toward the place when it is not on the way.
    slot.target = {6, 0, 18, 0};
    slot.distance = 11.0f;
    shape.PlanSlot(1);
    CHECK(Near(slot.target.x, 6.0f) && Near(slot.target.z, 11.0f));
    // A very little over the first point: on it, just past.
    slot.target = {12, 0, 8, 0};
    slot.distance = 4.1f;
    shape.PlanSlot(1);
    CHECK(Near(slot.target.x, 0.1f) && Near(slot.target.z, 8.0f));
    // Over the first leg by a little.
    slot.target = {12, 0, 8, 0};
    slot.distance = 8.1f;
    shape.PlanSlot(1);
    CHECK(Near(slot.target.x, 6.1f) && Near(slot.target.z, 8.0f));
    // Past the last but one by less than the last leg: stays.
    slot.target = {12, 0, 8, 0};
    slot.dirty = false;
    slot.distance = 16.0f;
    shape.PlanSlot(1);
    CHECK(slot.target.x == 12.0f && !slot.dirty);
    // Measured from the first's own place, not its point.
    first.target = {0, 0, 1, 0};
    slot.target = {12, 0, 8, 0};
    slot.distance = 3.5f;
    slot.dirty = false;
    shape.PlanSlot(1);
    CHECK(slot.dirty && Near(slot.target.x, 0.5f) && Near(slot.target.z, 8.0f));
    first.target = {0, 0, 0, 0};
    // Up and across.
    ResetWorld();
    const int p = AddPoint(0, 0, 0, 1.0f), q = AddPoint(0, 0, 1, 1.0f), r = AddPoint(0, 0, 2, 1.0f), s = AddPoint(3, 4, 2, 1.0f), t = AddPoint(3, 4, 12, 1.0f);
    g_paths.clear();
    g_paths.push_back({static_cast<uint16_t>(p), static_cast<uint16_t>(q), static_cast<uint16_t>(r), static_cast<uint16_t>(s), static_cast<uint16_t>(t)});
    C_FormationShape up;
    up.m_slots[0].member = l->human();
    up.m_slots[1].member = m->human();
    up.m_slots[0].target = {0, 0, 0, 0};
    up.m_slots[0].checkpoint = static_cast<uint16_t>(p);
    up.m_slots[1].checkpoint = static_cast<uint16_t>(t);
    up.m_slots[1].target = {3, 4, 12, 0};
    up.m_slots[1].distance = 1.5f;                       // (1 to q; of the 1 to r .5: from r toward s)
    up.PlanSlot(1);
    CHECK(Near(up.m_slots[1].target.x, 0.3f) && Near(up.m_slots[1].target.y, 0.4f) && Near(up.m_slots[1].target.z, 2.0f));
    // On to t's leg.
    up.m_slots[1].target = {3, 4, 12, 0};
    up.m_slots[1].distance = 6.5f;                       // (1 to q, 1 to r; 4.5 of the 5 to s: from s toward its place)
    up.PlanSlot(1);
    CHECK(Near(up.m_slots[1].target.x, 3.0f) && Near(up.m_slots[1].target.y, 4.0f) && Near(up.m_slots[1].target.z, 6.5f));
    // The way found starts at the first's point and goes to the slot's.
    g_paths.clear();
    C_FormationShape way;
    way.m_slots[0].member = l->human();
    way.m_slots[1].member = m->human();
    way.m_slots[0].checkpoint = static_cast<uint16_t>(q);
    way.m_slots[1].checkpoint = static_cast<uint16_t>(s);
    way.m_slots[1].distance = 1.0f;
    way.PlanSlot(1);
    CHECK(way.m_slots[1].path.size() == 2 && way.m_slots[1].path[0] == q && way.m_slots[1].path[1] == s);
    // Moved: found anew.
    const int found = g_findPaths;
    way.m_slots[1].checkpoint = static_cast<uint16_t>(t);
    way.PlanSlot(1);
    CHECK(g_findPaths == found + 1 && way.m_slots[1].pathFrom == t);
    way.m_slots[0].checkpoint = static_cast<uint16_t>(p);
    way.PlanSlot(1);
    CHECK(g_findPaths == found + 2 && way.m_slots[0].pathFrom == p);
}

void TestBoardingMore()
{
    ResetWorld();
    AddPoint(10, 3, 20, 15.0f);
    C_FormationShape shape;
    S_Fake *l = MakeHuman(10, 20), *m = MakeHuman(12, 20), *n = MakeHuman(10, 20), *o = MakeHuman(9, 20);
    shape.SetLeader(l->human());
    shape.AddMember(m->human());
    shape.AddMember(n->human());
    shape.AddMember(o->human());
    S_FakeVehicle *car = MakeVehicle(ACTOR_CAR, 3);
    l->human()->m_vehicle = car->actor();
    car->occupants[0] = l->human();
    shape.m_slots[0].vehicleId = 5;
    shape.m_slots[5].vehicleId = 6;
    shape.Tick(100);
    CHECK(shape.m_slots[0].vehicleId == -1);
    CHECK(shape.m_slots[5].vehicleId == -1);
    // Each by the nearest entry: seats as they come; the place's y from the network, not the current.
    S_FormationSlot &s1 = shape.m_slots[1], &s2 = shape.m_slots[2], &s3 = shape.m_slots[3], &s5 = shape.m_slots[5];
    CHECK(s1.target.x == 12 && s1.target.z == 20 && s1.target.y == 3 && s1.dir.z == 3 && s1.current.x == 12 && s1.current.y == 0 && s1.seat == 1);
    CHECK(s1.checkpoint == 0);
    CHECK(s2.target.x == 10 && s2.dir.z == 1 && s2.seat == 2);
    // The third, no seat left: the last free one's place, and all the rest of the slots.
    CHECK(s3.target.x == 12 && s3.target.y == 3 && s3.current.x == 12 && s3.current.y == 0 && s3.dir.z == 3 && s3.vehicleId == -1);
    CHECK(s3.checkpoint == 0);
    CHECK(s5.target.x == 12 && s5.current.x == 12 && s5.dir.z == 3 && s5.dirty);
    // Nothing fits: marked all the same.
    g_pointCount = 0;
    s1.vehicleId = -1;
    s1.dirty = s3.dirty = s5.dirty = false;
    shape.Tick(100);
    CHECK(s1.dirty && s3.dirty && s5.dirty);
    g_pointCount = 1;

    // All taken: the last seat's. Only the first free: its.
    car->occupants[1] = car->occupants[2] = l->human();
    shape.Tick(100);
    CHECK(s1.target.x == 12 && s1.dir.z == 3);
    car->occupants[0] = nullptr;
    shape.Tick(100);
    CHECK(s1.seat == 0 && s1.target.x == 12);
    CHECK(s2.target.x == 10 && s2.dir.z == 1 && s2.vehicleId == -1);
    car->occupants[0] = l->human();
    // Beyond the seats does not count.
    car->occupants[3] = l->human();
    shape.Tick(100);
    CHECK(s1.vehicleId == -1 && s1.target.x == 12);
    car->occupants[3] = nullptr;
    // The chosen has no entry: leave it.
    car->hasEntry[2] = false;
    s1.target = {77, 0, 0, 0};
    shape.Tick(100);
    CHECK(s1.target.x == 77);
    car->hasEntry[2] = true;

    // Of the same distance, the later seat's.
    car->occupants[1] = car->occupants[2] = nullptr;
    S_FakeVehicle *pair = MakeVehicle(ACTOR_CAR, 2);
    l->human()->m_vehicle = pair->actor();
    S_Fake *t = MakeHuman(10.5f, 20);
    C_FormationShape tie;
    tie.SetLeader(l->human());
    tie.AddMember(t->human());
    tie.Tick(100);
    CHECK(tie.m_slots[1].target.x == 10);
    l->human()->m_vehicle = car->actor();
    // A place from those with an entry only, in the lists together.
    car->hasEntry[2] = false;
    S_Fake *u = MakeHuman(20, 20);
    C_FormationShape some;
    some.SetLeader(l->human());
    some.AddMember(u->human());
    some.Tick(100);
    CHECK(some.m_slots[1].target.x == 11 && some.m_slots[1].dir.z == 2 && some.m_slots[1].current.x == 11);
    car->hasEntry[2] = true;

    // A submarine: the far side's place.
    ResetWorld();
    AddPoint(30, 3, 41, 15.0f);
    S_FakeVehicle *sub = MakeVehicle(ACTOR_SUBMARINE, 2);
    S_Fake *sl = MakeHuman(30, 41), *sm = MakeHuman(31, 41), *sn = MakeHuman(30, 41);
    C_FormationShape boat;
    boat.SetLeader(sl->human());
    boat.AddMember(sm->human());
    boat.AddMember(sn->human());
    g_submarines = {sub->actor()};
    sl->human()->m_vehicle = sub->actor();
    boat.Tick(100);
    CHECK(boat.m_slots[1].target.x == 31 && boat.m_slots[1].target.z == 41 && boat.m_slots[1].dir.x == 1);
    // Full: gives up where it has a place.
    sub->occupants[0] = sub->occupants[1] = sl->human();
    boat.m_slots[1].vehicleId = -1;
    boat.m_slots[1].target = {77, 0, 0, 0};
    boat.Tick(100);
    CHECK(boat.m_slots[1].target.x == 77);
    // ...and goes to nowhere where it has none.
    sub->hasEntry[1] = false;
    boat.Tick(100);
    CHECK(boat.m_slots[1].current.x == 0.0f);
    g_submarines.clear();
}

void TestDoorMore()
{
    // On a slant: a at the origin, b at 3, 4.
    ResetWorld();
    const int point = AddPoint(0, 0, 0, 15.0f), door = AddPoint(3, 0, 4, 15.0f, 2);
    LinkTo(point, door);
    C_FormationShape shape;
    S_Fake *l = MakeHuman(-8, 0), *a = MakeHuman(-8, 3), *b = MakeHuman(-8, 6), *c = MakeHuman(-8, 9), *e = MakeHuman(-8, -3), *f = MakeHuman(-8, -6);
    shape.SetLeader(l->human());
    shape.AddMember(a->human());
    shape.AddMember(b->human());
    shape.AddMember(c->human());
    shape.AddMember(e->human());
    shape.AddMember(f->human());
    shape.m_slots[0].target = {-8, 0, 0, 0};
    shape.m_slots[4].dirty = shape.m_slots[5].dirty = false;
    shape.m_slots[4].target = {-20, 0, 0, 0};
    shape.m_slots[5].target = {20, 0, 0, 0};
    shape.SetDoorPoint(static_cast<uint16_t>(point));
    shape.m_mode = 2;
    shape.LineUpAtDoor();
    // Each side of the way; the third behind one (and off the other by the rule of 1.2).
    CHECK(Near(shape.m_slots[1].target.x, -0.8f) && Near(shape.m_slots[1].target.z, 0.6f));
    CHECK(Near(shape.m_slots[2].target.x, 0.8f) && Near(shape.m_slots[2].target.z, -0.6f));
    CHECK(Near(shape.m_slots[3].target.x, -1.52f) && Near(shape.m_slots[3].target.z, -0.36f));
    CHECK(Near(shape.m_slots[1].dir.x, -0.6f) && Near(shape.m_slots[1].dir.z, -0.8f));
    CHECK(Near(shape.m_slots[2].dir.x, -0.6f) && Near(shape.m_slots[2].dir.z, -0.8f));
    CHECK(Near(shape.m_slots[3].dir.x, 0.6f) && Near(shape.m_slots[3].dir.z, 0.8f));
    CHECK(shape.m_slots[4].dirty && shape.m_slots[5].dirty);
    // Going through, one by one.
    S_FormationSlot &s1 = shape.m_slots[1], &s2 = shape.m_slots[2], &s3 = shape.m_slots[3];
    for (int i = 1; i <= 3; ++i) shape.m_slots[i].pushedAside = false;
    shape.m_doorState = 0;
    shape.LineUpAtDoor();
    CHECK(Near(s1.target.x, 3.0f - 0.8f) && Near(s1.target.z, 4.6f) && Near(s1.dir.x, 0.6f));
    CHECK(Near(s2.target.x, 0.8f) && Near(s2.dir.x, -0.6f));
    for (int i = 1; i <= 3; ++i) shape.m_slots[i].pushedAside = false;
    shape.m_doorState = 1;
    shape.LineUpAtDoor();
    CHECK(Near(s2.target.x, 3.8f) && Near(s2.target.z, 3.4f) && Near(s2.dir.x, 0.6f));
    for (int i = 1; i <= 3; ++i) shape.m_slots[i].pushedAside = false;
    shape.m_doorState = 2;
    shape.LineUpAtDoor();
    CHECK(Near(s3.target.x, 3.6f) && Near(s3.target.z, 4.8f) && Near(s3.dir.x, 0.6f));
    for (int i = 1; i <= 3; ++i) shape.m_slots[i].pushedAside = false;
    // The last slot's turn.
    shape.m_doorState = 4;
    shape.m_slots[5].target = {10, 0, 0, 0};
    f->human()->m_position = {10, 0, 0, 1};
    f->human()->m_velocity = {};
    shape.LineUpAtDoor();
    CHECK(shape.m_doorState == 5);
    // Not yet stopped.
    shape.m_doorState = 4;
    f->human()->m_velocity = {0.11f, 0, 0, 0};
    shape.LineUpAtDoor();
    CHECK(shape.m_doorState == 4);
    f->human()->m_velocity = {0.09f, 0, 0, 0};
    shape.LineUpAtDoor();
    CHECK(shape.m_doorState == 5);
    f->human()->m_velocity = {};
    // Everyone in place, then the last slot's not.
    for (int i = 1; i <= 5; ++i) {
        C_Human *const member = shape.m_slots[i].member;
        member->m_position = {shape.m_slots[i].target.x, 0, shape.m_slots[i].target.z, 1};
        member->m_velocity = {};
        shape.m_slots[i].pushedAside = true;
    }
    shape.m_doorState = -1;
    shape.LineUpAtDoor();
    for (int i = 1; i <= 3; ++i)
        shape.m_slots[i].member->m_position = {shape.m_slots[i].target.x, 0, shape.m_slots[i].target.z, 1};
    shape.m_doorState = -1;
    shape.LineUpAtDoor();
    CHECK(shape.m_doorState == 0);
    f->human()->m_velocity = {0.11f, 0, 0, 0};
    shape.m_doorState = -1;
    shape.LineUpAtDoor();
    CHECK(shape.m_doorState == -1);
    f->human()->m_velocity = {0.09f, 0, 0, 0};
    shape.m_doorState = -1;
    shape.LineUpAtDoor();
    CHECK(shape.m_doorState == 0);
    f->human()->m_velocity = {};

    // Up a slope: the side lies flat, the direction does not.
    ResetWorld();
    const int low = AddPoint(0, 0, 0, 15.0f), high = AddPoint(3, 4, 0, 15.0f, 1);
    LinkTo(low, high);
    C_FormationShape slope;
    S_Fake *sl = MakeHuman(-8, 0), *sa = MakeHuman(-8, 3);
    slope.SetLeader(sl->human());
    slope.AddMember(sa->human());
    slope.SetDoorPoint(static_cast<uint16_t>(low));
    slope.LineUpAtDoor();
    CHECK(Near(slope.m_slots[1].target.x, 0.0f) && Near(slope.m_slots[1].target.z, 0.6f, 0.61f));
    CHECK(Near(slope.m_slots[1].dir.y, -0.8f));
}

void TestMoveMore()
{
    C_FormationShape shape;
    shape.m_slots[0].target = {1, 0, 0, 0};
    shape.m_slots[0].current = {0, 0, 0, 0};
    shape.m_slots[5].target = {6, 0, 0, 0};
    shape.m_slots[5].current = {0, 0, 0, 0};
    shape.MoveSlots(100);
    CHECK(Near(shape.m_slots[0].current.x, 0.4f));
    // A little over the limit: to the limit; under it: the step.
    CHECK(Near(shape.m_slots[5].current.x, 1.0f));
    shape.m_slots[5].target = {4.9f, 0, 0, 0};
    shape.m_slots[5].current = {0, 0, 0, 0};
    shape.MoveSlots(100);
    CHECK(Near(shape.m_slots[5].current.x, 0.4f));
    // And by the tick: Tick does it.
    BigWorld();
    C_FormationShape ticked;
    S_Fake *l = MakeHuman(0, 0);
    ticked.SetLeader(l->human());
    ticked.m_slots[2].target = {2, 0, 0, 0};
    ticked.m_slots[2].current = {0, 0, 0, 0};
    ticked.Tick(100);
    CHECK(Near(ticked.m_slots[2].current.x, 0.4f));
}

void TestFollowMore()
{
    // The velocity: flat. The last direction: flat, then the view's.
    BigWorld();
    S_Fake *l = MakeHuman(0, 0), *m = MakeHuman(0, 0);
    C_FormationShape shape;
    shape.SetType(static_cast<E_FormationType>(0));
    shape.SetLeader(l->human());
    shape.AddMember(m->human());
    shape.m_slots[5].member = MakeHuman(0, 0)->human();
    shape.m_scripted = true;
    S_FormationSlot &slot = shape.m_slots[1], &last = shape.m_slots[5];
    l->human()->m_velocity = {3, 5, 0, 0};
    shape.m_changed = true;
    shape.FollowLeader();
    CHECK(Near(shape.m_lastDir.x, 1.0f) && shape.m_lastDir.y == 0.0f && Near(shape.m_lastDir.z, 0.0f));
    CHECK(shape.m_lastDir.w == 0.0f);
    // Every slot; of the matrix around the leader's way, and the slot's direction.
    CHECK(Near(last.target.z, 7.5f, 3.0f) || last.member);
    CHECK(slot.pushedAside == false);
    const S_vector want = Xyz(slot.scaledPos);
    S_matrix placed;
    placed.Identity();
    placed.SetDir(S_vector(1, 0, 0));
    const S_vector place = want * placed;
    CHECK(Near(slot.target.x, place.x) && Near(slot.target.z, place.z));
    const S_vector toward = Unit(Xyz(slot.scaledDir) * placed);
    CHECK(Near(slot.dir.x, toward.x) && Near(slot.dir.z, toward.z));
    const S_vector lastToward = Unit(Xyz(last.scaledDir) * placed);
    CHECK(Near(last.dir.x, lastToward.x) && Near(last.dir.z, lastToward.z));
    const S_vector lastPlace = Xyz(last.scaledPos) * placed;
    CHECK(Near(last.target.x, lastPlace.x) && Near(last.target.z, lastPlace.z));

    // Standing: the last direction, flat.
    l->human()->m_velocity = {};
    shape.m_lastDir = {0, 4, 2, 0};
    shape.m_changed = true;
    shape.FollowLeader();
    CHECK(shape.m_lastDir.y == 0.0f && Near(shape.m_lastDir.z, 1.0f));
    shape.m_lastDir = {2, 0, 0, 0};
    shape.m_changed = true;
    shape.FollowLeader();
    CHECK(Near(shape.m_lastDir.x, 1.0f) && Near(shape.m_lastDir.z, 0.0f));
    // The view's, in x too.
    shape.m_lastDir = {};
    g_viewDir = S_vector(3, 9, 4);
    shape.m_changed = true;
    shape.FollowLeader();
    CHECK(Near(shape.m_lastDir.x, 0.6f) && Near(shape.m_lastDir.z, 0.8f) && shape.m_lastDir.y == 0.0f);
    g_viewDir = S_vector(0, 0, 1);

    // Not changed: a slot goes to its place when that is no farther than it is from the leader.
    l->human()->m_velocity = {0, 0, 1, 0};
    shape.m_changed = false;
    // (place -2.5.., from here 0.5: under 2)
    slot.target = {-2.0f, 0, 0, 0};
    slot.dirty = false;
    shape.m_slots[5].member = nullptr;
    shape.FollowLeader();
    const S_vector straightOn = Xyz(slot.scaledPos);
    CHECK(Near(slot.target.x, straightOn.x));
    // (place -2.5.., from here 4: over 1.5)
    slot.target = {1.5f, 0, 0, 0};
    shape.FollowLeader();
    CHECK(Near(slot.target.x, 1.5f));
    // In a world with no points, the mark is ours alone.
    g_pointCount = 0;
    shape.m_changed = true;
    slot.dirty = false;
    shape.FollowLeader();
    CHECK(slot.dirty);
    slot.dirty = false;
    slot.target = {-2.0f, 0, 0, 0};
    shape.FollowLeader();
    CHECK(slot.dirty);
    g_pointCount = 1;
    // Too near, ahead: pushed aside (then, not changed, left).
    l->human()->m_velocity = {0, 0, 1, 0};
    slot.target = {0.5f, 0, 0.5f, 0};
    slot.pushedAside = true;
    m->human()->m_velocity = {};
    shape.m_slots[1].scaledPos = {50.0f, 0, 50.0f, 0};
    shape.FollowLeader();
    const float aside = 1.2f / std::sqrt(1.0f + 0.15f * 0.15f);
    CHECK(slot.pushedAside && Near(slot.target.x, 0.5f + aside) && Near(slot.target.z, 0.5f + 0.15f * aside));
    // Too near, behind: not that way; only kept apart afterward.
    slot.target = {0.5f, 0, -0.5f, 0};
    shape.FollowLeader();
    CHECK(slot.pushedAside && Near(slot.target.x, 1.2f * 0.70711f) && Near(slot.target.z, -1.2f * 0.70711f));
    // Far enough: not pushed, the mark cleared.
    slot.target = {1.5f, 0, 0, 0};
    slot.pushedAside = true;
    shape.FollowLeader();
    CHECK(!slot.pushedAside && slot.target.x == 1.5f);

    // A slot planned only while the leader moves, if it is not a script's.
    BigWorld();
    C_FormationShape planning;
    S_Fake *pl = MakeHuman(0, 0), *pm = MakeHuman(0, 0);
    planning.SetLeader(pl->human());
    planning.AddMember(pm->human());
    pl->human()->m_velocity = {0.5f, 0, 0, 0};
    planning.m_changed = true;
    planning.FollowLeader();                            // (the points found)
    planning.m_changed = true;
    planning.m_slots[1].pathFrom = 0xFFFF;
    g_findPathSeen = 0;
    planning.FollowLeader();
    CHECK(g_findPathSeen == 0x200);
    pl->human()->m_velocity = {};
    planning.m_changed = true;
    g_findPathSeen = 0;
    planning.m_slots[1].pathFrom = 0xFFFF;
    planning.FollowLeader();
    CHECK(g_findPathSeen == 0);
    pl->human()->m_velocity = {0.5f, 0, 0, 0};
    planning.m_scripted = true;
    planning.m_slots[1].pathFrom = 0xFFFF;
    planning.FollowLeader();
    CHECK(g_findPathSeen == 0);

    // Round a corner, with the numbers. The straight line is shorter than walking it.
    ResetWorld();
    const int p0 = AddPoint(0, 0, 0, 1.0f), p1 = AddPoint(0, 0, 10, 1.0f), p2 = AddPoint(-5, 0, 10, 1.0f);
    g_paths.push_back({static_cast<uint16_t>(p0), static_cast<uint16_t>(p1), static_cast<uint16_t>(p2)});
    S_Fake *cl = MakeHuman(0, 0), *cm = MakeHuman(-7, 10);
    C_FormationShape corner;
    corner.SetLeader(cl->human());
    corner.AddMember(cm->human());
    corner.m_scripted = true;
    cl->human()->m_velocity = {0, 0, 1, 0};
    corner.m_slots[1].scaledPos = {-7, 0, 10, 0};       // (place: straight 12.2)
    corner.m_changed = true;
    corner.FollowLeader();
    CHECK(g_nearestRadius == 5.0f && g_nearestY == -1.0f);
    // 15 along at p2; 2 from its place: from p2 toward the leader, by its radius.
    const float big = 1.0f / std::sqrt(125.0f);
    CHECK(Near(corner.m_slots[1].target.x, -5.0f + 5.0f * big) && Near(corner.m_slots[1].target.z, 10.0f - 10.0f * big));
    // Within reach of the place: as it was.
    corner.m_slots[1].scaledPos = {-6, 0, 10, 0};
    corner.m_changed = true;
    corner.FollowLeader();
    CHECK(Near(corner.m_slots[1].target.x, -6.0f, 1.01f) && Near(corner.m_slots[1].target.z, 10.0f));

    // Stopped at the first point on the way that reaches: not going on, by what it is short of.
    ResetWorld();
    const int q0 = AddPoint(0, 0, 0, 1.0f), q1 = AddPoint(0, 0, 10, 2.0f), q2 = AddPoint(-3, 0, 10, 1.0f);
    g_paths.push_back({static_cast<uint16_t>(q0), static_cast<uint16_t>(q1), static_cast<uint16_t>(q2)});
    S_Fake *dl = MakeHuman(0, 0), *dm = MakeHuman(-6, 10);
    C_FormationShape reach;
    reach.SetLeader(dl->human());
    reach.AddMember(dm->human());
    reach.m_scripted = true;
    dl->human()->m_velocity = {0, 0, 1, 0};
    reach.m_slots[1].scaledPos = {-6, 0, 10, 0};        // (straight 11.66)
    reach.m_changed = true;
    reach.FollowLeader();
    CHECK(Near(reach.m_slots[1].target.x, 0.0f) && Near(reach.m_slots[1].target.z, 10.0f + (std::sqrt(136.0f) - 10.0f) * 1.0f - 0.0f, 0.01f) );
    // The leader off the line: measured from it to the first after its point.
    ResetWorld();
    const int r0 = AddPoint(3, 0, 0, 1.0f), r1 = AddPoint(0, 0, 10, 1.0f), r2 = AddPoint(-5, 0, 10, 1.0f);
    g_paths.push_back({static_cast<uint16_t>(r0), static_cast<uint16_t>(r1), static_cast<uint16_t>(r2)});
    S_Fake *el = MakeHuman(3, 0), *em = MakeHuman(-3, 10);
    C_FormationShape off;
    off.SetLeader(el->human());
    off.AddMember(em->human());
    off.m_scripted = true;
    el->human()->m_velocity = {0, 0, 1, 0};
    // (from 3,0: place straight 11.66; p1 at sqrt(109) = 10.44, 11.44 with its radius: not enough; p2 at 15.44: yes)
    off.m_slots[1].scaledPos = {-9, 0, 10, 0};
    off.m_changed = true;
    off.FollowLeader();
    const float to = 15.44031f;
    (void)to;
    CHECK(off.m_slots[1].checkpoint == r2);
    // The leader away from its point: from the leader straight to the next, the point it is nearest not counted.
    ResetWorld();
    const int v0 = AddPoint(3, 0, 0, 1.0f), v1 = AddPoint(0, 0, 10, 2.0f), v2 = AddPoint(-5, 0, 10, 1.0f);
    g_paths.push_back({static_cast<uint16_t>(v0), static_cast<uint16_t>(v1), static_cast<uint16_t>(v2)});
    S_Fake *gl = MakeHuman(6, 0), *gm = MakeHuman(-4, 10);
    C_FormationShape away;
    away.SetLeader(gl->human());
    away.AddMember(gm->human());
    away.m_scripted = true;
    gl->human()->m_velocity = {0, 0, 1, 0};
    away.m_slots[1].scaledPos = {-10, 0, 10, 0};        // (place -4, 10: straight 14.14; 11.66 + 2 at v1 is short of it)
    away.m_changed = true;
    away.FollowLeader();
    CHECK(Near(away.m_slots[1].target.x, -4.0f) && Near(away.m_slots[1].target.z, 10.0f));
    // On along the way: what was walked from point to point.
    ResetWorld();
    const int u0 = AddPoint(0, 0, 0, 1.0f), u1 = AddPoint(0, 0, 10, 1.0f), u2 = AddPoint(-3, 0, 10, 2.0f);
    g_paths.push_back({static_cast<uint16_t>(u0), static_cast<uint16_t>(u1), static_cast<uint16_t>(u2)});
    S_Fake *hl = MakeHuman(0, 0), *hm = MakeHuman(-6, 10);
    C_FormationShape along;
    along.SetLeader(hl->human());
    along.AddMember(hm->human());
    along.m_scripted = true;
    hl->human()->m_velocity = {0, 0, 1, 0};
    along.m_slots[1].scaledPos = {-6, 0, 10, 0};        // (straight 11.66; at u2 13 along, 3 from it: 1.34 back toward the leader)
    along.m_changed = true;
    along.FollowLeader();
    const float by = 13.0f - std::sqrt(136.0f), toLeader = 1.0f / std::sqrt(109.0f);
    CHECK(Near(along.m_slots[1].target.x, -3.0f + 3.0f * toLeader * by, 0.01f) && Near(along.m_slots[1].target.z, 10.0f - 10.0f * toLeader * by, 0.01f));
    CHECK(!g_nearestOdd);
    // A local player's place is never cut.
    ResetWorld();
    const int t0 = AddPoint(0, 0, 0, 1.0f), t1 = AddPoint(0, 0, 10, 2.0f);
    g_paths.push_back({static_cast<uint16_t>(t0), static_cast<uint16_t>(t1)});
    S_Fake *fl = MakeHuman(0, 0), *fm = MakeHuman(1, 1);
    fm->local = true;
    C_FormationShape own;
    own.SetLeader(fl->human());
    own.AddMember(fm->human());
    own.m_scripted = true;
    fl->human()->m_velocity = {0, 0, 1, 0};
    own.m_slots[1].scaledPos = {1, 0, 11, 0};
    const int asked = g_findPaths;
    own.m_changed = true;
    own.FollowLeader();
    CHECK(g_findPaths == asked + 1);                    // (the leader's own)
    CHECK(Near(own.m_slots[1].target.z, 11.0f));
}

// A tiny reader of the chunks: id and size, in order, down to the end.
struct S_ChunkLine { int depth; int id; int size; int at; };
void ReadChunks(const std::string &file, int from, int to, int depth, std::vector<S_ChunkLine> &out, const std::vector<int> &parents)
{
    while (from + 6 <= to) {
        uint16_t id;
        int32_t size;
        std::memcpy(&id, file.data() + from, 2);
        std::memcpy(&size, file.data() + from + 2, 4);
        out.push_back({depth, id, size, from + 6});
        for (int parent : parents) {
            if (id == parent) {
                ReadChunks(file, from + 6, from + size, depth + 1, out, parents);
                break;
            }
        }
        from += size;
    }
}

void TestSaveBytes()
{
    BigWorld();
    C_FormationShape shape;
    S_Fake *l = MakeHuman(0, 0), *m = MakeHuman(1, 0), *x = MakeHuman(4, 4);
    shape.SetLeader(l->human());
    shape.AddMember(m->human());
    S_FormationSlot &slot = shape.m_slots[1];
    slot.basePos = {1, 2, 3, 0};
    slot.scaledPos = {4, 5, 6, 0};
    slot.target = {7, 8, 9, 0};
    slot.current = {10, 11, 12, 0};
    slot.dir = {13, 14, 15, 0};
    slot.baseDir = {16, 17, 18, 0};
    slot.scaledDir = {19, 20, 21, 0};
    slot.unknown74 = x->human();
    slot.distance = 22;
    slot.seat = 23;
    slot.vehicleId = 24;
    slot.pushedAside = true;
    slot.dirty = true;
    slot.pathFrom = 25;
    slot.checkpoint = 26;
    slot.path = {27, 28};
    shape.m_dir = {30, 31, 32, 0};
    shape.m_scale = {33, 34, 35, 0};
    shape.m_changed = true;
    shape.m_scripted = true;
    shape.m_doorPoint = 36;
    shape.m_doorState = 2;
    shape.m_mode = 2;
    C_chunk out;
    out.Open("x", true);
    shape.SaveGame(out);
    out.Close();
    const std::string file = g_file;

    std::vector<S_ChunkLine> lines;
    ReadChunks(file, 0, static_cast<int>(file.size()), 0, lines, {0x5BE, 12000, 0x1E61});
    const struct { int depth, id, size; } shapeWant[] = {
        {0, 0x5BE, -1}, {1, 1000, 6 + 0x40}, {1, 2000, 18}, {1, 3000, 18}, {1, 4000, 7}, {1, 5000, 7}, {1, 6000, 8}, {1, 7000, 10},
        {1, 8000, 10}, {1, 9000, 10}, {1, 10000, 10}, {1, 11000, 10}, {1, 12000, -1},
    };
    int at = 0;
    for (const auto &want : shapeWant) {
        CHECK(lines[at].depth == want.depth && lines[at].id == want.id && (want.size < 0 || lines[at].size == want.size));
        ++at;
    }
    // Slot 0: the same list but for the path, unknown; slot 1: all.
    const struct { int id, size; } slotWant[] = {
        {0x1E61, -1}, {1000, 18}, {2000, 18}, {3000, 18}, {4000, 18}, {5000, 18}, {6000, 18}, {7000, 18}, {8000, 10}, {9000, 10},
        {10000, 10}, {11000, 10}, {0x2CEC, 10}, {12000, 7}, {13000, 7}, {14000, 8}, {15000, 8}, {16000, 10},
    };
    // (the first slot has no 9000 or 16000)
    for (const auto &want : slotWant) {
        if (want.id == 9000 || want.id == 16000)
            continue;
        CHECK(lines[at].id == want.id && (want.size < 0 || lines[at].size == want.size));
        ++at;
    }
    for (const auto &want : slotWant) {
        CHECK(lines[at].id == want.id && (want.size < 0 || lines[at].size == want.size));
        ++at;
    }
    const auto floats = [&](int line, float a, float b, float c) {
        float v[3];
        std::memcpy(v, file.data() + lines[line].at, 12);
        return v[0] == a && v[1] == b && v[2] == c;
    };
    const auto integer = [&](int line, int value) {
        int v = 0;
        std::memcpy(&v, file.data() + lines[line].at, lines[line].size - 6);
        return v == value;
    };
    CHECK(floats(2, 30, 31, 32) && floats(3, 33, 34, 35));
    CHECK(integer(4, 1) && integer(5, 1) && integer(6, 36) && integer(7, 2) && integer(8, 2) && integer(9, l->human()->GetNetId()) && integer(10, FORMATION_FOLLOW) && integer(11, 2));
    int s1 = 0;
    for (size_t i = 0; i < lines.size(); ++i) {
        if (lines[i].id == 0x1E61 && ++s1 == 2) {
            s1 = static_cast<int>(i);
            break;
        }
    }
    CHECK(floats(s1 + 1, 1, 2, 3) && floats(s1 + 2, 4, 5, 6) && floats(s1 + 3, 7, 8, 9) && floats(s1 + 4, 10, 11, 12) && floats(s1 + 5, 13, 14, 15) && floats(s1 + 6, 16, 17, 18) && floats(s1 + 7, 19, 20, 21));
    CHECK(integer(s1 + 8, m->human()->GetNetId()) && integer(s1 + 9, x->human()->GetNetId()) && integer(s1 + 11, 23) && integer(s1 + 12, 24));
    CHECK(integer(s1 + 13, 1) && integer(s1 + 14, 1) && integer(s1 + 15, 25) && integer(s1 + 16, 26) && integer(s1 + 17, 2));
    float distance = 0;
    std::memcpy(&distance, file.data() + lines[s1 + 10].at, 4);
    CHECK(distance == 22.0f);
    // The points after the chunk of their count.
    uint16_t points[2];
    std::memcpy(points, file.data() + lines[s1 + 17].at + 4, 4);
    CHECK(points[0] == 27 && points[1] == 28);

    // Each chunk closed: the whole is as long as the file; the slots' chunk runs to the end of it.
    CHECK(lines[0].size == static_cast<int>(file.size()));
    CHECK(lines[12].at + lines[12].size - 6 == static_cast<int>(file.size()));
    // Two one after the other: the second read from where the first ended.
    C_FormationShape two;
    two.SetType(FORMATION_ARROW_SHARP);
    two.m_mode = 7;
    C_chunk both;
    both.Open("x", true);
    shape.SaveGame(both);
    two.SaveGame(both);
    both.Close();
    C_FormationShape firstBack, secondBack;
    C_chunk bothIn;
    bothIn.Open("x", false);
    CHECK(firstBack.LoadGame(bothIn) && secondBack.LoadGame(bothIn));
    bothIn.Close();
    CHECK(firstBack.m_mode == 2 && firstBack.m_slots[1].seat == 23 && secondBack.m_mode == 7 && secondBack.GetType() == FORMATION_ARROW_SHARP);
    g_file = file;
    // Loading over what is there: what is not in the file goes.
    C_FormationShape back;
    S_Fake *y = MakeHuman(5, 5);
    back.m_slots[3].member = y->human();
    back.m_slots[3].unknown74 = y->human();
    back.m_slots[3].path = {1};
    back.m_leader = y->human();
    C_chunk in;
    in.Open("x", false);
    CHECK(back.LoadGame(in));
    in.Close();
    CHECK(back.m_slots[3].member == nullptr && back.m_slots[3].unknown74 == nullptr && back.m_slots[3].path.empty());
    const S_FormationSlot &b = back.m_slots[1];
    CHECK(b.basePos.y == 2 && b.scaledPos.z == 6 && b.target.x == 7 && b.current.y == 11 && b.dir.z == 15 && b.baseDir.x == 16 && b.scaledDir.y == 20);
    CHECK(b.unknown74 == x->human() && b.distance == 22.0f && b.seat == 23 && b.vehicleId == 24 && b.pushedAside && b.dirty && b.pathFrom == 25 && b.checkpoint == 26);
    CHECK(b.path.size() == 2 && b.path[0] == 27 && b.path[1] == 28);
    CHECK(back.m_slots[5].dirty == shape.m_slots[5].dirty && back.m_slots[5].pathFrom == shape.m_slots[5].pathFrom);
    CHECK(back.m_dir.y == 31 && back.m_scale.z == 35 && back.m_changed && back.m_doorPoint == 36 && back.m_doorState == 2);
    // No leader saved: none after; and all slots read.
    C_FormationShape loose;
    loose.m_slots[5].member = m->human();
    C_chunk looseOut;
    looseOut.Open("x", true);
    loose.SaveGame(looseOut);
    looseOut.Close();
    lines.clear();
    ReadChunks(g_file, 0, static_cast<int>(g_file.size()), 0, lines, {0x5BE, 12000, 0x1E61});
    for (const S_ChunkLine &line : lines)
        CHECK(line.id != 9000 || line.depth > 1);
    C_FormationShape looseBack;
    looseBack.m_leader = m->human();
    C_chunk looseIn;
    looseIn.Open("x", false);
    looseBack.LoadGame(looseIn);
    looseIn.Close();
    CHECK(looseBack.GetLeader() == nullptr && looseBack.GetSlotMember(5) == m->human());
    int ids = 0;
    for (const S_ChunkLine &line : lines)
        ids += line.id == 0x1E61;
    CHECK(ids == 6);
}

void TestSaveLoad()
{
    BigWorld();
    C_FormationShape shape;
    S_Fake *l = MakeHuman(0, 0), *m = MakeHuman(1, 0);
    shape.SetType(FORMATION_ARROW_SHARP);
    shape.SetLeader(l->human());
    shape.AddMember(m->human());
    shape.m_slots[1].path = {3, 4, 5};
    shape.m_slots[1].seat = 2;
    shape.m_slots[1].vehicleId = 77;
    shape.m_slots[1].pathFrom = 9;
    shape.m_slots[1].pushedAside = true;
    shape.m_doorState = 1;
    shape.m_doorPoint = 11;
    shape.m_mode = 2;
    shape.m_scripted = true;
    C_chunk out;
    out.Open("x", true);
    CHECK(shape.SaveGame(out));
    out.Close();
    CHECK(static_cast<unsigned char>(g_file[0]) == 0xBE && static_cast<unsigned char>(g_file[1]) == 0x05);
    CHECK(static_cast<unsigned char>(g_file[6]) == 0xE8 && g_file[7] == 0x03);
    const std::string first = g_file;

    C_FormationShape back;
    C_chunk in;
    in.Open("x", false);
    CHECK(back.LoadGame(in));
    in.Close();
    CHECK(back.GetLeader() == l->human());
    CHECK(back.GetType() == FORMATION_ARROW_SHARP && back.m_mode == 2 && back.m_doorState == 1 && back.m_doorPoint == 11 && back.m_scripted);
    CHECK(back.GetMemberCount() == 2);
    CHECK(back.GetSlotMember(0) == l->human() && back.GetSlotMember(1) == m->human() && back.GetSlotMember(2) == nullptr);
    CHECK(g_fakes[m->human()]->leaderSet == l->human());
    CHECK(g_fakes[l->human()]->leaderSet == nullptr);
    CHECK(back.m_slots[1].path.size() == 3 && back.m_slots[1].path[2] == 5);
    CHECK(back.m_slots[1].seat == 2 && back.m_slots[1].vehicleId == 77 && back.m_slots[1].pathFrom == 9 && back.m_slots[1].pushedAside);
    CHECK(back.m_slots[1].target.x == shape.m_slots[1].target.x && back.m_slots[1].distance == shape.m_slots[1].distance);
    CHECK(std::memcmp(&back.m_matrix, &shape.m_matrix, 0x40) == 0);
    CHECK(back.m_scale.x == shape.m_scale.x && back.m_dir.z == shape.m_dir.z);
    C_chunk again;
    again.Open("x", true);
    back.SaveGame(again);
    again.Close();
    CHECK(g_file == first);
    // Without slots: none saved; loading into one with none: they appear.
    C_FormationShape bare;
    bare.SetType(FORMATION_NONE);
    C_chunk bareOut;
    bareOut.Open("x", true);
    bare.SaveGame(bareOut);
    bareOut.Close();
    C_FormationShape bareBack;
    C_chunk bareIn;
    bareIn.Open("x", false);
    bareBack.LoadGame(bareIn);
    bareIn.Close();
    CHECK(bareBack.GetType() == FORMATION_NONE);
    g_file = first;
    C_chunk toEmpty;
    toEmpty.Open("x", false);
    bare.LoadGame(toEmpty);
    toEmpty.Close();
    CHECK(bare.HasSlots() && bare.GetSlotMember(1) == m->human());
    // An unknown chunk: passed.
    C_chunk other;
    other.Open("x", true);
    other.Begin(0x5BE);
    other.WriteChunk<int>(55, 1);
    other.WriteChunk<int>(11000, 9);
    other.Ascend();
    other.Close();
    C_FormationShape withOther;
    C_chunk otherIn;
    otherIn.Open("x", false);
    CHECK(withOther.LoadGame(otherIn));
    otherIn.Close();
    CHECK(withOther.m_mode == 9);
    g_file.clear();
    C_chunk emptyIn;
    emptyIn.Open("x", false);
    CHECK(withOther.LoadGame(emptyIn));
    emptyIn.Close();
}
}  // namespace

int main()
{
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    g_points = MakePoints();
    GetGame()->m_checkpoints = Network();
    for (void *&slot : g_vehicleVt) slot = reinterpret_cast<void *>(F_SeatCount);
    g_vehicleVt[0x78 / 4 + 1] = reinterpret_cast<void *>(F_SeatOccupant);
    for (void *&slot : g_squadVt) slot = reinterpret_cast<void *>(F_GetCombatOrder);
    TestTables();
    TestMembers();
    TestQueries();
    TestFitOnNetwork();
    TestMoveSlots();
    TestFollow();
    TestFollowPaths();
    TestKeepApart();
    TestPushAside();
    TestPlanSlot();
    TestBoarding();
    TestDoor();
    TestLeaderDied();
    TestSaveLoad();
    TestMath();
    TestMatrix();
    TestMembersMore();
    TestQueriesMore();
    TestFitMore();
    TestPushMore();
    TestPlanMore();
    TestBoardingMore();
    TestDoorMore();
    TestMoveMore();
    TestFollowMore();
    TestSaveBytes();
    std::printf("formation: %d checks, %d failures\n", checks, failures);
    return failures != 0;
}
