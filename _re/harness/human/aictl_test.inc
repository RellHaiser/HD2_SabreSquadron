// Part 33 of the human harness (HUMAN_AICTL builds, build_aictl.bat; included by human_test.cpp after
// controller_test.inc): C_AIController, the AI controllers' base (game/ai_controller.cpp), against the original's, on
// part 23's controllers, human and fakes, with game/ai_parts.cpp linked (part 32's parts, real on both sides).
//
// Each side has its AI controller: the port's (a test class over C_AIController, its one pure virtual logged) and the
// original's made by its own constructor (0x0056ee90: its parts too). Before each call the port's is copied into the
// original's layout: the base's fields by part 23's table, the AI's by layout_offsets.py's (S_AIFire's throwPos moved),
// the waypoints into an MSVC6 vector of the original pool's waypoints, the task into one of the original's pool, the
// parts' contents into the original's own parts; after the call the two are compared the same way, with the messages
// each queued. The net object is a block per side: the port's out-of-line setters write it at the original's offsets,
// as the original's inline ones do. The AI's other virtuals (the senses, the combat: other files) are logged stubs.
//
// Part 33a: the life cycle, the small virtuals, the body flags, the saved games.
#include "common/chunk.h"
#include "game/ai_parts.h"
#include "game/controller_net.h"
#include "net/bitstream.h"

// A stub that should not be reached.
[[noreturn]] void AiUnexpected(const char *what)
{
    std::printf("unexpected: %s\n", what);
    std::exit(4);
}

S_NetPlayer *g_netPlayerTable = nullptr;                // (net/net_game.cpp's, not linked)
int g_netPlayerCount = 0;

// ---- the AI's virtuals in other files (ai_senses.cpp, ai_combat.cpp, ai_movement.cpp): logged
#ifndef HUMAN_AICOMBAT   // (the combat build links game/ai_combat.cpp's)
bool C_AIController::AdjustAim(S_vector *) { Log(this, "AI.AdjustAim"); return false; }
#endif
bool C_AIController::AdjustToPathHeight(S_vector *) { Log(this, "AI.AdjustToPathHeight"); return false; }
#ifndef HUMAN_AICOMBAT
int C_AIController::ApplyAlertState() { Log(this, "ApplyAlertState"); return 0; }
#endif
bool C_AIController::Unknown5C(float *, float *) { Log(this, "AI.Unknown5C"); return false; }
#ifndef HUMAN_AISENSES   // (the senses build links game/ai_senses.cpp's)
void C_AIController::OnBodyHeard(C_Actor *) { Log(this, "AI.OnBodyHeard"); }
void C_AIController::OnBodySeen(C_Human *) { Log(this, "AI.OnBodySeen"); }
void C_AIController::OnEnemyLost(C_Human *) { Log(this, "AI.OnEnemyLost"); }
void C_AIController::OnEnemySeen(C_Human *) { Log(this, "AI.OnEnemySeen"); }
void C_AIController::OnFriendHeard(C_Actor *) { Log(this, "AI.OnFriendHeard"); }
void C_AIController::OnHit(int, int, int, C_Actor *, const S_vector &, const S_vector &) { Log(this, "AI.OnHit"); }
void C_AIController::OnKilledEnemy(C_Human *) { Log(this, "AI.OnKilledEnemy"); }
void C_AIController::OnLoudSound(C_Actor *) { Log(this, "AI.OnLoudSound"); }
void C_AIController::OnShotAt(C_Actor *, const S_vector &) { Log(this, "AI.OnShotAt"); }
void C_AIController::OnSoundHeard(S_RememberedSound *) { Log(this, "AI.OnSoundHeard"); }
void C_AIController::TestSightOfHuman(C_Human *, int *, float *) { Log(this, "AI.TestSightOfHuman"); }
void C_AIController::TestSightOfMeleeTracker(C_Actor *, int *, float *) { Log(this, "AI.TestSightOfMeleeTracker"); }
#endif

// ---- the controller's network callbacks (controller_net.cpp, not linked: only their addresses are compared)
#ifndef HUMAN_PLAYERNET   // (part 42 links player_net.cpp)
C_NetObjectMessage *__fastcall NetCreateMessage(void *, C_BitStream *, bool) { AiUnexpected(__PRETTY_FUNCTION__); }
void __fastcall NetReadControlBits(void *, C_BitStream *) { AiUnexpected(__PRETTY_FUNCTION__); }
void __fastcall NetWriteControlBits(void *, C_BitStream *) { AiUnexpected(__PRETTY_FUNCTION__); }
#endif

namespace {
#define AC __attribute__((thiscall))
struct S_AiScript {
    std::vector<uint16_t> ladderPoints;                 // GetActorPoints' answer
    int climbTarget = 0;                                // GetClimbTarget's
    int localId = 3;                                    // the replication's local id
    bool lineOfFire = false;                            // Unknown114's (part 36b: the original's slot too)
} g_ai;
// The net object's contents after each side's call (the original's offsets: send mask +0x38, live +0x3C, forwarding
// +0x40, the state handlers +0x54 / +0x58 and their context +0x5C, the factory's context +0x60 and the factory +0x64).
alignas(16) uint8_t g_aiNetPort[0x100], g_aiNetOrig[0x100];
}  // namespace

void C_NetObject::SetStateHandlers(NetStateHandler read, NetStateHandler write, void *context)
{
    auto *b = reinterpret_cast<uint8_t *>(this);
    std::memcpy(b + 0x54, &read, 4);
    std::memcpy(b + 0x58, &write, 4);
    std::memcpy(b + 0x5C, &context, 4);
}
void C_NetObject::SetMessageFactory(NetMessageFactory factory, void *context)
{
    auto *b = reinterpret_cast<uint8_t *>(this);
    std::memcpy(b + 0x64, &factory, 4);
    std::memcpy(b + 0x60, &context, 4);
}
void C_NetObject::SetSendMask(int mask) { std::memcpy(reinterpret_cast<uint8_t *>(this) + 0x38, &mask, 4); }
void C_NetObject::SetLive(int value) { std::memcpy(reinterpret_cast<uint8_t *>(this) + 0x3C, &value, 4); }
void C_NetObject::SetForwarding(bool value) { reinterpret_cast<uint8_t *>(this)[0x40] = value; }
// The net object's queued messages (the same layout on both sides: the type +0x1C, from a client +0x2C; the action's
// direction +0x30, position +0x40, shot interval +0x50, strength +0x54, ammunition +0x58, rounds +0x5C, action +0x60),
// logged and freed (each side's _aligned_malloc'd block).
// (`onAt`: the binoculars message's on flag: +0x30 in the original and an MSVC build, the base's tail padding +0x2D in
// this g++ build.)
void LogNetMessage(const void *object, const uint8_t *m, int sentTo, bool outgoing, size_t onAt)
{
    int type;
    std::memcpy(&type, m + 0x1C, 4);
    std::vector<double> nums = {double(type), double(sentTo), double(outgoing)};
    if (type == NET_MSG_ACTION) {
        float f[9];
        std::memcpy(f, m + 0x30, 12);
        std::memcpy(f + 3, m + 0x40, 12);
        std::memcpy(f + 6, m + 0x54, 4);
        int ints[3];
        std::memcpy(ints, m + 0x50, 4);
        std::memcpy(ints + 1, m + 0x58, 4);
        std::memcpy(ints + 2, m + 0x60, 4);
        for (int k = 0; k < 7; ++k)
            nums.push_back(f[k]);
        nums.insert(nums.end(), {double(ints[0]), double(ints[1]), double(ints[2]), double(m[0x5C]), double(m[0x2C])});
    } else if (type == NET_MSG_BINOCULARS) {
        // (Part 40c: on, fromClient +0x2C.)
        nums.insert(nums.end(), {double(m[onAt]), double(m[0x2C])});
    } else if (type == NET_MSG_DIVE) {
        nums.push_back(m[0x2C]);   // (part 42: fromClient)
    }
    S_Call c{NameOf(object) + ".QueueMessage", nums};
    g_calls.push_back(c);
}
// (Part 42: the messages its test hands out and frees itself, queued again by ProcessNetMessages, kept.)
bool (*g_aiTestMessage)(const void *m) = nullptr;
void C_NetObject::QueueMessage(C_NetObjectMessage *m, int sentTo, bool outgoing)
{
    LogNetMessage(this, reinterpret_cast<const uint8_t *>(m), sentTo, outgoing, offsetof(C_NetBinocularsMessage, on));
    if (g_aiTestMessage && g_aiTestMessage(m))
        return;
    // (The binoculars' message has no 16-aligned member: the plain new's; the action's and the dive's the aligned one.)
    if (m->GetType() == 2)
        ::operator delete(m);
    else
        _aligned_free(m);
}
int C_NetReplication::GetLocalId() const { return g_ai.localId; }
C_NetObjectMessage::C_NetObjectMessage(int t) : type(t) {}
// (Raw storage under the vtables' names: these classes' key functions are in net code not linked; part 33b. Part 42
// defines their key functions, so that its linked messages' vtables have their bases'.)
#ifndef HUMAN_PLAYERNET
asm(".globl __ZTV14C_NetQuantized\n\t.data\n\t.balign 4\n__ZTV14C_NetQuantized:\n\t.space 64\n"
    ".globl __ZTV18C_NetObjectMessage\n__ZTV18C_NetObjectMessage:\n\t.space 64\n\t.text");
#endif
#ifndef HUMAN_PLAYERNET   // (part 42 links player_net.cpp: their key functions)
asm(".globl __ZTV18C_NetActionMessage\n\t.data\n\t.balign 4\n__ZTV18C_NetActionMessage:\n\t.space 64\n"
    ".globl __ZTV16C_NetDiveMessage\n__ZTV16C_NetDiveMessage:\n\t.space 64\n\t.text");
#endif

// The road network's paths (the driver's planner: part 32's) and the actors' walking points.
bool C_CheckpointNetwork::FindPath(int, int, std::vector<uint16_t> *, uint32_t) { AiUnexpected(__PRETTY_FUNCTION__); }
void C_CheckpointNetwork::GetActorPoints(const C_Actor *actor, std::vector<uint16_t> &out) const
{
    Log(actor, "GetActorPoints");
    out = g_ai.ladderPoints;
}
void C_Human::ClientDropItem(int actor, int uid, const S_vector &v) { Log(this, "ClientDropItem", {double(actor), double(uid), v.x, v.y, v.z}); }
bool C_Vehicle::SetUnknown4f2630(float value)
{
    Log(this, "SetUnknown4f2630", {value});
    return true;
}
// The body's movement flags and climb target.
void S_HumanBody::ClearMovementFlags(uint32_t flags) { Log(this, "BodyClearMovementFlags", {double(flags)}); }
void S_HumanBody::SetMovementFlags(uint32_t flags) { Log(this, "BodySetMovementFlags", {double(flags)}); }
int S_HumanBody::GetClimbTarget() const
{
    Log(this, "BodyGetClimbTarget");
    return g_ai.climbTarget;
}

namespace {
// ---- the original's side
void AC OA_ClearMovementFlags(void *self, uint32_t flags) { Log(self, "BodyClearMovementFlags", {double(flags)}); }
void AC OA_SetMovementFlags(void *self, uint32_t flags) { Log(self, "BodySetMovementFlags", {double(flags)}); }
int AC OA_GetClimbTarget(void *self)
{
    Log(self, "BodyGetClimbTarget");
    return static_cast<int>(reinterpret_cast<intptr_t>(ToOrig(reinterpret_cast<void *>(static_cast<intptr_t>(g_ai.climbTarget)))));
}
// (Its vector: allocator, first, last, end; with the original's new.)
void AC OA_GetActorPoints(void *, const void *actor, uint32_t *out)
{
    Log(actor, "GetActorPoints");
    const size_t n = g_ai.ladderPoints.size();
    if (!n)
        return;
    auto *data = static_cast<uint16_t *>(std::malloc(n * 2));
    std::memcpy(data, g_ai.ladderPoints.data(), n * 2);
    out[1] = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(data));
    out[2] = out[3] = out[1] + 2 * static_cast<uint32_t>(n);
}
int AC OA_ApplyAlertState(void *self, int)
{
    Log(self, "ApplyAlertState");
    return 0;
}
void AC OA_QueueMessage(void *self, uint8_t *m, int sentTo, int outgoing)
{
    LogNetMessage(self, m, sentTo, (outgoing & 0xFF) != 0, 0x30);
    if (g_aiTestMessage && g_aiTestMessage(m))
        return;
    _aligned_free(m);
}
alignas(16) uint8_t g_aiReplication[0x100];
// (Frees not logged in this build: the original's driver frees its path's old storage as it grows it, the port's vector
// silently; the queued messages are compared as queues.)
void AO_AlignedFree(void *p)
{
    if (p)
        _aligned_free(p);
}
void AC AO_MessageDelete(void *) {}             // the original's g_netReplication (its local id +0xDC)
uint32_t AC OA_SetUnknown4f2630(void *self, float value)
{
    Log(self, "SetUnknown4f2630", {value});
    return 1;
}

// ---- the controllers
void LogCommand(const void *self, const S_HumanCommand &c)
{
    Log(self, "ExecuteCommand", {double(c.category), double(c.type), double(c.param1), double(c.param2)});
}
// (ExecuteCommand: part 23's, logged here on both sides: the original's through a copy of its vtable.)
uint32_t AC OA_ExecuteCommand(void *self, const S_HumanCommand *c)
{
    LogCommand(self, *c);
    return 1;
}
void *g_oaVt[0x48];
alignas(16) uint8_t g_aiPoints[8][sizeof(S_Checkpoint)];
struct S_TestAi : C_AIController {
    using C_AIController::C_AIController;
    bool ExecuteCommand(const S_HumanCommand &c) override
    {
        LogCommand(this, c);
        return true;
    }
    bool Unknown114(const S_vector *dir) override
    {
        Log(this, "Unknown114", {dir->x, dir->y, dir->z});
        return g_ai.lineOfFire;
    }
};
S_TestAi *g_pa;
uint8_t *g_oa;                                          // (0x138 bytes)
const S_LayoutField kAiFields[] = {
    // (Generated by _re/tools/layout_offsets.py from human_controller.h; m_waypoints apart.)
    S_LayoutField{0xC0, 176, 48, "m_fire", 'm'},
    S_LayoutField{0xF0, 224, 4, "m_task", 'p'},
    S_LayoutField{0x104, 240, 4, "m_waypoint", 'p'},
    S_LayoutField{0x108, 244, 4, "m_lookAroundTime", 'i'},
    S_LayoutField{0x10C, 248, 4, "m_unknown10C", 'i'},
    S_LayoutField{0x110, 252, 4, "m_unknown110", 'i'},
    S_LayoutField{0x114, 256, 4, "m_unknown114", 'i'},
    S_LayoutField{0x118, 260, 4, "m_unknown118", 'i'},
    S_LayoutField{0x11C, 264, 1, "m_ignoreCircumvention", 'i'},
    S_LayoutField{0x120, 268, 4, "m_unknown120", 'i'},
    S_LayoutField{0x124, 272, 4, "m_facing", 'p'},
    S_LayoutField{0x128, 276, 4, "m_fireOrder", 'p'},
    S_LayoutField{0x12C, 280, 4, "m_move", 'p'},
    S_LayoutField{0x130, 284, 4, "m_driver", 'p'},
    S_LayoutField{0x134, 288, 4, "m_alert", 'p'},
};

// The original's pools (0x008ab158; part 32): its waypoints and tasks.
void *AiPools() { return OriginalAt<void *>(0x008ab158); }
uint8_t *OrigWaypoint()
{
    typedef void *(AC * T_Raw)(void *);
    return static_cast<uint8_t *>(OriginalAt<T_Raw>(0x00582700)(AiPools()));
}
uint8_t *OrigTask(int type)
{
    typedef void *(AC * T_Alloc)(void *, int);
    return static_cast<uint8_t *>(OriginalAt<T_Alloc>(0x00582df0)(AiPools(), type));
}
// A task's fields in each layout (part 32's: the derived fields +0x14 in the port, +0x20 in the original).
struct S_AiRun {
    size_t port, orig, size;
};
std::vector<S_AiRun> TaskRuns(int type)
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
uint32_t OrigWord(uint32_t w) { return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(ToOrig(reinterpret_cast<void *>(w)))); }
void CopyMapped(const uint8_t *from, uint8_t *to, size_t size)
{
    for (size_t i = 0; i < size; i += 4) {
        uint32_t w = 0;
        const size_t n = std::min<size_t>(4, size - i);
        std::memcpy(&w, from + i, n);
        if (n == 4)
            w = OrigWord(w);
        std::memcpy(to + i, &w, n);
    }
}
// The first word of `size` bytes that differs (the port's mapped), or -1; `skip`: offsets left out.
int FirstDiff(const uint8_t *port, const uint8_t *orig, size_t size, std::initializer_list<size_t> skip = {})
{
    for (size_t i = 0; i < size; i += 4) {
        if (std::find(skip.begin(), skip.end(), i) != skip.end())
            continue;
        uint32_t a = 0, b = 0;
        const size_t n = std::min<size_t>(4, size - i);
        std::memcpy(&a, port + i, n);
        std::memcpy(&b, orig + i, n);
        if (n == 4)
            a = OrigWord(a);
        if (a != b)
            return int(i);
    }
    return -1;
}

// The driver: its fields by part 32's table, its path an MSVC6 vector of 16-byte aligned storage.
const S_LayoutField kAiDriverFields[] = {
    S_LayoutField{0x10, 12, 2, "m_pathIndex", 'i'},
    S_LayoutField{0x14, 16, 4, "m_progress", 'f'},
    S_LayoutField{0x18, 20, 4, "m_nearestDist2", 'f'},
    S_LayoutField{0x1C, 24, 1, "m_reversing", 'i'},
    S_LayoutField{0x1D, 25, 1, "m_moving", 'i'},
    S_LayoutField{0x20, 28, 4, "m_stuckTime", 'i'},
    S_LayoutField{0x24, 32, 4, "m_noProgressTime", 'i'},
    S_LayoutField{0x30, 36, 16, "m_lastPos", 'f'},
    S_LayoutField{0x40, 52, 4, "m_flags", 'i'},
    S_LayoutField{0x50, 56, 16, "m_target", 'f'},
    S_LayoutField{0x60, 72, 16, "m_targetDir", 'f'},
    S_LayoutField{0x70, 88, 4, "m_roads", 'p'},
    S_LayoutField{0x74, 92, 4, "m_vehicle", 'p'},
    S_LayoutField{0x88, 108, 1, "m_unknown88", 'i'},
    S_LayoutField{0x8C, 112, 4, "m_speed", 'f'},
    S_LayoutField{0x90, 116, 4, "m_pace", 'i'},
    S_LayoutField{0x94, 120, 4, "m_label", 'i'},
};
void DriverToOrigAi(const C_AIDriver &d, uint8_t *o)
{
    const auto *p = reinterpret_cast<const uint8_t *>(&d);
    for (const S_LayoutField &f : kAiDriverFields)
        if (std::strcmp(f.name, "m_roads") != 0)
            CopyMapped(p + f.port, o + f.orig, f.size);
    uint32_t first;
    std::memcpy(&first, o + 4, 4);
    if (first)
        _aligned_free(reinterpret_cast<void *>(static_cast<uintptr_t>(first)));
    uint32_t v[4] = {0, 0, 0, 0};
    if (!d.m_path.empty()) {
        const size_t bytes = d.m_path.size() * sizeof(S_AIPathPoint);
        void *data = _aligned_malloc(bytes, 16);
        std::memcpy(data, d.m_path.data(), bytes);
        v[1] = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(data));
        v[2] = v[3] = v[1] + static_cast<uint32_t>(bytes);
    }
    std::memcpy(o, v, 16);
}
std::string DriverDiffers(const C_AIDriver &d, const uint8_t *o)
{
    const auto *p = reinterpret_cast<const uint8_t *>(&d);
    for (const S_LayoutField &f : kAiDriverFields) {
        if (std::strcmp(f.name, "m_roads") == 0)
            continue;
        const int at = FirstDiff(p + f.port, o + f.orig, f.size, {f.size == 16 ? size_t(12) : size_t(99)});
        if (at >= 0)
            return std::string("driver.") + f.name + "+" + std::to_string(at);
    }
    uint32_t v[4];
    std::memcpy(v, o, 16);
    const size_t count = v[1] ? (v[2] - v[1]) / sizeof(S_AIPathPoint) : 0;
    if (count != d.m_path.size())
        return "driver path size";
    for (size_t k = 0; k < count; ++k) {
        const auto *op = reinterpret_cast<const uint8_t *>(static_cast<uintptr_t>(v[1])) + k * sizeof(S_AIPathPoint);
        const auto *pp = reinterpret_cast<const uint8_t *>(&d.m_path[k]);
        if (std::memcmp(op, pp, 12) != 0 || std::memcmp(op + 0x10, pp + 0x10, 12) != 0 || op[0x20] != pp[0x20])
            return "driver path[" + std::to_string(k) + "]";
    }
    return "";
}

// The port's AI fields into the original's (the base's by part 23's ControllerToOrig).
void AiToOrig(S_TestAi *pa, uint8_t *oa)
{
    ControllerToOrig(pa, oa);
    const auto *pb = reinterpret_cast<const uint8_t *>(pa);
    // S_AIFire (+0xC0; its throwPos +0x1C in the port, +0x20 in the original).
    CopyMapped(pb + offsetof(C_AIController, m_fire), oa + 0xC0, 0x1C);
    std::memcpy(oa + 0xE0, pb + offsetof(C_AIController, m_fire) + 0x1C, 16);
    for (const S_LayoutField &f : kAiFields)
        if (f.kind != 'p' && f.kind != 'm')
            std::memcpy(oa + f.orig, pb + f.port, f.size);
    // The task: one of the original's pool, the same type and fields.
    uint8_t *ot = nullptr;
    if (pa->m_task) {
        ot = OrigTask(pa->m_task->type);
        for (const S_AiRun &r : TaskRuns(pa->m_task->type))
            CopyMapped(reinterpret_cast<const uint8_t *>(pa->m_task) + r.port, ot + r.orig, r.size);
    }
    std::memcpy(oa + 0xF0, &ot, 4);
    // The waypoints (allocator, first, last, end; the original's new) and the last one passed.
    uint32_t v[4] = {0, 0, 0, 0};
    if (!pa->m_waypoints.empty()) {
        auto **data = static_cast<uint8_t **>(std::malloc(pa->m_waypoints.size() * 4));
        for (size_t k = 0; k < pa->m_waypoints.size(); ++k) {
            data[k] = OrigWaypoint();
            std::memcpy(data[k], pa->m_waypoints[k], sizeof(S_AIWaypoint));
        }
        v[1] = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(data));
        v[2] = v[3] = v[1] + 4 * static_cast<uint32_t>(pa->m_waypoints.size());
    }
    std::memcpy(oa + 0xF4, v, 16);
    uint8_t *ow = nullptr;
    if (pa->m_waypoint) {
        ow = OrigWaypoint();
        std::memcpy(ow, pa->m_waypoint, sizeof(S_AIWaypoint));
    }
    std::memcpy(oa + 0x104, &ow, 4);
    // The parts' contents into the original's own.
    auto part = [&](size_t at) {
        uint8_t *p;
        std::memcpy(&p, oa + at, 4);
        return p;
    };
    CopyMapped(reinterpret_cast<const uint8_t *>(pa->m_facing), part(0x124), 0x60);
    CopyMapped(reinterpret_cast<const uint8_t *>(pa->m_fireOrder), part(0x128), 0x50);
    CopyMapped(reinterpret_cast<const uint8_t *>(pa->m_move), part(0x12C), 0x60);
    std::memcpy(part(0x134), pa->m_alert, 8);
    DriverToOrigAi(*pa->m_driver, part(0x130));
}

// The messages to the clients part 23's describer does not know: the equipment's human, the command's fields (the
// port's class by its vtable, the original's by 0x00810ce4 / 0x00810c6c).
// Another message: its type (+4 on both sides) and the fields the fakes' Write sends (part 33b's table), whole words.
struct S_AiMsgField;
std::vector<S_AiMsgField> AiMsgFields(int type);
std::string AiMsgText(const uint8_t *m, bool orig);
std::string AiMessages(const std::vector<S_ControllerMessage *> &port, const uint8_t *vec)
{
    static const S_MsgEquipment equipment;
    static const S_MsgCommand command;
    std::string p, o;
    for (const S_ControllerMessage *m : port) {
        if (*reinterpret_cast<void *const *>(m) == *reinterpret_cast<void *const *>(&equipment))
            p += "Equipment[" + std::to_string(static_cast<const S_MsgEquipment *>(m)->human) + "]";
        else if (*reinterpret_cast<void *const *>(m) == *reinterpret_cast<void *const *>(&command)) {
            const auto *c = static_cast<const S_MsgCommand *>(m);
            p += "Command[" + std::to_string(c->command) + " " + std::to_string(c->actor) + " " + std::to_string(c->arg) + "]";
        } else
            p += AiMsgText(reinterpret_cast<const uint8_t *>(m), false);
    }
    void *const *first = *reinterpret_cast<void *const *const *>(vec + 4);
    void *const *last = *reinterpret_cast<void *const *const *>(vec + 8);
    for (void *const *q = first; q && q != last; ++q) {
        const auto *b = static_cast<const uint8_t *>(*q);
        const uint32_t vt = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(*reinterpret_cast<void *const *>(b)));
        int w[3];
        std::memcpy(w, b + 8, 12);
        if (vt == 0x00810ce4)
            o += "Equipment[" + std::to_string(w[0]) + "]";
        else if (vt == 0x00810c6c)
            o += "Command[" + std::to_string(w[0]) + " " + std::to_string(w[1]) + " " + std::to_string(w[2]) + "]";
        else
            o += AiMsgText(b, true);
    }
    return p == o ? "" : "to the clients: port " + p + ", original " + o;
}

std::string AiDiffers(S_TestAi *pa, const uint8_t *oa)
{
    std::string why = ControllerDiffers(pa, oa);
    if (why.empty())
        why = AiMessages(pa->m_toClients, oa + 0x90);
    if (!why.empty())
        return why;
    const auto *pb = reinterpret_cast<const uint8_t *>(pa);
    const uint8_t *pf = pb + offsetof(C_AIController, m_fire);
    // (S_AIFire's padding after its bool.)
    int at = FirstDiff(pf, oa + 0xC0, 0x1C, {0});
    if (at < 0 && (pf[0] != oa[0xC0]))
        at = 0;
    if (at < 0 && std::memcmp(pf + 0x1C, oa + 0xE0, 12) != 0)
        at = 0x1C;
    if (at >= 0) {
        if (at == 0x1C) {
            float a[3], b[3];
            std::memcpy(a, pf + 0x1C, 12);
            std::memcpy(b, oa + 0xE0, 12);
            char words[160];
            std::snprintf(words, sizeof words, " (throwPos port %.9g %.9g %.9g original %.9g %.9g %.9g)", a[0], a[1], a[2], b[0], b[1], b[2]);
            return "fire+" + std::to_string(at) + words;
        }
        return "fire+" + std::to_string(at);
    }
    for (const S_LayoutField &f : kAiFields) {
        if (f.kind == 'p' || f.kind == 'm')
            continue;
        if (std::memcmp(oa + f.orig, pb + f.port, f.size) != 0)
            return std::string("ai.") + f.name;
    }
    // The task.
    uint8_t *ot;
    std::memcpy(&ot, oa + 0xF0, 4);
    if ((pa->m_task != nullptr) != (ot != nullptr))
        return "task (null on one side)";
    if (pa->m_task) {
        uint32_t otype;
        std::memcpy(&otype, ot + 4, 4);
        if (int(otype) != pa->m_task->type)
            return "task type";
        for (const S_AiRun &r : TaskRuns(pa->m_task->type)) {
            // (The vectors' w, +0x2C and +0x3C: saved games keep 12 bytes, a loaded task keeps its pool object's.)
            const int at = FirstDiff(reinterpret_cast<const uint8_t *>(pa->m_task) + r.port, ot + r.orig, r.size,
                                     {0x2C - r.orig, 0x3C - r.orig});
            if (at >= 0) {
                uint32_t a = 0, b = 0;
                std::memcpy(&a, reinterpret_cast<const uint8_t *>(pa->m_task) + r.port + at, 4);
                std::memcpy(&b, ot + r.orig + at, 4);
                char text[160];
                std::snprintf(text, sizeof text, "task %d +0x%zx: port %08x (%s) original %08x (%s)", pa->m_task->type, r.orig + at, a,
                              NameOf(reinterpret_cast<void *>(a)).c_str(), b, NameOf(reinterpret_cast<void *>(b)).c_str());
                return text;
            }
        }
    }
    // The waypoints.
    uint32_t v[4];
    std::memcpy(v, oa + 0xF4, 16);
    const size_t count = v[1] ? (v[2] - v[1]) / 4 : 0;
    if (count != pa->m_waypoints.size())
        return "waypoints: " + std::to_string(pa->m_waypoints.size()) + " / " + std::to_string(count);
    for (size_t k = 0; k < count; ++k) {
        const uint8_t *ow = reinterpret_cast<uint8_t *const *>(static_cast<uintptr_t>(v[1]))[k];
        if (std::memcmp(ow, pa->m_waypoints[k], sizeof(S_AIWaypoint)) != 0)
            return "waypoint " + std::to_string(k);
    }
    uint8_t *ow;
    std::memcpy(&ow, oa + 0x104, 4);
    if ((pa->m_waypoint != nullptr) != (ow != nullptr))
        return "waypoint passed (null on one side)";
    if (ow && std::memcmp(ow, pa->m_waypoint, sizeof(S_AIWaypoint)) != 0)
        return "waypoint passed";
    // The parts.
    auto part = [&](size_t at) {
        uint8_t *p;
        std::memcpy(&p, oa + at, 4);
        return p;
    };
    if ((at = FirstDiff(reinterpret_cast<const uint8_t *>(pa->m_facing), part(0x124), 0x60, {0x54, 0x58, 0x5C})) >= 0)
        return "facing+" + std::to_string(at);
    if ((at = FirstDiff(reinterpret_cast<const uint8_t *>(pa->m_fireOrder), part(0x128), 0x50, {0x4C})) >= 0)
        return "fireOrder+" + std::to_string(at);
    if ((at = FirstDiff(reinterpret_cast<const uint8_t *>(pa->m_move), part(0x12C), 0x60, {0x5C})) >= 0) {
        uint32_t a = 0, b = 0;
        std::memcpy(&a, reinterpret_cast<const uint8_t *>(pa->m_move) + at, 4);
        std::memcpy(&b, part(0x12C) + at, 4);
        char words[48];
        std::snprintf(words, sizeof words, " (port %08x original %08x)", a, b);
        return "move+" + std::to_string(at) + words;
    }
    if (std::memcmp(pa->m_alert, part(0x134), 5) != 0)
        return "alert";
    return DriverDiffers(*pa->m_driver, part(0x130));
}

// (The human's net object, part 20's block, is both sides': each side's writes kept apart, the port's undone before
// the original runs.)
template <class P, class O> void AiCall(const char *what, P port, O original)
{
    AiToOrig(g_pa, g_oa);
    ControllerToOrig(g_pc2, g_oc2);
    std::memset(g_netObjectBytes, 0x5A, sizeof g_netObjectBytes);
    std::memcpy(g_aiNetPort, g_netObjectBytes, sizeof g_aiNetPort);
    DiffCall(what,
             [&] {
                 const int r = port();
                 std::memcpy(g_aiNetOrig, g_netObjectBytes, sizeof g_aiNetOrig);
                 std::memcpy(g_netObjectBytes, g_aiNetPort, sizeof g_aiNetPort);
                 std::memcpy(g_aiNetPort, g_aiNetOrig, sizeof g_aiNetPort);
                 return r;
             },
             [&](void *o) {
                 const int r = original(o);
                 std::memcpy(g_aiNetOrig, g_netObjectBytes, sizeof g_aiNetOrig);
                 return r;
             });
    std::string why = AiDiffers(g_pa, g_oa);
    if (why.empty()) {
        // The net objects (the callbacks: each side's own; the contexts mapped).
        static const struct {
            size_t at;
            const void *port;
            uint32_t orig;
        } callbacks[] = {{0x54, reinterpret_cast<const void *>(NetReadControlBits), 0x0053b970},
                         {0x58, reinterpret_cast<const void *>(NetWriteControlBits), 0x0053b990},
                         {0x64, reinterpret_cast<const void *>(NetCreateMessage), 0x0053b760}};
        for (size_t at = 0x38; at < 0x68 && why.empty(); at += 4) {
            uint32_t a, b;
            std::memcpy(&a, g_aiNetPort + at, 4);
            std::memcpy(&b, g_aiNetOrig + at, 4);
            a = OrigWord(a);
            for (const auto &c : callbacks)
                if (c.at == at && a == static_cast<uint32_t>(reinterpret_cast<uintptr_t>(c.port)))
                    a = c.orig;
            if (at == 0x40)
                a &= 0xFF, b &= 0xFF;
            if (a != b)
                why = "netObject+" + std::to_string(at);
        }
    }
    CHECK(why.empty());
    static std::map<std::string, int> shown;
    if (!why.empty() && shown[what]++ < 3)
        std::printf("  %s: %s\n", what, why.c_str());
}
template <class R, class... A> R OAi(uint32_t at, A... a)
{
    using F = R(AC *)(void *, A...);
    return OriginalAt<F>(at)(static_cast<void *>(g_oa), a...);
}

// The port's C_WeaponItem::m_shotInterval (private here): found through its getter.
size_t ShotIntervalAt()
{
    static size_t at = 0;
    if (!at) {
        alignas(16) static uint8_t probe[0x200];
        for (size_t k = 4; k + 4 <= sizeof probe; k += 4) {
            std::memset(probe, 0, sizeof probe);
            const int marker = 0x5A5A1234;
            std::memcpy(probe + k, &marker, 4);
            if (reinterpret_cast<const C_WeaponItem *>(probe)->GetShotInterval() == marker) {
                at = k;
                break;
            }
        }
    }
    return at;
}

// ---- the random state
float AiFloat() { return P(0.1f) ? 0.0f : U(-30, 30); }
S_vector4 AiDir()
{
    if (P(0.5f)) {
        const float a = U(-3.2f, 3.2f), c = U(-1, 1), s = std::sqrt(1 - c * c);
        return {std::cos(a) * s, c, std::sin(a) * s, 0.0f};
    }
    return {AiFloat(), AiFloat(), AiFloat(), P(0.5f) ? 0.0f : U(-1, 1)};
}
int AiInt() { return P(0.2f) ? -1 : P(0.5f) ? I(0, 20) : I(-5000, 50000); }
// (Part 23c's actors: their ids 100 + their kind, which both sides' GetActorById answer; 12 the other human.)
C_Actor *AiActor() { return P(0.3f) ? nullptr : static_cast<C_Actor *>(PortActor(I(0, KIND_HUMAN))); }
void RandomAiTask(S_AITask *t)
{
    t->unknown08 = AiInt();
    t->unknown0C = AiInt();
    t->unknown10 = AiInt();
    if (t->type == 1) {
        auto *k = static_cast<S_AITask1 *>(t);
        k->pos = AiDir();
        k->dir = AiDir();
        k->unknown40 = AiFloat();
        k->unknown44 = AiInt();
        k->checkpoint = AiInt();
    } else if (t->type == 2) {
        auto *k = static_cast<S_AITask2 *>(t);
        k->unknown20 = AiDir();
        k->unknown30 = AiDir();
        k->unknown40 = AiInt();
        // (None or a human: a load naming another actor leaves the task's human as its pool object had it, which
        // differs between the sides' pools.)
        k->human = P(0.5f) ? nullptr : PortOther();
    } else if (t->type == 3) {
        auto *k = static_cast<S_AITask3 *>(t);
        k->pos = AiDir();
        k->actor = AiActor();
    }
}
void RandomAi()
{
    // (Part 23e's state: the controller's, the interactions', the weapons'; their choices for the hands taken over.)
    RandomWeapons();
    C_Human *h = g_h.human;
    S_TestAi *a = g_pa;
    RandomControllerFields(a);
    a->m_human = h;
    a->m_handsItem = g_pc->m_handsItem;
    a->m_axes = g_pc->m_axes;
    a->m_buttons = g_pc->m_buttons;
    a->m_weaponRaised = g_pc->m_weaponRaised;
    a->m_viewDir = g_pc->m_viewDir;
    a->m_toServer.clear();
    a->m_toClients.clear();
    std::memset(g_oa + 0x80, 0, 0x20);
    h->m_controller = a;
    // S_AIFire.
    S_AIFire &f = a->m_fire;
    f.unknown00 = P(0.5f);
    f.burstTime = AiInt();
    f.pauseTime = AiInt();
    f.unknown0C = AiInt();
    f.target = AiActor();
    f.orderTarget = AiActor();
    f.flags = uint32_t(I(0, 3));
    f.throwPos = AiDir();
    // The task, the waypoints (the old ones back to the pools).
    if (a->m_task)
        ReleaseTask(a->m_task);
    a->m_task = P(0.4f) ? nullptr : AllocTask(I(1, 12));
    if (a->m_task)
        RandomAiTask(a->m_task);
    for (S_AIWaypoint *w : a->m_waypoints)
        ReleaseWaypoint(w);
    a->m_waypoints.clear();
    for (int k = P(0.4f) ? 0 : I(1, 4); k > 0; --k)
        a->m_waypoints.push_back(AllocWaypoint(I(-1, 300), AiDir(), uint16_t(I(0, 0xFFFF)), uint8_t(I(0, 20)), U(0, 4)));
    ReleaseWaypoint(a->m_waypoint);
    a->m_waypoint = P(0.5f) ? nullptr : AllocWaypoint(I(-1, 300), AiDir(), uint16_t(I(0, 0xFFFF)), uint8_t(I(0, 20)), U(0, 4));
    a->m_lookAroundTime = AiInt();
    a->m_unknown10C = AiInt();
    a->m_unknown110 = AiInt();
    a->m_unknown114 = AiInt();
    a->m_unknown118 = AiInt();
    a->m_ignoreCircumvention = P(0.5f);
    a->m_unknown120 = AiInt();
    // The parts.
    S_AIFacing &fc = *a->m_facing;
    fc.facing = AiDir();
    fc.view = AiDir();
    fc.lookDir = AiDir();
    fc.facingX = U(-1, 1);
    fc.facingZ = U(-1, 1);
    fc.viewX = U(-1, 1);
    fc.viewZ = U(-1, 1);
    fc.locks = uint16_t(I(0, 3));
    fc.cone = P(0.5f) ? S_AIFacing::CONE : S_AIFacing::CONE_LYING;
    fc.lookPeriod = AiInt();
    fc.lookTimer = AiInt();
    fc.lookAt = nullptr;
    S_AIFireOrder &fo = *a->m_fireOrder;
    fo.unknown00 = AiDir();
    fo.fireDir = AiDir();
    fo.throwPos = AiDir();
    fo.target = AiActor();
    fo.reload = P(0.5f);
    fo.throwGrenade = P(0.5f);
    fo.result = uint32_t(I(0, 15));
    fo.burstTime = AiInt();
    fo.burstSpread = AiInt();
    fo.pauseTime = AiInt();
    fo.pauseSpread = AiInt();
    S_AIMoveOrder &m = *a->m_move;
    m.unknown00 = AiDir();
    m.pos = AiDir();
    m.checkpoint = uint16_t(I(0, 0xFFFF));
    m.unknown24 = AiInt();
    m.unknown28 = AiInt();
    m.unknown2C = U(0, 100);
    m.unknown30 = AiInt();
    m.unknown34 = AiInt();
    m.unknown38 = AiInt();
    m.unknown3C = AiInt();
    m.door = AiActor();
    m.ladder = AiActor();
    m.ladderFrom = uint16_t(I(0, 0xFFFF));
    m.ladderTo = uint16_t(I(0, 0xFFFF));
    m.unknown4C = I(-1, 6);
    m.flags = (uint32_t(std::rand()) << 17) ^ uint32_t(std::rand());
    m.unknown54 = uint32_t(I(0, 0x1F000));
    m.unknown58 = P(0.5f);
    a->m_alert->state = I(0, 5);
    a->m_alert->changed = P(0.5f);
    C_AIDriver &d = *a->m_driver;
    d.m_path.clear();
    for (int k = P(0.5f) ? 0 : I(1, 3); k > 0; --k) {
        S_AIPathPoint p;
        p.pos = AiDir();
        p.dir = AiDir();
        p.last = P(0.3f);
        d.m_path.push_back(p);
    }
    d.m_pathIndex = uint16_t(I(0, 4));
    d.m_progress = U(0, 1);
    d.m_nearestDist2 = U(0, 1e8f);
    d.m_reversing = P(0.5f);
    d.m_moving = P(0.5f);
    d.m_stuckTime = AiInt();
    d.m_noProgressTime = AiInt();
    d.m_lastPos = AiDir();
    d.m_flags = uint32_t(I(0, 7));
    d.m_target = AiDir();
    d.m_targetDir = AiDir();
    d.m_vehicle = nullptr;
    d.m_unknown88 = P(0.5f);
    d.m_speed = U(-5, 30);
    d.m_pace = uint32_t(I(0, 0x1F)) << 12;
    d.m_label = AiInt();
    g_ai.climbTarget = P(0.5f) ? 0 : static_cast<int>(reinterpret_cast<intptr_t>(AiActor()));
    g_ai.ladderPoints.clear();
    for (int k = P(0.3f) ? 2 : I(0, 3); k > 0; --k)
        g_ai.ladderPoints.push_back(uint16_t(I(0, 500)));
    g_ai.localId = I(0, 7);
    std::memcpy(g_aiReplication + 0xDC, &g_ai.localId, 4);
    *OriginalAt<void **>(0x0097d084) = g_aiReplication;
}

void SetUpAi()
{
    // (Part 23's, as its TestController: the human's combat side, motion and deaths, the controllers, the
    // interactions, speech and weapons; part 20's respawn for the net property's track.)
    SetUpCombat();
    SetUpMotion();
    SetUpDeaths();
    SetUpController();
    SetUpInteractions();
    SetUpSpeech();
    SetUpWeapons();
    SetUpRespawn();
    const std::pair<uint32_t, const void *> redirects[] = {
        {0x005ac3e0, FAKE(OA_ClearMovementFlags)},
        {0x005ac390, FAKE(OA_SetMovementFlags)},
        {0x005ac2e0, FAKE(OA_GetClimbTarget)},
        {0x0051e6d0, FAKE(OA_GetActorPoints)},
#ifndef HUMAN_AICOMBAT   // (the combat build tests the real one)
        {0x00575850, FAKE(OA_ApplyAlertState)},
#endif
        {0x004f2630, FAKE(OA_SetUnknown4f2630)},
        {0x0073e970, FAKE(OA_QueueMessage)},
        {0x007e559c, FAKE(AO_AlignedFree)},
    };
    for (const auto &r : redirects)
        RedirectOriginal(r.first, r.second);
    for (void **vt : {g_vtMsgMoveItem, g_vtMsgTransferItem, g_vtMsgDropItem, g_vtMsgRemoteRequest, g_vtMsg23, g_vtMsgSay})
        vt[2] = vt[3] = reinterpret_cast<void *>(AO_MessageDelete);
    // The original's pools: their growth (their static initialiser did not run; part 32).
    for (int k = 0; k < 16; ++k)
        *OriginalAt<int *>(0x008ab158 + 0x18 * k + 0x0C) = 16;
    g_pa = new S_TestAi(HUMAN_CONTROLLER_ENEMY);
    g_oa = static_cast<uint8_t *>(OC_AlignedNew(0x140, 16));
    typedef void(AC * T_Ctor)(void *, int);
    OriginalAt<T_Ctor>(0x0056ee90)(g_oa, HUMAN_CONTROLLER_ENEMY);
    std::memcpy(g_oaVt, *reinterpret_cast<void **>(g_oa), sizeof g_oaVt);
    g_oaVt[0xDC / 4] = reinterpret_cast<void *>(OA_ExecuteCommand);
    void *vt = g_oaVt;
    std::memcpy(g_oa, &vt, 4);
    g_names[g_pa] = g_names[g_oa] = "ai";
    g_extraPointers.push_back({g_pa, g_oa});
    const char *const parts[] = {"facing", "fireOrder", "move", "driver", "alert"};
    void *const portParts[] = {g_pa->m_facing, g_pa->m_fireOrder, g_pa->m_move, g_pa->m_driver, g_pa->m_alert};
    for (int k = 0; k < 5; ++k) {
        void *op;
        std::memcpy(&op, g_oa + 0x124 + 4 * k, 4);
        g_names[portParts[k]] = g_names[op] = parts[k];
        g_extraPointers.push_back({portParts[k], op});
    }
    // The hands' weapons (part 23e's: the hands container given the original's before the original runs).
    g_toOriginalBeforeWeapons = g_toOriginalExtra;
    g_toOriginalExtra = WeaponsToOriginal;
    for (int k = 0; k < 2; ++k)
        g_extraPointers.push_back({g_weaponPort[k], g_weaponOrig[k]});
    // The game's road network (the checkpoints +0x38 in the port, +0x3C in the original).
    g_names[g_checkpointNet + 0x38] = g_names[g_checkpointNet + 0x3C] = "roads";
    // The human's body (+0x98 in the original; the port's by offsetof).
    g_names[&g_h.human->m_body] = g_names[o_human + 0x98] = "body";
}

void RandomAiQueue(std::vector<S_ControllerMessage *> &port, uint8_t *vec);   // (part 33b's)
void TestAiLife()
{
    std::printf("ai controller: life cycle, small virtuals, body flags\n");
    C_Human *h = g_h.human;
    for (int i = 0; i < 12000 / QUICK; ++i) {
        RandomAi();
        const int which = I(0, 20);
        const S_vector4 v = AiDir();
        const int n = I(-1, 8);
        static const uint32_t ROLES[] = {GAME_ROLE_LOCAL, GAME_ROLE_LOCAL, GAME_ROLE_SERVER, GAME_ROLE_CLIENT};
        g_gameRole = ROLES[I(0, 3)];
        switch (which) {
        case 0:
            AiCall("SetAlertState", [&] { g_pa->SetAlertState(n); return 0; }, [&](void *) { OAi<void>(0x0056f1e0, n); return 0; });
            break;
        case 1: {
            // (Network games: the human's net object, whose property's track the respawn's fakes log.)
            const bool previous = P(0.5f);
            C_HumanController *pp = previous ? g_pc2 : nullptr;
            void *op = previous ? static_cast<void *>(g_oc2) : nullptr;
            g_pa->m_type = static_cast<E_HumanController>(P(0.7f) ? int(CONTROLLER_TYPES[I(0, 6)]) : I(0, 9));
            g_pa->m_events = nullptr;
            // (On a ladder: a walking network; the original asks a missing one for the ladder's points, the port's
            // fix does not.)
            // (The base's Attach keeps the previous controller's ladder flag.)
            if (previous && P(0.4f))
                g_pc2->m_moveFlags |= MOVE_ON_LADDER;
            if (previous && (g_pc2->m_moveFlags & MOVE_ON_LADDER)) {
                void *net = g_checkpointNet;
                GetGame()->m_checkpoints = static_cast<C_Checkpoints *>(net);
                std::memcpy(g_origGame + 0x54, &net, 4);
            }
            AiCall("Attach", [&] { g_pa->Attach(h, pp); return 0; },
                   [&](void *) { OAi<void>(0x0056f200, static_cast<void *>(o_human), op); return 0; });
            break;
        }
        case 2:
            // (Messages to the clients: Detach flushes them; part 33b's streams.)
            RandomAiQueue(g_pa->m_toClients, g_oa + 0x90);
            AiCall("Detach", [&] { g_pa->Detach(); return 0; }, [&](void *) { OAi<void>(0x0056f630); return 0; });
            break;
        case 3:
            AiCall("ReleaseTask", [&] { g_pa->ReleaseTask(); return 0; }, [&](void *) { OAi<void>(0x00575820); return 0; });
            break;
        case 4:
            AiCall("LookAround", [&] { g_pa->LookAround(n); return 0; }, [&](void *) { OAi<void>(0x00570a10, n); return 0; });
            break;
        case 5:
            AiCall("LookAroundAt", [&] { g_pa->LookAroundAt(n, v); return 0; },
                   [&](void *) { OAi<void>(0x00570980, n, &v); return 0; });
            break;
        case 6: {
            const uint32_t flags = uint32_t(I(0, 0x7FFF)) | (P(0.5f) ? uint32_t(I(0, 0xFFFF)) << 15 : 0u);
            AiCall("SetMoveFlags", [&] { g_pa->SetMoveFlags(flags); return 0; },
                   [&](void *) { OAi<void>(0x0056fc30, flags); return 0; });
            break;
        }
        case 7:
            AiCall("GetFacingDir", [&] { return VectorHash(g_pa->GetFacingDir()); },
                   [&](void *) { return VectorHash(*OAi<const S_vector *>(0x0056fbf0)); });
            break;
        case 8:
            AiCall("GetViewDir", [&] { return VectorHash(g_pa->GetViewDir()); },
                   [&](void *) { return VectorHash(*OAi<const S_vector *>(0x0056fc00)); });
            break;
        case 9:
            AiCall("SetFacingDir", [&] { g_pa->SetFacingDir(v); return 0; }, [&](void *) { OAi<void>(0x0056fc10, &v); return 0; });
            break;
        case 10:
            AiCall("SetViewDir", [&] { g_pa->SetViewDir(v); return 0; }, [&](void *) { OAi<void>(0x0056fc20, &v); return 0; });
            break;
        case 11: {
            // (A ladder or another kind.)
            C_Actor *other = P(0.5f) ? reinterpret_cast<C_Actor *>(g_vehicleBytes) : reinterpret_cast<C_Actor *>(g_portOther);
            static const int kinds[] = {ACTOR_LADDER, ACTOR_CAR, ACTOR_DOOR, ACTOR_HUMAN};
            const int kind = kinds[I(0, 3)];
            reinterpret_cast<C_Actor *>(g_vehicleBytes)->m_type = static_cast<E_ActorType>(kind);
            std::memcpy(g_vehicleBytes + 0x18, &kind, 4);
            void *oo = other == reinterpret_cast<C_Actor *>(g_vehicleBytes) ? static_cast<void *>(g_vehicleBytes) : static_cast<void *>(g_origOther);
            AiCall("OnCollisionEx", [&] { return int(g_pa->OnCollisionEx(other, nullptr, 0, 0, 0, 0, 0)); },
                   [&](void *) { return int(OAi<uint32_t>(0x005765f0, oo, nullptr, 0, 0, 0, 0, 0) & 0xFF); });
            break;
        }
        case 12:
            AiCall("PlanGrenade", [&] { return int(g_pa->PlanGrenade()); },
                   [&](void *) { return int(OAi<uint32_t>(0x00573210) & 0xFF); });
            break;
        case 13:
            AiCall("Unknown108", [&] { return int(g_pa->Unknown108()); },
                   [&](void *) { return int(OAi<uint32_t>(0x00524de0) & 0xFF); });
            break;
        case 14: {
            const bool on = P(0.5f);
            if ((g_gameRole & GAME_ROLE_LOCAL) != GAME_ROLE_LOCAL || P(0.5f))
                g_pa->m_netObject = reinterpret_cast<C_NetObject *>(g_netObjectBytes);   // (a network game has one)
            AiCall("Freeze", [&] { g_pa->Freeze(on); return 0; }, [&](void *) { OAi<void>(0x005786c0, on); return 0; });
            break;
        }
        case 15: {
            // (In a network game the throws' RequestAction queue an action message on the net object.)
            if ((g_gameRole & GAME_ROLE_LOCAL) != GAME_ROLE_LOCAL)
                g_pa->m_netObject = reinterpret_cast<C_NetObject *>(g_netObjectBytes);
            static const uint32_t FLAGS[] = {0, 0x1000, 0x2000, 0x4000, 0x20000, 0x100, 0x10000, 0x1};
            uint32_t flags = 0;
            for (int k = I(0, 3); k > 0; --k)
                flags |= FLAGS[I(0, 7)];
            // (Flag 0x20000 uses the hands item without checking for one: the original crashes on none.)
            if ((flags & 0x20000) && !(flags & 0x7000)) {
                g_handsWeapon = g_weaponPort[I(0, 1)];
                g_wornContainers[ITEM_CONTAINER_HANDS].item = g_handsWeapon;
            }
            AiCall("OnBodyFlags", [&] { g_pa->OnBodyFlags(flags); return 0; },
                   [&](void *) { OAi<void>(0x005783d0, flags); return 0; });
            break;
        }
        case 16: {
            // (In a car or not; a road point found or not; a stop.)
            static const int kinds[] = {ACTOR_CAR, ACTOR_CAR, ACTOR_DOOR};
            const int kind = kinds[I(0, 2)];
            reinterpret_cast<C_Actor *>(g_vehicleBytes)->m_type = static_cast<E_ActorType>(kind);
            std::memcpy(g_vehicleBytes + 0x18, &kind, 4);
            h->m_vehicle = P(0.8f) ? reinterpret_cast<C_Actor *>(g_vehicleBytes) : nullptr;
            void *net = g_checkpointNet;
            GetGame()->m_checkpoints = static_cast<C_Checkpoints *>(net);
            std::memcpy(g_origGame + 0x54, &net, 4);
            const int point = P(0.2f) ? 0xFFFF : P(0.2f) ? -1 : I(0, 9);
            const float speed = P(0.2f) ? 0.0f : U(-20, 30);
            const int label = I(-1, 50);
            AiCall("Drive", [&] { g_pa->Drive(point, speed, label); return 0; },
                   [&](void *) { OAi<void>(0x00577a60, point, speed, label); return 0; });
            break;
        }
        case 17: {
            // (The walking network's points at their own stride: the point's index by its address.)
            uint16_t count = uint16_t(I(0, 8));
            void *points = g_aiPoints;
            std::memcpy(g_checkpointNet, &count, 2);
            std::memcpy(g_checkpointNet + 4, &points, 4);
            g_pa->m_checkpoints = reinterpret_cast<C_Checkpoints *>(g_checkpointNet);
            g_pa->m_currentCheckpoint = P(0.3f) ? nullptr : reinterpret_cast<S_Checkpoint *>(g_aiPoints[I(0, 7)]);
            uint16_t pc = 0x1234, pn = 0x5678, oc = 0x1234, on = 0x5678;
            AiCall("GetPathPoints", [&] { g_pa->GetPathPoints(&pc, &pn); return pc * 0x10000 + pn; },
                   [&](void *) { OAi<void>(0x0057a390, &oc, &on); return oc * 0x10000 + on; });
            break;
        }
        case 18: {
            // (In a network game: the action message queued; the hands item a weapon, another item or none.)
            g_gameRole = P(0.2f) ? GAME_ROLE_LOCAL : P(0.5f) ? GAME_ROLE_SERVER : GAME_ROLE_CLIENT;
            g_pa->m_netObject = reinterpret_cast<C_NetObject *>(g_netObjectBytes);
            if (P(0.2f))
                g_pa->m_handsItem = reinterpret_cast<C_Item *>(g_wornItems[I(0, 15)]);
            // (The weapons' shot intervals, the original's +0x68: the message sends a hundredth.)
            for (int k = 0; k < 2; ++k) {
                const int interval = I(0, 3000);
                std::memcpy(g_weaponPort[k] + ShotIntervalAt(), &interval, 4);
                std::memcpy(g_weaponOrig[k] + 0x68, &interval, 4);
            }
            const S_vector4 pos = AiDir(), dir = AiDir();
            const int action = I(0, 12), ammo = I(-1, 500), rounds = I(-1, 300);
            const float strength = U(0, 1);
            AiCall("RequestAction", [&] { g_pa->RequestAction(action, &pos, &dir, strength, ammo, rounds); return 0; },
                   [&](void *) { OAi<void>(0x005785b0, action, &pos, &dir, strength, ammo, rounds); return 0; });
            break;
        }
        default: {
            S_HumanCommand command;
            command.category = P(0.8f) ? 2 : I(0, 4);
            command.type = I(0, 0x28);
            command.param1 = P(0.5f) ? h->m_netId : I(-1, 40);
            command.param2 = I(-1, 6);
            AiCall("IssueCommand", [&] { g_pa->IssueCommand(command); return 0; },
                   [&](void *) { OAi<void>(0x00571c10, &command); return 0; });
            break;
        }
        }
    }
}

// ---- the saved games: both sides save the same controller (the files compared); then both load, into controllers
// given other values, the saved file or one made by hand (the AI's chunks in any order, some left out, unknown ones),
// the controllers and where the file was left compared
std::string AiTempFile(const char *side)
{
    return std::string(std::getenv("TEMP") ? std::getenv("TEMP") : ".") + "\\aictl_" + std::to_string(GetCurrentProcessId()) + side;
}
std::vector<uint8_t> AiFileBytes(const std::string &name)
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
// The AI's chunks, by hand (from the port's parts' own savers), a random subset in a random order.
void HandMadeAiFile(C_chunk &c)
{
    S_TestAi *a = g_pa;
    std::vector<int> kinds = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12};
    for (size_t k = kinds.size(); k > 1; --k)
        std::swap(kinds[k - 1], kinds[size_t(I(0, int(k) - 1))]);
    kinds.resize(size_t(I(0, int(kinds.size()))));
    for (int kind : kinds) {
        switch (kind) {
        case 0: c.Begin(0x1388); a->C_HumanController::SaveGame(c); c.Ascend(); break;
        case 1: {
            c.Begin(0x1389);
            const int words[5] = {AiInt(), AiInt(), AiInt(), AiInt(), AiInt()};
            c.Write(words, 4 * I(0, 5));
            c.Ascend();
            break;
        }
        case 2: c.Begin(0x138C); a->m_facing->SaveGame(c); c.Ascend(); break;
        case 3: c.Begin(0x138D); a->m_fireOrder->SaveGame(c); c.Ascend(); break;
        case 4: c.Begin(0x138E); a->m_move->SaveGame(c); c.Ascend(); break;
        case 5: c.Begin(0x138F); a->m_alert->SaveGame(c); c.Ascend(); break;
        case 6: c.Begin(0x139C); a->m_driver->SaveGame(c); c.Ascend(); break;
        case 7: c.Begin(0x1390); a->m_fire.SaveGame(c); c.Ascend(); break;
        case 8: {
            // (Known task types only: the original calls through null for another.)
            S_AITask *t = AllocTask(I(1, 12));
            RandomAiTask(t);
            c.Begin(0x1392);
            c.WriteChunk(0x1391, t->type);
            t->SaveGame(c);
            c.Ascend();
            ReleaseTask(t);
            break;
        }
        case 9: {
            // (Whole waypoints: the original loops for ever on a part of one.)
            c.Begin(0x138A);
            for (int k = I(0, 4); k > 0; --k) {
                S_AIWaypoint *w = AllocWaypoint(I(-1, 300), AiDir(), uint16_t(I(0, 0xFFFF)), uint8_t(I(0, 20)), U(0, 4));
                c.Write(w, sizeof(S_AIWaypoint));
                ReleaseWaypoint(w);
            }
            c.Ascend();
            break;
        }
        case 10: {
            S_AIWaypoint *w = AllocWaypoint(I(-1, 300), AiDir(), uint16_t(I(0, 0xFFFF)), uint8_t(I(0, 20)), U(0, 4));
            c.Begin(0x138B);
            c.Write(w, sizeof(S_AIWaypoint));
            c.Ascend();
            ReleaseWaypoint(w);
            break;
        }
        case 11: c.Begin(0x13A6); c.Ascend(); break;
        default: {
            c.Begin(uint16_t(I(1, 0x1300)));
            const int x = I(0, 1000);
            c.Write(&x, 4);
            c.Ascend();
            break;
        }
        }
    }
}
int AiNextChunk(C_chunk &c)
{
    uint16_t id = 0;
    return c.Descend(id) ? int(id) : -1;
}
void TestAiSaves()
{
    std::printf("ai controller: saved games\n");
    const std::string portFile = AiTempFile("_port.sav"), origFile = AiTempFile("_orig.sav");
    typedef void(AC * T_Chunk)(void *, C_chunk *);
    for (int i = 0; i < 2400 / QUICK; ++i) {
        RandomAi();
        // (An interaction has its target, as part 23's: the original saves the target's id without a check.)
        static const int kinds[] = {0, 1, 2, 9, 11, KIND_HUMAN};
        if (P(0.6f)) {
            g_pa->m_interaction = P(0.2f) ? HUMAN_COMMAND_HEAL_SELF : I(0, 0x26);
            g_pa->m_interactionTarget = static_cast<C_Actor *>(PortActor(kinds[I(0, 5)]));
        } else {
            g_pa->m_interaction = -1;
        }
        std::remove(portFile.c_str());
        std::remove(origFile.c_str());
        AiCall("SaveGame",
               [&] {
                   C_chunk c;
                   c.Open(portFile.c_str(), true);
                   c.Begin(0x7777);
                   g_pa->SaveGame(c);
                   c.Ascend();
                   c.Begin(0x0BD1);
                   c.Ascend();
                   return 0;
               },
               [&](void *) {
                   C_chunk c;
                   c.Open(origFile.c_str(), true);
                   c.Begin(0x7777);
                   OAi<void>(0x00576fe0, &c);
                   c.Ascend();
                   c.Begin(0x0BD1);
                   c.Ascend();
                   return 0;
               });
        const bool same = AiFileBytes(portFile) == AiFileBytes(origFile);
        CHECK(same);
        static int shown;
        if (!same && shown++ < 3)
            std::printf("  SaveGame: the files differ\n");
        if (P(0.5f)) {
            C_chunk c;
            c.Open(portFile.c_str(), true);
            c.Begin(0x7777);
            HandMadeAiFile(c);
            c.Ascend();
            c.Begin(0x0BD1);
            c.Ascend();
        }
        RandomAi();
        AiCall("LoadGame",
               [&] {
                   C_chunk c;
                   c.Open(portFile.c_str(), false);
                   uint16_t id;
                   c.Descend(id);
                   g_pa->LoadGame(c);
                   c.Ascend();
                   return AiNextChunk(c);
               },
               [&](void *) {
                   C_chunk c;
                   c.Open(portFile.c_str(), false);
                   uint16_t id;
                   c.Descend(id);
                   OAi<void>(0x005774a0, &c);
                   c.Ascend();
                   return AiNextChunk(c);
               });
    }
    std::remove(portFile.c_str());
    std::remove(origFile.c_str());
}

// The destructor: fresh controllers of each side with waypoints and a task (the port's puts them back into the pools,
// the original's does not: neither logs), their parts freed; the calls compared.
void TestAiDestructor()
{
    std::printf("ai controller: destructor\n");
    for (int i = 0; i < 400 / QUICK; ++i) {
        auto *pa = new S_TestAi(HUMAN_CONTROLLER_ENEMY);
        auto *oa = static_cast<uint8_t *>(OC_AlignedNew(0x140, 16));
        typedef void(AC * T_Ctor)(void *, int);
        OriginalAt<T_Ctor>(0x0056ee90)(oa, HUMAN_CONTROLLER_ENEMY);
        for (int k = I(0, 3); k > 0; --k)
            pa->m_waypoints.push_back(AllocWaypoint());
        if (P(0.5f))
            pa->m_waypoint = AllocWaypoint();
        if (P(0.5f))
            pa->m_task = AllocTask(I(1, 12));
        g_names[pa] = g_names[oa] = "dyingAi";
        DiffCall("Destructor", [&] { pa->~S_TestAi(); return 0; },
                 [&](void *) {
                     typedef void(AC * T_Dtor)(void *);
                     OriginalAt<T_Dtor>(0x0056f0e0)(oa);
                     return 0;
                 });
    }
}

// ---- part 33b: the network side (UpdateNetwork, FlushMessages, OnMessage, OnMessage2)
//
// The messages' Read and Write are fakes of the same format on both sides (part 31 tested the real ones): Write sends
// the type in 5 bits and the fields the controller reads (each side's layout), Read reads those fields; the port's
// through its message classes' stubs, part 23's raw vtables' Write slot and the Read wraps, the original's through
// redirects of its 23 Reads and 23 Writes. The actor messages' streams are the test's: CreateActorMessage(Ex) hands out
// a stream, SendActorMessage logs its bits.

// A message's fields as the controller uses them: (port offset, original offset, kind: 'i' a 16-bit int, 'b' a bool
// byte, 'f' a raw float).
struct S_AiMsgField {
    size_t port, orig;
    char kind;
};
std::vector<S_AiMsgField> AiMsgFields(int type)
{
    switch (type) {
    case CTRL_MSG_HIT:
        return {{8, 8, 'i'}, {0xC, 0xC, 'i'}, {0x10, 0x10, 'i'}, {0x14, 0x14, 'i'}, {0x18, 0x20, 'f'}, {0x1C, 0x24, 'f'},
                {0x20, 0x28, 'f'}, {0x28, 0x30, 'f'}, {0x2C, 0x34, 'f'}, {0x30, 0x38, 'f'}};
    case CTRL_MSG_DROP_ITEM:
        return {{8, 8, 'i'}, {0xC, 0x10, 'f'}, {0x10, 0x14, 'f'}, {0x14, 0x18, 'f'}, {0x1C, 0x20, 'f'}, {0x20, 0x24, 'i'}};
    case CTRL_MSG_SAY: return {{8, 8, 'i'}, {0xC, 0xC, 'b'}};
    case CTRL_MSG_MOVE_ITEM:
    case CTRL_MSG_TRANSFER_ITEM: return {{8, 8, 'i'}, {0xC, 0xC, 'i'}, {0x10, 0x10, 'i'}, {0x14, 0x14, 'i'}, {0x18, 0x18, 'i'}};
    case CTRL_MSG_TAKE_FROM_BOX: return {{8, 8, 'i'}, {0xC, 0xC, 'i'}, {0x10, 0x10, 'i'}, {0x14, 0x14, 'i'}};
    case CTRL_MSG_CONTROL_STATE: return {{0x28, 0x30, 'f'}, {0x2C, 0x34, 'f'}, {0x30, 0x38, 'f'}, {0x34, 0x3C, 'f'}, {0x48, 0x50, 'i'}};
    case CTRL_MSG_COMMAND: return {{8, 8, 'i'}, {0xC, 0xC, 'i'}, {0x10, 0x10, 'i'}};
    case CTRL_MSG_PICK_UP_ITEM: return {{8, 8, 'i'}, {0xC, 0xC, 'i'}};
    case CTRL_MSG_HEAL: return {{8, 8, 'i'}, {0xC, 0xC, 'i'}, {0x10, 0x10, 'i'}, {0x14, 0x14, 'b'}};
    case CTRL_MSG_ANIMATION: return {{8, 8, 'i'}, {0x88, 0x88, 'i'}, {0x8C, 0x8C, 'i'}, {0x90, 0x90, 'b'}};
    // (The action command's: its vtable since PR 23; the original's +0x40.)
    case CTRL_MSG_ACTION: return {{offsetof(S_MsgAction, command) + offsetof(S_ActionCommand, action), 0x40, 'i'}};
    case CTRL_MSG_SUSPEND:
    case CTRL_MSG_23: return {{8, 8, 'b'}};
    case CTRL_MSG_INVENTORY_CHECK: return {};
    // (Part 42: the four values the real Read reads; the server's local message leaves them unset otherwise.)
    case CTRL_MSG_17: return {{8, 8, 'i'}, {0xC, 0xC, 'i'}, {0x10, 0x10, 'i'}, {0x14, 0x14, 'i'}};
    default: return {{8, 8, 'i'}};
    }
}
std::string AiMsgText(const uint8_t *m, bool orig)
{
    int type;
    std::memcpy(&type, m + 4, 4);
    std::string t = "Msg" + std::to_string(type) + "[";
    for (const S_AiMsgField &f : AiMsgFields(type)) {
        uint32_t w = 0;
        std::memcpy(&w, m + (orig ? f.orig : f.port), f.kind == 'b' ? 1 : 4);
        t += std::to_string(f.kind == 'b' ? (w & 1u) : w) + " ";
    }
    return t + "]";
}
void FakeMsgWrite(uint8_t *m, C_BitStream *s, bool orig)
{
    int type;
    std::memcpy(&type, m + 4, 4);
    s->WriteBits(uint32_t(type), 5);
    std::vector<double> nums = {double(type)};
    for (const S_AiMsgField &f : AiMsgFields(type)) {
        uint32_t w = 0;
        std::memcpy(&w, m + (orig ? f.orig : f.port), f.kind == 'b' ? 1 : 4);
        s->WriteBits(f.kind == 'f' ? w : w & (f.kind == 'b' ? 1u : 0xFFFFu), f.kind == 'f' ? 32 : f.kind == 'b' ? 1 : 16);
        nums.push_back(double(w));
    }
    S_Call c{"MessageWrite", nums};
    g_calls.push_back(c);
}
void FakeMsgRead(uint8_t *m, int type, C_BitStream *s, bool orig)
{
    std::vector<double> nums = {double(type)};
    // (The controls' arrays and the animation's name are cleared first; the name then a short one of the tag.)
    if (type == CTRL_MSG_CONTROLS)
        std::memset(m + 8, 0, 0x200);
    if (type == CTRL_MSG_ANIMATION)
        std::memset(m + 8, 0, 0x80);
    if (type == CTRL_MSG_INVENTORY_CHECK) {
        void *h;
        std::memcpy(&h, m + 8, 4);
        S_Call c{"MessageRead " + NameOf(h), nums};
        g_calls.push_back(c);
        return;
    }
    for (const S_AiMsgField &f : AiMsgFields(type)) {
        uint32_t w = s->ReadBits(f.kind == 'f' ? 32 : f.kind == 'b' ? 1 : 16);
        if (f.kind == 'i')
            w = uint32_t(int32_t(int16_t(w)));
        std::memcpy(m + (orig ? f.orig : f.port), &w, f.kind == 'b' ? 1 : 4);
        nums.push_back(double(int32_t(w)));
    }
    if (type == CTRL_MSG_ANIMATION) {
        const char name[4] = {char('a' + (m[8] & 15)), char('a' + (m[9] & 15)), 0, 0};
        std::memcpy(m + 8, name, 4);
    }
    S_Call c{"MessageRead", nums};
    g_calls.push_back(c);
}
// The original's: its Reads and Writes by type (0x005b0e20..0x005b30d0).
const uint32_t kOrigMsgRead[24] = {0x005b10c0, 0x005b1510, 0x005b1980, 0x005b1180, 0x005b1000, 0, 0x005b0f70, 0x005b1a10,
                                   0x005b1bb0, 0x005b1e40, 0x005b1f10, 0x005b1d10, 0x005b1290, 0x005b2340, 0x005b2100,
                                   0x005b0ee0, 0x005b0e50, 0x005b1800, 0x005b21d0, 0x005b22b0, 0x005b2a90, 0x005b2f80,
                                   0x005b2ff0, 0x005b3060};
const uint32_t kOrigMsgWrite[24] = {0x005b1100, 0x005b1540, 0x005b19a0, 0x005b11a0, 0x005b1030, 0, 0x005b0f90, 0x005b1ad0,
                                    0x005b1c40, 0x005b1e90, 0x005b2020, 0x005b1d80, 0x005b13f0, 0x005b2700, 0x005b2150,
                                    0x005b0f00, 0x005b0e70, 0x005b1820, 0x005b2230, 0x005b22e0, 0x005b2d20, 0x005b2fa0,
                                    0x005b3010, 0x005b3080};
const uint32_t kOrigMsgVt[24] = {0x00810cc4, 0x00810c84, 0x00810c8c, 0x00810c6c, 0x00810cbc, 0, 0x00810cfc, 0x00810ca4,
                                 0x00810cac, 0x00810c9c, 0x00810cb4, 0x00810c94, 0x00810c74, 0x00810cf4, 0x00810d04,
                                 0x00810d0c, 0x00810d14, 0x00810d1c, 0x00810d24, 0x00810cec, 0x00810ce4, 0x00810ccc,
                                 0x00810cdc, 0x00810cd4};
// (One fake per type for the original's Reads: the type is not in the message before Read.)
template <int T> void AC OM_Read(uint8_t *m, C_BitStream *s) { FakeMsgRead(m, T, s, true); }
void AC OM_Write(uint8_t *m, C_BitStream *s) { FakeMsgWrite(m, s, true); }
void AC PM_Write(uint8_t *m, C_BitStream *s) { FakeMsgWrite(m, s, false); }
template <int... T> void RedirectOrigReads(std::integer_sequence<int, T...>)
{
    (void)std::initializer_list<int>{(kOrigMsgRead[T] ? (RedirectOriginal(kOrigMsgRead[T], reinterpret_cast<const void *>(OM_Read<T>)), 0) : 0)...};
}

// The actor messages' streams: four per side, handed out in turn.
struct S_AiStream {
    uint32_t words[64] = {};
    C_NetMessage msg;
};
S_AiStream g_aiStreams[4];
int g_aiStreamNext = 0;
C_BitStream *NewAiStream(int id, bool reliable, bool ex)
{
    S_AiStream &s = g_aiStreams[g_aiStreamNext++ & 3];
    std::memset(s.words, 0, sizeof s.words);
    s.msg.Attach(s.words, sizeof s.words * 8);
    s.msg.ResetPosition();                              // (Attach keeps the position)
    S_Call c{"CreateActorMessage", {double(id), double(reliable), double(ex)}};
    g_calls.push_back(c);
    return &s.msg;
}
void SendAiStream(C_BitStream *stream)
{
    auto *m = static_cast<C_NetMessage *>(stream);
    std::vector<double> nums = {double(m->BitPosition())};
    for (S_AiStream &s : g_aiStreams)
        if (&s.msg == m)
            for (uint32_t i = 0; i < (m->BitPosition() + 31) / 32; ++i)
                nums.push_back(double(s.words[i]));
    S_Call c{"SendActorMessage", nums};
    g_calls.push_back(c);
}
void *AC OS_CreateActorMessage(void *, int id, int reliable) { return NewAiStream(id, (reliable & 0xFF) != 0, false); }
void *AC OS_CreateActorMessageEx(void *, int id, int reliable) { return NewAiStream(id, (reliable & 0xFF) != 0, true); }
void AC OS_SendActorMessage(void *, C_BitStream *stream) { SendAiStream(stream); }

// The net object's Tick, the property's direction track (the original's at the property's +0xC0, SetDirection its
// slot 0x50).
void AC OS_Tick(void *self, float seconds) { Log(self, "NetObjectTick", {seconds}); }
void AC TR_SetDirection(void *self, const S_vector4 *dir, float angle) { Log(self, "TrackSetDirection", {dir->x, dir->y, dir->z, angle}); }
void *g_aiDirTrackVtPort[32], *g_aiDirTrackVtOrig[32];
alignas(16) uint8_t g_aiDirTrackPort[0x100];
void AC OS_ApplyRemoteHit(void *self, int a, int b, int c, int d, const float *v1, const float *v2)
{
    Log(self, "ApplyRemoteHit", {double(a), double(b), double(c), double(d), v1[0], v1[1], v1[2], v2[0], v2[1], v2[2]});
}
void AC OS_ClientDropItem(void *self, int actor, int uid, const float *v) { Log(self, "ClientDropItem", {double(actor), double(uid), v[0], v[1], v[2]}); }

void SetUpAiNetwork()
{
    RedirectOrigReads(std::make_integer_sequence<int, 24>());
    for (uint32_t w : kOrigMsgWrite)
        if (w)
            RedirectOriginal(w, reinterpret_cast<const void *>(OM_Write));
    const std::pair<uint32_t, const void *> redirects[] = {
        {0x00475530, FAKE(OS_CreateActorMessage)},
        {0x00475570, FAKE(OS_CreateActorMessageEx)},
        {0x004755b0, FAKE(OS_SendActorMessage)},
        {0x0073eaf0, FAKE(OS_Tick)},
        {0x005a15d0, FAKE(OS_ApplyRemoteHit)},
        {0x005a3750, FAKE(OS_ClientDropItem)},
    };
    for (const auto &r : redirects)
        RedirectOriginal(r.first, r.second);
    // Part 23's raw message vtables: Write (slot 5) the port's fake.
    for (void **vt : {g_vtMsgMoveItem, g_vtMsgTransferItem, g_vtMsgDropItem, g_vtMsgRemoteRequest, g_vtMsg23, g_vtMsgSay})
        vt[5] = reinterpret_cast<void *>(PM_Write);
    // The direction tracks.
    for (int k = 0; k < 32; ++k)
        g_aiDirTrackVtPort[k] = g_aiDirTrackVtOrig[k] = reinterpret_cast<void *>(CT_Unexpected);
    g_aiDirTrackVtPort[VSlot(&C_NetDirTrack::SetDirection)] = reinterpret_cast<void *>(TR_SetDirection);
    g_aiDirTrackVtOrig[0x50 / 4] = reinterpret_cast<void *>(TR_SetDirection);
    *reinterpret_cast<void ***>(g_aiDirTrackPort) = g_aiDirTrackVtPort;
    *reinterpret_cast<void ***>(g_origProperty + 0xC0) = g_aiDirTrackVtOrig;
    g_names[g_aiDirTrackPort] = g_names[g_origProperty + 0xC0] = "dirTrack";
}

// A message of `type` on each side (the port's class, the original's block with its vtable), its fields random.
S_ControllerMessage *NewPortMessage(int type)
{
    switch (type) {
    case 0: return new S_MsgControls;
    case 1: return new S_MsgHit;
    case 2: return new S_MsgPutOnUniform;
    case 3: return new S_MsgCommand;
    case 4: return new S_MsgControlState;
    case 6: return new S_MsgUseObject;
    case 7: return new S_MsgMoveItem;
    case 8: return new S_MsgTransferItem;
    case 9: return new S_MsgPickUpItem;
    case 10: return new S_MsgDropItem;
    case 11: return new S_MsgTakeFromBox;
    case 12: return new S_MsgAction;
    case 13: return new S_MsgInventoryCheck;
    case 14: return new S_MsgHeal;
    case 15: return new S_MsgRemoteRequest;
    case 16: return new S_MsgSoldierClass;
    case 17: return new S_Msg17;
    case 18: return new S_MsgAnimation;
    case 19: return new S_MsgSay;
    case 20: return new S_MsgEquipment;
    case 21: return new S_Msg21;
    case 22: return new S_MsgSuspend;
    default: return new S_Msg23;
    }
}
// Random queues on both sides (types 5 and above 23 too: a class's object with its type word changed; the original's
// the same class's vtable).
void RandomAiQueue(std::vector<S_ControllerMessage *> &port, uint8_t *vec)
{
    port.clear();
    const int n = P(0.2f) ? 0 : I(1, 6);
    auto **data = static_cast<uint8_t **>(std::malloc(size_t(n ? n : 1) * 4));
    for (int k = 0; k < n; ++k) {
        int type = P(0.15f) ? (P(0.5f) ? 5 : I(24, 31)) : I(0, 23);
        const int cls = type == 5 || type > 23 ? (P(0.5f) ? 19 : 7) : type;
        S_ControllerMessage *m = NewPortMessage(cls);
        auto *pb = reinterpret_cast<uint8_t *>(m);
        auto *ob = static_cast<uint8_t *>(_aligned_malloc(0x100, 16));
        std::memset(ob, 0, 0x100);
        const uint32_t vt = kOrigMsgVt[cls];
        std::memcpy(ob, &vt, 4);
        m->type = type;
        std::memcpy(ob + 4, &type, 4);
        for (const S_AiMsgField &f : AiMsgFields(cls)) {
            uint32_t w = f.kind == 'b' ? uint32_t(P(0.5f)) : f.kind == 'f' ? (uint32_t(std::rand()) << 17) ^ uint32_t(std::rand()) : uint32_t(I(-3, 2000));
            std::memcpy(pb + f.port, &w, f.kind == 'b' ? 1 : 4);
            std::memcpy(ob + f.orig, &w, f.kind == 'b' ? 1 : 4);
        }
        port.push_back(m);
        data[k] = ob;
    }
    uint32_t v[4] = {0, 0, 0, 0};
    if (n) {
        v[1] = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(data));
        v[2] = v[3] = v[1] + 4 * uint32_t(n);
    } else
        std::free(data);
    std::memcpy(vec, v, 16);
}
// Input for OnMessage / OnMessage2: messages in the fakes' format, now and then types the switch skips, the end.
void RandomAiInput(C_BitStream &w, bool server)
{
    static const int CLIENT_TYPES[] = {0, 1, 2, 3, 4, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 22, 23, 5, 21, 25};
    static const int SERVER_TYPES[] = {7, 8, 10, 11, 7, 8, 10, 11, 0, 5, 19};
    for (int k = I(0, 5); k > 0; --k) {
        const int type = server ? SERVER_TYPES[I(0, 10)] : CLIENT_TYPES[I(0, 24)];
        w.WriteBits(uint32_t(type), 5);
        const bool known = server ? (type == 7 || type == 8 || type == 10 || type == 11) : !(type == 5 || type == 21 || type > 23);
        if (!known)
            continue;
        for (const S_AiMsgField &f : AiMsgFields(type)) {
            if (f.kind == 'f') {
                const float x = U(-50, 50);
                uint32_t b;
                std::memcpy(&b, &x, 4);
                w.WriteBits(b, 32);
            } else if (f.kind == 'b') {
                w.WriteBits(P(0.5f), 1);
            } else {
                // (Ids of part 23's actors now and then: 100 + a kind.)
                w.WriteBits(uint32_t(P(0.3f) ? 100 + I(0, KIND_HUMAN) : I(0, 0xFFFF)) & 0xFFFF, 16);
            }
        }
    }
    w.WriteBits(CTRL_MSG_END, 5);
}

void TestAiNetwork()
{
    std::printf("ai controller: network\n");
    C_Human *h = g_h.human;
    for (int i = 0; i < 12000 / QUICK; ++i) {
        RandomAi();
        static const uint32_t ROLES[] = {GAME_ROLE_LOCAL, GAME_ROLE_SERVER, GAME_ROLE_SERVER, GAME_ROLE_CLIENT, GAME_ROLE_CLIENT};
        g_gameRole = ROLES[I(0, 4)];
        g_pa->m_netObject = reinterpret_cast<C_NetObject *>(g_netObjectBytes);
        // (A speech set, part 23d's: the original's SayNow calls through a missing one, the port's fix does not.)
        h->m_speechSet = reinterpret_cast<C_SpeechSet *>(g_speechSetBytes2);
        g_cs.speech = P(0.5f) ? I(0, 40000) : int(uint32_t(I(0, 0x7FFFFFFF)));
        const int which = I(0, 4);
        switch (which) {
        case 0: {
            // (Co-operative games, or another type; in a seat or not; the dive's conditions.)
            const int gameType = P(0.7f) ? MP_GAME_COOPERATIVE : I(0, 5);
            g_mpSettings->gameType = gameType;
            std::memcpy(g_mpBlock + 0x224, &gameType, 4);
            if (g_pa->m_buttons && P(0.5f))
                g_pa->m_buttons[BUTTON_DIVE] = 1.0f;
            h->m_stamina = P(0.5f) ? U(0, 9.9f) : P(0.2f) ? 10.0f : U(10.1f, 100);
            const int frameTime = I(0, 200);
            AiCall("UpdateNetwork", [&] { g_pa->UpdateNetwork(frameTime); return 0; },
                   [&](void *) { OAi<void>(0x0056f710, frameTime); return 0; });
            break;
        }
        case 1:
        case 2: {
            const bool toClients = which == 1;
            RandomAiQueue(toClients ? g_pa->m_toClients : g_pa->m_toServer, g_oa + (toClients ? 0x90 : 0x80));
            AiCall("FlushMessages", [&] { g_pa->FlushMessages(toClients); return 0; },
                   [&](void *) { OAi<void>(0x00577b30, toClients); return 0; });
            break;
        }
        default: {
            const bool server = which == 4;
            uint32_t input[128] = {};
            {
                C_BitStream w(input, sizeof input * 8);
                RandomAiInput(w, server);
            }
            S_AiStream ps, os;
            std::memcpy(ps.words, input, sizeof input < sizeof ps.words ? sizeof input : sizeof ps.words);
            std::memcpy(os.words, ps.words, sizeof ps.words);
            ps.msg.Attach(ps.words, sizeof ps.words * 8);
            os.msg.Attach(os.words, sizeof os.words * 8);
            AiCall(server ? "OnMessage2" : "OnMessage",
                   [&] {
                       if (server)
                           g_pa->OnMessage2(&ps.msg);
                       else
                           g_pa->OnMessage(&ps.msg);
                       return int(ps.msg.BitPosition());
                   },
                   [&](void *) {
                       OAi<void>(server ? 0x00578190 : 0x00577ce0, &os.msg);
                       return int(os.msg.BitPosition());
                   });
            break;
        }
        }
    }
}

void TestAiController()
{
    SetUpAi();
    SetUpAiNetwork();
    TestAiLife();
    TestAiSaves();
    TestAiDestructor();
    TestAiNetwork();
}
}  // namespace

// ---- part 33b's port side: the messages' Read and Write (fakes of the test's format), the actor messages' streams,
// the net object's Tick, the property's direction track, ApplyRemoteHit (wrapped)
#define AI_MSG_FAKES(T) \
    void T::Read(C_NetMessage &msg) { FakeMsgRead(reinterpret_cast<uint8_t *>(this), type, &msg, false); } \
    void T::Write(C_NetMessage &msg) { FakeMsgWrite(reinterpret_cast<uint8_t *>(this), &msg, false); }
AI_MSG_FAKES(S_MsgControls)
AI_MSG_FAKES(S_MsgHit)
AI_MSG_FAKES(S_MsgPutOnUniform)
AI_MSG_FAKES(S_MsgCommand)
AI_MSG_FAKES(S_MsgControlState)
AI_MSG_FAKES(S_MsgUseObject)
AI_MSG_FAKES(S_MsgPickUpItem)
AI_MSG_FAKES(S_MsgTakeFromBox)
AI_MSG_FAKES(S_MsgAction)
AI_MSG_FAKES(S_MsgInventoryCheck)
AI_MSG_FAKES(S_MsgHeal)
AI_MSG_FAKES(S_MsgSoldierClass)
AI_MSG_FAKES(S_Msg17)
AI_MSG_FAKES(S_MsgAnimation)
AI_MSG_FAKES(S_MsgEquipment)
AI_MSG_FAKES(S_Msg21)
AI_MSG_FAKES(S_MsgSuspend)
extern "C" {
// (Part 23's six raw vtables: their Read through these wraps; the type is the object's.)
void AC AiWrappedRead(uint8_t *m, C_NetMessage *msg)
{
    int type;
    std::memcpy(&type, m + 4, 4);
    FakeMsgRead(m, type, msg, false);
}
void AC __wrap__ZN13S_MsgMoveItem4ReadER12C_NetMessage(uint8_t *m, C_NetMessage *msg) { AiWrappedRead(m, msg); }
void AC __wrap__ZN17S_MsgTransferItem4ReadER12C_NetMessage(uint8_t *m, C_NetMessage *msg) { AiWrappedRead(m, msg); }
void AC __wrap__ZN13S_MsgDropItem4ReadER12C_NetMessage(uint8_t *m, C_NetMessage *msg) { AiWrappedRead(m, msg); }
void AC __wrap__ZN18S_MsgRemoteRequest4ReadER12C_NetMessage(uint8_t *m, C_NetMessage *msg) { AiWrappedRead(m, msg); }
void AC __wrap__ZN7S_Msg234ReadER12C_NetMessage(uint8_t *m, C_NetMessage *msg) { AiWrappedRead(m, msg); }
void AC __wrap__ZN8S_MsgSay4ReadER12C_NetMessage(uint8_t *m, C_NetMessage *msg) { AiWrappedRead(m, msg); }
void AC __wrap__ZN7C_Human14ApplyRemoteHitEiiiiRK8S_vectorS2_(C_Human *h, int a, int b, int c, int d, const S_vector &v1, const S_vector &v2)
{
    OS_ApplyRemoteHit(h, a, b, c, d, &v1.x, &v2.x);
}
}
C_BitStream *CreateActorMessage(int id, bool reliable) { return NewAiStream(id, reliable, false); }
C_BitStream *CreateActorMessageEx(int id, bool reliable) { return NewAiStream(id, reliable, true); }
void SendActorMessage(C_BitStream *stream) { SendAiStream(stream); }
void C_NetObject::Tick(float seconds) { Log(this, "NetObjectTick", {seconds}); }
C_NetDirTrack &C_NetProperty::GetDirTrack() { return *reinterpret_cast<C_NetDirTrack *>(g_aiDirTrackPort); }

