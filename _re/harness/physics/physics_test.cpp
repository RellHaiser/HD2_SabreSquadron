// The physics objects: game/physics.cpp, physics_box.cpp and physics_door.cpp linked as they are, with the real
// collision world (game/collision*.cpp) on ground of my own (collision/synth_level.inc) and the LS3DF port's math.
// MinGW; build.bat; run physics_test.exe ("physics: N checks, 0 failures").
//
// Engine objects are fakes with the engine's layout: frames (their vtable slots logged; world matrices from
// LS3DF.dll's own I3D_frame::UpdateWMatrixProc when it is there, HD2_GAME_DIR), the scene, the driver; the model
// cache and the breakable list are logged. The game's effects module is absent (falling debris do not fade).
//
// Part 1 (the module, the objects' life): CreateObject for every type, the handles and the active / resting lists
// (checked after every step of random operation sequences), the server's and the client's steps by type, coming to
// rest (the callback, deleting), waking by a push or a hit, the mission start / stop / restart, the scene's "phobj"
// frames and their user properties, the opening doors sent to joining clients and read back, the volumes and the
// frames' sync. Checked against my own reading of the rules.
// Part 2 (motion_test.inc): every object's step against the original executable's.
#include <windows.h>

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <map>
#include <random>
#include <set>
#include <string>
#include <vector>

#define private public
#define protected public
#include "game/actors_module.h"
#include "game/collision.h"
#include "game/collision_internal.h"
#include "game/game.h"
#include "game/geffects.h"
#include "game/human.h"
#include "game/pending.h"
#include "game/physics.h"
#include "game/physics_internal.h"
#include "game/resource_cache.h"
#include "ls3d/i3d.h"
#include "ls3d/rw_data.h"
#include "net/bitstream.h"
#include "original_exe.h"
#undef private
#undef protected

extern "C" unsigned int __cdecl _controlfp(unsigned int, unsigned int);

namespace {
int checks, failures;
#define CHECK(cond)                                                                 \
    do {                                                                            \
        ++checks;                                                                   \
        if (!(cond)) {                                                              \
            ++failures;                                                             \
            if (failures < 60)                                                      \
                std::printf("FAIL %s:%d: %s\n", __func__, __LINE__, #cond);         \
        }                                                                           \
    } while (0)

#ifdef PHYSICS_QUICK
constexpr int QUICK = 4;
#else
constexpr int QUICK = 1;
#endif

}  // namespace

#include "fake_engine.inc"

C_GEffectsModule *GetGEffects() { return nullptr; }
uint32_t C_Human::GetStanceFlags() const { return 0; }

// ---- the breakable fences, the model cache: logged
namespace {
std::vector<S_BreakableObject> g_breakables;
int g_clearBreakables;
// The model files the cache "loads": a builder per file name.
std::map<std::string, std::function<void(I3D_frame *)>> g_modelFiles;
std::vector<std::string> g_opened;
}  // namespace
void C_TemporaryActorsList::AddBreakable(const S_BreakableObject &object) { g_breakables.push_back(object); }
void C_TemporaryActorsList::ClearBreakables() { ++g_clearBreakables; }
template <> HRESULT C_ResourceCache<I3D_model>::OpenInto(I3D_model *object, const char *fileName, uint32_t, void *, void *, int)
{
    g_opened.push_back(fileName);
    auto it = g_modelFiles.find(fileName);
    if (it == g_modelFiles.end())
        return -1;
    it->second(object);
    return I3D_OK;
}

namespace {
// ---- the world: ground of my own
#include "synth_level.inc"

// Ground: a floor at y 0 over 64 x 64 m, a wall along z at x 40 (3 m), a block (1 m), a ramp.
void LoadGround()
{
    S_Synth s;
    s.withRect = false;
    s.originX = 0;
    s.originZ = 0;
    for (int x = 0; x < 64; x += 8)
        for (int z = 0; z < 64; z += 8)
            AddQuad(s, S_vector(x, 1, z), S_vector(x + 8, 1, z), S_vector(x + 8, 1, z + 8), S_vector(x, 1, z + 8), S_vector(0, 1, 0));
    AddBlock(s, S_vector(40, 1, 4), S_vector(40.5f, 4, 60));
    AddBlock(s, S_vector(20, 1, 20), S_vector(24, 2, 24));
    AddRamp(s, 8, 40, 8, 6, 3);
    const std::string path = TempFile("ground.klz");
    WriteSynth(s, path);
    g_collisionWorld.Load(path.c_str(), true);
}

// ---- the objects
S_PhysicsObjectDesc Desc(I3D_frame *frame, int type, bool active = false)
{
    S_PhysicsObjectDesc d;
    d.frame = frame;
    d.type = type;
    d.startActive = active;
    d.radius = -1.0f;
    return d;
}
int g_callbackCalls, g_callbackResult;
std::vector<void *> g_callbackContexts;
std::vector<int> g_callbackKinds;
std::vector<S_PhysicsHandle *> g_callbackHandles;
// (The original calls it __fastcall: the kind in ECX, the handle in EDX, the context on the stack, popped here.)
int __fastcall Callback(int kind, S_PhysicsHandle *handle, void *context)
{
    ++g_callbackCalls;
    g_callbackContexts.push_back(context);
    g_callbackKinds.push_back(kind);
    g_callbackHandles.push_back(handle);
    return g_callbackResult;
}
C_PhysObject *ObjectOf(T_PhysicsObject h)
{
    if (!h || h == g_noPhysicsObject || h->index < 0)
        return nullptr;
    const auto &list = h->active ? g_physicsModule.m_active : g_physicsModule.m_resting;
    return h->index < static_cast<int>(list.size()) ? list[h->index] : nullptr;
}
// Every live handle points at its object and every object at its handle; every frame's data names its handle.
bool ListsConsistent()
{
    bool ok = true;
    for (int a = 0; a < 2; ++a) {
        const auto &list = a ? g_physicsModule.m_active : g_physicsModule.m_resting;
        for (size_t i = 0; i < list.size(); ++i)
            if (C_PhysObject *o = list[i]) {
                S_PhysicsHandle *h = o->GetHandle();
                ok = ok && h && h->index == static_cast<int>(i) && h->active == (a == 1);
                ok = ok && (!o->GetFrame() || !o->GetFrame()->GetData() || o->GetFrame()->GetData()->physicsObject == h);
            }
    }
    for (S_PhysicsHandle *h : g_physicsModule.m_handles)
        ok = ok && h && (h->index == -1 || ObjectOf(h));
    return ok;
}
bool InWorld(const S_CollisionVolume &v) { return v.cell != nullptr; }
}  // namespace

#include "module_test.inc"
#include "motion_test.inc"
#include "break_test.inc"
#include "ctor_test.inc"

int main(int argc, char **argv)
{
    int childExit;
    if (RunReservingOriginalBase(childExit))
        return childExit;
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    _controlfp(0x00020000u, 0x00030000u);                       // _PC_24, _MCW_PC: the game's precision
    LoadOriginalExe();
    InstallFakeEngine();
    // The player table the messages quantise with (2 km around the origin in 1 cm steps).
    static S_NetQuantization table[2];
    S_vector4 span, from, step;
    span.x = span.y = span.z = 2000.0f;
    from.x = from.y = from.z = -1000.0f;
    step.x = step.y = step.z = 0.01f;
    span.w = from.w = step.w = 0.0f;
    for (S_NetQuantization &q : table)
        q.Setup(span, from, step, 0);
    g_netPlayerTable = reinterpret_cast<S_NetPlayer *>(table);
    g_netPlayerCount = 2;
    LoadGround();

    const bool motionOnly = argc > 1 && std::strcmp(argv[1], "motion") == 0;
    if (!motionOnly)
        TestModule();
    TestMotion();
    if (!motionOnly)
        TestBreaking();
    TestConstructors();
    g_physicsModule.DeleteAll();
    g_collisionWorld.Unload(false);
    for (const std::string &f : g_tempFiles)
        std::remove(f.c_str());
    std::printf("physics: %d checks, %d failures\n", checks, failures);
    return failures != 0;
}
