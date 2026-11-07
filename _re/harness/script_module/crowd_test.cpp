// The humans keeping out of each other's way (part 12a: game/human_crowd.cpp). MinGW; build_crowd.bat; run crowd_test.exe ("crowd: N checks, 0 failures").
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
#include "game/ai_parts.h"
#include "game/car.h"
#include "game/checkpoints.h"
#include "game/game.h"
#include "game/human.h"
#include "game/human_controller.h"
#include "game/human_crowd.h"
#include "game/human_movement.h"
#include "game/pending.h"
#include "game/profile.h"
#include "ls3d/i3d.h"
#include "ls3d/rw_data.h"
#undef private
#undef protected

namespace crowd { S_vector CarHalfSize(const C_Car *car); }

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
void __stdcall S_quat::Normalize() {}
void __stdcall S_matrix::SetRot3(const S_quat &) {}
S_Checkpoint::~S_Checkpoint() {}

uint32_t g_gameRole = 7;
bool g_coop;
bool C_GameProfile::IsCooperativeGame() const { return g_coop; }

namespace {
// ---- the network: points on a line, each linked to its neighbours; or as a test wants ----
struct S_Net {
    S_Checkpoint points[32];
    S_CheckpointLink links[32][8];
    int count = 0;
};
S_Net g_net;
alignas(16) unsigned char g_checkpointBytes[0x4000];
C_Checkpoints *Checkpoints() { return reinterpret_cast<C_Checkpoints *>(g_checkpointBytes); }
int AddPoint(float x, float z, float radius, uint8_t type = 0)
{
    const int index = g_net.count++;
    Checkpoints()->m_pointCount = static_cast<uint16_t>(g_net.count);
    S_Checkpoint &point = g_net.points[index];
    point.flags = 0;
    point.type = 0;
    point.pos = S_vector(x, 0.0f, z);
    point.radius = static_cast<uint8_t>(radius * 16.0f);
    point.type = type;
    point.links = g_net.links[index];
    return index;
}
void Link(int a, int b)
{
    S_Checkpoint &pa = g_net.points[a], &pb = g_net.points[b];
    pa.links[pa.flags & CHECKPOINT_LINK_COUNT].target = static_cast<uint16_t>(b);
    pa.flags++;
    pb.links[pb.flags & CHECKPOINT_LINK_COUNT].target = static_cast<uint16_t>(a);
    pb.flags++;
}
void ClearNet()
{
    g_net.count = 0;
    Checkpoints()->m_points = g_net.points;
}
}  // namespace

S_Checkpoint *C_CheckpointNetwork::Get(int index) { return index >= 0 && index < g_net.count ? &g_net.points[index] : nullptr; }
int C_CheckpointNetwork::FindNearestPoint(S_vector pos, float, float) const
{
    int best = 0xFFFF;
    float bestSq = 1e20f;
    for (int i = 0; i < g_net.count; ++i) {
        const float dx = g_net.points[i].pos.x - pos.x, dz = g_net.points[i].pos.z - pos.z;
        if (dx * dx + dz * dz < bestSq) {
            bestSq = dx * dx + dz * dz;
            best = i;
        }
    }
    return best;
}

// ---- waypoints ----
S_AIWaypoint *AllocWaypoint(int checkpoint, const S_vector4 &pos, uint16_t flags, uint8_t type, float radius)
{
    S_AIWaypoint *waypoint = new S_AIWaypoint();
    waypoint->checkpoint = checkpoint;
    waypoint->pos = pos;
    waypoint->flags = flags;
    waypoint->type = type;
    waypoint->radius = radius;
    return waypoint;
}
void ReleaseWaypoint(S_AIWaypoint *waypoint) { delete waypoint; }

namespace {
// ---- a human ----
struct S_FakeController {
    void **vtable;
    unsigned char pad[0x400];
    std::vector<S_AIWaypoint *> *route;
    bool busy;
};
void *g_controllerVt[128];
std::vector<S_AIWaypoint *> *__thiscall F_GetWaypoints(S_FakeController *self) { return self->route; }
bool __thiscall F_Unknown108(S_FakeController *self) { return self->busy; }

struct S_Fake {
    alignas(16) unsigned char frameBytes[0x400];
    alignas(16) unsigned char humanBytes[0x2000];
    S_FakeController controller;
    C_HumanAIMemory *memory;
    std::vector<S_AIWaypoint *> route;
    std::vector<C_Actor *> nearby;
    int stance = 0, moveClass = 0;
    uint32_t moveMode = 1;
    bool order = false, unconscious = false, suspended = false, ctrl104 = true;
    int climb = 0;
    bool work5 = false;
    C_Human *human() { return reinterpret_cast<C_Human *>(humanBytes); }
    I3D_frame *frame() { return reinterpret_cast<I3D_frame *>(frameBytes); }
};
std::map<const C_Human *, S_Fake *> g_fakes;
std::map<const C_HumanEvents *, S_Fake *> g_eventOwners;
std::vector<C_Actor *> g_active;
alignas(16) unsigned char g_eventsPool[64][0x100];
int g_eventsUsed;

void SetFramePos(unsigned char *frameBytes, float x, float y, float z)
{
    float *matrix = reinterpret_cast<float *>(frameBytes + 0x40);
    for (int i = 0; i < 16; ++i) matrix[i] = 0;
    for (int i = 0; i < 4; ++i) matrix[i * 5] = 1.0f;
    matrix[12] = x; matrix[13] = y; matrix[14] = z;
    float *local = reinterpret_cast<float *>(frameBytes + 0x80);
    for (int i = 0; i < 4; ++i) local[i * 5] = 1.0f;
    reinterpret_cast<I3D_frame *>(frameBytes)->m_frameFlags = 0x20 | 0x10;
}
void ResetAll()
{
    g_fakes.clear();
    g_eventOwners.clear();
    g_active.clear();
    g_eventsUsed = 0;
    ClearNet();
    g_gameRole = 7;
    g_coop = false;
}
S_Fake *MakeHuman(float x, float z, int stance = 0, int type = 0)
{
    S_Fake *fake = new S_Fake();
    std::memset(fake->frameBytes, 0, sizeof(fake->frameBytes));
    std::memset(fake->humanBytes, 0, sizeof(fake->humanBytes));
    SetFramePos(fake->frameBytes, x, 0.0f, z);
    C_Human *human = fake->human();
    human->m_type = ACTOR_HUMAN;
    human->m_frame = fake->frame();
    human->m_stats[0] = 100;
    fake->memory = new C_HumanAIMemory(human);
    human->m_aiMemory = fake->memory;
    fake->controller.vtable = g_controllerVt;
    reinterpret_cast<C_HumanController *>(&fake->controller)->m_type = static_cast<E_HumanController>(type);
    fake->controller.route = nullptr;
    fake->controller.busy = false;
    human->m_controller = reinterpret_cast<C_HumanController *>(&fake->controller);
    C_HumanEvents *events = reinterpret_cast<C_HumanEvents *>(g_eventsPool[g_eventsUsed++]);
    human->m_events = events;
    g_eventOwners[events] = fake;
    fake->stance = stance;
    g_fakes[human] = fake;
    g_active.push_back(human);
    return fake;
}
void SetVelocity(S_Fake *fake, float x, float z)
{
    fake->human()->m_velocity = {x, 0.0f, z, 0.0f};
}
void Meet(S_Fake *a, S_Fake *b)
{
    a->nearby.push_back(b->human());
    b->nearby.push_back(a->human());
}
void GiveRoute(S_Fake *fake, std::initializer_list<int> points)
{
    for (int index : points) {
        const S_vector &pos = g_net.points[index].pos;
        fake->route.push_back(AllocWaypoint(index, {pos.x, pos.y, pos.z, 0.0f}, 0, 0, 1.0f));
    }
    fake->controller.route = &fake->route;
}

// ---- a car ----
struct S_FakeCar {
    alignas(16) unsigned char frameBytes[0x400];
    alignas(16) unsigned char carBytes[0x3000];
    C_Car *car() { return reinterpret_cast<C_Car *>(carBytes); }
};
S_FakeCar *MakeCar(float x, float z, float halfX = 1.0f, float halfZ = 2.0f, float speed = 0.0f)
{
    S_FakeCar *fake = new S_FakeCar();
    std::memset(fake->frameBytes, 0, sizeof(fake->frameBytes));
    std::memset(fake->carBytes, 0, sizeof(fake->carBytes));
    SetFramePos(fake->frameBytes, x, 0.0f, z);
    C_Car *car = fake->car();
    car->m_type = ACTOR_CAR;
    car->m_frame = reinterpret_cast<I3D_frame *>(fake->frameBytes);
    float *box = reinterpret_cast<float *>(fake->carBytes + 0x210);
    const float kx = halfX / 0.55f, kz = halfZ / 0.55f;
    box[0] = -kx / 2; box[1] = -1.0f; box[2] = -kz / 2;
    box[4] = kx / 2; box[5] = 1.0f; box[6] = kz / 2;
    car->m_vehicle.m_speed = speed;
    g_active.push_back(reinterpret_cast<C_Actor *>(car));
    return fake;
}

float Length(const float *step) { return std::sqrt(step[0] * step[0] + step[1] * step[1] + step[2] * step[2]); }
}  // namespace

// ---- what the crowd asks of a human ----
int C_Human::GetStance() const { return g_fakes[this]->stance; }
int C_Human::GetMoveSpeedClass() const { return g_fakes[this]->moveClass; }
uint32_t C_Human::GetMoveMode() const { return g_fakes[this]->moveMode; }
bool C_Human::HasOrder() const { return g_fakes[this]->order; }
bool C_Human::IsUnconscious() const { return g_fakes[this]->unconscious; }
bool C_Human::IsAISuspended() const { return g_fakes[this]->suspended; }
bool C_Human::ControllerUnknown104() const { return g_fakes[this]->ctrl104; }
int S_HumanBody::GetClimbState() const
{
    for (auto &entry : g_fakes)
        if (&entry.first->m_body == this) return entry.second->climb;
    return 0;
}
bool S_HumanBody::IsWorkState5() const
{
    for (auto &entry : g_fakes)
        if (&entry.first->m_body == this) return entry.second->work5;
    return false;
}
C_Actor *C_HumanEvents::GetNearbyActor(int index) const { return g_eventOwners[this]->nearby[index]; }
int FakeNearbyCount(const C_HumanEvents *events) { return static_cast<int>(g_eventOwners[events]->nearby.size()); }

alignas(16) unsigned char g_gameBytes[0x400];
C_game *GetGame() { return reinterpret_cast<C_game *>(g_gameBytes); }

namespace {
void SyncNearbyCounts()
{
    // (GetNearbyActorCount is the size of the real vector: kept the same length.)
    for (auto &entry : g_eventOwners) {
        C_HumanEvents *events = const_cast<C_HumanEvents *>(entry.first);
        auto &ids = events->m_nearbyActors;
        new (&ids) std::vector<int>(entry.second->nearby.size());
    }
}
void Tick(int ms = 100)
{
    SyncNearbyCounts();
    HumanPass_Tick(ms, g_active);
}

void TestHelpers()
{
    using crowd::AddPush;
    S_vector4 total = {};
    AddPush(total, S_vector(1, 0, 0));
    CHECK(total.x == 1 && total.z == 0);                // (first: as it is)
    AddPush(total, S_vector(0, 0, 2));
    CHECK(Near(total.x, 1) && Near(total.z, 2));        // (across: added)
    S_vector4 opposite = {1, 0, 0, 0};
    AddPush(opposite, S_vector(-1, 0, 1));
    CHECK(Near(opposite.x, 0) && Near(opposite.z, 1));  // (against: added)
    S_vector4 less = {2, 0, 0, 0};
    AddPush(less, S_vector(1, 0, 1));
    CHECK(Near(less.x, 2.0f) && Near(less.z, 1));       // (along, shorter: only what it adds across)
    S_vector4 more = {1, 0, 0, 0};
    AddPush(more, S_vector(3, 0, 0));
    CHECK(Near(more.x, 3.0f));                          // (along, longer: the longer)
    S_vector4 more2 = {1, 0, 0, 0};
    AddPush(more2, S_vector(2, 0, 2));
    // along = 2: back = 2 * 1 / 8 = .25: (1 + 2 - .5, 2 - .5)
    CHECK(Near(more2.x, 2.5f) && Near(more2.z, 1.5f));

    using crowd::KeepInsideStrip;
    const S_vector a(0, 0, 0), b(10, 0, 0);
    S_vector p(5, 0, 0.5f);
    CHECK(KeepInsideStrip(p, a, 1.0f, b, 1.0f));
    CHECK(p.x == 5 && p.z == 0.5f);
    p = S_vector(5, 0, 3);
    CHECK(!KeepInsideStrip(p, a, 1.0f, b, 1.0f));
    CHECK(Near(p.x, 5) && Near(p.z, 1));
    // Widening: the radius goes with where along it is.
    p = S_vector(5, 0, 3);
    CHECK(!KeepInsideStrip(p, a, 1.0f, b, 3.0f));
    CHECK(Near(p.z, 2));
    p = S_vector(5, 0, 1.9f);
    CHECK(KeepInsideStrip(p, a, 1.0f, b, 3.0f));
    // In either circle.
    p = S_vector(10.5f, 0, 0.5f);
    CHECK(KeepInsideStrip(p, a, 1.0f, b, 1.0f));
    p = S_vector(-0.5f, 0, 0.5f);
    CHECK(KeepInsideStrip(p, a, 1.0f, b, 1.0f));
    // Beyond each end: to its circle.
    p = S_vector(-3, 0, 0);
    CHECK(!KeepInsideStrip(p, a, 1.0f, b, 1.0f));
    CHECK(Near(p.x, -1) && Near(p.z, 0));
    p = S_vector(13, 0, 0);
    CHECK(!KeepInsideStrip(p, a, 1.0f, b, 1.0f));
    CHECK(Near(p.x, 11));
    // Height does not count; a strip of no length: the first circle.
    p = S_vector(5, 7, 0.5f);
    CHECK(KeepInsideStrip(p, a, 1.0f, b, 1.0f) && p.y == 7);
    p = S_vector(3, 0, 0);
    CHECK(!KeepInsideStrip(p, a, 1.0f, S_vector(0, 0, 0), 1.0f));
    CHECK(Near(p.x, 1));

    using crowd::SortCorners;
    S_vector corners[4];
    int negative[4], positive[4], nc = 0, pc = 0;
    SortCorners(S_vector(0, 0, -2), S_vector(1, 0, 0), S_vector(0, 0, 1), corners, negative, positive, &nc, &pc);
    CHECK(pc == 2 && nc == 2);
    // Corner 0 (1, 1), 1 (1, -1): the positive side, the nearer along z first.
    CHECK(positive[0] == 1 && positive[1] == 0);
    CHECK(negative[0] == 3 && negative[1] == 2);
    CHECK(Near(corners[0].x, 1) && Near(corners[0].z, 3) && Near(corners[3].x, -1) && Near(corners[3].z, 1));
    SortCorners(S_vector(0, 0, 0), S_vector(1, 0, 0), S_vector(0, 0, -1), corners, negative, positive, &nc, &pc);
    CHECK(positive[0] == 0 && positive[1] == 1 && negative[0] == 2 && negative[1] == 3);
    SortCorners(S_vector(5, 0, 0), S_vector(1, 0, 0), S_vector(1, 0, 0), corners, negative, positive, &nc, &pc);
    CHECK(pc == 0 && nc == 4);
    CHECK(negative[0] == 2 || negative[0] == 3);
    SortCorners(S_vector(-5, 0, 0), S_vector(1, 0, 0), S_vector(1, 0, 0), corners, negative, positive, &nc, &pc);
    CHECK(pc == 4 && nc == 0);
}

void TestKeepOnNetwork()
{
    ResetAll();
    const int a = AddPoint(0, 0, 1.0f), b = AddPoint(10, 0, 1.0f), c = AddPoint(0, 10, 1.0f);
    Link(a, b);
    Link(a, c);
    const C_CheckpointNetwork *network = Checkpoints();
    const S_Checkpoint *point = nullptr;
    S_vector4 pos = {0.5f, 0, 0, 0};
    // No point: false, nothing.
    CHECK(!crowd::KeepOnNetwork(network, pos, nullptr, &point));
    CHECK(point == nullptr);
    // In the circle.
    CHECK(crowd::KeepOnNetwork(network, pos, &g_net.points[a], &point));
    CHECK(point == &g_net.points[a]);
    // In a strip: the nearer end.
    pos = {3, 0, 0.5f, 0};
    CHECK(crowd::KeepOnNetwork(network, pos, &g_net.points[a], &point));
    CHECK(point == &g_net.points[a]);
    pos = {7, 0, 0.5f, 0};
    CHECK(crowd::KeepOnNetwork(network, pos, &g_net.points[a], &point));
    CHECK(point == &g_net.points[b]);
    CHECK(pos.x == 7 && pos.z == 0.5f);
    // Off everything: the nearest place, which point.
    pos = {3, 0, 2, 0};
    CHECK(!crowd::KeepOnNetwork(network, pos, &g_net.points[a], &point));
    CHECK(Near(pos.x, 3) && Near(pos.z, 1));
    CHECK(point == &g_net.points[b]);                   // (of the strip toward b)
    pos = {2, 0, 3, 0};
    CHECK(!crowd::KeepOnNetwork(network, pos, &g_net.points[a], &point));
    CHECK(Near(pos.x, 1) && Near(pos.z, 3));
    CHECK(point == &g_net.points[c]);
    // Nothing linked: its circle.
    const int lone = AddPoint(50, 50, 2.0f);
    pos = {55, 0, 50, 0};
    CHECK(!crowd::KeepOnNetwork(network, pos, &g_net.points[lone], &point));
    CHECK(Near(pos.x, 52) && point == &g_net.points[lone]);
}

void TestFindPointNear()
{
    ResetAll();
    const int a = AddPoint(0, 0, 1.0f), b = AddPoint(10, 0, 1.0f), c = AddPoint(20, 0, 1.0f), d = AddPoint(30, 0, 1.0f), side = AddPoint(10, 10, 1.0f, 4), beyond = AddPoint(10, 20, 1.0f);
    Link(a, b);
    Link(b, c);
    Link(c, d);
    Link(b, side);
    Link(side, beyond);
    const C_CheckpointNetwork *network = Checkpoints();
    C_HumanCrowd &crowd = g_humanCrowd;
    const S_Checkpoint *point = nullptr;
    CHECK(crowd.FindPointNear(network, &point, S_vector(0, 0, 0), 4, nullptr) == 0);
    point = &g_net.points[a];
    CHECK(crowd.FindPointNear(network, &point, S_vector(0.5f, 0, 0), 4, nullptr) == 1);
    CHECK(point == &g_net.points[a]);
    // Two links on: found.
    CHECK(crowd.FindPointNear(network, &point, S_vector(20, 0, 0.5f), 4, nullptr) == 1);
    CHECK(point == &g_net.points[c]);
    // Not within the depth: the nearest seen.
    point = &g_net.points[a];
    CHECK(crowd.FindPointNear(network, &point, S_vector(30, 0, 0), 1, nullptr) == 2);
    CHECK(point == &g_net.points[b]);
    // Outside all, within the strip's reach: the nearer of its ends.
    point = &g_net.points[a];
    CHECK(crowd.FindPointNear(network, &point, S_vector(7, 0, 3), 4, nullptr) == 2);
    CHECK(point == &g_net.points[b]);
    // Types 3..6 are not gone through.
    point = &g_net.points[b];
    CHECK(crowd.FindPointNear(network, &point, S_vector(10, 0, 20), 4, nullptr) == 2);
    CHECK(point == &g_net.points[side]);
    // The hint: at it, it is; in the strip between, the nearer.
    point = &g_net.points[a];
    CHECK(crowd.FindPointNear(network, &point, S_vector(10, 0, 0), 4, &g_net.points[b]) == 1);
    CHECK(point == &g_net.points[b]);
    point = &g_net.points[a];
    CHECK(crowd.FindPointNear(network, &point, S_vector(3, 0, 0), 4, &g_net.points[b]) == 1);
    CHECK(point == &g_net.points[a]);
    point = &g_net.points[a];
    CHECK(crowd.FindPointNear(network, &point, S_vector(7, 0, 0), 4, &g_net.points[b]) == 1);
    CHECK(point == &g_net.points[b]);
    // A hint not linked to it: ignored.
    point = &g_net.points[a];
    CHECK(crowd.FindPointNear(network, &point, S_vector(5, 0, 0), 1, &g_net.points[d]) == 2);
}

void TestAlone()
{
    ResetAll();
    AddPoint(0, 0, 3.0f);
    AddPoint(10, 0, 3.0f);
    Link(0, 1);
    S_Fake *fake = MakeHuman(0, 0);
    GiveRoute(fake, {1});
    Tick();
    C_HumanAIMemory &memory = *fake->memory;
    CHECK(memory.stepping);
    CHECK(Near(memory.step[0], 1.4f * 2.0f, 0.01f) && Near(memory.step[2], 0.0f));         // (the speeds' table says 2 for class 0, stance 0)
    CHECK(Near(memory.pos.x, 0) && Near(memory.target.x, 10));
    CHECK(Near(memory.lastGoal.x, 0));
    CHECK(Near(memory.routeStart.x, 10));
    // Again: the goal stays.
    Tick();
    CHECK(Near(memory.lastGoal.x, 0));
    // Without a route: standing, nothing asked; the marks where it is.
    ResetAll();
    S_Fake *stand = MakeHuman(3, 4);
    SetVelocity(stand, 1, 0);
    Tick();
    CHECK(!stand->memory->stepping && Length(stand->memory->step) == 0);
    CHECK(stand->memory->routeChanged && Near(stand->memory->target.x, 3) && Near(stand->memory->routeStart.z, 4) && Near(stand->memory->lastGoal.x, 3));
    // On the way the point is passed to the next waypoint within reach.
    ResetAll();
    AddPoint(0, 0, 3.0f);
    AddPoint(0.01f, 0, 3.0f);
    AddPoint(10, 0, 3.0f);
    S_Fake *pass = MakeHuman(0, 0);
    GiveRoute(pass, {1, 2});
    pass->memory->lastGoal = {-1.0f, 0, 0, 0};
    pass->memory->routeStart = {-1.0f, 0, 0, 0};
    Tick();
    CHECK(pass->memory->waypoint == 1);
    CHECK(Near(pass->memory->target.x, 10));
    // Lost marks are taken from where it is; when the route changed, the goal from the last start.
    ResetAll();
    AddPoint(5, 0, 3.0f);
    S_Fake *mark = MakeHuman(1, 0);
    GiveRoute(mark, {0});
    mark->memory->routeChanged = true;
    mark->memory->routeStart = {9.0f, 0, 0, 0};
    mark->memory->lastGoal = {8.0f, 0, 0, 0};
    Tick();
    CHECK(Near(mark->memory->lastGoal.x, 9) && !mark->memory->routeChanged);
    // Out of play: nothing worked out.
    struct { void (*set)(S_Fake *); uint8_t bit; } outs[] = {
        {[](S_Fake *f) { f->human()->m_stats[0] = 0; }, 1},
        {[](S_Fake *f) { f->unconscious = true; }, 2},
        {[](S_Fake *f) { f->order = true; }, 8},
        {[](S_Fake *f) { f->human()->m_vehicle = reinterpret_cast<C_Actor *>(f); }, 0x10},
        {[](S_Fake *f) { f->climb = 1; }, 0x20},
    };
    for (auto &out : outs) {
        ResetAll();
        S_Fake *f = MakeHuman(0, 0);
        f->memory->stepping = true;
        out.set(f);
        Tick();
        CHECK(!f->memory->stepping);
        CHECK(f->memory->flags == out.bit);
    }
    // Nobody at all: fine. Not for a client without co-op; for co-op too.
    ResetAll();
    Tick();
    AddPoint(9, 0, 3.0f);
    S_Fake *client = MakeHuman(0, 0);
    GiveRoute(client, {0});
    g_gameRole = 2;
    Tick();
    CHECK(!client->memory->stepping);
    g_coop = true;
    Tick();
    CHECK(client->memory->stepping);
    g_gameRole = 7;
    g_coop = false;
}

// Two humans nearby go through the solver.
void Pair(S_Fake **a, S_Fake **b, float ax, float bx, int stanceB = 0)
{
    ResetAll();
    for (int i = 0; i < 5; ++i)
        AddPoint(-20.0f + 10.0f * i, 0, 8.0f);
    for (int i = 0; i < 4; ++i)
        Link(i, i + 1);
    *a = MakeHuman(ax, 0);
    *b = MakeHuman(bx, 0, stanceB);
    Meet(*a, *b);
}

void TestPushes()
{
    S_Fake *a, *b;
    // Too close and on their way: apart along the line between them.
    Pair(&a, &b, 0.0f, 0.4f);
    GiveRoute(a, {4});
    GiveRoute(b, {0});
    Tick();
    CHECK(a->memory->stepping && b->memory->stepping);
    CHECK(a->memory->step[0] < 0 && b->memory->step[0] > 0);
    CHECK(Near(a->memory->step[0], -b->memory->step[0], 0.5f));
    // Standing still they stay, however close (a push needs a velocity to go across).
    Pair(&a, &b, 0.0f, 0.4f);
    Tick();
    CHECK(!a->memory->stepping && !b->memory->stepping);
    // Far enough: nothing.
    Pair(&a, &b, 0.0f, 1.0f);
    Tick();
    CHECK(!a->memory->stepping && !b->memory->stepping);
    // At different heights: nothing.
    Pair(&a, &b, 0.0f, 0.4f);
    SetFramePos(b->frameBytes, 0.4f, 3.0f, 0);
    SetVelocity(a, 0.5f, 0);
    Tick();
    CHECK(a->memory->step[2] == 0.0f);
    a->memory->stepping = false;
    CHECK(!a->memory->stepping);
    // Heading for each other: they pass, one to each side.
    Pair(&a, &b, -3.0f, 3.0f);
    GiveRoute(a, {3});
    GiveRoute(b, {1});
    SetVelocity(a, 1.4f, 0);
    SetVelocity(b, -1.4f, 0);
    float sideA = 0, sideB = 0;
    for (int i = 0; i < 30; ++i) {
        Tick();
        const S_vector pa = a->frame()->GetWorldPos(), pb = b->frame()->GetWorldPos();
        SetFramePos(a->frameBytes, pa.x + a->memory->step[0] * 0.1f, 0, pa.z + a->memory->step[2] * 0.1f);
        SetFramePos(b->frameBytes, pb.x + b->memory->step[0] * 0.1f, 0, pb.z + b->memory->step[2] * 0.1f);
        SetVelocity(a, a->memory->step[0], a->memory->step[2]);
        SetVelocity(b, b->memory->step[0], b->memory->step[2]);
        sideA = a->frame()->GetWorldPos().z;
        sideB = b->frame()->GetWorldPos().z;
        const float dx = a->frame()->GetWorldPos().x - b->frame()->GetWorldPos().x;
        const float dz = sideA - sideB;
        CHECK(dx * dx + dz * dz > 0.25f);               // (they never go through each other)
    }
    CHECK(std::fabs(sideA - sideB) > 0.1f);
    // Lying in the way: pushed out of its long box, not a circle.
    Pair(&a, &b, 1.2f, 0.0f, 2);
    Tick();
    CHECK(b->memory->halfSize[2] == 1.3f && b->memory->axisZ.z == 1.0f && b->memory->axisX.x == -1.0f || b->memory->axisX.z == 1.0f);
    // 1.2 along x at half 1.1: outside. Along z (its long side) the same is inside.
    Pair(&a, &b, 0.5f, 0.0f, 2);
    Tick();
    CHECK(a->memory->stepping && a->memory->step[0] > 0);
    // A suspended neighbour: no push, marked.
    Pair(&a, &b, 0.0f, 0.4f);
    b->suspended = true;
    Tick();
    CHECK(!a->memory->stepping || a->memory->step[0] <= 0.0f);
    CHECK((b->memory->flags & 4) != 0);
    // A neighbour out of play: no push.
    Pair(&a, &b, 0.0f, 0.4f);
    b->unconscious = true;
    Tick();
    CHECK(!a->memory->stepping);
    // Off the network it is brought back.
    ResetAll();
    AddPoint(0, 0, 1.0f);
    S_Fake *lost = MakeHuman(3, 0);
    S_Fake *other = MakeHuman(8, 0);
    Meet(lost, other);
    Tick();
    CHECK(lost->memory->stepping && lost->memory->step[0] < 0);
    CHECK(lost->memory->nearPoint == &g_net.points[0]);
    CHECK(lost->memory->currentPoint == &g_net.points[0]);
    // Player's controller (type 1): counts as alone, its step as it goes; the other goes round it.
    Pair(&a, &b, 0.0f, 0.4f);
    reinterpret_cast<C_HumanController *>(&a->controller)->m_type = static_cast<E_HumanController>(1);
    GiveRoute(b, {4});
    GiveRoute(a, {4});
    Tick();
    CHECK(a->memory->stepping && a->memory->step[0] > 0.0f && a->memory->nearPoint == nullptr);
    CHECK(b->memory->step[0] > 0);
    // Type 5 stands, and takes no part.
    Pair(&a, &b, 0.0f, 0.4f);
    reinterpret_cast<C_HumanController *>(&a->controller)->m_type = static_cast<E_HumanController>(5);
    SetVelocity(a, 1, 0);
    Tick();
    CHECK(!a->memory->walking && a->memory->velocity.x == 0 && !a->memory->solved && a->memory->notPlayer);
    CHECK(a->memory->nearPoint == nullptr);
    // A controller that says no (Unknown104): does not walk aside.
    Pair(&a, &b, 0.0f, 0.4f);
    a->ctrl104 = false;
    Tick();
    CHECK(!a->memory->walking);
}

void TestCars()
{
    ResetAll();
    for (int i = 0; i < 5; ++i)
        AddPoint(-20.0f + 10.0f * i, 0, 8.0f);
    for (int i = 0; i < 4; ++i)
        Link(i, i + 1);
    // Close to a car: pushed off (half 1.4 x 2.4 with the margin).
    S_Fake *human = MakeHuman(1.0f, 0);
    S_FakeCar *car = MakeCar(0, 0);
    Tick();
    CHECK(human->memory->stepping && human->memory->step[0] > 0);
    // Far from it: alone.
    ResetAll();
    AddPoint(0, 0, 15.0f);
    S_Fake *far1 = MakeHuman(8.0f, 0);
    MakeCar(0, 0);
    Tick();
    CHECK(!far1->memory->stepping);
    // Above it: too.
    ResetAll();
    AddPoint(0, 0, 15.0f);
    S_Fake *above = MakeHuman(1.0f, 0);
    SetFramePos(above->frameBytes, 1.0f, 3.0f, 0);
    MakeCar(0, 0);
    Tick();
    CHECK(!above->memory->stepping);
    // Along its long side the same distance is inside, beyond it outside.
    ResetAll();
    AddPoint(0, 0, 15.0f);
    S_Fake *longSide = MakeHuman(0, 2.0f);
    MakeCar(0, 0);
    Tick();
    CHECK(longSide->memory->step[2] > 0);
    ResetAll();
    AddPoint(0, 0, 15.0f);
    S_Fake *wide = MakeHuman(2.0f, 0);
    MakeCar(0, 0);
    Tick();
    CHECK(!wide->memory->stepping);
    // The target out of the car.
    ResetAll();
    const int p0 = AddPoint(-10, 0, 8.0f), p1 = AddPoint(0, 0, 8.0f);
    Link(p0, p1);
    S_Fake *walker = MakeHuman(-5, 0);
    GiveRoute(walker, {p1});
    MakeCar(0, 0);
    Tick();
    S_vector4 target = walker->memory->target;
    CHECK(target.x * target.x + target.z * target.z > 1.0f);
    CHECK(walker->route[0]->pos.x == target.x && walker->route[0]->pos.z == target.z);
    (void)car;
}

void TestStuckTimer()
{
    ResetAll();
    AddPoint(-5, 0, 8.0f);
    AddPoint(5, 0, 8.0f);
    Link(0, 1);
    S_Fake *walker = MakeHuman(0, 0);
    S_Fake *other = MakeHuman(0, 3);
    Meet(walker, other);
    GiveRoute(walker, {1});
    walker->memory->bestDistance = 1e8f;
    walker->memory->stuckTimer = 5;
    walker->memory->routeChanged = false;
    Tick();
    CHECK(walker->memory->stuckTimer == 2000);
    const float best = walker->memory->bestDistance;
    CHECK(best < 100);
    walker->memory->bestDistance = 0.0f;
    Tick();
    CHECK(walker->memory->stuckTimer == 1900);
    CHECK(walker->memory->bestDistance == 0.0f);
    walker->memory->stuckTimer = 50;
    Tick();
    CHECK(walker->memory->stuckTimer == 2000);
}

void TestReplan()
{
    // A human standing on the way: the route goes round it.
    ResetAll();
    const int from = AddPoint(0, 0, 8.0f), to = AddPoint(10, 0, 8.0f);
    Link(from, to);
    S_Fake *walker = MakeHuman(0, 0);
    S_Fake *block = MakeHuman(5, 0);
    block->moveMode = 0;
    Meet(walker, block);
    GiveRoute(walker, {to});
    walker->memory->routeChanged = true;
    walker->memory->plannedFrom = -1;
    walker->memory->nearPoint = &g_net.points[from];
    Tick();
    CHECK(walker->route.size() >= 2);
    if (walker->route.size() >= 2) {
        const S_AIWaypoint *first = walker->route[0];
        CHECK(std::fabs(first->pos.x - 5.0f) < 0.1f);
        CHECK(std::fabs(std::fabs(first->pos.z) - 0.8f) < 0.2f);
        CHECK(walker->route.back()->checkpoint == to);
        CHECK(walker->memory->plannedFrom == 0);            // (the old goal went: it is the last of the new way)
    }
    // Once planned it is not again.
    const size_t size = walker->route.size();
    walker->memory->routeChanged = true;
    Tick();
    CHECK(walker->route.size() == size);
    // Clear way: no change.
    ResetAll();
    const int a = AddPoint(0, 0, 8.0f), b = AddPoint(10, 0, 8.0f);
    Link(a, b);
    S_Fake *free1 = MakeHuman(0, 0);
    S_Fake *aside = MakeHuman(5, 5);
    Meet(free1, aside);
    GiveRoute(free1, {b});
    free1->memory->routeChanged = true;
    free1->memory->plannedFrom = -1;
    free1->memory->nearPoint = &g_net.points[a];
    Tick();
    CHECK(free1->route.size() == 1);
    // The first waypoints blocked go.
    ResetAll();
    const int c = AddPoint(0, 0, 8.0f), d = AddPoint(5, 0, 8.0f), e = AddPoint(15, 0, 8.0f);
    Link(c, d);
    Link(d, e);
    S_Fake *skip = MakeHuman(0, 0);
    S_Fake *onPoint = MakeHuman(5, 0);
    Meet(skip, onPoint);
    GiveRoute(skip, {d, e});
    skip->memory->routeChanged = true;
    skip->memory->plannedFrom = -1;
    skip->memory->nearPoint = &g_net.points[c];
    Tick();
    CHECK(skip->route.back()->checkpoint == e);
    bool hasD = false;
    for (const S_AIWaypoint *w : skip->route)
        hasD = hasD || (w->checkpoint == d && w->pos.x == 5.0f && w->pos.z == 0.0f);
    CHECK(!hasD);
    // A standing car in the way: round it. One moving: not.
    ResetAll();
    const int f = AddPoint(-10, 0, 8.0f), g = AddPoint(10, 0, 8.0f);
    Link(f, g);
    S_Fake *driver = MakeHuman(-5, 0);
    GiveRoute(driver, {g});
    driver->memory->routeChanged = true;
    driver->memory->plannedFrom = -1;
    driver->memory->nearPoint = &g_net.points[f];
    MakeCar(0, 0);
    Tick();
    CHECK(driver->route.size() >= 2);
    ResetAll();
    const int h = AddPoint(-10, 0, 8.0f), i = AddPoint(10, 0, 8.0f);
    Link(h, i);
    S_Fake *driver2 = MakeHuman(-5, 0);
    GiveRoute(driver2, {i});
    driver2->memory->routeChanged = true;
    driver2->memory->plannedFrom = -1;
    driver2->memory->nearPoint = &g_net.points[h];
    MakeCar(0, 0, 1.0f, 2.0f, 5.0f);
    Tick();
    CHECK(driver2->route.size() == 1);
    // A lying human: round its box.
    ResetAll();
    const int j = AddPoint(0, 0, 8.0f), k = AddPoint(10, 0, 8.0f);
    Link(j, k);
    S_Fake *walker2 = MakeHuman(0, 0);
    S_Fake *lying = MakeHuman(5, 0, 2);
    Meet(walker2, lying);
    GiveRoute(walker2, {k});
    walker2->memory->routeChanged = true;
    walker2->memory->plannedFrom = -1;
    walker2->memory->nearPoint = &g_net.points[j];
    Tick();
    CHECK(walker2->route.size() >= 2);
}

// Numbers worked out by hand.
void TestRoots()
{
    float t = -1.0f;
    CHECK(crowd::FirstRootAhead(-4.0f, 4.0f, 1.0f, &t) && Near(t, 1.0f));   // (3 and 1)
    t = -1.0f;
    CHECK(crowd::FirstRootAhead(-2.0f, 0.0f, 1.0f, &t) && Near(t, 1.0f));
    CHECK(!crowd::FirstRootAhead(-4.0f, 1.0f, 1.0f, &t));                   // (2.5 and 1.5: both beyond)
    t = -1.0f;
    CHECK(crowd::FirstRootAhead(1.0f, 9.0f, 1.0f, &t) && Near(t, 1.0f));    // (1 and -2)
    CHECK(!crowd::FirstRootAhead(2.0f, 1.0f, 1.0f, &t));                    // (-.5 and -1.5)
    t = -1.0f;
    CHECK(crowd::FirstRootAhead(-3.0f, 1.0f, 2.0f, &t) && Near(t, 0.5f));   // (1 and .5)
    t = -1.0f;
    CHECK(crowd::FirstRootAhead(1.0f, 1.0f, 1.0f, &t) && Near(t, 0.0f));    // (0 and -1)
    t = -1.0f;
    CHECK(crowd::FirstRootAhead(-0.5f, 0.25f, 1.0f, &t) && Near(t, 0.0f, 0.51f));

    CHECK(Near(crowd::FastInvSqrt(4.0f), 0.4992f, 0.002f));
    CHECK(Near(crowd::FastInvSqrt(1.0f), 0.998f, 0.003f));
    CHECK(Near(crowd::FastInvSqrt(100.0f), 0.0998f, 0.001f));

    S_vector v = crowd::NormalizedZero(S_vector(3, 0, 4));
    CHECK(Near(v.x, 0.6f) && Near(v.z, 0.8f));
    v = crowd::NormalizedZero(S_vector(0, 2, 0));
    CHECK(Near(v.y, 1.0f));
    v = crowd::NormalizedZero(S_vector(1, 0, 0));
    CHECK(v.x == 1);
    v = crowd::NormalizedZero(S_vector(-1e-9f, 0, 0));
    CHECK(v.x == -1);
    v = crowd::NormalizedZero(S_vector(1e-9f, 0, 0));
    CHECK(v.x == 1);
    v = crowd::NormalizedZero(S_vector(0, 0, -1e-9f));
    CHECK(v.z == -1 && v.x == 0);
    v = crowd::NormalizedZero(S_vector(0, 0, 1e-9f));
    CHECK(v.z == 1);
    v = crowd::NormalizedZero(S_vector(0, -1e-9f, 0));
    CHECK(v.y == -1);
    v = crowd::NormalizedZero(S_vector(0, 0, 0));
    CHECK(v.y == 1);

    // Side stepping a unit sphere, head on: no way to go: along the tangent, by how soon.
    S_vector out(9, 9, 9);
    const S_vector r(0, 0, -2);
    S_vector l(0, 0, 2);
    CHECK(crowd::SideStepEllipsoid(l, r, 3.0f, 0.125f, &out));
    const float f = crowd::FastInvSqrt(4.0f);
    CHECK(Near(out.x, -2.0f * f * 0.0625f) && out.y == 0.0f && Near(out.z, 0.0f));
    // Off to one side: the other way, a little weaker.
    l = S_vector(0.1f, 0, 2);
    CHECK(crowd::SideStepEllipsoid(l, r, 0.01f + 4.0f - 1.0f, 0.125f, &out));
    CHECK(out.x > 0.0f);
    // Scaled by what it is given.
    S_vector twice;
    CHECK(crowd::SideStepEllipsoid(l, r, 3.01f, 0.25f, &twice));
    CHECK(Near(twice.x, out.x * 2.0f));
    // Going away; too slow; passing wide; not getting there in the step.
    CHECK(!crowd::SideStepEllipsoid(S_vector(0, 0, 2), S_vector(0, 0, 2), 3.0f, 0.125f, &out));
    CHECK(!crowd::SideStepEllipsoid(S_vector(0, 0, 2), S_vector(0, 0, 0), 3.0f, 0.125f, &out));
    CHECK(!crowd::SideStepEllipsoid(S_vector(3, 0, 2), S_vector(0, 0, -2), 12.0f, 0.125f, &out));
    CHECK(!crowd::SideStepEllipsoid(S_vector(0, 0, 5), S_vector(0, 0, -2), 24.0f, 0.125f, &out));
}

C_HumanAIMemory *Memory(S_Fake *fake, float x, float z)
{
    C_HumanAIMemory &memory = *fake->memory;
    memory.pos = {x, 0, z, 0};
    memory.velocity = {};
    memory.push = {};
    memory.target = {x, 0, z, 0};
    memory.solved = true;
    memory.walking = true;
    memory.givesWay = true;
    return &memory;
}

void TestSteps()
{
    ResetAll();
    AddPoint(0, 0, 3.0f);
    AddPoint(10, 0, 3.0f);
    AddPoint(20, 0, 3.0f);
    Link(0, 1);
    S_Fake *fake = MakeHuman(0, 0);
    C_HumanAIMemory &m = *Memory(fake, 0, 0);

    // With no route, walking: the velocity is what the last pushes came to; it does not move yet.
    m.route = nullptr;
    m.push = {0.5f, 0.1f, 0.25f, 0};
    crowd::MoveOneStep(m, 0.5f);
    CHECK(Near(m.velocity.x, 1.0f) && Near(m.velocity.y, 0.2f) && Near(m.velocity.z, 0.5f));
    CHECK(m.pos.x == 0 && m.pos.z == 0);
    // Not walking: it moves as it goes.
    m.walking = false;
    m.velocity = {2, 1, 4, 0};
    crowd::MoveOneStep(m, 0.5f);
    CHECK(Near(m.pos.x, 1.0f) && Near(m.pos.y, 0.5f) && Near(m.pos.z, 2.0f));
    CHECK(Near(m.velocity.x, 2.0f));
    // Lying: the box with it.
    fake->stance = 2;
    crowd::MoveOneStep(m, 0.5f);
    CHECK(Near(m.boxCenter.x, 2.0f));
    fake->stance = 0;
    // With a route: toward the waypoint at its speed, flat; its point noted.
    GiveRoute(fake, {1});
    m.route = &fake->route;
    m.waypoint = 0;
    m.pos = {0, 7, 0, 0};
    m.lastGoal = {-1.0f, 0, 0, 0};
    m.speed = 2.0f;
    crowd::MoveOneStep(m, 0.5f);
    CHECK(Near(m.target.x, 10.0f));
    CHECK(m.targetPoint == &g_net.points[1]);
    const float k = crowd::FastInvSqrt(100.0f) * 2.0f;
    CHECK(Near(m.velocity.x, 10.0f * k) && m.velocity.y == 0.0f && Near(m.velocity.z, 0.0f));
    CHECK(Near(m.pos.x, 0.5f * 10.0f * k) && Near(m.pos.y, 7.0f));
    // At the waypoint: the velocity kept.
    m.pos = {10, 0, 0, 0};
    m.velocity = {3, 0, 0, 0};
    crowd::MoveOneStep(m, 0.5f);
    CHECK(Near(m.velocity.x, 3.0f) && Near(m.pos.x, 11.5f));

    // On to the next waypoint when this one would be passed: not for the last.
    GiveRoute(fake, {2});
    m.waypoint = 0;
    m.pos = {9, 0, 0, 0};
    m.velocity = {4, 0, 0, 0};
    m.lastGoal = {0, 0, 0, 0};
    crowd::AdvanceWaypoint(m, 0.5f);                    // (reaching 11: past 10)
    CHECK(m.waypoint == 1);
    m.waypoint = 0;
    m.pos = {0, 0, 0, 0};
    crowd::AdvanceWaypoint(m, 0.5f);                    // (reaching 2: not yet)
    CHECK(m.waypoint == 0);
    m.pos = {9.96f, 0, 0, 0};
    m.velocity = {0, 0, 0, 0};
    crowd::AdvanceWaypoint(m, 0.5f);                    // (almost there: on)
    CHECK(m.waypoint == 1);
    // Measured from the waypoint before.
    m.waypoint = 1;
    m.pos = {25, 0, 0, 0};
    crowd::AdvanceWaypoint(m, 0.5f);
    CHECK(m.waypoint == 1);

    // Stuck: nearer sets the mark; else the timer runs down and starts again.
    m.waypoint = 0;
    m.pos = {7, 0, 0, 0};
    m.bestDistance = 1e8f;
    m.stuckTimer = 50;
    crowd::UpdateStuckTimer(m, 100);
    CHECK(Near(m.bestDistance, 9.0f) && m.stuckTimer == 2000);
    m.pos = {6, 0, 0, 0};
    crowd::UpdateStuckTimer(m, 100);
    CHECK(Near(m.bestDistance, 9.0f) && m.stuckTimer == 1900);
    m.stuckTimer = 50;
    crowd::UpdateStuckTimer(m, 100);
    CHECK(m.stuckTimer == 2000);
    m.stuckTimer = 100;
    crowd::UpdateStuckTimer(m, 100);
    CHECK(m.stuckTimer == 0);

    // Turned aside: only a push nearly straight back.
    C_HumanAIMemory &t = *Memory(fake, 0, 0);
    t.velocity = {0, 0, 2, 0};
    t.push = {0, 0, -1, 0};
    crowd::TurnPushAside(t, 0.5f);
    CHECK(Near(t.push.x, 2.0f) && Near(t.push.z, -1.0f));   // (z * 2 * step)
    t.push = {0.1f, 0, -1, 0};
    crowd::TurnPushAside(t, 0.5f);
    CHECK(Near(t.push.x, 2.1f));                        // (.01 / 1 < .08)
    t.velocity = {1, 0, 0, 0};
    t.push = {-1, 0, 0, 0};
    crowd::TurnPushAside(t, 0.5f);
    CHECK(Near(t.push.z, 0.0f - 1.0f) && Near(t.push.x, -1.0f));
    t.velocity = {0, 0, 2, 0};
    t.push = {0.5f, 0, -1, 0};
    crowd::TurnPushAside(t, 0.5f);
    CHECK(Near(t.push.x, 0.5f));                        // (.25 / 1 >= .08)
    t.push = {0, 0, 1, 0};
    crowd::TurnPushAside(t, 0.5f);
    CHECK(Near(t.push.x, 0.0f));                        // (forward)
    t.push = {};
    crowd::TurnPushAside(t, 0.5f);
    CHECK(t.push.x == 0.0f);
    t.velocity = {};
    t.push = {0, 0, -1, 0};
    crowd::TurnPushAside(t, 0.5f);
    CHECK(t.push.x == 0.0f);
    t.velocity = {0, 0, 2, 0};
    t.solved = false;
    crowd::TurnPushAside(t, 0.5f);
    CHECK(t.push.x == 0.0f);
}

void TestPushNumbers()
{
    ResetAll();
    AddPoint(0, 0, 15.0f);
    S_Fake *a = MakeHuman(0, 0), *b = MakeHuman(0, 0);
    C_HumanAIMemory &self = *Memory(a, 0, 0), &other = *Memory(b, 0.4f, 0);

    // Overlapping, going straight at it: back along the line, .55 of what overlaps.
    self.velocity = {1, 0, 0, 0};
    crowd::PushFromHuman(self, other);
    CHECK(Near(self.push.x, -0.4f * (0.55f / 0.4f) * 0.4f) && self.push.y == 0.0f && Near(self.push.z, 0.0f));
    // Going past it: across, .75.
    self.push = {};
    self.velocity = {0, 0, 1, 0};
    crowd::PushFromHuman(self, other);
    CHECK(Near(self.push.x, -0.4f * (0.75f / 0.4f) * 0.4f) && Near(self.push.z, 0.0f));
    // Not moving: nothing. On top of it: nothing. Taking no part: nothing.
    self.push = {};
    self.velocity = {};
    crowd::PushFromHuman(self, other);
    CHECK(self.push.x == 0.0f);
    self.velocity = {1, 0, 0, 0};
    other.pos = {0, 0, 0, 0};
    crowd::PushFromHuman(self, other);
    CHECK(self.push.x == 0.0f);
    other.pos = {0.4f, 0, 0, 0};
    self.solved = false;
    crowd::PushFromHuman(self, other);
    CHECK(self.push.x == 0.0f);
    self.solved = true;
    // Another floor: nothing; just within: yes.
    other.pos = {0.4f, 2.5f, 0, 0};
    crowd::PushFromHuman(self, other);
    CHECK(self.push.x == 0.0f);
    other.pos = {0.4f, 1.9f, 0, 0};
    crowd::PushFromHuman(self, other);
    CHECK(self.push.x < 0.0f);

    // Coming up to it from outside: to the side, sooner stronger.
    self.push = {};
    other.pos = {1.0f, 0, 0.1f, 0};
    self.velocity = {2, 0, 0, 0};
    crowd::PushFromHuman(self, other);
    CHECK(self.push.z != 0.0f);
    const float nearPush = std::fabs(self.push.z);
    self.push = {};
    other.pos = {1.5f, 0, 0.1f, 0};
    crowd::PushFromHuman(self, other);
    CHECK(std::fabs(self.push.z) < nearPush);
    // To the other side when it stands to the other side.
    self.push = {};
    other.pos = {1.0f, 0, -0.1f, 0};
    crowd::PushFromHuman(self, other);
    CHECK(self.push.z > 0.0f);
    self.push = {};
    other.pos = {1.0f, 0, 0.1f, 0};
    crowd::PushFromHuman(self, other);
    CHECK(self.push.z < 0.0f);
    // By the relative velocity: both going along: nothing.
    self.push = {};
    other.velocity = {2, 0, 0, 0};
    crowd::PushFromHuman(self, other);
    CHECK(self.push.z == 0.0f);
    other.velocity = {};
    // One that does not give way is walked into. Going away, or passing wide: nothing.
    other.givesWay = false;
    crowd::PushFromHuman(self, other);
    CHECK(self.push.z == 0.0f);
    other.givesWay = true;
    self.velocity = {-2, 0, 0, 0};
    crowd::PushFromHuman(self, other);
    CHECK(self.push.z == 0.0f);
    self.velocity = {2, 0, 0, 0};
    other.pos = {1.0f, 0, 1.5f, 0};
    crowd::PushFromHuman(self, other);
    CHECK(self.push.z == 0.0f);
    // Not reaching it in time: nothing.
    other.pos = {10.0f, 0, 0.1f, 0};
    crowd::PushFromHuman(self, other);
    CHECK(self.push.z == 0.0f);

    // Lying: its box. Inside: out along the local axes; its long side counts.
    b->stance = 2;
    other.axisX = {1, 0, 0, 0};
    other.axisY = {0, 1, 0, 0};
    other.axisZ = {0, 0, 1, 0};
    other.halfSize[0] = 0.7f;
    other.halfSize[1] = 0.3f;
    other.halfSize[2] = 1.3f;
    other.pos = {0, 0, 0, 0};
    self.pos = {0, 0, 1.0f, 0};
    self.velocity = {};
    self.push = {};
    crowd::PushFromHuman(self, other);
    // l.z = 1 / 1.7: pushed to the surface at 1.7.
    CHECK(Near(self.push.z, 0.7f) && Near(self.push.x, 0.0f));
    self.pos = {1.0f, 0, 0, 0};
    self.push = {};
    crowd::PushFromHuman(self, other);
    CHECK(Near(self.push.x, 0.1f));                     // (1.1 - 1)
    // Turned: its own axes.
    other.axisX = {0, 0, 1, 0};
    other.axisZ = {1, 0, 0, 0};
    self.pos = {1.0f, 0, 0, 0};
    self.push = {};
    crowd::PushFromHuman(self, other);
    CHECK(Near(self.push.x, 0.7f));
    other.axisX = {1, 0, 0, 0};
    other.axisZ = {0, 0, 1, 0};
    // Dead on its centre: nothing. Outside, standing still: nothing; coming at it: to the side.
    self.pos = {0, 0, 0, 0};
    self.push = {};
    crowd::PushFromHuman(self, other);
    CHECK(self.push.x == 0.0f && self.push.z == 0.0f);
    self.pos = {-2.0f, 0, 0.1f, 0};
    crowd::PushFromHuman(self, other);
    CHECK(self.push.z == 0.0f);
    self.velocity = {4, 0, 0, 0};
    crowd::PushFromHuman(self, other);
    CHECK(self.push.z != 0.0f);
    S_vector4 taken = self.push;
    self.push = {};
    other.givesWay = false;
    crowd::PushFromHuman(self, other);
    CHECK(self.push.z == 0.0f);
    other.givesWay = true;
    (void)taken;
    b->stance = 0;

    // Targets too near each other: half each way when both are going, with .75.
    ResetAll();
    AddPoint(0, 0, 15.0f);
    S_Fake *c = MakeHuman(0, 0), *d = MakeHuman(0, 0);
    C_HumanAIMemory &p = *Memory(c, 0, 0), &q = *Memory(d, 0, 0);
    GiveRoute(c, {0});
    GiveRoute(d, {0});
    p.route = &c->route;
    q.route = &d->route;
    p.targetPoint = nullptr;
    q.targetPoint = nullptr;
    p.target = {0, 0, 0, 0};
    q.target = {0.4f, 0, 0, 0};
    crowd::PushTargetFromHuman(p, q);
    const float by = 0.4f * (0.75f / 0.4f) * 0.4f;
    CHECK(Near(p.target.x, -by) && Near(q.target.x, 0.4f + by));
    // Far enough: nothing; another floor: nothing.
    p.target = {0, 0, 0, 0};
    q.target = {0.81f, 0, 0, 0};
    crowd::PushTargetFromHuman(p, q);
    CHECK(p.target.x == 0.0f && q.target.x == 0.81f);
    q.target = {0.4f, 3.0f, 0, 0};
    crowd::PushTargetFromHuman(p, q);
    CHECK(p.target.x == 0.0f);
    // The other without a route stays, this one goes.
    q.target = {0.4f, 0, 0, 0};
    q.route = nullptr;
    crowd::PushTargetFromHuman(p, q);
    CHECK(Near(p.target.x, -by) && q.target.x == 0.4f);
    q.route = &d->route;
    // Not taking part: stays; an empty route: stays; no route at all: goes.
    p.target = {0, 0, 0, 0};
    q.solved = false;
    crowd::PushTargetFromHuman(p, q);
    CHECK(Near(p.target.x, -by) && q.target.x == 0.4f);
    q.solved = true;
    p.target = {0, 0, 0, 0};
    p.solved = false;
    crowd::PushTargetFromHuman(p, q);
    CHECK(p.target.x == 0.0f);
    p.solved = true;
    std::vector<S_AIWaypoint *> empty;
    p.route = &empty;
    crowd::PushTargetFromHuman(p, q);
    CHECK(p.target.x == 0.0f);
    p.route = nullptr;
    q.target = {0.4f, 0, 0, 0};
    crowd::PushTargetFromHuman(p, q);
    CHECK(Near(p.target.x, -by));
    p.route = &c->route;
    // At one place: by where they themselves are, .8.
    p.target = {5, 0, 5, 0};
    q.target = {5, 0, 5, 0};
    p.pos = {0, 0, 0, 0};
    q.pos = {0, 0, 2, 0};
    crowd::PushTargetFromHuman(p, q);
    CHECK(Near(p.target.z, 4.2f) && Near(q.target.z, 5.8f));
    // Kept on the network.
    p.targetPoint = &g_net.points[0];
    p.target = {14.9f, 0, 0, 0};
    q.target = {14.9f + 0.4f, 0, 0, 0};
    q.solved = false;
    p.target = {14.9f, 0, 0, 0};
    crowd::MoveTarget(p, S_vector(5, 0, 0));
    CHECK(p.target.x <= 15.0f && p.target.x > 14.0f);
    q.solved = true;
    // Lying: its box.
    d->stance = 2;
    q.axisX = {1, 0, 0, 0};
    q.axisY = {0, 1, 0, 0};
    q.axisZ = {0, 0, 1, 0};
    q.halfSize[0] = 0.7f;
    q.halfSize[1] = 0.3f;
    q.halfSize[2] = 1.3f;
    p.targetPoint = nullptr;
    p.target = {0, 0, 1.0f, 0};
    q.target = {0, 0, 0, 0};
    crowd::PushTargetFromHuman(p, q);
    // .75 of the way to the surface at 1.7, half (the other) each.
    CHECK(Near(p.target.z, 1.0f + 0.75f * 0.7f) && Near(q.target.z, -0.75f * 0.7f));
    q.solved = false;
    p.target = {0, 0, 1.0f, 0};
    q.target = {0, 0, 0, 0};
    crowd::PushTargetFromHuman(p, q);
    CHECK(Near(p.target.z, 1.7f) && q.target.z == 0.0f);
    q.solved = true;
    p.target = {0, 0, 2.0f, 0};
    q.target = {0, 0, 0, 0};
    crowd::PushTargetFromHuman(p, q);
    CHECK(p.target.z == 2.0f);
    // On it: by where they are, to its side.
    p.target = {0, 0, 0, 0};
    q.target = {0, 0, 0, 0};
    p.pos = {1, 0, 0, 0};
    q.pos = {0, 0, 0, 0};
    crowd::PushTargetFromHuman(p, q);
    CHECK(Near(p.target.x, 1.1f));
    d->stance = 0;
}

void TestOnNetwork()
{
    ResetAll();
    AddPoint(0, 0, 1.0f);
    AddPoint(10, 0, 1.0f);
    Link(0, 1);
    S_Fake *a = MakeHuman(0, 0);
    C_HumanAIMemory &m = *Memory(a, 0, 0);
    // Finds its point; off it, pushed back; on it, no push.
    m.nearPoint = nullptr;
    m.pos = {5, 0, 3, 0};
    crowd::PushOntoNetwork(m);
    CHECK(m.nearPoint != nullptr);
    CHECK(Near(m.push.z, -2.0f) && Near(m.push.x, 0.0f));
    m.push = {};
    m.pos = {5, 0, 0.5f, 0};
    crowd::PushOntoNetwork(m);
    CHECK(m.push.z == 0.0f && m.pos.z == 0.5f);
    CHECK(m.currentPoint != nullptr);
    m.solved = false;
    m.pos = {5, 0, 3, 0};
    crowd::PushOntoNetwork(m);
    CHECK(m.push.z == 0.0f);
    crowd::HoldOnNetwork(m);
    CHECK(m.pos.z == 3.0f);
    m.solved = true;
    // Held: moved there outright.
    m.nearPoint = nullptr;
    crowd::HoldOnNetwork(m);
    CHECK(Near(m.pos.z, 1.0f) && m.nearPoint != nullptr);
    m.pos = {5, 0, 0.5f, 0};
    crowd::HoldOnNetwork(m);
    CHECK(m.pos.z == 0.5f);
    // No point anywhere: nothing.
    ResetAll();
    S_Fake *b = MakeHuman(0, 0);
    C_HumanAIMemory &n = *Memory(b, 0, 3);
    n.nearPoint = nullptr;
    crowd::PushOntoNetwork(n);
    crowd::HoldOnNetwork(n);
    CHECK(n.push.z == 0.0f && n.pos.z == 3.0f);
}

void TestWholeTick()
{
    // After one tick the step is where the solver ended, per second.
    ResetAll();
    AddPoint(0, 0, 15.0f);
    AddPoint(10, 0, 15.0f);
    Link(0, 1);
    S_Fake *a = MakeHuman(0, 0), *b = MakeHuman(0, 5);
    Meet(a, b);
    GiveRoute(a, {1});
    Tick(100);
    C_HumanAIMemory &m = *a->memory;
    CHECK(m.solved && m.walking && m.notPlayer && m.givesWay);
    CHECK(Near(m.start.x, 0.0f));
    CHECK(Near(m.pos.x, 0.1f * m.speed, 0.02f));
    CHECK(Near(m.step[0], m.speed, 0.15f) && m.stepping);
    CHECK(Near(m.speed, 2.8f));
    CHECK(m.targetPoint == &g_net.points[1]);
    CHECK(m.route == &a->route && m.waypoint == 0);
    // The waypoint follows a target that was pushed.
    ResetAll();
    AddPoint(0, 0, 15.0f);
    AddPoint(5, 0, 15.0f);
    Link(0, 1);
    S_Fake *c = MakeHuman(0, 0), *d = MakeHuman(5.2f, 0);
    Meet(c, d);
    GiveRoute(c, {1});
    GiveRoute(d, {1});
    Tick(100);
    CHECK(c->route[0]->pos.x != 5.0f);
    CHECK(c->route[0]->pos.x == c->memory->target.x);
    // Stance and speed class choose the speed.
    ResetAll();
    AddPoint(0, 0, 15.0f);
    AddPoint(9, 0, 15.0f);
    Link(0, 1);
    S_Fake *e = MakeHuman(0, 0);
    GiveRoute(e, {1});
    e->moveClass = -1;
    e->stance = 1;
    Tick(100);
    CHECK(Near(e->memory->speed, 1.4f * 3.5f));
    // Lying down: the box takes its direction, the pass does not walk.
    ResetAll();
    AddPoint(0, 0, 15.0f);
    S_Fake *f = MakeHuman(0, 0, 2), *g = MakeHuman(1, 0);
    Meet(f, g);
    float *local = reinterpret_cast<float *>(f->frameBytes + 0x80);
    local[8] = 3.0f; local[9] = 0; local[10] = 4.0f;
    Tick(100);
    CHECK(!f->memory->walking);
    CHECK(Near(f->memory->axisZ.x, 0.6f) && Near(f->memory->axisZ.z, 0.8f));
    CHECK(Near(f->memory->axisX.x, -0.8f) && Near(f->memory->axisX.z, 0.6f));
    CHECK(f->memory->axisY.y == 1.0f && f->memory->halfSize[0] == 0.7f && f->memory->halfSize[1] == 0.3f && f->memory->halfSize[2] == 1.3f);
}

void SetCars(std::initializer_list<S_FakeCar *> cars)
{
    C_HumanCrowd &crowd = g_humanCrowd;
    crowd.m_cars.clear();
    crowd.m_carHalves.clear();
    for (S_FakeCar *car : cars) {
        crowd.m_cars.push_back(car->car());
        crowd.m_carHalves.push_back(crowd::CarHalfSize(car->car()));
    }
}
float NaN()
{
    const uint32_t bits = 0x7FC00000;
    float value;
    std::memcpy(&value, &bits, 4);
    return value;
}

void TestStepAloneDirect()
{
    C_HumanCrowd &crowd = g_humanCrowd;
    ResetAll();
    SetCars({});
    AddPoint(0, 0, 3.0f);
    AddPoint(3, 4, 3.0f);
    AddPoint(50, 0, 3.0f);
    S_Fake *fake = MakeHuman(0, 0);
    C_HumanAIMemory &m = *fake->memory;
    SyncNearbyCounts();
    reinterpret_cast<float *>(fake->frameBytes + 0x40)[15] = 2.5f;
    // No route: nothing to do, standing, its marks here.
    fake->human()->m_velocity = {1, 2, 3, 0};
    m.step[0] = m.step[1] = m.step[2] = 9.0f;
    m.stepping = true;
    m.routeChanged = false;
    m.waypoint = 7;
    CHECK(crowd.StepAlone(fake->human(), 0.5f));
    CHECK(m.pos.w == 2.5f && m.waypoint == 0);
    CHECK(m.velocity.x == 0 && m.velocity.y == 0 && m.velocity.z == 0);
    CHECK(m.step[0] == 0 && m.step[1] == 0 && m.step[2] == 0 && !m.stepping);
    CHECK(m.routeChanged);
    // A route: toward the waypoint, flat, at its speed.
    GiveRoute(fake, {1});
    fake->route[0]->pos.y = 5.0f;
    m.routeChanged = false;
    m.waypoint = 7;
    m.lastGoal = {};
    m.routeStart = {};
    CHECK(crowd.StepAlone(fake->human(), 0.5f));
    CHECK(m.route == &fake->route && m.waypoint == 0);
    const float k = crowd::FastInvSqrt(25.0f) * 2.8f;
    CHECK(Near(m.velocity.x, 3 * k) && m.velocity.y == 0 && Near(m.velocity.z, 4 * k));
    CHECK(m.stepping && m.step[0] == m.velocity.x && m.step[1] == 0 && m.step[2] == m.velocity.z);
    CHECK(m.target.y == 5.0f && m.routeStart.x == 3.0f);
    // At the waypoint: no step.
    fake->route[0]->pos = {0, 0, 0, 0};
    m.stepping = true;
    m.step[0] = m.step[1] = m.step[2] = 9.0f;
    CHECK(crowd.StepAlone(fake->human(), 0.5f));
    CHECK(!m.stepping && m.step[0] == 0 && m.step[1] == 0 && m.step[2] == 0 && m.velocity.x == 0 && m.velocity.y == 0 && m.velocity.z == 0);
    // Lost marks.
    fake->route[0]->pos = {3, 0, 4, 0};
    m.lastGoal = {NaN(), 0, 0, 0};
    m.routeStart = {7, 0, 0, 0};
    CHECK(crowd.StepAlone(fake->human(), 0.5f));
    CHECK(m.lastGoal.x == 0);
    m.lastGoal = {7, 0, 0, 0};
    m.routeStart = {NaN(), 0, 0, 0};
    CHECK(crowd.StepAlone(fake->human(), 0.5f));
    CHECK(m.lastGoal.x == 0 && m.routeStart.x == 3);
    // The velocity it goes by when moving, for the passing of a waypoint.
    fake->route.push_back(AllocWaypoint(2, {50, 0, 0, 0}, 0, 0, 1.0f));
    fake->route[0]->pos = {1, 0, 0, 0};
    m.lastGoal = {-1, 0, 0, 0};
    m.routeStart = {-1, 0, 0, 0};
    fake->human()->m_velocity = {8, 0, 0, 0};
    fake->moveMode = 1;
    CHECK(crowd.StepAlone(fake->human(), 0.5f));
    CHECK(m.waypoint == 1 && m.target.x == 50);
    fake->moveMode = 0;
    CHECK(crowd.StepAlone(fake->human(), 0.5f));
    CHECK(m.waypoint == 0);
    fake->moveMode = 1;
    fake->route.pop_back();
    // Its near point forgotten only by the player's and type 5.
    m.nearPoint = &g_net.points[0];
    CHECK(crowd.StepAlone(fake->human(), 0.5f));
    CHECK(m.nearPoint == &g_net.points[0]);
    reinterpret_cast<C_HumanController *>(&fake->controller)->m_type = static_cast<E_HumanController>(2);
    CHECK(crowd.StepAlone(fake->human(), 0.5f));
    CHECK(m.nearPoint == &g_net.points[0]);
    reinterpret_cast<C_HumanController *>(&fake->controller)->m_type = static_cast<E_HumanController>(5);
    CHECK(crowd.StepAlone(fake->human(), 0.5f));
    CHECK(m.nearPoint == nullptr);
    m.nearPoint = &g_net.points[0];
    reinterpret_cast<C_HumanController *>(&fake->controller)->m_type = static_cast<E_HumanController>(1);
    CHECK(crowd.StepAlone(fake->human(), 0.5f));
    CHECK(m.nearPoint == nullptr);
    reinterpret_cast<C_HumanController *>(&fake->controller)->m_type = static_cast<E_HumanController>(0);

    // Somebody near: not alone, nothing touched; a player or a working body is all the same.
    S_Fake *other = MakeHuman(50, 50);
    Meet(fake, other);
    SyncNearbyCounts();
    m.waypoint = 7;
    CHECK(!crowd.StepAlone(fake->human(), 0.5f));
    CHECK(m.waypoint == 7);
    fake->work5 = true;
    CHECK(crowd.StepAlone(fake->human(), 0.5f));
    fake->work5 = false;
    reinterpret_cast<C_HumanController *>(&fake->controller)->m_type = static_cast<E_HumanController>(1);
    CHECK(crowd.StepAlone(fake->human(), 0.5f));
    reinterpret_cast<C_HumanController *>(&fake->controller)->m_type = static_cast<E_HumanController>(0);
    fake->nearby.clear();
    SyncNearbyCounts();
    // A car within 7 m, on this floor.
    S_FakeCar *close = MakeCar(6.9f, 0), *distantCar = MakeCar(8.0f, 0), *above = MakeCar(1, 0);
    reinterpret_cast<float *>(above->frameBytes + 0x40)[13] = 3.0f;
    SetCars({distantCar});
    CHECK(crowd.StepAlone(fake->human(), 0.5f));
    SetCars({above});
    CHECK(crowd.StepAlone(fake->human(), 0.5f));
    SetCars({close});
    CHECK(!crowd.StepAlone(fake->human(), 0.5f));
    SetCars({close, distantCar});
    CHECK(!crowd.StepAlone(fake->human(), 0.5f));
    SetCars({distantCar, close});
    CHECK(!crowd.StepAlone(fake->human(), 0.5f));
    SetCars({});
}

void TestPrepareDirect()
{
    C_HumanCrowd &crowd = g_humanCrowd;
    ResetAll();
    SetCars({});
    const int pa = AddPoint(0, 0, 1.0f), pb = AddPoint(10, 0, 1.0f);
    Link(pa, pb);
    S_Fake *fake = MakeHuman(0.5f, 0);
    C_HumanAIMemory &m = *fake->memory;
    SyncNearbyCounts();
    C_HumanController *controller = reinterpret_cast<C_HumanController *>(&fake->controller);
    reinterpret_cast<float *>(fake->frameBytes + 0x40)[15] = 2.5f;
    // The player: target ahead by its velocity; stands in the solver.
    controller->m_type = static_cast<E_HumanController>(1);
    fake->human()->m_velocity = {2, 4, 6, 0};
    m.nearPoint = &g_net.points[0];
    m.targetPoint = &g_net.points[1];
    m.currentPoint = &g_net.points[1];
    m.waypoint = 7;
    m.routeChanged = false;
    m.plannedFrom = 3;
    crowd.Prepare(m, 0.5f);
    CHECK(m.pos.w == 2.5f && m.start.x == 0.5f && m.waypoint == 0);
    CHECK(m.targetPoint == nullptr && m.currentPoint == nullptr && m.nearPoint == nullptr);
    CHECK(m.routeChanged && m.plannedFrom == -1);
    CHECK(Near(m.target.x, 1.5f) && Near(m.target.y, 2.0f) && Near(m.target.z, 3.0f));
    CHECK(m.routeStart.x == 0.5f && m.lastGoal.x == 0.5f);
    CHECK(!m.notPlayer && !m.walking && !m.solved);
    CHECK(m.velocity.x == 0 && m.velocity.y == 0 && m.velocity.z == 0);
    CHECK(m.speed == 2.8f);
    // Not moving: its velocity does not count.
    fake->moveMode = 0;
    crowd.Prepare(m, 0.5f);
    CHECK(m.target.x == 0.5f && m.target.y == 0 && m.target.z == 0);
    fake->moveMode = 1;
    // Type 5.
    controller->m_type = static_cast<E_HumanController>(5);
    m.nearPoint = &g_net.points[0];
    crowd.Prepare(m, 0.5f);
    CHECK(m.nearPoint == nullptr && m.notPlayer && !m.walking && !m.solved && m.target.x == 0.5f);
    CHECK(m.velocity.x == 0 && m.velocity.y == 0 && m.velocity.z == 0);
    // Others, moving: a near point found, or followed from the one it has.
    controller->m_type = static_cast<E_HumanController>(0);
    m.nearPoint = nullptr;
    crowd.Prepare(m, 0.5f);
    CHECK(m.nearPoint == &g_net.points[pa]);
    CHECK(m.notPlayer && m.walking && m.solved && m.velocity.x == 2 && m.velocity.y == 4 && m.velocity.z == 6);
    fake->frameBytes[0] = fake->frameBytes[0];
    SetFramePos(fake->frameBytes, 9.5f, 0, 0);
    m.nearPoint = &g_net.points[pa];
    crowd.Prepare(m, 0.5f);
    CHECK(m.nearPoint == &g_net.points[pb]);
    // Standing: the point stays.
    fake->human()->m_velocity = {};
    m.nearPoint = &g_net.points[pa];
    crowd.Prepare(m, 0.5f);
    CHECK(m.nearPoint == &g_net.points[pa]);
    m.nearPoint = nullptr;
    crowd.Prepare(m, 0.5f);
    CHECK(m.nearPoint == nullptr);
    // Not wanting to walk: only if it has a route to keep to.
    fake->ctrl104 = false;
    fake->human()->m_velocity = {1, 0, 0, 0};
    crowd.Prepare(m, 0.5f);
    CHECK(!m.walking && !m.solved && m.notPlayer && m.velocity.x == 0);
    fake->ctrl104 = true;

    // With a route.
    SetFramePos(fake->frameBytes, 0.5f, 0, 0);
    GiveRoute(fake, {pb});
    m.nearPoint = &g_net.points[pa];
    m.routeStart = {1, 0, 0, 0};
    m.lastGoal = {2, 0, 0, 0};
    m.routeChanged = false;
    m.stuckTimer = 5;
    m.bestDistance = 5;
    m.plannedFrom = 0;
    m.targetPoint = nullptr;
    crowd.Prepare(m, 0.5f);
    CHECK(m.nearPoint == &g_net.points[pa]);                     // (the one before, as the hint)
    CHECK(m.stuckTimer == 5 && m.bestDistance == 5 && m.lastGoal.x == 2);
    CHECK(m.routeStart.x == 10 && m.target.x == 10 && m.targetPoint == &g_net.points[pb]);
    CHECK(m.route == &fake->route);
    // Lost marks.
    m.routeStart = {NaN(), 0, 0, 0};
    m.lastGoal = {2, 0, 0, 0};
    crowd.Prepare(m, 0.5f);
    CHECK(m.lastGoal.x == 0.5f);
    m.lastGoal = {NaN(), 0, 0, 0};
    crowd.Prepare(m, 0.5f);
    CHECK(m.lastGoal.x == 0.5f);
    // The route changed: planned again, unless it is what was planned.
    m.routeChanged = true;
    m.plannedFrom = 0;
    m.routeStart = {3, 0, 0, 0};
    m.unknown109 = true;
    m.stuckTimer = 5;
    m.bestDistance = 5;
    crowd.Prepare(m, 0.5f);
    CHECK(!m.routeChanged && m.stuckTimer == 2000 && m.bestDistance == 1e8f && m.lastGoal.x == 3);
    CHECK(m.unknown109);
    m.routeChanged = true;
    m.plannedFrom = -1;
    crowd.Prepare(m, 0.5f);
    CHECK(!m.unknown109);
    m.unknown109 = true;
    m.routeChanged = true;
    m.plannedFrom = 1;
    crowd.Prepare(m, 0.5f);
    CHECK(!m.unknown109);
    // A route with a stand at the moment: solved; wanting to walk or not.
    fake->ctrl104 = false;
    crowd.Prepare(m, 0.5f);
    CHECK(m.solved && !m.walking);
    fake->ctrl104 = true;
    // The player of a coop game is not given way to by clients' copies.
    controller->m_type = static_cast<E_HumanController>(1);
    g_gameRole = 2;
    g_coop = true;
    crowd.Prepare(m, 0.5f);
    CHECK(!m.givesWay && !m.solved);
    g_gameRole = 6;
    crowd.Prepare(m, 0.5f);
    CHECK(!m.givesWay);
    g_coop = false;
    crowd.Prepare(m, 0.5f);
    CHECK(m.givesWay);
    g_coop = true;
    g_gameRole = 7;
    crowd.Prepare(m, 0.5f);
    CHECK(m.givesWay);
    g_gameRole = 2;
    controller->m_type = static_cast<E_HumanController>(0);
    crowd.Prepare(m, 0.5f);
    CHECK(m.givesWay);
    g_gameRole = 7;
    g_coop = false;

    // Lying: the box by where it faces, flat.
    fake->stance = 2;
    float *local = reinterpret_cast<float *>(fake->frameBytes + 0x80);
    const auto setDir = [&](float x, float y, float z) { local[8] = x; local[9] = y; local[10] = z; };
    m.boxCenter = {};
    setDir(3, 9, 4);
    crowd.Prepare(m, 0.5f);
    CHECK(!m.walking && m.velocity.x == 0);
    CHECK(Near(m.axisZ.x, 0.6f) && m.axisZ.y == 0 && Near(m.axisZ.z, 0.8f));
    CHECK(Near(m.axisX.x, -0.8f) && m.axisX.y == 0 && Near(m.axisX.z, 0.6f));
    CHECK(m.axisY.x == 0 && m.axisY.y == 1 && m.axisY.z == 0);
    CHECK(m.boxCenter.x == 0.5f && m.halfSize[0] == 0.7f && m.halfSize[1] == 0.3f && m.halfSize[2] == 1.3f);
    setDir(0, 0, 1);
    crowd.Prepare(m, 0.5f);
    CHECK(m.axisZ.z == 1 && m.axisZ.x == 0);
    setDir(0, 0, 1.5f);
    crowd.Prepare(m, 0.5f);
    CHECK(Near(m.axisZ.z, 1.0f));
    setDir(1e-5f, 0, 0);
    crowd.Prepare(m, 0.5f);
    CHECK(m.axisZ.x == 1 && m.axisZ.z == 0);
    setDir(-1e-5f, 0, 0);
    crowd.Prepare(m, 0.5f);
    CHECK(m.axisZ.x == -1);
    setDir(0, 0, 1e-5f);
    crowd.Prepare(m, 0.5f);
    CHECK(m.axisZ.z == 1 && m.axisZ.x == 0);
    setDir(0, 0, -1e-5f);
    crowd.Prepare(m, 0.5f);
    CHECK(m.axisZ.z == -1);
    setDir(0, 0, 0);
    crowd.Prepare(m, 0.5f);
    CHECK(m.axisZ.x == 0 && m.axisZ.z == 0);
    fake->stance = 0;

    // Its speed by stance and class; a class below nothing is 1.
    fake->moveClass = -1;
    crowd.Prepare(m, 0.5f);
    CHECK(Near(m.speed, 1.4f * 3.0f));
    fake->stance = 1;
    crowd.Prepare(m, 0.5f);
    CHECK(Near(m.speed, 1.4f * 3.5f));
    fake->moveClass = 2;
    crowd.Prepare(m, 0.5f);
    CHECK(Near(m.speed, 1.4f * 4.0f));
    fake->moveClass = 0;
    fake->stance = 0;
}

void TestPointsDirect()
{
    C_HumanCrowd &crowd = g_humanCrowd;
    ResetAll();
    const int a = AddPoint(0, 0, 1.0f), b = AddPoint(10, 0, 1.0f), big = AddPoint(100, 0, 4.0f);
    Link(a, b);
    const C_CheckpointNetwork *network = Checkpoints();
    const S_Checkpoint *point;
    // Its radius is sixteenths: 4: within at 3.5, without at 4.5.
    point = &g_net.points[big];
    CHECK(crowd.FindPointNear(network, &point, S_vector(103.5f, 0, 0), 1, nullptr) == 1);
    point = &g_net.points[big];
    CHECK(crowd.FindPointNear(network, &point, S_vector(104.5f, 0, 0), 1, nullptr) == 2);
    // The hint.
    point = &g_net.points[b];
    CHECK(crowd.FindPointNear(network, &point, S_vector(0.5f, 0, 0), 1, &g_net.points[a]) == 1);
    CHECK(point == &g_net.points[a]);
    // A strip from the hint: which end.
    point = &g_net.points[a];
    CHECK(crowd.FindPointNear(network, &point, S_vector(2, 0, 0.5f), 1, &g_net.points[b]) == 1);
    CHECK(point == &g_net.points[a]);
    point = &g_net.points[b];
    CHECK(crowd.FindPointNear(network, &point, S_vector(2, 0, 0.5f), 1, &g_net.points[a]) == 1);
    CHECK(point == &g_net.points[a]);
    // Beyond its ends or wide of it: no.
    point = &g_net.points[a];
    CHECK(crowd.FindPointNear(network, &point, S_vector(-3, 0, 0), 1, &g_net.points[b]) == 2);
    point = &g_net.points[a];
    CHECK(crowd.FindPointNear(network, &point, S_vector(12, 0, 0), 1, &g_net.points[b]) == 2);
    CHECK(point == &g_net.points[b]);
    point = &g_net.points[a];
    CHECK(crowd.FindPointNear(network, &point, S_vector(5, 0, 2), 1, &g_net.points[b]) == 2);
    // By links: in a strip outward: the nearer end; beyond it: nearest.
    point = &g_net.points[a];
    CHECK(crowd.FindPointNear(network, &point, S_vector(7, 0, 0.5f), 1, nullptr) == 2);
    CHECK(point == &g_net.points[b]);
    point = &g_net.points[a];
    CHECK(crowd.FindPointNear(network, &point, S_vector(3, 0, 0.5f), 1, nullptr) == 2);
    CHECK(point == &g_net.points[a]);
    point = &g_net.points[a];
    CHECK(crowd.FindPointNear(network, &point, S_vector(-5, 0, 0), 1, nullptr) == 2);
    CHECK(point == &g_net.points[a]);
    point = &g_net.points[a];
    CHECK(crowd.FindPointNear(network, &point, S_vector(15, 0, 0), 1, nullptr) == 2);
    CHECK(point == &g_net.points[b]);
    // Which kinds are gone through.
    ResetAll();
    const int start = AddPoint(0, 0, 1.0f);
    const uint8_t kinds[][2] = {{2, 1}, {3, 0}, {6, 0}, {7, 1}};
    int x = 100;
    for (const auto &kind : kinds) {
        const int mid = AddPoint(static_cast<float>(x), 0, 1.0f, kind[0]), beyond = AddPoint(static_cast<float>(x), 10, 1.0f);
        Link(start, mid);
        Link(mid, beyond);
        point = &g_net.points[start];
        const int got = crowd.FindPointNear(Checkpoints(), &point, S_vector(static_cast<float>(x), 0, 10), 4, nullptr);
        CHECK(got == kind[1] + 1 || (got == 1) == (kind[1] != 0));
        CHECK((point == &g_net.points[beyond]) == (kind[1] != 0));
        x += 100;
    }
    // A way round: each only once, and to the depth.
    ResetAll();
    const int p0 = AddPoint(0, 0, 1.0f), p1 = AddPoint(10, 0, 1.0f), p2 = AddPoint(20, 0, 1.0f), p3 = AddPoint(30, 0, 1.0f), p4 = AddPoint(40, 0, 1.0f), p5 = AddPoint(50, 0, 1.0f), p6 = AddPoint(60, 0, 1.0f);
    Link(p0, p1); Link(p1, p2); Link(p2, p3); Link(p3, p4); Link(p4, p5); Link(p5, p6); Link(p2, p0);
    point = &g_net.points[p0];
    CHECK(crowd.FindPointNear(Checkpoints(), &point, S_vector(50, 0, 0), 4, nullptr) == 1);
    point = &g_net.points[p0];
    CHECK(crowd.FindPointNear(Checkpoints(), &point, S_vector(60, 0, 0), 4, nullptr) == 2);
    CHECK(point == &g_net.points[p5]);
    point = &g_net.points[p0];
    CHECK(crowd.FindPointNear(Checkpoints(), &point, S_vector(60, 0, 0), 5, nullptr) == 1);
    // A better one seen is kept, not a later worse.
    ResetAll();
    const int q0 = AddPoint(0, 0, 1.0f), near1 = AddPoint(10, 0, 1.0f), far1 = AddPoint(-30, 0, 1.0f);
    Link(q0, far1);
    Link(q0, near1);
    point = &g_net.points[q0];
    CHECK(crowd.FindPointNear(Checkpoints(), &point, S_vector(20, 0, 3), 1, nullptr) == 2);
    CHECK(point == &g_net.points[near1]);
    ResetAll();
    const int r0 = AddPoint(0, 0, 1.0f), r1 = AddPoint(10, 0, 1.0f), r2 = AddPoint(-30, 0, 1.0f);
    Link(r0, r1);
    Link(r0, r2);
    point = &g_net.points[r0];
    CHECK(crowd.FindPointNear(Checkpoints(), &point, S_vector(20, 0, 3), 1, nullptr) == 2);
    CHECK(point == &g_net.points[r1]);
}

void TestAddPushDirect()
{
    S_vector4 total = {0, 0, 0, 5};
    crowd::AddPush(total, S_vector(1, 2, 3));
    CHECK(total.x == 1 && total.y == 2 && total.z == 3 && total.w == 5);
    // Against the pushes: all of it.
    total = {1, 1, 0, 0};
    crowd::AddPush(total, S_vector(-1, -2, 0));
    CHECK(total.x == 0 && total.y == -1);
    // Longer along the same line: the part beyond.
    total = {0, 1, 0, 0};
    crowd::AddPush(total, S_vector(0, 2, 0));
    // along = 2; back = 2 * 1 / 4 = .5: 1 + 2 - .5 * 2 = 2
    CHECK(Near(total.y, 2.0f));
    total = {1, 0, 0, 0};
    crowd::AddPush(total, S_vector(3, 0, 1));
    // along = 3, back = 3 / 10: 1 + 3 - .9, 0 + 1 - .3
    CHECK(Near(total.x, 3.1f) && Near(total.z, 0.7f));
    total = {0, 1, 0, 0};
    crowd::AddPush(total, S_vector(0, 3, 1));
    CHECK(Near(total.y, 1.0f + 3.0f - 0.9f) && Near(total.z, 0.7f));
    // Shorter: what is off the line.
    total = {0, 2, 0, 0};
    crowd::AddPush(total, S_vector(1, 1, 3));
    // along = .5: x 1 - 0 + 0, y (1 - 1) + 2, z 3
    CHECK(Near(total.x, 1.0f) && Near(total.y, 2.0f) && Near(total.z, 3.0f));
    total = {2, 0, 0, 0};
    crowd::AddPush(total, S_vector(1, 3, 1));
    CHECK(Near(total.x, 2.0f) && Near(total.y, 3.0f) && Near(total.z, 1.0f));
    // Along by 1.2: over one, not over 1.5.
    total = {1, 0, 0, 0};
    crowd::AddPush(total, S_vector(1.2f, 0, 0));
    CHECK(Near(total.x, 1.2f));

    // The box of a car.
    S_FakeCar *car = MakeCar(0, 0, 1.0f, 2.0f);
    float *box = reinterpret_cast<float *>(car->carBytes + 0x210);
    box[0] = -1; box[1] = -2; box[2] = -3; box[4] = 1; box[5] = 2; box[6] = 3;
    const S_vector half = crowd::CarHalfSize(car->car());
    CHECK(Near(half.x, 1.1f) && Near(half.y, 2.2f) && Near(half.z, 3.3f));
    // The strip's pull.
    S_vector pos(5, 7, 3);
    CHECK(!crowd::KeepInsideStrip(pos, S_vector(0, 0, 0), 1.0f, S_vector(10, 0, 0), 1.0f));
    CHECK(Near(pos.x, 5.0f) && Near(pos.z, 1.0f) && pos.y == 7);
    pos = S_vector(-3, 2, 4);
    CHECK(!crowd::KeepInsideStrip(pos, S_vector(0, 1, 0), 1.0f, S_vector(10, 0, 0), 1.0f));
    CHECK(Near(pos.x, -0.6f) && Near(pos.z, 0.8f) && pos.y == 2);
    pos = S_vector(13, 2, 4);
    CHECK(!crowd::KeepInsideStrip(pos, S_vector(0, 0, 0), 1.0f, S_vector(10, 0, 0), 1.0f));
    CHECK(Near(pos.x, 10.6f) && Near(pos.z, 0.8f));
    pos = S_vector(5, 0, 0.5f);
    CHECK(crowd::KeepInsideStrip(pos, S_vector(0, 0, 0), 1.0f, S_vector(10, 0, 0), 1.0f));
    // Wider at one end.
    pos = S_vector(5, 0, 1.4f);
    CHECK(crowd::KeepInsideStrip(pos, S_vector(0, 0, 0), 1.0f, S_vector(10, 0, 0), 2.0f));
    pos = S_vector(5, 0, 1.6f);
    CHECK(!crowd::KeepInsideStrip(pos, S_vector(0, 0, 0), 1.0f, S_vector(10, 0, 0), 2.0f));
    CHECK(Near(pos.z, 1.5f));
    // The same point twice: its circle.
    pos = S_vector(0, 0, 3);
    CHECK(!crowd::KeepInsideStrip(pos, S_vector(0, 0, 0), 1.0f, S_vector(0, 0, 0), 0.5f));
    CHECK(Near(pos.z, 1.0f));

    // The corners.
    S_vector corners[4];
    int negative[4], positive[4], negativeCount = 0, positiveCount = 0;
    crowd::SortCorners(S_vector(-4, 0, 0), S_vector(0, 0, -1), S_vector(1, 0, 0), corners, negative, positive, &negativeCount, &positiveCount);
    CHECK(negativeCount == 2 && positiveCount == 2);
    CHECK(Near(corners[0].x, 5) && Near(corners[0].z, 1) && Near(corners[3].x, 3) && Near(corners[3].z, -1));
    CHECK(negative[0] == 2 && negative[1] == 0 && positive[0] == 3 && positive[1] == 1);
    crowd::SortCorners(S_vector(4, 0, 0), S_vector(0, 0, -1), S_vector(-1, 0, 0), corners, negative, positive, &negativeCount, &positiveCount);
    CHECK(negative[0] == 0 && negative[1] == 2 && positive[0] == 1 && positive[1] == 3);
    crowd::SortCorners(S_vector(0, 0, 0), S_vector(1, 0, 1), S_vector(1, 0, 1), corners, negative, positive, &negativeCount, &positiveCount);
    CHECK(negativeCount == 1 && positiveCount == 3 && negative[0] == 3);
    CHECK(positive[0] == 2 && positive[1] == 1 && positive[2] == 0);
}

void TestIsPointBlockedDirect()
{
    C_HumanCrowd &crowd = g_humanCrowd;
    ResetAll();
    SetCars({});
    AddPoint(0, 0, 15.0f);
    S_Fake *self = MakeHuman(0, 0), *other = MakeHuman(5, 0);
    Meet(self, other);
    SyncNearbyCounts();
    C_HumanAIMemory &o = *other->memory;
    o.pos = {5, 0, 0, 0};
    const auto at = [&](float x, float y, float z) { return crowd.IsPointBlocked(*self->memory, S_vector4{x, y, z, 0}); };
    CHECK(at(5.7f, 0, 0));
    CHECK(at(5, 0, 0.7f));
    CHECK(!at(5.9f, 0, 0));
    CHECK(!at(5, 0, 0.9f));
    CHECK(at(5.5f, 1.9f, 0));
    CHECK(!at(5.5f, 2.1f, 0));
    o.flags = 1;
    CHECK(!at(5.5f, 0, 0));
    o.flags = 0x20;
    CHECK(!at(5.5f, 0, 0));
    o.flags = 8;                                         // (having an order does not count)
    CHECK(at(5.5f, 0, 0));
    o.flags = 0;
    other->controller.busy = true;
    CHECK(!at(5.5f, 0, 0));
    other->controller.busy = false;
    other->suspended = true;
    CHECK(!at(5.5f, 0, 0));
    CHECK(o.flags == 4);
    other->suspended = false;
    o.flags = 0;
    // Lying: its ellipse.
    other->stance = 2;
    o.axisX = {1, 0, 0, 0};
    o.axisY = {0, 1, 0, 0};
    o.axisZ = {0, 0, 1, 0};
    o.halfSize[0] = 0.7f; o.halfSize[1] = 0.3f; o.halfSize[2] = 1.3f;
    CHECK(at(5, 0, 1.6f));
    CHECK(!at(5, 0, 1.8f));
    CHECK(at(6.0f, 0, 0));
    CHECK(!at(6.2f, 0, 0));
    CHECK(!at(5.8f, 0, 1.4f));
    CHECK(at(5, 1.9f, 1.6f));
    CHECK(!at(5, 2.1f, 1.6f));
    o.axisX = {0, 0, 1, 0};
    o.axisZ = {1, 0, 0, 0};
    CHECK(at(6.6f, 0, 0));
    CHECK(!at(6.8f, 0, 0));
    other->stance = 0;
    // Cars standing.
    self->nearby.clear();
    SyncNearbyCounts();
    S_FakeCar *car = MakeCar(0, 10, 1.0f, 2.0f);
    SetCars({car});
    CHECK(at(0, 0, 12.3f));
    CHECK(!at(0, 0, 12.5f));
    CHECK(at(1.3f, 0, 10));
    CHECK(!at(1.5f, 0, 10));
    CHECK(!at(1.2f, 0, 12.2f));
    CHECK(at(0, 1.9f, 12.2f));
    CHECK(!at(0, 2.1f, 12.2f));
    car->car()->m_vehicle.m_speed = 1.0f;
    CHECK(!at(0, 0, 11));
    car->car()->m_vehicle.m_speed = 0.0f;
    self->human()->m_vehicle = reinterpret_cast<C_Actor *>(car->car());
    CHECK(!at(0, 0, 11));
    self->human()->m_vehicle = nullptr;
    // A big one, too far by the measure of 7 m, though in its box.
    S_FakeCar *huge = MakeCar(0, 10, 20.0f, 20.0f);
    SetCars({huge});
    CHECK(at(0, 0, 16.9f));
    CHECK(!at(0, 0, 17.1f));
    // All the cars are looked at.
    SetCars({car, huge});
    CHECK(at(0, 0, 12.3f));
    SetCars({huge, car});
    CHECK(at(0, 0, 12.3f));
    CHECK(!at(40, 0, 12.3f));
    SetCars({});
}

// What a blocker test looks at.
struct S_Block {
    S_vector4 left[4], right[4];
    int leftCount = 0, rightCount = 0;
    C_Human *human = nullptr;
    C_Car *car = nullptr;
    int kind = -1;
    S_PlanNode from, to;
};
void FindIt(S_Block &b, C_HumanAIMemory &memory, float ax, float az, float tx, float tz, const S_Checkpoint *point, S_PlanNode *to = nullptr)
{
    std::memset(&b.from, 0, sizeof(b.from));
    if (!to) {
        std::memset(&b.to, 0, sizeof(b.to));
        to = &b.to;
    }
    b.from.pos = {ax, 0, az, 0};
    b.from.point = point;
    to->pos = {tx, 0, tz, 0};
    b.human = nullptr;
    b.car = nullptr;
    b.leftCount = b.rightCount = 0;
    b.kind = g_humanCrowd.FindBlocker(memory, &b.from, to, b.left, &b.leftCount, b.right, &b.rightCount, &b.human, &b.car);
}

void TestFindBlockerDirect()
{
    ResetAll();
    SetCars({});
    const int base = AddPoint(5, 0, 15.0f);
    const S_Checkpoint *pt = &g_net.points[base];
    S_Fake *self = MakeHuman(0, 0), *other = MakeHuman(5, 0);
    Meet(self, other);
    SyncNearbyCounts();
    C_HumanAIMemory &m = *self->memory, &o = *other->memory;
    S_Block b;
    // Off the way a bit; ahead, in reach.
    o.pos = {5, 0, 0.1f, 0};
    FindIt(b, m, 0, 0, 10, 0, pt);
    CHECK(b.kind == 1 && b.human == other->human() && b.car == nullptr);
    CHECK(b.leftCount == 1 && b.rightCount == 1);
    CHECK(b.to.point == pt);
    const float side = -0.1f * crowd::FastInvSqrt(0.01f) * 0.8f;
    CHECK(Near(b.left[0].x, 5.0f) && Near(b.left[0].z, 0.1f + side, 0.002f));
    CHECK(Near(b.right[0].x, 5.0f) && Near(b.right[0].z, 0.1f - side, 0.002f));
    // Short of it, in reach of its circle: a second kind.
    FindIt(b, m, 0, 0, 5, 0, pt);
    CHECK(b.kind == 2 && b.human == other->human());
    // Not reaching it.
    FindIt(b, m, 0, 0, 3, 0, pt);
    CHECK(b.kind == 1 && b.human == nullptr);
    // On the line exactly: across by its radius.
    o.pos = {5, 0, 0, 0};
    FindIt(b, m, 0, 0, 10, 0, pt);
    const float inv = crowd::FastInvSqrt(100.0f) * 0.8f;
    CHECK(Near(b.left[0].z, -10.0f * inv) && Near(b.right[0].z, 10.0f * inv) && b.left[0].x == 5.0f);
    FindIt(b, m, 0, 0, 0, 10, pt);
    CHECK(b.kind == 1 && b.human == nullptr);
    o.pos = {0, 0, 5, 0};
    FindIt(b, m, 0, 0, 0, 10, pt);
    CHECK(Near(b.left[0].x, 10.0f * inv) && Near(b.right[0].x, -10.0f * inv));
    o.pos = {5, 0, 0, 0};
    // Standing inside it at the start: from there; with the first root.
    o.pos = {0.1f, 0, 0, 0};
    FindIt(b, m, 0, 0, 10, 0, pt);
    CHECK(b.human == other->human());
    // Behind; wide; another floor; out; suspended.
    o.pos = {-5, 0, 0, 0};
    FindIt(b, m, 0, 0, 10, 0, pt);
    CHECK(b.human == nullptr && b.kind == 1);
    o.pos = {5, 0, 0.9f, 0};
    FindIt(b, m, 0, 0, 10, 0, pt);
    CHECK(b.human == nullptr);
    o.pos = {5, 0, 0.7f, 0};
    FindIt(b, m, 0, 0, 10, 0, pt);
    CHECK(b.human == other->human());
    o.pos = {5, 2.1f, 0, 0};
    FindIt(b, m, 0, 0, 10, 0, pt);
    CHECK(b.human == nullptr);
    o.pos = {5, 1.9f, 0, 0};
    FindIt(b, m, 0, 0, 10, 0, pt);
    CHECK(b.human == other->human());
    o.pos = {5, 0, 0, 0};
    o.flags = 1;
    FindIt(b, m, 0, 0, 10, 0, pt);
    CHECK(b.human == nullptr);
    o.flags = 0x20;
    FindIt(b, m, 0, 0, 10, 0, pt);
    CHECK(b.human == nullptr);
    o.flags = 0;
    other->suspended = true;
    FindIt(b, m, 0, 0, 10, 0, pt);
    CHECK(b.human == nullptr && o.flags == 4);
    other->suspended = false;
    o.flags = 0;
    // Gone round already.
    S_PlanNode to;
    std::memset(&to, 0, sizeof(to));
    to.avoided[0] = other->human();
    to.avoidedCount = 1;
    FindIt(b, m, 0, 0, 10, 0, pt, &to);
    CHECK(b.human == nullptr && b.kind == 1);
    S_Fake *third = MakeHuman(3, 0);
    to.avoided[0] = third->human();
    to.avoided[1] = other->human();
    to.avoidedCount = 2;
    FindIt(b, m, 0, 0, 10, 0, pt, &to);
    CHECK(b.human == nullptr);
    // No point; off the network; no way at all.
    FindIt(b, m, 0, 0, 10, 0, nullptr);
    CHECK(b.kind == 0);
    FindIt(b, m, 0, 0, 40, 0, pt);
    CHECK(b.kind == 0);
    FindIt(b, m, 2, 2, 2, 2, pt);
    CHECK(b.kind == 0);
    // Of two, the nearer, whichever comes first.
    third->memory->pos = {2.5f, 0, 0, 0};
    Meet(self, third);
    SyncNearbyCounts();
    FindIt(b, m, 0, 0, 10, 0, pt);
    CHECK(b.human == other->human() || b.human == third->human());
    CHECK(b.human == third->human() && Near(b.left[0].x, 2.5f));
    std::swap(self->nearby[0], self->nearby[1]);
    FindIt(b, m, 0, 0, 10, 0, pt);
    CHECK(b.human == third->human() && Near(b.left[0].x, 2.5f));
    self->nearby.pop_back();
    SyncNearbyCounts();
    self->nearby.clear();
    self->nearby.push_back(other->human());
    SyncNearbyCounts();

    // Lying across the way: the corners of its box.
    other->stance = 2;
    o.pos = {5, 0, 0, 0};
    o.axisX = {1, 0, 0, 0};
    o.axisY = {0, 1, 0, 0};
    o.axisZ = {0, 0, 1, 0};
    o.halfSize[0] = 0.7f; o.halfSize[1] = 0.3f; o.halfSize[2] = 1.3f;
    FindIt(b, m, 0, 0, 10, 0, pt);
    CHECK(b.kind == 1 && b.human == other->human() && b.leftCount == 2 && b.rightCount == 2);
    CHECK(Near(b.left[0].x, 3.9f, 0.01f) && Near(b.left[0].z, -1.7f, 0.01f) && Near(b.left[1].x, 6.1f, 0.01f) && Near(b.left[1].z, -1.7f, 0.01f));
    CHECK(Near(b.right[0].x, 3.9f, 0.01f) && Near(b.right[0].z, 1.7f, 0.01f) && Near(b.right[1].x, 6.1f, 0.01f) && Near(b.right[1].z, 1.7f, 0.01f));
    // Not reaching: nothing. Short of its far side: the other kind.
    FindIt(b, m, 0, 0, 3, 0, pt);
    CHECK(b.human == nullptr);
    FindIt(b, m, 0, 0, 5, 0, pt);
    CHECK(b.kind == 2 && b.human == other->human());
    // Off to the side a little: which side has how many.
    o.pos = {5, 0, 0.5f, 0};
    FindIt(b, m, 0, 0, 10, 0, pt);
    CHECK(b.human == other->human() && b.leftCount + b.rightCount == 4);
    CHECK(Near(b.left[0].z, 0.5f - 1.7f, 0.01f) && Near(b.right[0].z, 0.5f + 1.7f, 0.01f));
    // Wide of it; behind it; floors; inside it.
    o.pos = {5, 0, 2.0f, 0};
    FindIt(b, m, 0, 0, 10, 0, pt);
    CHECK(b.human == nullptr);
    o.pos = {-5, 0, 0, 0};
    FindIt(b, m, 0, 0, 10, 0, pt);
    CHECK(b.human == nullptr);
    o.pos = {5, 2.5f, 0, 0};
    FindIt(b, m, 0, 0, 10, 0, pt);
    CHECK(b.human == nullptr);
    o.pos = {5, 1.9f, 0, 0};
    FindIt(b, m, 0, 0, 10, 0, pt);
    CHECK(b.human == other->human());
    o.pos = {0.5f, 0, 0, 0};
    FindIt(b, m, 0, 0, 10, 0, pt);
    CHECK(b.human == other->human());
    other->stance = 0;
    self->nearby.clear();
    SyncNearbyCounts();

    // A standing car.
    S_FakeCar *car = MakeCar(5, 0, 1.0f, 2.0f);
    SetCars({car});
    FindIt(b, m, 0, 0, 10, 0, pt);
    CHECK(b.kind == 1 && b.human == nullptr && b.car == car->car() && b.leftCount == 2 && b.rightCount == 2);
    CHECK(Near(b.left[0].x, 5.0f - 1.05f * 1.4f, 0.02f) && Near(b.left[0].z, -1.05f * 2.4f, 0.02f));
    CHECK(Near(b.left[1].x, 5.0f + 1.05f * 1.4f, 0.02f));
    CHECK(Near(b.right[0].x, 5.0f - 1.05f * 1.4f, 0.02f) && Near(b.right[0].z, 1.05f * 2.4f, 0.02f));
    FindIt(b, m, 0, 0, 5, 0, pt);
    CHECK(b.kind == 2 && b.car == car->car());
    FindIt(b, m, 0, 0, 3, 0, pt);
    CHECK(b.car == nullptr);
    FindIt(b, m, 4.5f, 0.5f, 10, 0.5f, pt);             // (inside: at once)
    CHECK(b.car == car->car());
    FindIt(b, m, 0, 3, 10, 3, pt);                      // (wide)
    CHECK(b.car == nullptr);
    FindIt(b, m, 10, 0, 20, 0, pt);                     // (going away)
    CHECK(b.car == nullptr);
    car->car()->m_vehicle.m_speed = 1.0f;
    FindIt(b, m, 0, 0, 10, 0, pt);
    CHECK(b.car == nullptr);
    car->car()->m_vehicle.m_speed = 0.0f;
    self->human()->m_vehicle = reinterpret_cast<C_Actor *>(car->car());
    FindIt(b, m, 0, 0, 10, 0, pt);
    CHECK(b.car == nullptr);
    self->human()->m_vehicle = nullptr;
    std::memset(&to, 0, sizeof(to));
    to.avoidedCar = car->car();
    FindIt(b, m, 0, 0, 10, 0, pt, &to);
    CHECK(b.car == nullptr);
    reinterpret_cast<float *>(car->frameBytes + 0x40)[13] = 2.5f;
    FindIt(b, m, 0, 0, 10, 0, pt);
    CHECK(b.car == nullptr);
    reinterpret_cast<float *>(car->frameBytes + 0x40)[13] = 0.0f;
    // Too far off at both ends.
    S_FakeCar *distant = MakeCar(-20, 0, 1.0f, 2.0f);
    SetCars({distant});
    FindIt(b, m, 0, 0, 10, 0, pt);
    CHECK(b.car == nullptr);
    // A human before it wins; a car before the human does.
    self->nearby.push_back(other->human());
    SyncNearbyCounts();
    o.pos = {2, 0, 0.1f, 0};
    SetCars({car});
    FindIt(b, m, 0, 0, 10, 0, pt);
    CHECK(b.human == other->human() && b.car == nullptr);
    CHECK(Near(b.left[0].x, 2.0f));
    o.pos = {8, 0, 0.1f, 0};
    FindIt(b, m, 0, 0, 10, 0, pt);
    CHECK(b.human == nullptr && b.car == car->car());
    // Two cars: the nearer.
    S_FakeCar *second = MakeCar(1.5f, 7, 1.0f, 2.0f);
    self->nearby.clear();
    SyncNearbyCounts();
    SetCars({car, second});
    FindIt(b, m, 0, 0, 0, 7, pt);
    CHECK(b.car == second->car() || b.car == nullptr);
    SetCars({});
}

void TestSideWayDirect()
{
    C_HumanCrowd &crowd = g_humanCrowd;
    ResetAll();
    S_Fake *h = MakeHuman(0, 0);
    S_FakeCar *car = MakeCar(0, 0);
    S_vector4 corners[3] = {{1, 0, 1, 0}, {2, 0, 2, 0}, {3, 0, 3, 0}};
    S_PlanNode &node = crowd.m_nodes[0], &child = crowd.m_nodes[1];
    const auto reset = [&]() {
        std::memset(crowd.m_nodes, 0, sizeof(crowd.m_nodes));
        crowd.m_nodeCount = 2;
        node.children[0] = reinterpret_cast<uintptr_t>(&child);
        node.childCount = 1;
        child.pos = {9, 0, 9, 0};
    };
    // A chain of two, then the way on.
    reset();
    child.children[0] = 1234;
    crowd.AddSideWay(&node, &child, 1, corners, 2, h->human(), nullptr);
    CHECK(crowd.m_nodeCount == 4);
    S_PlanNode *first = reinterpret_cast<S_PlanNode *>(node.children[1]);
    CHECK(first == &crowd.m_nodes[2] && node.childCount == 2);
    CHECK(first->pos.x == 1 && first->childCount == 1 && first->avoidedCount == 1 && first->avoided[0] == h->human() && first->point == nullptr && first->avoidedCar == nullptr);
    S_PlanNode *second = reinterpret_cast<S_PlanNode *>(first->children[0]);
    CHECK(second == &crowd.m_nodes[3] && second->pos.x == 2 && second->childCount == 1);
    CHECK(reinterpret_cast<S_PlanNode *>(second->children[0]) == &child);
    CHECK(child.avoidedCount == 1 && child.avoided[0] == h->human());
    // Again the same way from somewhere else: not twice over by the slot... kept as the original has it.
    reset();
    crowd.AddSideWay(&node, &child, 1, corners, 1, nullptr, car->car());
    S_PlanNode *one = reinterpret_cast<S_PlanNode *>(node.children[1]);
    CHECK(one->avoidedCount == 0 && one->avoidedCar == car->car());
    CHECK(child.avoidedCount == 0 && child.avoidedCar == car->car());
    // Its avoided full: no more.
    reset();
    child.avoidedCount = CROWD_MAX_AVOIDED;
    crowd.AddSideWay(&node, &child, 1, corners, 1, h->human(), nullptr);
    CHECK(child.avoidedCount == CROWD_MAX_AVOIDED);
    // The second kind: takes on what the child has.
    reset();
    child.children[0] = 11;
    child.children[1] = 22;
    child.childCount = 2;
    crowd.AddSideWay(&node, &child, 2, corners, 1, h->human(), nullptr);
    one = reinterpret_cast<S_PlanNode *>(node.children[1]);
    CHECK(one->children[0] == 11 && one->children[1] == 22 && one->childCount == 2);
    CHECK(child.avoidedCount == 0);
    // No corners; full of children; known already.
    reset();
    crowd.AddSideWay(&node, &child, 1, corners, 0, h->human(), nullptr);
    CHECK(crowd.m_nodeCount == 2);
    node.childCount = CROWD_MAX_CHILDREN;
    for (int i = 1; i < CROWD_MAX_CHILDREN; ++i)
        node.children[i] = reinterpret_cast<uintptr_t>(&child);
    crowd.AddSideWay(&node, &child, 1, corners, 1, h->human(), nullptr);
    CHECK(crowd.m_nodeCount == 2);
    reset();
    node.children[1] = reinterpret_cast<uintptr_t>(&crowd.m_nodes[5]) | 1;
    node.children[2] = reinterpret_cast<uintptr_t>(&child);
    node.childCount = 3;
    crowd.m_nodes[5].pos = corners[0];
    crowd.AddSideWay(&node, &child, 1, corners, 1, h->human(), nullptr);
    CHECK(crowd.m_nodeCount == 2);
    node.children[1] = reinterpret_cast<uintptr_t>(&child);
    node.children[0] = reinterpret_cast<uintptr_t>(&crowd.m_nodes[5]);
    crowd.AddSideWay(&node, &child, 1, corners, 1, h->human(), nullptr);
    CHECK(crowd.m_nodeCount == 2);
    // No room left: it stops.
    reset();
    crowd.m_nodeCount = CROWD_MAX_NODES - 1;
    crowd.AddSideWay(&node, &child, 1, corners, 2, h->human(), nullptr);
    CHECK(crowd.m_nodeCount == CROWD_MAX_NODES);
    CHECK(child.avoidedCount == 0);
    crowd.m_nodeCount = 2;
}

void TestExpandDirect()
{
    C_HumanCrowd &crowd = g_humanCrowd;
    ResetAll();
    SetCars({});
    const int from = AddPoint(0, 0, 15.0f), to = AddPoint(10, 0, 15.0f);
    Link(from, to);
    S_Fake *self = MakeHuman(0, 0), *other = MakeHuman(5, 0);
    C_HumanAIMemory &m = *self->memory;
    SyncNearbyCounts();
    other->memory->pos = {5, 0, 0.1f, 0};
    S_PlanNode &root = crowd.m_nodes[0], &goal = crowd.m_nodes[1];
    const auto reset = [&]() {
        std::memset(crowd.m_nodes, 0, sizeof(crowd.m_nodes));
        crowd.m_nodeCount = 2;
        root.pos = {0, 0, 0, 0};
        root.point = &g_net.points[from];
        goal.pos = {10, 0, 0, 0};
        crowd.m_goal = goal.pos;
        crowd.m_goalPoint = &g_net.points[to];
        root.children[0] = reinterpret_cast<uintptr_t>(&goal);
        root.childCount = 1;
    };
    // At the goal: done, with its point.
    reset();
    goal.childCount = 3;
    crowd.ExpandNode(m, &goal, 1);
    CHECK(goal.childCount == 0 && goal.point == &g_net.points[to]);
    // Nothing in the way: on down; the goal's point stays.
    reset();
    crowd.ExpandNode(m, &root, 0);
    CHECK(crowd.m_nodeCount == 2 && goal.point == &g_net.points[to] && root.children[0] == reinterpret_cast<uintptr_t>(&goal));
    // At the depth: nothing done.
    reset();
    goal.pos = {99, 0, 0, 0};
    crowd.ExpandNode(m, &root, CROWD_MAX_PATH);
    CHECK(goal.point == nullptr);
    // A way marked closed is not looked at.
    reset();
    root.children[0] |= 1;
    crowd.ExpandNode(m, &root, 0);
    CHECK(goal.point == nullptr);
    // Off the network: closed.
    reset();
    goal.pos = {50, 0, 0, 0};
    crowd.m_goal = {99, 0, 0, 0};
    crowd.ExpandNode(m, &root, 0);
    CHECK(root.children[0] == (reinterpret_cast<uintptr_t>(&goal) | 1));
    // Blocked: two ways round, this one closed, each taken on to the goal.
    Meet(self, other);
    SyncNearbyCounts();
    reset();
    crowd.ExpandNode(m, &root, 0);
    CHECK(root.children[0] == (reinterpret_cast<uintptr_t>(&goal) | 1));
    CHECK(root.childCount == 3 && crowd.m_nodeCount == 4);
    S_PlanNode *left = reinterpret_cast<S_PlanNode *>(root.children[1] & ~1u), *right = reinterpret_cast<S_PlanNode *>(root.children[2] & ~1u);
    CHECK(left->pos.z < 0.0f && right->pos.z > 0.0f);
    CHECK(left->point == &g_net.points[from] || left->point == &g_net.points[to]);
    CHECK(right->point != nullptr);
    CHECK(left->children[0] == reinterpret_cast<uintptr_t>(&goal) && right->children[0] == reinterpret_cast<uintptr_t>(&goal));
    // Short of room: the second is not tried.
    reset();
    crowd.m_nodeCount = CROWD_MAX_NODES - 1;
    crowd.ExpandNode(m, &root, 0);
    CHECK(crowd.m_nodeCount == CROWD_MAX_NODES && root.childCount == 2);
    CHECK(root.children[0] == reinterpret_cast<uintptr_t>(&goal));
    crowd.m_nodeCount = 2;
    // A child with no point takes one near its parent's.
    self->nearby.clear();
    SyncNearbyCounts();
    reset();
    S_PlanNode &mid = crowd.m_nodes[2];
    crowd.m_nodeCount = 3;
    mid.pos = {3, 0, 3, 0};
    root.children[0] = reinterpret_cast<uintptr_t>(&mid);
    mid.children[0] = reinterpret_cast<uintptr_t>(&goal);
    mid.childCount = 1;
    crowd.ExpandNode(m, &root, 0);
    CHECK(mid.point == &g_net.points[from]);
    CHECK(goal.point == &g_net.points[to]);
}

void TestCollectDirect()
{
    C_HumanCrowd &crowd = g_humanCrowd;
    ResetAll();
    AddPoint(0, 0, 15.0f);
    S_Fake *self = MakeHuman(0, 0);
    GiveRoute(self, {0});
    C_HumanAIMemory &m = *self->memory;
    m.route = &self->route;
    m.waypoint = 0;
    self->route[0]->pos = {10, 0, 0, 0};
    S_PlanNode *path[CROWD_MAX_PATH];
    S_PlanNode &root = crowd.m_nodes[0], &a = crowd.m_nodes[1], &b = crowd.m_nodes[2], &c = crowd.m_nodes[3];
    const auto reset = [&]() {
        std::memset(crowd.m_nodes, 0, sizeof(crowd.m_nodes));
        crowd.m_bestCost = 1e8f;
        crowd.m_bestDistance = 1e8f;
        crowd.m_resultCount = 0;
        for (S_PlanNode *n : {&a, &b, &c})
            n->point = &g_net.points[0];
    };
    // The one that gets nearest the goal, not the cheapest.
    reset();
    a.pos = {5, 0, 0, 0};
    b.pos = {9, 0, 5, 0};
    root.children[0] = reinterpret_cast<uintptr_t>(&a);
    root.children[1] = reinterpret_cast<uintptr_t>(&b);
    root.childCount = 2;
    crowd.CollectBest(m, &root, 0.0f, false, path, 0);
    CHECK(crowd.m_resultCount == 1 && crowd.m_results[0].pos.x == 5.0f);
    CHECK(Near(crowd.m_bestDistance, 25.0f) && Near(crowd.m_bestCost, 25.0f));
    reset();
    a.pos = {9, 0, 5, 0};
    b.pos = {5, 0, 0, 0};
    root.children[0] = reinterpret_cast<uintptr_t>(&a);
    root.children[1] = reinterpret_cast<uintptr_t>(&b);
    root.childCount = 2;
    crowd.CollectBest(m, &root, 0.0f, false, path, 0);
    CHECK(crowd.m_results[0].pos.x == 5.0f);
    // As near: the cheaper; not a dearer.
    reset();
    a.pos = {7, 0, 4, 0};
    b.pos = {7, 0, -4, 0};
    c.pos = {10, 0, 0, 0};
    a.children[0] = reinterpret_cast<uintptr_t>(&c);
    a.childCount = 1;
    b.children[0] = reinterpret_cast<uintptr_t>(&c);
    b.childCount = 1;
    root.children[0] = reinterpret_cast<uintptr_t>(&a);
    root.children[1] = reinterpret_cast<uintptr_t>(&b);
    root.childCount = 2;
    b.pos = {7, 0, -3, 0};
    crowd.CollectBest(m, &root, 0.0f, false, path, 0);
    CHECK(crowd.m_resultCount == 2 && crowd.m_results[0].pos.z == -3.0f && crowd.m_results[1].pos.x == 10.0f);
    CHECK(crowd.m_bestDistance == 0.0f && Near(crowd.m_bestCost, 58.0f + 18.0f));
    reset();
    a.pos = {7, 0, 3, 0};
    b.pos = {7, 0, -4, 0};
    c.pos = {10, 0, 0, 0};
    a.children[0] = reinterpret_cast<uintptr_t>(&c);
    a.childCount = 1;
    b.children[0] = reinterpret_cast<uintptr_t>(&c);
    b.childCount = 1;
    root.children[0] = reinterpret_cast<uintptr_t>(&a);
    root.children[1] = reinterpret_cast<uintptr_t>(&b);
    root.childCount = 2;
    crowd.CollectBest(m, &root, 0.0f, false, path, 0);
    CHECK(crowd.m_results[0].pos.z == 3.0f);
    // Closed ways and those with no point are no way; a node with only those is the end of its way; the root with none is nothing.
    reset();
    a.pos = {5, 0, 0, 0};
    c.pos = {10, 0, 0, 0};
    a.children[0] = reinterpret_cast<uintptr_t>(&c) | 1;
    a.childCount = 1;
    root.children[0] = reinterpret_cast<uintptr_t>(&a);
    root.childCount = 1;
    crowd.CollectBest(m, &root, 0.0f, false, path, 0);
    CHECK(crowd.m_resultCount == 1 && crowd.m_results[0].pos.x == 5.0f);
    reset();
    a.pos = {5, 0, 0, 0};
    a.point = nullptr;
    root.children[0] = reinterpret_cast<uintptr_t>(&a);
    root.childCount = 1;
    crowd.CollectBest(m, &root, 0.0f, false, path, 0);
    CHECK(crowd.m_resultCount == 0);
    // Too deep.
    reset();
    root.children[0] = reinterpret_cast<uintptr_t>(&a);
    root.childCount = 1;
    crowd.CollectBest(m, &root, 0.0f, false, path, CROWD_MAX_PATH);
    CHECK(crowd.m_resultCount == 0);
}

void TestReplanDirect()
{
    C_HumanCrowd &crowd = g_humanCrowd;
    ResetAll();
    SetCars({});
    const int from = AddPoint(0, 0, 15.0f), to = AddPoint(10, 0, 15.0f), past = AddPoint(20, 0, 15.0f);
    Link(from, to);
    Link(to, past);
    g_net.points[from].type = 2;
    S_Fake *self = MakeHuman(0, 0), *other = MakeHuman(5, 0);
    Meet(self, other);
    SyncNearbyCounts();
    C_HumanAIMemory &m = *self->memory;
    const auto start = [&](std::initializer_list<int> points) {
        self->route.clear();
        GiveRoute(self, points);
        m.route = &self->route;
        m.waypoint = 0;
        m.pos = {0, 0, 0, 0};
        m.nearPoint = &g_net.points[from];
        m.plannedFrom = -1;
        m.unknown109 = true;
    };
    other->memory->pos = {5, 0, 0.1f, 0};
    start({to});
    CHECK(crowd.Replan(m));
    CHECK(!m.unknown109 && m.waypoint == 0);
    CHECK(self->route.size() == 2 && m.plannedFrom == 0);
    const float side = -0.1f * crowd::FastInvSqrt(0.01f) * 0.8f;
    CHECK(Near(self->route[0]->pos.x, 5.0f) && Near(self->route[0]->pos.z, 0.1f + side, 0.002f));
    CHECK(self->route[0]->checkpoint == from || self->route[0]->checkpoint == to);
    CHECK(self->route[1]->checkpoint == to && Near(self->route[1]->pos.x, 10.0f) && self->route[1]->pos.z == 0.0f);
    // What is after the goal stays after it, counted. (The nodes as new: a stale avoided slot counts, kept: see AddSideWay.)
    std::memset(crowd.m_nodes, 0, sizeof(crowd.m_nodes));
    start({to, past});
    CHECK(crowd.Replan(m));
    CHECK(self->route.size() == 3 && m.plannedFrom == 1);
    CHECK(self->route[1]->checkpoint == to && self->route[2]->checkpoint == past);
    // Nothing in the way: nothing.
    other->memory->pos = {5, 0, 5, 0};
    start({to, past});
    CHECK(!crowd.Replan(m));
    CHECK(self->route.size() == 2 && m.plannedFrom == -1 && !m.unknown109);
    // Waypoints stood on go; the last stays at worst.
    other->memory->pos = {10, 0, 0, 0};
    start({to, past});
    crowd.Replan(m);
    CHECK(self->route[0]->checkpoint != to || self->route.size() == 1);
    CHECK(self->route.back()->checkpoint == past);
    bool hasTo = false;
    for (const S_AIWaypoint *w : self->route)
        hasTo = hasTo || w->checkpoint == to;
    CHECK(!hasTo);
    start({to});
    CHECK(!crowd.Replan(m) || self->route.size() >= 1);
    CHECK(self->route.back()->checkpoint == to);
    // From a later waypoint it counts from there.
    other->memory->pos = {5, 0, 5, 0};
    start({to, past});
    m.waypoint = 1;
    crowd.Replan(m);
    CHECK(self->route.size() == 1 && self->route[0]->checkpoint == past && m.waypoint == 0);
    // A goal that cannot be reached: the way ends short; the goal stays after it.
    ResetAll();
    SetCars({});
    const int a2 = AddPoint(0, 0, 15.0f), b2 = AddPoint(10, 0, 15.0f);
    Link(a2, b2);
    S_Fake *s2 = MakeHuman(0, 0), *o2 = MakeHuman(10, 0, 2);
    Meet(s2, o2);
    SyncNearbyCounts();
    C_HumanAIMemory &n = *s2->memory, &oo = *o2->memory;
    oo.pos = {5, 0, 0, 0};
    oo.axisX = {1, 0, 0, 0};
    oo.axisY = {0, 1, 0, 0};
    oo.axisZ = {0, 0, 1, 0};
    oo.halfSize[0] = 0.7f; oo.halfSize[1] = 0.3f; oo.halfSize[2] = 1.3f;
    GiveRoute(s2, {b2});
    n.route = &s2->route;
    n.waypoint = 0;
    n.pos = {0, 0, 0, 0};
    n.nearPoint = &g_net.points[a2];
    CHECK(crowd.Replan(n));
    CHECK(s2->route.size() == 3);
    CHECK(Near(s2->route[0]->pos.x, 3.9f, 0.01f) && Near(s2->route[1]->pos.x, 6.1f, 0.01f) && s2->route[2]->checkpoint == b2);
}

void Prone(C_HumanAIMemory &o)
{
    o.axisX = {1, 0, 0, 0};
    o.axisY = {0, 1, 0, 0};
    o.axisZ = {0, 0, 1, 0};
    o.halfSize[0] = 0.7f; o.halfSize[1] = 0.3f; o.halfSize[2] = 1.3f;
}

void TestConeNumbers()
{
    ResetAll();
    AddPoint(0, 0, 15.0f);
    S_Fake *a = MakeHuman(0, 0), *b = MakeHuman(0, 0);
    C_HumanAIMemory &self = *Memory(a, 0, 0), &other = *Memory(b, 2.0f, 0.1f);
    self.velocity = {2, 1, 0, 0};                        // (height does not count)
    crowd::PushFromHuman(self, other);
    // Worked by hand: a = 4, half = -4, c = 3.37, t = (8 - sqrt(10.08)) / 8
    const float t = (8.0f - std::sqrt(10.08f)) / 8.0f;
    const float inverse = 0.25f;
    const float v = (-0.1f * -2.0f) * inverse;
    float force = 1.0f - (2.0f * v) * (2.0f * v) * 1.5625f;
    force *= 0.4f;
    force *= crowd::FastInvSqrt(4.0f);
    const float s = (1.0f - t) * 0.125f;
    CHECK(Near(self.push.z, -2.0f * force * s, 1e-5f) && Near(self.push.x, 0.0f, 1e-5f) && self.push.y == 0.0f);
    // On the other side.
    self.push = {};
    other.pos = {2.0f, 0, -0.1f, 0};
    crowd::PushFromHuman(self, other);
    CHECK(Near(self.push.z, 2.0f * force * s, 1e-5f));
    // The other's velocity counts: coming at half of it.
    self.push = {};
    self.velocity = {1, 0, 0, 0};
    other.velocity = {-1, 0, 0, 0};
    other.pos = {2.0f, 0, 0.1f, 0};
    crowd::PushFromHuman(self, other);
    CHECK(Near(self.push.z, -2.0f * force * s, 1e-5f));
    other.velocity = {};
    // Passing just inside it, just outside it.
    self.velocity = {2, 0, 0, 0};
    self.push = {};
    other.pos = {2.0f, 0, 0.79f, 0};
    crowd::PushFromHuman(self, other);
    CHECK(self.push.z != 0.0f);
    self.push = {};
    other.pos = {2.0f, 0, 0.81f, 0};
    crowd::PushFromHuman(self, other);
    CHECK(self.push.z == 0.0f);

    // Lying: the sphere's rule, in its own coordinates.
    b->stance = 2;
    Prone(other);
    other.pos = {3.0f, 0, 0.1f, 0};
    self.push = {};
    self.velocity = {2, 1, 0, 0};
    crowd::PushFromHuman(self, other);
    S_vector aside;
    const S_vector l(-3.0f / 1.1f, 0, -0.1f / 1.7f), r(2.0f / 1.1f, 0, 0);
    CHECK(crowd::SideStepEllipsoid(l, r, l.x * l.x + l.z * l.z - 1.0f, 0.125f, &aside));
    CHECK(Near(self.push.x, aside.x * 1.1f, 1e-5f) && Near(self.push.z, aside.z * 1.7f, 1e-5f) && self.push.y == 0.0f);
    b->stance = 0;

    // Two targets, heights apart: not in the sum.
    S_Fake *c = MakeHuman(0, 0), *d = MakeHuman(0, 0);
    C_HumanAIMemory &p = *Memory(c, 0, 0), &q = *Memory(d, 0, 0);
    GiveRoute(c, {0});
    GiveRoute(d, {0});
    p.route = &c->route;
    q.route = &d->route;
    p.targetPoint = q.targetPoint = nullptr;
    p.target = {0, 1, 0, 0};
    q.target = {0.4f, 0, 0, 0};
    crowd::PushTargetFromHuman(p, q);
    const float by = 0.4f * (0.75f / 0.4f) * 0.4f;
    CHECK(Near(p.target.x, -by) && Near(p.target.y, 1.0f) && Near(q.target.x, 0.4f + by) && q.target.y == 0.0f);
    // Kept to the network with its height.
    ResetAll();
    AddPoint(0, 0, 1.0f);
    S_Fake *e = MakeHuman(0, 0);
    C_HumanAIMemory &m = *Memory(e, 0, 0);
    m.targetPoint = &g_net.points[0];
    m.target = {0.5f, 2, 0, 0};
    crowd::MoveTarget(m, S_vector(0.1f, 0.25f, 0.1f));
    CHECK(Near(m.target.x, 0.6f) && Near(m.target.y, 2.25f) && Near(m.target.z, 0.1f));
    m.target = {0.5f, 2, 0, 0};
    crowd::MoveTarget(m, S_vector(5, 0, 0));
    CHECK(Near(m.target.x, 1.0f) && Near(m.target.y, 2.0f));

    // A push turned aside only when nearly straight back: .1 of it across is not.
    S_Fake *f = MakeHuman(0, 0);
    C_HumanAIMemory &t2 = *Memory(f, 0, 0);
    t2.velocity = {0, 0, 2, 0};
    t2.push = {0.316f, 0, -1, 0};
    crowd::TurnPushAside(t2, 0.5f);
    CHECK(Near(t2.push.x, 0.316f));
    t2.push = {0.26f, 0, -1, 0};
    crowd::TurnPushAside(t2, 0.5f);
    CHECK(Near(t2.push.x, 2.26f));
    CHECK(t2.push.y == 0.0f);
}

void TestPassingWaypoints()
{
    // Each leg by its own start.
    ResetAll();
    AddPoint(10, 0, 15.0f);
    AddPoint(10, 10, 15.0f);
    AddPoint(20, 10, 15.0f);
    S_Fake *fake = MakeHuman(0, 0);
    GiveRoute(fake, {0, 1, 2});
    C_HumanAIMemory &m = *Memory(fake, 0, 0);
    m.route = &fake->route;
    m.waypoint = 0;
    m.velocity = {};
    m.lastGoal = {0, 0, -5, 0};
    m.pos = {9.5f, 0, 10.1f, 0};
    crowd::AdvanceWaypoint(m, 0.5f);
    CHECK(m.waypoint == 2);
    // As near as .05: on; a little more: not.
    GiveRoute(fake, {});
    fake->route.clear();
    GiveRoute(fake, {0, 2});
    m.waypoint = 0;
    m.lastGoal = {0, 0, 0, 0};
    m.pos = {9.945f, 0, 0, 0};
    crowd::AdvanceWaypoint(m, 0.5f);
    CHECK(m.waypoint == 0);
    m.pos = {9.96f, 0, 0, 0};
    crowd::AdvanceWaypoint(m, 0.5f);
    CHECK(m.waypoint == 1);
    // A step does it too.
    m.waypoint = 0;
    m.pos = {9.96f, 0, 0, 0};
    m.speed = 1.0f;
    crowd::MoveOneStep(m, 0.01f);
    CHECK(m.waypoint == 1 && m.target.x == 20);
}

void TestCarPushNumbers()
{
    C_HumanCrowd &crowd_ = g_humanCrowd;
    ResetAll();
    SetCars({});
    AddPoint(0, 0, 15.0f);
    S_FakeCar *car = MakeCar(0, 0, 1.0f, 2.0f);
    SetCars({car});
    S_Fake *fake = MakeHuman(0, 0);
    C_HumanAIMemory &m = *Memory(fake, 0, 0);
    // Inside: out to its surface.
    m.pos = {1, 0, 0, 0};
    crowd_.PushFromCar(m, 0, 0.0125f);
    CHECK(Near(m.push.x, 0.4f) && Near(m.push.z, 0.0f) && m.push.y == 0.0f);
    m.push = {};
    m.pos = {0, 0, 1.2f, 0};
    crowd_.PushFromCar(m, 0, 0.0125f);
    CHECK(Near(m.push.z, 1.2f));
    // In its middle: nowhere.
    m.push = {};
    m.pos = {0, 0, 0, 0};
    crowd_.PushFromCar(m, 0, 0.0125f);
    CHECK(m.push.x == 0.0f && m.push.z == 0.0f);
    // Coming at it: the sphere's rule; by the step; going by its height no difference.
    m.pos = {-3, 2, 0.1f, 0};
    m.velocity = {2, 3, 0, 0};
    crowd_.PushFromCar(m, 0, 0.25f);
    const S_vector l(-3.0f / 1.4f, 0, 0.1f / 2.4f), r(2.0f / 1.4f, 0, 0);
    S_vector aside;
    CHECK(crowd::SideStepEllipsoid(l, r, l.x * l.x + l.z * l.z - 1.0f, 0.25f, &aside));
    CHECK(Near(m.push.x, aside.x * 1.4f, 1e-5f) && Near(m.push.z, aside.z * 2.4f, 1e-5f));
    // Not taking part.
    m.push = {};
    m.solved = false;
    crowd_.PushFromCar(m, 0, 0.25f);
    CHECK(m.push.x == 0.0f);
    m.solved = true;

    // The target: out of it, and a little more.
    m.route = nullptr;
    m.targetPoint = nullptr;
    m.target = {1, 0, 0, 0};
    crowd_.PushTargetFromCar(m, 0);
    CHECK(Near(m.target.x, 1.44f) && Near(m.target.z, 0.0f));
    m.target = {0, 0, 1.2f, 0};
    crowd_.PushTargetFromCar(m, 0);
    CHECK(Near(m.target.z, 1.2f + 1.32f));
    m.target = {1.5f, 0, 0, 0};                          // (outside)
    crowd_.PushTargetFromCar(m, 0);
    CHECK(m.target.x == 1.5f);
    // Dead in the middle: by where it stands.
    m.target = {0, 0, 0, 0};
    m.pos = {3, 0, 0, 0};
    crowd_.PushTargetFromCar(m, 0);
    CHECK(Near(m.target.x, 1.54f) && Near(m.target.z, 0.0f));
    m.target = {0, 0, 0, 0};
    m.pos = {0, 0, 3, 0};
    crowd_.PushTargetFromCar(m, 0);
    CHECK(Near(m.target.z, 2.64f) && Near(m.target.x, 0.0f));
    // Not taking part, or a route with nothing on it.
    m.target = {1, 0, 0, 0};
    m.solved = false;
    crowd_.PushTargetFromCar(m, 0);
    CHECK(m.target.x == 1);
    m.solved = true;
    std::vector<S_AIWaypoint *> empty;
    m.route = &empty;
    crowd_.PushTargetFromCar(m, 0);
    CHECK(m.target.x == 1);
    m.route = nullptr;
    SetCars({});
}

void TestWhichBlockerFirst()
{
    ResetAll();
    SetCars({});
    const int base = AddPoint(5, 0, 15.0f);
    const S_Checkpoint *pt = &g_net.points[base];
    S_Fake *self = MakeHuman(0, 0), *a = MakeHuman(0, 0), *b = MakeHuman(0, 0);
    C_HumanAIMemory &m = *self->memory;
    S_Block block;
    const auto order = [&](S_Fake *first, S_Fake *second) {
        self->nearby.clear();
        self->nearby.push_back(second->human());        // (looked at from the last)
        self->nearby.push_back(first->human());
        SyncNearbyCounts();
    };
    // a: met at .42, left at .58. b: met at .5. Whichever is looked at first.
    a->memory->pos = {5, 0, 0, 0};
    b->memory->pos = {5.8f, 0, 0, 0};
    order(a, b);
    FindIt(block, m, 0, 0, 10, 0, pt);
    CHECK(block.human == a->human() && block.left[0].x == 5.0f);
    CHECK(block.left[0].y == 0.0f);
    order(b, a);
    FindIt(block, m, 0, 0, 10, 0, pt);
    CHECK(block.human == a->human() && block.left[0].x == 5.0f);
    // Standing in one: no other is looked at.
    a->memory->pos = {0.1f, 0, 0, 0};
    b->memory->pos = {1.5f, 0, 0, 0};
    order(a, b);
    FindIt(block, m, 0, 0, 10, 0, pt);
    CHECK(block.human == a->human());
    // The same lying down.
    b->stance = 2;
    Prone(*b->memory);
    a->memory->pos = {5, 0, 0, 0};                       // (met at .42)
    b->memory->pos = {6.0f, 0, 0, 0};                    // (met at .49)
    order(a, b);
    FindIt(block, m, 0, 0, 10, 0, pt);
    CHECK(block.human == a->human());
    order(b, a);
    FindIt(block, m, 0, 0, 10, 0, pt);
    CHECK(block.human == a->human());
    a->stance = 2;
    Prone(*a->memory);
    b->stance = 0;
    a->memory->pos = {5, 0, 0, 0};                       // (met at .39, left at .61)
    b->memory->pos = {5.5f, 0, 0, 0};                    // (met at .47)
    order(a, b);
    FindIt(block, m, 0, 0, 10, 0, pt);
    CHECK(block.human == a->human() && Near(block.left[0].x, 3.9f, 0.01f));
    a->memory->pos = {0.5f, 0, 0, 0};                    // (standing in it)
    b->memory->pos = {2.5f, 0, 0, 0};                    // (met at .17)
    order(a, b);
    FindIt(block, m, 0, 0, 10, 0, pt);
    CHECK(block.human == a->human());
    a->stance = 0;
    self->nearby.clear();
    SyncNearbyCounts();

    // Cars. x: met at .36, left at .64. y: met at .46.
    S_FakeCar *x = MakeCar(5, 0), *y = MakeCar(6, 0);
    SetCars({y, x});
    FindIt(block, m, 0, 0, 10, 0, pt);
    CHECK(block.car == x->car() && Near(block.left[0].x, 5.0f - 1.05f * 1.4f, 0.02f));
    SetCars({x, y});
    FindIt(block, m, 0, 0, 10, 0, pt);
    CHECK(block.car == x->car());
    // In one, the rest are not looked at.
    S_FakeCar *inner = MakeCar(4.5f, 0.5f), *beyond = MakeCar(6.5f, 0.5f);
    SetCars({beyond, inner});
    FindIt(block, m, 4, 0.5f, 10, 0.5f, pt);
    CHECK(block.car == inner->car());
    // Between a car's two: the human goes, the car has it.
    a->memory->pos = {5, 0, 0, 0};
    self->nearby.push_back(a->human());
    SyncNearbyCounts();
    SetCars({x});
    FindIt(block, m, 0, 0, 10, 0, pt);
    CHECK(block.human == nullptr && block.car == x->car());
    // Standing in the car: over the human.
    a->memory->pos = {8.0f, 0, 0.5f, 0};
    SetCars({inner});
    FindIt(block, m, 4, 0.5f, 10, 0.5f, pt);
    CHECK(block.human == nullptr && block.car == inner->car());
    self->nearby.clear();
    SyncNearbyCounts();
    SetCars({});

    // A far point: followed by its links, or nothing.
    ResetAll();
    const int c0 = AddPoint(0, 0, 1.0f), c1 = AddPoint(10, 0, 1.0f), c2 = AddPoint(20, 0, 1.0f);
    Link(c0, c1);
    Link(c1, c2);
    S_Fake *lone = MakeHuman(0, 0);
    SyncNearbyCounts();
    FindIt(block, *lone->memory, 0, 0, 20, 0, &g_net.points[c0]);
    CHECK(block.kind == 1 && block.to.point == &g_net.points[c2]);
}

void TestPlanStale()
{
    C_HumanCrowd &crowd_ = g_humanCrowd;
    ResetAll();
    SetCars({});
    const int from = AddPoint(0, 0, 15.0f), to = AddPoint(10, 0, 15.0f);
    Link(from, to);
    S_Fake *self = MakeHuman(0, 0), *other = MakeHuman(5, 0);
    Meet(self, other);
    SyncNearbyCounts();
    C_HumanAIMemory &m = *self->memory;
    other->memory->pos = {5, 0, 0.1f, 0};
    const auto start = [&]() {
        for (S_PlanNode &node : crowd_.m_nodes) {
            std::memset(&node, 0, sizeof(node));
            node.point = &g_net.points[to];
            node.childCount = 1;
        }
        crowd_.m_nodes[0].avoidedCount = 1;
        crowd_.m_nodes[0].avoided[0] = other->human();
        crowd_.m_nodes[0].avoidedCar = reinterpret_cast<C_Car *>(1);
        crowd_.m_nodes[1].avoidedCount = 1;
        crowd_.m_nodes[1].avoided[0] = nullptr;
        crowd_.m_nodes[1].avoidedCar = reinterpret_cast<C_Car *>(1);
        crowd_.m_bestCost = -1.0f;
        crowd_.m_bestDistance = -1.0f;
        crowd_.m_resultCount = 3;
        self->route.clear();
        GiveRoute(self, {to});
        m.route = &self->route;
        m.waypoint = 0;
        m.pos = {0, 0, 0, 0};
        m.nearPoint = &g_net.points[from];
        m.plannedFrom = -1;
    };
    // What the last plan left is not looked at.
    start();
    CHECK(crowd_.Replan(m));
    CHECK(self->route.size() == 2 && crowd_.m_nodeCount == 4 && crowd_.m_resultCount == 2);
    CHECK(crowd_.m_nodes[1].avoidedCount >= 1);
    CHECK(Near(crowd_.m_nodes[2].pos.x, 5.0f) && crowd_.m_nodes[1].pos.x == 10.0f);
    CHECK(Near(crowd_.m_bestCost, 25.0f + 0.488f + 25.0f + 0.488f, 0.05f));
    // Nowhere to go round: no plan, the route as it was.
    ResetAll();
    const int a2 = AddPoint(0, 0, 0.5f), b2 = AddPoint(10, 0, 0.5f);
    Link(a2, b2);
    S_Fake *s2 = MakeHuman(0, 0), *o2 = MakeHuman(5, 0);
    Meet(s2, o2);
    SyncNearbyCounts();
    o2->memory->pos = {5, 0, 0.1f, 0};
    GiveRoute(s2, {b2});
    C_HumanAIMemory &n = *s2->memory;
    n.route = &s2->route;
    n.waypoint = 0;
    n.pos = {0, 0, 0, 0};
    n.nearPoint = &g_net.points[a2];
    n.plannedFrom = -1;
    CHECK(!crowd_.Replan(n));
    CHECK(s2->route.size() == 1 && n.plannedFrom == -1 && s2->route[0]->checkpoint == b2);
    // Somebody standing on the goal: the way ends beside it, and the goal stays after it.
    ResetAll();
    const int a3 = AddPoint(0, 0, 15.0f), b3 = AddPoint(10, 0, 15.0f);
    Link(a3, b3);
    S_Fake *s3 = MakeHuman(0, 0), *o3 = MakeHuman(10.2f, 0);
    Meet(s3, o3);
    SyncNearbyCounts();
    o3->memory->pos = {10.2f, 0, 0, 0};
    GiveRoute(s3, {b3});
    C_HumanAIMemory &k = *s3->memory;
    k.route = &s3->route;
    k.waypoint = 0;
    k.pos = {0, 0, 0, 0};
    k.nearPoint = &g_net.points[a3];
    k.plannedFrom = -1;
    CHECK(crowd_.Replan(k));
    CHECK(s3->route.size() >= 2 && k.plannedFrom == 1 && s3->route.back()->checkpoint == b3);
    // A child further than the one before it: the end of the way is still the end.
    ResetAll();
    AddPoint(0, 0, 15.0f);
    S_Fake *s4 = MakeHuman(0, 0);
    GiveRoute(s4, {0});
    C_HumanAIMemory &w = *s4->memory;
    w.route = &s4->route;
    w.waypoint = 0;
    s4->route[0]->pos = {10, 0, 0, 0};
    S_PlanNode *path[CROWD_MAX_PATH];
    std::memset(crowd_.m_nodes, 0, sizeof(crowd_.m_nodes));
    crowd_.m_bestCost = crowd_.m_bestDistance = 1e8f;
    crowd_.m_resultCount = 0;
    S_PlanNode &root = crowd_.m_nodes[0], &a = crowd_.m_nodes[1], &c = crowd_.m_nodes[2];
    a.point = c.point = &g_net.points[0];
    a.pos = {9, 0, 0, 0};
    c.pos = {20, 0, 0, 0};
    a.children[0] = reinterpret_cast<uintptr_t>(&c);
    a.childCount = 1;
    root.children[0] = reinterpret_cast<uintptr_t>(&a);
    root.childCount = 1;
    crowd_.CollectBest(w, &root, 1.0f, false, path, 0);
    CHECK(crowd_.m_resultCount == 2 && crowd_.m_results[1].pos.x == 20.0f);
    CHECK(Near(crowd_.m_bestDistance, 100.0f) && Near(crowd_.m_bestCost, 1.0f + 81.0f + 121.0f));
    // Nearly as near but not nearer, and cheaper.
    std::memset(crowd_.m_nodes, 0, sizeof(crowd_.m_nodes));
    crowd_.m_bestCost = crowd_.m_bestDistance = 1e8f;
    crowd_.m_resultCount = 0;
    a.point = c.point = &g_net.points[0];
    a.pos = {10, 0, 3, 0};
    c.pos = {10, 0, -3.00001f, 0};
    root.children[0] = reinterpret_cast<uintptr_t>(&a);
    root.children[1] = reinterpret_cast<uintptr_t>(&c);
    root.childCount = 2;
    crowd_.CollectBest(w, &root, 0.0f, false, path, 0);
    CHECK(crowd_.m_results[0].pos.z == 3.0f);
    // A node's position stale, its point gone.
    std::memset(crowd_.m_nodes, 0, sizeof(crowd_.m_nodes));
    S_vector4 corners[1] = {{1, 0, 1, 0}};
    crowd_.m_nodeCount = 2;
    crowd_.m_nodes[2].point = &g_net.points[0];
    root.children[0] = reinterpret_cast<uintptr_t>(&a);
    root.childCount = 1;
    crowd_.AddSideWay(&root, &a, 1, corners, 1, s4->human(), nullptr);
    CHECK(crowd_.m_nodes[2].point == nullptr);
    // Known by the last child too.
    crowd_.m_nodeCount = 2;
    std::memset(crowd_.m_nodes, 0, sizeof(crowd_.m_nodes));
    crowd_.m_nodes[3].pos = corners[0];
    root.children[0] = reinterpret_cast<uintptr_t>(&a);
    root.children[1] = reinterpret_cast<uintptr_t>(&crowd_.m_nodes[3]);
    root.childCount = 2;
    crowd_.AddSideWay(&root, &a, 1, corners, 1, s4->human(), nullptr);
    CHECK(crowd_.m_nodeCount == 2);
    // Which of its ends a strip of different widths gives; a strip from a point that was not best.
    ResetAll();
    const int t0 = AddPoint(0, 0, 1.0f), t1 = AddPoint(10, 0, 3.0f);
    Link(t0, t1);
    const S_Checkpoint *point = &g_net.points[t0];
    CHECK(crowd_.FindPointNear(Checkpoints(), &point, S_vector(5, 0, 2.28f), 1, &g_net.points[t1]) == 2);
    point = &g_net.points[t0];
    CHECK(crowd_.FindPointNear(Checkpoints(), &point, S_vector(5, 0, 2.2f), 1, &g_net.points[t1]) == 1);
    ResetAll();
    const int s = AddPoint(0, 0, 1.0f), y = AddPoint(10, 0, 1.0f), z = AddPoint(20, 0, 1.0f), other2 = AddPoint(12, 3, 1.0f);
    Link(s, y);
    Link(y, z);
    Link(s, other2);
    point = &g_net.points[s];
    CHECK(crowd_.FindPointNear(Checkpoints(), &point, S_vector(12, 0, 0.9f), 2, nullptr) == 2);
    CHECK(point == &g_net.points[y]);
}

void TestSolverByHand()
{
    // The tick's loop, as a list of the steps: the same place in the end.
    const auto scene = [](S_Fake **a, S_Fake **b) {
        ResetAll();
        SetCars({});
        AddPoint(0, 0, 15.0f);
        AddPoint(10, 0, 15.0f);
        AddPoint(-10, 0, 15.0f);
        Link(0, 1);
        Link(0, 2);
        *a = MakeHuman(0, 0);
        *b = MakeHuman(0.5f, 0.3f);
        GiveRoute(*a, {1});
        GiveRoute(*b, {2});
        Meet(*a, *b);
        SyncNearbyCounts();
    };
    S_Fake *a, *b;
    scene(&a, &b);
    Tick();
    const S_vector4 posA = a->memory->pos, posB = b->memory->pos, targetA = a->memory->target;
    float stepZ = a->memory->step[2];
    S_Fake *c, *d;
    scene(&c, &d);
    C_HumanAIMemory &m = *c->memory, &n = *d->memory;
    C_HumanCrowd &crowd_ = g_humanCrowd;
    crowd_.Prepare(n, 0.1f);
    crowd_.Prepare(m, 0.1f);
    const float step = 0.1f * 0.125f;
    for (int i = 0; i < 8; ++i) {
        crowd::MoveOneStep(n, step);
        n.push.x = n.push.y = n.push.z = 0.0f;
        crowd::MoveOneStep(m, step);
        m.push.x = m.push.y = m.push.z = 0.0f;
        crowd::PushFromHuman(n, m);
        crowd::PushTargetFromHuman(n, m);
        crowd::PushOntoNetwork(n);
        crowd::PushFromHuman(m, n);
        crowd::PushTargetFromHuman(m, n);
        crowd::PushOntoNetwork(m);
        for (C_HumanAIMemory *one : {&n, &m}) {
            crowd::TurnPushAside(*one, step);
            one->pos.x += one->push.x;
            one->pos.y += one->push.y;
            one->pos.z += one->push.z;
            crowd::HoldOnNetwork(*one);
        }
    }
    CHECK(Near(m.pos.x, posA.x, 1e-5f) && Near(m.pos.z, posA.z, 1e-5f));
    CHECK(Near(n.pos.x, posB.x, 1e-5f) && Near(n.pos.z, posB.z, 1e-5f));
    CHECK(Near(m.target.x, targetA.x, 1e-5f) && Near(m.target.z, targetA.z, 1e-5f));
    CHECK(Near((m.pos.z - m.start.z) * 10.0f, stepZ, 1e-4f));
    CHECK(posA.x != 0.0f && stepZ != 0.0f);

    // Two saved one after the other.
    C_chunk out;
    out.Open("x", true);
    m.routeStart = {1, 2, 3, 4};
    m.stuckTimer = 11;
    n.stuckTimer = 22;
    m.SaveGame(out);
    n.SaveGame(out);
    out.Close();
    C_chunk in;
    in.Open("x", false);
    C_HumanAIMemory first(c->human()), second(d->human());
    first.LoadGame(in);
    second.LoadGame(in);
    in.Close();
    CHECK(first.stuckTimer == 11 && second.stuckTimer == 22 && first.routeStart.z == 3);
}

// The tick against its steps called one by one, in scenes where each of them has something to do; `changed` says the step made a difference.
void SolverScene(int id, S_Fake **a, S_Fake **b, S_FakeCar **car)
{
    ResetAll();
    SetCars({});
    *car = nullptr;
    switch (id) {
    case 0:                                             // (head on)
        AddPoint(0, 0, 15.0f);
        AddPoint(10, 0, 15.0f);
        AddPoint(-10, 0, 15.0f);
        Link(0, 1);
        Link(0, 2);
        *a = MakeHuman(0, 0);
        *b = MakeHuman(1.0f, 0);
        GiveRoute(*a, {1});
        GiveRoute(*b, {2});
        break;
    case 1:                                             // (to the same place)
        AddPoint(0, 0, 15.0f);
        AddPoint(10, 0, 15.0f);
        Link(0, 1);
        *a = MakeHuman(0, 0);
        *b = MakeHuman(0.5f, 0.3f);
        GiveRoute(*a, {1});
        GiveRoute(*b, {1});
        break;
    case 2:                                             // (a narrow way)
        AddPoint(0, 0, 0.5f);
        AddPoint(10, 0, 0.5f);
        Link(0, 1);
        *a = MakeHuman(2.0f, 0.4f);
        *b = MakeHuman(2.5f, 0.1f);
        GiveRoute(*a, {1});
        GiveRoute(*b, {1});
        break;
    default:                                            // (to a place in a car)
        AddPoint(-3, 0, 15.0f);
        AddPoint(0.5f, 0, 15.0f);
        Link(0, 1);
        *a = MakeHuman(-3.0f, 0.1f);
        *b = MakeHuman(20.0f, 20.0f);
        GiveRoute(*a, {1});
        *car = MakeCar(0, 0);
        break;
    }
    if (id != 0)
        (*a)->memory->routeChanged = (*b)->memory->routeChanged = false;   // (no plan: the targets are met as they are)
    Meet(*a, *b);
    SyncNearbyCounts();
}

void TestSolverSteps()
{
    for (int id = 0; id < 4; ++id) {
        S_Fake *a, *b;
        S_FakeCar *car;
        SolverScene(id, &a, &b, &car);
        Tick();
        const S_vector4 posA = a->memory->pos, posB = b->memory->pos, targetA = a->memory->target, targetB = b->memory->target;
        const float stepA = a->memory->step[0];
        SolverScene(id, &a, &b, &car);
        C_HumanAIMemory &m = *a->memory, &n = *b->memory;
        C_HumanCrowd &crowd_ = g_humanCrowd;
        if (car)
            SetCars({car});
        crowd_.Prepare(n, 0.1f);
        crowd_.Prepare(m, 0.1f);
        const float step = 0.1f * 0.125f;
        for (int i = 0; i < 8; ++i) {
            for (C_HumanAIMemory *one : {&n, &m}) {
                crowd::MoveOneStep(*one, step);
                one->push.x = one->push.y = one->push.z = 0.0f;
            }
            for (C_HumanAIMemory *one : {&n, &m}) {
                C_HumanAIMemory &other = one == &n ? m : n;
                crowd::PushFromHuman(*one, other);
                crowd::PushTargetFromHuman(*one, other);
                if (car) {
                    const S_vector here = one->owner->GetFrame()->GetWorldPos();
                    const float dx = here.x, dz = here.z;
                    if (dx * dx + dz * dz <= 49.0f) {
                        crowd_.PushFromCar(*one, 0, step);
                        crowd_.PushTargetFromCar(*one, 0);
                    }
                }
                crowd::PushOntoNetwork(*one);
            }
            for (C_HumanAIMemory *one : {&n, &m}) {
                crowd::TurnPushAside(*one, step);
                one->pos.x += one->push.x;
                one->pos.y += one->push.y;
                one->pos.z += one->push.z;
                crowd::HoldOnNetwork(*one);
            }
        }
        CHECK(Near(m.pos.x, posA.x, 1e-5f) && Near(m.pos.z, posA.z, 1e-5f));
        CHECK(Near(n.pos.x, posB.x, 1e-5f) && Near(n.pos.z, posB.z, 1e-5f));
        CHECK(Near(m.target.x, targetA.x, 1e-5f) && Near(m.target.z, targetA.z, 1e-5f));
        CHECK(Near(n.target.x, targetB.x, 1e-5f) && Near(n.target.z, targetB.z, 1e-5f));
        CHECK(Near((m.pos.x - m.start.x) * 10.0f, stepA, 1e-3f));
        SetCars({});
    }
    // In the crowd but not moving: its last step goes.
    ResetAll();
    AddPoint(0, 0, 15.0f);
    S_Fake *still = MakeHuman(0, 0), *away = MakeHuman(9, 9);
    still->ctrl104 = false;
    away->ctrl104 = false;
    Meet(still, away);
    still->memory->stepping = true;
    still->memory->step[0] = still->memory->step[1] = still->memory->step[2] = 9.0f;
    Tick();
    CHECK(!still->memory->stepping && still->memory->step[0] == 0 && still->memory->step[1] == 0 && still->memory->step[2] == 0);
    // What the last plan left: a goal thought to be gone round already.
    ResetAll();
    SetCars({});
    const int from = AddPoint(0, 0, 15.0f), to = AddPoint(10, 0, 15.0f);
    Link(from, to);
    S_Fake *self = MakeHuman(0, 0), *other = MakeHuman(5, 0);
    Meet(self, other);
    SyncNearbyCounts();
    other->memory->pos = {5, 0, 0.1f, 0};
    GiveRoute(self, {to});
    C_HumanAIMemory &m = *self->memory;
    m.route = &self->route;
    m.waypoint = 0;
    m.pos = {0, 0, 0, 0};
    m.nearPoint = &g_net.points[from];
    g_humanCrowd.m_nodes[1].avoidedCount = 1;
    g_humanCrowd.m_nodes[1].avoided[0] = other->human();
    CHECK(g_humanCrowd.Replan(m));
    CHECK(self->route.size() >= 2);
}

void TestSaveLoad()
{
    ResetAll();
    S_Fake *fake = MakeHuman(0, 0);
    C_HumanAIMemory &memory = *fake->memory;
    memory.stuckTimer = 1234;
    memory.bestDistance = 5.5f;
    memory.plannedFrom = 3;
    memory.routeChanged = true;
    memory.notPlayer = false;
    memory.walking = true;
    memory.solved = true;
    memory.givesWay = false;
    memory.unknown109 = true;
    memory.lastGoal = {1, 2, 3, 4};
    memory.routeStart = {5, 6, 7, 8};
    C_chunk out;
    out.Open("x", true);
    CHECK(memory.SaveGame(out));
    out.Close();
    CHECK(static_cast<unsigned char>(g_file[0]) == 100 && g_file[1] == 0);
    // 2 + 4 + 4 + 5 x 1 + 1 + 16 + 16 bytes in eleven chunks.
    CHECK(g_file.size() == 6 + 11 * 6 + 2 + 4 + 4 + 5 + 1 + 16 + 16 - 0);
    S_Fake *other = MakeHuman(0, 0);
    C_HumanAIMemory &back = *other->memory;
    back.solved = false;
    C_chunk in;
    in.Open("x", false);
    CHECK(back.LoadGame(in));
    in.Close();
    CHECK(back.stuckTimer == 1234 && back.bestDistance == 5.5f && back.plannedFrom == 3);
    CHECK(back.routeChanged && !back.notPlayer && back.walking && back.solved && !back.givesWay && back.unknown109);
    CHECK(back.lastGoal.x == 1 && back.lastGoal.w == 4 && back.routeStart.x == 5 && back.routeStart.w == 8);
    // Another chunk: passed over. An empty file: false.
    C_chunk other2;
    other2.Open("x", true);
    other2.WriteChunk<int>(55, 7);
    other2.WriteChunk<int>(56, 7);
    other2.Close();
    C_chunk in2;
    in2.Open("x", false);
    back.plannedFrom = 9;
    CHECK(back.LoadGame(in2));
    CHECK(back.plannedFrom == 9 && g_at == 10);
    in2.Close();
    g_file.clear();
    C_chunk in3;
    in3.Open("x", false);
    CHECK(!back.LoadGame(in3));
    in3.Close();
    // Reset.
    back.Reset(other->human());
    CHECK(back.owner == other->human() && back.routeChanged && back.plannedFrom == -1 && back.walking && back.notPlayer);
    uint32_t bits;
    std::memcpy(&bits, &back.routeStart.x, 4);
    CHECK(bits == 0x7FC00000);
    std::memcpy(&bits, &back.lastGoal.x, 4);
    CHECK(bits == 0x7FC00000);
    CHECK(back.route == nullptr && back.nearPoint == nullptr && back.targetPoint == nullptr && back.waypoint == 0 && !back.stepping && back.flags == 0);
}
}  // namespace

int main()
{
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    for (void *&slot : g_controllerVt) slot = reinterpret_cast<void *>(F_Unknown108);
    g_controllerVt[0xD4 / 4 + 1] = reinterpret_cast<void *>(F_GetWaypoints);   // (g++: two destructor slots)
    GetGame()->m_checkpoints = Checkpoints();
    Checkpoints()->m_points = g_net.points;

    TestHelpers();
    TestKeepOnNetwork();
    TestFindPointNear();
    TestAlone();
    TestPushes();
    TestCars();
    TestStuckTimer();
    TestReplan();
    TestStepAloneDirect();
    TestPrepareDirect();
    TestPointsDirect();
    TestAddPushDirect();
    TestIsPointBlockedDirect();
    TestFindBlockerDirect();
    TestSideWayDirect();
    TestExpandDirect();
    TestCollectDirect();
    TestReplanDirect();
    TestConeNumbers();
    TestPassingWaypoints();
    TestCarPushNumbers();
    TestWhichBlockerFirst();
    TestPlanStale();
    TestSolverByHand();
    TestSolverSteps();
    TestRoots();
    TestSteps();
    TestPushNumbers();
    TestOnNetwork();
    TestWholeTick();
    TestSaveLoad();
    std::printf("crowd: %d checks, %d failures\n", checks, failures);
    return failures != 0;
}
