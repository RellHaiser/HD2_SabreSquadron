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

std::map<int, FILE *> g_open;
int g_nextHandle = 1;
}  // namespace

// ---- files (the test's own collision files, on disk)
int RW_DATA_CALL dtaOpen(const char *name, bool)
{
    FILE *f = std::fopen(name, "rb");
    if (!f)
        return DTA_INVALID_HANDLE;
    g_open[g_nextHandle] = f;
    return g_nextHandle++;
}
bool RW_DATA_CALL dtaClose(int handle)
{
    if (!g_open.count(handle))
        return false;
    std::fclose(g_open[handle]);
    g_open.erase(handle);
    return true;
}
int RW_DATA_CALL dtaSeek(int handle, int offset, int origin)
{
    FILE *f = g_open[handle];
    std::fseek(f, offset, origin == 0 ? SEEK_SET : origin == 1 ? SEEK_CUR : SEEK_END);
    return static_cast<int>(std::ftell(f));
}
int RW_DATA_CALL dtaRead(int handle, void *data, int size) { return static_cast<int>(std::fread(data, 1, size, g_open[handle])); }
// (The module's chunk code links them; nothing here writes files.)
int RW_DATA_CALL dtaOpenWrite(const char *, int) { return DTA_INVALID_HANDLE; }
int RW_DATA_CALL dtaWrite(int, const void *, int) { return 0; }

namespace {
std::set<std::string> g_tempFiles;
std::string TempFile(const char *name)
{
    const char *temp = std::getenv("TEMP");
    const std::string path = std::string(temp ? temp : ".") + "\\phys_" + std::to_string(GetCurrentProcessId()) + "_" + name;
    g_tempFiles.insert(path);
    return path;
}

// ---- engine frames
// A fake frame: the engine's layout (I3D_frame's fields; visuals' local bound at +0x1A0, flags at +0x1D0), a vtable
// of logging fakes.
struct S_FakeFrame {
    alignas(16) uint8_t bytes[0x400];
    std::string name, props, fileName;
    int setPos = 0, setOn = 0, links = 0, removedFromModel = 0;
};
std::vector<S_FakeFrame *> g_fakes;
std::map<const void *, S_FakeFrame *> g_fakeOf;
void *g_frameVt[64];
I3D_frame *AsFrame(S_FakeFrame *f) { return reinterpret_cast<I3D_frame *>(f->bytes); }
S_FakeFrame *Fake(const I3D_frame *frame) { return g_fakeOf.count(frame) ? g_fakeOf[frame] : nullptr; }

// I3D_frame::UpdateWMatrixProc: LS3DF.dll's own (the original's frames go through it too), else my own.
typedef void(__attribute__((thiscall)) * T_UpdateWMatrix)(I3D_frame *);
T_UpdateWMatrix g_ls3dfUpdateWMatrix;
}  // namespace

void I3D_frame::UpdateWMatrixProc()
{
    if (g_ls3dfUpdateWMatrix) {
        g_ls3dfUpdateWMatrix(this);
        return;
    }
    const S_matrix &local = GetLocalMatrix();
    m_worldMatrix = m_parent ? local * m_parent->GetMatrix() : local;
    m_frameFlags |= FRMFLAGS_WMATRIX_VALID;
}

namespace {
void Unlink(I3D_frame *frame)
{
    if (I3D_frame *parent = frame->m_parent) {
        I3D_frame **link = &parent->m_firstChild;
        while (*link && *link != frame)
            link = &(*link)->m_nextSibling;
        if (*link)
            *link = frame->m_nextSibling;
    }
    frame->m_parent = nullptr;
    frame->m_nextSibling = nullptr;
}
void LinkUnder(I3D_frame *frame, I3D_frame *parent)
{
    Unlink(frame);
    frame->m_parent = parent;
    if (parent) {
        I3D_frame **link = &parent->m_firstChild;
        while (*link)
            link = &(*link)->m_nextSibling;
        *link = frame;
    }
}
void UpdateTree(I3D_frame *frame)
{
    frame->m_frameFlags &= ~FRMFLAGS_WMATRIX_VALID;
    frame->UpdateWMatrixProc();
    for (I3D_frame *c = frame->m_firstChild; c; c = c->m_nextSibling)
        UpdateTree(c);
}

uint32_t __stdcall F_Release(I3D_frame *self) { return static_cast<uint32_t>(--self->m_refCount); }
uint32_t __stdcall F_GetVisualType(I3D_frame *) { return 0; }
void __stdcall F_SetPos(I3D_frame *self, const S_vector &pos)
{
    ++Fake(self)->setPos;
    self->SetPosInline(pos);
}
void __stdcall F_SetDir(I3D_frame *self, const S_vector &dir, float)
{
    self->SetLocalDir3(dir, S_vector(0, 1, 0));
}
void __stdcall F_Update(I3D_frame *self) { UpdateTree(self); }
void __stdcall F_SetOn(I3D_frame *self, bool on)
{
    ++Fake(self)->setOn;
    self->m_frameFlags = on ? self->m_frameFlags | 1u : self->m_frameFlags & ~1u;
}
void __stdcall F_SetName(I3D_frame *self, const char *name) { Fake(self)->name = name ? name : ""; self->m_name = Fake(self)->name.c_str(); }
void __stdcall F_LinkTo(I3D_frame *self, I3D_frame *parent, uint32_t flags)
{
    ++Fake(self)->links;
    if (flags & I3DLINK_KEEP_WORLD) {
        const S_matrix world = self->GetMatrix();
        S_matrix local = world;
        if (parent) {
            S_matrix inv;
            inv.Inverse(parent->GetMatrix());
            local = world * inv;
        }
        self->SetLocalMatrix(local);
    }
    if (self->m_parent != parent)
        LinkUnder(self, parent);
    self->m_frameFlags &= ~FRMFLAGS_WMATRIX_VALID;
}
int __stdcall F_NumChildren(I3D_frame *self)
{
    int n = 0;
    for (I3D_frame *c = self->m_firstChild; c; c = c->m_nextSibling)
        ++n;
    return n;
}
I3D_frame *__stdcall F_GetChild(I3D_frame *self, int index)
{
    for (I3D_frame *c = self->m_firstChild; c; c = c->m_nextSibling)
        if (index-- == 0)
            return c;
    return nullptr;
}
void EnumTree(I3D_frame *frame, I3D_ENUMPROC *callback, void *context, uint32_t typeMask)
{
    // (The children as they were: the callback may relink them.)
    std::vector<I3D_frame *> children;
    for (I3D_frame *c = frame->m_firstChild; c; c = c->m_nextSibling)
        children.push_back(c);
    for (I3D_frame *c : children) {
        if (typeMask & (1u << (c->m_type - 1)))
            callback(c, context);
        EnumTree(c, callback, context, typeMask);
    }
}
void __stdcall F_EnumFrames(I3D_frame *self, I3D_ENUMPROC *callback, void *context, uint32_t typeMask, const char *)
{
    EnumTree(self, callback, context, typeMask);
}
I3D_frame *FindIn(I3D_frame *frame, const char *name)
{
    for (I3D_frame *c = frame->m_firstChild; c; c = c->m_nextSibling) {
        if (_stricmp(c->GetShortName(), name) == 0)
            return c;
        if (I3D_frame *found = FindIn(c, name))
            return found;
    }
    return nullptr;
}
I3D_frame *__stdcall F_FindChildFrame(I3D_frame *self, const char *name, uint32_t) { return FindIn(self, name); }
void __stdcall F_UpdateBound(I3D_frame *self) { self->m_frameFlags |= FRMFLAGS_BOUND_VALID; }
void __stdcall F_UpdateLocalBound(I3D_frame *self) { reinterpret_cast<I3D_visual *>(self)->VisualFlags() |= VISFLAGS_BOUND_VALID; }
void __stdcall F_RemoveModelFrame(I3D_frame *, I3D_frame *frame) { ++Fake(frame)->removedFromModel; }

// A frame of `type` named `name` at `pos` turned by `rot`, its bound `box` (visuals: also their own bound).
I3D_frame *NewFrame(E_I3D_FRAME_TYPE type, const std::string &name, const S_vector &pos, const S_quat &rot, const I3D_bbox &box,
                    const std::string &props = "", I3D_frame *parent = nullptr)
{
    auto *f = new S_FakeFrame();
    std::memset(f->bytes, 0, sizeof f->bytes);
    g_fakes.push_back(f);
    I3D_frame *frame = AsFrame(f);
    g_fakeOf[frame] = f;
    *reinterpret_cast<void ***>(frame) = g_frameVt;
    f->name = name;
    f->props = props;
    frame->m_name = f->name.c_str();
    frame->m_nameOffset = 0;
    frame->m_userProps = f->props.empty() ? nullptr : f->props.c_str();
    frame->m_type = type;
    frame->m_refCount = 1;
    frame->m_scale = S_vector(1, 1, 1);
    frame->m_localMatrix.Identity();
    frame->m_worldMatrix.Identity();
    frame->m_rot = rot;
    frame->m_localMatrix.m[3][0] = pos.x;
    frame->m_localMatrix.m[3][1] = pos.y;
    frame->m_localMatrix.m[3][2] = pos.z;
    frame->m_frameFlags = 1u | FRMFLAGS_ROT_DIRTY | FRMFLAGS_BOUND_VALID;   // (on; the local matrix built from m_rot)
    I3D_bound_volume bound = {};
    bound.bbox = box;
    bound.center = (box.min + box.max) * 0.5f;
    bound.radius = (box.max - box.min).Magnitude() * 0.5f;
    frame->m_bound = bound;
    if (type == FRAME_VISUAL) {
        auto *visual = reinterpret_cast<I3D_visual *>(frame);
        visual->Field<I3D_bound_volume>(0x1A0) = bound;
        visual->VisualFlags() |= VISFLAGS_BOUND_VALID;
    }
    LinkUnder(frame, parent);
    UpdateTree(frame);
    return frame;
}
I3D_bbox Box(const S_vector &half)
{
    I3D_bbox b;
    b.min = -half;
    b.max = half;
    return b;
}
S_quat Identity()
{
    S_quat q;
    q.Identity();
    return q;
}
S_quat YawQuat(float yaw)
{
    S_quat q;
    q.Make(S_vector(0, 1, 0), yaw);
    return q;
}

// ---- the scene (its primary sector a frame), the driver, the game
alignas(16) uint8_t g_sceneBytes[0x400];
void *g_sceneVt[96];
I3D_frame *g_sector;
std::vector<I3D_frame *> g_sceneAdded, g_sceneRemoved;
std::map<std::pair<const I3D_frame *, int>, int> g_sectorUpdates;
void __stdcall F_SceneEnumFrames(void *, I3D_ENUMPROC *callback, void *context, uint32_t typeMask, const char *)
{
    EnumTree(g_sector, callback, context, typeMask);
}
void __stdcall F_SceneAddFrame(void *, I3D_frame *frame)
{
    g_sceneAdded.push_back(frame);
    if (!frame->m_parent)
        LinkUnder(frame, g_sector);
}
int __stdcall F_SceneRemoveFrame(void *, I3D_frame *frame)
{
    g_sceneRemoved.push_back(frame);
    return frame->m_parent == g_sector || !frame->m_parent ? 1 : 0;
}
void __stdcall F_UpdateFrameSector(void *, I3D_frame *frame, const S_vector &) { ++g_sectorUpdates[{frame, 0}]; }

alignas(16) uint8_t g_driverBytes[0x100];
void *g_driverVt[64];
std::vector<I3D_frame *> g_created;
I3D_frame *__stdcall F_CreateFrame(void *, int type)
{
    I3D_frame *frame = NewFrame(static_cast<E_I3D_FRAME_TYPE>(type), "", S_vector(0, 0, 0), Identity(), Box(S_vector(0.1f, 0.1f, 0.1f)));
    g_created.push_back(frame);
    return frame;
}
I3D_frame *__stdcall F_DriverFindFrame(void *, void *, const char *) { return nullptr; }   // (the levels' frames: none)
const I3D_bbox *__stdcall F_GetHierarchyBBox(void *, I3D_bbox &out, I3D_frame *frame)
{
    out = frame->m_bound.bbox;
    return &out;
}
alignas(16) uint8_t g_gameBytes[sizeof(C_game) + 0x100];
}  // namespace

C_game *GetGame() { return reinterpret_cast<C_game *>(g_gameBytes); }
extern "C" I3D_driver *I3DGetDriver() { return reinterpret_cast<I3D_driver *>(g_driverBytes); }
C_GEffectsModule *GetGEffects() { return nullptr; }
uint32_t C_Human::GetStanceFlags() const { return 0; }
void SetFrameData(I3D_frame *frame, const S_FrameData &data)
{
    if (frame->GetData()) {
        *frame->GetData() = data;
        return;
    }
    frame->SetData(new S_FrameData(data));
}

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
    // LS3DF.dll's own world matrices (loaded with the original executable, or on its own).
    HMODULE ls3df = GetModuleHandleA("LS3DF.dll");
    if (!ls3df) {
        const char *dir = std::getenv("HD2_GAME_DIR");
        const std::string game = dir && *dir ? dir : "H:\\HD2_Unmodified";
        SetDllDirectoryA(game.c_str());                                  // (its own DLLs beside it)
        ls3df = LoadLibraryA((game + "\\LS3DF.dll").c_str());
        _controlfp(0x00020000u, 0x00030000u);
    }
    if (ls3df)
        g_ls3dfUpdateWMatrix = reinterpret_cast<T_UpdateWMatrix>(reinterpret_cast<void *>(GetProcAddress(ls3df, "?UpdateWMatrixProc@I3D_frame@@AAEXXZ")));
    if (!g_ls3dfUpdateWMatrix)
        std::printf("  (no LS3DF.dll: frames' world matrices are my own)\n");

    g_frameVt[0x00 / 4] = reinterpret_cast<void *>(F_Release);
    g_frameVt[0x04 / 4] = reinterpret_cast<void *>(F_GetVisualType);
    g_frameVt[0x08 / 4] = reinterpret_cast<void *>(F_SetPos);
    g_frameVt[0x0C / 4] = reinterpret_cast<void *>(F_SetDir);
    g_frameVt[0x24 / 4] = reinterpret_cast<void *>(F_Update);
    g_frameVt[0x30 / 4] = reinterpret_cast<void *>(F_SetOn);
    g_frameVt[0x34 / 4] = reinterpret_cast<void *>(F_SetName);
    g_frameVt[0x38 / 4] = reinterpret_cast<void *>(F_LinkTo);
    g_frameVt[0x3C / 4] = reinterpret_cast<void *>(F_NumChildren);
    g_frameVt[0x40 / 4] = reinterpret_cast<void *>(F_GetChild);
    g_frameVt[0x44 / 4] = reinterpret_cast<void *>(F_EnumFrames);
    g_frameVt[0x48 / 4] = reinterpret_cast<void *>(F_FindChildFrame);
    g_frameVt[0x58 / 4] = reinterpret_cast<void *>(F_UpdateBound);
    g_frameVt[0x80 / 4] = reinterpret_cast<void *>(F_RemoveModelFrame);
    g_frameVt[0x90 / 4] = reinterpret_cast<void *>(F_UpdateLocalBound);
    *reinterpret_cast<void ***>(g_sceneBytes) = g_sceneVt;
    g_sceneVt[0x44 / 4] = reinterpret_cast<void *>(F_SceneEnumFrames);
    g_sceneVt[0x80 / 4] = reinterpret_cast<void *>(F_SceneAddFrame);
    g_sceneVt[0x84 / 4] = reinterpret_cast<void *>(F_SceneRemoveFrame);
    g_sceneVt[0xEC / 4] = reinterpret_cast<void *>(F_UpdateFrameSector);
    *reinterpret_cast<void ***>(g_driverBytes) = g_driverVt;
    g_driverVt[0x54 / 4] = reinterpret_cast<void *>(F_CreateFrame);
    g_driverVt[0xAC / 4] = reinterpret_cast<void *>(F_GetHierarchyBBox);
    g_driverVt[0x88 / 4] = reinterpret_cast<void *>(F_DriverFindFrame);
    g_sector = NewFrame(FRAME_SECTOR, "primary sector", S_vector(0, 0, 0), Identity(), Box(S_vector(100, 100, 100)));
    *reinterpret_cast<I3D_frame **>(g_sceneBytes + 0x1F8) = g_sector;
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
