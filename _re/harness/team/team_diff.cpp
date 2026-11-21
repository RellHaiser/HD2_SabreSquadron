// Human part 77: the teams (game/team.cpp) against the original's (0x0045d100 .. 0x00462310). MinGW; build_team.bat;
// runs team_diff.exe ("team_diff: N checks, 0 failures"). team.cpp is compiled into this file. What the teams reach
// outside themselves is faked on both sides with the same loggers: the interface kit's frames (the members' models;
// EnumFrames hands out their collision volumes), the game's methods (team_fakes.inc, gen_ifakes.py), by hand the game,
// the camera module, the actors module (its actors, GetActorById, GetHuman), the squad commander, the soldiers (their
// fields: type, model, health, vehicle and seat, role, fade, net and sight ids, the enemies they see, the controller;
// their controller type and environment flags by the soldier asked), the vehicles (GetSeatFlags), the controllers
// (GetCombatOrder), the mission's objectives, the profile; the chunks real on both sides (common/chunk.cpp, the
// original's own) over data files in memory.
// Part 77a (the "Team" case): the squad (C_Team), the co-operative team (C_CoopTeam), their members (C_TeamBase) and
// enemy records (C_EnemyRecords): each scenario builds both teams (the original's by its constructor), runs up to 20
// operations (the members added and removed, the selection, the commanded member, switching, the camera target, the
// volumes, the deaths, the environment, the items, the squad's reactions, the enemies seen and forgotten, the tactical
// view, saves compared byte by byte, loads from files the world builds), the world changing between them, and destroys
// them; the logs (every call out, the teams' fields after each operation) and the arena compared.
// HD2_TEAM_ROUNDS: scenarios (default 6,000; HD2_TEAM_QUICK: 2,500); HD2_INTERFACE_ULPS, HD2_INTERFACE_SHOW,
// HD2_INTERFACE_ECHO, HD2_INTERFACE_FIRST_CHANCE: the kit's.
#include <windows.h>

#include <algorithm>
#include <climits>
#include <cmath>
#include <cstdarg>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <random>
#include <string>
#include <vector>

#define INTERFACE_FRAMES 64
#define protected public
#define private public
#include "game/team.cpp"
#include "game/campaign.h"
#include "game/camera_module.h"
#include "game/car.h"
#include "game/profile.h"
#include "game/sight.h"
#include "ls3d/rw_data.h"
#undef private
#undef protected

#include "original_exe.h"

#include "../interface/interface_kit.inc"

namespace H {

int g_side;
template <class T> void Put(void *b, uint32_t o, T v) { std::memcpy(static_cast<uint8_t *>(b) + o, &v, sizeof v); }
template <class T> T At(const void *b, uint32_t o)
{
    T v;
    std::memcpy(&v, static_cast<const uint8_t *>(b) + o, sizeof v);
    return v;
}
uint32_t Fnv(const char *s, uint32_t h)
{
    for (; *s; ++s)
        h = (h ^ uint8_t(*s)) * 16777619u;
    return h;
}
template <class M> int Slot(M m)   // a port virtual's byte offset in its vtable (g++: the member pointer holds 1 + it)
{
    struct {
        uintptr_t p;
        ptrdiff_t adjust;
    } r;
    static_assert(sizeof r == sizeof m, "member pointer");
    std::memcpy(&r, &m, sizeof r);
    return int(r.p - 1);
}

// ---- the frames: f0..f7 the soldiers' models, f8.. the pool (their collision volumes) ----
constexpr int F_BODY = 0, F_POOL = 8;

// ---- the soldiers (h0..h9), the vehicles (v0..v2), the controllers (c0..c9), the items, the objectives ----
constexpr int HUMANS = 10, VEHICLES = 3, OBJECTIVES = 4;
alignas(16) uint8_t g_humans[HUMANS][0x2000], g_vehicles[2][VEHICLES][0x2600], g_controllers[2][HUMANS][0x40], g_item[0x40],
    g_objectives[OBJECTIVES][0x40];
void *g_vehicleVt[2][0x80], *g_controllerVt[2][0x80];
C_Human *g_seen[HUMANS][HUMANS];   // the enemies each soldier sees (its vector's data)
void *Hum(int i) { return i < 0 || i >= HUMANS ? nullptr : g_humans[i]; }
int HumIndex(const void *p)
{
    for (int i = 0; i < HUMANS; ++i)
        if (p >= g_humans[i] && p < g_humans[i] + sizeof g_humans[i])
            return i;
    return -1;
}

// ---- the game and its objects (both sides' blocks where the layouts differ) ----
alignas(16) uint8_t g_origGame[0x200], g_portGame[0x800], g_cameraModule[2][0x100], g_ui[0x100], g_fpv[0x100], g_commander[2][0x100],
    g_mission[2][0x100], g_origActors[0x100];
void *g_origCameraVt[0x40], *g_commanderVt[2][0x40];

// ---- the answers ----
uint32_t g_retSeed;
std::vector<std::pair<std::string, int>> g_retCounts;
uint32_t RetWord(const char *name)
{
    int count = 0;
    for (auto &c : g_retCounts)
        if (c.first == name) {
            count = ++c.second;
            break;
        }
    if (!count)
        g_retCounts.emplace_back(name, count = 1);
    uint32_t h = Fnv(name, g_retSeed) ^ uint32_t(count) * 2654435761u;
    h ^= h >> 15;
    h *= 2246822519u;
    h ^= h >> 13;
    return h;
}

// ---- the world ----
struct S_World {
    uint32_t role;
    int difficulty;
    bool coop;
    // The soldiers: type, health, vehicle (-1 none), seat, role, faded out, net id, sight ids, the enemies they see (a
    // bit per soldier), controller type, its combat order, environment flags, the model's place and volumes.
    int type[HUMANS], health[HUMANS], vehicle[HUMANS], seat[HUMANS], humanRole[HUMANS], netId[HUMANS], sightId[HUMANS],
        sightTarget[HUMANS], controllerType[HUMANS], combatOrder[HUMANS], volumes[HUMANS];
    bool faded[HUMANS], hasController[HUMANS];
    uint32_t seen[HUMANS], env[HUMANS];
    float pos[HUMANS][3];
    int vehicleType[VEHICLES], unknown550[VEHICLES];
    uint32_t seatFlags[VEHICLES][4];
    // The mission (-1: none) and its objectives' statuses.
    int objectives;
    int status[OBJECTIVES];
    // The actors module's actors: the soldiers then nulls.
    int actorCount;
    bool nullActor[HUMANS];
};
S_World g_w, g_wStart;
const int TYPES[] = {ACTOR_HUMAN, ACTOR_HUMAN, ACTOR_HUMAN, ACTOR_CAR};

std::string PartName(const void *p);
void *RetPtrHook(const char *name, uint32_t w);
template <class T> T Ret(const char *name);
template <> bool Ret<bool>(const char *name)
{
    const uint32_t w = RetWord(name);
    // (Unconscious and spectating now and then: the records' losses and the selection need them false.)
    if (std::strcmp(name, "C_Human::IsUnconscious") == 0 || std::strcmp(name, "C_Human::IsSpectating") == 0)
        return (w & 3) == 0;
    return (w & 3) != 0;
}
template <> int Ret<int>(const char *name)
{
    const uint32_t w = RetWord(name);
    return (w & 3) == 0 ? -1 : int(w >> 8 & 7);
}
template <> float Ret<float>(const char *name)
{
    const uint32_t w = RetWord(name);
    return (w & 3) == 0 ? 0.0f : float(w >> 8 & 0xFF) / 64.0f;
}
template <> void *Ret<void *>(const char *name) { return RetPtrHook(name, RetWord(name)); }
template <> const char *Ret<const char *>(const char *) { return "Sgt. Text"; }
std::string F3(const float *v) { return FloatText(v[0]) + " " + FloatText(v[1]) + " " + FloatText(v[2]); }

}  // namespace H

#include "team_fakes.inc"
#define IFAKES_PORT
#include "team_fakes.inc"
#undef IFAKES_PORT

namespace H {

void *RetPtrHook(const char *name, uint32_t w)
{
    if (std::strcmp(name, "C_Human::FindItem") == 0 || std::strcmp(name, "C_Human::GetMedkit") == 0)
        return (w & 3) == 0 ? static_cast<void *>(g_item) : nullptr;
    return nullptr;
}

// ---- names ----
std::string PartName(const void *p)
{
    const int h = HumIndex(p);
    if (h >= 0)
        return p == g_humans[h] ? "h" + std::to_string(h) : "h" + std::to_string(h) + "+" + std::to_string(static_cast<const uint8_t *>(p) - g_humans[h]);
    for (int side = 0; side < 2; ++side) {
        for (int k = 0; k < VEHICLES; ++k)
            if (p == g_vehicles[side][k])
                return "v" + std::to_string(k);
        for (int k = 0; k < HUMANS; ++k)
            if (p == g_controllers[side][k])
                return "c" + std::to_string(k);
        if (p == g_cameraModule[side])
            return "cameraModule";
        if (p == g_commander[side])
            return "commander";
        if (p == g_mission[side])
            return "mission";
    }
    if (p == g_item)
        return "item";
    if (p == g_origGame || p == g_portGame)
        return "game";
    if (p == g_ui)
        return "ui";
    if (p == g_fpv)
        return "fpv";
    if (p == g_origActors || p == &g_actorsModule)
        return "actors";
    if (p == OriginalAt<void *>(0x0089b288) || p == &g_sightModule)
        return "sight";
    return "";
}

// ---- the soldiers' answers by the soldier asked; the actors module's ----
int ControllerType(const void *h)
{
    const int k = HumIndex(h);
    const int t = k < 0 ? 0 : g_w.controllerType[k];
    Log("%s C_Human::GetControllerType -> %d", Name(h).c_str(), t);
    return t;
}
uint32_t EnvFlags(const void *h)
{
    const int k = HumIndex(h);
    const uint32_t f = k < 0 ? 0 : g_w.env[k];
    Log("%s C_Human::GetEnvironmentFlags -> %x", Name(h).c_str(), f);
    return f;
}
void *ActorById(int id)
{
    void *a = nullptr;
    for (int k = 0; k < HUMANS && !a; ++k)
        if (g_w.netId[k] == id)
            a = g_humans[k];
    Log("actors GetActorById %d -> %s", id, Name(a).c_str());
    return a;
}
void *HumanAt(int id)
{
    void *a = id >= 0 && id < HUMANS && !g_w.nullActor[id] ? static_cast<void *>(g_humans[id]) : nullptr;
    Log("actors GetHuman %d -> %s", id, Name(a).c_str());
    return a;
}
int __attribute__((thiscall)) OH_ControllerType(void *h) { return ControllerType(h); }
uint32_t __attribute__((thiscall)) OH_EnvFlags(void *h) { return EnvFlags(h); }
void *__attribute__((thiscall)) O_ActorById(void *, int id) { return ActorById(id); }
void *__attribute__((thiscall)) O_HumanAt(void *, int id) { return HumanAt(id); }
uint32_t __attribute__((thiscall)) V_SeatFlags(void *v, int seat)
{
    uint32_t f = 0;
    for (int k = 0; k < VEHICLES; ++k)
        if (v == g_vehicles[g_side][k])
            f = seat >= 0 && seat < 4 ? g_w.seatFlags[k][seat] : 0;
    Log("%s GetSeatFlags %d -> %x", Name(v).c_str(), seat, f);
    return f;
}
int __attribute__((thiscall)) V_CombatOrder(void *c)
{
    int o = 0;
    for (int k = 0; k < HUMANS; ++k)
        if (c == g_controllers[g_side][k])
            o = g_w.combatOrder[k];
    Log("%s GetCombatOrder -> %x", Name(c).c_str(), o);
    return o;
}
void __attribute__((thiscall)) V_Other(void *self) { Log("%s other slot", Name(self).c_str()); }

// ---- the camera module (SelectView: the original's through its vtable), the game, the squad commander ----
void *__cdecl O_GetCameraModule() { return g_cameraModule[0]; }
void *__cdecl O_GetGame() { return g_origGame; }
void CommanderReset(const void *c) { Log("%s Reset", Name(c).c_str()); }
void CommanderTick(const void *c, int dt) { Log("%s Tick %d", Name(c).c_str(), dt); }
void __attribute__((thiscall)) O_CommanderReset(void *c) { CommanderReset(c); }
void __attribute__((thiscall)) O_CommanderTick(void *c, int dt) { CommanderTick(c, dt); }
void *__cdecl O_GetSquadCommander() { return g_commander[0]; }
void PrintText(const char *format, va_list args)
{
    char text[400];
    std::vsnprintf(text, sizeof text, format, args);
    Log("game Print %s", text);
}
void __cdecl O_Print(void *, const char *format, ...)
{
    va_list args;
    va_start(args, format);
    PrintText(format, args);
    va_end(args);
}
void __cdecl O_PrintColored(void *, const float *color, const char *format, ...)
{
    Log("game PrintColored %s", F3(color).c_str());
    va_list args;
    va_start(args, format);
    PrintText(format, args);
    va_end(args);
}
int __attribute__((thiscall)) O_Difficulty(void *)
{
    Log("profile GetDifficulty -> %d", g_w.difficulty);
    return g_w.difficulty;
}
bool __attribute__((thiscall)) O_Coop(void *)
{
    Log("profile IsCooperativeGame -> %d", g_w.coop);
    return g_w.coop;
}
// EnumFrames (0x44): the callback run on the member's volumes (pool frames by the model).
void __stdcall V_EnumFrames(void *self, uint32_t(__stdcall *proc)(void *, void *), void *context, uint32_t typeMask, const char *mask)
{
    const int i = FrameIndex(self);
    Log("%s EnumFrames %x %s", Name(self).c_str(), typeMask, mask ? mask : "null");
    const int n = i >= F_BODY && i < F_POOL ? g_w.volumes[i - F_BODY] : 0;
    for (int k = 0; k < n; ++k)
        proc(FrameAt(F_POOL + (i * 7 + k * 3) % (FRAMES - F_POOL)), context);
}
// The original's aligned allocations (its CRT's: not started here).
void *__cdecl O_AlignedMalloc(size_t size, size_t alignment) { return _aligned_malloc(size, alignment); }
void __cdecl O_AlignedFree(void *p) { _aligned_free(p); }
// The original's sprintf (its CRT's locks are not set up).
int __cdecl O_Sprintf(char *buffer, const char *format, ...)
{
    va_list args;
    va_start(args, format);
    const int n = std::vsprintf(buffer, format, args);
    va_end(args);
    return n;
}

}  // namespace H

// ---- the port's side of the hand-written fakes ----
C_game *GetGame() { return reinterpret_cast<C_game *>(H::g_portGame); }
C_CameraModule *GetCameraModule() { return reinterpret_cast<C_CameraModule *>(H::g_cameraModule[1]); }
C_SquadCommander *GetSquadCommander() { return reinterpret_cast<C_SquadCommander *>(H::g_commander[1]); }
int C_Human::GetControllerType() const { return H::ControllerType(this); }
uint32_t C_Human::GetEnvironmentFlags() const { return H::EnvFlags(this); }
C_Actor *C_ActorsModule::GetActorById(int id) const { return static_cast<C_Actor *>(H::ActorById(id)); }
C_Actor *C_ActorsModule::GetHuman(int id) const { return static_cast<C_Actor *>(H::HumanAt(id)); }
void C_game::Print(const char *format, ...)
{
    va_list args;
    va_start(args, format);
    H::PrintText(format, args);
    va_end(args);
}
void C_game::PrintColored(const S_vector &color, const char *format, ...)
{
    H::Log("game PrintColored %s", H::VecText(color).c_str());
    va_list args;
    va_start(args, format);
    H::PrintText(format, args);
    va_end(args);
}
int C_GameProfile::GetDifficulty() const { return H::O_Difficulty(nullptr); }
bool C_GameProfile::IsCooperativeGame() const { return H::O_Coop(nullptr); }
// (game_menu.cpp's, not compiled here; the original inlines it: the same reads.)
bool C_MissionDef::AreObjectivesDecided() const
{
    if (AreObjectivesInactive())
        return false;
    for (const S_MissionObjective *objective : m_objectives) {
        if (objective->status == OBJECTIVE_OPEN)
            return false;
    }
    return true;
}
// The formation shape's constructor and destructor (formation.cpp, not compiled here): logged; the fields the teams
// read inline (the leader +0x74, the scripted flag +0x8C) cleared on both sides.
namespace H {
void ShapeNew(void *shape)
{
    Log("shape construct");
    Put<void *>(shape, 0x74, nullptr);
    Put<bool>(shape, 0x8C, false);
}
void *__attribute__((fastcall)) O_ShapeNew(void *shape)
{
    std::memset(shape, 0, 0xA0);
    ShapeNew(shape);
    return shape;
}
void __attribute__((fastcall)) O_ShapeDelete(void *) { Log("shape destroy"); }
}  // namespace H
C_FormationShape::C_FormationShape()
{
    static_assert(offsetof(C_FormationShape, m_leader) == 0x74 && offsetof(C_FormationShape, m_scripted) == 0x8C, "shape layout");
    H::ShapeNew(this);
}
C_FormationShape::~C_FormationShape() { H::Log("shape destroy"); }
// The base module (module.cpp, not compiled here).
C_Module *g_modules[MODULE_COUNT];
C_Module::C_Module(int id, const char *name) : m_name(name), m_id(id) {}
C_Module::~C_Module() {}
// The data files in memory (both sides).
namespace H {
std::map<std::string, std::string> g_files;
struct S_OpenFile {
    std::string name;
    size_t at = 0;
};
std::map<int, S_OpenFile> g_openFiles;
int g_nextFile = 1;
int FileOpen(const char *name, bool write)
{
    if (!write && !g_files.count(name)) {
        Log("dta open %s -> -1", name);
        return -1;
    }
    if (write)
        g_files[name].clear();
    g_openFiles[g_nextFile] = S_OpenFile{name, 0};
    Log("dta open %s %d -> %d", name, write, g_nextFile);
    return g_nextFile++;
}
int FileSeek(int handle, int offset, int origin)
{
    S_OpenFile &o = g_openFiles[handle];
    const std::string &f = g_files[o.name];
    o.at = origin == 0 ? size_t(offset) : origin == 1 ? o.at + offset : f.size() + offset;
    return int(o.at);
}
int FileRead(int handle, void *data, int size)
{
    S_OpenFile &o = g_openFiles[handle];
    const std::string &f = g_files[o.name];
    const int got = o.at >= f.size() ? 0 : std::max(0, std::min(int(f.size() - o.at), size));
    if (got)
        std::memcpy(data, f.data() + o.at, size_t(got));
    o.at += size_t(got);
    return got;
}
int FileWrite(int handle, const void *data, int size)
{
    S_OpenFile &o = g_openFiles[handle];
    std::string &f = g_files[o.name];
    if (o.at + size_t(size) > f.size())
        f.resize(o.at + size_t(size));
    std::memcpy(&f[o.at], data, size_t(size));
    o.at += size_t(size);
    return size;
}
bool FileClose(int handle)
{
    Log("dta close %d", handle);
    return g_openFiles.erase(handle) != 0;
}
int __stdcall O_dtaOpen(const char *name, bool) { return FileOpen(name, false); }
int __stdcall O_dtaOpenWrite(const char *name, int) { return FileOpen(name, true); }
int __stdcall O_dtaSeek(int h, int offset, int origin) { return FileSeek(h, offset, origin); }
int __stdcall O_dtaRead(int h, void *data, int size) { return FileRead(h, data, size); }
int __stdcall O_dtaWrite(int h, const void *data, int size) { return FileWrite(h, data, size); }
bool __stdcall O_dtaClose(int h) { return FileClose(h); }
}  // namespace H
int RW_DATA_CALL dtaOpen(const char *name, bool) { return H::FileOpen(name, false); }
int RW_DATA_CALL dtaOpenWrite(const char *name, int) { return H::FileOpen(name, true); }
int RW_DATA_CALL dtaSeek(int h, int offset, int origin) { return H::FileSeek(h, offset, origin); }
int RW_DATA_CALL dtaRead(int h, void *data, int size) { return H::FileRead(h, data, size); }
int RW_DATA_CALL dtaWrite(int h, const void *data, int size) { return H::FileWrite(h, data, size); }
bool RW_DATA_CALL dtaClose(int h) { return H::FileClose(h); }

namespace H {

// ---- each side's world: the soldiers' fields, the vehicles, the controllers, the actors, the game, the mission ----
// (An MSVC6 vector {allocator, first, last, end}; a g++ one {begin, end, capacity}.)
void PutVector(void *b, uint32_t at, int side, void *first, int count, uint32_t elem = 4)
{
    uint8_t *f = static_cast<uint8_t *>(first), *l = f ? f + elem * uint32_t(count) : nullptr;
    if (side) {
        Put<uint8_t *>(b, at, f);
        Put<uint8_t *>(b, at + 4, l);
        Put<uint8_t *>(b, at + 8, l);
    } else {
        Put<uint8_t *>(b, at + 4, f);
        Put<uint8_t *>(b, at + 8, l);
        Put<uint8_t *>(b, at + 12, l);
    }
}
void SetWorld(int side)
{
    g_gameRole = g_w.role;
    *OriginalAt<uint32_t *>(0x00982100) = g_w.role;
    for (int h = 0; h < HUMANS; ++h) {
        uint8_t *b = g_humans[h];
        std::memset(b, 0, sizeof g_humans[h]);
        I3D_frame *frame = FrameAt(F_BODY + h % (F_POOL - F_BODY));
        void *vehicle = g_w.vehicle[h] < 0 ? nullptr : static_cast<void *>(g_vehicles[side][g_w.vehicle[h]]);
        void *controller = g_w.hasController[h] ? static_cast<void *>(g_controllers[side][h]) : nullptr;
        int n = 0;
        for (int k = 0; k < HUMANS; ++k)
            if (g_w.seen[h] >> k & 1)
                g_seen[h][n++] = static_cast<C_Human *>(Hum(k));
        auto o = [side](uint32_t orig, size_t port) { return side ? uint32_t(port) : orig; };
        Put<int>(b, o(0x10, offsetof(C_Actor, m_netId)), g_w.netId[h]);
        Put<int>(b, o(0x18, offsetof(C_Actor, m_type)), g_w.type[h]);
        Put<void *>(b, o(0x20, offsetof(C_Actor, m_frame)), frame);
        Put<void *>(b, o(0xB0, offsetof(C_Human, m_controller)), controller);
        Put<int>(b, o(0xEC, offsetof(C_Human, m_stats) + 4 * STAT_HEALTH), g_w.health[h]);
        Put<bool>(b, o(0x150, offsetof(C_Human, m_fadedOut)), g_w.faded[h]);
        Put<void *>(b, o(0x160, offsetof(C_Human, m_vehicle)), vehicle);
        Put<int>(b, o(0x164, offsetof(C_Human, m_seat)), g_w.seat[h]);
        Put<int>(b, o(0x19C, offsetof(C_Human, m_role)), g_w.humanRole[h]);
        Put<int>(b, o(0x258, offsetof(C_Human, m_sightId)), g_w.sightId[h]);
        Put<int>(b, o(0x25C, offsetof(C_Human, m_sightTargetId)), g_w.sightTarget[h]);
        PutVector(b, o(0x264, offsetof(C_Human, m_seenEnemies)), side, n ? g_seen[h] : nullptr, n);
        // The model's place (its world matrix taken as made).
        if (h < F_POOL - F_BODY) {
            frame->m_frameFlags |= FRMFLAGS_WMATRIX_VALID;
            frame->m_worldMatrix(3) = S_vector(g_w.pos[h][0], g_w.pos[h][1], g_w.pos[h][2]);
        }
    }
    for (int k = 0; k < VEHICLES; ++k) {
        uint8_t *v = g_vehicles[side][k];
        std::memset(v, 0, sizeof g_vehicles[side][k]);
        *reinterpret_cast<void ***>(v) = g_vehicleVt[side];
        Put<int>(v, side ? uint32_t(offsetof(C_Actor, m_type)) : 0x18, g_w.vehicleType[k]);
        Put<int>(v, side ? uint32_t(offsetof(C_Car, m_vehicle) + offsetof(C_Vehicle, m_unknown550)) : 0x5E0, g_w.unknown550[k]);
    }
    for (int k = 0; k < HUMANS; ++k) {
        std::memset(g_controllers[side][k], 0, 0x40);
        *reinterpret_cast<void ***>(g_controllers[side][k]) = g_controllerVt[side];
    }
    // The actors: the soldiers (some null), the original's at 0x0089b4b0 (its vector at +0x0C), the port's.
    static void *actors[2][HUMANS];
    for (int k = 0; k < HUMANS; ++k)
        actors[side][k] = g_w.nullActor[k] ? nullptr : static_cast<void *>(g_humans[k]);
    if (side)
        PutVector(&g_actorsModule, uint32_t(offsetof(C_ActorsModule, m_actors)), 1, actors[1], g_w.actorCount);
    else
        PutVector(OriginalAt<void *>(0x0089b4b0), 0x0C, 0, actors[0], g_w.actorCount);
    // The mission's objectives (the port's S_MissionObjective and the original's: status +0x10).
    static void *objectives[OBJECTIVES];
    for (int k = 0; k < OBJECTIVES; ++k) {
        std::memset(g_objectives[k], 0, 0x40);
        Put<int>(g_objectives[k], side ? uint32_t(offsetof(S_MissionObjective, status)) : 0x10, g_w.status[k]);
        objectives[k] = g_objectives[k];
    }
    uint8_t *mission = g_mission[side];
    std::memset(mission, 0, 0x100);
    PutVector(mission, side ? uint32_t(offsetof(C_MissionDef, m_objectives)) : 0x30, side, g_w.objectives > 0 ? objectives : nullptr,
              std::max(0, g_w.objectives));
    // The game.
    uint8_t *game = side ? g_portGame : g_origGame;
    Put<void *>(game, side ? uint32_t(offsetof(C_game, m_interface)) : 0x58, g_ui);
    Put<void *>(game, side ? uint32_t(offsetof(C_game, m_firstPersonView)) : 0x5C, g_fpv);
    Put<void *>(game, side ? uint32_t(offsetof(C_game, m_mission)) : 0xA8, g_w.objectives < 0 ? nullptr : static_cast<void *>(mission));
}
void RandomWorld()
{
    g_w.role = P(0.85) ? 7u : Pick({6u, 2u, 3u, 5u, 1u});
    g_w.difficulty = I(1, 4);
    g_w.coop = P(0.3);
    for (int h = 0; h < HUMANS; ++h) {
        g_w.type[h] = P(0.9) ? int(ACTOR_HUMAN) : int(ACTOR_CAR);
        g_w.health[h] = P(0.8) ? I(1, 100) : Pick({0, -5});
        g_w.vehicle[h] = P(0.7) ? -1 : I(0, VEHICLES - 1);
        g_w.seat[h] = I(0, 3);
        g_w.humanRole[h] = P(0.7) ? int(HUMAN_ROLE_PLAYER) : Pick({int(HUMAN_ROLE_NPC_TEAM), 0, 2, 3});
        g_w.faded[h] = P(0.3);
        g_w.netId[h] = P(0.5) ? h : P(0.5) ? 32 + h : I(0, 40);
        g_w.sightId[h] = I(0, 20);
        g_w.sightTarget[h] = I(0, 20);
        g_w.seen[h] = P(0.4) ? 0u : U32() & ((1u << HUMANS) - 1);
        g_w.hasController[h] = P(0.9);
        // (No controller: no controller type.)
        g_w.controllerType[h] = !g_w.hasController[h] ? 0 : P(0.6) ? int(HUMAN_CONTROLLER_SQUAD) : I(0, 6);
        g_w.combatOrder[h] = P(0.6) ? Pick({int(SQUAD_ORDER_MODE2), int(SQUAD_ORDER_MODE2_ALT)}) : I(0, 0x12);
        g_w.env[h] = P(0.7) ? 0u : Pick({uint32_t(HUMAN_ENV_DEADLY), uint32_t(HUMAN_ENV_RESTRICTED), 0x1u, 0x6000u});
        g_w.volumes[h] = P(0.1) ? 0 : I(1, 4);
        for (float &f : g_w.pos[h])
            f = P(0.3) ? F(-5, 5) : F(-60, 60);
        g_w.nullActor[h] = P(0.1);
    }
    // (Now and then on a line at whole distances: 30 m and 40 m apart exactly, the reach of the squad's reactions.)
    if (P(0.2))
        for (auto &p : g_w.pos) {
            p[0] = Pick({0.0f, 30.0f, 40.0f, 70.0f, -30.0f, -40.0f});
            p[1] = p[2] = 0.0f;
        }
    for (int k = 0; k < VEHICLES; ++k) {
        g_w.vehicleType[k] = Pick({int(ACTOR_CAR), int(ACTOR_CAR), int(ACTOR_SUBMARINE), int(ACTOR_STATIC_WEAPON), int(ACTOR_JUNKERS88), 0});
        g_w.unknown550[k] = P(0.5) ? 0 : I(1, 2);
        for (uint32_t &f : g_w.seatFlags[k])
            f = (P(0.4) ? uint32_t(SEAT_FLAG_HIDDEN) : 0u) | (U32() & 0x0Fu);
    }
    g_w.objectives = P(0.2) ? -1 : I(0, OBJECTIVES);
    for (int &s : g_w.status)
        s = P(0.5) ? Pick({int(OBJECTIVE_OPEN), int(OBJECTIVE_INACTIVE)}) : I(0, 4);
    g_w.actorCount = P(0.8) ? HUMANS : I(0, HUMANS);
}

// ---- the teams: each side's, their fields snapped ----
uint8_t *g_orig, *g_origCoop;
C_Team *g_port;
C_CoopTeam *g_portCoop;
// An MSVC6 vector's elements / a g++ one's.
std::vector<uint32_t> Words(const void *b, uint32_t at, int side, uint32_t elem = 4)
{
    const uint8_t *f = At<const uint8_t *>(b, at + (side ? 0 : 4)), *l = At<const uint8_t *>(b, at + (side ? 4 : 8));
    std::vector<uint32_t> out;
    for (const uint8_t *p = f; p && p < l; p += elem)
        out.push_back(At<uint32_t>(p, 0));
    return out;
}
std::string Names(const std::vector<uint32_t> &words)
{
    std::string s;
    for (uint32_t w : words)
        s += " " + Name(reinterpret_cast<void *>(uintptr_t(w)));
    return s;
}
// The records (C_EnemyRecords at `at`): each record's enemy, seen by, alive, forget time; the flags and counts.
std::string Records(const void *b, uint32_t at, int side)
{
    std::string s = " records";
    for (uint32_t w : Words(b, at, side)) {
        const S_EnemyRecord *r = reinterpret_cast<const S_EnemyRecord *>(uintptr_t(w));
        s += " " + Name(r->enemy) + "/" + std::to_string(r->seenBy) + "/" + std::to_string(r->alive) + "/" + std::to_string(r->forgetTime);
    }
    auto o = [side, at](uint32_t orig, size_t port) { return side ? at + uint32_t(port) : at + orig; };
    char text[100];
    std::snprintf(text, sizeof text, " changed %d alive %d dead %d tactical %d", At<uint8_t>(b, o(0x10, offsetof(C_EnemyRecords, m_changed))),
                  At<int>(b, o(0x14, offsetof(C_EnemyRecords, m_aliveCount))), At<int>(b, o(0x18, offsetof(C_EnemyRecords, m_deadCount))),
                  At<uint8_t>(b, o(0x1C, offsetof(C_EnemyRecords, m_tacticalView))));
    return s + text;
}
// The base: members, name (MSVC6 string {allocator, ptr, length, reserve}), id.
std::string Base(const void *b, int side)
{
    std::string name;
    if (side)
        name = reinterpret_cast<const C_TeamBase *>(b)->m_name;
    else if (const char *p = At<const char *>(b, 0x18))
        name.assign(p, At<uint32_t>(b, 0x1C));
    return "members" + Names(Words(b, side ? uint32_t(offsetof(C_TeamBase, m_members)) : 4, side)) + " name '" + name + "' id " +
           std::to_string(At<int>(b, side ? uint32_t(offsetof(C_TeamBase, m_id)) : 0x24));
}
#define OT(f) uint32_t(offsetof(C_Team, f))
std::string Snap(int side)
{
    const uint8_t *b = side ? reinterpret_cast<const uint8_t *>(g_port) : g_orig;
    auto o = [side](uint32_t orig, uint32_t port) { return side ? port : orig; };
    std::string s = "team " + Base(b, side);
    char text[200];
    std::snprintf(text, sizeof text, " selected %d active %d commanded %d environment %x watched %s", At<int>(b, o(0x28, OT(m_selected))),
                  At<int>(b, o(0x2C, OT(m_active))), At<int>(b, o(0x30, OT(m_commanded))), At<uint32_t>(b, o(0x34, OT(m_environment))),
                  Name(At<void *>(b, o(0x58, OT(m_watchedHuman)))).c_str());
    s += text;
    s += Records(b, o(0x38, OT(m_enemyRecords)), side);
    s += " volumes";
    for (uint32_t w : Words(b, o(0x5C, OT(m_volumes)), side))
        s += " [" + Names(Words(reinterpret_cast<const void *>(uintptr_t(w)), 0, side)) + " ]";
    s += " trackers" + Names(Words(b, o(0xBC, OT(m_seenMeleeTrackers)), side));
    const uint8_t *c = side ? reinterpret_cast<const uint8_t *>(g_portCoop) : g_origCoop;
    s += " | coop " + Base(c, side) + Records(c, side ? uint32_t(offsetof(C_CoopTeam, m_enemyRecords)) : 0x28, side) + " unknown48 " +
         std::to_string(At<int>(c, side ? uint32_t(offsetof(C_CoopTeam, m_unknown48)) : 0x48));
    return s;
}

// ---- the files a load reads: the team's chunks (C_Team: 0xDFB { 1000 { 0x9A4 { 1000 { count } ids, 2000 { name } } } })
std::string MakeLoadFile(bool coop)
{
    auto chunk = [](uint16_t id, const std::string &payload) {
        std::string c;
        const uint32_t size = uint32_t(6 + payload.size());
        c.append(reinterpret_cast<const char *>(&id), 2);
        c.append(reinterpret_cast<const char *>(&size), 4);
        return c + payload;
    };
    auto int4 = [](int x) { return std::string(reinterpret_cast<const char *>(&x), 4); };
    std::string base;
    for (int n = I(0, 3); n > 0; --n) {
        switch (I(0, 3)) {
        case 0:
        case 1: {
            const int count = I(0, 4);
            base += chunk(1000, int4(count));
            for (int k = 0; k < count; ++k)
                base += int4(P(0.8) ? g_w.netId[I(0, HUMANS - 1)] : I(-1, 50));
            break;
        }
        case 2:
            // (Always ended: the original reads the name into a buffer it does not end; its saves always end it.)
            base += chunk(2000, std::string(P(0.5) ? "Squad" : "Alpha") + std::string(1, '\0'));
            break;
        default:
            base += chunk(uint16_t(I(3000, 3002)), int4(I(0, 9)));
            break;
        }
    }
    std::string f = chunk(uint16_t(P(0.9) ? 0x9A4 : 0x9A5), base);
    if (!coop) {
        std::string team;
        if (P(0.8))
            team += chunk(1000, f);
        if (P(0.3))
            team += chunk(1001, int4(7));
        f = chunk(0xDFB, team);
    }
    // (Now and then the next module's chunk after it; now and then nothing at all: the first Descend fails.)
    if (P(0.5))
        f += chunk(0x7777, int4(1));
    return P(0.05) ? std::string() : f;
}

// ---- the scenarios ----
enum {
    OP_CLEAR, OP_ADD, OP_REMOVE, OP_NPC, OP_UPDATE, OP_TICK, OP_RESTART, OP_ACTIVE, OP_SELECTED, OP_COMMANDED, OP_SELECT, OP_SWITCH,
    OP_NEXT, OP_PREVIOUS, OP_COMMAND_INDEX, OP_COMMAND, OP_RETURN, OP_CAMERA, OP_WATCH, OP_VOLUMES, OP_RANDOM_VOLUME, OP_DIED,
    OP_HAS_ITEM, OP_MEDKIT, OP_GRENADE, OP_HEAL, OP_RELEASE, OP_PLAYER_SLOTS, OP_KNOWN, OP_SEEN, OP_SEEN_LIVING, OP_SIGHT, OP_NEARBY,
    OP_UNSEEN, OP_TRACKER, OP_REC_ADD, OP_REC_REMOVE, OP_TACTICAL, OP_SAVE, OP_LOAD, OP_ALIVE, OP_MEMBER, OP_INDEX, OP_KILLED,
    OP_COOP_CLEAR, OP_COOP_ADD, OP_COOP_REMOVE, OP_COOP_TICK, OP_COOP_RESTART, OP_COOP_DIED, OP_COOP_ITEM, OP_COOP_REC_ADD,
    OP_COOP_REC_REMOVE, OP_COOP_SAVE, OP_COOP_LOAD, OP_COUNT,
    W_WORLD = 100,
    W_ROLE = 101,   // the game's role changed alone
};
struct S_Op {
    int kind;
    int a = 0, b = 0;
};
S_Op RandomOp()
{
    S_Op op;
    if (P(0.05)) {
        op.kind = W_WORLD;
        op.a = int(U32());
        return op;
    }
    // (The members added more often: the rest needs them.)
    op.kind = P(0.2) ? Pick({int(OP_ADD), int(OP_COOP_ADD), int(OP_REC_ADD)}) : I(0, OP_COUNT - 1);
    op.a = P(0.1) ? -1 : I(0, HUMANS - 1);
    op.b = I(-1, 6);
    switch (op.kind) {
    case OP_UPDATE:
    case OP_TICK:
    case OP_COOP_TICK:
        op.a = P(0.3) ? Pick({0, 10000, 9999, 20000}) : I(1, 3000);
        break;
    case OP_SWITCH:
    case OP_COMMAND_INDEX:
    case OP_VOLUMES:
    case OP_RANDOM_VOLUME:
    case OP_MEMBER:
        op.a = I(-2, 7);
        break;
    case OP_HAS_ITEM:
    case OP_COOP_ITEM:
        op.a = I(0, 300);
        break;
    default:
        break;
    }
    return op;
}
using T_V = void(__attribute__((thiscall)) *)(void *);
using T_I = void(__attribute__((thiscall)) *)(void *, int);
using T_B = void(__attribute__((thiscall)) *)(void *, bool);
using T_P = void(__attribute__((thiscall)) *)(void *, void *);
using T_PB = void(__attribute__((thiscall)) *)(void *, void *, bool);
using T_RetBP = bool(__attribute__((thiscall)) *)(void *, void *);
using T_RetBI = bool(__attribute__((thiscall)) *)(void *, int);
using T_RetB = bool(__attribute__((thiscall)) *)(void *);
using T_RetI = int(__attribute__((thiscall)) *)(void *);
using T_RetP = void *(__attribute__((thiscall)) *)(void *);
using T_RetPP = void *(__attribute__((thiscall)) *)(void *, void *);
using T_RetPI = void *(__attribute__((thiscall)) *)(void *, int);
using T_RetPPB = void *(__attribute__((thiscall)) *)(void *, void *, bool);
using T_RetIP = int(__attribute__((thiscall)) *)(void *, void *);
using T_Grenade = void(__attribute__((thiscall)) *)(void *, const float *, const float *, void *);
using T_Vec = void(__attribute__((thiscall)) *)(void *, void *);
using T_Ctor = void *(__attribute__((thiscall)) *)(void *);
using T_ChunkOpen = bool(__attribute__((thiscall)) *)(void *, const char *, bool);
using T_ChunkClose = bool(__attribute__((thiscall)) *)(void *);
using T_ChunkBegin = bool(__attribute__((thiscall)) *)(void *, uint16_t);
using T_ChunkWrite = bool(__attribute__((thiscall)) *)(void *, const void *, int);
using T_ChunkAscend = bool(__attribute__((thiscall)) *)(void *, uint32_t *);
using T_Killed = void(__stdcall *)(const void *);
// What both sides would crash on (or read past their members, the vectors' spare room differing) is skipped; from the
// side's own state (the same while the sides agree).
bool Skip(int side, const S_Op &op, std::string &why)
{
    const uint8_t *b = side ? reinterpret_cast<const uint8_t *>(g_port) : g_orig;
    auto o = [side](uint32_t orig, uint32_t port) { return side ? port : orig; };
    const int count = int(Words(b, side ? uint32_t(offsetof(C_TeamBase, m_members)) : 4, side).size());
    const int selected = At<int>(b, o(0x28, OT(m_selected))), active = At<int>(b, o(0x2C, OT(m_active))),
              commanded = At<int>(b, o(0x30, OT(m_commanded)));
    const bool watched = At<void *>(b, o(0x58, OT(m_watchedHuman))) != nullptr;
    // (An index past the members, left by a removal that does not follow it: only what reads it is skipped.)
    auto bad = [count](int i) { return i < -1 || i >= count; };
    switch (op.kind) {
    case OP_ACTIVE:
    case OP_CAMERA:
    case OP_RETURN:
    case OP_SWITCH:
    case OP_COMMAND_INDEX:
    case OP_COMMAND:
        if (bad(active) && !watched)
            why = "the active index past the members";
        if (bad(active) && (op.kind == OP_SWITCH || op.kind == OP_COMMAND_INDEX || op.kind == OP_COMMAND || op.kind == OP_RETURN))
            why = "the active index past the members";
        break;
    case OP_SELECTED:
        if (bad(selected))
            why = "the selected index past the members";
        break;
    case OP_COMMANDED:
    case OP_DIED:
        if (bad(commanded))
            why = "the commanded index past the members";
        break;
    default:
        break;
    }
    switch (op.kind) {
    case OP_SWITCH:
        if (active < 0 && op.a >= 0 && op.a < count)
            why = "switching without an active member";
        break;
    case OP_COMMAND_INDEX:
    case OP_COMMAND:
        if (active < 0 && selected >= 0)
            why = "commanding the selected member without an active one";
        break;
    case OP_RANDOM_VOLUME: {
        // (Both read the member's list unchecked.)
        const std::vector<uint32_t> lists = Words(b, o(0x5C, OT(m_volumes)), side);
        if (op.a < 0 || op.a >= int(lists.size()) || Words(reinterpret_cast<const void *>(uintptr_t(lists[size_t(op.a)])), 0, side).empty())
            why = "a volume of no list";
        break;
    }
    case OP_VOLUMES:
        if (op.a < 0 || op.a >= count || op.a > int(Words(b, o(0x5C, OT(m_volumes)), side).size()))
            why = "the volumes of no member";
        break;
    case OP_RELEASE:
    case OP_NEARBY:
    case OP_UNSEEN:
        break;
    case OP_REMOVE:
    case OP_NPC: {
        // (The removed member's volumes list: both index it unchecked; and the original's active-member read past the
        // members when the last one goes: the port's fix, alone.)
        const std::vector<uint32_t> members = Words(b, side ? uint32_t(offsetof(C_TeamBase, m_members)) : 4, side);
        const auto it = std::find(members.begin(), members.end(), uint32_t(uintptr_t(Hum(op.a))));
        if (it != members.end() && (op.kind == OP_REMOVE || op.b < 0)) {
            const int index = int(it - members.begin());
            if (index >= int(Words(b, o(0x5C, OT(m_volumes)), side).size()))
                why = "removing a member without a volumes list";
            else if (index == active)
                why = "removing the active member";
        }
        break;
    }
    default:
        break;
    }
    (void)watched;
    return !why.empty();
}
void Run(int side, const S_Op &op)
{
    std::string why;
    if (Skip(side, op, why)) {
        Log("skip: %s", why.c_str());
        return;
    }
    void *o = g_orig, *oc = g_origCoop, *orec = g_orig + 0x38, *ocrec = g_origCoop + 0x28;
    C_Team *p = g_port;
    C_CoopTeam *pc = g_portCoop;
    C_Human *h = static_cast<C_Human *>(Hum(op.a));
    const float pos[3] = {1.5f, -2.0f, 3.25f}, dir[3] = {0.0f, 0.6f, 0.8f};
    switch (op.kind) {
    case OP_CLEAR:
        side ? p->Clear() : OriginalAt<T_V>(0x0045d3a0)(o);
        break;
    case OP_ADD:
        Log("result %d", side ? p->AddMember(h) : OriginalAt<T_RetBP>(0x0045e5d0)(o, h));
        break;
    case OP_REMOVE:
        Log("result %d", side ? p->RemoveMember(h) : OriginalAt<T_RetBP>(0x0045e670)(o, h));
        break;
    case OP_NPC:
        side ? p->SetNPCMember(h, op.b >= 0) : OriginalAt<T_PB>(0x0045d100)(o, h, op.b >= 0);
        break;
    case OP_UPDATE:
        side ? p->Update(op.a) : OriginalAt<T_I>(0x0045ddc0)(o, op.a);
        break;
    case OP_TICK:
        side ? p->Tick(op.a) : OriginalAt<T_I>(0x0045dd90)(o, op.a);
        break;
    case OP_RESTART:
        side ? p->RestartMission(op.b >= 0) : OriginalAt<T_B>(0x0045dde0)(o, op.b >= 0);
        break;
    case OP_ACTIVE:
        Log("result %s", Name(side ? static_cast<void *>(p->GetActivePlayer()) : OriginalAt<T_RetP>(0x0045d1d0)(o)).c_str());
        break;
    case OP_SELECTED:
        Log("result %s", Name(side ? static_cast<void *>(p->GetSelectedMember()) : OriginalAt<T_RetP>(0x0045d160)(o)).c_str());
        break;
    case OP_COMMANDED:
        Log("result %s", Name(side ? static_cast<void *>(p->GetCommandedMember()) : OriginalAt<T_RetP>(0x0045d1b0)(o)).c_str());
        break;
    case OP_SELECT:
        side ? p->SelectMember(h) : OriginalAt<T_P>(0x0045da60)(o, h);
        break;
    case OP_SWITCH:
        Log("result %d", side ? p->SwitchTo(op.a) : OriginalAt<T_RetBI>(0x0045dc20)(o, op.a));
        break;
    case OP_NEXT:
        Log("result %d", side ? p->GetNextControllable() : OriginalAt<T_RetI>(0x0045d660)(o));
        break;
    case OP_PREVIOUS:
        Log("result %d", side ? p->GetPreviousControllable() : OriginalAt<T_RetI>(0x0045d9d0)(o));
        break;
    case OP_COMMAND_INDEX:
        Log("result %d", side ? p->SetCommandedIndex(op.a) : OriginalAt<T_RetBI>(0x0045dcc0)(o, op.a));
        break;
    case OP_COMMAND:
        Log("result %d", side ? p->SetCommanded(h) : OriginalAt<T_RetBP>(0x0045dd40)(o, h));
        break;
    case OP_RETURN:
        side ? p->ReturnControl() : OriginalAt<T_V>(0x0045da40)(o);
        break;
    case OP_CAMERA:
        side ? p->UpdateCameraTarget() : OriginalAt<T_V>(0x0045dac0)(o);
        break;
    case OP_WATCH:
        side ? p->SetWatchedHuman(h) : OriginalAt<T_P>(0x0045e8f0)(o, h);
        break;
    case OP_VOLUMES:
        side ? p->CollectVolumes(op.a) : OriginalAt<T_I>(0x0045d710)(o, op.a);
        break;
    case OP_RANDOM_VOLUME:
        Log("result %s", Name(side ? static_cast<void *>(p->GetRandomVolume(op.a)) : OriginalAt<T_RetPI>(0x0045d6d0)(o, op.a)).c_str());
        break;
    case OP_DIED:
        side ? p->OnMemberDied(h) : OriginalAt<T_P>(0x0045e790)(o, h);
        break;
    case OP_HAS_ITEM:
        Log("result %d", side ? p->HasItem(op.a) : OriginalAt<T_RetBI>(0x0045e9a0)(o, op.a));
        break;
    case OP_MEDKIT:
        Log("result %d", side ? p->HasMedkitBesides(h) : OriginalAt<T_RetBP>(0x0045ea00)(o, h));
        break;
    case OP_GRENADE:
        if (side)
            p->NotifyGrenadeThrown(*reinterpret_cast<const S_vector *>(pos), *reinterpret_cast<const S_vector *>(dir), h);
        else
            OriginalAt<T_Grenade>(0x0045eab0)(o, pos, dir, h);
        break;
    case OP_HEAL:
        side ? p->HealMembers() : OriginalAt<T_V>(0x0045f020)(o);
        break;
    case OP_RELEASE:
        if (h)
            side ? p->ReleaseHoldingMembers(h) : OriginalAt<T_P>(0x0045f110)(o, h);
        break;
    case OP_PLAYER_SLOTS:
        Log("result %d", side ? p->HasMemberInPlayerSlots() : OriginalAt<T_RetB>(0x0045d120)(o));
        break;
    case OP_KNOWN:
        Log("result %d", side ? p->IsEnemyKnown(h) : OriginalAt<T_RetBP>(0x0045e920)(o, h));
        break;
    case OP_SEEN:
    case OP_SEEN_LIVING: {
        // (The original's vector out: MSVC6 {allocator, first, last, end}, malloc'd; the port's.)
        std::string names;
        if (side) {
            std::vector<C_Human *> enemies(size_t(I(0, 2)), nullptr);
            op.kind == OP_SEEN ? p->GetSeenEnemies(enemies) : p->GetSeenLivingEnemies(enemies);
            for (C_Human *e : enemies)
                names += " " + Name(e);
        } else {
            uint32_t v[4] = {0, 0, 0, 0};
            OriginalAt<T_Vec>(op.kind == OP_SEEN ? 0x0045eb30 : 0x0045ed10)(o, v);
            for (uint32_t w : Words(v, 0, 0))
                names += " " + Name(reinterpret_cast<void *>(uintptr_t(w)));
            std::free(reinterpret_cast<void *>(uintptr_t(v[1])));
        }
        Log("result%s", names.c_str());
        break;
    }
    case OP_SIGHT:
        side ? p->RefreshSight() : OriginalAt<T_V>(0x0045f080)(o);
        break;
    case OP_NEARBY:
        Log("result %s", Name(side ? static_cast<void *>(p->GetEnemySeenNearby(h)) : OriginalAt<T_RetPP>(0x0045f280)(o, h)).c_str());
        break;
    case OP_UNSEEN:
        Log("result %s",
            Name(side ? static_cast<void *>(p->GetEnemyUnseenBy(h, op.b >= 0)) : OriginalAt<T_RetPPB>(0x0045f3a0)(o, h, op.b >= 0)).c_str());
        break;
    case OP_TRACKER:
        side ? p->AddSeenMeleeTracker(reinterpret_cast<C_MeleeTracker *>(Hum(op.a))) : OriginalAt<T_P>(0x0045d4a0)(o, Hum(op.a));
        break;
    case OP_REC_ADD:
        if (h)
            side ? p->GetEnemyRecords().Add(h) : OriginalAt<T_P>(0x0045e410)(orec, h);
        break;
    case OP_REC_REMOVE:
        if (h)
            side ? p->GetEnemyRecords().Remove(h) : OriginalAt<T_P>(0x0045e540)(orec, h);
        break;
    case OP_TACTICAL:
        // (Set directly by the interface on both.)
        if (side)
            p->GetEnemyRecords().SetTacticalView(op.b >= 0);
        else
            Put<bool>(orec, 0x1C, op.b >= 0);
        break;
    case OP_SAVE:
    case OP_COOP_SAVE:
    case OP_LOAD:
    case OP_COOP_LOAD: {
        const bool save = op.kind == OP_SAVE || op.kind == OP_COOP_SAVE, coop = op.kind == OP_COOP_SAVE || op.kind == OP_COOP_LOAD;
        const char *name = save ? "save.sav" : "load.sav";
        if (!save) {
            const std::mt19937 kept = g_rng;
            g_rng.seed(uint32_t(op.a) * 7919u + uint32_t(op.b));
            g_files[name] = MakeLoadFile(coop);
            g_rng = kept;
        }
        bool result;
        if (side) {
            C_chunk chunk;
            chunk.Open(name, save);
            result = coop ? (save ? pc->SaveGame(chunk) : pc->LoadGame(chunk)) : (save ? p->SaveGame(chunk) : p->LoadGame(chunk));
            // (The game's save goes on: the next module's chunk; a load: where the reader is left.)
            if (save) {
                chunk.Begin(0x7777);
                chunk.Write(int(0x12345678));
                chunk.Ascend();
            } else
                Log("left at %d depth %d", chunk.GetPosition(), chunk.m_depth);
            chunk.Close();
        } else {
            alignas(16) uint8_t chunk[0x500] = {};
            OriginalAt<T_ChunkOpen>(0x00405620)(chunk, name, save);
            // (The base's SaveGame / LoadGame through the co-op team's vtable: its slots 0x1C / 0x20.)
            void *self = coop ? oc : o;
            const uint32_t fn = coop ? (*reinterpret_cast<uint32_t **>(oc))[(save ? 0x1C : 0x20) / 4] : (save ? 0x0045de90u : 0x0045e040u);
            result = reinterpret_cast<T_RetBP>(uintptr_t(fn))(self, chunk);
            if (save) {
                const int next = 0x12345678;
                OriginalAt<T_ChunkBegin>(0x00405820)(chunk, 0x7777);
                OriginalAt<T_ChunkWrite>(0x004059b0)(chunk, &next, 4);
                OriginalAt<T_ChunkAscend>(0x00405890)(chunk, nullptr);
            } else
                Log("left at %d depth %d", FileSeek(At<int>(chunk, 4), 0, 1), At<int>(chunk, 0x410));
            OriginalAt<T_ChunkClose>(0x00405750)(chunk);
        }
        Log("result %d", result);
        if (save) {
            std::string hex;
            for (unsigned char c : g_files[name]) {
                char t[4];
                std::snprintf(t, sizeof t, "%02x", c);
                hex += t;
            }
            Log("saved %s", hex.c_str());
        }
        break;
    }
    case OP_ALIVE:
        Log("result %d", side ? p->GetAliveCount() : OriginalAt<T_RetI>(0x004622c0)(o));
        break;
    case OP_MEMBER:
        Log("result %s", Name(side ? static_cast<void *>(p->GetMember(op.a)) : OriginalAt<T_RetPI>(0x004619f0)(o, op.a)).c_str());
        break;
    case OP_INDEX:
        Log("result %d %d", side ? p->IndexOf(h) : OriginalAt<T_RetIP>(0x00461a60)(o, h),
            side ? p->IsMember(h) : OriginalAt<T_RetBP>(0x00461a20)(o, h));
        break;
    case OP_KILLED:
        if (h)
            side ? PrintEnemyKilled(h) : OriginalAt<T_Killed>(0x0045e8a0)(h);
        break;
    case OP_COOP_CLEAR:
        side ? pc->Clear() : OriginalAt<T_V>(0x0045fc80)(oc);
        break;
    case OP_COOP_ADD:
        // (C_TeamBase's, through the co-op team's vtable on both.)
        Log("result %d", side ? pc->AddMember(h) : reinterpret_cast<T_RetBP>((*reinterpret_cast<void ***>(oc))[0x08 / 4])(oc, h));
        break;
    case OP_COOP_REMOVE:
        Log("result %d", side ? pc->RemoveMember(h) : OriginalAt<T_RetBP>(0x0045fdf0)(oc, h));
        break;
    case OP_COOP_TICK:
        side ? pc->Tick(op.a) : OriginalAt<T_I>(0x0045fd50)(oc, op.a);
        break;
    case OP_COOP_RESTART:
        side ? pc->RestartMission(op.b >= 0) : OriginalAt<T_B>(0x0045fd80)(oc, op.b >= 0);
        break;
    case OP_COOP_DIED:
        side ? pc->OnMemberDied(h) : OriginalAt<T_P>(0x0045fe10)(oc, h);
        break;
    case OP_COOP_ITEM:
        Log("result %d", side ? pc->HasItem(op.a) : OriginalAt<T_RetBI>(0x0045fef0)(oc, op.a));
        break;
    case OP_COOP_REC_ADD:
        if (h)
            side ? pc->GetEnemyRecords().Add(h) : OriginalAt<T_P>(0x0045e410)(ocrec, h);
        break;
    case OP_COOP_REC_REMOVE:
        if (h)
            side ? pc->GetEnemyRecords().Remove(h) : OriginalAt<T_P>(0x0045e540)(ocrec, h);
        break;
    default:
        break;
    }
}

struct S_Run {
    std::vector<std::string> log;
    S_Arena arena;
};
S_Run g_runs[2];
void TestScenarios()
{
    g_retSeed = U32();
    for (int i = 0; i < FRAMES; ++i)
        RandomFrame(i);
    RandomWorld();
    std::vector<S_Op> ops;
    for (int n = I(0, 6); n > 0; --n) {
        S_Op add{P(0.8) ? int(OP_ADD) : int(OP_COOP_ADD)};
        add.a = I(0, HUMANS - 1);
        ops.push_back(add);
    }
    auto op = [](int kind, int a = 0, int b = 0) {
        S_Op o{kind};
        o.a = a;
        o.b = b;
        return o;
    };
    switch (P(0.5) ? I(0, 6) : -1) {
    case 4: {
        // An enemy seen by two members, lost by one.
        const int h = I(0, HUMANS - 1);
        g_w.health[h] = 50;
        for (S_Op o : {op(OP_REC_ADD, h), op(OP_REC_ADD, h), op(OP_REC_REMOVE, h)})
            ops.push_back(o);
        break;
    }
    case 5:
        // The environment found in a single player game, the mission restarted in a network one.
        g_w.role = 7;
        g_w.env[0] = Pick({uint32_t(HUMAN_ENV_RESTRICTED), 0x1u});
        g_w.health[0] = 50;
        for (S_Op o : {op(OP_CLEAR), op(OP_ADD, 0), op(OP_TICK, 10), op(W_ROLE, Pick({6, 2, 3})), op(OP_RESTART), op(OP_TICK, 10)})
            ops.push_back(o);
        break;
    case 6:
        // A network game: the last member dies.
        g_w.role = Pick({6u, 2u, 3u});
        g_w.health[0] = 0;
        for (S_Op o : {op(OP_CLEAR), op(OP_ADD, 0), op(OP_DIED, 0)})
            ops.push_back(o);
        break;
    case 2:
        // The commanded member dies.
        for (S_Op o : {op(OP_CLEAR), op(OP_ADD, 0), op(OP_ADD, 1), op(OP_ADD, 2), op(OP_COMMAND_INDEX, 1), op(OP_DIED, 1)})
            ops.push_back(o);
        break;
    case 3:
        // A network game: the selected member spectating (or not).
        g_w.role = Pick({6u, 2u});
        for (S_Op o : {op(OP_CLEAR), op(OP_ADD, 0), op(OP_ADD, 1), op(OP_SELECT, 1), op(OP_SELECTED), op(OP_SELECTED)})
            ops.push_back(o);
        break;
    case 0: {
        // An enemy seen and lost, then forgotten.
        const int h = I(0, HUMANS - 1);
        for (S_Op o : {op(OP_REC_ADD, h), op(OP_REC_REMOVE, h), op(OP_UPDATE, Pick({10000, 9999, 20000, 5000}))})
            ops.push_back(o);
        break;
    }
    case 1: {
        // The selected member apart from the active one: the last selected, the first removed (the active index then
        // reset), a new member in the selected slot, removed.
        ops.push_back(op(OP_CLEAR));
        for (int k = 0; k < 4; ++k)
            ops.push_back(op(OP_ADD, k));
        for (S_Op o : {op(OP_SELECT, 3), op(OP_REMOVE, 0), op(OP_ADD, 5), op(OP_SELECTED), op(OP_COMMAND_INDEX, 3), op(OP_REMOVE, 5)})
            ops.push_back(o);
        break;
    }
    default:
        break;
    }
    for (int n = I(1, 20); n > 0; --n)
        ops.push_back(RandomOp());
    std::memcpy(&g_arenaStart, &g_arena, sizeof g_arena);
    g_wStart = g_w;
    const uint32_t randSeed = U32();
    for (int side = 0; side < 2; ++side) {
        g_side = side;
        std::memcpy(&g_arena, &g_arenaStart, sizeof g_arena);
        g_w = g_wStart;
        g_log.clear();
        g_retCounts.clear();
        g_files.clear();
        g_openFiles.clear();
        g_nextFile = 1;
        std::srand(randSeed);
        SetWorld(side);
        // The teams, built: the original's by its constructors (in malloc'd blocks), the port's.
        if (side) {
            g_port = new C_Team();
            g_portCoop = new C_CoopTeam();
        } else {
            g_orig = static_cast<uint8_t *>(std::calloc(1, 0xCC));
            g_origCoop = static_cast<uint8_t *>(std::calloc(1, 0x4C));
            OriginalAt<T_Ctor>(0x0045d1f0)(g_orig);
            OriginalAt<T_Ctor>(0x0045fb90)(g_origCoop);
        }
        for (const S_Op &op : ops) {
            Log("-- %d %d %d", op.kind, op.a, op.b);
            if (op.kind == W_WORLD) {
                // (The world's own draws: the shared generator kept, else the scenarios after it repeat a cycle.)
                const std::mt19937 kept = g_rng;
                g_rng.seed(uint32_t(op.a));
                RandomWorld();
                g_rng = kept;
                SetWorld(side);
            } else if (op.kind == W_ROLE) {
                g_w.role = uint32_t(op.a);
                g_gameRole = g_w.role;
                *OriginalAt<uint32_t *>(0x00982100) = g_w.role;
            } else
                Run(side, op);
            g_log.push_back(Snap(side));
        }
        Log("-- destroy");
        if (side) {
            delete g_port;
            delete g_portCoop;
        } else {
            OriginalAt<T_V>(0x0045d2e0)(g_orig);
            OriginalAt<T_V>(0x0045fc10)(g_origCoop);
            std::free(g_orig);
            std::free(g_origCoop);
        }
        g_runs[side].log.swap(g_log);
        std::memcpy(&g_runs[side].arena, &g_arena, sizeof g_arena);
    }
    CompareLogs(g_runs[0].log, g_runs[1].log);
    CompareArenas(g_runs[0].arena, g_runs[1].arena);
    ShowLogs(g_runs[0].log, g_runs[1].log);
}

// ---- the port's fixes, alone ----
bool Logged(const std::string &what) { return std::find(g_log.begin(), g_log.end(), what) != g_log.end(); }
void TestFixes()
{
    g_side = 1;
    for (int i = 0; i < FRAMES; ++i)
        RandomFrame(i);
    RandomWorld();
    g_w.role = 7;
    SetWorld(1);
    // RemoveMember of the active member: its own model shown (the original showed the member moved into its slot, and
    // read past the members when it was the last one); the first member active then.
    for (int removed : {0, 2}) {
        C_Team team;
        for (int k = 0; k < 3; ++k)
            team.AddMember(static_cast<C_Human *>(Hum(k)));
        team.SelectMember(static_cast<C_Human *>(Hum(removed)));
        g_log.clear();
        CHECK(team.RemoveMember(static_cast<C_Human *>(Hum(removed))), "the active member %d removed", removed);
        CHECK(Logged(Name(FrameAt(F_BODY + removed)) + " SetOn 1"), "the removed active member %d's model shown", removed);
        CHECK(team.GetActiveIndex() == 0 && team.GetMemberCount() == 2, "the first member active after removing %d", removed);
    }
}

void SetUp()
{
    g_partName = PartName;
    g_frameVt[0x44 / 4] = reinterpret_cast<void *>(V_EnumFrames);
    for (int side = 0; side < 2; ++side) {
        for (void *&v : g_vehicleVt[side])
            v = reinterpret_cast<void *>(V_Other);
        for (void *&v : g_controllerVt[side])
            v = reinterpret_cast<void *>(V_Other);
        for (void *&v : g_commanderVt[side])
            v = reinterpret_cast<void *>(V_Other);
        *reinterpret_cast<void ***>(g_commander[side]) = g_commanderVt[side];
    }
    g_vehicleVt[0][0x8C / 4] = reinterpret_cast<void *>(V_SeatFlags);
    g_vehicleVt[1][Slot(&C_Actor::GetSeatFlags) / 4] = reinterpret_cast<void *>(V_SeatFlags);
    g_controllerVt[0][0x148 / 4] = reinterpret_cast<void *>(V_CombatOrder);
    g_controllerVt[1][Slot(&C_SquadController::GetCombatOrder) / 4] = reinterpret_cast<void *>(V_CombatOrder);
    g_commanderVt[1][Slot(&C_SquadCommander::Reset) / 4] = reinterpret_cast<void *>(O_CommanderReset);
    g_commanderVt[1][Slot(&C_SquadCommander::Tick) / 4] = reinterpret_cast<void *>(O_CommanderTick);
    for (void *&v : g_origCameraVt)
        v = reinterpret_cast<void *>(V_Other);
    g_origCameraVt[0x40 / 4] = reinterpret_cast<void *>(O_C_CameraModule_SelectView);
    *reinterpret_cast<void ***>(g_cameraModule[0]) = g_origCameraVt;
    const struct {
        uint32_t at;
        const void *to;
    } redirects[] = {
        IFAKE_REDIRECTS
        {0x005a5fe0, reinterpret_cast<const void *>(OH_ControllerType)},
        {0x005a9720, reinterpret_cast<const void *>(OH_EnvFlags)},
        {0x00475d50, reinterpret_cast<const void *>(O_ActorById)},
        {0x00475e00, reinterpret_cast<const void *>(O_HumanAt)},
        {0x006987d0, reinterpret_cast<const void *>(O_GetGame)},
        {0x00464e90, reinterpret_cast<const void *>(O_GetCameraModule)},
        {0x004563e0, reinterpret_cast<const void *>(O_GetSquadCommander)},
        {0x00456440, reinterpret_cast<const void *>(O_CommanderReset)},
        {0x004564a0, reinterpret_cast<const void *>(O_CommanderTick)},
        {0x0069c2b0, reinterpret_cast<const void *>(O_Print)},
        {0x0069c2a0, reinterpret_cast<const void *>(O_PrintColored)},
        {0x006b6a20, reinterpret_cast<const void *>(O_Difficulty)},
        {0x006b69d0, reinterpret_cast<const void *>(O_Coop)},
        {0x005949c0, reinterpret_cast<const void *>(O_ShapeNew)},
        {0x00594a70, reinterpret_cast<const void *>(O_ShapeDelete)},
        {0x007e53c2, reinterpret_cast<const void *>(O_AlignedMalloc)},
        {0x007e559c, reinterpret_cast<const void *>(O_AlignedFree)},
        {0x007e6334, reinterpret_cast<const void *>(O_Sprintf)},
        {0x007e53a4, reinterpret_cast<const void *>(O_dtaOpen)},
        {0x007e53aa, reinterpret_cast<const void *>(O_dtaOpenWrite)},
        {0x007e53b0, reinterpret_cast<const void *>(O_dtaClose)},
        {0x007e5392, reinterpret_cast<const void *>(O_dtaWrite)},
        {0x007e5398, reinterpret_cast<const void *>(O_dtaSeek)},
        {0x007e539e, reinterpret_cast<const void *>(O_dtaRead)},
    };
    for (const auto &r : redirects)
        RedirectOriginal(r.at, r.to);
    g_log.clear();
}

}  // namespace H

int main()
{
    using namespace H;
    int exitCode = 0;
    if (!StartKit("team_diff", exitCode))
        return exitCode;
    SetUp();
    int rounds = std::getenv("HD2_TEAM_QUICK") ? 2500 : 6000;
    if (const char *r = std::getenv("HD2_TEAM_ROUNDS"))
        rounds = std::atoi(r);
    g_case = "Team";
    for (g_round = 0; g_round < rounds; ++g_round)
        TestScenarios();
    g_case = "Fixes";
    g_round = 0;
    TestFixes();
    std::printf("team_diff: %d checks, %d failures\n", checks, failures);
    return failures != 0;
}
