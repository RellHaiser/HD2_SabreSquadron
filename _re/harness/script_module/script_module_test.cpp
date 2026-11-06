// C_ScriptModule's saved games, state and messages (part 11a: script/script_module_save.cpp) with the engine and the game
// faked: frames with logging tables, a memory file behind rw_data, the module's other members logging. MinGW; build.bat; run
// script_module_test.exe ("script_module: N checks, 0 failures"). Mutations: mutations/mutate_11a.py.
#include <windows.h>

#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <vector>

#define protected public
#define private public
#include "common/chunk.h"
#include "game/actor.h"
#include "game/animation_actor.h"
#include "game/game.h"
#include "game/geffects.h"
#include "game/human.h"
#include "game/human_movement.h"
#include "game/lightmap_groups.h"
#include "game/particles.h"
#include "game/pending.h"
#include "game/resource_cache.h"
#include "game/team.h"
#include "ls3d/i3d.h"
#include "ls3d/rw_data.h"
#include "net/bitstream.h"
#include "script/script.h"
#include "script/script_module.h"
#undef private
#undef protected

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

std::vector<std::string> g_log;
void Log(const char *format, ...)
{
    char text[400];
    va_list args;
    va_start(args, format);
    std::vsnprintf(text, sizeof text, format, args);
    va_end(args);
    g_log.push_back(text);
}
std::string TakeLog()
{
    std::string all;
    for (const std::string &line : g_log)
        all += line + "\n";
    g_log.clear();
    return all;
}
}  // namespace

// ---- rw_data: one memory file ------------------------------------------------------------------------------------------

namespace {
std::string g_file;
size_t g_at;
}  // namespace
int RW_DATA_CALL dtaOpen(const char *, bool) { g_at = 0; return 1; }
int RW_DATA_CALL dtaOpenWrite(const char *, int) { g_file.clear(); g_at = 0; return 1; }
bool RW_DATA_CALL dtaClose(int) { return true; }
int RW_DATA_CALL dtaSeek(int, int offset, int origin)
{
    const long base = origin == 0 ? 0 : origin == 1 ? static_cast<long>(g_at) : static_cast<long>(g_file.size());
    g_at = static_cast<size_t>(base + offset);
    return static_cast<int>(g_at);
}
int RW_DATA_CALL dtaRead(int, void *data, int size)
{
    const int left = static_cast<int>(g_file.size()) - static_cast<int>(g_at);
    const int n = size < left ? size : (left < 0 ? 0 : left);
    if (n > 0)
        std::memcpy(data, g_file.data() + g_at, static_cast<size_t>(n));
    g_at += static_cast<size_t>(n);
    return n;
}
int RW_DATA_CALL dtaWrite(int, const void *data, int size)
{
    if (g_at + static_cast<size_t>(size) > g_file.size())
        g_file.resize(g_at + static_cast<size_t>(size));
    std::memcpy(&g_file[g_at], data, static_cast<size_t>(size));
    g_at += static_cast<size_t>(size);
    return size;
}

// ---- frames --------------------------------------------------------------------------------------------------------------

namespace {
const char *NameOf(const I3D_frame *f) { return f ? f->GetName() : "(null)"; }
uint32_t __stdcall F_Release(I3D_frame *f) { Log("release %s", NameOf(f)); return --f->m_refCount; }
uint32_t g_visualType;
uint32_t __stdcall F_VisualType(I3D_frame *) { return g_visualType; }
void __stdcall F_Tick(I3D_frame *f, int t) { Log("tick %s %d", NameOf(f), t); }
void __stdcall F_Update(I3D_frame *f) { Log("update %s", NameOf(f)); }
void __stdcall F_SetOn(I3D_frame *f, bool on)
{
    Log("on %s %d", NameOf(f), on);
    f->m_frameFlags = on ? f->m_frameFlags | 1 : f->m_frameFlags & ~1u;
}
void __stdcall F_LinkTo(I3D_frame *f, I3D_frame *parent, uint32_t flags) { Log("link %s %s %u", NameOf(f), NameOf(parent), flags); f->m_parent = parent; }
// Models.
struct S_ModelState {
    I3D_animation_set *anims[8] = {};
    int times[8] = {};
    bool active[8] = {};
};
std::map<const void *, S_ModelState> g_models;
void __stdcall F_SetAnim(I3D_model *m, I3D_animation_set *a, int stage, float weight, int flags)
{
    Log("anim %s %s %d %g %d", NameOf(m), a ? a->GetFileName() : "null", stage, weight, flags);
    g_models[m].anims[stage] = a;
}
I3D_animation_set *__stdcall F_GetAnim(I3D_model *m, int stage) { return g_models[m].anims[stage]; }
void __stdcall F_StopAnim(I3D_model *m, int stage) { Log("stop %s %d", NameOf(m), stage); }
void __stdcall F_SetAnimTime(I3D_model *m, int stage, int time) { Log("time %s %d %d", NameOf(m), stage, time); }
int __stdcall F_GetAnimTime(I3D_model *m, int stage) { return g_models[m].times[stage]; }
bool __stdcall F_AnimActive(I3D_model *m, int stage) { return g_models[m].active[stage]; }
// Sounds.
void __stdcall F_SetVolume(I3D_sound *s, float v) { Log("volume %s %g", NameOf(s), v); }
void __stdcall F_Play(I3D_sound *s, int position) { Log("play %s %d", NameOf(s), position); }
int __stdcall F_GetPosition(I3D_sound *) { return 1234; }
void __stdcall F_SetActive(I3D_sound *s, bool on, bool immediate) { Log("active %s %d %d", NameOf(s), on, immediate); }
void __stdcall F_SetVelocity(I3D_sound *s, const S_vector &v) { Log("velocity %s %g %g %g", NameOf(s), v.x, v.y, v.z); }

void *g_frameVt[64];
bool g_vtReady;
void SetUpVt()
{
    if (g_vtReady)
        return;
    g_vtReady = true;
    const auto at = [](int offset, void *f) { g_frameVt[offset / 4] = f; };
    at(0x00, reinterpret_cast<void *>(F_Release));
    at(0x04, reinterpret_cast<void *>(F_VisualType));
    at(0x20, reinterpret_cast<void *>(F_Tick));
    at(0x24, reinterpret_cast<void *>(F_Update));
    at(0x30, reinterpret_cast<void *>(F_SetOn));
    at(0x38, reinterpret_cast<void *>(F_LinkTo));
    at(0x84, reinterpret_cast<void *>(F_SetAnim));        // (models)
    at(0x88, reinterpret_cast<void *>(F_GetAnim));
    at(0x9C, reinterpret_cast<void *>(F_SetAnimTime));
    at(0xA0, reinterpret_cast<void *>(F_GetAnimTime));
}
// Tables by kind: models and sounds use the same slots differently.
void *g_modelVt[64], *g_soundVt[64];
void BuildKindTables()
{
    SetUpVt();
    std::memcpy(g_modelVt, g_frameVt, sizeof g_frameVt);
    std::memcpy(g_soundVt, g_frameVt, sizeof g_frameVt);
    g_modelVt[0x8C / 4] = reinterpret_cast<void *>(F_StopAnim);
    g_modelVt[0xAC / 4] = reinterpret_cast<void *>(F_AnimActive);
    g_soundVt[0x84 / 4] = nullptr;
    g_soundVt[0x88 / 4] = nullptr;
    g_soundVt[0x9C / 4] = nullptr;
    g_soundVt[0xA0 / 4] = nullptr;
    g_soundVt[0x8C / 4] = reinterpret_cast<void *>(F_SetVolume);
    g_soundVt[0x94 / 4] = reinterpret_cast<void *>(F_Play);
    g_soundVt[0xA4 / 4] = reinterpret_cast<void *>(F_GetPosition);
    g_soundVt[0xAC / 4] = reinterpret_cast<void *>(F_SetActive);
    g_soundVt[0xB0 / 4] = reinterpret_cast<void *>(F_SetVelocity);
}

std::map<std::string, I3D_frame *> g_sceneFrames, g_driverFrames;

I3D_frame *NewFrame(const char *name, E_I3D_FRAME_TYPE type, bool inScene = true)
{
    BuildKindTables();
    auto *buf = static_cast<char *>(_aligned_malloc(0x400, 16));
    std::memset(buf, 0, 0x400);
    auto *f = reinterpret_cast<I3D_frame *>(buf);
    *reinterpret_cast<void ***>(buf) = type == FRAME_MODEL ? g_modelVt : type == FRAME_SOUND ? g_soundVt : g_frameVt;
    f->m_name = _strdup(name);
    f->m_type = type;
    f->m_refCount = 2;
    f->m_frameFlags = 0x28;                             // (the matrices valid, no rebuild of the quaternion)
    (inScene ? g_sceneFrames : g_driverFrames)[name] = f;
    return f;
}

// ---- the scene, the driver ----------------------------------------------------------------------------------------------

uint32_t g_weather[16];
void *g_sceneVt[128], *g_driverVt[64];
struct S_FakeScene {
    void **vtable;
};
S_FakeScene g_scene;
struct S_FakeDriver {
    void **vtable;
};
S_FakeDriver g_driver;
std::vector<uint32_t> g_findMasks;
I3D_frame *__stdcall F_FindFrame(S_FakeScene *, const char *name, uint32_t mask)
{
    g_findMasks.push_back(mask);
    const auto it = g_sceneFrames.find(name);
    return it == g_sceneFrames.end() ? nullptr : it->second;
}
void __stdcall F_SetWeather(S_FakeScene *, int param, uint32_t value)
{
    Log("weather %d %u", param, value);
    g_weather[param] = value;
}
uint32_t __stdcall F_GetWeather(S_FakeScene *, int param) { return g_weather[param]; }
I3D_frame *__stdcall F_DriverFind(S_FakeDriver *, S_FakeScene *, const char *name)
{
    const auto it = g_driverFrames.find(name);
    return it == g_driverFrames.end() ? nullptr : it->second;
}
alignas(16) unsigned char g_gameStorage[0x4000];
}  // namespace

C_game *GetGame() { return reinterpret_cast<C_game *>(g_gameStorage); }
I3D_driver *I3DGetDriver() { return reinterpret_cast<I3D_driver *>(&g_driver); }

// ---- the module's other members ------------------------------------------------------------------------------------------

alignas(16) unsigned char g_scriptModuleStorage[sizeof(C_ScriptModule)];
int g_scriptMessageTypeBits = 9;
std::vector<S_PendingSound *> g_pendingSounds;

C_Module::C_Module(int, const char *) {}
C_Module::~C_Module() {}
C_ScriptModule::C_ScriptModule(int id, const char *name) : C_Module(id, name) {}
C_ScriptModule::~C_ScriptModule() {}
void C_ScriptModule::Tick(int) {}
void C_ScriptModule::Update(int) {}
void C_ScriptModule::FreeMissionData(bool) {}
void C_ScriptModule::StartMission(bool) {}
void C_ScriptModule::StopMission(bool) {}
void C_ScriptModule::RestartMission(bool) {}
void C_ScriptModule::RememberFrame(I3D_frame *frame, uint32_t what) { Log("remember %s %u", NameOf(frame), what); }
bool C_ScriptModule::RegisterAnimatedModel(I3D_frame *model) { Log("register %s", NameOf(model)); return true; }
void C_ScriptModule::RotateFrame(I3D_frame *frame, int axis, int angle, int speed) { Log("rotate %s %d %d %d", NameOf(frame), axis, angle, speed); }
void C_ScriptModule::AddPauseSound(const char *name) { Log("pause sound %s", name); }
bool C_ScriptModule::WriteSaveGameValues(C_chunk &chunk) { return chunk.Write(m_saveGameValues, sizeof m_saveGameValues); }
bool C_ScriptModule::ReadSaveGameValues(C_chunk &chunk) { return chunk.Read(m_saveGameValues, sizeof m_saveGameValues); }
void S_RememberedFrame::Restore() { Log("restore %s", NameOf(frame)); }

// ---- scripts ------------------------------------------------------------------------------------------------------------

namespace {
void __thiscall F_ScriptDelete(C_Script *s) { Log("delete script %s", NameOf(s->m_frame)); }
void *g_scriptVt[8] = {reinterpret_cast<void *>(F_ScriptDelete), reinterpret_cast<void *>(F_ScriptDelete)};
bool g_scriptLoadResult = true;
bool g_assignGivesScript = true;
C_Script *NewScript(I3D_frame *frame, const char *file)
{
    auto *buf = static_cast<char *>(_aligned_malloc(sizeof(C_Script), 16));
    std::memset(buf, 0, sizeof(C_Script));
    auto *s = reinterpret_cast<C_Script *>(buf);
    *reinterpret_cast<void ***>(buf) = g_scriptVt;
    s->m_frame = frame;
    new (&s->m_fileName) C_str(file);
    return s;
}
S_FrameData g_scriptData;
}  // namespace

bool C_Script::SaveGame(C_chunk &chunk) const
{
    Log("save script %s", NameOf(m_frame));
    return chunk.Write("STATE", 5);
}
bool C_Script::LoadGame(C_chunk &chunk)
{
    char state[6] = {};
    chunk.Read(state, 5);
    Log("load script %s %s", NameOf(m_frame), state);
    return g_scriptLoadResult;
}
void C_Script::OnExit() { Log("exit script %s", NameOf(m_frame)); }
bool C_ScriptModule::AssignScript(I3D_frame *frame, const char *name)
{
    Log("assign %s %s", NameOf(frame), name);
    if (g_assignGivesScript) {
        g_scriptData.script = NewScript(frame, name);
        frame->m_data = &g_scriptData;
    }
    return true;
}

// ---- the rest of the game -----------------------------------------------------------------------------------------------

namespace {
void __thiscall F_SetRain(void *, bool on) { Log("rain %d", on); }
void __thiscall F_MpLightning(void *, int t) { Log("lightning %d", t); }
void __thiscall F_SetHierarchyOn(void *, I3D_frame *f, bool on, uint32_t mask) { Log("hierarchy %s %d 0x%x", NameOf(f), on, mask); }
I3D_frame *__thiscall F_CreateShadow(void *, I3D_frame *f) { Log("create shadow %s", NameOf(f)); return nullptr; }
I3D_frame *g_shadow;
I3D_frame *__thiscall F_GetShadow(void *, I3D_frame *f) { Log("get shadow %s", NameOf(f)); return g_shadow; }
// (g++ has two destructor slots: offset / 4 + 1.)
void *g_effectsVt[64];
struct S_FakeEffects {
    void **vtable;
} g_effects;
}  // namespace
C_GEffectsModule *GetGEffects()
{
    const auto at = [](int offset, void *f) { g_effectsVt[offset / 4 + 1] = f; };
    at(0x40, reinterpret_cast<void *>(F_SetRain));
    at(0x48, reinterpret_cast<void *>(F_MpLightning));
    at(0x58, reinterpret_cast<void *>(F_SetHierarchyOn));
    at(0x78, reinterpret_cast<void *>(F_CreateShadow));
    at(0x7C, reinterpret_cast<void *>(F_GetShadow));
    g_effects.vtable = g_effectsVt;
    return reinterpret_cast<C_GEffectsModule *>(&g_effects);
}

template <>
HRESULT C_ResourceCache<I3D_animation_set>::Get(const char *name, I3D_animation_set **out, T_CacheErrorProc, void *)
{
    Log("cache %s", name);
    extern HRESULT TestCacheGet(const char *, I3D_animation_set **);
    return TestCacheGet(name, out);
}

void C_QuickMessages::ShowSubtitles(bool on) { Log("subtitles %d", on); }
void C_QuickMessages::SetSubtitle(int text) { Log("subtitle %d", text); }
void C_HumanVisual::RemoveShadow() { Log("human shadow off"); }
void C_HumanMovement::UpdateModelNames() { Log("human names"); }
void C_HumanMovement::PlayDubbing(int dubbing, I3D_frame *frame, C_Human *owner, float nearRange, float farRange)
{
    Log("dubbing %d %s %p %g %g", dubbing, NameOf(frame), static_cast<void *>(owner), nearRange, farRange);
}
void C_AnimationActor::SetCollisionsOn(bool on) { Log("collisions %d", on); }
C_Human *C_Team::GetActivePlayer() const { extern C_Human *g_activePlayer; return g_activePlayer; }
void C_Team::UpdateCameraTarget() { Log("camera target"); }
C_LightMapGroupsModule *GetLightMapGroups()
{
    extern C_LightMapGroupsModule *g_groups;
    return g_groups;
}
void C_LightMapGroupsModule::SetFrameLightmap(I3D_frame *frame, int lightmap) { Log("frame lightmap %s %d", NameOf(frame), lightmap); }
void C_LightMapGroupsModule::SetGroupLightmap(const S_LightmapGroup *, int lightmap) { Log("group lightmap %d", lightmap); }
int C_ParticlesModule::Create(int effect, const S_matrix &, I3D_frame *parent, bool faceCamera, bool transient)
{
    Log("particle %d %s %d %d", effect, NameOf(parent), faceCamera, transient);
    return 0;
}

// (The globals by raw storage: script_module_globals.cpp.)
void I3D_frame::UpdateWMatrixProc() { m_frameFlags |= FRMFLAGS_WMATRIX_VALID; }
C_Human *g_activePlayer;
C_LightMapGroupsModule *g_groups;
extern bool g_groupFound;
S_LightmapGroup *C_LightMapGroupsModule::FindGroup(const char *) const { return g_groupFound ? reinterpret_cast<S_LightmapGroup *>(const_cast<C_LightMapGroupsModule *>(this)) : nullptr; }
extern int g_rotMade;
void __stdcall S_quat::Make(const S_matrix &) { ++g_rotMade; }
void __stdcall S_quat::Normalize()
{
    const float n = std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z + s * s);
    v.x /= n;
    v.y /= n;
    v.z /= n;
    s /= n;
}
static std::map<std::string, I3D_animation_set *> g_cacheAnims;
HRESULT TestCacheGet(const char *name, I3D_animation_set **out)
{
    const auto it = g_cacheAnims.find(name);
    if (it == g_cacheAnims.end())
        return static_cast<HRESULT>(0x80000001);
    *out = it->second;
    return I3D_OK;
}

#include <functional>
int g_rotMade;
bool g_groupFound;
#include "script_module_tests.inc"

int main()
{
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    g_scene.vtable = g_sceneVt;
    g_sceneVt[0x7C / 4] = reinterpret_cast<void *>(F_FindFrame);
    g_sceneVt[0x11C / 4] = reinterpret_cast<void *>(F_SetWeather);
    g_sceneVt[0x120 / 4] = reinterpret_cast<void *>(F_GetWeather);
    g_driver.vtable = g_driverVt;
    g_driverVt[0x88 / 4] = reinterpret_cast<void *>(F_DriverFind);
    GetGame()->m_scene = reinterpret_cast<I3D_scene *>(&g_scene);
    new (g_scriptModuleStorage) C_ScriptModule(MODULE_SCRIPT, "Script");
    RunTests();
    std::printf("script_module: %d checks, %d failures\n", checks, failures);
    return failures != 0;
}
