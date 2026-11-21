// The game interface's core (game/interface.cpp, part 75) against the original's: what its parts share, included after
// the kit (interface_kit.inc, interface_gfx.inc). The port's C_Interface and the original's (0x6B900 bytes, built by its
// constructor 0x0049a420) are each built once and restored before each scenario. Every method of the panels and of the
// game's objects the interface calls is faked on both sides by the generated loggers (ui_fakes.inc, from the methods'
// declarations with their addresses: gen_ifakes.py), the engine's objects by the generated slot loggers (ui_vt.inc:
// the scene, the driver, cameras, lights, sounds, sectors, the sound driver). Their answers come from Ret<T>(method):
// drawn from the scenario's seed, the method's name and its count of calls on this side (the same on both sides while
// the calls match); the pointers by the method (the humans, the camera module).
// Names: the interface's members by their offsets in each side's block ("members[2]", "rollMenu"), frames "fN", the
// humans "hN".

namespace H {

int g_side;
alignas(16) uint8_t g_origCameraModule[0x100], g_portCameraModule[0x100], g_effects[0x100], g_squadCommander[2][0x200];

// ---- the two interfaces ----
uint8_t *g_orig, *g_origPristine, *g_portPristine;
C_Interface *g_port;
constexpr uint32_t ORIG_SIZE = 0x6B900;
#define OF(f) uint32_t(offsetof(C_Interface, f))
struct S_Member {
    const char *name;
    uint32_t orig, port, size;   // size: an array's element (0: one)
    int count;
};
const S_Member MEMBERS[] = {
    {"inventory", 0x30, OF(m_inventory), 0, 1},        {"speedometer", 0x2E50, OF(m_speedometer), 0, 1},
    {"reticle", 0x2E60, OF(m_reticle), 0, 1},           {"signals", 0x2E80, OF(m_signals), 0, 1},
    {"rollMenu", 0x2F00, OF(m_rollMenu), 0, 1},         {"messages", 0x2F90, OF(m_messages), 0, 1},
    {"members", 0x10320, OF(m_members), 0xC0, 6},       {"crosshair", 0x107A0, OF(m_crosshair), 0, 1},
    {"weaponPanel", 0x107E0, OF(m_weaponPanel), 0, 1},  {"stancePanel", 0x1081C, OF(m_stancePanel), 0, 1},
    {"compass", 0x10854, OF(m_compass), 0, 1},          {"map", 0x108F8, OF(m_map), 0, 1},
    {"progress", 0x10B08, OF(m_progress), 0, 1},        {"commandPanel", 0x10B20, OF(m_commandPanel), 0, 1},
    {"infoSelector", 0x6B2E0, OF(m_infoSelector), 0, 1}, {"cursor", 0x6B320, OF(m_cursor), 0, 1},
    {"info", 0x6B3A0, OF(m_info), 0, 1},                {"weaponSelector", 0x6B4E4, OF(m_weaponSelector), 0, 1},
    {"injuryFlash", 0x6B520, OF(m_injuryFlash), 0, 1},  {"bigMessage", 0x6B550, OF(m_bigMessage), 0, 1},
    {"photoFlash", 0x6B558, OF(m_photoFlash), 0, 1},    {"objectives", 0x6B564, OF(m_objectives), 0, 1},
    {"scoreTable", 0x6B628, OF(m_scoreTable), 0, 1},    {"voting", 0x6B634, OF(m_voting), 0, 1},
    {"explosiveTimer", 0x6B63C, OF(m_explosiveTimer), 0, 1}, {"carnage", 0x6B6B0, OF(m_carnage), 0, 1},
    {"pauseText", 0x6B6B8, OF(m_pauseText), 0, 1},      {"countdown", 0x6B6C4, OF(m_countdown), 0, 1},
    {"stateIcons", 0x6B6D0, OF(m_stateIcons), 4, 20},   {"stateIcons2", 0x6B720, OF(m_stateIcons2), 4, 40},
    {"actionIcons", 0x6B7C0, OF(m_actionIcons), 4, 39}, {"debugText", 0x6B85C, OF(m_debugText), 0, 1},
    {"ui", 0, 0, 0, 1},
};
// A pointer into either side's interface: its member's name (the last member starting at or before it).
std::string MemberName(const void *p)
{
    for (int side = 0; side < 2; ++side) {
        const uint8_t *base = side ? reinterpret_cast<const uint8_t *>(g_port) : g_orig;
        if (!base)
            continue;
        const uint32_t size = side ? uint32_t(sizeof(C_Interface)) : ORIG_SIZE;
        const uint8_t *b = static_cast<const uint8_t *>(p);
        if (b < base || b >= base + size)
            continue;
        const uint32_t off = uint32_t(b - base);
        const S_Member *best = nullptr;
        for (const S_Member &m : MEMBERS) {
            const uint32_t start = side ? m.port : m.orig;
            if (start <= off && (!best || start > (side ? best->port : best->orig)))
                best = &m;
        }
        const uint32_t start = side ? best->port : best->orig;
        std::string n = best->name;
        if (best->size) {
            const uint32_t k = (off - start) / best->size;
            n += "[" + std::to_string(k) + "]";
            if ((off - start) % best->size)
                n += "+" + std::to_string((off - start) % best->size);
        } else if (off != start)
            n += "+" + std::to_string(off - start);
        return n;
    }
    return "";
}

// ---- the humans (h0..h7: raw blocks, both sides' offsets written) ----
constexpr int HUMANS = 8;
alignas(16) uint8_t g_humans[HUMANS][0x1000];
void *Hum(int i) { return i < 0 || i >= HUMANS ? nullptr : g_humans[i]; }
int HumIndex(const void *p)
{
    for (int i = 0; i < HUMANS; ++i)
        if (p == g_humans[i])
            return i;
    return -1;
}

// ---- the answers ----
uint32_t g_retSeed;
std::vector<std::pair<std::string, int>> g_retCounts;
uint32_t Fnv(const char *s, uint32_t h)
{
    for (; *s; ++s)
        h = (h ^ uint8_t(*s)) * 16777619u;
    return h;
}
// The method's next answer word: the scenario's seed, its name, its count of calls on this side.
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
// (A part's own pointer answers by the method's name: null when it has none.)
void *(*g_retPtr)(const char *name, uint32_t word) = nullptr;
// (A part's own int answers: true when it gives one; `count` the method's calls on this side.)
bool (*g_retInt)(const char *name, int count, int &value) = nullptr;
int RetCount(const char *name)
{
    for (auto &c : g_retCounts)
        if (c.first == name)
            return c.second;
    return 0;
}
template <class T> T Ret(const char *name);
template <> bool Ret<bool>(const char *name) { return (RetWord(name) & 7) != 0; }
template <> int Ret<int>(const char *name)
{
    const uint32_t w = RetWord(name);
    int value;
    if (g_retInt && g_retInt(name, RetCount(name), value))
        return value;
    return (w & 3) == 0 ? -1 : int(w >> 8 & 7);
}
template <> float Ret<float>(const char *name)
{
    const uint32_t w = RetWord(name);
    return (w & 3) == 0 ? 0.0f : float(w >> 8 & 0xFF) / 64.0f;
}
template <> void *Ret<void *>(const char *name)
{
    const uint32_t w = RetWord(name);
    if (g_retPtr)
        if (void *p = g_retPtr(name, w))
            return p;
    if (std::strstr(name, "::GetCameraModule"))
        return g_side ? g_portCameraModule : g_origCameraModule;
    if (std::strstr(name, "::GetHumanEffects"))
        return g_effects;
    if (std::strstr(name, "GetActivePlayer") || std::strstr(name, "GetCommandedMember") || std::strstr(name, "GetSelectedMember") ||
        std::strstr(name, "GetMember"))
        return (w & 7) == 0 ? nullptr : Hum(int(w >> 4 & 7));
    return nullptr;
}
template <> const char *Ret<const char *>(const char *) { return "text"; }

std::string F3(const float *v) { return FloatText(v[0]) + " " + FloatText(v[1]) + " " + FloatText(v[2]); }
std::string V4(const float *v) { return F3(v) + " " + FloatText(v[3]); }

}  // namespace H

#include "ui_vt.inc"
#include "ui_fakes.inc"
#define IFAKES_PORT
#include "ui_fakes.inc"
#undef IFAKES_PORT

namespace H {

// ---- the engine's objects: each kind its vtable (the kit's frame slots, then the kind's own) ----
void *g_bigSceneVt[0x60], *g_bigDriverVt[0x30], *g_cameraVt[64], *g_lightVt[64], *g_soundVt[64], *g_sectorVt[64], *g_sndDriverVt[16],
     *g_modelVt[64];
S_Fake g_sndDriver{g_sndDriverVt};
void __stdcall V_Other0(void *self) { Log("%s other slot", Name(self).c_str()); }
// Frames by kind: f0 the camera, f1 the light, f2 the primary sector, f3..f18 the models, f19..f26 the sounds (handed
// out in turn: Init's own and the caches' copies; a scenario makes fewer); f27.. the pool.
constexpr int F_CAMERA = 0, F_LIGHT = 1, F_SECTOR = 2, F_MODEL = 3, MODELS = 16, F_SOUND = 19, SOUNDS = 8, F_POOL = 27;
int g_models, g_sounds;   // this side's models and sounds made
void SetKinds()
{
    *reinterpret_cast<void ***>(FrameAt(F_CAMERA)) = g_cameraVt;
    *reinterpret_cast<void ***>(FrameAt(F_LIGHT)) = g_lightVt;
    *reinterpret_cast<void ***>(FrameAt(F_SECTOR)) = g_sectorVt;
    for (int k = 0; k < MODELS; ++k)
        *reinterpret_cast<void ***>(FrameAt(F_MODEL + k)) = g_modelVt;
    for (int k = 0; k < SOUNDS; ++k)
        *reinterpret_cast<void ***>(FrameAt(F_SOUND + k)) = g_soundVt;
}
bool g_noScene;   // CreateFrame gives no scene (Init fails)
// The interface's own scene (CreateFrame's): its vtable the big one, the primary sector at +0x1F8, the viewport at
// +0x224 (and +0x244: the SDK header's other copy) written from the world.
alignas(16) uint8_t g_uiScene[0x400];
void *__stdcall D_CreateFrame(void *, uint32_t type)
{
    void *f = type == FRAME_SCENE    ? (g_noScene ? nullptr : static_cast<void *>(g_uiScene))
              : type == FRAME_MODEL  ? static_cast<void *>(FrameAt(F_MODEL + g_models++ % MODELS))
              : type == FRAME_CAMERA ? static_cast<void *>(FrameAt(F_CAMERA))
              : type == FRAME_LIGHT  ? static_cast<void *>(FrameAt(F_LIGHT))
              : type == FRAME_SOUND  ? static_cast<void *>(FrameAt(F_SOUND + g_sounds++ % SOUNDS))
                                     : nullptr;
    Log("driver CreateFrame %u -> %s", type, Name(f).c_str());
    return f;
}
// EnumFrames (0x44): the callback run on the frame itself (the caches' clears its data).
void __stdcall V_EnumFrames(void *self, uint32_t(__stdcall *proc)(void *, void *), void *context, uint32_t typeMask, const char *mask)
{
    Log("%s I3D_frame EnumFrames %s %x %s", Name(self).c_str(), Name(context).c_str(), typeMask, mask ? mask : "null");
    if (proc)
        proc(self, context);
}
// The driver's states (the kit keeps 16; the interface's model loads with states 0x15 and 0x19): a part's world sets
// them.
uint32_t g_states[64];
void __stdcall D_SetState64(void *, int state, uint32_t value)
{
    Log("driver SetState %d %u", state, value);
    if (state >= 0 && state < 64)
        g_states[state] = value;
}
uint32_t __stdcall D_GetState64(void *, int state)
{
    Log("driver GetState %d", state);
    return state >= 0 && state < 64 ? g_states[state] : 0;
}
void SetUpEngine()
{
    // (The frames' slots the kit leaves: I3D_frame's generated loggers, a catch-all otherwise.)
    {
        void *base[64];
        for (void *&v : base)
            v = reinterpret_cast<void *>(V_Other0);
        FillVt_I3D_frame(base);
        base[0x44 / 4] = reinterpret_cast<void *>(V_EnumFrames);
        for (int k = 0; k < 64; ++k)
            if (!g_frameVt[k] || g_frameVt[k] == reinterpret_cast<void *>(F_Other))   // (the kit's catch-all: one argument)
                g_frameVt[k] = base[k];
    }
    std::copy(g_sceneVt, g_sceneVt + 64, g_bigSceneVt);
    for (int k = 64; k < 0x60; ++k)
        g_bigSceneVt[k] = reinterpret_cast<void *>(V_Other0);
    for (void *&v : g_bigSceneVt)
        if (!v)
            v = reinterpret_cast<void *>(V_Other0);
    FillVt_I3D_scene(g_bigSceneVt);
    g_bigSceneVt[0x7C / 4] = g_sceneVt[0x7C / 4];   // (the kit's FindFrame)
    g_scene.vt = g_bigSceneVt;
    std::copy(g_driverVt, g_driverVt + 32, g_bigDriverVt);
    for (void *&v : g_bigDriverVt)
        if (!v)
            v = reinterpret_cast<void *>(V_Other0);
    FillVt_I3D_driver(g_bigDriverVt);
    for (int s : {0x18, 0x1C, 0x50})
        g_bigDriverVt[s / 4] = g_driverVt[s / 4];   // (the kit's states and visuals)
    g_bigDriverVt[0x54 / 4] = reinterpret_cast<void *>(D_CreateFrame);
    g_bigDriverVt[0x18 / 4] = reinterpret_cast<void *>(D_SetState64);
    g_bigDriverVt[0x1C / 4] = reinterpret_cast<void *>(D_GetState64);
    g_driver.vt = g_bigDriverVt;
    for (void **vt : {g_cameraVt, g_lightVt, g_soundVt, g_sectorVt, g_modelVt})
        for (int k = 0; k < 64; ++k)
            vt[k] = g_frameVt[k] ? g_frameVt[k] : reinterpret_cast<void *>(V_Other0);
    FillVt_I3D_camera(g_cameraVt);
    FillVt_I3D_light(g_lightVt);
    FillVt_I3D_sound(g_soundVt);
    FillVt_I3D_sector(g_sectorVt);
    FillVt_I3D_model(g_modelVt);
    for (void *&v : g_sndDriverVt)
        v = reinterpret_cast<void *>(V_Other0);
    FillVt_ISND_driver(g_sndDriverVt);
}
void *__cdecl O_ISndGetDriver() { return &g_sndDriver; }

}  // namespace H

ISND_driver *ISndGetDriver() { return reinterpret_cast<ISND_driver *>(&H::g_sndDriver); }

// ---- by hand: the free functions and objects the interface reaches, the methods the original inlines ----
namespace H {
// ---- the panels' fields the interface reads inline (their methods faked: as their Inits and updates leave them) ----
// Each written on both sides at a side's start from the scenario's values: frames (a pool frame or null), humans, the
// radar (an object or null), flags and counts.
enum { PF_FRAME, PF_HUMAN, PF_RADAR, PF_BOOL, PF_INT, PF_TYPE };
struct S_PanelField {
    const char *what;
    uint32_t orig, port;
    int kind;
};
#define PF(member, origMember, cls, field, origField, kind) \
    {#member "." #field, origMember + origField, OF(member) + uint32_t(offsetof(cls, field)), kind}
#define PFM(i, field, origField, kind) \
    {"members." #field, 0x10320 + 0xC0 * i + origField, OF(m_members) + 0xC0 * i + uint32_t(offsetof(C_MemberPanel, field)), kind}
const S_PanelField PANEL_FIELDS[] = {
    PF(m_carnage, 0x6B6B0, C_CarnageStats, m_frame, 0, PF_FRAME),
    PF(m_commandPanel, 0x10B20, C_CommandPanel, m_target, 0x1C, PF_HUMAN),
    PF(m_compass, 0x10854, C_Compass, m_radar, 0x1C, PF_RADAR),
    PF(m_countdown, 0x6B6C4, C_CountdownText, m_shown, 4, PF_BOOL),
    PF(m_explosiveTimer, 0x6B63C, C_ExplosiveTimer, m_frame, 0, PF_FRAME),
    PF(m_info, 0x6B3A0, C_InfoScreen, m_frame, 0x14, PF_FRAME),
    PF(m_injuryFlash, 0x6B520, C_InjuryFlash, m_frame, 0, PF_FRAME),
    PF(m_map, 0x108F8, C_MapScreen, m_frame, 4, PF_FRAME),
    PFM(0, m_frame, 8, PF_FRAME), PFM(1, m_frame, 8, PF_FRAME), PFM(2, m_frame, 8, PF_FRAME),
    PFM(3, m_frame, 8, PF_FRAME), PFM(4, m_frame, 8, PF_FRAME), PFM(5, m_frame, 8, PF_FRAME),
    PFM(0, m_human, 0x74, PF_HUMAN), PFM(1, m_human, 0x74, PF_HUMAN), PFM(2, m_human, 0x74, PF_HUMAN),
    PFM(3, m_human, 0x74, PF_HUMAN), PFM(4, m_human, 0x74, PF_HUMAN), PFM(5, m_human, 0x74, PF_HUMAN),
    PF(m_messages, 0x2F90, C_MessageLog, m_mode, 0x10, PF_INT),
    PF(m_messages, 0x2F90, C_MessageLog, m_inputOpen, 0xD385, PF_BOOL),
    PF(m_objectives, 0x6B564, C_ObjectiveIcons, m_frame, 0, PF_FRAME),
    PF(m_pauseText, 0x6B6B8, C_PauseText, m_shown, 4, PF_BOOL),
    PF(m_photoFlash, 0x6B558, C_PhotoFlash, m_frame, 0, PF_FRAME),
    PF(m_progress, 0x10B08, C_ProgressBar, m_frame, 0, PF_FRAME),
    PF(m_rollMenu, 0x2F00, C_RollMenu, m_target, 0x88, PF_HUMAN),
    PF(m_rollMenu, 0x2F00, C_RollMenu, m_open, 0x30, PF_BOOL),
    PF(m_scoreTable, 0x6B628, C_ScoreTable, m_frame, 0, PF_FRAME),
    PF(m_scoreTable, 0x6B628, C_ScoreTable, m_gameType, 8, PF_TYPE),
    PF(m_speedometer, 0x2E50, C_Speedometer, m_frame, 0, PF_FRAME),
    PF(m_stancePanel, 0x1081C, C_StancePanel, m_frame, 4, PF_FRAME),
    PF(m_stancePanel, 0x1081C, C_StancePanel, m_timer, 0x34, PF_INT),
    PF(m_voting, 0x6B634, C_VotingPanel, m_frame, 0, PF_FRAME),
    PF(m_weaponPanel, 0x107E0, C_WeaponPanel, m_frame, 4, PF_FRAME),
    PF(m_weaponSelector, 0x6B4E4, C_WeaponSelector, m_frame, 4, PF_FRAME),
    PF(m_inventory, 0x30, C_InventoryScreen, m_frame, 4, PF_FRAME),
    PF(m_bigMessage, 0x6B550, C_BigMessage, m_frame, 0, PF_FRAME),
};
constexpr int PANEL_FIELD_COUNT = int(sizeof PANEL_FIELDS / sizeof *PANEL_FIELDS);
int g_panelValues[PANEL_FIELD_COUNT];
alignas(16) uint8_t g_radar[0x100];
// (Frames from the pool's end, kept apart from what the parts' scenes find.)
constexpr int PANEL_FRAMES_FROM = 96;
void RandomPanels()
{
    for (int k = 0; k < PANEL_FIELD_COUNT; ++k) {
        switch (PANEL_FIELDS[k].kind) {
        case PF_FRAME:
            // (Only the objectives' frame is ever missing: maps without objectives.)
            g_panelValues[k] = (std::strstr(PANEL_FIELDS[k].what, "objectives") || std::strstr(PANEL_FIELDS[k].what, "m_map.")) && P(0.3)
                                   ? -1
                                   : PANEL_FRAMES_FROM + I(0, 31);
            break;
        case PF_HUMAN:
            g_panelValues[k] = P(0.3) ? -1 : I(0, HUMANS - 1);
            break;
        case PF_RADAR:
            g_panelValues[k] = P(0.8);
            break;
        case PF_BOOL:
            g_panelValues[k] = P(0.3);
            break;
        case PF_INT:
            g_panelValues[k] = P(0.5) ? 0 : I(-1, 3);
            break;
        case PF_TYPE:
            g_panelValues[k] = P(0.5) ? -1 : I(0, 4);
            break;
        }
    }
}
void SetPanelFields(int side)
{
    uint8_t *b = side ? reinterpret_cast<uint8_t *>(g_port) : g_orig;
    for (int k = 0; k < PANEL_FIELD_COUNT; ++k) {
        const S_PanelField &f = PANEL_FIELDS[k];
        uint8_t *at = b + (side ? f.port : f.orig);
        const int v = g_panelValues[k];
        switch (f.kind) {
        case PF_FRAME: {
            void *p = v < 0 ? nullptr : static_cast<void *>(FrameAt(v));
            std::memcpy(at, &p, 4);
            break;
        }
        case PF_HUMAN: {
            void *p = Hum(v);
            std::memcpy(at, &p, 4);
            break;
        }
        case PF_RADAR: {
            void *p = v ? static_cast<void *>(g_radar) : nullptr;
            std::memcpy(at, &p, 4);
            break;
        }
        case PF_BOOL:
            *at = uint8_t(v);
            break;
        default:
            std::memcpy(at, &v, 4);
            break;
        }
    }
}
}  // namespace H

namespace H {
void *g_origCameraModuleVt[0x40];
void *__cdecl O_GetCameraModule() { return g_origCameraModule; }
void *__cdecl O_GetSquadCommander() { return g_squadCommander[0]; }
int CheatValue(int index)
{
    const int v = Ret<int>("GetCheatValue");
    Log("GetCheatValue %d -> %d", index, v);
    return v;
}
int __attribute__((fastcall)) O_GetCheatValue(int index) { return CheatValue(index); }
// (The inventory's Refresh: 0x004a26d0, no address in the header; the port's logs the same.)
void __attribute__((thiscall)) O_InventoryRefresh(void *self) { Log("%s C_InventoryScreen::Refresh", Name(self).c_str()); }
// (The original reaches the camera module's methods through its vtable.)
void SetUpCameraModule()
{
    for (void *&v : g_origCameraModuleVt)
        v = reinterpret_cast<void *>(V_Other0);
    g_origCameraModuleVt[0x58 / 4] = reinterpret_cast<void *>(O_C_CameraModule_GetViewDistance);
    g_origCameraModuleVt[0x68 / 4] = reinterpret_cast<void *>(O_C_CameraModule_GetCameraDir);
    g_origCameraModuleVt[0x80 / 4] = reinterpret_cast<void *>(O_C_CameraModule_HasScriptedView);
    g_origCameraModuleVt[0x88 / 4] = reinterpret_cast<void *>(O_C_CameraModule_GetHumanEffects);
    *reinterpret_cast<void ***>(g_origCameraModule) = g_origCameraModuleVt;
}
// The deterministic clock (the double clicks): the world's.
uint32_t g_time;
DWORD Time()
{
    Log("timeGetTime -> %u", g_time);
    return g_time;
}
DWORD __stdcall O_TimeGetTime() { return Time(); }
std::string CommonName(const void *p)
{
    if (p == g_origCameraModule || p == g_portCameraModule)
        return "cameraModule";
    if (p == g_squadCommander[0] || p == g_squadCommander[1])
        return "squadCommander";
    if (p == g_effects)
        return "effects";
    if (p == g_radar)
        return "radar";
    return "";
}
}  // namespace H

DWORD HarnessTimeGetTime() { return H::Time(); }
C_CameraModule *GetCameraModule() { return reinterpret_cast<C_CameraModule *>(H::g_portCameraModule); }
C_SquadCommander *GetSquadCommander() { return reinterpret_cast<C_SquadCommander *>(H::g_squadCommander[1]); }
int GetCheatValue(int index) { return H::CheatValue(index); }
C_MessageLog::C_MessageLog() {}
C_MessageLog::~C_MessageLog() {}
// (Inlined in the original: the same reads.)
bool C_InfoScreen::IsShown() const { return m_frame->IsOn(); }
bool C_InventoryScreen::IsShown() const { return m_frame->IsOn(); }
bool C_BigMessage::IsShown() const { return m_frame->IsOn(); }
void C_InventoryScreen::Refresh() { H::Log("%s C_InventoryScreen::Refresh", H::Name(this).c_str()); }
void C_Radar::Forget(const C_Human *human)
{
    H::Log("%s C_Radar::Forget %s", H::Name(this).c_str(), H::Name(human).c_str());
}
// (The saved game's chunks: part 75f's.)
bool C_chunk::Begin(uint16_t) { H::Log("chunk Begin"); return true; }
bool C_chunk::Ascend(uint32_t *) { H::Log("chunk Ascend"); return true; }
bool C_chunk::Descend(uint16_t &) { H::Log("chunk Descend"); return false; }
bool C_chunk::Read(void *, int) { H::Log("chunk Read"); return false; }
int C_chunk::RemainingSize() const { H::Log("chunk RemainingSize"); return 0; }
bool C_chunk::Write(const void *, int) { H::Log("chunk Write"); return true; }
extern "C" void dbgPrintf(const char *format, ...) { H::Log("dbgPrintf %s", format); }
