// The actor lists: game/actors_module.cpp (C_ActorsModule, C_TemporaryActorsList, the actor messages) linked as it is,
// with net/bitstream.cpp (the real message encoding), common/chunk.cpp and the LS3DF port's math. The actors are raw
// objects with a logging vtable made by fake creators in g_actorCreators; the items module, the humans, the frames,
// collision, the net game and the interface are faked (logged). MinGW; build.bat; run actors_test.exe
// ("actors: N checks, 0 failures").
#include <windows.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <set>
#include <string>
#include <vector>

#define private public
#define protected public
#include "common/chunk.h"
#include "game/actor.h"
#include "game/actors_module.h"
#include "game/collision.h"
#include "game/game.h"
#include "game/human.h"
#include "game/interface.h"
#include "game/item_actor.h"
#include "game/item_defs.h"
#include "game/items.h"
#include "game/items_module.h"
#include "game/mine.h"
#include "game/pending.h"
#include "game/physics.h"
#include "game/profile.h"
#include "game/resource_cache.h"
#include "game/spawn_point.h"
#include "game/wire_cut.h"
#include "ls3d/i3d.h"
#include "ls3d/rw_data.h"
#include "net/bitstream.h"
#include "net/net_game.h"
#undef private
#undef protected

C_BitStream *CreateActorMessage(int id, bool reliable);
C_BitStream *CreateActorMessageEx(int id, bool reliable);
void SendActorMessage(C_BitStream *msg);
C_BitStream *CreateTempActorMessage(int id, bool reliable);
C_BitStream *CreateTempActorCommand(int command, bool reliable);
C_BitStream *CreateTempActorMessage2(int id, bool reliable);
void SendTempActorMessage(C_BitStream *msg);

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
void Log(const std::string &s) { g_log.push_back(s); }
int Count(const std::string &line) { return static_cast<int>(std::count(g_log.begin(), g_log.end(), line)); }
bool Logged(const std::string &prefix)
{
    return std::any_of(g_log.begin(), g_log.end(), [&](const std::string &l) { return l.rfind(prefix, 0) == 0; });
}
int At(const std::string &line)
{
    for (int i = 0; i < static_cast<int>(g_log.size()); ++i)
        if (g_log[i] == line)
            return i;
    return -1;
}
std::vector<std::string> Lines(const std::string &prefix)
{
    std::vector<std::string> out;
    for (const std::string &l : g_log)
        if (l.rfind(prefix, 0) == 0)
            out.push_back(l);
    return out;
}
std::string V(const S_vector &v)
{
    char s[96];
    std::snprintf(s, sizeof s, "%g %g %g", v.x, v.y, v.z);
    return s;
}
template <class F> int VirtualSlot(F f)
{
    union {
        F f;
        struct {
            uintptr_t ptr;
            ptrdiff_t adj;
        } s;
    } u;
    u.f = f;
    return static_cast<int>((u.s.ptr - 1) / sizeof(void *));
}

// ---- files: a name -> contents map (for C_chunk)
std::map<std::string, std::string> g_files;
struct S_Open {
    std::string name;
    size_t at = 0;
};
std::map<int, S_Open> g_open;
int g_nextHandle = 1;
}  // namespace

int RW_DATA_CALL dtaOpen(const char *name, bool)
{
    if (!g_files.count(name))
        return DTA_INVALID_HANDLE;
    g_open[g_nextHandle] = S_Open{name, 0};
    return g_nextHandle++;
}
int RW_DATA_CALL dtaOpenWrite(const char *name, int)
{
    g_files[name].clear();
    g_open[g_nextHandle] = S_Open{name, 0};
    return g_nextHandle++;
}
bool RW_DATA_CALL dtaClose(int handle) { return g_open.erase(handle) != 0; }
int RW_DATA_CALL dtaSeek(int handle, int offset, int origin)
{
    S_Open &o = g_open[handle];
    const std::string &f = g_files[o.name];
    o.at = origin == 0 ? offset : origin == 1 ? o.at + offset : f.size() + offset;
    return static_cast<int>(o.at);
}
int RW_DATA_CALL dtaRead(int handle, void *data, int size)
{
    S_Open &o = g_open[handle];
    const std::string &f = g_files[o.name];
    int got = std::max(0, std::min(static_cast<int>(f.size()) - static_cast<int>(o.at), size));
    std::memcpy(data, f.data() + o.at, got);
    o.at += got;
    return got;
}
int RW_DATA_CALL dtaWrite(int handle, const void *data, int size)
{
    S_Open &o = g_open[handle];
    std::string &f = g_files[o.name];
    if (o.at + size > f.size())
        f.resize(o.at + size);
    std::memcpy(&f[o.at], data, size);
    o.at += size;
    return size;
}

// ============================================================ the game, the modules only named
namespace {
alignas(16) unsigned char g_gameBytes[sizeof(C_game) + 0x100];
alignas(16) unsigned char g_interfaceBytes[0x100];
bool g_coop;
}  // namespace
C_game *GetGame() { return reinterpret_cast<C_game *>(g_gameBytes); }
C_Module::C_Module(int, const char *) {}
C_Module::~C_Module() {}
void C_Interface::OnActorEvent(int e, C_Actor *actor) { Log("interface " + std::to_string(e) + " " + std::to_string(actor->GetNetId())); }
bool C_GameProfile::IsCooperativeGame() const { return g_coop; }
void HumanPass_Tick(int t, const std::vector<C_Actor *> &active)
{
    Log("humanpass " + std::to_string(t) + " " + std::to_string(active.size()));
}
void PeriodicLinks_Tick() { Log("links tick"); }
void PeriodicLinks_Clear() { Log("links clear"); }
bool PeriodicLinks_Save(C_chunk &chunk)
{
    Log("links save");
    chunk.WriteChunk(0x77, 1234);
    return true;
}
bool PeriodicLinks_Load(C_chunk &chunk)
{
    uint16_t id = 0;
    int value = 0;
    chunk.Descend(id);
    chunk.Read(value);
    chunk.Ascend();
    Log("links load " + std::to_string(id) + " " + std::to_string(value));
    return true;
}

// Collision: a ray hits when asked (at g_rayHitPos, normal g_rayHitNormal).
namespace {
bool g_rayHit;
S_vector g_rayHitPos, g_rayHitNormal;
}  // namespace
S_CollisionVolume *C_CollisionWorld::RayTest(const S_vector &from, const S_vector &dir, S_vector *pos, S_vector *normal, uint32_t a,
                                             uint32_t b) const
{
    Log("raytest " + V(from) + " dir " + V(dir) + " " + std::to_string(a) + " " + std::to_string(b));
    if (!g_rayHit)
        return nullptr;
    *pos = g_rayHitPos;
    *normal = g_rayHitNormal;
    static S_CollisionVolume hit;
    return &hit;
}
void C_CollisionWorld::MoveVolume(S_CollisionVolume *volume)
{
    Log("movevolume " + std::to_string(volume->actor ? volume->actor->GetNetId() : -99));
}

// The net game: messages are real C_NetMessages, kept when sent; the player states a raw array.
namespace {
std::vector<C_NetMessage *> g_sent;
alignas(16) unsigned char g_players[64 * sizeof(S_NetPlayerState)];
}  // namespace
C_BitStream *C_NetGame::CreateMessage(int type, int channel)
{
    Log("netmsg " + std::to_string(type) + " " + std::to_string(channel));
    return new C_NetMessage();
}
C_BitStream *C_NetGame::CreateMessageEx(int type, int channel)
{
    Log("netmsgex " + std::to_string(type) + " " + std::to_string(channel));
    return new C_NetMessage();
}
void C_NetGame::SendNetMessage(C_BitStream *msg)
{
    Log("netsend");
    g_sent.push_back(static_cast<C_NetMessage *>(msg));
}
S_NetPlayerState *C_NetGame::LockPlayers()
{
    Log("lockplayers");
    return reinterpret_cast<S_NetPlayerState *>(g_players);
}
void C_NetGame::UnlockPlayers() { Log("unlockplayers"); }
S_NetPlayerState &Player(int i) { return reinterpret_cast<S_NetPlayerState *>(g_players)[i]; }

// ============================================================ frames
namespace {
void *g_frameVt[128];
std::map<const I3D_frame *, std::string> g_frameNames;
std::map<const I3D_frame *, uint32_t> g_visualTypes;
int g_framesReleased;
std::string FrameName(const I3D_frame *f) { return !f ? "null" : g_frameNames.count(f) ? g_frameNames[f] : "?"; }
I3D_frame *NewFrame(int type, const std::string &name = "")
{
    auto *bytes = static_cast<unsigned char *>(::operator new(0x400, std::align_val_t(16)));
    std::memset(bytes, 0, 0x400);
    auto *frame = reinterpret_cast<I3D_frame *>(bytes);
    *reinterpret_cast<void ***>(frame) = g_frameVt;
    frame->m_type = static_cast<E_I3D_FRAME_TYPE>(type);
    frame->m_refCount = 1;
    frame->m_frameFlags |= FRMFLAGS_WMATRIX_VALID;
    if (!name.empty())
        g_frameNames[frame] = name;
    return frame;
}
uint32_t __stdcall F_Release(I3D_frame *self)
{
    ++g_framesReleased;
    Log("release " + FrameName(self));
    return 0;
}
uint32_t __stdcall F_GetVisualType(I3D_frame *self) { return g_visualTypes.count(self) ? g_visualTypes[self] : 0; }
void __stdcall F_SetPos(I3D_frame *self, const S_vector &p)
{
    Log("setpos " + FrameName(self) + " " + V(p));
    self->m_worldMatrix(3) = p;
}
void __stdcall F_SetDir(I3D_frame *self, const S_vector &d, float roll)
{
    char s[32];
    std::snprintf(s, sizeof s, " %g", roll);
    Log("setdir " + FrameName(self) + " " + V(d) + s);
}
void __stdcall F_SetPosDir(I3D_frame *self, const S_vector &p, const S_vector &d, float roll)
{
    char s[32];
    std::snprintf(s, sizeof s, " %g", roll);
    Log("setposdir " + FrameName(self) + " " + V(p) + " dir " + V(d) + s);
    self->m_worldMatrix(3) = p;
}
void __stdcall F_SetWorldDir(I3D_frame *self, const S_vector &d, float roll)
{
    char s[32];
    std::snprintf(s, sizeof s, " %g", roll);
    Log("setworlddir " + FrameName(self) + " " + V(d) + s);
}
void __stdcall F_Update(I3D_frame *self) { Log("update " + FrameName(self)); }
HRESULT __stdcall F_Duplicate(I3D_frame *self, I3D_frame *src)
{
    g_frameNames[self] = "copy of " + FrameName(src);
    Log("duplicate " + FrameName(src));
    return 0;
}
void *g_driverVt[64];
struct {
    void **vt = g_driverVt;
} g_driver;
I3D_frame *__stdcall F_CreateFrame(void *, int type)
{
    Log("createframe " + std::to_string(type));
    return NewFrame(type, "newframe");
}
I3D_frame *__stdcall F_CreateVisual(void *, uint32_t type)
{
    Log("createvisual " + std::to_string(type));
    return NewFrame(FRAME_VISUAL, "newvisual");
}
std::set<std::string> g_models;                         // model files that open
std::map<const I3D_frame *, int> g_frameDataFreed;
}  // namespace
extern "C" I3D_driver *I3DGetDriver() { return reinterpret_cast<I3D_driver *>(&g_driver); }
void I3D_frame::UpdateWMatrixProc() {}
template <> HRESULT C_ResourceCache<I3D_model>::OpenInto(I3D_model *model, const char *name, uint32_t, void *, void *, int)
{
    Log(std::string("openmodel ") + name);
    if (!g_models.count(name))
        return E_FAIL;
    g_frameNames[reinterpret_cast<I3D_frame *>(model)] = name;
    return I3D_OK;
}
void SetFrameData(I3D_frame *frame, const S_FrameData &data)
{
    Log("setframedata " + FrameName(frame));
    auto *copy = new S_FrameData(data);
    frame->SetData(copy);
}
void FreeFrameData(I3D_frame *frame)
{
    Log("freeframedata " + FrameName(frame));
    ++g_frameDataFreed[frame];
    frame->SetData(nullptr);
}

// ============================================================ items (faked: the test's own items and records)
namespace {
std::map<int, C_ItemDef *> g_defs;
std::map<const C_Item *, I3D_frame *> g_itemFrames;
int __attribute__((thiscall)) F_DefBehaviour(C_ItemDef *self) { return static_cast<C_AnimatedItemDef *>(self)->m_behaviour; }
void *g_defVt[64];
// A definition of `type` with `behaviour` and a model file.
C_ItemDef *Def(int type, int behaviour, const char *model)
{
    auto *bytes = static_cast<unsigned char *>(::operator new(sizeof(C_AnimatedItemDef) + 0x40, std::align_val_t(16)));
    std::memset(bytes, 0, sizeof(C_AnimatedItemDef) + 0x40);
    auto *def = reinterpret_cast<C_AnimatedItemDef *>(bytes);
    *reinterpret_cast<void ***>(def) = g_defVt;
    def->m_behaviour = behaviour;
    def->m_modelName = const_cast<char *>(model);
    g_defs[type] = def;
    return def;
}
// An item of `type` (its definition made before) with `uid` and a model frame.
C_Item *Item(int type, int uid)
{
    auto *bytes = static_cast<unsigned char *>(::operator new(0x100, std::align_val_t(16)));
    std::memset(bytes, 0, 0x100);
    auto *item = reinterpret_cast<C_Item *>(bytes);
    item->m_def = g_defs[type];
    item->m_typeId = type;
    item->m_uid = uid;
    g_itemFrames[item] = NewFrame(FRAME_MODEL, "item" + std::to_string(uid));
    return item;
}
std::vector<C_Item *> g_destroyedItems;
bool g_failCreateItem;
}  // namespace
C_ItemDef *GetItemDef(int type) { return g_defs.count(type) ? g_defs[type] : nullptr; }
// (The model frame is frame 0; others are not faked.)
I3D_frame *C_Item::GetFrame(int index) { return index == 0 && g_itemFrames.count(this) ? g_itemFrames[this] : nullptr; }
C_Item *C_ItemsModule::CreateItem(int type, int container, C_Actor *owner, int uid)
{
    Log("createitem " + std::to_string(type) + " " + std::to_string(container) + " " + (owner ? "owner" : "none") + " " + std::to_string(uid));
    return Item(type, uid);
}
void C_ItemsModule::DestroyItem(C_Item *item)
{
    Log("destroyitem " + std::to_string(item->GetUid()));
    g_destroyedItems.push_back(item);
}
void C_ItemsModule::FreePlacements()
{
    Log("freeplacements");
    m_placementCount = 0;
}
// Item records: type, uid, the container number (SaveItem's), as the test writes them.
bool C_ItemsModule::SaveItem(C_Item *item, int container, int slot, int index, C_chunk &chunk)
{
    Log("saveitem " + std::to_string(item->GetUid()) + " " + std::to_string(container) + " " + std::to_string(slot) + " " + std::to_string(index));
    chunk.Write(item->GetTypeId());
    chunk.Write(item->GetUid());
    chunk.Write(container);
    return true;
}
C_Item *C_ItemsModule::LoadItem(int *container, int *slot, int *index, C_chunk &chunk, C_Actor *actor)
{
    int type = 0, uid = 0;
    chunk.Read(type);
    chunk.Read(uid);
    chunk.Read(*container);
    Log("loaditem " + std::to_string(type) + " " + std::to_string(uid) + " " + std::to_string(*container) + " " + std::to_string(*slot) + " " +
        std::to_string(*index) + (actor ? " actor" : " none"));
    return Item(type, uid);
}

// ============================================================ the actors
namespace {
// Every fake actor is this big (the largest of the kinds the lists look into).
constexpr size_t ACTOR_SIZE = std::max({sizeof(C_Human), sizeof(C_ItemActor), sizeof(C_Mine), sizeof(C_SpawnPoint), sizeof(C_WireCut)}) + 0x40;
void *g_actorVt[256];
std::set<C_Actor *> g_live;                             // created, not destroyed
std::vector<C_Actor *> g_destroyed;                     // in the order the creators destroyed them
std::string Id(const C_Actor *a) { return std::to_string(a->GetNetId()); }
// Actors that change the active set while being ticked: actor -> (other, active).
std::map<C_Actor *, std::pair<C_Actor *, bool>> g_tickChanges;
std::map<C_Actor *, C_Actor *> g_tickDestroys;          // temporary actors destroyed while ticked
void __attribute__((thiscall)) F_ActorUpdate(C_Actor *self, int t)
{
    Log("actor update " + Id(self) + " " + std::to_string(t));
    if (g_tickChanges.count(self)) {
        auto [other, on] = g_tickChanges[self];
        (other->IsTemporary() ? static_cast<void>(g_temporaryActors.SetActorActive(other, on)) : g_actorsModule.SetActorActive(other, on));
    }
    if (g_tickDestroys.count(self))
        g_temporaryActors.DestroyActor(g_tickDestroys[self]);
}
void __attribute__((thiscall)) F_ActorTick(C_Actor *self, int t)
{
    Log("actor tick " + Id(self) + " " + std::to_string(t));
    if (g_tickChanges.count(self)) {
        auto [other, on] = g_tickChanges[self];
        (other->IsTemporary() ? static_cast<void>(g_temporaryActors.SetActorActive(other, on)) : g_actorsModule.SetActorActive(other, on));
    }
    if (g_tickDestroys.count(self))
        g_temporaryActors.DestroyActor(g_tickDestroys[self]);
}
void __attribute__((thiscall)) F_ActorMessage(C_Actor *self, C_BitStream *msg)
{
    Log("actor message " + Id(self) + " " + std::to_string(msg->ReadBits(8)));
}
void __attribute__((thiscall)) F_ActorMessage2(C_Actor *self, C_BitStream *msg)
{
    Log("actor message2 " + Id(self) + " " + std::to_string(msg->ReadBits(8)));
}
bool __attribute__((thiscall)) F_ActorSetFrame(C_Actor *self, I3D_frame *frame)
{
    Log("actor setframe " + FrameName(frame));
    self->m_frame = frame;
    return true;
}
void __attribute__((thiscall)) F_ActorStart(C_Actor *self, bool server) { Log("actor start " + Id(self) + " " + std::to_string(server)); }
void __attribute__((thiscall)) F_ActorStop(C_Actor *self, bool server) { Log("actor stop " + Id(self) + " " + std::to_string(server)); }
void __attribute__((thiscall)) F_ActorRestart(C_Actor *self, bool server) { Log("actor restart " + Id(self) + " " + std::to_string(server)); }
// Saved: the actor's own marker (its id times 10 plus 7).
bool __attribute__((thiscall)) F_ActorSave(C_Actor *self, C_chunk &chunk)
{
    Log("actor save " + Id(self));
    chunk.Write(self->GetNetId() * 10 + 7);
    return true;
}
bool __attribute__((thiscall)) F_ActorLoad(C_Actor *self, C_chunk &chunk)
{
    int marker = -1;
    chunk.Read(marker);
    Log("actor load " + Id(self) + " " + std::to_string(marker));
    return true;
}
void __attribute__((thiscall)) F_ActorWriteState(C_Actor *self, C_BitStream *msg)
{
    Log("actor writestate " + Id(self));
    msg->WriteBits(static_cast<uint32_t>(self->GetNetId() & 0xFF), 8);
}
void __attribute__((thiscall)) F_ActorReadState(C_Actor *self, C_BitStream *msg)
{
    Log("actor readstate " + Id(self) + " " + std::to_string(msg->ReadBits(8)));
}
bool __attribute__((thiscall)) F_ActorIsActive(const C_Actor *self) { return self->m_activeIndex >= 0; }

struct S_FakeCreator : C_ActorCreator {
    int type = 0;
    bool fail = false;
    C_Actor *Create() override
    {
        Log("create " + std::to_string(type));
        if (fail)
            return nullptr;
        auto *bytes = static_cast<unsigned char *>(::operator new(ACTOR_SIZE, std::align_val_t(16)));
        std::memset(bytes, 0, ACTOR_SIZE);
        auto *actor = reinterpret_cast<C_Actor *>(bytes);
        *reinterpret_cast<void ***>(actor) = g_actorVt;
        actor->m_type = static_cast<E_ActorType>(type);
        actor->m_netId = -1;
        actor->m_activeIndex = -1;
        g_live.insert(actor);
        return actor;
    }
    const char *GetName() override { return "fake"; }
    void Destroy(C_Actor *actor) override
    {
        Log("destroy " + std::to_string(type) + " " + Id(actor));
        CHECK(g_live.erase(actor) == 1);         // (never twice)
        g_destroyed.push_back(actor);
    }
};
S_FakeCreator g_creators[ACTOR_TYPE_COUNT];
C_ActorCreator *g_creatorTable[ACTOR_TYPE_COUNT];
constexpr int NO_CREATOR = ACTOR_TYPE_COUNT - 1;        // the type without a creator

void InitFakes()
{
    for (int t = 0; t < ACTOR_TYPE_COUNT; ++t) {
        g_creators[t].type = t;
        g_creatorTable[t] = t == NO_CREATOR ? nullptr : &g_creators[t];
    }
    g_actorCreators = g_creatorTable;
    g_actorVt[VirtualSlot(&C_Actor::Update)] = reinterpret_cast<void *>(F_ActorUpdate);
    g_actorVt[VirtualSlot(&C_Actor::Tick)] = reinterpret_cast<void *>(F_ActorTick);
    g_actorVt[VirtualSlot(&C_Actor::OnMessage)] = reinterpret_cast<void *>(F_ActorMessage);
    g_actorVt[VirtualSlot(&C_Actor::Unknown20)] = reinterpret_cast<void *>(F_ActorMessage2);
    g_actorVt[VirtualSlot(&C_Actor::SetFrame)] = reinterpret_cast<void *>(F_ActorSetFrame);
    g_actorVt[VirtualSlot(&C_Actor::StartMission)] = reinterpret_cast<void *>(F_ActorStart);
    g_actorVt[VirtualSlot(&C_Actor::StopMission)] = reinterpret_cast<void *>(F_ActorStop);
    g_actorVt[VirtualSlot(&C_Actor::RestartMission)] = reinterpret_cast<void *>(F_ActorRestart);
    g_actorVt[VirtualSlot(&C_Actor::SaveGame)] = reinterpret_cast<void *>(F_ActorSave);
    g_actorVt[VirtualSlot(&C_Actor::LoadGame)] = reinterpret_cast<void *>(F_ActorLoad);
    g_actorVt[VirtualSlot(&C_Actor::WriteState)] = reinterpret_cast<void *>(F_ActorWriteState);
    g_actorVt[VirtualSlot(&C_Actor::ReadState)] = reinterpret_cast<void *>(F_ActorReadState);
    g_actorVt[VirtualSlot(&C_Actor::IsActive)] = reinterpret_cast<void *>(F_ActorIsActive);
    g_defVt[VirtualSlot(&C_ItemDef::GetBehaviour)] = reinterpret_cast<void *>(F_DefBehaviour);
    g_frameVt[0x00 / 4] = reinterpret_cast<void *>(F_Release);
    g_frameVt[0x04 / 4] = reinterpret_cast<void *>(F_GetVisualType);
    g_frameVt[0x08 / 4] = reinterpret_cast<void *>(F_SetPos);
    g_frameVt[0x0C / 4] = reinterpret_cast<void *>(F_SetDir);
    g_frameVt[0x14 / 4] = reinterpret_cast<void *>(F_SetPosDir);
    g_frameVt[0x1C / 4] = reinterpret_cast<void *>(F_SetWorldDir);
    g_frameVt[0x24 / 4] = reinterpret_cast<void *>(F_Update);
    g_frameVt[0x4C / 4] = reinterpret_cast<void *>(F_Duplicate);
    g_driverVt[0x54 / 4] = reinterpret_cast<void *>(F_CreateFrame);
    g_driverVt[0x50 / 4] = reinterpret_cast<void *>(F_CreateVisual);
    GetGame()->m_interface = reinterpret_cast<C_Interface *>(g_interfaceBytes);
    // The player table the messages quantise with (sender 0, recipient 1): 2 km around the origin in 1 cm steps.
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
}
}  // namespace

// The humans: logged; their items in a list per human.
namespace {
std::map<const C_Human *, std::vector<C_Item *>> g_humanItems;
std::set<const C_Human *> g_suspended, g_unconsciousHumans;
}  // namespace
void C_Human::DetachFromWorld() { Log("detach " + Id(this)); }
void C_Human::HandleMessage(C_Human *human, C_BitStream *msg)
{
    Log("human message " + (human ? Id(human) : std::string("null")) + " " + std::to_string(msg->ReadBits(8)));
}
void C_Human::HandleMessage2(C_Human *human, C_BitStream *msg)
{
    Log("human message2 " + (human ? Id(human) : std::string("null")) + " " + std::to_string(msg->ReadBits(8)));
}
bool C_Human::IsAISuspended() const { return g_suspended.count(this) != 0; }
bool C_Human::IsUnconscious() const { return g_unconsciousHumans.count(this) != 0; }
C_Item *C_Human::FindItem(int type, int *) const
{
    for (C_Item *item : g_humanItems[this])
        if (item->GetTypeId() == type)
            return item;
    return nullptr;
}
C_Item *C_Human::FindItemByUid(int uid) const
{
    for (C_Item *item : g_humanItems[this])
        if (item->GetUid() == uid)
            return item;
    return nullptr;
}
void C_Human::RemoveItem(int uid, bool destroy)
{
    Log("human remove " + Id(this) + " " + std::to_string(uid) + " " + std::to_string(destroy));
    auto &items = g_humanItems[this];
    items.erase(std::remove_if(items.begin(), items.end(), [uid](C_Item *i) { return i->GetUid() == uid; }), items.end());
}
void C_ItemActor::SetupItem(C_Item *item, const S_vector &velocity)
{
    Log("setupitem " + Id(this) + " " + std::to_string(item->GetUid()) + " vel " + V(velocity));
    m_item = item;
}
void C_Mine::Setup(C_Human *owner, int itemId, int arg)
{
    Log("minesetup " + Id(this) + " " + (owner ? Id(owner) : std::string("none")) + " " + std::to_string(itemId) + " " + std::to_string(arg));
    m_owner = owner;
    m_itemId = itemId;
}

namespace {
// ============================================================ helpers
// Resets both lists (no actors, the module's player slots empty) and the logs.
void Fresh(uint32_t role = GAME_ROLE_LOCAL)
{
    g_gameRole = GAME_ROLE_LOCAL;
    g_actorsModule.m_iterating = false;
    g_actorsModule.m_removed.clear();
    g_actorsModule.Clear();
    g_temporaryActors.m_iterating = false;
    g_temporaryActors.Clear();
    g_temporaryActors.m_toDestroy.clear();
    g_temporaryActors.m_breakables.clear();
    g_temporaryActors.m_respawnItems.clear();
    g_temporaryActors.m_nextId = 0;
    g_temporaryActors.m_debrisCount = 0;
    g_gameRole = role;
    g_log.clear();
    g_sent.clear();
    g_destroyed.clear();
    g_tickChanges.clear();
    g_tickDestroys.clear();
}
C_Human *AsHuman(C_Actor *a) { return static_cast<C_Human *>(a); }
std::vector<int> Ids(const std::vector<C_Actor *> &list)
{
    std::vector<int> out;
    for (C_Actor *a : list)
        out.push_back(a ? a->GetNetId() : -1);
    return out;
}
bool ActiveConsistent(const std::vector<C_Actor *> &active)
{
    for (size_t i = 0; i < active.size(); ++i)
        if (active[i]->GetActiveIndex() != static_cast<int>(i))
            return false;
    return true;
}
// A message with `bits` values, read back from its start.
C_NetMessage *Msg(std::initializer_list<std::pair<uint32_t, int>> fields)
{
    auto *m = new C_NetMessage();
    for (auto [v, n] : fields)
        m->WriteBits(v, n);
    m->ResetPosition();
    return m;
}

// ============================================================ C_ActorsModule
void TestModuleCreate()
{
    Fresh();
    // Cleared: the 32 player slots, empty, unused.
    CHECK(g_actorsModule.GetActorCount() == C_ActorsModule::PLAYER_SLOTS && g_actorsModule.m_active.empty());
    for (int i = 0; i < C_ActorsModule::PLAYER_SLOTS; ++i)
        CHECK(!g_actorsModule.GetActor(i) && g_actorsModule.GetPlayerSlotUse(i) == -1);
    CHECK(g_actorsModule.GetPlayerSlotUse(-1) == -1 && g_actorsModule.GetPlayerSlotUse(32) == -1);
    g_actorsModule.SetPlayerSlotUse(32, 5);
    g_actorsModule.SetPlayerSlotUse(-1, 5);
    g_actorsModule.SetPlayerSlotUse(3, 0x7F);
    CHECK(g_actorsModule.GetPlayerSlotUse(3) == 0x7F);
    g_actorsModule.SetPlayerSlotUse(3, 0xFF);
    // No type, a type out of range, no creator: nothing.
    CHECK(!g_actorsModule.CreateActor(static_cast<E_ActorType>(-1), nullptr, true, -1));
    CHECK(!g_actorsModule.CreateActor(static_cast<E_ActorType>(ACTOR_TYPE_COUNT), nullptr, true, -1));
    CHECK(!g_actorsModule.CreateActor(static_cast<E_ActorType>(NO_CREATOR), nullptr, true, -1));
    CHECK(g_log.empty() && g_actorsModule.GetActorCount() == 32);
    // Appended: the next index as its id; active or not.
    C_Actor *a = g_actorsModule.CreateActor(ACTOR_HUMAN, nullptr, true, -1);
    C_Actor *b = g_actorsModule.CreateActor(ACTOR_CAR, nullptr, false, -1);
    C_Actor *c = g_actorsModule.CreateActor(ACTOR_DOOR, nullptr, true, -1);
    CHECK(a && b && c && a->GetNetId() == 32 && b->GetNetId() == 33 && c->GetNetId() == 34 && g_actorsModule.GetActorCount() == 35);
    CHECK(Ids(g_actorsModule.m_active) == std::vector<int>({32, 34}) && ActiveConsistent(g_actorsModule.m_active) && b->GetActiveIndex() == -1);
    CHECK(g_actorsModule.GetActor(33) == b && !g_actorsModule.GetActor(35) && !g_actorsModule.GetActor(-1));
    // A player slot: the id is the slot; its use counted (0xFF + 1 = 0, then 1); an occupant destroyed first.
    C_Actor *p = g_actorsModule.CreateActor(ACTOR_HUMAN, nullptr, true, 5);
    CHECK(p->GetNetId() == 5 && g_actorsModule.GetActor(5) == p && g_actorsModule.GetPlayerSlotUse(5) == 0 && g_actorsModule.GetActorCount() == 35);
    g_log.clear();
    C_Actor *q = g_actorsModule.CreateActor(ACTOR_HUMAN, nullptr, false, 5);
    CHECK(q && g_actorsModule.GetActor(5) == q && g_actorsModule.GetPlayerSlotUse(5) == 1 && g_destroyed.back() == p && p->GetActiveIndex() == -1 &&
          At("create 3") < At("destroy 3 5"));
    CHECK(Ids(g_actorsModule.m_active) == std::vector<int>({32, 34}) && ActiveConsistent(g_actorsModule.m_active));
    // The creator failing: nothing made, but an occupant is destroyed all the same.
    g_creators[ACTOR_HUMAN].fail = true;
    CHECK(!g_actorsModule.CreateActor(ACTOR_HUMAN, nullptr, true, 5));
    CHECK(!g_actorsModule.GetActor(5) && g_destroyed.back() == q && g_actorsModule.GetPlayerSlotUse(5) == 1);
    CHECK(!g_actorsModule.CreateActor(ACTOR_HUMAN, nullptr, true, -1) && g_actorsModule.GetActorCount() == 35);
    g_creators[ACTOR_HUMAN].fail = false;
    // Frames: one without data gets a data block (the actor, no physics object); one with data is pointed at it.
    I3D_frame *f1 = NewFrame(FRAME_MODEL, "f1");
    g_log.clear();
    C_Actor *d = g_actorsModule.CreateActor(ACTOR_DOOR, f1, false, -1);
    CHECK(Count("setframedata f1") == 1 && f1->GetData() && f1->GetData()->actor == d &&
          f1->GetData()->physicsObject == reinterpret_cast<S_PhysicsHandle *>(g_noPhysicsObject) && !f1->GetData()->script);
    S_FrameData *kept = f1->GetData();
    kept->script = reinterpret_cast<C_Script *>(0x1234);
    g_log.clear();
    C_Actor *e = g_actorsModule.CreateActor(ACTOR_DOOR, f1, false, -1);
    CHECK(!Logged("setframedata") && f1->GetData() == kept && kept->actor == e && kept->script == reinterpret_cast<C_Script *>(0x1234));
    // (The actor's own frame is not set by the module: that is the caller's.)
    CHECK(!e->GetFrame());
}

void TestModuleDestroy()
{
    Fresh();
    C_Actor *a = g_actorsModule.CreateActor(ACTOR_DOOR, nullptr, true, -1);     // 32
    C_Actor *b = g_actorsModule.CreateActor(ACTOR_DOOR, nullptr, true, -1);     // 33
    C_Actor *c = g_actorsModule.CreateActor(ACTOR_DOOR, nullptr, false, -1);    // 34
    C_Actor *d = g_actorsModule.CreateActor(ACTOR_DOOR, nullptr, true, -1);     // 35
    g_actorsModule.DestroyActor(nullptr, true);
    // A non-player actor: the last takes its place and id; out of the active set; destroyed now.
    g_log.clear();
    g_actorsModule.DestroyActor(a, true);
    CHECK(g_actorsModule.GetActor(32) == d && d->GetNetId() == 32 && g_actorsModule.GetActorCount() == 35 && g_destroyed.back() == a &&
          a->GetActiveIndex() == -1 && Count("destroy " + std::to_string(ACTOR_DOOR) + " 32") == 1);
    CHECK(Ids(g_actorsModule.m_active) == std::vector<int>({32, 33}) && ActiveConsistent(g_actorsModule.m_active));
    // The last one itself.
    g_actorsModule.DestroyActor(c, true);
    CHECK(g_actorsModule.GetActorCount() == 34 && g_actorsModule.GetActor(33) == b);
    // A player slot: emptied, nothing moves.
    C_Actor *p = g_actorsModule.CreateActor(ACTOR_HUMAN, nullptr, true, 7);
    g_actorsModule.DestroyActor(p, true);
    CHECK(!g_actorsModule.GetActor(7) && g_actorsModule.GetActorCount() == 34 && g_destroyed.back() == p);
    // Its frame data: freed when nothing else uses it, else only unbound; no frame or data: nothing.
    struct {
        int field;
        bool freed;
    } const cases[] = {{0, true}, {1, false}, {2, false}, {3, false}, {4, false}, {5, false}};
    for (const auto &k : cases) {
        I3D_frame *f = NewFrame(FRAME_MODEL, "fd");
        C_Actor *x = g_actorsModule.CreateActor(ACTOR_DOOR, f, false, -1);
        x->m_frame = f;
        S_FrameData *data = f->GetData();
        switch (k.field) {
        case 1: data->script = reinterpret_cast<C_Script *>(1); break;
        case 2: data->unknown08 = 1; break;
        case 3: data->physicsObject = nullptr; break;
        case 4: data->versionMaster = reinterpret_cast<C_VersionMaster *>(1); break;
        case 5: data->versionKeeper = reinterpret_cast<C_VersionKeeper *>(1); break;
        default: break;
        }
        g_log.clear();
        g_actorsModule.DestroyActor(x, true);
        CHECK(Count("freeframedata fd") == (k.freed ? 1 : 0) && (k.freed ? !f->GetData() : f->GetData() == data && !data->actor));
    }
    C_Actor *nodata = g_actorsModule.CreateActor(ACTOR_DOOR, nullptr, false, -1);
    nodata->m_frame = NewFrame(FRAME_MODEL, "plain");
    g_log.clear();
    g_actorsModule.DestroyActor(nodata, true);
    CHECK(!Logged("freeframedata"));

    // Later: a human queued for 20 s, taken out of the world, not destroyed yet; the slot empty already.
    C_Actor *h = g_actorsModule.CreateActor(ACTOR_HUMAN, nullptr, true, 3);
    g_log.clear();
    const size_t destroyed = g_destroyed.size();
    g_actorsModule.DestroyActor(h, false);
    CHECK(!g_actorsModule.GetActor(3) && g_actorsModule.m_removed.size() == 1 && g_actorsModule.m_removed[0].actor == h &&
          g_actorsModule.m_removed[0].timeLeft == C_ActorsModule::REMOVAL_DELAY && Count("detach 3") == 1 && g_destroyed.size() == destroyed &&
          h->GetActiveIndex() == -1);
    // Found by id while its slot is empty (the latest removed first), and as a human; not once the slot is taken.
    C_Actor *h2 = g_actorsModule.CreateActor(ACTOR_HUMAN, nullptr, true, 3);
    g_actorsModule.DestroyActor(h2, false);
    CHECK(g_actorsModule.GetActorById(3) == h2 && g_actorsModule.GetHuman(3) == h2);
    C_Actor *h3 = g_actorsModule.CreateActor(ACTOR_HUMAN, nullptr, true, 3);
    CHECK(g_actorsModule.GetActorById(3) == h3 && g_actorsModule.GetHuman(3) == h3);
    // A non-human in the slot: GetHuman looks among the removed.
    g_actorsModule.DestroyActor(h3, true);
    C_Actor *notHuman = g_actorsModule.CreateActor(ACTOR_DOOR, nullptr, false, 3);
    CHECK(g_actorsModule.GetActorById(3) == notHuman && g_actorsModule.GetHuman(3) == h2);
    // Ids out of range; a non-player id; a non-human non-player actor: null.
    CHECK(!g_actorsModule.GetActorById(-1) && !g_actorsModule.GetActorById(g_actorsModule.GetActorCount()) && !g_actorsModule.GetHuman(-1) &&
          !g_actorsModule.GetHuman(g_actorsModule.GetActorCount()));
    CHECK(g_actorsModule.GetActorById(32) == d && !g_actorsModule.GetHuman(32) && !g_actorsModule.GetHuman(10));
    CHECK(!g_actorsModule.GetActorById(10));
}

void TestModuleActivity()
{
    Fresh();
    C_Actor *a = g_actorsModule.CreateActor(ACTOR_DOOR, nullptr, false, -1);
    C_Actor *b = g_actorsModule.CreateActor(ACTOR_DOOR, nullptr, false, -1);
    C_Actor *c = g_actorsModule.CreateActor(ACTOR_DOOR, nullptr, false, -1);
    g_actorsModule.SetActorActive(a, true);
    g_actorsModule.SetActorActive(a, true);             // (already)
    g_actorsModule.SetActorActive(b, true);
    g_actorsModule.SetActorActive(c, true);
    CHECK(Ids(g_actorsModule.m_active) == std::vector<int>({32, 33, 34}) && ActiveConsistent(g_actorsModule.m_active));
    // Off: the last takes the place.
    g_actorsModule.SetActorActive(a, false);
    g_actorsModule.SetActorActive(a, false);            // (already)
    CHECK(Ids(g_actorsModule.m_active) == std::vector<int>({34, 33}) && ActiveConsistent(g_actorsModule.m_active) && a->GetActiveIndex() == -1);
    // The only one.
    g_actorsModule.SetActorActive(b, false);
    g_actorsModule.SetActorActive(c, false);
    CHECK(g_actorsModule.m_active.empty());
    // While the set is iterated: queued; a no-change request is not queued.
    g_actorsModule.SetActorActive(a, true);
    g_actorsModule.m_iterating = true;
    g_actorsModule.SetActorActive(a, true);
    g_actorsModule.SetActorActive(b, true);
    g_actorsModule.SetActorActive(a, false);
    CHECK(g_actorsModule.m_pending.size() == 2 && Ids(g_actorsModule.m_active) == std::vector<int>({32}));
    g_actorsModule.m_iterating = false;

    // Tick: removals (not a client), the periodic links, the human pass, every active actor; the activations asked
    // for meanwhile after (each queued actor toggles).
    g_actorsModule.m_pending.clear();
    g_actorsModule.SetActorActive(b, true);             // active: a, b
    g_tickChanges[a] = {c, true};
    g_tickChanges[b] = {a, false};
    g_log.clear();
    g_actorsModule.Tick(100);
    CHECK(Lines("actor tick") == std::vector<std::string>({"actor tick 32 100", "actor tick 33 100"}) && Count("links tick") == 1 && At("links tick") < At("humanpass 100 2") &&
          At("humanpass 100 2") < At("actor tick 32 100"));
    // (Queued: c on, then a off: the last (c) takes a's place.)
    CHECK(Ids(g_actorsModule.m_active) == std::vector<int>({34, 33}) && ActiveConsistent(g_actorsModule.m_active) && g_actorsModule.m_pending.empty() &&
          !g_actorsModule.m_iterating);
    // Update likewise (no links, no human pass), then the enemies counted.
    g_tickChanges.clear();
    g_tickChanges[c] = {a, true};
    g_log.clear();
    g_actorsModule.Update(16);
    CHECK(Lines("actor update") == std::vector<std::string>({"actor update 34 16", "actor update 33 16"}) && !Logged("links") && !Logged("humanpass"));
    CHECK(Ids(g_actorsModule.m_active) == std::vector<int>({34, 33, 32}));
    // An actor queued twice toggles twice.
    g_tickChanges.clear();
    g_tickChanges[a] = {b, false};
    g_tickChanges[c] = {b, false};
    g_actorsModule.Tick(100);
    CHECK(b->GetActiveIndex() >= 0 && ActiveConsistent(g_actorsModule.m_active) && g_actorsModule.m_active.size() == 3);
    // Created active while the set is iterated: added at once (not counted in that pass).
    g_tickChanges.clear();
    g_actorsModule.m_iterating = true;
    C_Actor *n = g_actorsModule.CreateActor(ACTOR_DOOR, nullptr, true, -1);
    CHECK(n->GetActiveIndex() == 3 && g_actorsModule.m_active.size() == 4);
    g_actorsModule.m_iterating = false;
}

void TestModuleSlotsAndSpawns()
{
    Fresh();
    CHECK(g_actorsModule.FindFreePlayerSlot() == 0);
    for (int i = 0; i < 3; ++i)
        g_actorsModule.CreateActor(ACTOR_HUMAN, nullptr, false, i);
    CHECK(g_actorsModule.FindFreePlayerSlot() == 3);
    g_actorsModule.DestroyActor(g_actorsModule.GetActor(1), true);
    CHECK(g_actorsModule.FindFreePlayerSlot() == 1);
    for (int i = 0; i < C_ActorsModule::PLAYER_SLOTS; ++i)
        if (!g_actorsModule.GetActor(i))
            g_actorsModule.CreateActor(ACTOR_HUMAN, nullptr, false, i);
    CHECK(g_actorsModule.FindFreePlayerSlot() == -1);

    // Spawn points: team, enabled; the random one also of the kind or kind 2.
    Fresh();
    const auto spawn = [](int team, int kind, bool enabled) {
        auto *s = static_cast<C_SpawnPoint *>(g_actorsModule.CreateActor(ACTOR_SPAWN_POINT, nullptr, false, -1));
        s->m_team = team;
        s->m_kind = kind;
        s->m_free = enabled;
        return s;
    };
    CHECK(!g_actorsModule.FindSpawnPoint(0) && !g_actorsModule.FindRandomSpawnPoint(0, 0));
    g_actorsModule.CreateActor(ACTOR_DOOR, nullptr, false, -1);
    C_SpawnPoint *s0 = spawn(1, 0, false);   // 33
    C_SpawnPoint *s1 = spawn(1, 0, true);    // 34
    C_SpawnPoint *s2 = spawn(0, 1, true);    // 35
    C_SpawnPoint *s3 = spawn(1, 1, true);    // 36
    C_SpawnPoint *s4 = spawn(1, 2, true);    // 37
    CHECK(g_actorsModule.FindSpawnPoint(1) == s1 && g_actorsModule.FindSpawnPoint(0) == s2 && !g_actorsModule.FindSpawnPoint(2) && s0);
    // Random: count the matches, pick rand() / 32767 of them.
    const auto expectPick = [](int count) { return static_cast<int>(std::rand() * (1.0f / 32767.0f) * count); };
    for (unsigned seed = 1; seed < 40; ++seed) {
        std::srand(seed);
        const int pick1 = expectPick(2);     // team 1 kind 0: s1, s4
        const int pick2 = expectPick(2);     // team 1 kind 1: s3, s4
        const int pick3 = expectPick(1);     // team 0 kind 1: s2
        const int pick4 = expectPick(0);     // team 0 kind 0: none
        std::srand(seed);
        C_Actor *r1 = g_actorsModule.FindRandomSpawnPoint(1, 0);
        C_Actor *r2 = g_actorsModule.FindRandomSpawnPoint(1, 1);
        C_Actor *r3 = g_actorsModule.FindRandomSpawnPoint(0, 1);
        C_Actor *r4 = g_actorsModule.FindRandomSpawnPoint(0, 0);
        CHECK(r1 == (pick1 == 0 ? static_cast<C_Actor *>(s1) : s4) && r2 == (pick2 == 0 ? static_cast<C_Actor *>(s3) : s4) && r3 == s2 &&
              r4 == nullptr && pick4 == 0);
        (void)pick3;
    }
    // A run of draws: the same picks, and both of them happen.
    std::srand(7);
    std::vector<int> want;
    for (int i = 0; i < 60; ++i)
        want.push_back(expectPick(2));
    std::srand(7);
    int firsts = 0;
    for (int i = 0; i < 60; ++i) {
        C_Actor *r = g_actorsModule.FindRandomSpawnPoint(1, 0);
        CHECK(r == (want[i] == 0 ? static_cast<C_Actor *>(s1) : s4));
        firsts += r == s1;
    }
    CHECK(firsts > 10 && firsts < 50);
}

void TestModuleRemovals()
{
    // A server: the removed human's player state cleared of "left", stopped as on a server, destroyed; a client:
    // stopped as a client and the interface told.
    for (const uint32_t role : {static_cast<uint32_t>(GAME_ROLE_SERVER), static_cast<uint32_t>(GAME_ROLE_CLIENT),
                                static_cast<uint32_t>(GAME_ROLE_LOCAL)}) {
        Fresh();
        C_Actor *h = g_actorsModule.CreateActor(ACTOR_HUMAN, nullptr, false, 4);
        g_actorsModule.DestroyActor(h, false);
        Player(4).left = true;
        g_gameRole = role;
        g_log.clear();
        g_actorsModule.TickRemovals(20000);             // (exactly the delay: kept)
        CHECK(g_actorsModule.m_removed.size() == 1 && g_actorsModule.m_removed[0].timeLeft == 0 && g_log.empty());
        g_actorsModule.TickRemovals(1);
        const bool server = role & GAME_ROLE_SERVER, client = role & GAME_ROLE_CLIENT;
        CHECK(g_actorsModule.m_removed.empty() && g_destroyed.back() == h && Player(4).left == !server &&
              Count("actor stop 4 0") == (client ? 1 : 0) && Count("actor stop 4 1") == (server ? 1 : 0) &&
              Count("interface " + std::to_string(INTERFACE_ACTOR_DELETED) + " 4") == (client ? 1 : 0) &&
              Count("lockplayers") == (server ? 1 : 0) && Count("unlockplayers") == (server ? 1 : 0));
        if (server && client)
            CHECK(At("actor stop 4 0") < At("actor stop 4 1") && At("actor stop 4 1") < At("interface " + std::to_string(INTERFACE_ACTOR_DELETED) + " 4"));
    }
    // Several: the last entry takes a removed one's place and is checked next (its time taken off once: it had not
    // been reached; the original 0x00476860 does the same).
    Fresh(GAME_ROLE_SERVER);
    C_Actor *h1 = g_actorsModule.CreateActor(ACTOR_HUMAN, nullptr, false, 1);
    C_Actor *h2 = g_actorsModule.CreateActor(ACTOR_HUMAN, nullptr, false, 2);
    C_Actor *h3 = g_actorsModule.CreateActor(ACTOR_HUMAN, nullptr, false, 3);
    g_actorsModule.DestroyActor(h1, false);
    g_actorsModule.DestroyActor(h2, false);
    g_actorsModule.DestroyActor(h3, false);
    g_actorsModule.m_removed[0].timeLeft = 50;          // h1: goes
    g_actorsModule.m_removed[1].timeLeft = 500;         // h2
    g_actorsModule.m_removed[2].timeLeft = 150;         // h3: moved to [0], 150 - 100 = 50: stays
    g_actorsModule.TickRemovals(100);
    CHECK(g_actorsModule.m_removed.size() == 2 && g_actorsModule.m_removed[0].actor == h3 && g_actorsModule.m_removed[0].timeLeft == 50 &&
          g_actorsModule.m_removed[1].actor == h2 && g_actorsModule.m_removed[1].timeLeft == 400 && g_destroyed.size() == 1 && g_destroyed[0] == h1);
    // ...and one that goes too once moved: 30 - 100.
    g_actorsModule.m_removed[1].timeLeft = 30;          // h2 (last): moved into h3's place when h3 goes
    g_actorsModule.m_removed[0].timeLeft = 10;
    g_actorsModule.TickRemovals(100);
    CHECK(g_actorsModule.m_removed.empty() && g_destroyed.size() == 3 && g_destroyed[1] == h3 && g_destroyed[2] == h2);
    C_Actor *h4 = g_actorsModule.CreateActor(ACTOR_HUMAN, nullptr, false, 4);
    g_actorsModule.DestroyActor(h4, false);
    // Tick removes them on a server only (a local game has the client bit); Update outside a local game (the
    // original 0x00476080 / 0x00476160): a local game never.
    for (const uint32_t role : {static_cast<uint32_t>(GAME_ROLE_SERVER), static_cast<uint32_t>(GAME_ROLE_CLIENT),
                                static_cast<uint32_t>(GAME_ROLE_LOCAL)}) {
        g_gameRole = role;
        g_actorsModule.m_removed[0].timeLeft = 400;
        g_actorsModule.Tick(100);
        CHECK(g_actorsModule.m_removed[0].timeLeft == (role & GAME_ROLE_CLIENT ? 400 : 300));
        g_actorsModule.m_removed[0].timeLeft = 400;
        g_actorsModule.Update(30);
        CHECK(g_actorsModule.m_removed[0].timeLeft == (role == GAME_ROLE_LOCAL ? 400 : 370));
    }
    g_gameRole = GAME_ROLE_LOCAL;
}

void TestModuleMission()
{
    // StartMission: the counts zeroed, every player slot unused again, every actor started.
    Fresh();
    C_Actor *p = g_actorsModule.CreateActor(ACTOR_HUMAN, nullptr, false, 2);
    C_Actor *a = g_actorsModule.CreateActor(ACTOR_DOOR, nullptr, false, -1);
    g_actorsModule.m_liveEnemies = 4;
    g_actorsModule.m_carnageEnemies = 2;
    g_log.clear();
    g_actorsModule.StartMission(true);
    CHECK(g_actorsModule.GetPlayerSlotUse(2) == -1 && g_actorsModule.m_liveEnemies == 0 && g_actorsModule.m_carnageEnemies == 0 &&
          Lines("actor start") == std::vector<std::string>({"actor start 2 1", "actor start 32 1"}));
    // RestartMission: the counts zeroed, the links cleared, every actor restarted and activated; a server counts.
    g_coop = false;
    g_actorsModule.m_liveEnemies = 4;
    g_log.clear();
    g_actorsModule.RestartMission(false);
    CHECK(Lines("actor restart") == std::vector<std::string>({"actor restart 2 0", "actor restart 32 0"}) && Count("links clear") == 1 &&
          p->GetActiveIndex() >= 0 && a->GetActiveIndex() >= 0 && g_actorsModule.m_liveEnemies == 0);
    AsHuman(p)->m_role = HUMAN_ROLE_ENEMY;
    AsHuman(p)->m_stats[STAT_HEALTH] = 10;
    AsHuman(p)->m_controller = reinterpret_cast<C_HumanController *>(1);
    g_actorsModule.RestartMission(false);
    CHECK(g_actorsModule.m_liveEnemies == 0);
    g_actorsModule.RestartMission(true);
    CHECK(g_actorsModule.m_liveEnemies == 1);
    // StopMission: every actor stopped; the players' actors destroyed by a server's stop, or by any stop outside a
    // server; queued removals done first outside a local game.
    for (const uint32_t role : {static_cast<uint32_t>(GAME_ROLE_LOCAL), static_cast<uint32_t>(GAME_ROLE_SERVER),
                                static_cast<uint32_t>(GAME_ROLE_CLIENT)}) {
        for (const bool server : {false, true}) {
            Fresh();
            C_Actor *h = g_actorsModule.CreateActor(ACTOR_HUMAN, nullptr, false, 2);
            C_Actor *x = g_actorsModule.CreateActor(ACTOR_DOOR, nullptr, false, -1);
            C_Actor *gone = g_actorsModule.CreateActor(ACTOR_HUMAN, nullptr, false, 9);
            g_actorsModule.DestroyActor(gone, false);
            g_gameRole = role;
            g_log.clear();
            g_actorsModule.StopMission(server);
            const bool destroys = server || !(role & GAME_ROLE_SERVER);
            CHECK(Count("actor stop 2 " + std::to_string(server)) == 1 && Count("actor stop 32 " + std::to_string(server)) == 1 &&
                  (g_actorsModule.GetActor(2) == nullptr) == destroys && g_actorsModule.GetActor(32) == x &&
                  g_actorsModule.m_removed.empty() == (role != GAME_ROLE_LOCAL));
            if (destroys)
                CHECK(std::find(g_destroyed.begin(), g_destroyed.end(), h) != g_destroyed.end());
        }
    }
    g_gameRole = GAME_ROLE_LOCAL;
}

void TestModuleEnemies()
{
    Fresh();
    // Humans counted: enemies alive with a controller, conscious; live ones not suspended; carnage ones.
    struct S_Case {
        int role, health;
        bool controller, unconscious, suspended, carnage;
        int live, carnageCount;
    } const cases[] = {
        {HUMAN_ROLE_ENEMY, 50, true, false, false, false, 1, 0}, {HUMAN_ROLE_ENEMY, 0, true, false, false, true, 0, 0},
        {HUMAN_ROLE_ENEMY, 1, true, false, false, true, 1, 1},   {HUMAN_ROLE_ENEMY, 50, false, false, false, true, 0, 0},
        {HUMAN_ROLE_ENEMY, 50, true, true, false, true, 0, 0},   {HUMAN_ROLE_ENEMY, 50, true, false, true, false, 0, 0},
        {HUMAN_ROLE_ENEMY, 50, true, false, true, true, 0, 1},   {HUMAN_ROLE_NEUTRAL, 50, true, false, false, true, 0, 0},
    };
    for (const S_Case &k : cases) {
        Fresh();
        C_Human *h = AsHuman(g_actorsModule.CreateActor(ACTOR_HUMAN, nullptr, false, -1));
        h->m_role = static_cast<E_HumanRole>(k.role);
        h->m_stats[STAT_HEALTH] = k.health;
        h->m_controller = k.controller ? reinterpret_cast<C_HumanController *>(1) : nullptr;
        h->m_carnage = k.carnage;
        if (k.unconscious)
            g_unconsciousHumans.insert(h);
        if (k.suspended)
            g_suspended.insert(h);
        g_actorsModule.CountEnemies();
        CHECK(g_actorsModule.GetLiveEnemyCount() == k.live && g_actorsModule.GetCarnageEnemyCount() == k.carnageCount &&
              g_actorsModule.GetCarnageEnemy() == (k.carnageCount == 1 ? h : nullptr));
    }
    // A non-human actor of the enemy's fields is not counted; two carnage enemies: no single one.
    Fresh();
    C_Human *x = AsHuman(g_actorsModule.CreateActor(ACTOR_DOOR, nullptr, false, -1));
    x->m_role = HUMAN_ROLE_ENEMY;
    x->m_stats[STAT_HEALTH] = 50;
    x->m_controller = reinterpret_cast<C_HumanController *>(1);
    C_Human *e1 = AsHuman(g_actorsModule.CreateActor(ACTOR_HUMAN, nullptr, false, -1));
    C_Human *e2 = AsHuman(g_actorsModule.CreateActor(ACTOR_HUMAN, nullptr, false, 3));
    for (C_Human *e : {e1, e2}) {
        e->m_role = HUMAN_ROLE_ENEMY;
        e->m_stats[STAT_HEALTH] = 50;
        e->m_controller = reinterpret_cast<C_HumanController *>(1);
        e->m_carnage = true;
    }
    g_actorsModule.CountEnemies();
    CHECK(g_actorsModule.GetLiveEnemyCount() == 2 && g_actorsModule.GetCarnageEnemyCount() == 2 && !g_actorsModule.GetCarnageEnemy());
    // Only in a local or co-operative game (else the counts stay); Update counts.
    g_gameRole = GAME_ROLE_SERVER;
    g_coop = false;
    g_actorsModule.m_liveEnemies = 9;
    g_actorsModule.CountEnemies();
    CHECK(g_actorsModule.GetLiveEnemyCount() == 9);
    g_coop = true;
    g_actorsModule.CountEnemies();
    CHECK(g_actorsModule.GetLiveEnemyCount() == 2);
    g_coop = false;
    g_gameRole = GAME_ROLE_LOCAL;
    g_actorsModule.m_liveEnemies = 9;
    g_actorsModule.Update(10);
    CHECK(g_actorsModule.GetLiveEnemyCount() == 2);
}

void TestModuleSaveLoad()
{
    Fresh();
    C_Actor *p = g_actorsModule.CreateActor(ACTOR_HUMAN, nullptr, true, 1);
    C_Actor *a = g_actorsModule.CreateActor(ACTOR_DOOR, nullptr, false, -1);
    C_Actor *b = g_actorsModule.CreateActor(ACTOR_DOOR, nullptr, true, -1);
    {
        C_chunk out;
        out.Open("actors.sav", true);
        out.Begin(0x100);
        g_log.clear();
        CHECK(g_actorsModule.SaveGame(out));
        out.Ascend();
        out.Close();
    }
    CHECK(Count("links save") == 1 && Lines("actor save") == std::vector<std::string>({"actor save 1", "actor save 32", "actor save 33"}));
    // The bytes: the links' chunk, then a 0x29A chunk per actor: the active byte, the actor's own data.
    const std::string &f = g_files["actors.sav"];
    int at = 6 + 6 + 4;                                 // (the module chunk's head, the links' chunk)
    for (C_Actor *x : {p, a, b}) {
        uint16_t id;
        uint32_t size;
        std::memcpy(&id, f.data() + at, 2);
        std::memcpy(&size, f.data() + at + 2, 4);
        int marker;
        std::memcpy(&marker, f.data() + at + 7, 4);
        CHECK(id == 0x29A && size == 6 + 1 + 4 && static_cast<uint8_t>(f[at + 6]) == (x->GetActiveIndex() >= 0) && marker == x->GetNetId() * 10 + 7);
        at += size;
    }
    CHECK(at == static_cast<int>(f.size()));
    // Loaded back into the same actors: the activity restored, each actor's data read, the enemies counted.
    g_actorsModule.SetActorActive(p, false);
    g_actorsModule.SetActorActive(a, true);
    g_actorsModule.SetActorActive(b, false);
    g_actorsModule.m_liveEnemies = 5;
    {
        C_chunk in;
        in.Open("actors.sav", false);
        uint16_t id;
        in.Descend(id);
        g_log.clear();
        CHECK(g_actorsModule.LoadGame(in));
        in.Ascend();
        in.Close();
    }
    CHECK(Count("links load 119 1234") == 1 &&
          Lines("actor load") == std::vector<std::string>({"actor load 1 17", "actor load 32 327", "actor load 33 337"}) &&
          p->GetActiveIndex() >= 0 && a->GetActiveIndex() == -1 && b->GetActiveIndex() >= 0 && g_actorsModule.m_liveEnemies == 0);
    // A chunk without the active byte (the actor's data empty): active.
    {
        C_chunk out;
        out.Open("short.sav", true);
        out.Begin(0x100);
        out.WriteChunk(0x77, 1);
        for (int i = 0; i < 3; ++i) {
            out.Begin(0x29A);
            out.Ascend();
        }
        out.Ascend();
        out.Close();
    }
    g_actorsModule.SetActorActive(a, false);
    {
        C_chunk in;
        in.Open("short.sav", false);
        uint16_t id;
        in.Descend(id);
        CHECK(g_actorsModule.LoadGame(in));
        in.Close();
    }
    CHECK(p->GetActiveIndex() >= 0 && a->GetActiveIndex() >= 0 && b->GetActiveIndex() >= 0);
}

void TestModuleState()
{
    Fresh();
    C_Actor *p = g_actorsModule.CreateActor(ACTOR_HUMAN, nullptr, false, 6);
    C_Actor *a = g_actorsModule.CreateActor(ACTOR_DOOR, nullptr, false, -1);
    // WriteState / ReadState: every actor in order, the empty slots skipped.
    C_NetMessage msg;
    g_log.clear();
    g_actorsModule.WriteState(&msg);
    CHECK(Lines("actor writestate") == std::vector<std::string>({"actor writestate 6", "actor writestate 32"}));
    msg.ResetPosition();
    g_actorsModule.ReadState(&msg);
    CHECK(Lines("actor readstate") == std::vector<std::string>({"actor readstate 6 6", "actor readstate 32 32"}));
    // Messages: a 10-bit index; a player slot goes to the human's handler (even an empty one), else to the actor.
    g_log.clear();
    CHECK(g_actorsModule.OnMessage(Msg({{32, 10}, {77, 8}})) && Count("actor message 32 77") == 1);
    CHECK(g_actorsModule.OnMessage(Msg({{6, 10}, {78, 8}})) && Count("human message 6 78") == 1);
    CHECK(g_actorsModule.OnMessage(Msg({{7, 10}, {79, 8}})) && Count("human message null 79") == 1);
    CHECK(g_actorsModule.Unknown28(Msg({{32, 10}, {80, 8}})) && Count("actor message2 32 80") == 1);
    CHECK(g_actorsModule.Unknown28(Msg({{6, 10}, {81, 8}})) && Count("human message2 6 81") == 1);
    CHECK(a && p);
    // The message helpers: the module's channel, delivery by reliability, the 10-bit id first.
    g_log.clear();
    C_BitStream *m1 = CreateActorMessage(700, true);
    C_BitStream *m2 = CreateActorMessageEx(5, false);
    SendActorMessage(m1);
    CHECK(Count("netmsg " + std::to_string(MODULE_ACTORS) + " " + std::to_string(NET_DELIVERY_RELIABLE)) == 1 &&
          Count("netmsgex " + std::to_string(MODULE_ACTORS) + " " + std::to_string(NET_DELIVERY_3)) == 1 && g_sent.back() == m1);
    m1->ResetPosition();
    m2->ResetPosition();
    CHECK(m1->ReadBits(10) == 700 && m2->ReadBits(10) == 5);
    // (11 bits of id: only the low 10 go.)
    C_BitStream *m3 = CreateActorMessage(1024 + 3, false);
    m3->ResetPosition();
    CHECK(m3->ReadBits(10) == 3);
    // Clear / FreeMissionData: every actor destroyed, the 32 empty slots back.
    g_log.clear();
    const size_t before = g_destroyed.size();
    g_actorsModule.FreeMissionData(true);
    CHECK(g_destroyed.size() == before + 2 && g_actorsModule.GetActorCount() == 32 && g_actorsModule.m_active.empty());
}
}  // namespace

#include "temp_actors_test.inc"

int main()
{
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    // (No crash dialog: a crashing run must end, not wait for one; a stuck one could not be stopped.)
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    InitFakes();
    TestModuleCreate();
    TestModuleDestroy();
    TestModuleActivity();
    TestModuleSlotsAndSpawns();
    TestModuleRemovals();
    TestModuleMission();
    TestModuleEnemies();
    TestModuleSaveLoad();
    TestModuleState();
    RunTempActorsTests();
    // (Both lists emptied now: their static destructors run after the fake creators are gone.)
    Fresh();
    std::printf("actors: %d checks, %d failures\n", checks, failures);
    return failures != 0;
}
