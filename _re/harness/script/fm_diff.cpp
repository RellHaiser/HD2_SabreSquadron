// Formation part 1: the formations' shapes (game/formation.cpp, the original's 0x005942d0..0x0059bb60) against the
// original executable's. cmd_diff.cpp's world, fakes and call logger are included (CMD_DIFF_NO_MAIN); the shapes' calls go
// through fm_calls.s / fm_calls.inc (gen_cmd_calls.py fm). The LS3DF math (SetDir, the vector by matrix products, the
// frames' world matrices) is LS3DF.dll's on both sides.
//
// Each case builds the same world on each side (cmd_diff's) and the shape's own: humans (one to seven) on model frames
// under the primary sector (turned, now and then scaled), with their health, velocity, vehicle, controller (a squad
// controller's combat order answered) and network id; vehicles (cars, submarines, static weapons, others) with their
// seats; a walking network of eight points (places, radii, types, links: doors among them). The shape is made on each
// side (the original's by its constructor 0x005949c0 in a block of 0xA0, the port's by its own) and runs the same random
// sequence of operations: SetType (every type, the next, none), SetScale, members added and removed, SetLeader (a member,
// another, none), OnLeaderDied, Tick (following the leader, at a door, boarding a vehicle), FindSlot and the slots'
// getters, InvalidateSlot, SetSlotTarget, PlanSlot, SetDoorPoint, the scripted flag, the world changed between ticks
// (moves, velocities, health, vehicles, controllers), saves loaded back, saves made by the test (every chunk, some
// missing, unknown ones, fewer slots) loaded. After each operation the answer, the shape's state (every field,
// every slot's) and every call made into the game and the engine are compared; the saves' bytes and the reader's depth
// too.
// MinGW; build_fm.bat; runs fm_diff.exe ("fm_diff: N checks, 0 failures"). Env: HD2_FM_CASES (default 1,500;
// HD2_FM_QUICK: 300), HD2_FM_SEED, HD2_FM_SHOW (1: the port's lines; 2: both), HD2_FM_ONLY (the case shown),
// HD2_FM_TRACE (a case's operations as they start).
#define HARNESS_NAME "fm_diff"
#define CMD_CALLS_INC "fm_calls.inc"
#define CMD_DIFF_NO_MAIN
#include "cmd_diff.cpp"

#include <deque>
#include <functional>

#define private public
#define protected public
#include "common/chunk.h"
#include "game/submarine.h"
#undef protected
#undef private
#include "game/formation_tables.inc"                     // (the port's table: HD2_FM_TABLE's check)

// ---- the port's side of what the shapes call and the test provides ----
int RW_DATA_CALL dtaOpenWrite(const char *name, int) { return H::FileOpenWrite(name); }
int RW_DATA_CALL dtaSeek(int h, int offset, int origin) { return H::FileSeek(h, offset, origin); }
int RW_DATA_CALL dtaWrite(int h, const void *data, int size) { return H::FileWrite(h, data, size); }

// LS3DF.dll's math (the original's calls go there too).
template <class T> T Ls3dfFn(const char *name) { return reinterpret_cast<T>(reinterpret_cast<void *>(Ls3df(name))); }
void __stdcall S_matrix::SetDir(const S_vector &dir)
{
    static const auto f = Ls3dfFn<void(__stdcall *)(S_matrix *, const S_vector &)>("?SetDir@S_matrix@@QAGXABUS_vector@@@Z");
    f(this, dir);
}
// The port's allocations zeroed, as the original's (its _aligned_malloc redirected to a zeroing one).
void *operator new(size_t n) { return std::calloc(n ? n : 1, 1); }
void operator delete(void *p) noexcept { std::free(p); }
void operator delete(void *p, size_t) noexcept { std::free(p); }
void *operator new(size_t n, std::align_val_t a)
{
    void *p = _aligned_malloc(n ? n : 1, size_t(a));
    std::memset(p, 0, n ? n : 1);
    return p;
}
void operator delete(void *p, std::align_val_t) noexcept { _aligned_free(p); }
void operator delete(void *p, size_t, std::align_val_t) noexcept { _aligned_free(p); }

namespace H {

#define OT __attribute__((thiscall))
int __cdecl O_Atexit(void *) { return 0; }
void *__cdecl O_AlignedMalloc(size_t size, size_t align)
{
    void *p = _aligned_malloc(size ? size : 1, align);
    std::memset(p, 0, size ? size : 1);
    return p;
}
void __cdecl O_AlignedFree(void *p) { _aligned_free(p); }
// (The slots' new[]: the count before the array, the array aligned.)
void *__cdecl O_AlignedOffsetMalloc(size_t size, size_t align, size_t offset)
{
    void *p = _aligned_offset_malloc(size ? size : 1, align, offset);
    std::memset(p, 0, size ? size : 1);
    return p;
}

uint32_t Ptr(const void *p) { return reinterpret_cast<uint32_t>(p); }
template <class T> T &Mut(void *p, uint32_t off) { return *reinterpret_cast<T *>(static_cast<uint8_t *>(p) + off); }
uint8_t *Child(uint8_t *parent, int type, const std::string &name, uint8_t *owner)
{
    uint8_t *b = NewFrame(type, name);
    S_FakeFrame &f = W().frames.back();
    I3D_frame *x = AsFrame(b);
    x->m_name = f.name.c_str();
    x->m_ownerModel = reinterpret_cast<I3D_frame *>(owner);
    x->m_frameFlags |= 1;
    if (parent)
        LinkUnder(b, parent);
    return b;
}
// A frame's local matrix set whole (rows scaled), as I3D_frame::SetLocalMatrix: the rotation taken from it.
void SetLocal(uint8_t *frame, const S_matrix &m, const S_vector &scale)
{
    I3D_frame *f = AsFrame(frame);
    f->m_localMatrix = m;
    f->m_scale = scale;
    f->m_frameFlags = (f->m_frameFlags & ~0x28u) | FRMFLAGS_CHANGED | FRMFLAGS_LMATRIX_VALID | FRMFLAGS_ROT_VALID;
}
std::string Hex(const std::string &bytes)
{
    std::string hex;
    for (unsigned char ch : bytes)
        hex += F("%02x", ch);
    return hex;
}

// ---- the formation's world ----
constexpr int POINTS = 8, MAX_HUMANS = 7, MAX_VEHICLES = 3;
constexpr uint32_t O_FM_CTOR = 0x005949c0, O_FM_DTOR = 0x00594a70, O_FM_SET_TYPE = 0x00597420, O_FM_SET_SCALE = 0x005977f0,
                   O_FM_ADD = 0x00597f80, O_FM_REMOVE = 0x005981c0, O_FM_LEADER = 0x00597c50, O_FM_LEADER_DIED = 0x00595710,
                   O_FM_TICK = 0x0059ba70, O_FM_SAVE = 0x005942d0, O_FM_LOAD = 0x00594560, O_FM_FIND_SLOT = 0x00598290,
                   O_FM_TARGET = 0x00598320, O_FM_DIR = 0x005983c0, O_FM_SEAT = 0x00598460, O_FM_INVALIDATE = 0x005982d0,
                   O_FM_SET_TARGET = 0x00599040, O_FM_PLAN = 0x0059af30, O_FM_DOOR = 0x00594a50;

struct S_FmHuman {
    float x = 0.0f, y = 0.0f, z = 0.0f, heading = 0.0f;
    float px = 0.0f, py = 0.0f, pz = 0.0f;              // (the actor's own position: near the frame's)
    int scale = 0;                                      // (0: none; 1, 2: scaled)
    int health = 100;
    int velocity = 0;
    int vehicle = -1;
    int controllerType = HUMAN_CONTROLLER_SQUAD;
    bool controller = true;
};
struct S_FmVehicle {
    int type = ACTOR_CAR;
    int seats = 2;
};
struct S_FmPoint {
    float x = 0.0f, y = 0.0f, z = 0.0f;
    int radius = 16;
    int type = 0;
    int links = 0;
    uint16_t targets[3] = {};
};
enum { OP_SET_TYPE, OP_SET_SCALE, OP_ADD, OP_REMOVE, OP_LEADER, OP_LEADER_DIED, OP_TICK, OP_FIND_SLOT, OP_TARGET, OP_DIR,
       OP_SEAT, OP_INVALIDATE, OP_SET_TARGET, OP_PLAN, OP_DOOR, OP_SCRIPTED, OP_WORLD, OP_SAVE_LOAD, OP_LOAD_MADE, OP_KINDS };
const char *const OP_KIND_NAMES[] = {"SetType", "SetScale", "AddMember", "RemoveMember", "SetLeader", "OnLeaderDied", "Tick",
                                     "FindSlot", "GetSlotTarget", "GetSlotDir", "GetSlotSeat", "InvalidateSlot", "SetSlotTarget",
                                     "PlanSlot", "SetDoorPoint", "Scripted", "World", "SaveLoad", "LoadMade"};
struct S_FmOp {
    int kind = 0;
    int a = 0, b = 0, c = 0, d = 0;
    std::string data;                                   // (OP_LOAD_MADE: the save's bytes)
};
struct S_FmCase {
    std::vector<S_FmHuman> humans;
    std::vector<S_FmVehicle> vehicles;
    S_FmPoint points[POINTS];
    bool subsListed = true;                             // (GetSubmarines lists the case's submarines)
    bool noPoints = false;                              // (no point near anywhere: the slots stay at the shape's places)
    std::vector<S_FmOp> ops;
};

// The case's world on each side.
uint8_t *g_human[2][MAX_HUMANS];
uint8_t *g_humanFrame[2][MAX_HUMANS];
uint8_t *g_vehicle[2][MAX_VEHICLES];
uint8_t *g_points[2];
uint8_t *g_shape[2];
uint8_t *g_subsOrig;                                    // (the original's vector: first +4, last +8)
std::vector<C_Actor *> g_subsPort;
int g_humanCount, g_vehicleCount;
bool g_noPoints;
int g_seats[MAX_VEHICLES];
int g_controllerType[MAX_HUMANS];                       // (GetControllerType's answers)
std::vector<int> g_doorPoints;                          // (the points with a door's point among their links)
C_FormationShape *PortShape() { return reinterpret_cast<C_FormationShape *>(g_shape[0]); }

int HumanIndex(uint32_t p)
{
    for (int i = 0; i < g_humanCount; ++i)
        if (Ptr(g_human[g_side][i]) == p)
            return i;
    return -1;
}
uint8_t *HumanAt(int i) { return i >= 0 && i < g_humanCount ? g_human[g_side][i] : nullptr; }
const S_vector VELOCITIES[] = {{0.0f, 0.0f, 0.0f},   {1.5f, 0.0f, 0.5f}, {0.0f, 0.0f, -2.0f},  {0.0f, 1.0f, 0.0f},
                               {1e-20f, 0.0f, 0.0f}, {0.3f, 0.0f, 0.3f}, {-4.0f, 0.2f, 1.0f},  {0.05f, 0.0f, 0.0f},
                               {0.0f, 0.0f, 0.0f},   {2.0f, 0.0f, 0.0f}, {-0.5f, 0.0f, -0.5f}, {0.06f, 0.0f, 0.06f},
                               // (Upward too (the push aside's cross products), a way 0.12 from another's (its 0.1
                               // limit), a square of 0.0121 (the doors' 0.01).)
                               {1.0f, 0.5f, 1.0f},   {0.5f, -0.3f, -1.0f}, {0.12f, 0.0f, 0.99f}, {0.11f, 0.0f, 0.0f}};
constexpr int VELOCITY_COUNT = int(sizeof VELOCITIES / sizeof *VELOCITIES);

// A human's frame placed (its local matrix: turned about y, scaled now and then).
void PlaceHuman(int i, const S_FmHuman &h)
{
    S_matrix m;
    m.Identity();
    const float ch = std::cos(h.heading), sh = std::sin(h.heading);
    m.m[0][0] = ch;
    m.m[0][2] = -sh;
    m.m[2][0] = sh;
    m.m[2][2] = ch;
    static const float scales[3] = {1.0f, 1.25f, 0.8f};
    const float s = scales[h.scale];
    for (int r = 0; r < 3; ++r)
        for (int k = 0; k < 3; ++k)
            m.m[r][k] *= s;
    m.m[3][0] = h.x;
    m.m[3][1] = h.y;
    m.m[3][2] = h.z;
    SetLocal(g_humanFrame[g_side][i], m, S_vector(s, s, s));
    uint8_t *a = g_human[g_side][i];
    const S_vector pos(h.px, h.py, h.pz);
    if (g_side)
        std::memcpy(a + 0x30, &pos, 12);
    else
        reinterpret_cast<C_Actor *>(a)->SetPosition(pos);
}
void SetVelocity(int i, int v)
{
    const S_vector &vel = VELOCITIES[v % VELOCITY_COUNT];
    uint8_t *a = g_human[g_side][i];
    if (g_side)
        std::memcpy(a + 0x1B0, &vel, 12);
    else
        reinterpret_cast<C_Human *>(a)->m_velocity = {vel.x, vel.y, vel.z, 0.0f};
}
void SetHealth(int i, int health)
{
    uint8_t *a = g_human[g_side][i];
    if (g_side)
        Mut<int>(a, 0xEC) = health;
    else
        reinterpret_cast<C_Human *>(a)->m_stats[STAT_HEALTH] = health;
}
void SetVehicle(int i, int v)
{
    uint8_t *a = g_human[g_side][i];
    uint8_t *vehicle = v >= 0 && v < g_vehicleCount ? g_vehicle[g_side][v] : nullptr;
    if (g_side)
        Mut<uint8_t *>(a, 0x160) = vehicle;
    else
        reinterpret_cast<C_Human *>(a)->m_vehicle = reinterpret_cast<C_Actor *>(vehicle);
}
void SetController(int i, bool on)
{
    uint8_t *a = g_human[g_side][i];
    static void **vtables[2];
    if (!vtables[g_side])
        vtables[g_side] = GameVtable("C_SquadController", VirtualSlots("C_SquadController::", g_side != 0));
    uint8_t *c = nullptr;
    if (on) {
        c = Block(std::max(sizeof(C_SquadController), size_t(0x400)) + 0x40);
        *reinterpret_cast<void ***>(c) = vtables[g_side];
        Name(c, F("controller of human%d", i));
    }
    if (g_side)
        Mut<uint8_t *>(a, 0xB0) = c;
    else
        reinterpret_cast<C_Human *>(a)->m_controller = reinterpret_cast<C_HumanController *>(c);
}

void BuildFormationWorld(const S_FmCase &c)
{
    static void **actorVtables[2];
    if (!actorVtables[g_side])
        actorVtables[g_side] = GameVtable("C_Actor", VirtualSlots("C_Actor::", g_side != 0));
    uint8_t *sector = *reinterpret_cast<uint8_t **>(W().scene + 0x1F8);
    // The vehicles first (the humans ride them).
    g_vehicleCount = int(c.vehicles.size());
    std::vector<uint8_t *> subs;
    for (int i = 0; i < g_vehicleCount; ++i) {
        uint8_t *v = Block(ActorBytes(ACTOR_CAR));
        *reinterpret_cast<void ***>(v) = actorVtables[g_side];
        Name(v, F("vehicle%d", i));
        const int type = c.vehicles[size_t(i)].type;
        if (g_side) {
            Mut<int>(v, 0x18) = type;
            Mut<int>(v, 0x10) = 300 + i;
        } else {
            reinterpret_cast<C_Actor *>(v)->m_type = E_ActorType(type);
            reinterpret_cast<C_Actor *>(v)->m_netId = 300 + i;
        }
        g_seats[i] = c.vehicles[size_t(i)].seats;
        g_vehicle[g_side][i] = v;
        if (type == ACTOR_SUBMARINE && c.subsListed)
            subs.push_back(v);
    }
    // (The submarines' list: one more, not the case's, now and then.)
    if (c.subsListed && !subs.empty() && (c.vehicles.size() & 1)) {
        uint8_t *v = Block(ActorBytes(ACTOR_CAR));
        *reinterpret_cast<void ***>(v) = actorVtables[g_side];
        Name(v, "other submarine");
        if (g_side) {
            Mut<int>(v, 0x18) = ACTOR_SUBMARINE;
            Mut<int>(v, 0x10) = 399;
        } else {
            reinterpret_cast<C_Actor *>(v)->m_type = ACTOR_SUBMARINE;
            reinterpret_cast<C_Actor *>(v)->m_netId = 399;
        }
        subs.insert(subs.begin(), v);
    }
    if (g_side) {
        g_subsOrig = Block(0x10);
        uint8_t **list = reinterpret_cast<uint8_t **>(Block(4 * (subs.size() + 1)));
        std::copy(subs.begin(), subs.end(), list);
        Mut<uint8_t **>(g_subsOrig, 4) = list;
        Mut<uint8_t **>(g_subsOrig, 8) = list + subs.size();
        Mut<uint8_t **>(g_subsOrig, 12) = list + subs.size();
    } else {
        g_subsPort.clear();
        for (uint8_t *v : subs)
            g_subsPort.push_back(reinterpret_cast<C_Actor *>(v));
    }
    // The humans.
    g_humanCount = int(c.humans.size());
    for (int i = 0; i < g_humanCount; ++i) {
        const S_FmHuman &h = c.humans[size_t(i)];
        uint8_t *a = Block(ActorBytes(ACTOR_HUMAN));
        *reinterpret_cast<void ***>(a) = actorVtables[g_side];
        Name(a, F("human%d", i));
        uint8_t *frame = Child(sector, FRAME_MODEL, F("human%d", i), nullptr);
        g_human[g_side][i] = a;
        g_humanFrame[g_side][i] = frame;
        if (g_side) {
            Mut<int>(a, 0x18) = ACTOR_HUMAN;
            Mut<int>(a, 0x10) = 200 + i;
            Mut<uint8_t *>(a, 0x20) = frame;
        } else {
            reinterpret_cast<C_Actor *>(a)->m_type = ACTOR_HUMAN;
            reinterpret_cast<C_Actor *>(a)->m_netId = 200 + i;
            reinterpret_cast<C_Actor *>(a)->m_frame = AsFrame(frame);
        }
        Name(g_side ? a + 0x98 : reinterpret_cast<uint8_t *>(&reinterpret_cast<C_Human *>(a)->m_body), F("body of human%d", i));
        PlaceHuman(i, h);
        SetVelocity(i, h.velocity);
        SetHealth(i, h.health);
        SetVehicle(i, h.vehicle);
        SetController(i, h.controller);
        g_controllerType[i] = h.controllerType;
    }
    // The walking network (the same layout on both sides).
    g_points[g_side] = Block(0x24 * POINTS);
    g_doorPoints.clear();
    for (int i = 0; i < POINTS; ++i) {
        const S_FmPoint &p = c.points[i];
        uint8_t *at = g_points[g_side] + 0x24 * i;
        S_Checkpoint &q = *reinterpret_cast<S_Checkpoint *>(at);
        q.pos = S_vector(p.x, p.y, p.z);
        q.radius = uint8_t(p.radius);
        q.type = uint8_t(p.type);
        q.flags = uint16_t(p.links);
        S_CheckpointLink *links = reinterpret_cast<S_CheckpointLink *>(Block(4 * 4));
        bool door = false;
        for (int k = 0; k < p.links; ++k) {
            links[k].target = p.targets[k];
            links[k].cost = uint16_t(100 + k);
            const int t = c.points[p.targets[k]].type;
            door = door || t == 1 || t == 2;
        }
        q.links = links;
        Name(at, F("point%d", i));
        if (door)
            g_doorPoints.push_back(i);
    }
}

// ---- the answers ----
S_vector g_viewDirs[2][64];
int g_viewDirCount[2];
void SetUpFormationAnswers()
{
    g_answers["C_CheckpointNetwork::Get"] = [](const std::vector<uint32_t> &a) {
        return a[1] < uint32_t(POINTS) ? Ptr(g_points[g_side] + 0x24 * a[1]) : 0u;
    };
    auto nearest = [](const std::vector<uint32_t> &) {
        return !g_noPoints && NextFake() % 5 ? NextFake() % POINTS : uint32_t(CHECKPOINT_NONE);
    };
    g_answers["C_CheckpointNetwork::FindNearest"] = nearest;
    g_answers["C_CheckpointNetwork::FindNearestType15"] = nearest;
    // A path (0..5 points, or none found; the out vector in each side's layout).
    g_answers["C_CheckpointNetwork::FindPath"] = [](const std::vector<uint32_t> &a) {
        if (NextFake() % 5 == 0)
            return 0u;
        std::vector<uint16_t> found(NextFake() % 6);
        for (uint16_t &p : found)
            p = uint16_t(NextFake() % POINTS);
        std::string text = " path";
        for (uint16_t p : found)
            text += F(" %d", int(p));
        g_log[g_side].push_back(text);
        uint8_t *out = reinterpret_cast<uint8_t *>(a[3]);
        if (g_side) {
            uint8_t *buf = static_cast<uint8_t *>(O_New(2 * found.size() + 2));
            if (!found.empty())
                std::memcpy(buf, found.data(), 2 * found.size());
            *reinterpret_cast<uint8_t **>(out + 4) = buf;
            *reinterpret_cast<uint8_t **>(out + 8) = buf + 2 * found.size();
            *reinterpret_cast<uint8_t **>(out + 0xC) = buf + 2 * found.size();
        } else {
            reinterpret_cast<std::vector<uint16_t> *>(out)->assign(found.begin(), found.end());
        }
        return 1u;
    };
    g_answers["C_ActorsModule::GetActorById"] = [](const std::vector<uint32_t> &a) {
        const int id = int(a[1]);
        if (id >= 200 && id < 200 + g_humanCount)
            return Ptr(g_human[g_side][id - 200]);
        if (id >= 300 && id < 300 + g_vehicleCount)
            return Ptr(g_vehicle[g_side][id - 300]);
        return 0u;
    };
    g_answers["C_Human::GetControllerType"] = [](const std::vector<uint32_t> &a) {
        const int i = HumanIndex(a[0]);
        return i < 0 ? 0u : uint32_t(g_controllerType[i]);
    };
    g_answers["C_SquadController::GetCombatOrder"] = [](const std::vector<uint32_t> &) {
        static const int orders[] = {0xC, 0xD, 0xE, 0xF, 0x10, 0x11, 0, 3, 0xB, 0x12};
        return uint32_t(orders[NextFake() % 10]);
    };
    g_answers["C_Human::GetViewDir"] = [](const std::vector<uint32_t> &) {
        // (Tiny ones too: the unit's signs.)
        static const S_vector views[] = {{0.0f, 0.0f, 1.0f},  {1.0f, 0.0f, 0.0f},   {0.0f, 0.0f, 0.0f},  {0.6f, 0.5f, -0.8f}, {0.0f, 1.0f, 0.0f},
                                         {1e-5f, 0.0f, 0.0f}, {-1e-5f, 0.0f, 0.0f}, {0.0f, 0.0f, 1e-5f}, {0.0f, 0.0f, -1e-5f}};
        S_vector &v = g_viewDirs[g_side][g_viewDirCount[g_side]++ % 64];
        v = views[NextFake() % 9];
        return Ptr(&v);
    };
    g_answers["S_HumanBody::GetClimbState"] = [](const std::vector<uint32_t> &) { return NextFake() % 4 ? 0u : 1u + NextFake() % 2; };
    g_answers["GetSubmarines"] = [](const std::vector<uint32_t> &) { return g_side ? Ptr(g_subsOrig) : Ptr(&g_subsPort); };
    // The vehicles' seats: their counts, now and then an occupant, the places to get in (from the seat).
    auto vehicleIndex = [](uint32_t p) {
        for (int i = 0; i < g_vehicleCount; ++i)
            if (Ptr(g_vehicle[g_side][i]) == p)
                return i;
        return -1;
    };
    g_answers["C_Actor::GetSeatCount"] = [vehicleIndex](const std::vector<uint32_t> &a) {
        const int i = vehicleIndex(a[0]);
        return i < 0 ? 2u : uint32_t(g_seats[i]);
    };
    g_answers["C_Actor::GetSeatOccupant"] = [](const std::vector<uint32_t> &) {
        if (NextFake() % 3)
            return 0u;
        return g_humanCount ? Ptr(g_human[g_side][NextFake() % uint32_t(g_humanCount)]) : 0u;
    };
    auto place = [vehicleIndex](const std::vector<uint32_t> &a, float side) {
        const int v = vehicleIndex(a[0]);
        const float seat = float(int(a[1]));
        S_vector4 *pos = reinterpret_cast<S_vector4 *>(a[2]);
        S_vector4 *dir = reinterpret_cast<S_vector4 *>(a[3]);
        // (Now and then every seat's at one place: the nearest is a tie.)
        *pos = NextFake() % 4 ? S_vector4{float(v) * 3.0f + seat * 0.75f + side, 0.5f, -seat * 0.5f, 1.0f}
                              : S_vector4{float(v) * 3.0f + side, 0.5f, 0.0f, 1.0f};
        static const S_vector4 dirs[] = {{0.0f, 0.0f, 1.0f, 0.0f}, {1.0f, 0.0f, 0.0f, 0.0f}, {0.6f, 0.0f, -0.8f, 0.0f}};
        *dir = dirs[NextFake() % 3];
        return NextFake() % 5 ? 1u : 0u;
    };
    g_answers["C_Car::GetSeatEntry"] = [place](const std::vector<uint32_t> &a) { return place(a, 0.0f); };
    g_answers["C_Submarine::FindExit"] = [place](const std::vector<uint32_t> &a) { return place(a, a[4] & 0xFF ? 1.5f : -1.5f); };
}

// ---- the compared state ----
std::string V(uint32_t p, int n = 4) { return VectorText(p, n); }
std::vector<std::string> ShapeState()
{
    std::vector<std::string> out;
    const uint8_t *b = g_shape[g_side];
    // (The w words of dir, scale and lastDir not: the original's are stack leftovers, never read nor saved.)
    if (g_side) {
        out.push_back("matrix " + V(Ptr(b), 16) + " dir " + V(Ptr(b + 0x40), 3) + " scale " + V(Ptr(b + 0x50), 3) + " lastDir " +
                      V(Ptr(b + 0x60), 3));
        out.push_back(F("changed %d leader %s type %d count %d mode %d door %d %d scripted %d slots %d", int(b[0x70]),
                        NameOf(At<uint32_t>(b, 0x74)).c_str(), At<int>(b, 0x78), At<int>(b, 0x7C), At<int>(b, 0x80),
                        int(At<uint16_t>(b, 0x84)), At<int>(b, 0x88), int(b[0x8C]), int(At<uint32_t>(b, 0x90) != 0)));
        const uint8_t *slots = reinterpret_cast<const uint8_t *>(At<uint32_t>(b, 0x90));
        for (int i = 0; slots && i < C_FormationShape::SLOT_COUNT; ++i) {
            const uint8_t *s = slots + 0xA0 * i;
            std::string path;
            for (uint32_t p = At<uint32_t>(s, 0x90); p && p < At<uint32_t>(s, 0x94); p += 2)
                path += F(" %d", int(*reinterpret_cast<const uint16_t *>(p)));
            // (Nor the slots' vectors' w words: the original's are stack leftovers wherever it writes them (FitOnNetwork
            // pulling the target onto a point's radius, SetLeader's places, FollowLeader's directions) or the table's
            // padding (the base place); nothing reads them, the saves write 12 bytes.)
            out.push_back(F("slot%d base ", i) + V(Ptr(s), 3) + " scaled " + V(Ptr(s + 0x10), 3) + " target " + V(Ptr(s + 0x20), 3) +
                          " current " + V(Ptr(s + 0x30), 3));
            out.push_back("  dir " + V(Ptr(s + 0x40), 3) + " baseDir " + V(Ptr(s + 0x50), 3) + " scaledDir " + V(Ptr(s + 0x60), 3) +
                          F(" member %s u74 %s distance ", NameOf(At<uint32_t>(s, 0x70)).c_str(), NameOf(At<uint32_t>(s, 0x74)).c_str()) +
                          FloatText(At<float>(s, 0x78)) +
                          F(" aside %d checkpoint %d seat %d vehicle %d dirty %d from %d path", int(s[0x7C]), int(At<uint16_t>(s, 0x7E)),
                            At<int>(s, 0x80), At<int>(s, 0x84), int(s[0x88]), int(At<uint16_t>(s, 0x9C))) +
                          path);
        }
        return out;
    }
    const C_FormationShape *p = PortShape();
    out.push_back("matrix " + V(Ptr(&p->m_matrix), 16) + " dir " + V(Ptr(&p->m_dir), 3) + " scale " + V(Ptr(&p->m_scale), 3) +
                  " lastDir " + V(Ptr(&p->m_lastDir), 3));
    out.push_back(F("changed %d leader %s type %d count %d mode %d door %d %d scripted %d slots %d", int(p->m_changed),
                    NameOf(Ptr(p->m_leader)).c_str(), int(p->m_type), p->m_memberCount, p->m_mode, int(p->m_doorPoint), p->m_doorState,
                    int(p->m_scripted), int(p->m_slots != nullptr)));
    for (int i = 0; p->m_slots && i < C_FormationShape::SLOT_COUNT; ++i) {
        const S_FormationSlot &s = p->m_slots[i];
        std::string path;
        for (uint16_t q : s.path)
            path += F(" %d", int(q));
        out.push_back(F("slot%d base ", i) + V(Ptr(&s.basePos), 3) + " scaled " + V(Ptr(&s.scaledPos), 3) + " target " +
                      V(Ptr(&s.target), 3) + " current " + V(Ptr(&s.current), 3));
        out.push_back("  dir " + V(Ptr(&s.dir), 3) + " baseDir " + V(Ptr(&s.baseDir), 3) + " scaledDir " + V(Ptr(&s.scaledDir), 3) +
                      F(" member %s u74 %s distance ", NameOf(Ptr(s.member)).c_str(), NameOf(Ptr(s.unknown74)).c_str()) +
                      FloatText(s.distance) +
                      F(" aside %d checkpoint %d seat %d vehicle %d dirty %d from %d path", int(s.pushedAside), int(s.checkpoint), s.seat,
                        s.vehicleId, int(s.dirty), int(s.pathFrom)) +
                      path);
    }
    return out;
}

// ---- the saves the test makes ----
struct S_Writer {
    std::string out;
    std::vector<size_t> open;
    void Begin(uint16_t id)
    {
        open.push_back(out.size());
        out.append(reinterpret_cast<const char *>(&id), 2);
        out.append(4, '\0');
    }
    void End()
    {
        const size_t at = open.back();
        open.pop_back();
        const uint32_t size = uint32_t(out.size() - at);
        std::memcpy(&out[at + 2], &size, 4);
    }
    void Raw(const void *p, size_t n) { out.append(static_cast<const char *>(p), n); }
    template <class T> void Chunk(uint16_t id, const T &value)
    {
        Begin(id);
        Raw(&value, sizeof value);
        End();
    }
};
float SaveFloat()
{
    static const float values[] = {0.0f, 1.0f, -1.0f, 2.5f, 0.5f, 3.0f, -2.0f, 0.25f, 10.0f, 1.2f};
    return values[I(0, 9)];
}
struct S_V3 {
    float x, y, z;
};
S_V3 SaveVector() { return {SaveFloat(), SaveFloat(), SaveFloat()}; }
// A save of the shape's layout (every chunk now and then missing, unknown ones among them, the slots' members distinct,
// the leader only in the first; fewer slots now and then: the reader goes on past their end), a sibling after it.
std::string MadeSave(const S_FmCase &c, const std::vector<int> &doorPoints, bool &slotsSaved)
{
    slotsSaved = false;
    S_Writer w;
    w.Begin(0x2000);
    // (Now and then no shape in it at all.)
    if (P(0.04)) {
        w.End();
        return w.out;
    }
    w.Begin(0x5BE);
    const int humans = int(c.humans.size());
    const int leader = humans && P(0.7) ? I(0, humans - 1) : P(0.5) ? -1 : 999;
    if (P(0.9)) {
        float m[16];
        for (float &f : m)
            f = SaveFloat();
        w.Begin(1000);
        w.Raw(m, sizeof m);
        w.End();
    }
    if (P(0.9))
        w.Chunk(2000, SaveVector());
    if (P(0.9))
        w.Chunk(3000, SaveVector());
    if (P(0.9))
        w.Chunk(4000, uint8_t(I(0, 1)));
    if (P(0.9))
        w.Chunk(5000, uint8_t(I(0, 1)));
    if (P(0.9)) {
        const uint16_t door = doorPoints.empty() || P(0.6) ? uint16_t(0xFFFF) : uint16_t(doorPoints[size_t(I(0, int(doorPoints.size()) - 1))]);
        w.Chunk(6000, door);
    }
    if (P(0.9))
        w.Chunk(7000, int32_t(I(-1, 3)));
    if (P(0.9))
        w.Chunk(8000, int32_t(I(0, 6)));
    if (P(0.2))
        w.Chunk(777, int32_t(I(0, 100)));
    if (leader != -1 && P(0.95))
        w.Chunk(9000, int32_t(leader == 999 ? 999 : 200 + leader));
    if (P(0.9)) {
        static const int types[] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 13};
        w.Chunk(10000, int32_t(types[I(0, 11)]));
    }
    if (P(0.9))
        w.Chunk(11000, int32_t(I(0, 2)));
    if (P(0.85)) {
        std::vector<int> order;
        for (int i = 0; i < humans; ++i)
            if (i != leader)
                order.push_back(i);
        for (size_t i = order.size(); i > 1; --i)
            std::swap(order[i - 1], order[size_t(I(0, int(i) - 1))]);
        w.Begin(12000);
        slotsSaved = true;
        const int slots = P(0.85) ? 6 : I(0, 5);
        // (The leader's slot mostly the first; now and then one of the next four: RemoveMember then moves it along.
        // Not the last: removing a member before it, the original reads past the slots.)
        const int leaderSlot = P(0.7) ? 0 : I(1, 4);
        size_t next = 0;
        for (int s = 0; s < slots; ++s) {
            w.Begin(0x1E61);
            for (uint16_t id : {1000, 2000, 3000, 4000, 5000, 6000, 7000})
                if (P(0.9))
                    w.Chunk(id, SaveVector());
            int member = -1;
            if (s == leaderSlot && leader >= 0 && leader < humans)
                member = leader;
            else if (next < order.size() && P(0.7))
                member = order[next++];
            if (member >= 0 && P(0.95))
                w.Chunk(8000, int32_t(200 + member));
            if (P(0.15))
                w.Chunk(9000, int32_t(P(0.5) ? 300 : 200 + I(0, 9)));
            if (P(0.9))
                w.Chunk(10000, SaveFloat());
            if (P(0.9))
                w.Chunk(11000, int32_t(I(0, 3)));
            if (P(0.9))
                w.Chunk(0x2CEC, int32_t(P(0.5) ? -1 : 300 + I(0, 3)));
            if (P(0.9))
                w.Chunk(12000, uint8_t(I(0, 1)));
            if (P(0.9))
                w.Chunk(13000, uint8_t(I(0, 1)));
            if (P(0.9))
                w.Chunk(14000, uint16_t(P(0.4) ? 0xFFFF : I(0, POINTS - 1)));
            if (P(0.9))
                w.Chunk(15000, uint16_t(P(0.4) ? 0xFFFF : I(0, POINTS - 1)));
            if (P(0.1))
                w.Chunk(4321, int32_t(5));
            if (P(0.5)) {
                // (The points after the count's chunk, in the slot's.)
                const int count = I(0, 5);
                w.Chunk(16000, int32_t(count));
                for (int k = 0; k < count; ++k) {
                    const uint16_t q = uint16_t(I(0, POINTS - 1));
                    w.Raw(&q, 2);
                }
            }
            w.End();
        }
        w.End();
    }
    w.End();
    // (A sibling after the shape: its loop ends at the shape's end.)
    if (P(0.3))
        w.Chunk(0x5BF, int32_t(7));
    w.End();
    // (Not cut short: past the file's end the original handles the last id again and reads its stack words, a failed
    // read's leftovers; see COVERAGE.md.)
    return w.out;
}

// ---- the cases ----
int g_fmCases, g_opCounts[OP_KINDS];
S_FmCase RandomFmCase()
{
    S_FmCase c;
    const int humans = P(0.1) ? MAX_HUMANS : I(1, 6);
    const int vehicles = I(0, MAX_VEHICLES);
    for (int i = 0; i < vehicles; ++i) {
        S_FmVehicle v;
        static const int types[] = {ACTOR_CAR, ACTOR_CAR, ACTOR_SUBMARINE, ACTOR_SUBMARINE, 0x15, 0x22, ACTOR_DOOR};
        v.type = types[I(0, 6)];
        v.seats = I(0, 6);
        c.vehicles.push_back(v);
    }
    c.subsListed = P(0.85);
    c.noPoints = P(0.2);
    for (int i = 0; i < humans; ++i) {
        S_FmHuman h;
        h.x = float(I(-20, 20)) * 0.5f;
        h.y = float(I(-2, 4)) * 0.5f;
        h.z = float(I(-20, 20)) * 0.5f;
        h.heading = float(I(0, 628)) * 0.01f;
        h.px = h.x + (P(0.7) ? 0.0f : float(I(-4, 4)) * 0.25f);
        h.py = h.y + (P(0.7) ? 0.0f : float(I(-4, 4)) * 0.25f);
        h.pz = h.z + (P(0.7) ? 0.0f : float(I(-4, 4)) * 0.25f);
        h.scale = P(0.8) ? 0 : I(1, 2);
        static const int healths[] = {100, 100, 100, 50, 1, 0, -5};
        h.health = healths[I(0, 6)];
        h.velocity = I(0, VELOCITY_COUNT - 1);
        h.vehicle = vehicles && P(0.3) ? I(0, vehicles - 1) : -1;
        static const int types[] = {HUMAN_CONTROLLER_SQUAD, HUMAN_CONTROLLER_SQUAD, HUMAN_CONTROLLER_SQUAD, HUMAN_CONTROLLER_PLAYER,
                                    HUMAN_CONTROLLER_AI, 0};
        h.controllerType = types[I(0, 5)];
        h.controller = P(0.9);
        c.humans.push_back(h);
    }
    for (int i = 0; i < POINTS; ++i) {
        S_FmPoint &p = c.points[i];
        p.x = float(I(-20, 20)) * 0.5f;
        p.y = float(I(-2, 2)) * 0.5f;
        p.z = float(I(-20, 20)) * 0.5f;
        static const int radii[] = {0, 8, 16, 32, 64, 255};
        p.radius = radii[I(0, 5)];
        static const int types[] = {0, 0, 0, 1, 2, 15, 15, 3};
        p.type = types[I(0, 7)];
        p.links = I(0, 3);
        for (int k = 0; k < p.links; ++k)
            p.targets[k] = uint16_t(I(0, POINTS - 1));
    }
    // (Point 0 mostly by a door's point: the doors' line-up.)
    if (P(0.8)) {
        c.points[1].type = I(1, 2);
        if (c.points[0].links == 0)
            c.points[0].links = 1;
        c.points[0].targets[I(0, c.points[0].links - 1)] = 1;
    }
    std::vector<int> doors;
    for (int i = 0; i < POINTS; ++i) {
        bool door = false;
        for (int k = 0; k < c.points[i].links; ++k) {
            const int t = c.points[c.points[i].targets[k]].type;
            door = door || t == 1 || t == 2;
        }
        if (door)
            doors.push_back(i);
    }
    const int count = P(0.1) ? I(40, 120) : I(6, 40);
    for (int i = 0; i < count; ++i) {
        S_FmOp op;
        const int r = I(0, 99);
        op.kind = r < 5 ? OP_SET_TYPE : r < 9 ? OP_SET_SCALE : r < 17 ? OP_ADD : r < 21 ? OP_REMOVE : r < 27 ? OP_LEADER
                : r < 30 ? OP_LEADER_DIED : r < 52 ? OP_TICK : r < 55 ? OP_FIND_SLOT : r < 59 ? OP_TARGET : r < 61 ? OP_DIR
                : r < 63 ? OP_SEAT : r < 65 ? OP_INVALIDATE : r < 68 ? OP_SET_TARGET : r < 72 ? OP_PLAN : r < 76 ? OP_DOOR
                : r < 78 ? OP_SCRIPTED : r < 90 ? OP_WORLD : r < 95 ? OP_SAVE_LOAD : OP_LOAD_MADE;
        op.a = I(0, 1000);
        op.b = I(0, 1000);
        op.c = I(0, 1000);
        op.d = I(0, 1000);
        if (op.kind == OP_DOOR)
            op.b = doors.empty() || op.b % 4 == 0 ? -1 : doors[size_t(op.b) % doors.size()];
        if (op.kind == OP_LOAD_MADE) {
            bool slotsSaved = false;
            op.data = MadeSave(c, doors, slotsSaved);
            op.c = slotsSaved;
        }
        c.ops.push_back(op);
    }
    // Mostly members added and a leader set first.
    if (P(0.85)) {
        std::vector<S_FmOp> first;
        for (int i = 0; i < humans; ++i)
            if (P(0.85)) {
                S_FmOp add;
                add.kind = OP_ADD;
                add.a = i;
                first.push_back(add);
            }
        S_FmOp lead;
        lead.kind = OP_LEADER;
        lead.a = I(0, 1000);
        first.push_back(lead);
        c.ops.insert(c.ops.begin(), first.begin(), first.end());
    }
    return c;
}

bool HasSlots() { return g_side ? At<uint32_t>(g_shape[1], 0x90) != 0 : PortShape()->m_slots != nullptr; }

using T_FmVoid = void(TC *)(void *);
using T_FmInt = void(TC *)(void *, int);
using T_FmHuman = bool(TC *)(void *, void *);
using T_FmChunk = bool(TC *)(void *, void *);
std::string RunFmOp(const S_FmCase &c, const S_FmOp &op, int index)
{
    const bool o = g_side != 0;
    uint8_t *b = g_shape[g_side];
    C_FormationShape *p = PortShape();
    // A human of the case's (one in eight none, one in eight another's: not in the shape).
    auto human = [&](int n) -> uint8_t * {
        if (n % 8 == 0)
            return nullptr;
        return HumanAt(n % (g_humanCount + 1));
    };
    switch (op.kind) {
    case OP_SET_TYPE: {
        // (11, 14 and 21: past the original's table, its initializers' rows.)
        static const int types[] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, FORMATION_NEXT, FORMATION_NEXT, FORMATION_NONE, FORMATION_NONE, 11, 14, 21};
        const int type = types[op.a % 18];
        if (o)
            OriginalAt<T_FmInt>(O_FM_SET_TYPE)(b, type);
        else
            p->SetType(E_FormationType(type));
        return F("SetType %d", type);
    }
    case OP_SET_SCALE: {
        // (Small ones: the slots near the leader, pushed aside.)
        static const S_vector scales[] = {{2.5f, 2.5f, 2.5f}, {1.0f, 1.0f, 1.0f}, {0.0f, 0.0f, 0.0f}, {3.0f, 1.0f, 0.5f},
                                          {-1.0f, 1.0f, 1.0f}, {1.5f, 2.0f, 0.75f}, {0.2f, 0.2f, 0.2f}, {0.4f, 1.0f, 0.3f}};
        const S_vector &s = scales[op.a % 8];
        if (o)
            OriginalAt<void(TC *)(void *, const S_vector *)>(O_FM_SET_SCALE)(b, &s);
        else
            p->SetScale(s);
        return "SetScale " + V(Ptr(&s), 3);
    }
    case OP_ADD: {
        // (Not without slots: the original and the port read them through null.)
        if (!HasSlots())
            return "AddMember (no slots)";
        uint8_t *h = HumanAt(op.a % std::max(g_humanCount, 1));
        if (!h)
            return "AddMember (no human)";
        const bool r = o ? OriginalAt<T_FmHuman>(O_FM_ADD)(b, h) : p->AddMember(reinterpret_cast<C_Human *>(h));
        return F("AddMember %s -> %d", NameOf(Ptr(h)).c_str(), int(r));
    }
    case OP_REMOVE: {
        if (!HasSlots())
            return "RemoveMember (no slots)";
        uint8_t *h = human(op.a);
        const bool r = o ? OriginalAt<T_FmHuman>(O_FM_REMOVE)(b, h) : p->RemoveMember(reinterpret_cast<C_Human *>(h));
        return F("RemoveMember %s -> %d", NameOf(Ptr(h)).c_str(), int(r));
    }
    case OP_LEADER: {
        uint8_t *h = human(op.a);
        if (h && !HasSlots())
            return "SetLeader (no slots)";
        const bool r = o ? OriginalAt<T_FmHuman>(O_FM_LEADER)(b, h) : p->SetLeader(reinterpret_cast<C_Human *>(h));
        return F("SetLeader %s -> %d", NameOf(Ptr(h)).c_str(), int(r));
    }
    case OP_LEADER_DIED:
        if (o)
            OriginalAt<T_FmVoid>(O_FM_LEADER_DIED)(b);
        else
            p->OnLeaderDied();
        return "OnLeaderDied";
    case OP_TICK: {
        static const int deltas[] = {0, 1, 16, 33, 100, 250, 1000, 5000};
        const int delta = deltas[op.a % 8];
        if (o)
            OriginalAt<T_FmInt>(O_FM_TICK)(b, delta);
        else
            p->Tick(delta);
        return F("Tick %d", delta);
    }
    case OP_FIND_SLOT: {
        if (!HasSlots())
            return "FindSlot (no slots)";
        uint8_t *h = human(op.a);
        int slot = -7;
        int *out = op.b & 1 ? &slot : nullptr;
        const bool r = o ? OriginalAt<bool(TC *)(void *, void *, int *)>(O_FM_FIND_SLOT)(b, h, out)
                         : p->FindSlot(reinterpret_cast<C_Human *>(h), out);
        return F("FindSlot %s -> %d %d", NameOf(Ptr(h)).c_str(), int(r), slot);
    }
    case OP_TARGET: {
        uint8_t *h = human(op.a);
        alignas(16) S_vector4 pos = {-9.0f, -9.0f, -9.0f, -9.0f};
        uint16_t checkpoint = 0xABCD;
        uint16_t *out = op.b & 1 ? &checkpoint : nullptr;
        const bool r = o ? OriginalAt<bool(TC *)(void *, void *, S_vector4 *, uint16_t *)>(O_FM_TARGET)(b, h, &pos, out)
                         : p->GetSlotTarget(reinterpret_cast<C_Human *>(h), &pos, out);
        // (Its w not: see ShapeState.)
        return F("GetSlotTarget %s -> %d ", NameOf(Ptr(h)).c_str(), int(r)) + V(Ptr(&pos), 3) + F(" %d", int(checkpoint));
    }
    case OP_DIR: {
        uint8_t *h = human(op.a);
        alignas(16) S_vector4 dir = {-9.0f, -9.0f, -9.0f, -9.0f};
        const bool r = o ? OriginalAt<bool(TC *)(void *, void *, S_vector4 *)>(O_FM_DIR)(b, h, &dir)
                         : p->GetSlotDir(reinterpret_cast<C_Human *>(h), &dir);
        return F("GetSlotDir %s -> %d ", NameOf(Ptr(h)).c_str(), int(r)) + V(Ptr(&dir), 3);
    }
    case OP_SEAT: {
        uint8_t *h = human(op.a);
        int vehicle = 12345;
        const int r = o ? OriginalAt<int(TC *)(void *, void *, int *)>(O_FM_SEAT)(b, h, &vehicle)
                        : p->GetSlotSeat(reinterpret_cast<C_Human *>(h), &vehicle);
        return F("GetSlotSeat %s -> %d %d", NameOf(Ptr(h)).c_str(), r, vehicle);
    }
    case OP_INVALIDATE: {
        uint8_t *h = human(op.a);
        if (o)
            OriginalAt<void(TC *)(void *, void *)>(O_FM_INVALIDATE)(b, h);
        else
            p->InvalidateSlot(reinterpret_cast<C_Human *>(h));
        return F("InvalidateSlot %s", NameOf(Ptr(h)).c_str());
    }
    case OP_SET_TARGET: {
        if (!HasSlots())
            return "SetSlotTarget (no slots)";
        uint8_t *h = human(op.a);
        alignas(16) const S_vector4 pos = {float(op.b % 40 - 20) * 0.5f, float(op.c % 5 - 2) * 0.5f, float(op.d % 40 - 20) * 0.5f,
                                           op.b & 1 ? 1.0f : 0.0f};
        if (o)
            OriginalAt<void(TC *)(void *, void *, const S_vector4 *)>(O_FM_SET_TARGET)(b, h, &pos);
        else
            p->SetSlotTarget(reinterpret_cast<C_Human *>(h), pos);
        return F("SetSlotTarget %s ", NameOf(Ptr(h)).c_str()) + V(Ptr(&pos));
    }
    case OP_PLAN: {
        if (!HasSlots())
            return "PlanSlot (no slots)";
        const int slot = op.a % 6;
        if (o)
            OriginalAt<T_FmInt>(O_FM_PLAN)(b, slot);
        else
            p->PlanSlot(slot);
        return F("PlanSlot %d", slot);
    }
    case OP_DOOR: {
        // (A point with a door's point among its links, or none: the original's line-up reads the door through null.)
        const int point = op.b < 0 ? 0xFFFF : op.b;
        if (o)
            OriginalAt<T_FmInt>(O_FM_DOOR)(b, point);
        else
            p->SetDoorPoint(uint16_t(point));
        return F("SetDoorPoint %d", point);
    }
    case OP_SCRIPTED: {
        const bool on = op.a & 1;
        if (o)
            b[0x8C] = on;
        else
            p->SetUnknown8C(on);
        return F("Scripted %d", int(on));
    }
    case OP_WORLD: {
        // A human moved, its velocity, health, vehicle or controller changed (alike on both sides).
        if (!g_humanCount)
            return "World (no humans)";
        const int i = op.a % g_humanCount;
        S_FmHuman h = c.humans[size_t(i)];
        switch (op.b % 10 < 8 ? op.b % 10 : 7) {
        case 6: {
            // A step from its first place (a leader then a little ahead of its slot).
            static const float steps[] = {0.3f, -0.3f, 0.6f, 0.0f, 0.05f};
            h.x += steps[op.c % 5];
            h.z += steps[op.d % 5];
            h.px = h.x;
            h.pz = h.z;
            PlaceHuman(i, h);
            return F("World human%d stepped", i);
        }
        case 7: {
            // Standing still at its slot's target (or near it), or everyone at theirs: the door's line-up goes on.
            const bool all = op.c % 2 == 0;
            std::string text = all ? "World all at their slots" : F("World human%d at its slot", i);
            const uint8_t *slots = g_side ? reinterpret_cast<const uint8_t *>(At<uint32_t>(g_shape[1], 0x90))
                                          : reinterpret_cast<const uint8_t *>(PortShape()->m_slots);
            for (int k = 0; slots && k < C_FormationShape::SLOT_COUNT; ++k) {
                const int h = HumanIndex(g_side ? At<uint32_t>(slots, 0xA0 * k + 0x70) : Ptr(PortShape()->m_slots[k].member));
                if (h < 0 || (!all && h != i))
                    continue;
                const S_vector4 &target = g_side ? *reinterpret_cast<const S_vector4 *>(slots + 0xA0 * k + 0x20) : PortShape()->m_slots[k].target;
                static const float offsets[] = {0.0f, 0.0f, 0.3f, 0.8f};
                // (Each slot's own offset and speed.)
                const S_vector pos(target.x + offsets[(op.d >> (2 * k)) & 3], target.y, target.z);
                uint8_t *a = g_human[g_side][h];
                if (g_side)
                    std::memcpy(a + 0x30, &pos, 12);
                else
                    reinterpret_cast<C_Actor *>(a)->SetPosition(pos);
                static const int speeds[] = {0, 0, 0, 7, 15, 0, 0, 0};
                SetVelocity(h, speeds[(op.d >> (12 + k)) & 7]);
                text += F(" %d", k);
            }
            return text;
        }
        case 0:
            h.x = float(op.c % 41 - 20) * 0.5f;
            h.z = float(op.d % 41 - 20) * 0.5f;
            h.heading = float(op.c % 628) * 0.01f;
            h.px = h.x;
            h.py = h.y;
            h.pz = h.z;
            PlaceHuman(i, h);
            return F("World human%d moved", i);
        case 1:
        case 2:
            SetVelocity(i, op.c);
            return F("World human%d velocity %d", i, op.c % VELOCITY_COUNT);
        case 3: {
            static const int healths[] = {100, 0, 1, -5};
            SetHealth(i, healths[op.c % 4]);
            return F("World human%d health %d", i, healths[op.c % 4]);
        }
        case 4: {
            const int v = g_vehicleCount && op.c % 3 ? op.c % g_vehicleCount : -1;
            SetVehicle(i, v);
            return F("World human%d vehicle %d", i, v);
        }
        default: {
            static const int types[] = {HUMAN_CONTROLLER_SQUAD, HUMAN_CONTROLLER_PLAYER, HUMAN_CONTROLLER_AI, 0};
            g_controllerType[i] = types[op.c % 4];
            return F("World human%d controller %d", i, g_controllerType[i]);
        }
        }
    }
    case OP_SAVE_LOAD: {
        const std::string file = F("fm_save%d.sav", index);
        C_chunk save;
        save.Open(file.c_str(), true);
        save.Begin(0x2000);
        const bool rs = o ? OriginalAt<T_FmChunk>(O_FM_SAVE)(b, &save) : p->SaveGame(save);
        // (A sibling after the shape: its loop ends at the shape's end.)
        if (op.b & 1) {
            const int value = 5;
            save.WriteChunk(0x5BF, &value, 4);
        }
        save.Ascend();
        save.Close();
        std::string bytes;
        FileText(file, bytes);
        bool rl = false;
        int depth = -1;
        if (op.c % 4) {
            C_chunk chunk;
            chunk.Open(file.c_str(), false);
            uint16_t id;
            chunk.Descend(id);
            rl = o ? OriginalAt<T_FmChunk>(O_FM_LOAD)(b, &chunk) : p->LoadGame(chunk);
            depth = chunk.m_depth;
            chunk.Close();
        }
        return F("SaveGame -> %d LoadGame -> %d depth %d bytes ", int(rs), int(rl), depth) + Hex(bytes);
    }
    default: {
        // (Not when the save has slots and the shape none: the original reads them into null, the port's fix makes them.)
        if (op.c && !HasSlots())
            return "LoadMade (no slots)";
        const std::string name = F("fm_made%d.sav", index);
        g_overrides[Lower(name)] = op.data;
        C_chunk chunk;
        chunk.Open(name.c_str(), false);
        uint16_t id;
        chunk.Descend(id);
        const bool r = o ? OriginalAt<T_FmChunk>(O_FM_LOAD)(b, &chunk) : p->LoadGame(chunk);
        const int depth = chunk.m_depth;
        chunk.Close();
        return F("LoadMade -> %d depth %d bytes ", int(r), depth) + Hex(op.data);
    }
    }
}

volatile DWORD g_caseStart;
DWORD WINAPI Watchdog(void *)
{
    for (;;) {
        Sleep(1000);
        const DWORD start = g_caseStart;
        if (start && GetTickCount() - start > 60000) {
            std::printf("fm_diff: HANG in %s (%s)\n", g_current.c_str(), g_case);
            std::fflush(stdout);
            ExitProcess(4);
        }
    }
}
int g_fmShow;
void RunFmCase(const S_FmCase &c)
{
    g_caseStart = GetTickCount();
    ++g_fmCases;
    g_holdFrees = true;
    g_current = F("case %d", g_fmCases);
    const uint32_t worldSeed = U32(), fakeBase = U32(), randSeed = U32();
    g_noPoints = c.noPoints;
    S_Result r;
    for (int side = 1; side >= 0; --side) {
        g_side = side;
        FreeWorld(side);
        BuildWorld(side, worldSeed);
        BuildFormationWorld(c);
        g_fakeBase = fakeBase;
        g_fakeCount = 0;
        g_nextHandle = 1;
        g_viewDirCount[side] = 0;
        std::srand(randSeed);
        g_log[side].clear();
        std::vector<std::string> &out = side ? r.orig : r.port;
        g_case = SIDE_NAMES[side];
        uint8_t *b;
        if (side) {
            b = Block(0xA0 + 16);
            OriginalAt<T_FmVoid>(O_FM_CTOR)(b);
        } else {
            b = Block(sizeof(C_FormationShape) + 16);
            new (b) C_FormationShape();
        }
        g_shape[side] = b;
        Name(b, "shape");
        for (const std::string &s : g_log[side])
            out.push_back("call " + s);
        g_log[side].clear();
        for (const std::string &s : ShapeState())
            out.push_back(s);
        static const int trace = std::getenv("HD2_FM_TRACE") ? std::atoi(std::getenv("HD2_FM_TRACE")) : -1;
        for (size_t i = 0; i < c.ops.size(); ++i) {
            ++g_opCounts[c.ops[i].kind];
            if (trace == g_fmCases)
                std::printf("trace %s op %d kind %s a %d b %d c %d d %d\n", SIDE_NAMES[side], int(i), OP_KIND_NAMES[c.ops[i].kind],
                            c.ops[i].a, c.ops[i].b, c.ops[i].c, c.ops[i].d);
            out.push_back(RunFmOp(c, c.ops[i], int(i)));
            for (const std::string &s : g_log[side])
                out.push_back("  call " + s);
            g_log[side].clear();
            for (const std::string &s : ShapeState())
                out.push_back("  " + s);
        }
        // The end: destroyed.
        if (side)
            OriginalAt<T_FmVoid>(O_FM_DTOR)(b);
        else
            PortShape()->~C_FormationShape();
        g_case = "";
        for (const std::string &s : g_log[side])
            out.push_back("call " + s);
        g_shape[side] = nullptr;
    }
    static const int only = std::getenv("HD2_FM_ONLY") ? std::atoi(std::getenv("HD2_FM_ONLY")) : 0;
    if (g_fmShow && (!only || only == g_fmCases)) {
        std::printf("%s\n", g_current.c_str());
        for (const std::string &s : r.port)
            std::printf("  %s\n", s.c_str());
        if (g_fmShow == 2)
            for (const std::string &s : r.orig)
                std::printf("  orig: %s\n", s.c_str());
    }
    Compare(g_current, r);
    ReleaseHeldFrees();
    if (!g_overrun.empty()) {
        ++failures;
        std::printf("FAIL %s: the original wrote past a block\n", g_overrun.c_str());
        g_overrun.clear();
    }
    FreeWorld(0);
    FreeWorld(1);
}

void SetUpFormation()
{
    SetUpCmd();
    // The virtual calls the shapes make: the vehicles' seats, a squad controller's combat order.
    g_virtuals.push_back({"C_Actor::GetSeatOccupant", 0x78, PortSlot(&C_Actor::GetSeatOccupant), "i", 'p'});
    g_virtuals.push_back({"C_Actor::GetSeatCount", 0x88, PortSlot(&C_Actor::GetSeatCount), "", 'i'});
    g_virtuals.push_back({"C_SquadController::GetCombatOrder", 0x148, PortSlot(&C_SquadController::GetCombatOrder), "", 'i'});
    for (const S_Virtual &v : g_virtuals)
        if (v.orig >= 0x180 || v.port >= 0x180) {
            std::printf("fm_diff: %s past the test's vtables (0x%X, 0x%X)\n", v.name, v.orig, v.port);
            std::exit(2);
        }
    SetUpFormationAnswers();
    const struct {
        uint32_t at;
        const void *to;
    } redirects[] = {
        {0x007e53aa, reinterpret_cast<const void *>(&O_dtaOpenWrite)},
        {0x007e5398, reinterpret_cast<const void *>(&O_dtaSeek)},
        {0x007e5392, reinterpret_cast<const void *>(&O_dtaWrite)},
        {0x007e562e, reinterpret_cast<const void *>(&O_Atexit)},
        {0x007e53c2, reinterpret_cast<const void *>(&O_AlignedMalloc)},
        {0x007e559c, reinterpret_cast<const void *>(&O_AlignedFree)},
        {0x007e540a, reinterpret_cast<const void *>(&O_AlignedOffsetMalloc)},
    };
    for (const auto &x : redirects)
        RedirectOriginal(x.at, x.to);
    // The original's table of the types' slots: its static initializers (one a type, 0x00596160..0x00597060, then
    // 0x005971e0 copying them into the table SetType reads, 0x008ab2e0), which its copy here never ran.
    for (uint32_t at = 0x00596160; at <= 0x00597060; at += 0x180)
        OriginalAt<void (*)()>(at)();
    OriginalAt<void (*)()>(0x005971e0)();
    // HD2_FM_TABLE: the original's rows past its table (types 11..21) matched to the port's table's types.
    if (std::getenv("HD2_FM_TABLE"))
        for (int t = 0; t <= 21; ++t) {
            const float *o = OriginalAt<const float *>(0x008ab2e0 + 0xC0 * uint32_t(t));
            std::string match = "none";
            for (int k = 0; k <= 10; ++k) {
                bool same = true;
                for (int s = 0; s < 6 && same; ++s)
                    for (int c = 0; c < 3; ++c)
                        same = same && o[4 * s + c] == FORMATION_TABLE[k][s][c] && o[24 + 4 * s + c] == FORMATION_TABLE[k][s][3 + c];
                if (same)
                    match = F("%d", k);
            }
            std::printf("fm_diff: the original's type %d: the port's %s\n", t, match.c_str());
        }
}

}  // namespace H

int main()
{
    using namespace H;
    std::puts("fm_diff: main");
    std::fflush(stdout);
    SetUnhandledExceptionFilter(OnCrash);
    if (std::getenv("HD2_SC_FIRST_CHANCE"))
        AddVectoredExceptionHandler(1, FirstChance);
    int exitCode = 0;
    if (RunReservingOriginalBase(exitCode))
        return exitCode;
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    if (!LoadOriginalExe()) {
        std::printf("fm_diff: no original executable (HD2_ORIGINAL_EXE): nothing to compare with\n");
        return 1;
    }
    _controlfp(0x00020000u, 0x00030000u);                       // _PC_24, _MCW_PC: the game's precision
    if (const char *s = std::getenv("HD2_FM_SEED"))
        g_seed = uint32_t(std::strtoul(s, nullptr, 10)) | 1;
    if (const char *s = std::getenv("HD2_FM_SHOW"))
        g_fmShow = std::atoi(s) ? std::atoi(s) : 1;
    int cases = std::getenv("HD2_FM_QUICK") ? 300 : 1500;
    if (const char *s = std::getenv("HD2_FM_CASES"))
        cases = std::atoi(s);
    new (&reinterpret_cast<C_game *>(g_portGameBuf)->m_missionName) C_str();
    SetUp();
    SetUpFormation();
    SetMission("AFRICA1");
    CreateThread(nullptr, 0, Watchdog, nullptr, 0, nullptr);
    for (int n = 0; n < cases; ++n)
        RunFmCase(RandomFmCase());
    std::string ops;
    for (int i = 0; i < OP_KINDS; ++i)
        ops += F("%s%s %d", i ? ", " : "", OP_KIND_NAMES[i], g_opCounts[i]);
    std::printf("fm_diff: %d cases (%s), %d calls logged, %d skipped (overruns), %d checks, %d failures\n", g_fmCases,
                ops.c_str(), g_calls[0], g_overruns, checks, failures);
    return failures != 0;
}
