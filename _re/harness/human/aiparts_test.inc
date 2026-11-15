// Part 32 of the human harness (HUMAN_AIPARTS builds, build_aiparts.bat; included by human_test.cpp after
// locomotion3_test.inc): the parts of the AI controllers (game/ai_parts.cpp) against the original's: where an AI looks
// (S_AIFacing), its fire and move orders, the car driver (C_AIDriver: its road path planner, 0x0055f320), the alert
// state, what it fires at (S_AIFire), the tasks, their saved games, and the pools of waypoints, tasks and remembered
// sounds (the original's at 0x008ab158, its static initialiser not run: its pools fill themselves when first asked).
//
// Each part is the port's struct and the original's copy of its bytes (the same layout for the facing, the orders, the
// alert state and the waypoints; the others moved field by field: S_AIFire's throwPos, the tasks' own fields at +0x20,
// the driver's fields and its path's MSVC6 vector of 16-byte aligned storage). Pointers to actors go over to the
// original's copy of each actor (its net id at +0x10, its type at +0x18); the frames and the road network's points are
// shared. What the parts call outside (the game, the road network's FindNearest, FindPath and Get, the car's
// SetUnknown4f2630, the actors module's GetActorById, the scene's FindFrame) is faked alike on both sides and logged;
// rand is the CRT's on both. Saved games go to files on both sides through each side's own C_chunk code, compared byte
// for byte; loads read the same file, and the objects and where the file was left are compared.
#include "common/chunk.h"
#include "game/ai_parts.h"
#include "game/car.h"
#include "game/checkpoints.h"
#include "game/game.h"

namespace {

#define AP __attribute__((thiscall))

bool g_apOn = false;
void ALog(const void *self, const std::string &what, std::initializer_list<double> nums = {})
{
    if (g_apOn)
        Log(self, what.c_str(), nums);
}

// ---- the actors: each side's layout (the port's C_Actor; the original's net id +0x10, type +0x18)
enum { AP_ACTORS = 8 };
uint8_t *g_apActorPort[AP_ACTORS], *g_apActorOrig[AP_ACTORS];
const E_ActorType AP_TYPES[AP_ACTORS] = {ACTOR_DOOR, ACTOR_LADDER, ACTOR_HUMAN, ACTOR_HUMAN, ACTOR_CAR, ACTOR_CAR, ACTOR_DOOR, ACTOR_STATIC_WEAPON};
C_Actor *PortActor(int i) { return reinterpret_cast<C_Actor *>(g_apActorPort[i]); }
int ActorIndexById(int id)
{
    for (int i = 0; i < AP_ACTORS; ++i)
        if (PortActor(i)->m_netId == id)
            return i;
    return -1;
}
// A port pointer's original one (actors; anything else as it is).
uint32_t OrigPointer(uint32_t w)
{
    for (int i = 0; i < AP_ACTORS; ++i)
        if (w == static_cast<uint32_t>(reinterpret_cast<uintptr_t>(g_apActorPort[i])))
            return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(g_apActorOrig[i]));
    return w;
}
// The cars: their vehicle's top speed (the port's C_Car::m_vehicle.m_topSpeed; the original's +0x780), the vehicle
// part (+0x90 in the original) named.
float g_apTopSpeed[AP_ACTORS];

// ---- the game, its checkpoints, the road network (each side's offsets: the checkpoints +0x50 / +0x54, the scene
// +0x18, the road network +0x38 / +0x3C)
alignas(16) uint8_t g_apGamePort[sizeof(C_game)], g_apGameOrig[0x200];
alignas(16) uint8_t g_apCheckpointsPort[sizeof(C_Checkpoints)], g_apCheckpointsOrig[0x200];
void *PortRoads() { return reinterpret_cast<C_Checkpoints *>(g_apCheckpointsPort)->GetRoadNetwork(); }
void *OrigRoads() { return g_apCheckpointsOrig + 0x3C; }
enum { AP_POINTS = 24 };
alignas(16) uint8_t g_apPoints[AP_POINTS][sizeof(S_Checkpoint)];
S_Checkpoint *Point(int i) { return reinterpret_cast<S_Checkpoint *>(g_apPoints[i]); }
struct S_ApScript {
    std::vector<uint16_t> path;                         // FindPath's answer
    bool pathFound = true;
    float nearestMax = 1e9f;                            // FindNearest: the points within this (else none)
} g_aps;

// (Not logged: a getter, which the original's facing load calls twice and the port's once.)
void *__cdecl AO_GetGame() { return g_apGameOrig; }
// The nearest point to `pos` within maxDist (the same on both sides: the test's own search).
int NearestPoint(const S_vector &pos, float maxDist)
{
    int best = -1;
    float bestD = maxDist * maxDist;
    for (int i = 0; i < AP_POINTS; ++i) {
        const S_vector &p = Point(i)->pos;
        const float d = (p.x - pos.x) * (p.x - pos.x) + (p.y - pos.y) * (p.y - pos.y) + (p.z - pos.z) * (p.z - pos.z);
        if (d < bestD) {
            bestD = d;
            best = i;
        }
    }
    return best < 0 || bestD > g_aps.nearestMax ? CHECKPOINT_NONE : best;
}
int AP AO_FindNearest(const void *roads, S_vector pos, float maxDist, float minDist)
{
    const int i = NearestPoint(pos, maxDist);
    ALog(roads, "FindNearest", {pos.x, pos.y, pos.z, maxDist, minDist, double(i)});
    return i;
}
// (The original's Get takes the index's low word: its callers load only that.)
S_Checkpoint *AP AO_Get(void *roads, int rawIndex)
{
    const int index = int(uint16_t(rawIndex));
    ALog(roads, "Get", {double(index)});
    return index >= 0 && index < AP_POINTS ? Point(index) : nullptr;
}
// The original's FindPath fills an MSVC6 vector of uint16_t (4 bytes, first, last, end) with its own new.
bool AP AO_FindPath(void *roads, int from, int to, uint32_t *path, uint32_t excludeFlags)
{
    ALog(roads, "FindPath", {double(from), double(to), double(excludeFlags), double(g_aps.pathFound)});
    if (!g_aps.pathFound)
        return false;
    const size_t n = g_aps.path.size();
    auto *data = static_cast<uint16_t *>(std::malloc(n * 2 + 2));
    std::memcpy(data, g_aps.path.data(), n * 2);
    path[1] = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(data));
    path[2] = path[1] + 2 * static_cast<uint32_t>(n);
    path[3] = path[2];
    return true;
}
bool AP AO_SetUnknown4f2630(void *vehicle, float value)
{
    ALog(vehicle, "SetUnknown4f2630", {value});
    return true;
}
void *AP AO_GetActorById(const void *, int id)
{
    const int i = ActorIndexById(id);
    ALog(nullptr, "GetActorById", {double(id)});
    return i < 0 ? nullptr : g_apActorOrig[i];
}
// The scene's FindFrame (COM: this on the stack), the frames to find: a name at +0xE8 on both sides.
enum { AP_FRAMES = 4 };
alignas(16) uint8_t g_apFrames[AP_FRAMES][0x200];
const char *const AP_FRAME_NAMES[AP_FRAMES] = {"head", "Gun_target", "a_frame_with_a_rather_long_name_01", "x"};
void *g_apSceneVt[64];
struct S_ApScene {
    void **vt;
} g_apScene;
void *__stdcall AP_FindFrame(void *scene, const char *name, uint32_t mask)
{
    int found = -1;
    for (int i = 0; i < AP_FRAMES; ++i)
        if (std::strcmp(name, AP_FRAME_NAMES[i]) == 0)
            found = i;
    ALog(scene, std::string("FindFrame ") + name, {double(mask), double(found)});
    return found < 0 ? nullptr : g_apFrames[found];
}
void *__stdcall AP_SceneUnexpected(void *scene)
{
    std::printf("unexpected scene call\n");
    std::abort();
}
void *__cdecl AO_AlignedMalloc(size_t size, size_t align) { return _aligned_malloc(size, align); }
void __cdecl AO_AlignedFree(void *p) { _aligned_free(p); }
void *__cdecl AO_New(size_t size) { return std::malloc(size); }
void __cdecl AO_Delete(void *p) { std::free(p); }
int __cdecl AO_Rand() { return std::rand(); }
}  // namespace

// ---- the port's side through ld --wrap
extern "C" {
C_game *__wrap__Z7GetGamev() { return reinterpret_cast<C_game *>(g_apGamePort); }
int AP __wrap__ZNK19C_CheckpointNetwork11FindNearestE8S_vectorff(const C_CheckpointNetwork *roads, S_vector pos, float maxDist, float minDist)
{
    return AO_FindNearest(roads, pos, maxDist, minDist);
}
S_Checkpoint *AP __wrap__ZN19C_CheckpointNetwork3GetEi(C_CheckpointNetwork *roads, int index) { return AO_Get(roads, index); }
bool AP __wrap__ZN19C_CheckpointNetwork8FindPathEiiPSt6vectorItSaItEEj(C_CheckpointNetwork *roads, int from, int to, std::vector<uint16_t> *path,
                                                                    uint32_t excludeFlags)
{
    ALog(roads, "FindPath", {double(from), double(to), double(excludeFlags), double(g_aps.pathFound)});
    if (!g_aps.pathFound)
        return false;
    *path = g_aps.path;
    return true;
}
bool AP __wrap__ZN9C_Vehicle16SetUnknown4f2630Ef(C_Vehicle *vehicle, float value) { return AO_SetUnknown4f2630(vehicle, value); }
C_Actor *AP __wrap__ZNK14C_ActorsModule12GetActorByIdEi(const void *, int id)
{
    const int i = ActorIndexById(id);
    ALog(nullptr, "GetActorById", {double(id)});
    return i < 0 ? nullptr : PortActor(i);
}
}  // extern "C"

namespace {
static_assert(offsetof(S_AIFire, throwPos) == 0x1C, "S_AIFire: throwPos at +0x1C in the port (+0x20 in the original)");
static_assert(offsetof(S_AITask1, pos) == 0x14 && offsetof(S_AITask1, checkpoint) == 0x3C, "S_AITask1");
static_assert(offsetof(S_AITask2, unknown20) == 0x14 && offsetof(S_AITask2, human) == 0x38, "S_AITask2");
static_assert(offsetof(S_AITask3, pos) == 0x14 && offsetof(S_AITask3, actor) == 0x24, "S_AITask3");
static_assert(offsetof(S_AITask7, unknown20) == 0x14 && offsetof(S_AITask12, unknown30) == 0x24, "S_AITask7, 12");
static_assert(offsetof(S_AITask8, unknown1C) == 0x1C, "S_AITask8");
static_assert(sizeof(S_AIFacing) == 0x60 && sizeof(S_AIFireOrder) == 0x50 && sizeof(S_AIMoveOrder) == 0x60, "parts");

// The driver's fields in both layouts (generated by _re/tools/layout_offsets.py from ai_parts.h; m_path apart, m_route
// left empty).
#define LAYOUT_C_AIDRIVER \
    S_LayoutField{0x10, 12, 2, "m_pathIndex", 'i'}, \
    S_LayoutField{0x14, 16, 4, "m_progress", 'f'}, \
    S_LayoutField{0x18, 20, 4, "m_nearestDist2", 'f'}, \
    S_LayoutField{0x1C, 24, 1, "m_reversing", 'i'}, \
    S_LayoutField{0x1D, 25, 1, "m_moving", 'i'}, \
    S_LayoutField{0x20, 28, 4, "m_stuckTime", 'i'}, \
    S_LayoutField{0x24, 32, 4, "m_noProgressTime", 'i'}, \
    S_LayoutField{0x30, 36, 16, "m_lastPos", 'f'}, \
    S_LayoutField{0x40, 52, 4, "m_flags", 'i'}, \
    S_LayoutField{0x50, 56, 16, "m_target", 'f'}, \
    S_LayoutField{0x60, 72, 16, "m_targetDir", 'f'}, \
    S_LayoutField{0x70, 88, 4, "m_roads", 'p'}, \
    S_LayoutField{0x74, 92, 4, "m_vehicle", 'p'}, \
    S_LayoutField{0x88, 108, 1, "m_unknown88", 'i'}, \
    S_LayoutField{0x8C, 112, 4, "m_speed", 'f'}, \
    S_LayoutField{0x90, 116, 4, "m_pace", 'i'}, \
    S_LayoutField{0x94, 120, 4, "m_label", 'i'}, \

const S_LayoutField kDriverFields[] = {LAYOUT_C_AIDRIVER};

// ---- the copies: the port's part into the original's layout (+ the actors' pointers), and the first word that differs
struct S_ApRun {
    size_t port, orig, size;
};
// Each part's runs of bytes (port offset, original offset, size).
const std::vector<S_ApRun> kFacingRuns = {{0, 0, 0x60}};
const std::vector<S_ApRun> kFireOrderRuns = {{0, 0, 0x50}};
const std::vector<S_ApRun> kMoveRuns = {{0, 0, 0x60}};
const std::vector<S_ApRun> kAlertRuns = {{0, 0, 8}};
const std::vector<S_ApRun> kFireRuns = {{0, 0, 0x1C}, {0x1C, 0x20, 0x10}};
std::vector<S_ApRun> TaskRuns(int type)
{
    switch (type) {
    case 1: return {{4, 4, 0x10}, {0x14, 0x20, 0x2C}};
    case 2: return {{4, 4, 0x10}, {0x14, 0x20, 0x28}};
    case 3: return {{4, 4, 0x10}, {0x14, 0x20, 0x14}};
    case 7: return {{4, 4, 0x10}, {0x14, 0x20, 0x20}};
    case 8: return {{4, 4, 0x1C}};
    case 12: return {{4, 4, 0x10}, {0x14, 0x20, 0x14}};
    default: return {{4, 4, 0x10}};
    }
}
void CopyRuns(const std::vector<S_ApRun> &runs, const void *port, uint8_t *orig)
{
    for (const S_ApRun &r : runs) {
        std::memcpy(orig + r.orig, static_cast<const uint8_t *>(port) + r.port, r.size);
        for (size_t i = 0; i + 4 <= r.size; i += 4) {
            uint32_t w;
            std::memcpy(&w, orig + r.orig + i, 4);
            w = OrigPointer(w);
            std::memcpy(orig + r.orig + i, &w, 4);
        }
    }
}
// (Words left out: the original's offsets.)
std::string RunsDiffer(const std::vector<S_ApRun> &runs, const void *port, const uint8_t *orig, std::initializer_list<size_t> skip = {})
{
    alignas(16) uint8_t asOrig[0x200] = {};
    CopyRuns(runs, port, asOrig);
    for (const S_ApRun &r : runs)
        for (size_t at = r.orig; at < r.orig + r.size; at += 4) {
            if (std::find(skip.begin(), skip.end(), at) != skip.end())
                continue;
            const size_t n = std::min<size_t>(4, r.orig + r.size - at);
            uint32_t a = 0, b = 0;
            std::memcpy(&a, asOrig + at, n);
            std::memcpy(&b, orig + at, n);
            if (a != b) {
                char text[96];
                std::snprintf(text, sizeof text, "+0x%02zx port %08x / original %08x", at, a, b);
                return text;
            }
        }
    return "";
}

// ---- the driver: its fields by the layout table, its path as an MSVC6 vector (4 bytes, first, last, end) of 16-byte
// aligned storage (_aligned_malloc'd as the original's own), m_route empty
void DriverToOrig(const C_AIDriver &d, uint8_t *o)
{
    std::memset(o, 0, 0xA0);
    const auto *p = reinterpret_cast<const uint8_t *>(&d);
    for (const S_LayoutField &f : kDriverFields)
        std::memcpy(o + f.orig, p + f.port, f.size);
    uint32_t roads = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(OrigRoads()));
    std::memcpy(o + 0x70, &roads, 4);
    uint32_t vehicle;
    std::memcpy(&vehicle, o + 0x74, 4);
    vehicle = OrigPointer(vehicle);
    std::memcpy(o + 0x74, &vehicle, 4);
    uint32_t v[4] = {0xCDCDCDCD, 0, 0, 0};
    if (!d.m_path.empty()) {
        const size_t bytes = d.m_path.size() * sizeof(S_AIPathPoint);
        void *data = _aligned_malloc(bytes, 16);
        std::memcpy(data, d.m_path.data(), bytes);
        v[1] = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(data));
        v[2] = v[3] = v[1] + static_cast<uint32_t>(bytes);
    }
    std::memcpy(o, v, 16);
}
void FreeOrigDriver(uint8_t *o)
{
    uint32_t first;
    std::memcpy(&first, o + 4, 4);
    if (first)
        _aligned_free(reinterpret_cast<void *>(static_cast<uintptr_t>(first)));
    std::memset(o, 0, 16);
}
std::string DriversDiffer(const C_AIDriver &d, const uint8_t *o, bool skipLonePointDir = false)
{
    alignas(16) uint8_t asOrig[0xA0];
    const auto *p = reinterpret_cast<const uint8_t *>(&d);
    std::memset(asOrig, 0, sizeof asOrig);
    for (const S_LayoutField &f : kDriverFields)
        std::memcpy(asOrig + f.orig, p + f.port, f.size);
    for (const S_LayoutField &f : kDriverFields) {
        if (std::strcmp(f.name, "m_roads") == 0) {
            // (Each side's own road network, or the same other value.)
            uint32_t a, b;
            std::memcpy(&a, p + f.port, 4);
            std::memcpy(&b, o + f.orig, 4);
            const bool portRoads = a == static_cast<uint32_t>(reinterpret_cast<uintptr_t>(PortRoads()));
            const bool origRoads = b == static_cast<uint32_t>(reinterpret_cast<uintptr_t>(OrigRoads()));
            if (portRoads != origRoads || (!portRoads && a != b))
                return "m_roads";
            continue;
        }
        for (size_t i = 0; i < f.size; i += 4) {
            const size_t at = f.orig + i;
            // (The vectors' w: m_lastPos's and m_target's come from stack temps in places.)
            if ((f.kind == 'f' && f.size == 16 && i == 12))
                continue;
            const size_t n = std::min<size_t>(4, f.size - i);
            uint32_t a = 0, b = 0;
            std::memcpy(&a, asOrig + at, n);
            std::memcpy(&b, o + at, n);
            if (std::strcmp(f.name, "m_vehicle") == 0)
                a = OrigPointer(a);
            if (a != b) {
                char text[120];
                std::snprintf(text, sizeof text, "%s+%zu port %08x / original %08x", f.name, i, a, b);
                return text;
            }
        }
    }
    uint32_t v[4];
    std::memcpy(v, o, 16);
    const size_t count = v[1] ? (v[2] - v[1]) / sizeof(S_AIPathPoint) : 0;
    if (count != d.m_path.size())
        return "path size " + std::to_string(d.m_path.size()) + " / " + std::to_string(count);
    for (size_t k = 0; k < count; ++k) {
        // (A path of one point: the original's tangent reads past its end; the port's is zero.)
        if (count == 1 && skipLonePointDir) {
            const auto *op = reinterpret_cast<const uint8_t *>(static_cast<uintptr_t>(v[1]));
            if (std::memcmp(op, &d.m_path[0], 12) != 0 || op[0x20] != reinterpret_cast<const uint8_t *>(&d.m_path[0])[0x20])
                return "path[0]";
            continue;
        }
        const auto *op = reinterpret_cast<const uint8_t *>(static_cast<uintptr_t>(v[1])) + k * sizeof(S_AIPathPoint);
        const auto *pp = reinterpret_cast<const uint8_t *>(&d.m_path[k]);
        // (pos, dir: x, y, z; last.)
        for (size_t at : {0, 4, 8, 0x10, 0x14, 0x18})
            if (std::memcmp(op + at, pp + at, 4) != 0) {
                float a, b;
                std::memcpy(&a, pp + at, 4);
                std::memcpy(&b, op + at, 4);
                char text[120];
                std::snprintf(text, sizeof text, "path[%zu]+0x%02zx port %.9g / original %.9g", k, at, a, b);
                return text;
            }
        if (pp[0x20] != op[0x20])
            return "path[" + std::to_string(k) + "].last";
    }
    return "";
}

// ---- random values
float ApFloat()
{
    const int kind = I(0, 9);
    if (kind == 0)
        return 0.0f;
    if (kind == 1)
        return P(0.5f) ? 1.0f : -1.0f;
    if (kind == 2)
        return U(-1e-4f, 1e-4f);
    return U(-50, 50);
}
S_vector4 ApVector()
{
    S_vector4 v = {ApFloat(), ApFloat(), ApFloat(), U(-2, 2)};
    if (P(0.3f)) {
        // (A unit direction: the parts' usual input.)
        const float a = U(-3.2f, 3.2f), c = U(-1, 1), s = std::sqrt(1 - c * c);
        v = {std::cos(a) * s, c, std::sin(a) * s, 0.0f};
    }
    if (P(0.05f))
        v.x = v.y = v.z = 0.0f;
    return v;
}
C_Actor *ApActor() { return P(0.25f) ? nullptr : PortActor(I(0, AP_ACTORS - 1)); }
int ApInt() { return P(0.2f) ? -1 : P(0.5f) ? I(-5, 20) : I(-100000, 100000); }

void RandomFacing(S_AIFacing &f)
{
    f.facing = ApVector();
    f.view = ApVector();
    f.lookDir = ApVector();
    float x, z;
    x = f.facing.x, z = f.facing.z;
    const float l = std::sqrt(x * x + z * z);
    f.facingX = l > 1e-8f ? x / l : x;
    f.facingZ = l > 1e-8f ? z / l : z;
    x = f.view.x, z = f.view.z;
    const float l2 = std::sqrt(x * x + z * z);
    f.viewX = l2 > 1e-8f ? x / l2 : x;
    f.viewZ = l2 > 1e-8f ? z / l2 : z;
    f.locks = static_cast<uint16_t>(I(0, 3));
    f.cone = P(0.4f) ? S_AIFacing::CONE : P(0.5f) ? S_AIFacing::CONE_LYING : U(-1, 1);
    f.lookPeriod = P(0.3f) ? -1 : I(0, 9000);
    f.lookTimer = P(0.3f) ? -1 : P(0.3f) ? I(0, 2) : I(-500, 9000);
    f.lookAt = P(0.5f) ? nullptr : reinterpret_cast<I3D_frame *>(g_apFrames[I(0, AP_FRAMES - 1)]);
}
void RandomFireOrder(S_AIFireOrder &f)
{
    f.unknown00 = ApVector();
    f.fireDir = ApVector();
    f.throwPos = ApVector();
    f.target = ApActor();
    f.reload = P(0.5f);
    f.throwGrenade = P(0.5f);
    f.result = uint32_t(I(0, 15));
    f.burstTime = ApInt();
    f.burstSpread = ApInt();
    f.pauseTime = ApInt();
    f.pauseSpread = ApInt();
}
void RandomMove(S_AIMoveOrder &m)
{
    m.unknown00 = ApVector();
    m.pos = ApVector();
    m.checkpoint = uint16_t(P(0.3f) ? 0xFFFF : I(0, 500));
    m.unknown24 = ApInt();
    m.unknown28 = ApInt();
    m.unknown2C = P(0.3f) ? 3.402823466e+38f : U(0, 100);
    m.unknown30 = ApInt();
    m.unknown34 = ApInt();
    m.unknown38 = ApInt();
    m.unknown3C = ApInt();
    m.door = ApActor();
    m.ladder = ApActor();
    m.ladderFrom = uint16_t(I(0, 0xFFFF));
    m.ladderTo = uint16_t(I(0, 0xFFFF));
    m.unknown4C = I(-1, 6);
    m.flags = (uint32_t(std::rand()) << 17) ^ (uint32_t(std::rand()) << 2) ^ uint32_t(std::rand());
    m.unknown54 = uint32_t(I(0, 0x1F000));
    m.unknown58 = P(0.5f);
}
void RandomFire(S_AIFire &f)
{
    f.unknown00 = P(0.5f);
    f.burstTime = ApInt();
    f.pauseTime = ApInt();
    f.unknown0C = ApInt();
    f.target = ApActor();
    f.orderTarget = ApActor();
    f.flags = uint32_t(I(0, 3));
    f.throwPos = ApVector();
}
void RandomDriver(C_AIDriver &d)
{
    d.m_path.clear();
    for (int k = P(0.3f) ? 0 : I(1, 6); k > 0; --k) {
        S_AIPathPoint p;
        p.pos = ApVector();
        p.dir = ApVector();
        p.last = P(0.2f);
        d.m_path.push_back(p);
    }
    d.m_pathIndex = uint16_t(I(0, 6));
    d.m_progress = U(0, 1);
    d.m_nearestDist2 = P(0.3f) ? 1e8f : U(0, 400);
    d.m_reversing = P(0.5f);
    d.m_moving = P(0.5f);
    d.m_stuckTime = ApInt();
    d.m_noProgressTime = ApInt();
    d.m_lastPos = ApVector();
    d.m_flags = uint32_t(I(0, 7));
    d.m_target = ApVector();
    d.m_targetDir = ApVector();
    if (P(0.3f))
        d.m_targetDir.x = std::numeric_limits<float>::quiet_NaN();
    d.m_roads = static_cast<C_CheckpointNetwork *>(PortRoads());
    d.m_vehicle = P(0.3f) ? nullptr : PortActor(P(0.8f) ? I(4, 5) : I(0, AP_ACTORS - 1));
    d.m_unknown88 = P(0.5f);
    d.m_speed = P(0.2f) ? -1.0f : U(-20, 40);
    d.m_pace = uint32_t(I(0, 0x1F000)) & 0x1F000;
    d.m_label = ApInt();
}
// (A driver's vehicle must be a car where it is used as one: SetVehicle(null) and Stop cast it.)
void CarOnly(C_AIDriver &d)
{
    if (d.m_vehicle && d.m_vehicle->m_type != ACTOR_CAR)
        d.m_vehicle = PortActor(4);
}

std::map<std::string, long> g_apFailures;
void ApReport(const std::string &what, const std::string &why)
{
    if (why.empty())
        return;
    if (++g_apFailures[what] <= 3)
        std::printf("  %s: %s\n", what.c_str(), why.c_str());
}

alignas(16) uint8_t g_apOrig[0x400];
typedef int(AP *T_PartV)(void *, const S_vector4 *);

// ---- S_AIFacing
void TestFacing()
{
    std::printf("aiparts: S_AIFacing\n");
    S_AIFacing f;
    for (int i = 0; i < 40000 / QUICK; ++i) {
        RandomFacing(f);
        CopyRuns(kFacingRuns, &f, g_apOrig);
        const int which = I(0, 10);
        const S_vector4 dir = ApVector();
        // (TickLookTimer: now and then the time left exactly.)
        const int n = which == 4 && P(0.3f) ? f.lookTimer + I(-1, 1) : I(-200, 9000);
        const bool on = P(0.5f);
        g_apOn = true;
        DiffCall("Facing", [&] {
                     switch (which) {
                     case 0: return f.SetView(dir);
                     case 1: return f.SetFacing(dir);
                     case 2: return f.LookToward(dir);
                     case 3: f.SetLookAround(on, n); return 0;
                     case 4: return int(f.TickLookTimer(n));
                     case 5: return int(f.LockFacing());
                     case 6: return int(f.LockView());
                     case 7: return int(f.UnlockFacing());
                     case 8: return int(f.UnlockView());
                     default: { S_AIFacing g; std::memcpy(static_cast<void *>(&f), &g, sizeof g); reinterpret_cast<uint8_t *>(&f)[0x42] = reinterpret_cast<uint8_t *>(&f)[0x43] = 0; return 0; }
                     }
                 },
                 [&](void *) {
                     typedef int(AP * T_Int)(void *, int);
                     typedef uint32_t(AP * T_Bool)(void *, int);
                     typedef void(AP * T_Look)(void *, bool, int);
                     typedef uint32_t(AP * T_Lock)(void *);
                     switch (which) {
                     case 0: return OriginalAt<T_PartV>(0x0055d2e0)(g_apOrig, &dir);
                     case 1: return OriginalAt<T_PartV>(0x0055d630)(g_apOrig, &dir);
                     case 2: return OriginalAt<T_PartV>(0x0055d1f0)(g_apOrig, &dir);
                     case 3: OriginalAt<T_Look>(0x0055d280)(g_apOrig, on, n); return 0;
                     case 4: return int(OriginalAt<T_Bool>(0x0055d2b0)(g_apOrig, n) & 0xFF);
                     case 5: return int(OriginalAt<T_Lock>(0x0055d8c0)(g_apOrig) & 0xFF);
                     case 6: return int(OriginalAt<T_Lock>(0x0055d8e0)(g_apOrig) & 0xFF);
                     case 7: return int(OriginalAt<T_Lock>(0x0055d900)(g_apOrig) & 0xFF);
                     case 8: return int(OriginalAt<T_Lock>(0x0055d920)(g_apOrig) & 0xFF);
                     default: {
                         typedef void *(AP * T_New)(void *);
                         OriginalAt<T_New>(0x0055d860)(g_apOrig);
                         g_apOrig[0x42] = g_apOrig[0x43] = 0;   // (the padding after locks: left as it was)
                         return 0;
                     }
                     }
                 });
        g_apOn = false;
        // (The w of the directions: the original's SetView / SetFacing copy the whole vector, as the port's.)
        // (The constructor: the original leaves the vectors' w as they were, the port's are zero; not read.)
        const std::string why = which >= 9 ? RunsDiffer(kFacingRuns, &f, g_apOrig, {0x0C, 0x1C, 0x2C, 0x54, 0x58, 0x5C})
                                           : RunsDiffer(kFacingRuns, &f, g_apOrig, {0x54, 0x58, 0x5C});
        ApReport("Facing " + std::to_string(which), why);
        CHECK(why.empty());
    }
}

// ---- S_AIFireOrder
void TestFireOrder()
{
    std::printf("aiparts: S_AIFireOrder\n");
    S_AIFireOrder f;
    for (int i = 0; i < 20000 / QUICK; ++i) {
        RandomFireOrder(f);
        CopyRuns(kFireOrderRuns, &f, g_apOrig);
        const int which = I(0, 6);
        const S_vector4 v = ApVector();
        C_Actor *actor = ApActor();
        int pt = 0, ps = 0, ot = 0, os = 0;
        g_apOn = true;
        DiffCall("FireOrder", [&] {
                     switch (which) {
                     case 0: f.SetTarget(actor); return 0;
                     case 1: f.SetFireDir(v); return 0;
                     case 2: f.SetThrow(v); return 0;
                     case 3: f.ResetTimes(); return 0;
                     case 4: return int(f.GetBurstTime(&pt, &ps));
                     case 5: return int(f.GetPauseTime(&pt, &ps));
                     default: { S_AIFireOrder g; std::memcpy(static_cast<void *>(&f), &g, sizeof g); reinterpret_cast<uint8_t *>(&f)[0x36] = reinterpret_cast<uint8_t *>(&f)[0x37] = 0; return 0; }
                     }
                 },
                 [&](void *) {
                     typedef void(AP * T_Ptr)(void *, const void *);
                     typedef void(AP * T_Void)(void *);
                     typedef uint32_t(AP * T_Get)(void *, int *, int *);
                     switch (which) {
                     case 0: OriginalAt<T_Ptr>(0x005607b0)(g_apOrig, reinterpret_cast<const void *>(static_cast<uintptr_t>(OrigPointer(static_cast<uint32_t>(reinterpret_cast<uintptr_t>(actor)))))); return 0;
                     case 1: OriginalAt<T_Ptr>(0x00560780)(g_apOrig, &v); return 0;
                     case 2: OriginalAt<T_Ptr>(0x00560750)(g_apOrig, &v); return 0;
                     case 3: OriginalAt<T_Void>(0x00560810)(g_apOrig); return 0;
                     case 4: return int(OriginalAt<T_Get>(0x00560860)(g_apOrig, &ot, &os) & 0xFF);
                     case 5: return int(OriginalAt<T_Get>(0x00560880)(g_apOrig, &ot, &os) & 0xFF);
                     default: OriginalAt<T_Void>(0x005607d0)(g_apOrig); g_apOrig[0x36] = g_apOrig[0x37] = 0; return 0;   // (padding: as it was)
                     }
                 });
        g_apOn = false;
        // (The constructor: the original leaves the vectors' w and throwPos as they were.)
        std::string why = which == 6 ? RunsDiffer(kFireOrderRuns, &f, g_apOrig, {0x0C, 0x1C, 0x20, 0x24, 0x28, 0x2C, 0x4C})
                                     : RunsDiffer(kFireOrderRuns, &f, g_apOrig, {0x4C});
        if (why.empty() && (pt != ot || ps != os))
            why = "the times out";
        ApReport("FireOrder " + std::to_string(which), why);
        CHECK(why.empty());
    }
}

// ---- S_AIMoveOrder
void TestMove()
{
    std::printf("aiparts: S_AIMoveOrder\n");
    S_AIMoveOrder m;
    for (int i = 0; i < 20000 / QUICK; ++i) {
        RandomMove(m);
        CopyRuns(kMoveRuns, &m, g_apOrig);
        const int which = I(0, 6);
        S_vector4 v = ApVector();
        if (P(0.2f))
            v.y = (P(0.3f) ? 100000.0f : U(99990, 100010)) * (P(0.5f) ? 1.0f : -1.0f);
        const int16_t index = int16_t(P(0.2f) ? -1 : I(-3, 600));
        const int a = I(-5, 100), b = I(-5, 100);
        g_apOn = true;
        DiffCall("Move", [&] {
                     switch (which) {
                     case 0: m.SetPos(v); return 0;
                     case 1: m.SetCheckpoint(index); return 0;
                     case 2: m.SetCheckpointPos(index, v); return 0;
                     case 3: m.SetFlee(a, b); return 0;
                     case 4: m.Finish(); return 0;
                     case 5: m.BeginRoute(); return 0;
                     default: m.Wait(); return 0;
                     }
                 },
                 [&](void *) {
                     typedef void(AP * T_Ptr)(void *, const void *);
                     typedef void(AP * T_Index)(void *, int);
                     typedef void(AP * T_IndexPos)(void *, int, const void *);
                     typedef void(AP * T_Two)(void *, int, int);
                     typedef void(AP * T_Void)(void *);
                     switch (which) {
                     case 0: OriginalAt<T_Ptr>(0x00560ea0)(g_apOrig, &v); return 0;
                     case 1: OriginalAt<T_Index>(0x00560f00)(g_apOrig, index); return 0;
                     case 2: OriginalAt<T_IndexPos>(0x00560e50)(g_apOrig, index, &v); return 0;
                     case 3: OriginalAt<T_Two>(0x00560e20)(g_apOrig, a, b); return 0;
                     case 4: OriginalAt<T_Void>(0x00560f30)(g_apOrig); return 0;
                     case 5: OriginalAt<T_Void>(0x00560f50)(g_apOrig); return 0;
                     default: OriginalAt<T_Void>(0x00560f90)(g_apOrig); return 0;
                     }
                 });
        g_apOn = false;
        const std::string why = RunsDiffer(kMoveRuns, &m, g_apOrig, {0x5C});
        ApReport("Move " + std::to_string(which), why);
        CHECK(why.empty());
    }
}

// ---- S_AIAlertState
void TestAlert()
{
    std::printf("aiparts: S_AIAlertState\n");
    S_AIAlertState a;
    for (int i = 0; i < 4000 / QUICK; ++i) {
        a.state = I(0, 5);
        a.changed = P(0.5f);
        CopyRuns(kAlertRuns, &a, g_apOrig);
        const int value = I(0, 5);
        DiffCall("Alert", [&] { a.Set(value); return 0; },
                 [&](void *) {
                     typedef void(AP * T_Set)(void *, int);
                     OriginalAt<T_Set>(0x00563a20)(g_apOrig, value);
                     return 0;
                 });
        const std::string why = RunsDiffer({{0, 0, 5}}, &a, g_apOrig);
        ApReport("Alert", why);
        CHECK(why.empty());
    }
}

// ---- C_AIDriver
void RandomRoads()
{
    for (int i = 0; i < AP_POINTS; ++i) {
        S_Checkpoint *p = Point(i);
        p->pos = S_vector(U(-200, 200), U(-5, 5), U(-200, 200));
        p->radius = uint8_t(I(0, 255));
    }
    g_aps.path.clear();
    for (int k = P(0.15f) ? 0 : I(1, 8); k > 0; --k)
        g_aps.path.push_back(uint16_t(I(0, AP_POINTS - 1)));
    // (Now and then the same point twice, or points on a line: the cuts that come to nothing.)
    if (P(0.2f) && !g_aps.path.empty())
        g_aps.path.push_back(g_aps.path.back());
    if (P(0.2f) && g_aps.path.size() >= 2) {
        const S_vector &a = Point(g_aps.path[0])->pos, &b = Point(g_aps.path[1])->pos;
        Point(g_aps.path[1])->pos = S_vector(a.x * 2 - b.x, a.y, a.z * 2 - b.z);
    }
    g_aps.pathFound = P(0.85f);
    g_aps.nearestMax = P(0.1f) ? 0.0f : 1e9f;
}
void TestDriver()
{
    std::printf("aiparts: C_AIDriver\n");
    C_AIDriver &d = *new C_AIDriver;
    for (int i = 0; i < 30000 / QUICK; ++i) {
        RandomDriver(d);
        RandomRoads();
        const int which = I(0, 6);
        if (which == 0 || which == 3)
            CarOnly(d);
        S_vector4 a = ApVector(), b = ApVector(), c = ApVector();
        // (PlanPath: from and to near points of the network, beyond none (NaN) or a point.)
        if (which == 5) {
            a = {Point(I(0, AP_POINTS - 1))->pos.x + U(-5, 5), U(-3, 3), Point(I(0, AP_POINTS - 1))->pos.z + U(-5, 5), 0.0f};
            b = {Point(I(0, AP_POINTS - 1))->pos.x + U(-5, 5), U(-3, 3), Point(I(0, AP_POINTS - 1))->pos.z + U(-5, 5), 0.0f};
            if (P(0.3f) && !d.m_path.empty())
                a = d.m_path.back().pos;
            if (P(0.1f))
                b = a;
            if (P(0.4f))
                c.x = std::numeric_limits<float>::quiet_NaN();
        }
        C_Actor *vehicle = P(0.3f) ? nullptr : PortActor(I(0, AP_ACTORS - 1));
        if (which == 0 && vehicle && vehicle->m_type != ACTOR_CAR && P(0.5f))
            vehicle = PortActor(5);
        float speed = P(0.2f) ? 0.0f : P(0.1f) ? U(-1e-8f, 1e-8f) : U(-30, 30);
        DriverToOrig(d, g_apOrig);
        g_apOn = true;
        DiffCall("Driver", [&] {
                     switch (which) {
                     case 0: d.SetVehicle(vehicle); return 0;
                     case 1: d.DriveTo(a, speed); return 0;
                     case 2: d.DriveTo(a, b, speed); return 0;
                     case 3: d.Stop(); return 0;
                     case 4: d.ResetPath(); return 0;
                     default: d.PlanPath(a, b, c); return 0;
                     }
                 },
                 [&](void *) {
                     typedef void(AP * T_Ptr)(void *, const void *);
                     typedef void(AP * T_To)(void *, const void *, float);
                     typedef void(AP * T_To3)(void *, const void *, const void *, float);
                     typedef void(AP * T_Plan)(void *, const void *, const void *, const void *);
                     typedef void(AP * T_Void)(void *);
                     switch (which) {
                     case 0: OriginalAt<T_Ptr>(0x0055e0e0)(g_apOrig, reinterpret_cast<const void *>(static_cast<uintptr_t>(OrigPointer(static_cast<uint32_t>(reinterpret_cast<uintptr_t>(vehicle)))))); return 0;
                     case 1: OriginalAt<T_To>(0x0055e160)(g_apOrig, &a, speed); return 0;
                     case 2: OriginalAt<T_To3>(0x0055e1d0)(g_apOrig, &a, &b, speed); return 0;
                     case 3: OriginalAt<T_Void>(0x0055e250)(g_apOrig); return 0;
                     case 4: OriginalAt<T_Void>(0x0055f2c0)(g_apOrig); return 0;
                     default: OriginalAt<T_Plan>(0x0055f320)(g_apOrig, &a, &b, &c); return 0;
                     }
                 });
        g_apOn = false;
        const std::string why = DriversDiffer(d, g_apOrig, which >= 5);
        ApReport("Driver " + std::to_string(which), why);
        CHECK(why.empty());
        FreeOrigDriver(g_apOrig);
    }
}

// The driver's constructor: over a driver of other values on both sides (what it does not set stays).
void TestDriverConstructor()
{
    std::printf("aiparts: C_AIDriver's constructor\n");
    alignas(16) static uint8_t storage[sizeof(C_AIDriver)];
    for (int i = 0; i < 400 / QUICK; ++i) {
        C_AIDriver *d = new (storage) C_AIDriver;
        RandomDriver(*d);
        d->m_path.clear();
        DriverToOrig(*d, g_apOrig);
        g_apOn = true;
        DiffCall("DriverConstructor", [&] {
                     d->~C_AIDriver();
                     d = new (storage) C_AIDriver;
                     return 0;
                 },
                 [&](void *) {
                     typedef void *(AP * T_New)(void *);
                     OriginalAt<T_New>(0x0055df90)(g_apOrig);
                     return 0;
                 });
        g_apOn = false;
        // (The original's leaves m_target, m_targetDir and m_label as they were; the port's are zero: taken over.)
        std::memcpy(&d->m_target, g_apOrig + 0x50, 16);
        std::memcpy(&d->m_targetDir, g_apOrig + 0x60, 16);
        std::memcpy(&d->m_label, g_apOrig + 0x94, 4);
        const std::string why = DriversDiffer(*d, g_apOrig);
        ApReport("DriverConstructor", why);
        CHECK(why.empty());
        FreeOrigDriver(g_apOrig);
        d->~C_AIDriver();
    }
}

// ---- the saved games: each part saved on both sides (the files compared), then loaded from a saved file or one made
// by hand (chunks missing, short, unknown, out of order), the objects and where the file was left compared
// (Named by the process: the mutation runs run several at once.)
const std::string g_apTempDir = std::string(std::getenv("TEMP") ? std::getenv("TEMP") : ".") + "\\aiparts_" +
                                std::to_string(GetCurrentProcessId());
const std::string g_apPortFile = g_apTempDir + "_port.sav";
const std::string g_apOrigFile = g_apTempDir + "_orig.sav";
std::vector<uint8_t> ApFileBytes(const std::string &name)
{
    std::vector<uint8_t> bytes;
    if (FILE *f = std::fopen(name.c_str(), "rb")) {
        int c;
        while ((c = std::fgetc(f)) != EOF)
            bytes.push_back(uint8_t(c));
        std::fclose(f);
    }
    return bytes;
}
int ApNextChunk(C_chunk &c)
{
    uint16_t id = 0;
    return c.Descend(id) ? int(id) : -1;
}
typedef uint32_t(AP *T_ApChunk)(void *, C_chunk *);
// Writes `bytes` random bytes (or values of the field kinds) into the chunk.
void RandomBytes(C_chunk &c, int bytes)
{
    for (int k = 0; k < bytes; k += 4) {
        uint32_t w = P(0.5f) ? uint32_t(I(-3, 50)) : (uint32_t(std::rand()) << 17) ^ uint32_t(std::rand());
        if (P(0.3f)) {
            const float f = U(-50, 50);
            std::memcpy(&w, &f, 4);
        }
        c.Write(&w, std::min(4, bytes - k));
    }
}
// A hand-made part chunk: the part's top chunk (or another), children from `ids` with random sizes up to `sizes`.
void HandMade(C_chunk &c, uint16_t top, const std::vector<std::pair<uint16_t, int>> &children, bool wrap)
{
    if (wrap)
        c.Begin(P(0.9f) ? top : uint16_t(I(1, 0x2000)));
    for (int k = I(0, 5); k > 0; --k) {
        const auto &child = children[I(0, int(children.size()) - 1)];
        c.Begin(P(0.9f) ? child.first : uint16_t(I(1, 0x2000)));
        // (Never short: the original reads stack garbage for what a short chunk lacks.)
        RandomBytes(c, P(0.6f) ? child.second : child.second + I(1, 8));
        c.Ascend();
    }
    if (wrap)
        c.Ascend();
}

// One part's save and load on both sides. `save`/`load` run the port's; `origSave`/`origLoad` the original's.
// A chunk after the part's, in the same parent: where a save or load leaves the file shows.
void Sibling(C_chunk &c)
{
    c.Begin(0x0BD2);
    const int x = 0x5EB;
    c.Write(&x, 4);
    c.Ascend();
}
// `again` puts other values into both objects before the load (a load into what was saved would hide fields it does
// not set).
template <class PS, class OS, class PL, class OL, class MK, class CMP, class AG>
void SaveLoad(const char *name, PS save, OS origSave, MK makeFile, PL load, OL origLoad, CMP compare, AG again)
{
    std::remove(g_apPortFile.c_str());
    std::remove(g_apOrigFile.c_str());
    g_apOn = true;
    DiffCall((std::string(name) + " SaveGame").c_str(),
             [&] {
                 C_chunk c;
                 c.Open(g_apPortFile.c_str(), true);
                 c.Begin(0x7777);
                 const int r = int(save(c));
                 Sibling(c);
                 c.Ascend();
                 return r;
             },
             [&](void *) {
                 C_chunk c;
                 c.Open(g_apOrigFile.c_str(), true);
                 c.Begin(0x7777);
                 const int r = int(origSave(c) & 0xFF);
                 Sibling(c);
                 c.Ascend();
                 return r;
             });
    g_apOn = false;
    const bool same = ApFileBytes(g_apPortFile) == ApFileBytes(g_apOrigFile);
    if (!same) {
        const std::vector<uint8_t> pb = ApFileBytes(g_apPortFile), ob = ApFileBytes(g_apOrigFile);
        size_t at = 0;
        while (at < pb.size() && at < ob.size() && pb[at] == ob[at])
            ++at;
        char text[160];
        std::snprintf(text, sizeof text, "files differ at %zu (sizes %zu / %zu): port %02x original %02x", at, pb.size(), ob.size(),
                      at < pb.size() ? pb[at] : 0, at < ob.size() ? ob[at] : 0);
        ApReport(std::string(name) + " SaveGame", text);
    }
    CHECK(same);
    // (Loaded from the saved file, or from one made by hand.)
    if (P(0.5f)) {
        C_chunk c;
        c.Open(g_apPortFile.c_str(), true);
        c.Begin(0x7777);
        makeFile(c);
        Sibling(c);
        c.Ascend();
        c.Begin(0x0BD1);                                // (a chunk after: where a load leaves the file)
        c.Ascend();
    }
    again();
    g_apOn = true;
    DiffCall((std::string(name) + " LoadGame").c_str(),
             [&] {
                 C_chunk c;
                 c.Open(g_apPortFile.c_str(), false);
                 uint16_t id;
                 c.Descend(id);
                 const int r = int(load(c));
                 return r * 0x10000 + (ApNextChunk(c) & 0xFFFF);
             },
             [&](void *) {
                 C_chunk c;
                 c.Open(g_apPortFile.c_str(), false);
                 uint16_t id;
                 c.Descend(id);
                 const int r = int(origLoad(c) & 0xFF);
                 return r * 0x10000 + (ApNextChunk(c) & 0xFFFF);
             });
    g_apOn = false;
    const std::string why = compare();
    ApReport(std::string(name) + " LoadGame", why);
    CHECK(why.empty());
}

void TestSaves()
{
    std::printf("aiparts: the saved games\n");
    S_AIFacing facing;
    S_AIFireOrder fireOrder;
    S_AIMoveOrder move;
    S_AIAlertState alert;
    S_AIFire fire;
    C_AIDriver &driver = *new C_AIDriver;
    for (int i = 0; i < 1200 / QUICK; ++i) {
        const int which = I(0, 6);
        switch (which) {
        case 0: {
            RandomFacing(facing);
            CopyRuns(kFacingRuns, &facing, g_apOrig);
            SaveLoad("Facing", [&](C_chunk &c) { return facing.SaveGame(c); },
                     [&](C_chunk &c) { return OriginalAt<T_ApChunk>(0x0055d960)(g_apOrig, &c); },
                     [&](C_chunk &c) {
                         // (The look-at name terminated: the original copies it as it is.)
                         c.Begin(P(0.9f) ? 0x41 : uint16_t(I(1, 0x2000)));
                         for (int k = I(0, 4); k > 0; --k) {
                             if (P(0.5f)) {
                                 c.Begin(P(0.9f) ? 0xD02 : uint16_t(I(1, 0x2000)));
                                 RandomBytes(c, P(0.6f) ? 0x44 : 0x44 + I(1, 8));
                             } else {
                                 c.Begin(0xD03);
                                 const char *name = P(0.8f) ? AP_FRAME_NAMES[I(0, AP_FRAMES - 1)] : "no_such_frame";
                                 c.Write(name, int(std::strlen(name)) + 1);
                             }
                             c.Ascend();
                         }
                         c.Ascend();
                     },
                     [&](C_chunk &c) { return facing.LoadGame(c); },
                     [&](C_chunk &c) { return OriginalAt<T_ApChunk>(0x0055dbc0)(g_apOrig, &c); },
                     [&] { return RunsDiffer(kFacingRuns, &facing, g_apOrig, {0x54, 0x58, 0x5C}); },
                     [&] { RandomFacing(facing); CopyRuns(kFacingRuns, &facing, g_apOrig); });
            break;
        }
        case 1:
            RandomFireOrder(fireOrder);
            CopyRuns(kFireOrderRuns, &fireOrder, g_apOrig);
            SaveLoad("FireOrder", [&](C_chunk &c) { return fireOrder.SaveGame(c); },
                     [&](C_chunk &c) { return OriginalAt<T_ApChunk>(0x005608a0)(g_apOrig, &c); },
                     [&](C_chunk &c) { HandMade(c, 0x41, {{0x1158, 0x2A}, {0x1159, 4}}, true); },
                     [&](C_chunk &c) { return fireOrder.LoadGame(c); },
                     [&](C_chunk &c) { return OriginalAt<T_ApChunk>(0x00560ac0)(g_apOrig, &c); },
                     [&] { return RunsDiffer(kFireOrderRuns, &fireOrder, g_apOrig, {0x4C}); },
                     [&] { RandomFireOrder(fireOrder); CopyRuns(kFireOrderRuns, &fireOrder, g_apOrig); });
            break;
        case 2:
            RandomMove(move);
            CopyRuns(kMoveRuns, &move, g_apOrig);
            SaveLoad("Move", [&](C_chunk &c) { return move.SaveGame(c); },
                     [&](C_chunk &c) { return OriginalAt<T_ApChunk>(0x00560fd0)(g_apOrig, &c); },
                     [&](C_chunk &c) { HandMade(c, 0x41, {{0x8AC, 0x35}, {0x8AD, 4}, {0x8AE, 4}, {0x8AF, 8}}, true); },
                     [&](C_chunk &c) { return move.LoadGame(c); },
                     [&](C_chunk &c) { return OriginalAt<T_ApChunk>(0x00561280)(g_apOrig, &c); },
                     [&] { return RunsDiffer(kMoveRuns, &move, g_apOrig, {0x5C}); },
                     [&] { RandomMove(move); CopyRuns(kMoveRuns, &move, g_apOrig); });
            break;
        case 3:
            alert.state = I(0, 5);
            alert.changed = P(0.5f);
            CopyRuns(kAlertRuns, &alert, g_apOrig);
            // (A full chunk: the original stores an undefined value from a short one.)
            SaveLoad("Alert", [&](C_chunk &c) { return alert.SaveGame(c); },
                     [&](C_chunk &c) { return OriginalAt<T_ApChunk>(0x00563a80)(g_apOrig, &c); },
                     [&](C_chunk &c) {
                         c.Begin(0x41);
                         for (int k = I(0, 3); k > 0; --k) {
                             c.Begin(P(0.8f) ? 0x456 : uint16_t(I(1, 0x2000)));
                             const int state = I(0, 5);
                             const bool changed = P(0.5f);
                             c.Write(&state, 4);
                             c.Write(&changed, 1);
                             c.Ascend();
                         }
                         c.Ascend();
                     },
                     [&](C_chunk &c) { return alert.LoadGame(c); },
                     [&](C_chunk &c) { return OriginalAt<T_ApChunk>(0x00563c70)(g_apOrig, &c); },
                     [&] { return RunsDiffer({{0, 0, 5}}, &alert, g_apOrig); },
                     [&] {
                         alert.state = I(0, 5);
                         alert.changed = P(0.5f);
                         CopyRuns(kAlertRuns, &alert, g_apOrig);
                     });
            break;
        case 4:
            RandomFire(fire);
            CopyRuns(kFireRuns, &fire, g_apOrig);
            SaveLoad("Fire", [&](C_chunk &c) { return fire.SaveGame(c); },
                     [&](C_chunk &c) { return OriginalAt<T_ApChunk>(0x00576ae0)(g_apOrig, &c); },
                     [&](C_chunk &c) { HandMade(c, 0, {{0x157C, 0x19}, {0x157D, 4}, {0x157E, 4}, {0x157F, 4}}, false); },
                     [&](C_chunk &c) { return fire.LoadGame(c); },
                     [&](C_chunk &c) { return OriginalAt<T_ApChunk>(0x00576d00)(g_apOrig, &c); },
                     [&] { return RunsDiffer(kFireRuns, &fire, g_apOrig, {0x2C}); },
                     [&] { RandomFire(fire); CopyRuns(kFireRuns, &fire, g_apOrig); });
            break;
        default: {
            RandomDriver(driver);
            DriverToOrig(driver, g_apOrig);
            SaveLoad("Driver", [&](C_chunk &c) { return driver.SaveGame(c); },
                     [&](C_chunk &c) { return OriginalAt<T_ApChunk>(0x0055e8d0)(g_apOrig, &c); },
                     [&](C_chunk &c) {
                         if (P(0.1f))
                             return;                    // (nothing to load: LoadGame finds no chunk)
                         HandMade(c, 100, {{200, 4}, {300, 12}, {400, 4}, {500, 25}, {600, 1}, {700, 4}, {800, 4}, {900, 4},
                                           {1000, 12}, {0x44C, 4}, {0x4B0, 4}, {0x514, 4}, {0x578, 1}, {0x5DC, 1},
                                           {0x640, 4}, {0x6A4, 4}, {0x708, 12}},
                                  true);
                     },
                     [&](C_chunk &c) { return driver.LoadGame(c); },
                     [&](C_chunk &c) { return OriginalAt<T_ApChunk>(0x0055ec80)(g_apOrig, &c); },
                     [&] { return DriversDiffer(driver, g_apOrig); },
                     [&] {
                         FreeOrigDriver(g_apOrig);
                         RandomDriver(driver);
                         DriverToOrig(driver, g_apOrig);
                     });
            FreeOrigDriver(g_apOrig);
            break;
        }
        }
    }
}

// ---- the tasks: made by each side's pool (the original's AllocTask: its vtable), their fields random, saved and loaded
const uint32_t kTaskSave[13] = {0, 0x0054fe40, 0x00550370, 0x00550870, 0x00550d20, 0x00551100, 0x005514e0,
                                0x0054fc40, 0x0054fc40, 0x0054fc40, 0x0054fc40, 0x0054fc40, 0x0054fc40};
const uint32_t kTaskLoad[13] = {0, 0x00550090, 0x005505b0, 0x00550a70, 0x00550ed0, 0x005512b0, 0x00551690,
                                0x0054fcd0, 0x0054fcd0, 0x0054fcd0, 0x0054fcd0, 0x0054fcd0, 0x0054fcd0};
void *AiPools() { return OriginalAt<void *>(0x008ab158); }
void RandomTask(S_AITask *t)
{
    t->unknown08 = ApInt();
    t->unknown0C = ApInt();
    t->unknown10 = ApInt();
    switch (t->type) {
    case 1: {
        auto *k = static_cast<S_AITask1 *>(t);
        k->pos = ApVector();
        k->dir = ApVector();
        k->unknown40 = ApFloat();
        k->unknown44 = ApInt();
        k->checkpoint = ApInt();
        break;
    }
    case 2: {
        auto *k = static_cast<S_AITask2 *>(t);
        k->unknown20 = ApVector();
        k->unknown30 = ApVector();
        k->unknown40 = ApInt();
        // (Mostly humans: a load sets only a human.)
        k->human = reinterpret_cast<C_Human *>(P(0.2f) ? nullptr : PortActor(P(0.6f) ? I(2, 3) : I(0, AP_ACTORS - 1)));
        break;
    }
    case 3: {
        auto *k = static_cast<S_AITask3 *>(t);
        k->pos = ApVector();
        k->actor = ApActor();
        break;
    }
    case 7: {
        auto *k = static_cast<S_AITask7 *>(t);
        k->unknown20 = ApVector();
        k->unknown30 = ApVector();
        break;
    }
    case 8: {
        auto *k = static_cast<S_AITask8 *>(t);
        k->unknown14 = ApInt();
        k->unknown18 = ApInt();
        k->unknown1C = ApInt();
        break;
    }
    case 12: {
        auto *k = static_cast<S_AITask12 *>(t);
        k->pos = ApVector();
        k->unknown30 = ApInt();
        break;
    }
    default:
        break;
    }
}
void TestTasks()
{
    std::printf("aiparts: the tasks\n");
    typedef void *(AP * T_Alloc)(void *, int);
    typedef void(AP * T_Release)(void *, void *);
    for (int i = 0; i < 1200 / QUICK; ++i) {
        const int type = I(1, 12);
        S_AITask *pt = AllocTask(type);
        auto *ot = static_cast<uint8_t *>(OriginalAt<T_Alloc>(0x00582df0)(AiPools(), type));
        RandomTask(pt);
        const std::vector<S_ApRun> runs = TaskRuns(type);
        CopyRuns(runs, pt, ot);
        // (A full base chunk: the original compares an undefined type from a short one.)
        SaveLoad(("Task" + std::to_string(type)).c_str(), [&](C_chunk &c) { return pt->SaveGame(c); },
                 [&](C_chunk &c) { return OriginalAt<T_ApChunk>(kTaskSave[type])(ot, &c); },
                 [&](C_chunk &c) {
                     for (int k = I(0, 4); k > 0; --k) {
                         const int kind = I(0, 2);
                         if (kind == 0) {
                             c.Begin(0x157C);
                             const int t = P(0.8f) ? type : I(0, 13);
                             c.Write(&t, 4);
                             RandomBytes(c, P(0.7f) ? 12 : I(12, 16));
                             c.Ascend();
                         } else if (kind == 1) {
                             static const uint16_t OWN[13] = {0, 0x157D, 0x157E, 0x157F, 0x1582, 0x1581, 0x1580, 0, 0, 0, 0, 0, 0};
                             c.Begin(OWN[type] ? OWN[type] : uint16_t(0x157D));
                             static const int SIZES[13] = {0, 0x24, 0x20, 0x10, 0, 0, 0, 0, 0, 0, 0, 0, 0};
                             RandomBytes(c, P(0.6f) ? SIZES[type] : SIZES[type] + I(1, 8));
                             c.Ascend();
                         } else {
                             c.Begin(uint16_t(I(1, 0x2000)));
                             RandomBytes(c, I(0, 12));
                             c.Ascend();
                         }
                     }
                 },
                 [&](C_chunk &c) { return pt->LoadGame(c); },
                 [&](C_chunk &c) { return OriginalAt<T_ApChunk>(kTaskLoad[type])(ot, &c); },
                 [&] { return RunsDiffer(runs, pt, ot); },
                 [&] { RandomTask(pt); CopyRuns(runs, pt, ot); });
        ReleaseTask(pt);
        OriginalAt<T_Release>(0x00582c50)(AiPools(), ot);
    }
}

// ---- the pools: what each Alloc fills in, and which object it hands out (a released one again: the last first)
int ReuseIndex(std::vector<const void *> &released, const void *p)
{
    for (size_t k = released.size(); k-- > 0;)
        if (released[k] == p) {
            const int at = int(released.size() - 1 - k);
            released.erase(released.begin() + long(k));
            return at;
        }
    return -1;
}
void TestPools()
{
    std::printf("aiparts: the pools\n");
    std::map<int, std::vector<const void *>> portReleased, origReleased;
    std::map<int, std::vector<std::pair<void *, uint8_t *>>> live;
    for (int i = 0; i < 30000 / QUICK; ++i) {
        // (Kinds: 0 waypoints, 1..12 the tasks, 13 the remembered sounds.)
        const int kind = I(0, 13);
        auto &mine = live[kind];
        if (!mine.empty() && P(0.45f)) {
            const size_t at = size_t(I(0, int(mine.size()) - 1));
            void *p = mine[at].first;
            uint8_t *o = mine[at].second;
            mine.erase(mine.begin() + long(at));
            portReleased[kind].push_back(p);
            origReleased[kind].push_back(o);
            if (kind == 0) {
                ReleaseWaypoint(static_cast<S_AIWaypoint *>(p));
                typedef void(AP * T_Rel)(void *, void *);
                OriginalAt<T_Rel>(0x005638e0)(OriginalAt<uint8_t *>(0x008ab158) + 0x30, o);
            } else if (kind == 13) {
                // (The original's is inline; its waypoints' release, 0x005638e0, is the same list code.)
                FreeRememberedSound(static_cast<S_RememberedSound *>(p));
                typedef void(AP * T_Rel)(void *, void *);
                OriginalAt<T_Rel>(0x005638e0)(OriginalAt<uint8_t *>(0x008ab158) + 0x48, o);
            } else {
                ReleaseTask(static_cast<S_AITask *>(p));
                typedef void(AP * T_Release)(void *, void *);
                OriginalAt<T_Release>(0x00582c50)(AiPools(), o);
            }
            continue;
        }
        const S_vector4 a = ApVector(), b = ApVector();
        const int n = ApInt(), m = ApInt();
        const float f = ApFloat();
        void *p = nullptr;
        uint8_t *o = nullptr;
        std::vector<S_ApRun> runs;
        bool rawWaypoint = false;
        if (kind == 0) {
            const bool raw = P(0.3f);
            rawWaypoint = raw;
            const uint16_t flags = uint16_t(I(0, 0xFFFF));
            const uint8_t type = uint8_t(I(0, 255));
            if (raw) {
                p = AllocWaypoint();
                typedef void *(AP * T_Raw)(void *);
                o = static_cast<uint8_t *>(OriginalAt<T_Raw>(0x00582700)(AiPools()));
                // (A raw one keeps what it had: compared after the fill below.)
            } else {
                p = AllocWaypoint(n, a, flags, type, f);
                typedef void *(AP * T_Way)(void *, int, const void *, uint32_t, uint32_t, float);
                o = static_cast<uint8_t *>(OriginalAt<T_Way>(0x00582670)(AiPools(), n, &a, flags, type, f));
            }
            runs = {{0, 0, 0x2C}};
        } else if (kind == 13) {
            C_Actor *source = ApActor();
            p = AllocRememberedSound(n, a, source, uint32_t(m));
            typedef void *(AP * T_Sound)(void *, int, const void *, void *, uint32_t);
            o = static_cast<uint8_t *>(OriginalAt<T_Sound>(0x00582740)(AiPools(), n, &a,
                    reinterpret_cast<void *>(static_cast<uintptr_t>(OrigPointer(static_cast<uint32_t>(reinterpret_cast<uintptr_t>(source))))), uint32_t(m)));
            runs = {{0, 0, 0x1D}};
        } else {
            typedef void *(AP * T_Void)(void *);
            typedef void *(AP * T_Int)(void *, int);
            typedef void *(AP * T_T1)(void *, const void *, uint32_t, float, const void *);
            typedef void *(AP * T_T2)(void *, const void *, const void *, int);
            typedef void *(AP * T_T3)(void *, int, void *, const void *);
            typedef void *(AP * T_T7)(void *, const void *, const void *);
            typedef void *(AP * T_T12)(void *, const void *, int);
            C_Actor *actor = ApActor();
            void *origActor = reinterpret_cast<void *>(static_cast<uintptr_t>(OrigPointer(static_cast<uint32_t>(reinterpret_cast<uintptr_t>(actor)))));
            const uint16_t checkpoint = uint16_t(I(0, 0xFFFF));
            switch (kind) {
            case 1: p = AllocTask1(a, checkpoint, f, b); o = static_cast<uint8_t *>(OriginalAt<T_T1>(0x005827b0)(AiPools(), &a, checkpoint, f, &b)); break;
            case 2: p = AllocTask2(a, b, n); o = static_cast<uint8_t *>(OriginalAt<T_T2>(0x00582890)(AiPools(), &a, &b, n)); break;
            case 3: p = AllocTask3(n, actor, a); o = static_cast<uint8_t *>(OriginalAt<T_T3>(0x00582990)(AiPools(), n, origActor, &a)); break;
            case 4: p = AllocTask4(); o = static_cast<uint8_t *>(OriginalAt<T_Void>(0x00582a00)(AiPools())); break;
            case 5: p = AllocTask5(n); o = static_cast<uint8_t *>(OriginalAt<T_Int>(0x00582a40)(AiPools(), n)); break;
            case 6: p = AllocTask6(); o = static_cast<uint8_t *>(OriginalAt<T_Void>(0x00582a90)(AiPools())); break;
            case 7: p = AllocTask7(a, b); o = static_cast<uint8_t *>(OriginalAt<T_T7>(0x00582ad0)(AiPools(), &a, &b)); break;
            case 8: p = AllocTask8(n); o = static_cast<uint8_t *>(OriginalAt<T_Int>(0x00582b50)(AiPools(), n)); break;
            case 12: p = AllocTask12(a, n); o = static_cast<uint8_t *>(OriginalAt<T_T12>(0x00582920)(AiPools(), &a, n)); break;
            default: p = AllocTask(kind); o = static_cast<uint8_t *>(OriginalAt<T_Int>(0x00582df0)(AiPools(), kind)); break;
            }
            runs = TaskRuns(kind);
        }
        const int pr = ReuseIndex(portReleased[kind], p), orr = ReuseIndex(origReleased[kind], o);
        std::string why = pr == orr ? "" : "reuse " + std::to_string(pr) + " / " + std::to_string(orr);
        // (A new object: what its allocation does not set is undefined in the original (its pool's constructor leaves
        // it): a raw waypoint's all, a waypoint's padding byte, task 2's unknown40. Taken from the port's.)
        if (orr < 0) {
            if (kind == 0 && rawWaypoint)
                CopyRuns(runs, p, o);
            else if (kind == 0)
                o[0x27] = static_cast<const uint8_t *>(p)[0x27];
            else if (kind == 2)
                std::memcpy(o + 0x40, static_cast<const uint8_t *>(p) + 0x34, 4);
        }
        if (why.empty())
            why = RunsDiffer(runs, p, o);
        ApReport("Pool " + std::to_string(kind), why);
        CHECK(why.empty());
        // (Then other contents, the same on both sides: what a reused object keeps of them shows.)
        if (kind >= 1 && kind <= 12)
            RandomTask(static_cast<S_AITask *>(p));
        else if (kind == 0) {
            auto *w = static_cast<S_AIWaypoint *>(p);
            w->pos = ApVector();
            w->pos2 = ApVector();
            w->checkpoint = ApInt();
            w->flags = uint16_t(I(0, 0xFFFF));
            w->type = uint8_t(I(0, 255));
            w->radius = ApFloat();
        }
        CopyRuns(runs, p, o);
        mine.push_back({p, o});
    }
}

void InstallAiPartsRedirects()
{
    const std::pair<uint32_t, const void *> redirects[] = {
        {0x006987d0, reinterpret_cast<const void *>(AO_GetGame)},
        {0x0051d780, reinterpret_cast<const void *>(AO_FindNearest)},
        {0x0051d5a0, reinterpret_cast<const void *>(AO_Get)},
        {0x005223b0, reinterpret_cast<const void *>(AO_FindPath)},
        {0x004f2630, reinterpret_cast<const void *>(AO_SetUnknown4f2630)},
        {0x00475d50, reinterpret_cast<const void *>(AO_GetActorById)},
        {0x007e53c2, reinterpret_cast<const void *>(AO_AlignedMalloc)},
        {0x007e559c, reinterpret_cast<const void *>(AO_AlignedFree)},
        {0x007e566f, reinterpret_cast<const void *>(AO_New)},
        {0x007e2120, reinterpret_cast<const void *>(AO_Delete)},
        {0x007e6393, reinterpret_cast<const void *>(AO_Rand)},
    };
    for (const auto &r : redirects)
        RedirectOriginal(r.first, r.second);
    // The original's 16 pools (0x18 bytes each from 0x008ab158): their static initialiser did not run; it sets each
    // one's growth (+0x0C) to 16 (the free lists fill themselves when first asked).
    for (int k = 0; k < 16; ++k)
        *OriginalAt<int *>(0x008ab158 + 0x18 * k + 0x0C) = 16;
    // The game, its checkpoints and scene.
    auto *game = reinterpret_cast<C_game *>(g_apGamePort);
    game->m_checkpoints = reinterpret_cast<C_Checkpoints *>(g_apCheckpointsPort);
    for (void *&slot : g_apSceneVt)
        slot = reinterpret_cast<void *>(AP_SceneUnexpected);
    g_apSceneVt[0x7C / 4] = reinterpret_cast<void *>(AP_FindFrame);
    g_apScene.vt = g_apSceneVt;
    game->m_scene = reinterpret_cast<I3D_scene *>(&g_apScene);
    void *checkpoints = g_apCheckpointsOrig, *scene = &g_apScene;
    std::memcpy(g_apGameOrig + 0x54, &checkpoints, 4);
    std::memcpy(g_apGameOrig + 0x18, &scene, 4);
    g_names[PortRoads()] = g_names[OrigRoads()] = "roads";
    g_names[&g_apScene] = "scene";
    for (int i = 0; i < AP_FRAMES; ++i) {
        const char *name = AP_FRAME_NAMES[i];
        reinterpret_cast<I3D_frame *>(g_apFrames[i])->m_name = const_cast<char *>(name);
        CHECK(reinterpret_cast<I3D_frame *>(g_apFrames[i])->GetName() == name);
        g_names[g_apFrames[i]] = std::string("frame ") + name;
    }
    // The actors (the cars big enough for the port's C_Car).
    for (int i = 0; i < AP_ACTORS; ++i) {
        const size_t size = std::max<size_t>(sizeof(C_Car), 0x1000);
        g_apActorPort[i] = static_cast<uint8_t *>(_aligned_malloc(size, 16));
        g_apActorOrig[i] = static_cast<uint8_t *>(_aligned_malloc(0x1000, 16));
        std::memset(g_apActorPort[i], 0, size);
        std::memset(g_apActorOrig[i], 0, 0x1000);
        C_Actor *p = PortActor(i);
        p->m_type = AP_TYPES[i];
        p->m_netId = 3 + 11 * i;
        const int type = AP_TYPES[i], id = p->m_netId;
        std::memcpy(g_apActorOrig[i] + 0x18, &type, 4);
        std::memcpy(g_apActorOrig[i] + 0x10, &id, 4);
        g_names[g_apActorPort[i]] = g_names[g_apActorOrig[i]] = "actor" + std::to_string(i);
        if (AP_TYPES[i] == ACTOR_CAR) {
            auto *car = reinterpret_cast<C_Car *>(g_apActorPort[i]);
            g_apTopSpeed[i] = 10.0f + 7.5f * float(i);
            // (m_topSpeed is private to the vehicle: found through the public getter.)
            for (size_t at = 0; at + 4 <= sizeof(C_Car); at += 4) {
                uint32_t was;
                std::memcpy(&was, g_apActorPort[i] + at, 4);
                std::memcpy(g_apActorPort[i] + at, &g_apTopSpeed[i], 4);
                if (car->GetUnknown780() == g_apTopSpeed[i])
                    break;
                std::memcpy(g_apActorPort[i] + at, &was, 4);
            }
            CHECK(p->m_netId == id && p->m_type == ACTOR_CAR);
            std::memcpy(g_apActorOrig[i] + 0x780, &g_apTopSpeed[i], 4);
            CHECK(car->GetUnknown780() == g_apTopSpeed[i]);
            g_names[car->GetVehicle()] = g_names[g_apActorOrig[i] + 0x90] = "car" + std::to_string(i) + ".vehicle";
        }
    }
}

void TestAiParts()
{
    InstallAiPartsRedirects();
    for (void (*test)() : {TestFacing, TestFireOrder, TestMove, TestAlert, TestDriver, TestDriverConstructor, TestSaves, TestTasks, TestPools}) {
        const DWORD t0 = GetTickCount();
        test();
        std::printf("  (%lu ms)\n", static_cast<unsigned long>(GetTickCount() - t0));
    }
    std::remove(g_apPortFile.c_str());
    std::remove(g_apOrigFile.c_str());
    for (const auto &f : g_apFailures)
        std::printf("  %s: %ld failures\n", f.first.c_str(), f.second);
}
}  // namespace
