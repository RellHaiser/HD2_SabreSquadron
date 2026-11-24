// Script part 3: the interpreter's game commands (script_vm.cpp's C_Script::ExecuteCommand past the script-level
// commands of part 2) against the original executable's (0x00416e10). One program with frame, number and string
// variables is compiled on both sides; then each case builds the same fake world on each side (engine frames of
// several types in a hierarchy, the scene, the driver, the game's modules), gives the variables the same values, runs
// one command on each side and compares: the answer, the script's state and variables, the frames' state, the game's
// fields and the log of every call each side made into the game or the engine. The calls go to one logger
// (CmdCallEntry in cmd_calls.s): the port's through link stubs (gen_cmd_calls.py), the original's through its patched
// functions, the engine's through the fake frames' vtables (both sides call the same __stdcall slots), the game's
// virtual ones through a vtable per side (the port's slots from member pointers, the original's from the headers).
// Each call is answered alike on both sides (a sequence both draw in the same order, or the world: a frame found by
// name, children). MinGW; build_cmd.bat; runs cmd_diff.exe ("cmd_diff: N checks, 0 failures").
// Script part 4: half the cases run an actor script's command instead (C_HumanScript::ExecuteCommand, the original's
// 0x00421c00, on one of the world's humans with its controller, work and vehicle; C_MeleeTrackerScript's, 0x00421970, on a
// tracker with its controller); the constructors are compared once.
// Script part 5: one case in 12 makes a script on a world frame (C_Script's constructor 0x0041dcb0 and destructor
// 0x0041df20 with the frame's data) and runs nothing, Stop, SendSignal or NotifyFrameHit on it.
// Env: HD2_CMD_CASES (cases, default 200,000; HD2_CMD_QUICK: 30,000), HD2_CMD_ONLY (one opcode's name), HD2_CMD_ACTORS
// (the actor scripts' cases only), HD2_CMD_SHOW (every case's logs printed), HD2_CMD_DUMP (a failing case's logs),
// HD2_CMD_SEED; sc_diff's HD2_SC_DATA / HD2_UNPACKED, HD2_SC_FIRST_CHANCE.
// (Script parts 6 and 7: mod_diff.cpp includes this file for its world and logger, with its own HARNESS_NAME, call
// table (CMD_CALLS_INC) and main (CMD_DIFF_NO_MAIN).)
#ifndef HARNESS_NAME
#define HARNESS_NAME "cmd_diff"
#endif
#include "sc_shared.inc"

#include <set>

extern "C" unsigned int __cdecl _controlfp(unsigned int value, unsigned int mask);   // (msvcrt; hidden by -std=c++17)

#define private public
#define protected public
#include "game/actor.h"
#include "game/checkpoints.h"
#include "game/geffects.h"
#include "game/actors_module.h"
#include "game/animation_actor.h"
#include "game/car.h"
#include "game/decorators.h"
#include "game/document_place.h"
#include "game/door.h"
#include "game/explosive_place.h"
#include "game/photo_place.h"
#include "game/pike.h"
#include "game/radio.h"
#include "game/static_weapon.h"
#include "game/switch_actor.h"
#include "game/ts_explosion.h"
#include "game/human.h"
#include "game/interface.h"
#include "game/junkers88.h"
#include "game/melee_tracker.h"
#include "game/melee_tracker_ai.h"
#include "game/bike.h"
#include "game/human_body.h"
#include "game/human_controller.h"
#include "game/profile.h"
#include "game/team.h"
#include "game/train.h"
#include "ls3d/i3d.h"
#include "script/actor_scripts.h"
#include "script/script_module.h"
#undef protected
#undef private

// (The port's own helpers of melee_tracker.cpp, which the test does not link: the full AI and the scripted variant.)
C_MeleeTrackerAI *C_MeleeTracker::GetFullAI() const
{
    return m_ai && (m_ai->GetMode() == 4 || m_ai->GetMode() == 5) ? static_cast<C_MeleeTrackerAI *>(m_ai) : nullptr;
}
C_MeleeTrackerScriptedAI *C_MeleeTracker::GetScriptedAI() const
{
    return m_ai && m_ai->GetMode() == 5 ? static_cast<C_MeleeTrackerScriptedAI *>(m_ai) : nullptr;
}
static_assert(offsetof(S_HumanCommand, category) == 4 && offsetof(S_HumanCommand, param2) == 0x10, "the original's layout");

struct S_CallDef {
    const char *name;
    uint32_t orig;
    const char *oconv;
    const char *pconv;
    const char *kinds;
    char ret;
    int vt;
};
struct S_SlotDef {
    const char *cls;
    const char *base;
    int off;
    const char *name;
    const char *kinds;
    char ret;
};
#ifndef CMD_CALLS_INC
#define CMD_CALLS_INC "cmd_calls.inc"
#endif
#include CMD_CALLS_INC

extern "C" {
int g_cmdPop, g_cmdFloat;
float g_cmdFloatValue;
uint32_t g_cmdSaveEax, g_cmdSaveRet;
void CmdCallEntry();
uint64_t CmdCall(uint32_t *regs);
}

uint32_t g_gameRole = GAME_ROLE_LOCAL;
// The word behind the port's g_playMode reference (its link stub).
extern uint8_t **g_playModeStorage asm("_g_playMode");
bool g_scriptModuleRestarting;
extern const void *const g_noPhysicsObject;             // (sc_globals.cpp: the port's "no physics object")

namespace H {

constexpr int NCALLS = int(sizeof CALLS / sizeof *CALLS);
constexpr int NSLOTS = int(sizeof ENGINE_SLOTS / sizeof *ENGINE_SLOTS);
constexpr uint32_t SLOT_BASE = 0x10000, VIRTUAL_BASE = 0x20000, TRAP_BASE = 0x30000;
constexpr uint32_t O_EXECUTE = 0x00416e10, O_RAND = 0x007e6393, O_SAVE_VALUES = 0x00868b3c,
                   O_SCRIPT_MODULE = 0x00868b18, O_GAME_PROFILE = 0x008aea0c;

// ---- the game's virtual calls: the port's slot (a member pointer's), the original's (the headers') ----
struct S_Virtual {
    const char *name;
    int orig;
    int port;
    const char *kinds;                                  // (after `this`)
    char ret;
};
template <class F> int PortSlot(F f)
{
    static_assert(sizeof(F) == 8, "a member function pointer");
    uint32_t w[2];
    std::memcpy(w, &f, 8);
    return int(w[0] - 1);                               // (a virtual's: 1 + its offset in the vtable)
}
std::vector<S_Virtual> g_virtuals;
void SetUpVirtuals()
{
    using G = C_GEffectsModule;
    g_virtuals = {
        {"C_GEffectsModule::SetThunderstorm", 0x44, PortSlot(&G::SetThunderstorm), "bpb", 'v'},
        {"C_GEffectsModule::SetModelTransparency", 0x50, PortSlot(&G::SetModelTransparency), "pf", 'b'},
        {"C_GEffectsModule::SetModelLightLevel", 0x54, PortSlot(&G::SetModelLightLevel), "pf", 'b'},
        {"C_GEffectsModule::SetHierarchyOn", 0x58, PortSlot(&G::SetHierarchyOn), "pbi", 'v'},
        {"C_GEffectsModule::HideVisual", 0x5C, PortSlot(&G::HideVisual), "pb", 'v'},
        {"C_GEffectsModule::CreateShadow", 0x78, PortSlot(&G::CreateShadow), "p", 'p'},
        {"C_GEffectsModule::GetShadow", 0x7C, PortSlot(&G::GetShadow), "p", 'p'},
        {"C_GEffectsModule::Fade", 0x8C, PortSlot(&G::Fade), "iiV", 'v'},
        {"C_GEffectsModule::SetTimeScale", 0x98, PortSlot(&G::SetTimeScale), "ff", 'v'},
        {"C_GEffectsModule::SwitchFaceTexture", 0xAC, PortSlot(&G::SwitchFaceTexture), "ps", 'b'},
        {"C_GEffectsModule::SetCameraAnimation", 0xB0, PortSlot(&G::SetCameraAnimation), "ps", 'v'},
        {"C_GEffectsModule::SetCloudsOn", 0xB8, PortSlot(&G::SetCloudsOn), "b", 'v'},
        {"C_GEffectsModule::DressUniform", 0xA8, PortSlot(&G::DressUniform), "pi", 'b'},
        {"C_MeleeTrackerController::SetAlarm", 0x38, PortSlot(&C_MeleeTrackerController::SetAlarm), "i", 'v'},
        {"C_Actor::Hit", 0xD4, PortSlot(&C_Actor::Hit), "ifvvpi", 'v'},
        // (The camera test's: the seats.)
        {"C_Actor::GetSeatFlags", 0x8C, PortSlot(&C_Actor::GetSeatFlags), "i", 'i'},
        {"C_Actor::GetSeatCamera", 0x90, PortSlot(&C_Actor::GetSeatCamera), "i", 'p'},
        {"C_Actor::ShowSeatView", 0xC4, PortSlot(&C_Actor::ShowSeatView), "ib", 'v'},
        {"C_Actor::IsSeatFirstPersonOnly", 0xCC, PortSlot(&C_Actor::IsSeatFirstPersonOnly), "i", 'b'},
        {"C_Formation::AddMember", 0x08, PortSlot(&C_Formation::AddMember), "p", 'b'},
    };
}
// A class's virtual calls (by the name's prefix) as (offset, logger index) pairs on one side.
std::vector<std::pair<int, uint32_t>> VirtualSlots(const char *cls, bool original);

std::vector<std::pair<int, uint32_t>> VirtualSlots(const char *cls, bool original)
{
    std::vector<std::pair<int, uint32_t>> out;
    for (size_t v = 0; v < g_virtuals.size(); ++v)
        if (std::strncmp(g_virtuals[v].name, cls, std::strlen(cls)) == 0)
            out.push_back({original ? g_virtuals[v].orig : g_virtuals[v].port, 0x20000u + uint32_t(v)});
    return out;
}

// The command's name (the keyword table's).
std::string OpName(int opcode)
{
    for (int i = 0; g_scriptKeywords[i].name; ++i)
        if (g_scriptKeywords[i].token == opcode)
            return g_scriptKeywords[i].name;
    return F("op%d", opcode);
}

// ---- the sides, the fake answers ----
int g_side;                                             // 0 the port, 1 the original
void *g_frameScript[2];                                 // (part 5: each side's script of a frame case: "this" in the logs)
const char *const SIDE_NAMES[2] = {"port", "original"};
uint32_t g_fakeBase;
int g_fakeCount;
uint32_t Mix(uint32_t a, uint32_t b)
{
    uint32_t h = a ^ (b * 0x9E3779B9u);
    h ^= h >> 16;
    h *= 0x85EBCA6Bu;
    h ^= h >> 13;
    h *= 0xC2B2AE35u;
    h ^= h >> 16;
    return h;
}
uint32_t NextFake() { return Mix(g_fakeBase, uint32_t(g_fakeCount++)); }
int FakeInt()
{
    static const int values[] = {0, 0, 1, 1, 2, 3, 5, -1, 100, 7};
    return values[NextFake() % 10];
}
float FakeFloat()
{
    static const float values[] = {0.0f, 1.0f, 0.5f, 10.0f, -1.0f, 2.5f};
    return values[NextFake() % 6];
}

// ---- the names both sides' objects go by in the logs ----
std::map<uint32_t, std::string> g_names[2];
void Name(const void *p, const std::string &name) { g_names[g_side][reinterpret_cast<uint32_t>(p)] = name; }
std::string NameOf(uint32_t p)
{
    if (!p)
        return "null";
    auto it = g_names[g_side].find(p);
    return it != g_names[g_side].end() ? it->second : std::string("?");
}

// ---- the world (one per side, built alike) ----
struct S_FakeFrame {
    uint8_t *bytes;
    std::string name;
    uint32_t visualType;
};
struct S_World {
    std::vector<void *> blocks;
    std::vector<S_FakeFrame> frames;
    std::vector<std::string> strings;                   // (the frames' names: m_name points here)
    uint8_t *driver = nullptr, *scene = nullptr, *soundModule = nullptr, *cameraModule = nullptr, *geffects = nullptr,
            *lightmaps = nullptr, *bitstream = nullptr, *interface_ = nullptr, *checkpoints = nullptr;
    int created = 0;
    uint8_t *roads = nullptr;                           // (the checkpoints' road network: inside them)
    uint8_t *points[2] = {};                            // the walking network's, the roads' (S_Checkpoint, 0x24 bytes)
    uint8_t *profile = nullptr;                         // the player's profile (null: none)
    std::vector<uint8_t *> tshiftEntries;               // (S_TShiftEntry: on +8, reverse +9 on both sides)
    struct S_Actor {
        uint8_t *bytes;
        int type;
        std::string name;
        uint8_t *movement = nullptr;                    // (a human's)
        uint8_t *vehicle = nullptr;                     // (a car's: inside it)
        uint8_t *controller = nullptr;                  // (a human's)
    };
    std::vector<S_Actor> actors;
    std::vector<int> humans;                            // (indices into actors: the player team's members)
    std::vector<uint8_t *> list;                        // the actors module's (GetActor, GetActorById)
    uint8_t *playerTeam = nullptr, *coopTeam = nullptr, *junkers88 = nullptr, *decorators = nullptr;
    uint8_t *tracker = nullptr, *trackerAI = nullptr;   // (the melee tracker script's: on no frame; its controller)
};
S_World g_world[2];
S_World &W() { return g_world[g_side]; }
I3D_frame *AsFrame(uint8_t *b) { return reinterpret_cast<I3D_frame *>(b); }

// The thunks: push the index, push 0, jump to the logger.
uint8_t *g_thunks;
int g_thunkCount;
std::map<uint32_t, void *> g_thunkOf;
void *Thunk(uint32_t index)
{
    auto it = g_thunkOf.find(index);
    if (it != g_thunkOf.end())
        return it->second;
    uint8_t *t = g_thunks + 16 * g_thunkCount++;
    t[0] = 0x68;
    std::memcpy(t + 1, &index, 4);
    t[5] = 0x6A;
    t[6] = 0x00;
    t[7] = 0xE9;
    const int32_t rel = int32_t(reinterpret_cast<uintptr_t>(&CmdCallEntry) - (reinterpret_cast<uintptr_t>(t) + 12));
    std::memcpy(t + 8, &rel, 4);
    g_thunkOf[index] = t;
    return t;
}
// A trap: a call through a slot the test does not know (its arguments unknown) ends the test.
std::vector<std::string> g_trapNames;
void *Trap(const std::string &what)
{
    g_trapNames.push_back(what);
    return Thunk(TRAP_BASE + uint32_t(g_trapNames.size() - 1));
}

// The engine's vtables: a class's slots and its bases', the rest traps.
std::map<std::string, void **> g_engineVtables;
void **EngineVtable(const std::string &cls)
{
    auto it = g_engineVtables.find(cls);
    if (it != g_engineVtables.end())
        return it->second;
    void **vt = new void *[0x80];
    for (int i = 0; i < 0x80; ++i)
        vt[i] = nullptr;
    for (std::string c = cls; !c.empty();) {
        for (int s = 0; s < NSLOTS; ++s)
            if (c == ENGINE_SLOTS[s].cls && !vt[ENGINE_SLOTS[s].off / 4])
                vt[ENGINE_SLOTS[s].off / 4] = Thunk(SLOT_BASE + uint32_t(s));
        std::string base;
        for (const auto &b : ENGINE_CLASSES)
            if (c == b[0])
                base = b[1];
        c = base;
    }
    for (int i = 0; i < 0x80; ++i)
        if (!vt[i])
            vt[i] = Trap(cls + F(" slot 0x%02X", i * 4));
    g_engineVtables[cls] = vt;
    return vt;
}
// A game object's vtable on one side: the given calls at their offsets, the rest traps.
void **GameVtable(const std::string &cls, const std::vector<std::pair<int, uint32_t>> &slots)
{
    void **vt = new void *[0x60];
    for (int i = 0; i < 0x60; ++i)
        vt[i] = Trap(cls + F(" slot 0x%02X (%s)", i * 4, SIDE_NAMES[g_side]));
    for (const auto &s : slots)
        vt[s.first / 4] = Thunk(s.second);
    return vt;
}
std::map<std::string, void **> g_gameVtables[2];

uint8_t *Block(size_t size)
{
    void *p = _aligned_malloc(size, 16);
    std::memset(p, 0, size);
    W().blocks.push_back(p);
    return static_cast<uint8_t *>(p);
}

const char *FrameClass(int type)
{
    switch (type) {
    case FRAME_VISUAL: return "I3D_object_visual";
    case FRAME_LIGHT: return "I3D_light";
    case FRAME_CAMERA: return "I3D_camera";
    case FRAME_SOUND: return "I3D_sound";
    case FRAME_SECTOR: return "I3D_sector";
    case FRAME_DUMMY: return "I3D_dummy";
    case FRAME_TARGET: return "I3D_target";
    case FRAME_MODEL: return "I3D_model";
    case FRAME_JOINT: return "I3D_joint";
    default: return "I3D_frame";
    }
}

// The world's own random numbers (the same seed on both sides).
uint32_t g_worldSeed;
uint32_t WR()
{
    g_worldSeed = g_worldSeed * 1664525u + 1013904223u;
    return g_worldSeed >> 8;
}
int WI(int lo, int hi) { return lo + int(WR() % uint32_t(hi - lo + 1)); }
// (Half the time near the origin: the boxes and ranges then hold some of the others.)
float WF() { return WI(0, 1) ? float(WI(-2000, 2000)) * 0.25f : float(WI(-40, 40)) * 0.25f; }

constexpr float DEG_TO_RAD_H = 0.017453292f;
uint8_t *NewFrame(int type, const std::string &name)
{
    S_World &w = W();
    uint8_t *b = Block(0x600);
    *reinterpret_cast<void ***>(b) = EngineVtable(FrameClass(type));
    I3D_frame *f = AsFrame(b);
    w.strings.push_back(name);
    f->m_name = nullptr;
    f->m_type = E_I3D_FRAME_TYPE(type);
    f->m_refCount = 1;
    f->m_localMatrix.Identity();
    f->m_localMatrix.m[3][0] = WF();
    f->m_localMatrix.m[3][1] = WF();
    f->m_localMatrix.m[3][2] = WF();
    f->m_rot = S_quat(1.0f, 0.0f, 0.0f, 0.0f);
    f->m_scale = S_vector(1.0f, 1.0f, 1.0f);
    // Turned, mostly (the rotation and the local matrix both valid).
    if (WI(0, 3)) {
        f->m_rot = S_quat(float(WI(-100, 100)), float(WI(-100, 100)), float(WI(-100, 100)), float(WI(-100, 100)));
        if (f->m_rot.s == 0.0f && f->m_rot.v.x == 0.0f && f->m_rot.v.y == 0.0f && f->m_rot.v.z == 0.0f)
            f->m_rot.s = 1.0f;
        f->m_rot.Normalize();
        const S_vector pos = f->m_localMatrix(3);
        f->m_localMatrix.SetRot3(f->m_rot);
        f->m_localMatrix(3) = pos;
    }
    f->m_frameFlags = (WI(0, 3) ? 1u : 0u) | FRMFLAGS_LMATRIX_VALID | FRMFLAGS_ROT_VALID | FRMFLAGS_ROT_DIRTY;
    if (type == FRAME_DUMMY) {
        I3D_bbox &box = *reinterpret_cast<I3D_bbox *>(b + 0x120);
        box.min = S_vector(-WI(1, 8) * 1.0f, -WI(1, 8) * 1.0f, -WI(1, 8) * 1.0f);
        box.max = S_vector(WI(1, 8) * 1.0f, WI(1, 8) * 1.0f, WI(1, 8) * 1.0f);
    }
    if (type == FRAME_CAMERA) {                         // FOV +0x120, range +0x124 / +0x128, aspect +0x138
        *reinterpret_cast<float *>(b + 0x120) = float(WI(30, 120)) * DEG_TO_RAD_H;
        *reinterpret_cast<float *>(b + 0x124) = float(WI(1, 20)) * 0.1f;
        *reinterpret_cast<float *>(b + 0x128) = float(WI(100, 3000));
        *reinterpret_cast<float *>(b + 0x138) = float(WI(100, 200)) * 0.01f;
    }
    static const uint32_t visuals[] = {I3DVISUAL_OBJECT, I3DVISUAL_LMAP, I3DVISUAL_SINGLEMESH, I3DVISUAL_BILLBOARD};
    if (w.frames.size() == w.frames.capacity()) {
        std::printf("cmd_diff: more frames than BuildWorld reserves [%s]\n", g_current.c_str());
        std::exit(2);
    }
    w.frames.push_back({b, name, type == FRAME_VISUAL ? visuals[WI(0, 3)] : 0});
    Name(b, "frame " + name);
    // A visual's mesh (GetVisualMesh, +0x1E0), mostly.
    if (type == FRAME_VISUAL && WI(0, 2)) {
        uint8_t *mesh = Block(0x40);
        *reinterpret_cast<uint8_t **>(b + 0x1E0) = mesh;
        Name(mesh, "mesh " + name);
    }
    return b;
}
void LinkUnder(uint8_t *child, uint8_t *parent)
{
    I3D_frame *c = AsFrame(child), *p = AsFrame(parent);
    c->m_parent = p;
    c->m_nextSibling = p->m_firstChild;
    p->m_firstChild = c;
}

// The checkpoints' names (in both networks; the rest are not found).
const char *const CHECKPOINT_NAMES[] = {"door", "cam1", "a", "x", "dummy1"};
constexpr int CHECKPOINT_NAME_COUNT = int(sizeof CHECKPOINT_NAMES / sizeof *CHECKPOINT_NAMES);

// Names the strings take (the frames' among them).
// (16..: a human model's parts the cutscene commands look for: the holsters hw1 / hw2, the hand rw, the pack d.)
const char *const FRAME_NAMES[] = {"door", "hlava", "model1", "snd_a", "cam1", "dummy1", "light1", "visual1", "joint1",
                                   "sector1", "model2", "child1", "child2", "cam2", "snd_b", "dummy2",
                                   "hw1", "hw2", "rw", "d", "pack1", "pack2", "weapon", "target1"};
const int FRAME_TYPES[] = {FRAME_VISUAL, FRAME_JOINT, FRAME_MODEL, FRAME_SOUND, FRAME_CAMERA, FRAME_DUMMY, FRAME_LIGHT,
                           FRAME_VISUAL, FRAME_JOINT, FRAME_SECTOR, FRAME_MODEL, FRAME_VISUAL, FRAME_DUMMY,
                           FRAME_CAMERA, FRAME_SOUND, FRAME_DUMMY,
                           FRAME_JOINT, FRAME_JOINT, FRAME_JOINT, FRAME_DUMMY, FRAME_VISUAL, FRAME_JOINT, FRAME_VISUAL,
                           FRAME_TARGET};
constexpr int WORLD_FRAMES = int(sizeof FRAME_NAMES / sizeof *FRAME_NAMES);

void SetGameField(uint32_t off, uint32_t value);

// ---- the actors: on about half the frames (S_FrameData's actor), of every kind the commands tell apart ----
constexpr uint32_t O_TEAM_MANAGER = 0x0089ae78, O_JUNKERS88 = 0x008ac9e4, O_DECORATORS = 0x0089bbb0,
                   O_ACTORS_MODULE = 0x0089b4b0, O_TEMPORARY_ACTORS = 0x0089b5d0;
const int ACTOR_KINDS[] = {ACTOR_HUMAN, ACTOR_HUMAN, ACTOR_HUMAN, ACTOR_HUMAN, ACTOR_DOOR, ACTOR_RADIO,
                           ACTOR_SUPPLY_BOX, ACTOR_ITEM, ACTOR_SWITCH, ACTOR_TRAIN, ACTOR_STATIC_WEAPON,
                           ACTOR_EXPLOSIVE_PLACE, ACTOR_DOCUMENT_PLACE, ACTOR_ANIMATION, ACTOR_PIKE,
                           ACTOR_FOWL_GENERATOR, ACTOR_MELEE_TRACKER, ACTOR_JUNKERS88, ACTOR_WIRE_CUT, ACTOR_AIR_ATTACK,
                           ACTOR_TS_EXPLOSION, ACTOR_CAR, ACTOR_CAR, ACTOR_BARREL, ACTOR_PHOTO_PLACE, ACTOR_BIKE};
constexpr int ACTOR_KIND_COUNT = int(sizeof ACTOR_KINDS / sizeof *ACTOR_KINDS);
// A kind's block: the bigger of the port's class and the original's (0x25C0 a car, 0x300 for the rest read here).
size_t ActorBytes(int type = 0)
{
    switch (type) {
    case ACTOR_HUMAN: return std::max(sizeof(C_Human), size_t(0x300)) + 0x40;
    case ACTOR_CAR: return std::max(sizeof(C_Car), size_t(0x25C0)) + 0x40;
    case ACTOR_JUNKERS88: return std::max(sizeof(C_Junkers88), size_t(0x300)) + 0x40;
    case ACTOR_MELEE_TRACKER: return std::max(sizeof(C_MeleeTracker), size_t(0x540)) + 0x40;
    default:
        return std::max({sizeof(C_Door), sizeof(C_Train), sizeof(C_AnimationActor), sizeof(C_Actor), sizeof(C_StaticWeapon),
                         sizeof(C_DocumentPlace), sizeof(C_Pike), sizeof(C_Radio), sizeof(C_Switch), sizeof(C_Bike), size_t(0x300)}) +
               0x40;
    }
}
using W_Actor = S_World::S_Actor;
// The state fields the functions read inline (GetState): kind, the original's offset, the port's, bytes.
struct S_StateField {
    int type;
    uint32_t orig;
    size_t port;
    int bytes;
};
const S_StateField STATE_FIELDS[] = {
    {ACTOR_RADIO, 0xA0, offsetof(C_Radio, m_state), 4},
    {ACTOR_SWITCH, 0xD4, offsetof(C_Switch, m_state), 4},
    {ACTOR_PHOTO_PLACE, 0x91, offsetof(C_PhotoPlace, m_photographed), 1},
    {ACTOR_STATIC_WEAPON, 0x2F0, offsetof(C_StaticWeapon, m_state), 4},
    {ACTOR_EXPLOSIVE_PLACE, 0x94, offsetof(C_ExplosivePlace, m_state), 4},
    {ACTOR_DOCUMENT_PLACE, 0x148, offsetof(C_DocumentPlace, m_state), 4},
    {ACTOR_PIKE, 0x184, offsetof(C_Pike, m_state), 4},
    {ACTOR_TS_EXPLOSION, 0x90, offsetof(C_TSExplosion, m_flags), 4},
    {ACTOR_DOOR, 0x224, offsetof(C_Door, m_motion), 4},
    {ACTOR_DOOR, 0x234, offsetof(C_Door, m_locked), 1},
};
// The fields: the original's offsets (the headers' comments), the port's members.
void BuildActors(int side)
{
    S_World &w = W();
    static void **vtables[2];
    if (!vtables[side])
        vtables[side] = GameVtable("C_Actor", VirtualSlots("C_Actor::", side != 0));
    const int frameCount = int(w.frames.size());
    for (int f = 0; f < frameCount; ++f) {
        if (WI(0, 1))
            continue;
        uint8_t *frame = w.frames[f].bytes;
        int type = ACTOR_KINDS[WI(0, ACTOR_KIND_COUNT - 1)];
        // (Mostly humans on the frames BuildWorld may put near a box's faces: players there for _PlayerInBox.)
        if ((f == 3 || f == 6 || f == 7 || f == 23) && WI(0, 2))
            type = ACTOR_HUMAN;
        uint8_t *a = Block(ActorBytes(type));
        *reinterpret_cast<void ***>(a) = vtables[side];
        W_Actor act{a, type, "actor " + w.frames[f].name};
        Name(a, act.name);
        S_FrameData *data = reinterpret_cast<S_FrameData *>(Block(0x20));
        data->actor = reinterpret_cast<C_Actor *>(a);
        AsFrame(frame)->m_data = data;
        if (side) {
            *reinterpret_cast<int *>(a + 0x18) = type;
            *reinterpret_cast<uint8_t **>(a + 0x20) = frame;
        } else {
            reinterpret_cast<C_Actor *>(a)->m_type = E_ActorType(type);
            reinterpret_cast<C_Actor *>(a)->m_frame = AsFrame(frame);
        }
        switch (type) {
        case ACTOR_HUMAN: {
            static const int healths[] = {0, 1, 50, 100, 100, -5};
            const int health = healths[WI(0, 5)], role = WI(0, 6);
            act.movement = Block(0x100);
            Name(act.movement, "movement of " + act.name);
            uint8_t *body = w.frames[WI(0, frameCount - 1)].bytes;
            // Half the time a dummy's own frame: its centre, strictly inside its box (HUMAN_KillAllInBox).
            if (WI(0, 1)) {
                static const int dummies[] = {5, 12, 15, 19};
                body = w.frames[dummies[WI(0, 3)]].bytes;
            }
            // Its controller (the original's +0xB0: its type +0x48, the current checkpoint +0xA4, the circumvention
            // flag +0x11C), its work (+0xA4: the body's +0x0C).
            static const int controllerTypes[] = {HUMAN_CONTROLLER_PLAYER, HUMAN_CONTROLLER_SQUAD, HUMAN_CONTROLLER_ENEMY,
                                                  HUMAN_CONTROLLER_AI, HUMAN_CONTROLLER_AI, HUMAN_CONTROLLER_AI,
                                                  HUMAN_CONTROLLER_NEUTRAL, HUMAN_CONTROLLER_REMOTE, HUMAN_CONTROLLER_SNIPER};
            const int controllerType = controllerTypes[WI(0, 8)];
            act.controller = Block(std::max(sizeof(C_ScriptedController), size_t(0x200)) + 0x40);
            Name(act.controller, "controller of " + act.name);
            uint8_t *work = Block(0x100);
            Name(work, "work of " + act.name);
            const int point = WI(-1, CHECKPOINT_NAME_COUNT - 1);
            const uint8_t *current = point < 0 ? nullptr : w.points[0] + 0x24 * point;
            if (side) {
                *reinterpret_cast<int *>(a + 0xEC) = health;
                *reinterpret_cast<int *>(a + 0x19C) = role;
                *reinterpret_cast<uint8_t **>(a + 0x98 + 8) = act.movement;
                *reinterpret_cast<uint8_t **>(a + 0x260) = body;
                *reinterpret_cast<uint8_t **>(a + 0xB0) = act.controller;
                *reinterpret_cast<uint8_t **>(a + 0xA4) = work;
                *reinterpret_cast<int *>(act.controller + 0x48) = controllerType;
                *reinterpret_cast<const uint8_t **>(act.controller + 0xA4) = current;
            } else {
                C_Human *h = reinterpret_cast<C_Human *>(a);
                h->m_stats[STAT_HEALTH] = health;
                h->m_role = E_HumanRole(role);
                h->m_body.movement = reinterpret_cast<C_HumanMovement *>(act.movement);
                h->m_bodyFrame = AsFrame(body);
                h->m_controller = reinterpret_cast<C_HumanController *>(act.controller);
                h->m_body.work = reinterpret_cast<C_HumanWork *>(work);
                h->m_controller->m_type = E_HumanController(controllerType);
                h->m_controller->m_currentCheckpoint = reinterpret_cast<S_Checkpoint *>(const_cast<uint8_t *>(current));
            }
            w.humans.push_back(int(w.actors.size()));
            break;
        }
        case ACTOR_DOOR: {
            const int key = WI(-1, 5);
            if (side)
                *reinterpret_cast<int *>(a + 0x230) = key;
            else
                reinterpret_cast<C_Door *>(a)->m_key = key;
            break;
        }
        case ACTOR_ANIMATION: {
            const bool stopped = WI(0, 1) != 0;
            if (side)
                a[0xA1] = stopped;
            else
                reinterpret_cast<C_AnimationActor *>(a)->m_stopped = stopped;
            break;
        }
        case ACTOR_CAR:
            act.vehicle = side ? a + 0x90 : reinterpret_cast<uint8_t *>(reinterpret_cast<C_Car *>(a)->GetVehicle());
            Name(act.vehicle, "vehicle of " + act.name);
            break;
        case ACTOR_MELEE_TRACKER: {
            // Its controller (the original's +0x534; its alarm at +0x1C).
            uint8_t *ai = Block(std::max(sizeof(C_MeleeTrackerController), size_t(0x100)) + 0x40);
            Name(ai, "controller of " + act.name);
            const int alarm = WI(0, 4);
            if (side) {
                *reinterpret_cast<uint8_t **>(a + 0x534) = ai;
                *reinterpret_cast<int *>(ai + 0x1C) = alarm;
            } else {
                reinterpret_cast<C_MeleeTracker *>(a)->m_ai = reinterpret_cast<C_MeleeTrackerController *>(ai);
                reinterpret_cast<C_MeleeTrackerController *>(ai)->m_alarm = alarm;
            }
            break;
        }
        case ACTOR_TRAIN: {
            const bool running = WI(0, 1) != 0;
            if (side)
                a[0x19C] = running;
            else
                reinterpret_cast<C_Train *>(a)->m_running = running;
            break;
        }
        default:
            break;
        }
        for (const S_StateField &s : STATE_FIELDS)
            if (s.type == type) {
                const int value = s.bytes == 1 ? WI(0, 1) : WI(-1, 4);
                std::memcpy(a + (side ? s.orig : s.port), &value, size_t(s.bytes));
            }
        w.actors.push_back(act);
    }
    // The network ids (the original's +0x10), the humans' vehicles (+0x160, a car, a static weapon or another actor; the
    // seat +0x164).
    for (size_t i = 0; i < w.actors.size(); ++i) {
        uint8_t *a = w.actors[i].bytes;
        const int id = 100 + int(i);
        if (side)
            *reinterpret_cast<int *>(a + 0x10) = id;
        else
            reinterpret_cast<C_Actor *>(a)->m_netId = id;
    }
    for (int h : w.humans) {
        uint8_t *a = w.actors[h].bytes;
        uint8_t *vehicle = nullptr;
        if (WI(0, 1)) {
            std::vector<uint8_t *> rides;
            for (const W_Actor &x : w.actors)
                if (x.type == ACTOR_CAR || x.type == ACTOR_STATIC_WEAPON || (x.type != ACTOR_HUMAN && !WI(0, 3)))
                    rides.push_back(x.bytes);
            if (!rides.empty())
                vehicle = rides[WI(0, int(rides.size()) - 1)];
        }
        const int seat = WI(0, 2) ? 0 : WI(-1, 3);
        if (side) {
            *reinterpret_cast<uint8_t **>(a + 0x160) = vehicle;
            *reinterpret_cast<int *>(a + 0x164) = seat;
        } else {
            reinterpret_cast<C_Human *>(a)->m_vehicle = reinterpret_cast<C_Actor *>(vehicle);
            reinterpret_cast<C_Human *>(a)->m_seat = seat;
        }
    }
    // The melee tracker script's tracker (on no frame) and its controller (the original's +0x534; the mode +0x14: the
    // full AI's 4 and 5 mostly, the others the port skips).
    {
        static void **controllerVtables[2];
        if (!controllerVtables[side])
            controllerVtables[side] = GameVtable("C_MeleeTrackerController", VirtualSlots("C_MeleeTrackerController::", side != 0));
        w.tracker = Block(ActorBytes(ACTOR_MELEE_TRACKER));
        *reinterpret_cast<void ***>(w.tracker) = vtables[side];
        Name(w.tracker, "the script's tracker");
        w.trackerAI = Block(std::max(sizeof(C_MeleeTrackerScriptedAI), size_t(0x100)) + 0x40);
        *reinterpret_cast<void ***>(w.trackerAI) = controllerVtables[side];
        Name(w.trackerAI, "the script's tracker's controller");
        static const int modes[] = {4, 5, 5, 4, 5, 1, 2, 3};
        const int mode = modes[WI(0, 7)];
        if (side) {
            *reinterpret_cast<int *>(w.tracker + 0x18) = ACTOR_MELEE_TRACKER;
            *reinterpret_cast<uint8_t **>(w.tracker + 0x534) = w.trackerAI;
            *reinterpret_cast<int *>(w.trackerAI + 0x14) = mode;
        } else {
            reinterpret_cast<C_Actor *>(w.tracker)->m_type = ACTOR_MELEE_TRACKER;
            reinterpret_cast<C_MeleeTracker *>(w.tracker)->m_ai = reinterpret_cast<C_MeleeTrackerController *>(w.trackerAI);
            reinterpret_cast<C_MeleeTrackerController *>(w.trackerAI)->m_mode = mode;
        }
    }
    // The actors module's list: the player slots first (every other human, then empty), then the other actors and the
    // rest of the humans (the original's vector at +0x0C: first +0x10, last +0x14, end +0x18).
    w.list.clear();
    std::vector<uint8_t *> players, others;
    for (size_t h = 0; h < w.humans.size(); ++h)
        (h % 2 ? others : players).push_back(w.actors[w.humans[h]].bytes);
    for (int i = 0; i < C_ActorsModule::PLAYER_SLOTS; ++i)
        w.list.push_back(i < int(players.size()) ? players[i] : nullptr);
    for (const W_Actor &x : w.actors)
        if (x.type != ACTOR_HUMAN)
            w.list.push_back(x.bytes);
    w.list.insert(w.list.end(), others.begin(), others.end());
    // The temporary actors: a few mines (not on frames) at places near the frames.
    std::vector<uint8_t *> mines;
    for (int m = WI(0, 3); m > 0; --m) {
        uint8_t *mine = Block(ActorBytes(ACTOR_MINE));
        *reinterpret_cast<void ***>(mine) = vtables[side];
        Name(mine, F("mine%d", m));
        const S_vector pos(WF() * 0.1f, WF() * 0.1f, WF() * 0.1f);
        if (side) {
            *reinterpret_cast<int *>(mine + 0x18) = WI(0, 3) ? ACTOR_MINE : ACTOR_ITEM;
            std::memcpy(mine + 0x30, &pos, 12);
        } else {
            reinterpret_cast<C_Actor *>(mine)->m_type = E_ActorType(WI(0, 3) ? ACTOR_MINE : ACTOR_ITEM);
            reinterpret_cast<C_Actor *>(mine)->SetPosition(pos);
        }
        mines.push_back(mine);
    }
    auto setVector = [&](uint32_t vectorAt, const std::vector<uint8_t *> &items) {
        uint8_t **list = reinterpret_cast<uint8_t **>(Block(4 * (items.size() + 1)));
        std::copy(items.begin(), items.end(), list);
        *OriginalAt<uint8_t ***>(vectorAt + 4) = list;
        *OriginalAt<uint8_t ***>(vectorAt + 8) = list + items.size();
        *OriginalAt<uint8_t ***>(vectorAt + 12) = list + items.size();
    };
    if (side) {
        setVector(O_ACTORS_MODULE + 0x0C, w.list);
        setVector(O_TEMPORARY_ACTORS + 0x0C, mines);
    } else {
        g_actorsModule.m_actors.clear();
        for (uint8_t *x : w.list)
            g_actorsModule.m_actors.push_back(reinterpret_cast<C_Actor *>(x));
        g_temporaryActors.m_actors.clear();
        for (uint8_t *x : mines)
            g_temporaryActors.m_actors.push_back(reinterpret_cast<C_Actor *>(x));
    }
    Name(side ? OriginalAt<void *>(O_TEMPORARY_ACTORS) : static_cast<void *>(&g_temporaryActors), "temporary actors");
    Name(side ? OriginalAt<void *>(O_ACTORS_MODULE) : static_cast<void *>(&g_actorsModule), "actors module");
    // The teams (the team manager's +0x1C and +0xE8; the members answered from the world's humans).
    w.playerTeam = side ? OriginalAt<uint8_t *>(O_TEAM_MANAGER + 0x1C) : reinterpret_cast<uint8_t *>(g_teamManager.GetPlayerTeam());
    w.coopTeam = side ? OriginalAt<uint8_t *>(O_TEAM_MANAGER + 0xE8) : reinterpret_cast<uint8_t *>(g_teamManager.GetCoopTeam());
    Name(w.playerTeam, "player team");
    Name(w.coopTeam, "coop team");
    Name(side ? OriginalAt<void *>(O_TEAM_MANAGER) : static_cast<void *>(&g_teamManager), "team manager");
    // The Junkers 88 and the decorators (globals: null or one).
    w.junkers88 = WI(0, 2) ? Block(ActorBytes(ACTOR_JUNKERS88)) : nullptr;
    w.decorators = WI(0, 1) ? Block(0x100) : nullptr;
    if (w.junkers88)
        Name(w.junkers88, "junkers88");
    if (w.decorators)
        Name(w.decorators, "decorators");
    if (side) {
        *OriginalAt<uint8_t **>(O_JUNKERS88) = w.junkers88;
        *OriginalAt<uint8_t **>(O_DECORATORS) = w.decorators;
    } else {
        g_junkers88 = reinterpret_cast<C_Junkers88 *>(w.junkers88);
        g_decorators = reinterpret_cast<C_Decorators *>(w.decorators);
    }
}

void BuildWorld(int side, uint32_t seed)
{
    g_side = side;
    g_worldSeed = seed;
    S_World &w = W();
    g_names[side].clear();
    // (Room for every frame a case creates: the frames' m_name point into these strings, which must not move.)
    w.frames.reserve(1024);
    for (int i = 0; i < WORLD_FRAMES; ++i)
        NewFrame(FRAME_TYPES[i], FRAME_NAMES[i]);
    // (The names' storage settled: m_name points into it.)
    for (S_FakeFrame &f : w.frames)
        AsFrame(f.bytes)->m_name = f.name.c_str();
    // A hierarchy: model1 holds hlava, child1 and joint1; model2 holds child2; the rest at the root.
    LinkUnder(w.frames[1].bytes, w.frames[2].bytes);
    LinkUnder(w.frames[11].bytes, w.frames[2].bytes);
    LinkUnder(w.frames[8].bytes, w.frames[2].bytes);
    LinkUnder(w.frames[12].bytes, w.frames[10].bytes);
    // model1 holds hw1 (holding weapon), rw and d (holding pack1 and pack2); model2 holds hw2.
    LinkUnder(w.frames[16].bytes, w.frames[2].bytes);
    LinkUnder(w.frames[18].bytes, w.frames[2].bytes);
    LinkUnder(w.frames[19].bytes, w.frames[2].bytes);
    LinkUnder(w.frames[17].bytes, w.frames[10].bytes);
    LinkUnder(w.frames[20].bytes, w.frames[19].bytes);
    LinkUnder(w.frames[21].bytes, w.frames[19].bytes);
    LinkUnder(w.frames[22].bytes, w.frames[16].bytes);
    // Often, snd_a, light1, visual1 and target1 under a dummy, near its box's faces, inside or out (the boxes' turned
    // corners matter there: HUMAN_KillAllInBox, _PlayerInBox).
    for (int f : {3, 6, 7, 23}) {
        if (!WI(0, 1))
            continue;
        static const int dummies[] = {5, 12, 15, 19};
        uint8_t *dummy = w.frames[dummies[WI(0, 3)]].bytes;
        const I3D_bbox &box = *reinterpret_cast<const I3D_bbox *>(dummy + 0x120);
        LinkUnder(w.frames[f].bytes, dummy);
        S_matrix &m = AsFrame(w.frames[f].bytes)->m_localMatrix;
        for (int i = 0; i < 3; ++i)
            m.m[3][i] = (WI(0, 1) ? (&box.max.x)[i] : (&box.min.x)[i]) * float(WI(80, 120)) * 0.01f;
    }
    // The engine and the game.
    w.driver = Block(0x400);
    *reinterpret_cast<void ***>(w.driver) = EngineVtable("I3D_driver");
    Name(w.driver, "driver");
    w.scene = Block(0x400);
    *reinterpret_cast<void ***>(w.scene) = EngineVtable("I3D_scene");
    Name(w.scene, "scene");
    // The active camera (always one: CSC_SetCameraFOV uses it unchecked on both sides).
    *reinterpret_cast<uint8_t **>(w.scene + 0x15C) = WI(0, 2) ? w.frames[4].bytes : w.frames[13].bytes;
    *reinterpret_cast<uint8_t **>(w.scene + 0x1F8) = w.frames[9].bytes;                     // the primary sector
    auto game = [&](const char *cls, const std::vector<std::pair<int, uint32_t>> &origSlots,
                    const std::vector<std::pair<int, uint32_t>> &portSlots) {
        uint8_t *b = Block(0x400);
        const std::string key = cls;
        auto &cache = g_gameVtables[side];
        if (!cache.count(key))
            cache[key] = GameVtable(cls, side ? origSlots : portSlots);
        *reinterpret_cast<void ***>(b) = cache[key];
        Name(b, cls);
        return b;
    };
    std::vector<std::pair<int, uint32_t>> soundSlots, cameraSlots;
    for (int i = 0; i < NCALLS; ++i)
        if (CALLS[i].vt >= 0) {
            if (std::strncmp(CALLS[i].name, "C_SoundModule::", 15) == 0)
                soundSlots.push_back({CALLS[i].vt, uint32_t(i)});
            else if (std::strncmp(CALLS[i].name, "C_CameraModule::", 16) == 0)
                cameraSlots.push_back({CALLS[i].vt, uint32_t(i)});
        }
    w.soundModule = game("C_SoundModule", soundSlots, {});
    w.cameraModule = game("C_CameraModule", cameraSlots, {});
    w.geffects = game("C_GEffectsModule", VirtualSlots("C_GEffectsModule::", true), VirtualSlots("C_GEffectsModule::", false));
    w.lightmaps = game("C_LightMapGroupsModule", {}, {});
    w.bitstream = game("C_BitStream", {}, {});
    // (The interface: the original's hints are a vector at +0x6B3A0; the port's in its m_info.)
    w.interface_ = side ? Block(0x6C000) : Block(sizeof(C_Interface) + 16);
    Name(w.interface_, "C_Interface");
    // Some hints shown already (HINT_ClearAll; the original's vector gets room for 64: no case grows it past).
    {
        std::vector<int> hints(WI(0, 1) ? 0 : WI(1, 3));
        for (int &h : hints)
            h = WI(0, 20);
        if (side) {
            uint8_t *buf = Block(64 * 4);
            std::memcpy(buf, hints.data(), hints.size() * 4);
            *reinterpret_cast<uint8_t **>(w.interface_ + 0x6B3A0 + 4) = buf;
            *reinterpret_cast<uint8_t **>(w.interface_ + 0x6B3A0 + 8) = buf + hints.size() * 4;
            *reinterpret_cast<uint8_t **>(w.interface_ + 0x6B3A0 + 0xC) = buf + 64 * 4;
        } else
            reinterpret_cast<C_Interface *>(w.interface_)->m_info.m_hints = hints;
    }
    // The checkpoints: a few named points in each network (Find answers by name, Get the point).
    w.checkpoints = Block(side ? 0x100 : sizeof(C_Checkpoints) + 16);
    Name(w.checkpoints, "checkpoints");
    w.roads = side ? w.checkpoints + 0x3C : reinterpret_cast<uint8_t *>(reinterpret_cast<C_Checkpoints *>(w.checkpoints)->GetRoadNetwork());
    Name(w.roads, "roads");
    for (uint8_t *&points : w.points) {
        points = Block(0x24 * CHECKPOINT_NAME_COUNT);
        for (int i = 0; i < CHECKPOINT_NAME_COUNT; ++i) {
            *reinterpret_cast<S_vector *>(points + 0x24 * i) = S_vector(WF(), WF(), WF());
            static const uint16_t flags[] = {0, 0, 0x100, 0x101, 0x001, 0x200, 0x300};
            *reinterpret_cast<uint16_t *>(points + 0x24 * i + 0x14) = flags[WI(0, 6)];
            Name(points + 0x24 * i, F("point %d of %s", i, &points == &w.points[0] ? "walks" : "roads"));
        }
    }
    // The game's fields: the scene, the checkpoints, the interface, the mission's time (mostly started: the commands
    // that advance the script do so only then) (the original's offsets; the port's members).
    const uint32_t missionTime = WI(0, 3) ? uint32_t(WI(1, 100000)) : 0u;
    if (side) {
        SetGameField(0x28, missionTime);
        SetGameField(0x18, reinterpret_cast<uint32_t>(w.scene));
        SetGameField(0x54, reinterpret_cast<uint32_t>(w.checkpoints));
        SetGameField(0x58, reinterpret_cast<uint32_t>(w.interface_));
    } else {
        C_game *g = reinterpret_cast<C_game *>(g_portGameBuf);
        g->m_scene = reinterpret_cast<I3D_scene *>(w.scene);
        g->m_checkpoints = reinterpret_cast<C_Checkpoints *>(w.checkpoints);
        g->m_interface = reinterpret_cast<C_Interface *>(w.interface_);
        g->m_missionTime = missionTime;
    }
    Name(side ? static_cast<void *>(g_origGame) : static_cast<void *>(g_portGameBuf), "game");
    // The role: single player, a dedicated server or a listen server.
    static const uint32_t roles[] = {GAME_ROLE_LOCAL, GAME_ROLE_LOCAL, GAME_ROLE_SERVER,
                                     GAME_ROLE_SERVER | GAME_ROLE_CLIENT};
    SetRole(roles[WI(0, 3)]);
    // The script module's cutscene flags (the original's at 0x00868b18 + 0x824 / 0x825; the port's members).
    const bool cutscene = WI(0, 1) != 0, fake = WI(0, 3) == 0;
    if (side) {
        *OriginalAt<uint8_t *>(O_SCRIPT_MODULE + 0x824) = cutscene;
        *OriginalAt<uint8_t *>(O_SCRIPT_MODULE + 0x825) = fake;
    } else {
        g_scriptModule.m_cutsceneRunning = cutscene;
        g_scriptModule.m_fakeCutscene = fake;
    }
    Name(side ? OriginalAt<void *>(O_SCRIPT_MODULE) : static_cast<void *>(&g_scriptModule), "script module");
    // The player's profile (the game profile's +0x20 / m_player), its debriefing inside it (+0xDC / m_debriefing).
    w.profile = WI(0, 3) ? Block(side ? 0x400 : sizeof(C_PlayerProfile) + 16) : nullptr;
    if (w.profile) {
        Name(w.profile, "player profile");
        Name(side ? w.profile + 0xDC : reinterpret_cast<uint8_t *>(&reinterpret_cast<C_PlayerProfile *>(w.profile)->m_debriefing),
             "debriefing");
    }
    if (side)
        *OriginalAt<uint8_t **>(O_GAME_PROFILE + 0x20) = w.profile;
    else
        g_gameProfile.m_player = reinterpret_cast<C_PlayerProfile *>(w.profile);
    Name(side ? OriginalAt<void *>(O_GAME_PROFILE) : static_cast<void *>(&g_gameProfile), "game profile");
    // The play mode (the game profile's +0x28; the port's g_playMode, a reference: its link stub's word points to a
    // pointer here).
    uint8_t *mode = Block(0x20);
    *reinterpret_cast<int *>(mode + 4) = WI(0, 1);
    *reinterpret_cast<int *>(mode + 8) = WI(-1, 8);                // (past 10 with the single mission's 4: no type)
    if (side) {
        *OriginalAt<uint8_t **>(O_GAME_PROFILE + 0x28) = mode;
    } else {
        static uint8_t *cell;
        cell = mode;
        g_playModeStorage = &cell;
    }
    BuildActors(side);
}
void FreeWorld(int side)
{
    if (!side && g_world[side].interface_)
        reinterpret_cast<C_Interface *>(g_world[side].interface_)->m_info.m_hints.~vector();
    for (void *p : g_world[side].blocks)
        _aligned_free(p);
    g_world[side] = S_World();
}

// The game's fields on the current side (the original's offsets; the port's C_game keeps them, probed in SetUp).
void SetGameField(uint32_t off, uint32_t value)
{
    uint8_t *game = g_side ? g_origGame : g_portGameBuf;
    std::memcpy(game + off, &value, 4);
}

// A human's controller's type (the original's +0xB0, then +0x48; 0 without one).
int ControllerType(const uint8_t *human)
{
    const uint8_t *c = g_side ? *reinterpret_cast<uint8_t *const *>(human + 0xB0)
                              : reinterpret_cast<const uint8_t *>(reinterpret_cast<const C_Human *>(human)->m_controller);
    if (!c)
        return 0;
    return g_side ? *reinterpret_cast<const int *>(c + 0x48) : int(reinterpret_cast<const C_HumanController *>(c)->m_type);
}
void SetControllerType(uint8_t *human, int type)
{
    uint8_t *c = g_side ? *reinterpret_cast<uint8_t **>(human + 0xB0)
                        : reinterpret_cast<uint8_t *>(reinterpret_cast<C_Human *>(human)->m_controller);
    if (g_side)
        *reinterpret_cast<int *>(c + 0x48) = type;
    else
        reinterpret_cast<C_HumanController *>(c)->m_type = E_HumanController(type);
}
// ---- the logger ----
std::vector<std::string> g_log[2];
int g_calls[2];
// The words of a call's arguments on the current side, in order (`this` first for a member).
struct S_Decoded {
    std::vector<uint32_t> words;
    int pop = 0;
};
S_Decoded Decode(const char *conv, int count, const uint32_t *regs)
{
    const uint32_t ecx = regs[6], edx = regs[5];
    const uint32_t *stack = regs + 11;
    S_Decoded d;
    const std::string c = conv;
    if (c == "thiscall") {
        if (count > 0)
            d.words.push_back(ecx);
        for (int i = 1; i < count; ++i)
            d.words.push_back(stack[i - 1]);
        d.pop = 4 * std::max(0, count - 1);
    } else if (c == "fastcall") {
        if (count > 0)
            d.words.push_back(ecx);
        if (count > 1)
            d.words.push_back(edx);
        for (int i = 2; i < count; ++i)
            d.words.push_back(stack[i - 2]);
        d.pop = 4 * std::max(0, count - 2);
    } else {
        for (int i = 0; i < count; ++i)
            d.words.push_back(stack[i]);
        d.pop = (c == "stdcall" || c == "stdcall_this") ? 4 * count : 0;
    }
    return d;
}
std::string FloatText(float f)
{
    if (f == 0.0f)
        f = 0.0f;                                       // (-0: as 0)
    return F("%.9g", double(f));
}
std::string VectorText(uint32_t p, int n)
{
    if (!p)
        return "null";
    const float *v = reinterpret_cast<const float *>(p);
    std::string s = "(";
    for (int i = 0; i < n; ++i)
        s += (i ? " " : "") + FloatText(v[i]);
    return s + ")";
}
std::string ArgText(char kind, uint32_t w)
{
    switch (kind) {
    case 'i': return F("%d", int(w));
    case 'b': return F("%d", int(w & 0xFF));
    case 'f': {
        float f;
        std::memcpy(&f, &w, 4);
        return FloatText(f);
    }
    case 's': return w ? "\"" + std::string(reinterpret_cast<const char *>(w)) + "\"" : std::string("null");
    case 'p': {
        std::string s = NameOf(w);
        if (s.compare(0, 5, "anim ") == 0)                // (an animation set: its play mode, +0x08)
            s += F(" mode %d", At<int>(reinterpret_cast<void *>(w), 8));
        return s;
    }
    case 'v': case 'V': return VectorText(w, 3);
    case 'h': return F("%d", int(w & 0xFFFF));
    case 'o': return "out";                             // (a vector the callee fills: the answer's)
    case 'c': {                                         // (S_HumanCommand: category, type, the two parameters)
        const int *c = reinterpret_cast<const int *>(w);
        return F("command {%d %d %d %d}", c[1], c[2], c[3], c[4]);
    }
    case 'q': return VectorText(w, 4);
    case 'm': return VectorText(w, 16);
    default: return F("%08x", w);
    }
}

// The answers: by the call's name (the world's), else by its kind from the sequence.
using T_Answer = std::function<uint32_t(const std::vector<uint32_t> &)>;
std::map<std::string, T_Answer> g_answers;
uint8_t *FindByName(const char *name)
{
    if (!name)
        return nullptr;
    for (S_FakeFrame &f : W().frames)
        if (f.name == name)
            return f.bytes;
    return nullptr;
}
uint8_t *FindChild(I3D_frame *parent, const char *name)
{
    for (I3D_frame *c = parent->m_firstChild; c; c = c->m_nextSibling) {
        if (c->m_name && std::strcmp(c->m_name, name) == 0)
            return reinterpret_cast<uint8_t *>(c);
        if (uint8_t *d = FindChild(c, name))
            return d;
    }
    return nullptr;
}
void SetUpAnswers()
{
    auto ptr = [](const void *p) { return reinterpret_cast<uint32_t>(p); };
    // Part 5: SetFrameData attaches the data as the game's does (a copy, or over the frame's own).
    g_answers["SetFrameData"] = [](const std::vector<uint32_t> &a) {
        I3D_frame *f = reinterpret_cast<I3D_frame *>(a[0]);
        if (!f->m_data)
            f->m_data = reinterpret_cast<S_FrameData *>(Block(0x20));
        std::memcpy(f->m_data, reinterpret_cast<const void *>(a[1]), 0x20);
        return 0u;
    };
    g_answers["I3D_frame::Release"] = [](const std::vector<uint32_t> &a) {
        I3D_frame *f = reinterpret_cast<I3D_frame *>(a[0]);
        return uint32_t(--f->m_refCount);
    };
    g_answers["I3D_frame::FindChildFrame"] = [](const std::vector<uint32_t> &a) {
        return reinterpret_cast<uint32_t>(FindChild(reinterpret_cast<I3D_frame *>(a[0]), reinterpret_cast<const char *>(a[1])));
    };
    g_answers["I3D_frame::NumChildren"] = [](const std::vector<uint32_t> &a) {
        int n = 0;
        for (I3D_frame *c = reinterpret_cast<I3D_frame *>(a[0])->m_firstChild; c; c = c->m_nextSibling)
            ++n;
        return uint32_t(n);
    };
    g_answers["I3D_frame::GetChild"] = [](const std::vector<uint32_t> &a) {
        int n = int(a[1]);
        for (I3D_frame *c = reinterpret_cast<I3D_frame *>(a[0])->m_firstChild; c; c = c->m_nextSibling)
            if (n-- == 0)
                return reinterpret_cast<uint32_t>(c);
        return 0u;
    };
    g_answers["I3D_frame::GetVisualType"] = [](const std::vector<uint32_t> &a) {
        for (S_FakeFrame &f : W().frames)
            if (reinterpret_cast<uint32_t>(f.bytes) == a[0])
                return f.visualType;
        return 0u;
    };
    g_answers["I3D_driver::FindFrame"] = [](const std::vector<uint32_t> &a) {
        return NextFake() % 4 ? reinterpret_cast<uint32_t>(FindByName(reinterpret_cast<const char *>(a[2]))) : 0u;
    };
    g_answers["I3D_scene::FindFrame"] = [](const std::vector<uint32_t> &a) {
        return reinterpret_cast<uint32_t>(FindByName(reinterpret_cast<const char *>(a[1])));
    };
    // (Always a frame: neither side checks, as the engine never fails it.)
    g_answers["I3D_driver::CreateFrame"] = [](const std::vector<uint32_t> &a) {
        uint8_t *b = NewFrame(int(a[1]), F("new%d", W().created++));
        AsFrame(b)->m_name = W().frames.back().name.c_str();
        return reinterpret_cast<uint32_t>(b);
    };
    g_answers["GetSoundModule"] = [ptr](const std::vector<uint32_t> &) { return ptr(W().soundModule); };
    g_answers["GetGEffects"] = [ptr](const std::vector<uint32_t> &) { return ptr(W().geffects); };
    g_answers["GetLightMapGroups"] = [ptr](const std::vector<uint32_t> &) { return ptr(W().lightmaps); };
    g_answers["C_game::GetCameraModule"] = [ptr](const std::vector<uint32_t> &) { return ptr(W().cameraModule); };
    g_answers["C_CheckpointNetwork::Find"] = [](const std::vector<uint32_t> &a) {
        const char *name = reinterpret_cast<const char *>(a[1]);
        for (int i = 0; i < CHECKPOINT_NAME_COUNT; ++i)
            if (name && std::strcmp(name, CHECKPOINT_NAMES[i]) == 0)
                return uint32_t(i);
        return uint32_t(CHECKPOINT_NONE);
    };
    g_answers["C_CheckpointNetwork::Get"] = [](const std::vector<uint32_t> &a) {
        uint8_t *points = W().points[a[0] == reinterpret_cast<uint32_t>(W().roads)];
        return a[1] < uint32_t(CHECKPOINT_NAME_COUNT) ? reinterpret_cast<uint32_t>(points + 0x24 * a[1]) : 0u;
    };
    // A debris actor on a new frame, or on none (its frame: the original's +0x20, the port's member).
    g_answers["C_TemporaryActorsList::CreateDebris"] = [](const std::vector<uint32_t> &) {
        uint8_t *debris = Block(0x200);
        Name(debris, F("debris%d", W().created));
        if (NextFake() % 4) {
            uint8_t *frame = NewFrame(FRAME_MODEL, F("new%d", W().created++));
            AsFrame(frame)->m_name = W().frames.back().name.c_str();
            if (g_side)
                *reinterpret_cast<uint8_t **>(debris + 0x20) = frame;
            else
                reinterpret_cast<C_Actor *>(debris)->m_frame = AsFrame(frame);
        }
        return reinterpret_cast<uint32_t>(debris);
    };
    // The animation cache: an animation set for most names (I3D_OK), else not found (its pointer left alone).
    g_answers["C_ResourceCache<I3D_animation_set>::Get"] = [](const std::vector<uint32_t> &a) {
        if (NextFake() % 4 == 0)
            return uint32_t(I3DERR_NOFILE);
        uint8_t *anim = Block(0x100);
        *reinterpret_cast<void ***>(anim) = EngineVtable("I3D_animation_set");
        Name(anim, F("anim %s", reinterpret_cast<const char *>(a[1])));
        *reinterpret_cast<uint8_t **>(a[2]) = anim;
        return uint32_t(I3D_OK);
    };
    // A model's copy: the source's frames copied under it (the same names and types).
    g_answers["I3D_model::DuplicateModel"] = [](const std::vector<uint32_t> &a) {
        std::function<void(I3D_frame *, I3D_frame *)> copy = [&](I3D_frame *from, I3D_frame *to) {
            for (I3D_frame *c = from->m_firstChild; c; c = c->m_nextSibling) {
                uint8_t *b = NewFrame(c->m_type, c->m_name ? c->m_name : "");
                AsFrame(b)->m_name = W().frames.back().name.c_str();
                Name(b, F("frame copy%d ", W().created++) + W().frames.back().name);
                LinkUnder(b, reinterpret_cast<uint8_t *>(to));
                copy(c, AsFrame(b));
            }
        };
        copy(reinterpret_cast<I3D_frame *>(a[1]), reinterpret_cast<I3D_frame *>(a[0]));
        return 0u;
    };
    // A model's shadow: a frame, or none.
    g_answers["C_GEffectsModule::GetShadow"] = [](const std::vector<uint32_t> &) {
        if (NextFake() % 2)
            return 0u;
        uint8_t *b = NewFrame(FRAME_SHADOW, F("shadow%d", W().created++));
        AsFrame(b)->m_name = W().frames.back().name.c_str();
        return reinterpret_cast<uint32_t>(b);
    };
    // The shift module's entry of a mesh, the same one each time (its on / reverse bytes compared).
    g_answers["C_TShiftModule::GetEntry"] = [](const std::vector<uint32_t> &a) {
        const std::string name = "tshift entry of " + NameOf(a[1]);
        for (uint8_t *e : W().tshiftEntries)
            if (NameOf(reinterpret_cast<uint32_t>(e)) == name)
                return reinterpret_cast<uint32_t>(e);
        uint8_t *entry = Block(0x20);
        entry[8] = 1;
        W().tshiftEntries.push_back(entry);
        Name(entry, name);
        return reinterpret_cast<uint32_t>(entry);
    };
    g_answers["C_LightMapGroupsModule::FindGroup"] = [](const std::vector<uint32_t> &) {
        if (NextFake() % 2)
            return 0u;
        uint8_t *group = Block(0x40);
        Name(group, "lightmap group");
        return reinterpret_cast<uint32_t>(group);
    };
    // A leader's formation: one per leader (created on demand, else sometimes there), its shape inside it (the
    // original's +0x28, the port's m_shape).
    g_answers["C_TeamManager::GetFormation"] = [](const std::vector<uint32_t> &a) {
        const std::string name = "formation of " + NameOf(a[1]);
        for (const auto &n : g_names[g_side])
            if (n.second == name)
                return n.first;
        if (!(a[2] & 0xFF) && NextFake() % 2)
            return 0u;
        static void **vtables[2];
        if (!vtables[g_side])
            vtables[g_side] = GameVtable("C_Formation", VirtualSlots("C_Formation::", g_side != 0));
        uint8_t *formation = Block(sizeof(C_Formation) + 0x40);
        *reinterpret_cast<void ***>(formation) = vtables[g_side];
        uint8_t *shape = Block(0x100);
        if (g_side)
            *reinterpret_cast<uint8_t **>(formation + 0x28) = shape;
        else
            reinterpret_cast<C_Formation *>(formation)->m_shape = reinterpret_cast<C_FormationShape *>(shape);
        Name(formation, name);
        Name(shape, "shape of " + name);
        return reinterpret_cast<uint32_t>(formation);
    };
    // The teams' members: the world's humans (the player team's and the co-op team's alike).
    g_answers["C_TeamBase::GetMemberCount"] = [](const std::vector<uint32_t> &) { return uint32_t(W().humans.size()); };
    g_answers["C_TeamBase::GetMember"] = [](const std::vector<uint32_t> &a) {
        return a[1] < W().humans.size() ? reinterpret_cast<uint32_t>(W().actors[W().humans[a[1]]].bytes) : 0u;
    };
    g_answers["C_TeamBase::GetAliveCount"] = [](const std::vector<uint32_t> &) {
        uint32_t alive = 0;
        for (int h : W().humans) {
            const uint8_t *a = W().actors[h].bytes;
            alive += (g_side ? *reinterpret_cast<const int *>(a + 0xEC) : reinterpret_cast<const C_Human *>(a)->m_stats[STAT_HEALTH]) > 0;
        }
        return alive;
    };
    // An item found, now and then; the team's active player, a human or none.
    g_answers["C_Human::FindItem"] = [](const std::vector<uint32_t> &) {
        if (NextFake() % 3)
            return 0u;
        uint8_t *item = Block(0x40);
        Name(item, "item");
        return reinterpret_cast<uint32_t>(item);
    };
    g_answers["C_Team::GetActivePlayer"] = [](const std::vector<uint32_t> &) {
        const auto &humans = W().humans;
        return humans.empty() || NextFake() % 4 == 0 ? 0u
                                                     : reinterpret_cast<uint32_t>(W().actors[humans[NextFake() % humans.size()]].bytes);
    };
    // The actors module's list (by index; by id the same: the ids are the indices here).
    g_answers["C_ActorsModule::GetActor"] = [](const std::vector<uint32_t> &a) {
        return a[1] < W().list.size() ? reinterpret_cast<uint32_t>(W().list[a[1]]) : 0u;
    };
    g_answers["C_ActorsModule::GetActorById"] = g_answers["C_ActorsModule::GetActor"];
    g_answers["BeginScriptNetMessage"] = [ptr](const std::vector<uint32_t> &) {
        return NextFake() % 4 ? ptr(W().bitstream) : 0u;
    };
    // Part 4: the checkpoints near a point or reachable from one (0..4 of the named ones; the out vector in each side's
    // layout: the original's MSVC 6 vector first +4, last +8, end +0xC, its buffer freed by its operator delete).
    auto points = [](const std::vector<uint32_t> &a) -> uint32_t {
        std::vector<uint16_t> found(NextFake() % 5);
        for (uint16_t &p : found)
            p = uint16_t(NextFake() % CHECKPOINT_NAME_COUNT);
        uint8_t *out = reinterpret_cast<uint8_t *>(a[3]);
        if (g_side) {
            uint8_t *buf = static_cast<uint8_t *>(O_New(2 * found.size() + 2));   // (its operator new's)
            if (!found.empty())
                std::memcpy(buf, found.data(), 2 * found.size());
            *reinterpret_cast<uint8_t **>(out + 4) = buf;
            *reinterpret_cast<uint8_t **>(out + 8) = buf + 2 * found.size();
            *reinterpret_cast<uint8_t **>(out + 0xC) = buf + 2 * found.size();
        } else {
            reinterpret_cast<std::vector<uint16_t> *>(out)->assign(found.begin(), found.end());
        }
        return NextFake() % 4 ? 1u : 0u;
    };
    g_answers["C_CheckpointNetwork::GetNeighbours"] = points;
    g_answers["C_CheckpointNetwork::GetReachable"] = points;
    g_answers["C_CheckpointNetwork::FindNearest"] = [](const std::vector<uint32_t> &) {
        return NextFake() % 4 ? NextFake() % CHECKPOINT_NAME_COUNT : uint32_t(CHECKPOINT_NONE);
    };
    // A human's checkpoint by name (0xFFFF missing, as the original's AX), its controller's type (0 without one).
    g_answers["C_Human::FindCheckpoint"] = [](const std::vector<uint32_t> &a) {
        const char *name = reinterpret_cast<const char *>(a[1]);
        for (int i = 0; i < CHECKPOINT_NAME_COUNT; ++i)
            if (name && std::strcmp(name, CHECKPOINT_NAMES[i]) == 0)
                return uint32_t(i);
        return uint32_t(CHECKPOINT_NONE);
    };
    g_answers["C_Human::GetControllerType"] = [](const std::vector<uint32_t> &a) {
        return uint32_t(ControllerType(reinterpret_cast<uint8_t *>(a[0])));
    };
}
uint32_t DefaultAnswer(char ret)
{
    switch (ret) {
    case 'i': return uint32_t(FakeInt());
    case 'b': return NextFake() & 1;
    case 'f': {
        const float f = FakeFloat();
        uint32_t w;
        std::memcpy(&w, &f, 4);
        return w;
    }
    default: return 0;
    }
}

bool g_failedCall;
std::string g_failText;
std::set<std::string> g_extraQuiet;                     // (mod_diff: calls one side inlines, answered unlogged)
std::set<std::string> g_outStrings;                     // (mod_diff: calls whose string arguments the answer writes)
uint64_t Call(uint32_t index, uint32_t *regs)
{
    const char *name, *kinds, *conv;
    char ret;
    std::string key;
    if (index >= TRAP_BASE) {
        std::printf("cmd_diff: a call through an unknown slot: %s [%s, %s]\n", g_trapNames[index - TRAP_BASE].c_str(),
                    g_current.c_str(), SIDE_NAMES[g_side]);
        std::exit(3);
    }
    std::string engineKinds;
    if (index >= VIRTUAL_BASE) {
        const S_Virtual &v = g_virtuals[index - VIRTUAL_BASE];
        name = v.name;
        engineKinds = std::string("p") + v.kinds;
        kinds = engineKinds.c_str();
        conv = "thiscall";
        ret = v.ret;
        key = name;
    } else if (index >= SLOT_BASE) {
        const S_SlotDef &s = ENGINE_SLOTS[index - SLOT_BASE];
        engineKinds = std::string("p") + s.kinds;
        kinds = engineKinds.c_str();
        conv = "stdcall";
        ret = s.ret;
        key = std::string(s.cls) + "::" + s.name;
        name = nullptr;
    } else {
        const S_CallDef &c = CALLS[index];
        name = c.name;
        kinds = c.kinds;
        conv = g_side ? c.oconv : c.pconv;
        ret = c.ret;
        key = name;
        const char *paren = std::strchr(name, '(');
        if (paren)
            key = std::string(name, paren);
    }
    const int count = int(std::strlen(kinds));
    const int fixed = std::strchr(kinds, '.') ? int(std::strchr(kinds, '.') - kinds) : count;
    S_Decoded d = Decode(conv, fixed, regs);
    // (C_CheckpointNetwork::Get reads AX alone: the original pushes a word whose upper half is a leftover.)
    if (key == "C_CheckpointNetwork::Get" && d.words.size() > 1)
        d.words[1] &= 0xFFFF;
    std::string line = key + "(";
    const bool outString = g_outStrings.count(key) != 0;
    for (int i = 0; i < fixed; ++i)
        line += (i ? ", " : "") + (outString && kinds[i] == 's' ? std::string("out") : ArgText(kinds[i], d.words[i]));
    line += ")";
    // Structures passed by address: their contents (the original's layouts, the headers' comments).
    {
        auto words = [&](uint32_t p, int from, int to) {
            std::string s = " {";
            for (int o = from; o < to; o += 4)
                s += (o > from ? " " : "") + FloatText(*reinterpret_cast<const float *>(p + o));
            return s + "}";
        };
        auto bytes = [](uint32_t p, int o) { return F(" %d", int(*reinterpret_cast<const uint8_t *>(p + o))); };
        auto ptr = [](uint32_t p, int o) { return " " + NameOf(*reinterpret_cast<const uint32_t *>(p + o)); };
        if (key == "S_Explosion::Execute" && d.words[0])   // pos, posW, range, damage; the flag, the source
            line += words(d.words[0], 0, 0x18) + bytes(d.words[0], 0x18) + ptr(d.words[0], 0x1C);
        else if (key == "C_SoundModule::Play" && d.words[1]) {
            const uint32_t r = d.words[1];              // pos, posW, flags; attach, source; the rest
            line += words(r, 0, 0x10) + F(" %08x", *reinterpret_cast<const uint32_t *>(r + 0x10)) + ptr(r, 0x14) +
                    ptr(r, 0x18) + F(" %d %d", *reinterpret_cast<const int *>(r + 0x1C), *reinterpret_cast<const int *>(r + 0x20)) +
                    ptr(r, 0x24) + words(r, 0x28, 0x30) + F(" %d", *reinterpret_cast<const int *>(r + 0x30)) + words(r, 0x34, 0x40);
        } else if (key == "SetFrameData" && d.words[1]) {   // part 5: the script's frame data
            const S_FrameData *fd = reinterpret_cast<const S_FrameData *>(d.words[1]);
            const uint32_t none = g_side ? *OriginalAt<const uint32_t *>(0x00832cdc)
                                         : reinterpret_cast<uint32_t>(g_noPhysicsObject);
            const uint32_t physics = reinterpret_cast<uint32_t>(fd->physicsObject);
            // (+0x1C not: C_Script's constructor (0x0041dcb0) leaves it as its stack had it; the port's is 0.)
            line += F(" {actor %s script %s physics %s %08x %08x %s %s}", NameOf(reinterpret_cast<uint32_t>(fd->actor)).c_str(),
                      fd->script == g_frameScript[g_side] ? "this" : fd->script ? "other" : "null",
                      physics == none ? "none" : NameOf(physics).c_str(), fd->unknown08, fd->unknown0C,
                      NameOf(reinterpret_cast<uint32_t>(fd->versionMaster)).c_str(),
                      NameOf(reinterpret_cast<uint32_t>(fd->versionKeeper)).c_str());
        } else if (key == "C_Debris::Setup" && d.words[1]) // velocity, weight, friction, callback, the moving flag
            // (Setup reads +0x20 on; the original leaves the first two vectors, the velocity's w, the context and +0x41
            // as the stack had them.)
            line += words(d.words[1], 0x20, 0x2C) + words(d.words[1], 0x30, 0x38) + ptr(d.words[1], 0x38) +
                    bytes(d.words[1], 0x40);
    }
    auto it = g_answers.find(key);
    uint32_t answer = it != g_answers.end() ? it->second(d.words) : DefaultAnswer(ret);
    if (ret == 'b')
        answer &= 0xFF;
    g_cmdFloat = 0;
    if (ret == 'f') {
        g_cmdFloat = 1;
        std::memcpy(&g_cmdFloatValue, &answer, 4);
        line += " -> " + ArgText('f', answer);
    } else if (ret != 'v')
        line += " -> " + ArgText(ret == 'p' ? 'p' : 'i', answer);
    if (outString)                                      // (the strings the answer wrote)
        for (int i = 0; i < fixed; ++i)
            if (kinds[i] == 's')
                line += " -> " + ArgText('s', d.words[i]);
    g_cmdPop = d.pop;
    // (The game's plain getters answer unlogged: the original fetches some twice where the port keeps the first.)
    static const std::set<std::string> quiet = {"C_game::GetCameraModule", "GetGEffects", "GetSoundModule",
                                                "GetLightMapGroups", "C_TShiftModule::GetEntry"};
    if (!quiet.count(key) && !g_extraQuiet.count(key))
        g_log[g_side].push_back(line);
    ++g_calls[g_side];
    return answer;
}

}  // namespace H

extern "C" uint64_t CmdCall(uint32_t *regs) { return H::Call(regs[9], regs); }

// ---- the port's side of the engine (LS3DF.dll's own math, on both sides) ----
namespace {
FARPROC Ls3df(const char *name)
{
    const FARPROC p = GetProcAddress(GetModuleHandleA("LS3DF.dll"), name);
    if (!p) {
        std::printf("cmd_diff: %s not in LS3DF.dll\n", name);
        std::exit(2);
    }
    return p;
}
}  // namespace
void I3D_frame::UpdateWMatrixProc()
{
    using T = void(__attribute__((thiscall)) *)(I3D_frame *);
    static const T update = reinterpret_cast<T>(reinterpret_cast<void *>(Ls3df("?UpdateWMatrixProc@I3D_frame@@AAEXXZ")));
    update(this);
}
void __stdcall S_quat::Make(const S_matrix &m)
{
    using T = void(__stdcall *)(S_quat *, const S_matrix &);
    static const T make = reinterpret_cast<T>(reinterpret_cast<void *>(Ls3df("?Make@S_quat@@QAGXABUS_matrix@@@Z")));
    make(this, m);
}
void __stdcall S_quat::Normalize()
{
    using T = void(__stdcall *)(S_quat *);
    static const T normalize = reinterpret_cast<T>(reinterpret_cast<void *>(Ls3df("?Normalize@S_quat@@QAGXXZ")));
    normalize(this);
}
void __stdcall S_matrix::SetRot3(const S_quat &q)
{
    using T = void(__stdcall *)(S_matrix *, const S_quat &);
    static const T set = reinterpret_cast<T>(reinterpret_cast<void *>(Ls3df("?SetRot3@S_matrix@@QAGXABUS_quat@@@Z")));
    set(this, q);
}
void __stdcall S_matrix::Identity()
{
    using T = void(__stdcall *)(S_matrix *);
    static const T identity = reinterpret_cast<T>(reinterpret_cast<void *>(Ls3df("?Identity@S_matrix@@QAGXXZ")));
    identity(this);
}
S_vector __stdcall S_vector::operator*(const S_matrix &m) const
{
    using T = void *(__stdcall *)(const S_vector *, float *, const S_matrix &);
    static const T mul = reinterpret_cast<T>(reinterpret_cast<void *>(Ls3df("??DS_vector@@QBG?AU0@ABUS_matrix@@@Z")));
    alignas(16) float r[4] = {};
    mul(this, r, m);
    return S_vector(r[0], r[1], r[2]);
}
// (Before any world, a static object's constructor may ask for the driver: camera_module.cpp's makes a camera. A
// driver of its own then, whose CreateFrame gives a frame that is never used.)
namespace H {
uint32_t __stdcall StaticFrameRelease(void *) { return 1; }
void *__stdcall StaticCreateFrame(void *, int)
{
    static void *vtable[0x80];
    alignas(16) static uint8_t frame[0x600];
    for (void *&slot : vtable)
        slot = reinterpret_cast<void *>(&StaticFrameRelease);
    *reinterpret_cast<void ***>(frame) = vtable;
    return frame;
}
uint8_t *StaticDriver()
{
    static void *vtable[0x80];
    static uint8_t driver[0x40];
    vtable[0x54 / 4] = reinterpret_cast<void *>(&StaticCreateFrame);
    *reinterpret_cast<void ***>(driver) = vtable;
    return driver;
}
}  // namespace H
extern "C" I3D_driver *I3DGetDriver()
{
    return reinterpret_cast<I3D_driver *>(H::W().driver ? H::W().driver : H::StaticDriver());
}

namespace H {

void *__stdcall O_I3DGetDriver() { return W().driver; }
int __cdecl O_Rand() { return std::rand(); }

// ---- the script on each side: one program's variables ----
constexpr int FRAMES = 8, NUMBERS = 12, STRINGS = 6, FILLER = 40;
uint8_t *g_block;                                       // the original's
C_Script *g_script;                                     // the port's (the case's kind of the three)
C_Script *g_scripts[3];                                 // the port's plain, human and melee tracker scripts
uint32_t g_baseVtable;                                  // (the original block's C_Script vtable)
// Part 4: the actor scripts' ExecuteCommand (the original's C_HumanScript / C_MeleeTrackerScript, vtables 0x0080f714 /
// 0x0080f70c, the actor at +0x10C).
constexpr uint32_t O_HUMAN_EXECUTE = 0x00421c00, O_TRACKER_EXECUTE = 0x00421970;
constexpr uint32_t O_HUMAN_VTABLE = 0x0080f714, O_TRACKER_VTABLE = 0x0080f70c;
using T_Exec = bool(TC *)(void *, const void *);
using T_Call = int(TC *)(void *, int, const int *, const int *);
constexpr uint32_t O_CALL_FUNCTION = 0x0041e3f0;
using T_StrDtor = void(TC *)(void *);
using T_Fstr = void *(__cdecl *)(void *, const char *, ...);

void CompileProgram()
{
    std::string text;
    for (int i = 0; i < FRAMES; ++i)
        text += F("Frame f%d;\r\n", i);
    for (int i = 0; i < NUMBERS; ++i)
        text += F("Integer n%d;\r\n", i);
    for (int i = 0; i < STRINGS; ++i)
        text += F("String s%d;\r\n", i);
    for (int i = 0; i < FILLER; ++i)
        text += "Delay(0);\r\n";
    const std::string path = "Scripts\\AFRICA1\\cmd_x.scr";
    g_overrides[Lower(path)] = text;
    SetRole(GAME_ROLE_LOCAL);
    g_block = static_cast<uint8_t *>(std::calloc(1, 0x200));
    OriginalAt<T_Ctor>(0x0041dcb0)(g_block, nullptr, 1);
    const bool ro = OriginalAt<T_Compile>(0x0042c850)(OriginalAt<void *>(0x008993f0), path.c_str(), g_block);
    g_baseVtable = At<uint32_t>(g_block, 0);
    g_scripts[0] = new C_Script(nullptr, true);
    g_scripts[1] = new C_HumanScript(nullptr, nullptr);
    g_scripts[2] = new C_MeleeTrackerScript(nullptr, nullptr);
    for (C_Script *t : g_scripts) {
        const bool rp = g_scriptCompiler.Compile(path.c_str(), t);
        if (!ro || !rp || OrigVec(g_block, 0x8C, 4).size() != FRAMES || t->m_frames.size() != FRAMES ||
            OrigVec(g_block, 0x9C, 8).size() < NUMBERS || t->m_numbers.size() < NUMBERS ||
            OrigVec(g_block, 0xAC, 4).size() != STRINGS || t->m_strings.size() != STRINGS) {
            std::printf("cmd_diff: the variables' program did not compile alike (%d %d)\n", int(ro), int(rp));
            std::exit(2);
        }
    }
    g_script = g_scripts[0];
}
uint32_t *OrigFrames() { return reinterpret_cast<uint32_t *>(At<uint32_t>(g_block, 0x8C + 4)); }
int32_t *OrigNumbers() { return reinterpret_cast<int32_t *>(At<uint32_t>(g_block, 0x9C + 4)); }
uint32_t *OrigStrings() { return reinterpret_cast<uint32_t *>(At<uint32_t>(g_block, 0xAC + 4)); }

const char *const STRING_VALUES[] = {"", "x", "door", "hlava", "model1", "snd_a", "cam1", "dummy1", "child1", "nothing",
                                     "anim\\a.i3d", "a", "model2", "joint1", "light1", "visual1", "sector1", "cam2", "rw"};
constexpr int STRING_VALUE_COUNT = int(sizeof STRING_VALUES / sizeof *STRING_VALUES);

// One case's inputs.
struct S_Case {
    int opcode;
    int function = -1;                                  // (a function's case: CallFunction, not a command)
    int types[4] = {};
    int args[4];
    int frames[FRAMES];                                 // world frame index, -1 null
    int numbers[NUMBERS];
    int strings[STRINGS];
    int myFrame;
    uint32_t signals;                                   // (pending)
    bool noCamera = false;                              // (the scene without an active camera: _CameraInRange only)
    bool hudHidden = false, panelHidden = false;        // (the script's flags: a script camera hid them)
    int ip;
    int kind = 0;                                       // part 4: 0 the plain script, 1 a human's, 2 a melee tracker's
    uint32_t actorPick = 0;                             // (which human: the world's, one in 8 none)
    bool actorFlag = false;                             // (the actor scripts' delayed suspend under way)
    int delay = 0;                                      // (the script's delay left: some commands clear it)
    // Part 5 (kind 3): a script made on a world frame (myFrame), an event, the script destroyed.
    int frameEvent = 0;                                 // 0 nothing, 1 Stop, 2 SendSignal, 3 NotifyFrameHit
    int dataMode = 0;                                   // the frame's data: 0 the world's, 1 none, 2 one without an actor
    bool global = false;
    bool running = false;
    int handlerSignal = -1, handlerLabel = 0, signal = 0, onHit = -1;
};
int RandomNumber()
{
    // (Past the clamps too: the weather's speed (200 m/s) and size (20 m, 500 m) in mm.)
    static const int values[] = {0, 1, 2, 3, -1, 5, 10, 100, 255, 256, 1000, 50, 7, 666, -5, 666, 300000, -2000, 30000, 600000};
    // (0 and 1 often: the booleans' both ways.)
    return P(0.4) ? I(0, 1) : P(0.6) ? values[I(0, 19)] : I(-3, 40);
}
std::vector<int> g_opcodes;
std::vector<int> g_humanOpcodes, g_trackerOpcodes;      // (part 4: the actor scripts' own ranges)
bool g_actorsOnly;
std::vector<const S_ScriptFunctionDef *> g_functions;
std::map<int, int> g_functionCases;
bool g_functionsOnly;
const char *FunctionName(int id)
{
    for (const S_ScriptFunctionDef *f : g_functions)
        if (f->id == id)
            return f->name;
    return "?";
}
int g_originalCrashes;
S_Case RandomCase()
{
    S_Case c;
    c.opcode = g_opcodes[I(0, int(g_opcodes.size()) - 1)];
    // Part 5: one case in 12 makes a script on a frame (the humans' and the models' often).
    if (!g_actorsOnly && !g_functionsOnly && P(1.0 / 12)) {
        c.kind = 3;
        c.frameEvent = I(0, 3);
        c.myFrame = P(0.08) ? -1 : P(0.5) ? I(0, WORLD_FRAMES - 1) : P(0.5) ? 2 : 10;
        c.dataMode = P(0.6) ? 0 : I(1, 2);
        c.global = P(0.1);
        c.running = P(0.5);
        c.ip = I(0, 30);
        c.onHit = P(0.5) ? -1 : I(0, 30);
        c.signal = P(0.8) ? I(0, 3) : I(-2, 40);
        c.handlerSignal = P(0.25) ? -1 : P(0.8) ? c.signal : I(0, 3);
        c.handlerLabel = I(0, 30);
        c.actorPick = U32();
        return c;
    }
    // Part 4: half the cases (HD2_CMD_ACTORS: all) run an actor script's command, mostly one of its own: a human's
    // (HUMAN_*, the frames' AI modes) or a melee tracker's (MELEE_TRACKER_*); the rest fall to the base or are skipped.
    const bool actor = !g_functionsOnly && (!g_humanOpcodes.empty() || !g_trackerOpcodes.empty()) && (g_actorsOnly || P(0.5));
    if (actor) {
        c.kind = g_trackerOpcodes.empty() || (!g_humanOpcodes.empty() && P(0.75)) ? 1 : 2;
        const std::vector<int> &own = c.kind == 1 ? g_humanOpcodes : g_trackerOpcodes;
        if (own.empty() || P(0.08))
            c.opcode = g_opcodes[I(0, int(g_opcodes.size()) - 1)];
        else
            c.opcode = own[I(0, int(own.size()) - 1)];
        c.actorPick = U32();
        c.actorFlag = P(0.3);
    }
    // A quarter of the cases call a function (the expressions' built-ins) instead.
    if (!actor && !g_functions.empty() && (g_functionsOnly || P(0.25))) {
        const S_ScriptFunctionDef &def = *g_functions[I(0, int(g_functions.size()) - 1)];
        c.function = def.id;
        for (int i = 0; i < 4; ++i)
            c.types[i] = def.argTypes[i];
    }
    for (int i = 0; i < 4; ++i) {
        const int t = c.function >= 0 ? c.types[i] : GetScriptArgType(c.opcode, i);
        c.args[i] = t == SCRIPT_TYPE_FRAME ? I(0, FRAMES - 1) : t == SCRIPT_TYPE_STRING ? I(0, STRINGS - 1) : I(0, NUMBERS - 1);
    }
    for (int &f : c.frames)
        f = P(0.15) ? -1 : P(0.2) ? (P(0.5) ? 2 : 10) : I(0, WORLD_FRAMES - 1);   // (the models often)
    for (int &n : c.numbers)
        n = RandomNumber();
    for (int &s : c.strings)
        s = I(0, STRING_VALUE_COUNT - 1);
    c.myFrame = P(0.2) ? -1 : I(0, WORLD_FRAMES - 1);
    c.ip = I(1, FILLER - 2);
    c.delay = P(0.5) ? 0 : I(1, 5000);
    c.signals = P(0.5) ? 0 : U32() & U32();
    c.noCamera = c.function == SFUNC_CAMERA_IN_RANGE && P(0.3);
    c.hudHidden = P(0.3);
    c.panelHidden = P(0.3);
    // Rare inputs made common: an animation actor's hold (666), and a dummy as the box (whose turned corners matter).
    if (c.function < 0 && c.opcode == OP_SetActorState && P(0.5))
        c.numbers[c.args[1]] = 666;
    if ((c.function == SFUNC_PLAYER_IN_BOX || (c.function < 0 && c.opcode == OP_HUMAN_KillAllInBox)) && P(0.6)) {
        static const int dummies[] = {5, 12, 15, 19};
        c.frames[c.args[0]] = dummies[I(0, 3)];
    }
    // Inputs the original crashes on and the port answers (its deliberate checks): not run, counted.
    // (The functions that use the script's own frame unchecked, on both sides.)
    if (c.myFrame < 0 && (c.function == SFUNC_ITEM_IN_RANGE || c.function == SFUNC_MP_ALL_TEAM_MEMBERS_ARE_IN_RANGE ||
                          c.function == SFUNC_MP_IS_TEAM_MEMBER_IN_RANGE || c.function == SFUNC_CAMERA_IN_RANGE ||
                          c.function == SFUNC_FRAME_IN_RANGE || c.function == SFUNC_GET_DIST_TO_ACTIVE_PLAYER)) {
        ++g_originalCrashes;
        c.myFrame = 0;
    }
    // (And the box of _PlayerInBox.)
    if (c.function == SFUNC_PLAYER_IN_BOX && c.frames[c.args[0]] < 0) {
        ++g_originalCrashes;
        c.frames[c.args[0]] = 5;
    }
    if (c.function < 0 && c.opcode == OP_FRM_CreateParticle && c.frames[c.args[1]] < 0) {
        ++g_originalCrashes;                            // (the frame's name sent unchecked)
        c.frames[c.args[1]] = 0;
    }
    return c;
}

// ---- the compared state ----
std::vector<std::string> FrameStates()
{
    std::vector<std::string> out;
    for (const S_FakeFrame &f : W().frames) {
        const I3D_frame *x = AsFrame(f.bytes);
        out.push_back(F("frame %s ref %d flags %08x pos ", f.name.c_str(), int(x->m_refCount), x->m_frameFlags) +
                      VectorText(reinterpret_cast<uint32_t>(&x->m_localMatrix.m[3][0]), 3) + " rot " +
                      VectorText(reinterpret_cast<uint32_t>(&x->m_rot), 4) + " scale " +
                      VectorText(reinterpret_cast<uint32_t>(&x->m_scale), 3) + " parent " +
                      NameOf(reinterpret_cast<uint32_t>(x->m_parent)));
    }
    return out;
}
std::vector<std::string> GameState()
{
    std::vector<std::string> out;
    if (g_side) {
        const uint8_t *g = g_origGame;
        out.push_back(F("game %d %d %d %d %d", int(g[0x34]), int(g[0x35]), int(g[0x36]), int(g[0x40]), int(g[0x6E])));
    } else {
        const C_game *g = reinterpret_cast<const C_game *>(g_portGameBuf);
        out.push_back(F("game %d %d %d %d %d", int(g->m_uniformDisguise), int(g->m_noTeamDamage), int(g->m_tacticalMode),
                        int(g->m_signalPistolFired), int(g->m_playerInputEnabled)));
    }
    if (g_side)
        out.push_back(F("cutscene %d fake %d", int(*OriginalAt<uint8_t *>(O_SCRIPT_MODULE + 0x824)),
                        int(*OriginalAt<uint8_t *>(O_SCRIPT_MODULE + 0x825))));
    else
        out.push_back(F("cutscene %d fake %d", int(g_scriptModule.m_cutsceneRunning), int(g_scriptModule.m_fakeCutscene)));
    for (const uint8_t *e : W().tshiftEntries)
        out.push_back(F("tshift entry on %d reverse %d", int(e[8]), int(e[9])));
    // The actors: type, position, direction and the fields the commands write.
    for (const W_Actor &x : W().actors) {
        const uint8_t *a = x.bytes;
        std::string s = x.name + F(" type %d", g_side ? *reinterpret_cast<const int *>(a + 0x18)
                                                       : int(reinterpret_cast<const C_Actor *>(a)->m_type));
        const float *pos = g_side ? reinterpret_cast<const float *>(a + 0x30)
                                  : &reinterpret_cast<const C_Actor *>(a)->m_position.x;
        const float *dir = g_side ? reinterpret_cast<const float *>(a + 0x40)
                                  : &reinterpret_cast<const C_Actor *>(a)->m_direction.x;
        // (x, y, z: the original's SetPosition / SetDirection copy a fourth word from past the vector they are given, a
        // stack leftover; the port writes 1 and 0.)
        s += " pos " + VectorText(reinterpret_cast<uint32_t>(pos), 3) + " dir " + VectorText(reinterpret_cast<uint32_t>(dir), 3);
        if (x.type == ACTOR_HUMAN) {
            const int *stats = g_side ? reinterpret_cast<const int *>(a + 0xEC) : reinterpret_cast<const C_Human *>(a)->m_stats;
            s += F(" health %d sight %d hear %d", stats[STAT_HEALTH], stats[STAT_SIGHT_RANGE], stats[STAT_HEARING_RANGE]);
            s += F(" circumvention %d", int(g_side ? x.controller[0x11C]
                                                   : reinterpret_cast<const C_AIController *>(x.controller)->m_ignoreCircumvention));
        }
        if (x.type == ACTOR_ANIMATION)
            s += F(" held %d", int(g_side ? a[0xB8] : reinterpret_cast<const C_AnimationActor *>(a)->m_animationHeld));
        if (x.type == ACTOR_TRAIN)
            s += F(" running %d", int(g_side ? a[0x19C] : reinterpret_cast<const C_Train *>(a)->m_running));
        out.push_back(s);
    }
    if (const uint8_t *j = W().junkers88) {
        const float *f = g_side ? reinterpret_cast<const float *>(j + 0x284)
                                : &reinterpret_cast<const C_Junkers88 *>(j)->m_firepower;
        const float *h = g_side ? reinterpret_cast<const float *>(j + 0x288)
                                : &reinterpret_cast<const C_Junkers88 *>(j)->m_interceptorHealth;
        out.push_back("junkers88 " + FloatText(*f) + " " + FloatText(*h));
    }
    std::string hints = "hints";
    if (g_side) {
        for (uint32_t q : OrigVec(W().interface_, 0x6B3A0, 4))
            hints += F(" %d", At<int>(reinterpret_cast<void *>(q), 0));
    } else {
        for (int h : reinterpret_cast<C_Interface *>(W().interface_)->m_info.m_hints)
            hints += F(" %d", h);
    }
    out.push_back(hints);
    return out;
}
std::vector<std::string> OrigScriptState()
{
    const uint8_t *b = g_block;
    std::vector<std::string> out;
    std::string flags = "flags";
    for (uint32_t off = 4; off <= 0xA; ++off)
        flags += F(" %d", int(b[off]));
    out.push_back(flags);
    out.push_back(F("signals %08x", At<uint32_t>(b, 0x10)));
    out.push_back(F("delay %d wait %d %s finished %d ip %d running %d actor %d", At<int>(b, 0x18), At<int>(b, 0x24),
                    NameOf(At<uint32_t>(b, 0x28)).c_str(), int(b[0xD4]), At<int>(b, 0xD8), int(b[0x104]), int(b[0x108])));
    for (int i = 0; i < FRAMES; ++i)
        out.push_back("frame var " + NameOf(OrigFrames()[i]));
    for (int i = 0; i < NUMBERS; ++i)
        out.push_back(F("number %d", OrigNumbers()[2 * i]));
    for (int i = 0; i < STRINGS; ++i)
        out.push_back("string " + OrigStr(OrigStrings()[i]));
    return out;
}
std::vector<std::string> PortScriptState()
{
    const C_Script &t = *g_script;
    std::vector<std::string> out;
    out.push_back(F("flags %d %d %d %d %d %d %d", int(t.m_signalsDisabled), int(t.m_alarmsDisabled),
                    int(t.m_wheneversDisabled), int(t.m_hudHiddenByScript), int(t.m_hudPanelHiddenByScript),
                    int(t.m_inCutsceneHandler), int(t.m_unknown00A)));
    out.push_back(F("signals %08x", t.m_pendingSignals));
    out.push_back(F("delay %d wait %d %s finished %d ip %d running %d actor %d", t.m_delay, t.m_waitAnimLabel,
                    NameOf(reinterpret_cast<uint32_t>(t.m_waitAnimModel)).c_str(), int(t.m_finished), t.m_ip,
                    int(t.m_running), int(t.m_actorFlag)));
    for (int i = 0; i < FRAMES; ++i)
        out.push_back("frame var " + NameOf(reinterpret_cast<uint32_t>(t.m_frames[i])));
    for (int i = 0; i < NUMBERS; ++i)
        out.push_back(F("number %d", t.m_numbers[i].value));
    for (int i = 0; i < STRINGS; ++i)
        out.push_back("string " + PortStr(t.m_strings[i]));
    return out;
}

// The actor of an actor script's case: the world's tracker, or one of its humans (one in 8 cases none).
uint8_t *ScriptActor(const S_Case &c)
{
    S_World &w = W();
    if (c.kind == 2)
        return w.tracker;
    if (w.humans.empty() || c.actorPick % 8 == 0)
        return nullptr;
    return w.actors[w.humans[(c.actorPick >> 3) % w.humans.size()]].bytes;
}
// Inputs the original misbehaves on, changed (counted): a melee tracker command with a controller other than the full
// AI's (modes 4 and 5; HafHaf: the scripted one's, 5) calls the full AI's functions on it (the port skips it); a
// human's Drive / DriveNearPlayer stopping the car drives through a player's or a remote controller (the port only
// through an AI's).
int g_originalWeaknesses;
void AvoidOriginalWeaknesses(const S_Case &c)
{
    S_World &w = W();
    if (c.kind == 2 && c.opcode >= OP_MELEE_TRACKER_SetOptimized && c.opcode <= OP_MELEE_TRACKER_SETAIMODE_Defensive) {
        int &mode = g_side ? *reinterpret_cast<int *>(w.trackerAI + 0x14)
                           : reinterpret_cast<C_MeleeTrackerController *>(w.trackerAI)->m_mode;
        if ((mode != 4 && mode != 5) || (c.opcode == OP_MELEE_TRACKER_HafHaf && mode != 5)) {
            mode = c.opcode == OP_MELEE_TRACKER_HafHaf || (c.actorPick & 1) ? 5 : 4;
            g_originalWeaknesses += g_side;
        }
    }
    if (c.kind == 1 && (c.opcode == OP_HUMAN_Drive || c.opcode == OP_HUMAN_DriveNearPlayer))
        if (uint8_t *h = ScriptActor(c)) {
            const int type = ControllerType(h);
            if (type == HUMAN_CONTROLLER_PLAYER || type == HUMAN_CONTROLLER_REMOTE) {
                SetControllerType(h, HUMAN_CONTROLLER_AI);
                g_originalWeaknesses += g_side;
            }
        }
}
// Rare inputs made common (part 4), after the world is built (alike on both sides): HUMAN_MoveToFrame / MoveFromFrame's
// target (the root frame snd_b) at the human's own root frame's place, or a unit or a tiny step (2^-15 m: exact in
// floats there) along x or z from it (the direction's unit and near-zero rules); HUMAN_Bike's bike (the actor of the
// frame its first string names turned into a bike).
void BiasWorld(S_Case &c)
{
    S_World &w = W();
    const uint32_t r = c.actorPick >> 16;
    if (c.kind == 1 && (c.opcode == OP_HUMAN_MoveToFrame || c.opcode == OP_HUMAN_MoveFromFrame) && r % 3 == 0) {
        uint8_t *h = ScriptActor(c);
        I3D_frame *hf = !h ? nullptr : g_side ? *reinterpret_cast<I3D_frame **>(h + 0x20) : reinterpret_cast<C_Actor *>(h)->m_frame;
        if (hf && !hf->m_parent && AsFrame(w.frames[14].bytes) != hf) {
            static const float steps[][3] = {{0, 0, 0}, {1, 0, 0}, {1.0f / 32768, 0, 0}, {0, 0, 1.0f / 32768}, {0, 0, -1.0f / 32768},
                                             {-1.0f / 32768, 0, 0}};
            const float *step = steps[(r / 3) % 6];
            S_matrix &m = AsFrame(w.frames[14].bytes)->m_localMatrix;
            for (int i = 0; i < 3; ++i)
                m.m[3][i] = hf->m_localMatrix.m[3][i] + step[i];
            AsFrame(w.frames[14].bytes)->m_frameFlags &= ~0x20u;
            c.frames[c.args[0]] = 14;
        }
    }
    // (HUMAN_Drive / DriveNearPlayer: the human in a car's driver seat, mostly; BoardVehicle: the frame its first string
    // names holds a car, a static weapon or a Junkers 88, mostly.)
    if (c.kind == 1 && (c.opcode == OP_HUMAN_Drive || c.opcode == OP_HUMAN_DriveNearPlayer) && r % 4 != 0)
        if (uint8_t *h = ScriptActor(c))
            for (const W_Actor &x : w.actors)
                if (x.type == ACTOR_CAR) {
                    const int seat = r % 4 == 3 ? 1 : 0;
                    if (g_side) {
                        *reinterpret_cast<uint8_t **>(h + 0x160) = x.bytes;
                        *reinterpret_cast<int *>(h + 0x164) = seat;
                    } else {
                        reinterpret_cast<C_Human *>(h)->m_vehicle = reinterpret_cast<C_Actor *>(x.bytes);
                        reinterpret_cast<C_Human *>(h)->m_seat = seat;
                    }
                    break;
                }
    if (c.kind == 1 && c.opcode == OP_HUMAN_BoardVehicle && r % 4 != 0) {
        uint8_t *f = FindByName(STRING_VALUES[c.strings[c.args[0]]]);
        S_FrameData *data = f ? AsFrame(f)->m_data : nullptr;
        if (data && data->actor) {
            static const int rides[] = {ACTOR_CAR, ACTOR_STATIC_WEAPON, ACTOR_JUNKERS88, ACTOR_BIKE};
            uint8_t *a = reinterpret_cast<uint8_t *>(data->actor);
            if (g_side)
                *reinterpret_cast<int *>(a + 0x18) = rides[(r / 4) % 4];
            else
                reinterpret_cast<C_Actor *>(a)->m_type = E_ActorType(rides[(r / 4) % 4]);
        }
    }
    if (c.kind == 1 && c.opcode == OP_HUMAN_Bike && r % 5 < 3) {
        uint8_t *f = FindByName(STRING_VALUES[c.strings[c.args[0]]]);
        S_FrameData *data = f ? AsFrame(f)->m_data : nullptr;
        if (data && data->actor) {
            uint8_t *a = reinterpret_cast<uint8_t *>(data->actor);
            if (g_side)
                *reinterpret_cast<int *>(a + 0x18) = ACTOR_BIKE;
            else
                reinterpret_cast<C_Actor *>(a)->m_type = ACTOR_BIKE;
        }
        if (r % 5 == 0)
            c.strings[c.args[1]] = 0;                   // (the point's name empty: get on only)
    }
}
// Gives the side's script the case's variables (over the world just built).
void SetVariables(const S_Case &c)
{
    S_World &w = W();
    auto frame = [&](int i) { return i < 0 ? nullptr : w.frames[i].bytes; };
    if (g_side) {
        for (int i = 0; i < FRAMES; ++i)
            OrigFrames()[i] = reinterpret_cast<uint32_t>(frame(c.frames[i]));
        for (int i = 0; i < NUMBERS; ++i)
            OrigNumbers()[2 * i] = c.numbers[i];
        for (int i = 0; i < STRINGS; ++i) {
            OriginalAt<T_StrDtor>(0x00405160)(&OrigStrings()[i]);
            OriginalAt<T_Fstr>(0x00405100)(&OrigStrings()[i], "%s", STRING_VALUES[c.strings[i]]);
        }
        *reinterpret_cast<uint32_t *>(g_block + 0x100) = reinterpret_cast<uint32_t>(frame(c.myFrame));
        *reinterpret_cast<int *>(g_block + 0xD8) = c.ip;
        g_block[0x104] = 0;
        g_block[0xD4] = 0;
        *reinterpret_cast<int *>(g_block + 0x24) = -1;
        *reinterpret_cast<uint32_t *>(g_block + 0x28) = 0;
        std::memset(g_block + 4, 0, 7);
        g_block[7] = c.hudHidden;
        g_block[8] = c.panelHidden;
        *reinterpret_cast<int *>(g_block + 0x18) = c.delay;
        *reinterpret_cast<uint32_t *>(g_block + 0x10) = c.signals;
        *reinterpret_cast<uint32_t *>(g_block) = c.kind == 1 ? O_HUMAN_VTABLE : c.kind == 2 ? O_TRACKER_VTABLE : g_baseVtable;
        *reinterpret_cast<uint8_t **>(g_block + 0x10C) = c.kind ? ScriptActor(c) : nullptr;
        g_block[0x108] = c.actorFlag;
        Name(g_block, "script");
    } else {
        g_script = g_scripts[c.kind];
        C_Script &t = *g_script;
        if (c.kind == 1)
            static_cast<C_HumanScript &>(t).m_human = reinterpret_cast<C_Human *>(ScriptActor(c));
        else if (c.kind == 2)
            static_cast<C_MeleeTrackerScript &>(t).m_meleeTracker = reinterpret_cast<C_MeleeTracker *>(ScriptActor(c));
        t.m_actorFlag = c.actorFlag;
        for (int i = 0; i < FRAMES; ++i)
            t.m_frames[i] = reinterpret_cast<I3D_frame *>(frame(c.frames[i]));
        for (int i = 0; i < NUMBERS; ++i)
            t.m_numbers[i].value = c.numbers[i];
        for (int i = 0; i < STRINGS; ++i)
            t.m_strings[i] = STRING_VALUES[c.strings[i]];
        t.m_frame = reinterpret_cast<I3D_frame *>(frame(c.myFrame));
        t.m_ip = c.ip;
        t.m_running = false;
        t.m_finished = false;
        t.m_waitAnimLabel = -1;
        t.m_waitAnimModel = nullptr;
        t.m_signalsDisabled = t.m_alarmsDisabled = t.m_wheneversDisabled = false;
        t.m_hudHiddenByScript = t.m_hudPanelHiddenByScript = t.m_inCutsceneHandler = t.m_unknown00A = false;
        t.m_hudHiddenByScript = c.hudHidden;
        t.m_hudPanelHiddenByScript = c.panelHidden;
        t.m_delay = c.delay;
        t.m_pendingSignals = c.signals;
        Name(g_script, "script");
    }
}
// Forgets the world's frames in the variables (no release: the world is freed).
void ClearVariables()
{
    for (int i = 0; i < FRAMES; ++i) {
        OrigFrames()[i] = 0;
        g_script->m_frames[i] = nullptr;
    }
    *reinterpret_cast<uint32_t *>(g_block + 0x100) = 0;
    g_script->m_frame = nullptr;
    *reinterpret_cast<uint32_t *>(g_block + 0x28) = 0;
    g_script->m_waitAnimModel = nullptr;
    *reinterpret_cast<uint32_t *>(g_block + 0x10C) = 0;
    static_cast<C_HumanScript *>(g_scripts[1])->m_human = nullptr;
    static_cast<C_MeleeTrackerScript *>(g_scripts[2])->m_meleeTracker = nullptr;
}

// The actor scripts' constructors (the original's 0x00421bc0 / 0x00421930, no frame): the actor (+0x10C), the global
// flag (+0x20) and the actor flag (+0x108), set over a block of 0xFF bytes.
void CompareConstructors()
{
    using T_ActorCtor = void *(TC *)(void *, void *, void *);
    uint8_t *actor = reinterpret_cast<uint8_t *>(0x12345678);
    for (int kind = 1; kind <= 2; ++kind) {
        uint8_t *o = static_cast<uint8_t *>(std::malloc(0x200));
        std::memset(o, 0xFF, 0x200);
        OriginalAt<T_ActorCtor>(kind == 1 ? 0x00421bc0 : 0x00421930)(o, nullptr, actor);
        alignas(16) uint8_t buf[sizeof(C_HumanScript) + sizeof(C_MeleeTrackerScript)];
        std::memset(buf, 0xFF, sizeof buf);
        C_Script *p = kind == 1 ? static_cast<C_Script *>(new (buf) C_HumanScript(nullptr, reinterpret_cast<C_Human *>(actor)))
                                : static_cast<C_Script *>(new (buf) C_MeleeTrackerScript(nullptr, reinterpret_cast<C_MeleeTracker *>(actor)));
        const uint8_t *pActor = kind == 1 ? reinterpret_cast<uint8_t *>(static_cast<C_HumanScript *>(p)->m_human)
                                          : reinterpret_cast<uint8_t *>(static_cast<C_MeleeTrackerScript *>(p)->m_meleeTracker);
        S_Result r;
        r.orig.push_back(F("actor %08x global %d flag %d vtable %08x", At<uint32_t>(o, 0x10C), int(o[0x20]), int(o[0x108]), At<uint32_t>(o, 0)));
        r.port.push_back(F("actor %08x global %d flag %d vtable %08x", reinterpret_cast<uint32_t>(pActor), int(p->m_global),
                           int(p->m_actorFlag), kind == 1 ? O_HUMAN_VTABLE : O_TRACKER_VTABLE));
        Compare(kind == 1 ? "the human script's constructor" : "the melee tracker script's constructor", r);
        p->~C_Script();
    }
}

// ---- one case on both sides ----
int g_cases, g_steps;
std::map<int, int> g_opcodeCases;
bool g_show;

// ---- part 5: a script made on a frame (C_Script's constructor and destructor with a frame, Stop and SendSignal with
// the frame's actor, NotifyFrameHit) ----
// The world's frame as it was built (an actor's frames have data), or without its data, or with data and no actor;
// a human there gets its controller type from the world (GetControllerType's answer). Compared: the frame's data after
// the constructor, the event and the destructor (its script: this one, another, none), the script's state, the frames'
// references, and every call (SetFrameData's data logged by its fields).
const char *const FRAME_EVENTS[] = {"nothing", "Stop", "SendSignal", "NotifyFrameHit"};
int g_frameCases, g_frameEvents[4];
std::string DataText(const uint8_t *frame)
{
    if (!frame)
        return "no frame";
    const S_FrameData *d = AsFrame(const_cast<uint8_t *>(frame))->m_data;
    if (!d)
        return "no data";
    return F("data actor %s script %s", NameOf(reinterpret_cast<uint32_t>(d->actor)).c_str(),
             d->script == g_frameScript[g_side] ? "this" : d->script ? "other" : "null");
}
void RunFrameScriptCase(const S_Case &c)
{
    using T_ScriptCtor = void *(TC *)(void *, void *, bool);
    using T_AddHandler = bool(TC *)(void *, int, int);
    using T_ScriptInt = void(TC *)(void *, int);
    using T_NotifyHit = void(__stdcall *)(void *);
    ++g_frameCases;
    ++g_frameEvents[c.frameEvent];
    g_current = F("case %d: a script on a frame, %s", g_cases, FRAME_EVENTS[c.frameEvent]);
    const uint32_t worldSeed = U32(), fakeBase = U32(), randSeed = U32();
    S_Result r;
    for (int side = 1; side >= 0; --side) {
        g_side = side;
        BuildWorld(side, worldSeed);
        uint8_t *frame = c.myFrame >= 0 ? W().frames[size_t(c.myFrame)].bytes : nullptr;
        if (frame && c.dataMode == 1)
            AsFrame(frame)->m_data = nullptr;
        else if (frame && c.dataMode == 2 && !AsFrame(frame)->m_data)
            AsFrame(frame)->m_data = reinterpret_cast<S_FrameData *>(Block(0x20));
        g_fakeBase = fakeBase;
        g_fakeCount = 0;
        g_log[side].clear();
        std::srand(randSeed);
        std::vector<std::string> &out = side ? r.orig : r.port;
        g_case = SIDE_NAMES[side];
        uint8_t *block = nullptr;
        C_Script *script = nullptr;
        if (side) {
            block = static_cast<uint8_t *>(std::calloc(1, 0x200));
            g_frameScript[side] = block;
            OriginalAt<T_ScriptCtor>(0x0041dcb0)(block, frame, c.global);
        } else {
            script = static_cast<C_Script *>(::operator new(sizeof(C_Script)));
            g_frameScript[side] = script;
            new (script) C_Script(AsFrame(frame), c.global);
        }
        out.push_back("made: " + DataText(frame));
        // Its state for the event: running or not at an instruction, the OnHit handler, a signal's handler.
        if (side) {
            block[0x104] = c.running;
            std::memcpy(block + 0xD8, &c.ip, 4);
            std::memcpy(block + 0xE4, &c.onHit, 4);
            if (c.handlerSignal >= 0)
                OriginalAt<T_AddHandler>(0x00415fe0)(block, c.handlerSignal, c.handlerLabel);
        } else {
            script->m_running = c.running;
            script->m_ip = c.ip;
            script->m_onHit = c.onHit;
            if (c.handlerSignal >= 0)
                script->AddSignalHandler(c.handlerSignal, c.handlerLabel);
        }
        switch (c.frameEvent) {
        case 1:
            if (side)
                OriginalAt<T_Void>(0x00416db0)(block);
            else
                script->Stop();
            break;
        case 2:
            if (side)
                OriginalAt<T_ScriptInt>(0x00416610)(block, c.signal);
            else
                script->SendSignal(c.signal);
            break;
        case 3:
            if (side)
                OriginalAt<T_NotifyHit>(0x00427700)(frame);
            else
                NotifyFrameHit(AsFrame(frame));
            break;
        default:
            break;
        }
        if (side)
            out.push_back(F("script finished %d ip %d running %d delay %d signals %08x", int(block[0xD4]),
                            At<int>(block, 0xD8), int(block[0x104]), At<int>(block, 0x18), At<uint32_t>(block, 0x10)));
        else
            out.push_back(F("script finished %d ip %d running %d delay %d signals %08x", int(script->m_finished),
                            script->m_ip, int(script->m_running), script->m_delay, script->m_pendingSignals));
        out.push_back("event: " + DataText(frame));
        if (side) {
            OriginalAt<T_Void>(0x0041df20)(block);
            std::free(block);
        } else {
            script->~C_Script();
            ::operator delete(script);
        }
        out.push_back("destroyed: " + DataText(frame));
        g_case = "";
        for (const std::string &s : FrameStates())
            out.push_back(s);
        for (const std::string &s : g_log[side])
            out.push_back("call " + s);
    }
    if (g_show) {
        std::printf("%s\n", g_current.c_str());
        for (const std::string &s : r.port)
            std::printf("  %s\n", s.c_str());
    }
    if (!Compare(g_current + F(" (frame %d, data %d, global %d)", c.myFrame, c.dataMode, int(c.global)), r) &&
        std::getenv("HD2_CMD_DUMP")) {
        for (int side = 1; side >= 0; --side) {
            std::printf("  -- %s\n", SIDE_NAMES[side]);
            for (const std::string &s : side ? r.orig : r.port)
                std::printf("    %s\n", s.c_str());
        }
    }
    FreeWorld(0);
    FreeWorld(1);
}

void RunCase(const S_Case &c)
{
    ++g_cases;
    if (c.kind == 3) {
        RunFrameScriptCase(c);
        return;
    }
    if (c.function >= 0)
        ++g_functionCases[c.function];
    else
        ++g_opcodeCases[c.opcode];
    static const char *const KINDS[] = {"", " (human script)", " (melee tracker script)"};
    g_current = c.function >= 0 ? F("case %d: function %s", g_cases, FunctionName(c.function))
                                : F("case %d: %s%s", g_cases, OpName(c.opcode).c_str(), KINDS[c.kind]);
    const uint32_t worldSeed = U32(), fakeBase = U32(), randSeed = U32();
    S_Result r;
    int answers[2];
    for (int side = 1; side >= 0; --side) {
        BuildWorld(side, worldSeed);
        if (c.noCamera)
            *reinterpret_cast<uint8_t **>(W().scene + 0x15C) = nullptr;
        AvoidOriginalWeaknesses(c);
        S_Case biased = c;
        BiasWorld(biased);
        SetVariables(biased);
        g_fakeBase = fakeBase;
        g_fakeCount = 0;
        g_log[side].clear();
        std::srand(randSeed);
        g_case = SIDE_NAMES[side];
        if (c.function >= 0) {
            answers[side] = side ? OriginalAt<T_Call>(O_CALL_FUNCTION)(g_block, c.function, c.types, c.args)
                                 : g_script->CallFunction(c.function, c.types, c.args);
        } else if (side) {
            const int cmd[2] = {c.opcode, reinterpret_cast<int>(c.args)};
            answers[side] = OriginalAt<T_Exec>(c.kind == 1 ? O_HUMAN_EXECUTE : c.kind == 2 ? O_TRACKER_EXECUTE : O_EXECUTE)(g_block, cmd);
        } else {
            S_ScriptCommand cmd;
            cmd.opcode = c.opcode;
            cmd.args = const_cast<int *>(c.args);
            answers[side] = c.kind ? g_script->ExecuteCommand(cmd) : g_script->C_Script::ExecuteCommand(cmd);
            cmd.args = nullptr;
        }
        g_case = "";
        std::vector<std::string> &out = side ? r.orig : r.port;
        out.push_back(F("answer %d", answers[side]));
        for (const std::string &s : side ? OrigScriptState() : PortScriptState())
            out.push_back(s);
        for (const std::string &s : FrameStates())
            out.push_back(s);
        for (const std::string &s : GameState())
            out.push_back(s);
        for (const std::string &s : g_log[side])
            out.push_back("call " + s);
    }
    std::string args;
    for (int i = 0; i < (c.function >= 0 ? GetScriptFunctionArgCount(c.function) : GetScriptArgCount(c.opcode)); ++i)
        args += F(" %d", c.args[i]);
    if (g_show) {
        std::printf("%s args%s\n", g_current.c_str(), args.c_str());
        for (const std::string &s : r.port)
            std::printf("  %s\n", s.c_str());
    }
    if (!Compare(g_current + " args" + args, r) && std::getenv("HD2_CMD_DUMP")) {
        for (int side = 1; side >= 0; --side) {
            std::printf("  -- %s\n", SIDE_NAMES[side]);
            for (const std::string &s : side ? r.orig : r.port)
                std::printf("    %s\n", s.c_str());
        }
    }
    ClearVariables();
    FreeWorld(0);
    FreeWorld(1);
}

void SetUpCmd()
{
    g_thunks = static_cast<uint8_t *>(VirtualAlloc(nullptr, 0x40000, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
    SetUpVirtuals();
    SetUpAnswers();
    // The original's calls into the game: each to its logger thunk.
    for (int i = 0; i < NCALLS; ++i)
        if (CALLS[i].orig && CALLS[i].vt < 0)
            RedirectOriginal(CALLS[i].orig, Thunk(uint32_t(i)));
    RedirectOriginal(0x007e529c, reinterpret_cast<const void *>(&O_I3DGetDriver));
    RedirectOriginal(O_RAND, reinterpret_cast<const void *>(&O_Rand));
    // The game commands: every opcode but the script-level ones (part 2).
    for (int op = 1; op <= OP_LAST_COMMAND; ++op) {
        switch (op) {
        case OP_EndScript: case OP_If: case OP_Else: case OP_BlockBegin: case OP_BlockEnd: case OP_Whenever:
        case OP_Evaluate: case OP_SetWhenever: case OP_SetWheneverTime: case OP_DisableSignals: case OP_DisableAlarms:
        case OP_DisableWhenevers: case OP_EnableSignal: case OP_SaveGameValue: case OP_FRM_TimerSetOn: case OP___Log:
        case OP___LogNumber: case OP___LogFrame: case OP___SetupAutoLogging: case OP_Integer: case OP_Real:
        case OP_Bool: case OP_String: case OP_Enum: case OP_Frame: case OP_Label: case OP_GoTo: case OP_Gosub:
        case OP_Return: case OP_Delay: case OP_OnSignal: case OP_ScriptStartPoint: case OP_OnDeath: case OP_OnUse:
        case OP_OnAlarm: case OP_OnHit: case OP_OnGlobalAlarm: case OP_OnExit: case OP_OnAlarmDone:
        case OP_OnCutsceneDone: case OP_OnCutscene: case OP_Block: case OP_SendSignal:
            continue;
        default:
            break;
        }
        if (const char *only = std::getenv("HD2_CMD_ONLY"))
            if (Lower(only) != Lower(OpName(op)))
                continue;
        g_opcodes.push_back(op);
    }
    // The functions (HD2_CMD_ONLY names one of them too).
    for (int i = 0; g_scriptFunctionDefs[i].name; ++i) {
        if (const char *only = std::getenv("HD2_CMD_ONLY"))
            if (Lower(only) != Lower(g_scriptFunctionDefs[i].name))
                continue;
        g_functions.push_back(&g_scriptFunctionDefs[i]);
    }
    if (std::getenv("HD2_CMD_ONLY") && !g_functions.empty()) {
        g_opcodes.clear();
        g_functionsOnly = true;
    }
    if (g_opcodes.empty() && !g_functions.empty())
        g_opcodes.push_back(OP_FRM_SetOn);           // (unused: every case calls the function)
    if (g_opcodes.empty()) {
        std::printf("cmd_diff: no opcode to test\n");
        std::exit(2);
    }
    // Part 4: the actor scripts' own commands (the melee tracker's 13..27, the human's 28..248).
    for (int op : g_opcodes) {
        if (op >= OP_MELEE_TRACKER_SetOptimized && op <= OP_MELEE_TRACKER_SETAIMODE_Defensive)
            g_trackerOpcodes.push_back(op);
        else if (op >= OP_HUMAN_CreateItemInStore && op <= OP_HUMAN_SETEVENTS)
            g_humanOpcodes.push_back(op);
    }
    g_actorsOnly = std::getenv("HD2_CMD_ACTORS") != nullptr;
    if (g_actorsOnly && (g_functionsOnly || (g_humanOpcodes.empty() && g_trackerOpcodes.empty()))) {
        std::printf("cmd_diff: HD2_CMD_ACTORS: no actor script command to test\n");
        std::exit(2);
    }
}

}  // namespace H

#ifndef CMD_DIFF_NO_MAIN
int main()
{
    using namespace H;
    SetUnhandledExceptionFilter(OnCrash);
    if (std::getenv("HD2_SC_FIRST_CHANCE"))
        AddVectoredExceptionHandler(1, FirstChance);
    int exitCode = 0;
    if (RunReservingOriginalBase(exitCode))
        return exitCode;
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    if (!LoadOriginalExe()) {
        std::printf("cmd_diff: no original executable (HD2_ORIGINAL_EXE): nothing to compare with\n");
        return 1;
    }
    _controlfp(0x00020000u, 0x00030000u);                       // _PC_24, _MCW_PC: the game's precision
    if (const char *d = std::getenv("HD2_UNPACKED"))
        g_data = d;
    if (const char *d = std::getenv("HD2_SC_DATA"))
        g_data = d;
    if (const char *s = std::getenv("HD2_CMD_SEED"))
        g_seed = uint32_t(std::strtoul(s, nullptr, 10)) | 1;
    g_show = std::getenv("HD2_CMD_SHOW") != nullptr;
    int cases = std::getenv("HD2_CMD_QUICK") ? 30000 : 200000;
    if (const char *s = std::getenv("HD2_CMD_CASES"))
        cases = std::atoi(s);
    new (&reinterpret_cast<C_game *>(g_portGameBuf)->m_missionName) C_str();
    SetUp();
    SetUpCmd();
    SetMission("AFRICA1");
    CompileProgram();
    CompareConstructors();
    for (int n = 0; n < cases; ++n)
        RunCase(RandomCase());
    std::printf("cmd_diff: %d cases over %d opcodes and %d functions (%d inputs the original crashes on changed, %d it "
                "misbehaves on), %d calls logged, %d checks, %d failures\n", g_cases, int(g_opcodeCases.size()),
                int(g_functionCases.size()), g_originalCrashes, g_originalWeaknesses, g_calls[0], checks, failures);
    std::printf("cmd_diff: part 5: %d scripts made on a frame (events: nothing %d, Stop %d, SendSignal %d, NotifyFrameHit %d)\n",
                g_frameCases, g_frameEvents[0], g_frameEvents[1], g_frameEvents[2], g_frameEvents[3]);
    return failures != 0;
}
#endif
