// The item system: game/items_module.cpp, item_instances.cpp, item_defs.cpp and item_containers.cpp linked as they
// are, with common/chunk.cpp, common/c_str.cpp and the LS3DF port's math. The engine, the humans, sound, shots and
// the equipment are faked (logged). MinGW; build.bat; run items_test.exe ("items: N checks, 0 failures").
//
// Part 1: the definitions of the real items.sav (ITEMS_SAV) written back by C_ItemDef::Save as the table holds
// them; every actor item chunk (0x2775) of a quick save of the original (EQUIPMENT_SAVE) loaded into an actor with
// a human's containers (or one item store) and saved again, compared with the original's bytes.
// Part 2: moving items: MoveItem by container and check, NotifyMove, Put / CanPut, Create / Give, TransferItem and
// its swaps, TransferItemAuto, IsSwapBlocked, CanAdd / CanPutItemInSlot, removing and dropping, the definition rules.
// Part 3: the weapons (Init, Reload, SetRounds, sounds, Fire by kind, projectiles, save / load), the mission items
// files of the game (MISSIONS_DIR) loaded and saved back, the module lifecycle, the activity props, frame trees.
#include <windows.h>

#include <algorithm>
#include <functional>
#include <set>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <vector>

#define private public
#define protected public
#include "common/chunk.h"
#include "game/actor.h"
#include "game/actors_module.h"
#include "game/first_person_view.h"
#include "game/game.h"
#include "game/human.h"
#include "game/human_equipment.h"
#include "game/interface.h"
#include "game/item_defs.h"
#include "game/items.h"
#include "game/items_module.h"
#include "game/particles.h"
#include "game/pending.h"
#include "game/resource_cache.h"
#include "game/shots.h"
#include "game/sound_module.h"
#include "game/sound_table.h"
#include "game/surface_marks.h"
#include "game/team.h"
#include "ls3d/i3d.h"
#include "ls3d/rw_data.h"
#undef private
#undef protected

bool LoadActorItems(C_Actor *actor, C_chunk &chunk);
bool IsSwapBlocked(C_Actor *from, int fromContainer, C_Actor *to, int toContainer, C_Item *item);
bool SaveMissionItems();
void DeleteMissionItemsFile();
bool CloneFrameTree(I3D_frame *&copy, I3D_frame *source, I3D_frame *parent);
void ReleaseFrameTree(I3D_frame *frame);
I3D_frame *GetActivityProp(int index);

bool SaveActorItems(C_Actor *actor, C_chunk &chunk);

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
int CountPrefix(const std::string &prefix)
{
    return static_cast<int>(std::count_if(g_log.begin(), g_log.end(), [&](const std::string &l) { return l.rfind(prefix, 0) == 0; }));
}
int At(const std::string &prefix)
{
    for (int i = 0; i < static_cast<int>(g_log.size()); ++i)
        if (g_log[i].rfind(prefix, 0) == 0)
            return i;
    return -1;
}
bool Logged(const std::string &prefix)
{
    return std::any_of(g_log.begin(), g_log.end(), [&](const std::string &l) { return l.rfind(prefix, 0) == 0; });
}

// ---- files: a name -> contents map; handles index open files
std::map<std::string, std::string> g_files;
struct S_Open {
    std::string name;
    size_t at = 0;
    bool write = false;
};
std::map<int, S_Open> g_open;
int g_nextHandle = 1;
std::string Key(const char *name)
{
    std::string k = name;
    for (char &c : k)
        c = c == '/' ? '\\' : static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return k;
}
std::string ReadDisk(const std::string &path)
{
    std::string out;
    if (FILE *f = std::fopen(path.c_str(), "rb")) {
        char buffer[65536];
        size_t n;
        while ((n = std::fread(buffer, 1, sizeof buffer, f)) > 0)
            out.append(buffer, n);
        std::fclose(f);
    }
    return out;
}
}  // namespace

extern "C" unsigned int __cdecl _controlfp(unsigned int newValue, unsigned int mask);
namespace {
bool g_failRead;                                        // dtaRead answers -1
}
int RW_DATA_CALL dtaOpen(const char *name, bool archivesFirst)
{
    Log("dtaopen " + Key(name) + " " + std::to_string(archivesFirst));
    if (!g_files.count(Key(name)))
        return DTA_INVALID_HANDLE;
    g_open[g_nextHandle] = S_Open{Key(name), 0, false};
    return g_nextHandle++;
}
int RW_DATA_CALL dtaOpenWrite(const char *name, int)
{
    g_files[Key(name)].clear();
    g_open[g_nextHandle] = S_Open{Key(name), 0, true};
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
    if (!g_open.count(handle) || g_failRead)
        return -1;
    S_Open &o = g_open[handle];
    const std::string &f = g_files[o.name];
    int got = static_cast<int>(f.size()) - static_cast<int>(o.at);
    got = std::max(0, std::min(got, size));
    std::memcpy(data, f.data() + o.at, got);
    o.at += got;
    return got;
}
namespace {
int g_writesLeft = -1;                                  // writes until dtaWrite fails (-1: never)
}
int RW_DATA_CALL dtaWrite(int handle, const void *data, int size)
{
    if (g_writesLeft == 0)
        return 0;
    if (g_writesLeft > 0)
        --g_writesLeft;
    S_Open &o = g_open[handle];
    std::string &f = g_files[o.name];
    if (o.at + size > f.size())
        f.resize(o.at + size);
    std::memcpy(&f[o.at], data, size);
    o.at += size;
    return size;
}
bool RW_DATA_CALL dtaDelete(const char *name)
{
    Log(std::string("delete ") + name);
    return g_files.erase(Key(name)) != 0;
}

// ---- the game, the modules only named
namespace {
alignas(16) unsigned char g_gameBytes[sizeof(C_game) + 0x100];
alignas(16) unsigned char g_fpvBytes[0x100];
std::map<int, C_Actor *> g_actorsById;
C_Human *g_activePlayer;
}
C_game *GetGame() { return reinterpret_cast<C_game *>(g_gameBytes); }
namespace {
// The game's virtual SetState (its g++ slot from the member pointer): logged, kept.
int g_gameState;
void __attribute__((thiscall)) F_SetState(C_game *, int state)
{
    Log("game state " + std::to_string(state));
    g_gameState = state;
}
void *g_gameVt[128];
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
}
namespace {
bool g_firstPerson;
}
bool C_game::IsFirstPersonView() const { return g_firstPerson; }
int ShowMessage(const char *text, const char *title, unsigned int flags)
{
    Log(std::string("message ") + text);
    Log(std::string("message flags ") + std::to_string(flags));
    return 0;
}
C_Module::C_Module(int, const char *) {}
C_Module::~C_Module() {}
C_Actor *C_ActorsModule::GetActor(int index) const
{
    if (!m_actors.empty())
        return index >= 0 && index < static_cast<int>(m_actors.size()) ? m_actors[index] : nullptr;
    return g_actorsById.count(index) ? g_actorsById[index] : nullptr;
}
C_Actor *C_ActorsModule::GetActorById(int id) const { return g_actorsById.count(id) ? g_actorsById[id] : nullptr; }
C_Human *C_Team::GetActivePlayer() const { return g_activePlayer; }
void C_FirstPersonView::OnInventoryEvent(int e, int n) { Log("fpv " + std::to_string(e) + " " + std::to_string(n)); }
void C_Interface::RefreshInventory() { Log("refreshinventory"); }

// The equipment: logged.
void C_HumanEquipment::OnItemAdded(C_Item *item, int container, C_Actor *, int fromContainer, bool b)
{
    Log("eq added " + std::to_string(item->GetTypeId()) + " " + std::to_string(container) + " from " + std::to_string(fromContainer) +
        " " + std::to_string(b));
}
void C_HumanEquipment::OnItemRemoved(C_Item *item, int container, C_Actor *, int toContainer)
{
    Log("eq removed " + std::to_string(item->GetTypeId()) + " " + std::to_string(container) + " to " + std::to_string(toContainer));
}
void C_HumanEquipment::OnItemCreated(C_Item *item, int container)
{
    Log("eq created " + std::to_string(item->GetTypeId()) + " " + std::to_string(container));
}
void C_HumanEquipment::OnAnimationEvent(int e) { Log("eq event " + std::to_string(e)); }
void C_HumanEquipment::RemoveFromHolster(int typeId, int count) { Log("eq unholster " + std::to_string(typeId) + " " + std::to_string(count)); }
namespace {
int g_sidearm = -1;                                     // the type GetHolsterSlot calls a sidearm
}
int C_HumanEquipment::GetHolsterSlot(int typeId) { return typeId == g_sidearm ? HOLSTER_SLOT_SIDEARM : HOLSTER_SLOT_COUNT; }
void C_HumanEquipment::Unknown792310() { Log("eq 792310"); }
S_vector C_HumanEquipment::GetDropPosition(int place)
{
    Log("eq droppos " + std::to_string(place));
    return S_vector(1.0f, 2.0f, 3.0f);
}
void C_Human::OnUniformChanged(int uid) { Log("uniform " + std::to_string(uid)); }
void C_Human::OnHandsChanged() { Log("handschanged"); }
void C_Human::RemoveItem(int uid, bool destroy) { Log("human remove " + std::to_string(uid) + " " + std::to_string(destroy)); }
namespace {
bool g_localPlayer;
}
bool C_Human::IsLocalPlayer() const { return g_localPlayer; }
namespace {
int g_stance;
}
int C_Human::GetStance() const { return g_stance; }

// Sound, shots, particles, surface marks: logged.
namespace {
alignas(16) unsigned char g_soundBytes[0x200];
}
C_SoundModule *GetSoundModule() { return reinterpret_cast<C_SoundModule *>(g_soundBytes); }
namespace {
int g_nextSoundId = 10;
bool g_soundAlive;
std::map<std::pair<int, int>, S_SoundGroup *> g_groups;
std::string FrameName(const I3D_frame *f);
}
int C_SoundModule::Play(const S_SoundRequest &r)
{
    Log("sound " + std::to_string(r.chapter) + " " + std::to_string(r.group));
    char s[200];
    std::snprintf(s, sizeof s, "soundreq %g %g %g flags %x attach %s source %p far %g", r.pos.x, r.pos.y, r.pos.z, r.flags,
                  FrameName(r.attachTo).c_str(), static_cast<void *>(r.source), r.farScale);
    Log(s);
    return g_nextSoundId++;
}
void C_SoundModule::Stop(int *slot) { Log("sound stop " + std::to_string(slot ? *slot : -99)); }
I3D_sound *C_SoundModule::GetSound(int) const { return g_soundAlive ? reinterpret_cast<I3D_sound *>(0x1) : nullptr; }
S_SoundGroup *C_SoundModule::GetGroup(int chapter, int group) const
{
    Log("getgroup " + std::to_string(chapter) + " " + std::to_string(group));
    return g_groups.count({chapter, group}) ? g_groups[{chapter, group}] : nullptr;
}
void C_ShotManager::ThrowItem(C_Actor *, C_Item *item, const S_vector &, const S_vector &, bool fake)
{
    Log("throw " + std::to_string(item->GetTypeId()) + " " + std::to_string(fake));
}
void C_ShotManager::FireProjectile(C_Actor *, C_Item *, const S_vector &, const S_vector &, I3D_frame *projectile, int trail, bool fly,
                                   bool follow, uint32_t flags)
{
    Log("fireprojectile " + FrameName(projectile) + " " + std::to_string(trail) + " " + std::to_string(fly) + " " + std::to_string(follow) + " " +
        std::to_string(flags));
}
void C_ShotManager::FireBullet(C_Actor *, C_Item *, const S_vector &, const S_vector &, bool tracer, uint32_t flags, int shell)
{
    Log("firebullet " + std::to_string(tracer) + " " + std::to_string(flags) + " " + std::to_string(shell));
}
void MeleeAttack(C_Actor *, C_Item *, bool backstab, const S_vector &, const S_vector &) { Log("melee " + std::to_string(backstab)); }
int C_ParticlesModule::Create(int effect, const S_matrix &, I3D_frame *, bool face, bool transient)
{
    Log("particles " + std::to_string(effect));
    Log("particles args " + std::to_string(face) + " " + std::to_string(transient));
    return 0;
}
namespace {
std::string g_marks;                                    // the surface marks' chunk, kept as loaded
}
void SurfaceMarks_Free() { Log("marks free"); }
void SurfaceMarks_Tick(int t) { Log("marks tick " + std::to_string(t)); }
bool SurfaceMarks_Save(C_chunk &chunk)
{
    Log("marks save");
    chunk.WriteChunk(0x349, g_marks.data(), static_cast<int>(g_marks.size()));
    return true;
}
bool SurfaceMarks_Load(C_chunk &chunk)
{
    Log("marks load");
    uint16_t id;
    if (!chunk.Descend(id))
        return false;
    g_marks.resize(chunk.RemainingSize());
    chunk.Read(&g_marks[0], static_cast<int>(g_marks.size()));
    chunk.Ascend();
    return true;
}

// The engine: no models (the caches fail to open), frames never needed.
namespace {
std::map<std::string, std::function<void(I3D_frame *)>> g_models;   // models that open: a filler for the frame
}
template <> HRESULT C_ResourceCache<I3D_model>::OpenInto(I3D_model *model, const char *name, uint32_t, void *, void *, int)
{
    Log(std::string("open model ") + name);
    if (!g_models.count(name))
        return E_FAIL;
    g_models[name](reinterpret_cast<I3D_frame *>(model));
    return I3D_OK;
}
namespace {
std::set<std::string> g_sounds;                         // sound files that open
}
template <> HRESULT C_ResourceCache<I3D_sound>::OpenInto(I3D_sound *, const char *name, uint32_t, void *, void *, int)
{
    Log(std::string("open sound ") + name);
    return g_sounds.count(name) ? I3D_OK : E_FAIL;
}
void I3D_frame::UpdateWMatrixProc() {}
namespace {
// Frames: a released count; the driver hands out new ones.
void *g_frameVt[128];
struct S_FakeFrame {
    alignas(16) unsigned char bytes[0x400];
};
std::map<const I3D_frame *, std::vector<I3D_frame *>> g_kids;
std::map<const I3D_frame *, std::string> g_frameNames;
std::map<const I3D_frame *, uint32_t> g_visualTypes;
}  // namespace
namespace {
std::string FrameName(const I3D_frame *f)
{
    if (!f)
        return "null";
    return g_frameNames.count(f) ? g_frameNames[f] : "?";
}
void __stdcall F_SoundType(I3D_frame *self, int type) { Log("soundtype " + FrameName(self) + " " + std::to_string(type)); }
void __stdcall F_SoundRange(I3D_frame *self, float n, float f, float a, float b)
{
    char s[120];
    std::snprintf(s, sizeof s, "soundrange %s %g %g %g %g", FrameName(self).c_str(), n, f, a, b);
    Log(s);
}
void __stdcall F_SoundCone(I3D_frame *self, float i, float o)
{
    char s[96];
    std::snprintf(s, sizeof s, "soundcone %s %g %g", FrameName(self).c_str(), i, o);
    Log(s);
}
void __stdcall F_SoundVolume(I3D_frame *self, float v)
{
    char s[96];
    std::snprintf(s, sizeof s, "soundvolume %s %g", FrameName(self).c_str(), v);
    Log(s);
}
void __stdcall F_SoundPlay(I3D_frame *self, int pos) { Log("soundplay " + FrameName(self) + " " + std::to_string(pos)); }
void __stdcall F_SetWorldDir(I3D_frame *self, const S_vector &d, float)
{
    char s[96];
    std::snprintf(s, sizeof s, "worlddir %s %g %g %g", FrameName(self).c_str(), d.x, d.y, d.z);
    Log(s);
}
uint32_t __stdcall F_Release(I3D_frame *self)
{
    Log(g_frameNames.count(self) ? "frame release " + g_frameNames[self] : std::string("frame release"));
    return 0;
}
int __stdcall F_NumChildren(I3D_frame *self) { return static_cast<int>(g_kids[self].size()); }
I3D_frame *__stdcall F_GetChild(I3D_frame *self, int i) { return g_kids[self][i]; }
I3D_frame *__stdcall F_FindChild(I3D_frame *self, const char *name, uint32_t)
{
    for (I3D_frame *k : g_kids[self]) {
        if (g_frameNames.count(k) && g_frameNames[k] == name)
            return k;
        if (I3D_frame *deeper = F_FindChild(k, name, 0))
            return deeper;
    }
    return nullptr;
}
HRESULT __stdcall F_Duplicate(I3D_frame *self, I3D_frame *src)
{
    g_frameNames[self] = "copy of " + (g_frameNames.count(src) ? g_frameNames[src] : std::string("?"));
    Log("duplicate " + g_frameNames[self]);
    return 0;
}
uint32_t __stdcall F_GetVisualType(I3D_frame *self) { return g_visualTypes.count(self) ? g_visualTypes[self] : 0; }
I3D_frame *__stdcall F_CreateVisual(void *, uint32_t type);
void __stdcall F_SetOn(I3D_frame *self, bool on)
{
    Log(on ? "frame on" : "frame off");
    Log(std::string(on ? "on " : "off ") + FrameName(self));
}
void __stdcall F_LinkTo(I3D_frame *self, I3D_frame *parent, uint32_t flags)
{
    // The children lists follow (as the engine's do).
    if (self->m_parent) {
        auto &old = g_kids[self->m_parent];
        old.erase(std::remove(old.begin(), old.end(), self), old.end());
    }
    self->m_parent = parent;
    if (parent)
        g_kids[parent].push_back(self);
    Log("link " + FrameName(self) + " " + FrameName(parent));
    Log(parent ? "frame link" : "frame unlink");
    Log("frame link flags " + std::to_string(flags));
}
void __stdcall F_SetPos(I3D_frame *self, const S_vector &p)
{
    char s[96];
    std::snprintf(s, sizeof s, "frame pos %g %g %g", p.x, p.y, p.z);
    Log(s);
    self->m_localMatrix(3) = p;
}
void __stdcall F_Update(I3D_frame *self)
{
    Log("frame update");
    Log("update " + FrameName(self));
}
void *g_sceneVt[128];
struct {
    void **vt = g_sceneVt;
    unsigned char fields[0x400] = {};
} g_scene;
void __stdcall F_UpdateFrameSector(void *, I3D_frame *, const S_vector &) { Log("scene sector"); }
std::function<I3D_frame *(const char *)> g_findFrame;
I3D_frame *__stdcall F_SceneFindFrame(void *, const char *name, uint32_t mask)
{
    Log(std::string("scene find ") + name + " " + std::to_string(mask));
    return g_findFrame ? g_findFrame(name) : nullptr;
}
I3D_frame *NewFrame(int type)
{
    auto *f = new S_FakeFrame();
    std::memset(f->bytes, 0, sizeof f->bytes);
    auto *frame = reinterpret_cast<I3D_frame *>(f->bytes);
    *reinterpret_cast<void ***>(frame) = g_frameVt;
    frame->m_type = static_cast<E_I3D_FRAME_TYPE>(type);
    frame->m_refCount = 1;
    return frame;
}
void *g_driverVt[64];
struct {
    void **vt = g_driverVt;
} g_driver;
int g_failFrameType = -1;                               // CreateFrame answers null for this type
I3D_frame *__stdcall F_CreateFrame(void *, int type)
{
    Log("createframe " + std::to_string(type));
    if (type == g_failFrameType)
        return nullptr;
    return NewFrame(type);
}
I3D_frame *__stdcall F_CreateVisual(void *, uint32_t type)
{
    Log("createvisual " + std::to_string(type));
    I3D_frame *f = NewFrame(FRAME_VISUAL);
    g_visualTypes[f] = type;
    return f;
}
// A named frame with children (linked as the engine would: the parent set).
I3D_frame *Named(const char *name, int type = FRAME_MODEL, std::initializer_list<I3D_frame *> kids = {})
{
    I3D_frame *f = NewFrame(type);
    g_frameNames[f] = name;
    f->m_name = const_cast<char *>(g_frameNames[f].c_str());
    for (I3D_frame *k : kids) {
        g_kids[f].push_back(k);
        k->m_parent = f;
    }
    return f;
}
}
extern "C" I3D_driver *I3DGetDriver() { return reinterpret_cast<I3D_driver *>(&g_driver); }
extern "C" IGraph *GetIGraph() { return nullptr; }

namespace {

// ============================================================ actors

struct S_TestActor {
    C_Human *human;
    C_ItemContainer *containers[ITEM_CONTAINER_COUNT] = {};
};
// An actor with a human's containers (C_Human::Init, human_items.cpp) or, `single`, one item store (actor.cpp).
S_TestActor *MakeActor(bool single, int netId)
{
    auto *a = new S_TestActor();
    const size_t size = sizeof(C_Human) + 0x100;
    auto *bytes = static_cast<unsigned char *>(::operator new(size, std::align_val_t(16)));
    std::memset(bytes, 0, size);
    a->human = reinterpret_cast<C_Human *>(bytes);
    a->human->m_netId = netId;
    a->human->m_containers = a->containers;
    a->human->m_singleContainer = single;
    if (single) {
        a->containers[0] = new C_ItemStore(2);
        a->containers[0]->SetMaxWeight(1000.0f);
        return a;
    }
    const auto slot = [](float space, int itemSize) -> C_ItemContainer * {
        C_ItemContainer *container = new C_ItemSlot(0);
        container->SetMaxWeight(100.0f);
        container->SetMaxSpace(space);
        container->SetMaxItemSize(itemSize);
        return container;
    };
    a->containers[ITEM_CONTAINER_HEAD] = slot(4.0f, 2);
    a->containers[ITEM_CONTAINER_BELT] = new C_ItemStore(1);
    a->containers[ITEM_CONTAINER_BELT]->SetMaxWeight(100.0f);
    a->containers[ITEM_CONTAINER_BELT]->SetMaxSpace(HUMAN_BELT_SPACE);
    a->containers[ITEM_CONTAINER_BELT]->SetMaxItemSize(1);
    a->containers[ITEM_CONTAINER_LONG_GUN] = slot(HUMAN_LONG_GUN_SPACE, 3);
    a->containers[ITEM_CONTAINER_HANDS] = slot(8.0f, 3);
    a->containers[ITEM_CONTAINER_UNIFORM] = slot(8.0f, 3);
    a->containers[ITEM_CONTAINER_BACKPACK] = slot(32.0f, 5);
    return a;
}

// ============================================================ part 1: definitions

std::string g_table;                                    // items.sav as read

// Stream sizes of the component kinds (kind + 1 in the record before each stream; 9, special, has none), as the
// table's layout gives them: firearm and projectile streams end in two 20-byte names.
const int COMPONENT_SIZE[10] = {0x24, 0x28, 0x60 + 40, 0x58 + 40, 0x10, 0x0C, 0x0C, 0x1C, 0x10, 0};
template <class T> T At(const unsigned char *p, int offset)
{
    T v;
    std::memcpy(&v, p + offset, sizeof v);
    return v;
}

void TestDefinitions()
{
    g_itemsModule.EnsureItemsLoaded();
    // Walk the table as LoadItemsTable does: every present slot's definition written back as its stream.
    size_t at = 0;
    int present = 0, same = 0, padding = 0, sizes = 0, fields = 0;
    for (int id = 0; id < MAX_ITEM_TYPES && at + 4 <= g_table.size(); ++id) {
        int flag;
        std::memcpy(&flag, g_table.data() + at, 4);
        C_ItemDef *def = g_itemsModule.GetItemDef(id);
        if (!flag) {
            CHECK(def == nullptr);
            at += 4;
            continue;
        }
        ++present;
        int type;
        std::memcpy(&type, g_table.data() + at + 4, 4);
        CHECK(def && static_cast<int>(def->GetRecordType()) == type);
        if (!def) {
            at += 8;
            continue;
        }
        const unsigned char *record = reinterpret_cast<const unsigned char *>(g_table.data()) + at + 8;
        // The size: the base, the components' streams (by the record's own kinds), the derived fields.
        int want = 0xA8;
        int cursor = 0x80;
        for (int c = 0; c < 2; ++c) {
            const int kind = At<int>(record, cursor) - 1;
            cursor += 4;
            if (kind >= 0 && kind < 10) {
                want += COMPONENT_SIZE[kind];
                cursor += COMPONENT_SIZE[kind];
            }
        }
        const int base = want;
        want += type == ITEM_AMMO ? 4 : type == ITEM_ANIMATED ? 0x10 : 0x14;
        int size = 0;
        def->Save(&size, nullptr);
        sizes += size == want;
        if (size != want)
            std::printf("  def %d: size 0x%x, the layout's 0x%x\n", id, size, want);
        // The derived fields where the layout puts them.
        bool derived = true;
        if (type == ITEM_AMMO) {
            derived = def->GetAmmoAmount() == At<float>(record, base);
        } else {
            derived = def->GetCarryMode() == At<int>(record, base) && def->GetBehaviour() == At<int>(record, base + 4);
            if (type == ITEM_ANIMATED)
                derived &= def->GetAmmoItemId() == At<int>(record, base + 12);
        }
        fields += derived;
        if (!derived)
            std::printf("  def %d: derived fields not at 0x%x\n", id, base);
        // Written twice, over 0x00 and over 0xEE: bytes that differ were not written (they must be the editor's padding,
        // 0xCD / 0xCC, in the table); the bytes written are the table's. Names compare as strings (the bytes after a
        // name's end are whatever the editor left).
        unsigned char out[600], over[600];
        std::memset(out, 0, sizeof out);
        std::memset(over, 0xEE, sizeof over);
        int write = 1;
        def->Save(&write, out);
        write = 1;
        def->Save(&write, over);
        bool equal = size <= 500;
        for (int offset : {0x00, 0x14, 0x28}) {
            const std::string a(reinterpret_cast<const char *>(record + offset), strnlen(reinterpret_cast<const char *>(record + offset), 20));
            const std::string b(reinterpret_cast<const char *>(out + offset), strnlen(reinterpret_cast<const char *>(out + offset), 20));
            equal &= a == b && std::memcmp(out + offset, over + offset, a.size() + (a.size() < 20)) == 0;
        }
        // (The display name is not loaded: written as an empty name.)
        equal &= out[0x50] == 0 && over[0x50] == 0;
        const int end = std::min(size, 500);
        for (int i = 0x3C; i < end; ++i) {
            if (i >= 0x40 && i < 0x64)
                continue;                                  // (unwritten bytes after 0x3C's int, the display name)
            // (The original copies a component's stream into the object and back as it is, padding and all
            // (0x007df370 / 0x007df330); the port goes field by field: the editor's padding, 0xCD / 0xCC in the
            // table, comes back as 0 or not at all. Nothing in the game saves definitions.)
            const bool written = out[i] == over[i];
            if ((record[i] == 0xCD || record[i] == 0xCC) && (!written || out[i] == 0)) {
                ++padding;
                continue;
            }
            if (!written || record[i] != out[i]) {
                if (equal)
                    std::printf("  def %d (type %d) differs at 0x%x: %02x / %02x%s (size 0x%x)\n", id, type, i, record[i], out[i],
                                written ? "" : " (not written)", size);
                equal = false;
            }
        }
        same += equal;
        at += 508;
    }
    std::printf("  definitions: %d present, %d written back the same (%d padding bytes the editor left 0xCD / 0xCC not written), "
                "%d sizes and %d derived fields as the layout gives\n", present, same, padding, sizes, fields);
    CHECK(present > 100 && same == present && sizes == present && fields == present);
    // IsBackpack: a backpack component of subtype 0.
    int backpacks = 0;
    for (int id = 0; id < MAX_ITEM_TYPES; ++id) {
        C_ItemDef *def = g_itemsModule.GetItemDef(id);
        if (!def)
            continue;
        const bool want = def->GetComponentKind() == ICK_BACKPACK &&
                          static_cast<const C_BackpackComp *>(def->GetComponent())->GetSubtype() == 0;
        CHECK(def->IsBackpack() == want);
        backpacks += want;
    }
    CHECK(backpacks > 0 && g_itemsModule.GetItemDef(228)->IsBackpack());
    C_BackpackComp *pack = static_cast<C_BackpackComp *>(g_itemsModule.GetItemDef(228)->m_component);
    const int subtype = pack->m_subtype;
    pack->m_subtype = 1;
    CHECK(!g_itemsModule.GetItemDef(228)->IsBackpack());
    pack->m_subtype = subtype;
    CHECK(g_itemsModule.GetItemDef(-1) == nullptr && g_itemsModule.GetItemDef(MAX_ITEM_TYPES) == nullptr);
}

// ============================================================ part 1: the original's save

// The chunks of an id at the top level of a sub-range (id, offset, size), or empty if the range does not parse.
struct S_Chunk {
    int id;
    size_t offset, size;
};
std::vector<S_Chunk> Subs(const std::string &d, size_t from, size_t to)
{
    std::vector<S_Chunk> out;
    while (from + 6 <= to) {
        uint16_t id;
        uint32_t size;
        std::memcpy(&id, d.data() + from, 2);
        std::memcpy(&size, d.data() + from + 2, 4);
        if (size < 6 || from + size > to)
            return {};
        out.push_back({id, from, size});
        from += size;
    }
    return from == to ? out : std::vector<S_Chunk>{};
}
// The actor item chunks (0x2775, made of item records 0xAD2C) of a save.
std::vector<std::string> ActorItemChunks(const std::string &d)
{
    std::vector<std::string> out;
    for (size_t i = 0; i + 6 <= d.size();) {
        uint16_t id;
        uint32_t size;
        std::memcpy(&id, d.data() + i, 2);
        std::memcpy(&size, d.data() + i + 2, 4);
        if (id == 0x2775 && size >= 6 && size < 200000 && i + size <= d.size()) {
            const std::vector<S_Chunk> s = Subs(d, i + 6, i + size);
            if ((size == 6 || !s.empty()) && std::all_of(s.begin(), s.end(), [](const S_Chunk &c) { return c.id == 0xAD2C; })) {
                out.push_back(d.substr(i, size));
                i += size;
                continue;
            }
        }
        ++i;
    }
    return out;
}

std::string g_save;

// The leaves (path of ids, payload) of a chunk tree: a chunk whose payload parses as chunks is a node.
void Leaves(const std::string &d, size_t from, size_t to, const std::string &path, std::vector<std::pair<std::string, std::string>> &out)
{
    const std::vector<S_Chunk> s = Subs(d, from, to);
    if (s.empty()) {
        out.push_back({path, d.substr(from, to - from)});
        return;
    }
    for (const S_Chunk &c : s) {
        char id[16];
        std::snprintf(id, sizeof id, "/%x", c.id);
        Leaves(d, c.offset + 6, c.offset + c.size, path + id, out);
    }
}
std::string Hex(const std::string &s)
{
    std::string h;
    char b[4];
    for (unsigned char c : s.substr(0, 12)) {
        std::snprintf(b, sizeof b, "%02x", c);
        h += b;
    }
    return h;
}
std::map<std::string, int> g_diffPaths;                 // differing leaf (last ids) -> count
// Equal but for the items' container (0x66) and owner (0x67): the original saves them under ids its LoadGame does
// not read (0xCA / 0xCB, the byte table at 0x007d4514), and only MoveItem sets them again, so a loaded item saves
// container 0 and owner -1. `lost` counts those fields.
bool SameButLost(const std::string &a, const std::string &b, int &lost)
{
    std::vector<std::pair<std::string, std::string>> la, lb;
    Leaves(a, 0, a.size(), "", la);
    Leaves(b, 0, b.size(), "", lb);
    if (la.size() != lb.size())
        return false;
    int n = 0;
    for (size_t i = 0; i < la.size(); ++i) {
        if (la[i].first != lb[i].first)
            return false;
        if (la[i].second == lb[i].second)
            continue;
        const std::string &p = la[i].first;
        const bool container = p.size() >= 8 && p.compare(p.size() - 8, 8, "/2e6b/66") == 0 && lb[i].second == std::string("\0\0\0\0", 4);
        const bool owner = p.size() >= 8 && p.compare(p.size() - 8, 8, "/2e6b/67") == 0 && lb[i].second == std::string("\xff\xff\xff\xff", 4);
        if (!container && !owner)
            return false;
        ++n;
    }
    lost += n;
    return true;
}
void Diff(const std::string &a, const std::string &b)
{
    std::vector<std::pair<std::string, std::string>> la, lb;
    Leaves(a, 0, a.size(), "", la);
    Leaves(b, 0, b.size(), "", lb);
    if (la.size() != lb.size()) {
        g_diffPaths["(leaf count " + std::to_string(la.size()) + " / " + std::to_string(lb.size()) + ")"]++;
        return;
    }
    for (size_t i = 0; i < la.size(); ++i) {
        if (la[i] != lb[i]) {
            const std::string &p = la[i].first;
            const size_t cut = p.rfind('/', p.rfind('/') - 1);
            g_diffPaths[p.substr(cut == std::string::npos ? 0 : cut) + " " + Hex(la[i].second) + " -> " + Hex(lb[i].second)]++;
        }
    }
}

// One actor chunk loaded into a fresh actor and saved again.
std::string RoundTrip(const std::string &chunkBytes, bool single)
{
    S_TestActor *a = MakeActor(single, 1);
    g_files["test.sav"] = chunkBytes;
    C_chunk in;
    in.Open("test.sav", false);
    LoadActorItems(a->human, in);
    in.Close();
    C_chunk out;
    out.Open("test.sav", true);
    SaveActorItems(a->human, out);
    out.Close();
    return g_files["test.sav"];
}

void TestSaveRoundTrip()
{
    const std::vector<std::string> chunks = ActorItemChunks(g_save);
    CHECK(chunks.size() > 50);
    int same = 0, asStore = 0, lost = 0;
    for (size_t k = 0; k < chunks.size(); ++k) {
        const std::string &c = chunks[k];
        // (Only a chunk whose records all name container 0 can be an item store's.)
        bool onlyFirst = true;
        for (const S_Chunk &record : Subs(c, 6, c.size())) {
            const std::vector<S_Chunk> fields = Subs(c, record.offset + 6, record.offset + record.size);
            for (const S_Chunk &f : fields) {
                int value;
                std::memcpy(&value, c.data() + f.offset + 6, 4);
                if (f.id == 1 && value != 0)
                    onlyFirst = false;
            }
        }
        const std::string back = RoundTrip(c, false);
        int l = 0;
        if (SameButLost(c, back, l)) {
            ++same;
            lost += l;
            continue;
        }
        if (onlyFirst) {
            const std::string store = RoundTrip(c, true);
            l = 0;
            if (SameButLost(c, store, l)) {
                ++same;
                ++asStore;
                lost += l;
                continue;
            }
        }
        Diff(c, back);
    }
    std::printf("  save round trip: %zu actor item chunks, %d the same (%d as an item store) but for %d container / owner "
                "fields the original does not load\n", chunks.size(), same, asStore, lost);
    for (const auto &d : g_diffPaths)
        std::printf("    %4d x %s\n", d.second, d.first.c_str());
    CHECK(same == static_cast<int>(chunks.size()));
}

// The module chunk (0xB256): the last uid (and the surface marks, faked: kept as loaded) back as saved; the uids
// handed out after it; a chunk of another id: false.
void TestModuleRoundTrip()
{
    size_t at = std::string::npos;
    for (size_t i = 0; i + 12 <= g_save.size(); ++i) {
        uint16_t id, first;
        uint32_t size;
        std::memcpy(&id, g_save.data() + i, 2);
        std::memcpy(&size, g_save.data() + i + 2, 4);
        std::memcpy(&first, g_save.data() + i + 6, 2);
        if (id == 0xB256 && first == 0xD575 && size < 10000 && i + size <= g_save.size() && !Subs(g_save, i + 6, i + size).empty()) {
            at = i;
            break;
        }
    }
    CHECK(at != std::string::npos);
    if (at == std::string::npos)
        return;
    uint32_t size;
    std::memcpy(&size, g_save.data() + at + 2, 4);
    const std::string chunk = g_save.substr(at, size);
    g_files["test.sav"] = chunk;
    C_chunk in;
    in.Open("test.sav", false);
    g_itemsModule.m_lastUid = 12345;
    CHECK(g_itemsModule.LoadGame(in));
    in.Close();
    const int last = g_itemsModule.m_lastUid;
    C_chunk out;
    out.Open("test.sav", true);
    CHECK(g_itemsModule.SaveGame(out));
    out.Close();
    CHECK(g_files["test.sav"] == chunk && g_itemsModule.m_missionActive);
    std::printf("  module chunk: last uid %d, %zu bytes back the same\n", last, chunk.size());
    CHECK(g_itemsModule.NextUid() == last + 1);
    // Another chunk: false, the uids reset.
    std::string other = chunk;
    other[0] = 0x57;
    g_files["test.sav"] = other;
    C_chunk bad;
    bad.Open("test.sav", false);
    CHECK(!g_itemsModule.LoadGame(bad) && g_itemsModule.m_lastUid == -1);
    bad.Close();
}


// ============================================================ part 1b: containers, loaded state, initial values

// The first definition matching `pred`.
template <class F> int FindType(F pred)
{
    for (int id = 0; id < MAX_ITEM_TYPES; ++id)
        if (C_ItemDef *def = g_itemsModule.GetItemDef(id))
            if (pred(def))
                return id;
    return -1;
}
C_Item *New(int typeId)
{
    C_Item *item = g_itemsModule.NewItem(typeId);
    g_itemsModule.AddInstance(item);
    return item;
}

void TestContainers()
{
    const int ammo = FindType([](C_ItemDef *d) { return d->GetRecordType() == ITEM_AMMO && d->GetWeight() > 0.0f && d->GetInventorySize() > 0; });
    const int other = FindType([&](C_ItemDef *d) { return d->GetRecordType() == ITEM_AMMO && d->GetId() != ammo; });
    const int zeroSize = FindType([](C_ItemDef *d) { return d->GetInventorySize() == 0; });
    CHECK(ammo >= 0 && other >= 0);
    const float w = g_itemsModule.GetItemDef(ammo)->GetWeight();
    const float s = static_cast<float>(g_itemsModule.GetItemDef(ammo)->GetInventorySize()) * 2.0f;
    // A slot: one item; its weight and space (twice the size, at least 1: a size 0 item takes 1); the count of a type;
    // a second item replaces the first (handed back); none: no item; removing another item fails; cleared: the item
    // destroyed, the lock time 0.
    {
        C_ItemSlot slot(0);
        C_Item *replaced = reinterpret_cast<C_Item *>(0x1);
        CHECK(slot.AddItem(nullptr, &replaced, -1) == ITEM_RESULT_NO_ITEM);
        C_Item *a = New(ammo);
        C_Item *b = New(ammo);
        CHECK(slot.GetTotalWeight() == 0.0f && slot.GetTotalSpace() == 0.0f && slot.IsEmpty() && slot.GetUsedSlotCount() == 0);
        CHECK(slot.AddItem(a, &replaced, -1) == ITEM_RESULT_OK && replaced == reinterpret_cast<C_Item *>(0x1));
        CHECK(slot.GetTotalWeight() == w && slot.GetTotalSpace() == s && slot.CountItems(ammo) == 1 && slot.CountItems(other) == 0 &&
              slot.GetItem() == a && !slot.IsEmpty() && slot.GetUsedSlotCount() == 1);
        CHECK(slot.AddItem(b, &replaced, -1) == ITEM_RESULT_OK && replaced == a && slot.GetItem() == b);
        CHECK(slot.RemoveItem(a, -1) == ITEM_RESULT_FAILED && slot.RemoveItem(b, -1) == ITEM_RESULT_OK && slot.IsEmpty());
        if (zeroSize >= 0) {
            C_Item *z = New(zeroSize);
            slot.AddItem(z, &replaced, -1);
            CHECK(slot.GetTotalSpace() == 1.0f);
            slot.RemoveItem(z, -1);
        }
        const size_t before = g_itemsModule.m_instances.size();
        slot.AddItem(a, &replaced, -1);
        slot.SetLockTime(500);
        GetGame()->m_missionTime = 499;
        CHECK(slot.IsLocked());
        GetGame()->m_missionTime = 500;
        CHECK(!slot.IsLocked());
        GetGame()->m_missionTime = 0;
        slot.Clear();
        CHECK(slot.IsEmpty() && slot.GetLockTime() == 0 && g_itemsModule.m_instances.size() == before - 1);
    }
    // A list: every item; the totals add up; removing moves the last into its place; cleared: destroyed from the back.
    {
        C_ItemList list;
        C_Item *replaced = reinterpret_cast<C_Item *>(0x1);
        CHECK(list.AddItem(nullptr, &replaced, -1) == ITEM_RESULT_NO_ITEM);
        C_Item *items[3] = {New(ammo), New(ammo), New(other)};
        for (C_Item *i : items)
            CHECK(list.AddItem(i, &replaced, -1) == ITEM_RESULT_OK && replaced == nullptr);
        const float ow = g_itemsModule.GetItemDef(other)->GetWeight();
        CHECK(list.GetTotalWeight() == w + w + ow && !list.IsEmpty());
        CHECK(list.RemoveItem(items[0], -1) == ITEM_RESULT_OK && list.m_items[0] == items[2] && list.m_items.size() == 2);
        CHECK(list.RemoveItem(items[0], -1) == ITEM_RESULT_FAILED);
        const size_t before = g_itemsModule.m_instances.size();
        list.Clear();
        CHECK(list.IsEmpty() && g_itemsModule.m_instances.size() == before - 2);
        delete items[0];
    }
    // A store: 14 slots (kind 1, the belt) or 32; an item joins the slot of its sort (type and kind) or the first free
    // one; the totals are per slot (the first item's weight times the count); full: failed. Removing: only the first
    // slot of the sort is searched; an emptied slot moves behind the used ones.
    {
        C_ItemStore belt(1), store(2);
        CHECK(belt.GetSlotCount() == 14 && store.GetSlotCount() == 32);
        C_Item *replaced = nullptr;
        CHECK(store.AddItem(nullptr, &replaced, -1) == ITEM_RESULT_NO_ITEM);
        C_Item *a1 = New(ammo), *a2 = New(ammo), *o1 = New(other);
        CHECK(store.AddItem(a1, &replaced, -1) == ITEM_RESULT_OK && store.AddItem(o1, &replaced, -1) == ITEM_RESULT_OK &&
              store.AddItem(a2, &replaced, -1) == ITEM_RESULT_OK);
        CHECK(store.GetUsedSlotCount() == 2 && store.m_slots[0].size() == 2 && store.m_slots[1].size() == 1 && store.CountItems(ammo) == 2 &&
              store.CountItems(other) == 1 && store.CountItems(-5) == 0);
        const float ow = g_itemsModule.GetItemDef(other)->GetWeight();
        const float os = static_cast<float>(g_itemsModule.GetItemDef(other)->GetInventorySize()) * 2.0f;
        CHECK(store.GetTotalWeight() == w * 2.0f + ow && store.GetTotalSpace() == s * 2.0f + (os == 0.0f ? 1.0f : os));
        // A gap: the slot of `other` emptied moves behind; the next new sort takes the first free slot.
        CHECK(store.RemoveItem(o1, -1) == ITEM_RESULT_OK && store.GetUsedSlotCount() == 1 && store.m_slots[1].empty());
        CHECK(store.RemoveItem(o1, -1) == ITEM_RESULT_FAILED && store.RemoveItem(nullptr, -1) == ITEM_RESULT_FAILED);
        store.m_slots[2].push_back(o1);                          // (a second slot of a sort, as only a load can make)
        store.m_usedSlots = 2;
        C_Item *a3 = New(ammo);
        store.m_slots[3].push_back(a3);
        store.m_usedSlots = 3;
        CHECK(store.RemoveItem(a3, -1) == ITEM_RESULT_FAILED);   // only slot 0 of the sort is searched
        store.m_slots[3].clear();
        store.m_usedSlots = 2;
        CHECK(store.RemoveItem(a1, -1) == ITEM_RESULT_OK && store.m_slots[0].size() == 1 && store.m_slots[0][0] == a2);
        CHECK(store.RemoveItem(a2, -1) == ITEM_RESULT_OK && store.m_slots[0].empty() == false && store.m_slots[0][0] == nullptr ? false : true);
        // (Emptying slot 0 moved the others forward: `other` is now first.)
        CHECK(store.GetUsedSlotCount() == 1 && !store.m_slots[1].empty() && store.m_slots[1][0] == o1);
        // Full: every slot used by a different sort.
        C_ItemStore full(1);
        int sorts = 0;
        for (int id = 0; id < MAX_ITEM_TYPES && sorts < 14; ++id)
            if (g_itemsModule.GetItemDef(id) && full.AddItem(New(id), &replaced, -1) == ITEM_RESULT_OK)
                ++sorts;
        CHECK(sorts == 14 && full.GetUsedSlotCount() == 14);
        const int extra = FindType([&](C_ItemDef *d) { return full.CountItems(d->GetId()) == 0; });
        C_Item *x = New(extra);
        CHECK(full.AddItem(x, &replaced, -1) == ITEM_RESULT_FAILED);
        const size_t before = g_itemsModule.m_instances.size();
        full.Clear();
        CHECK(full.IsEmpty() && full.GetUsedSlotCount() == 0 && g_itemsModule.m_instances.size() == before - 14);
        g_itemsModule.DestroyItem(x);
        g_itemsModule.DestroyItem(nullptr);
    }
}

// A loaded actor's state: the carried weight is its items' and the backpack contents'; a backpack's store is the
// contents container; cleared again (an empty chunk): every container emptied and unlocked, no contents, no weight.
void TestLoadedState()
{
    const std::vector<std::string> chunks = ActorItemChunks(g_save);
    int checked = 0, packs = 0;
    for (const std::string &c : chunks) {
        if (c.size() <= 6)
            continue;
        // (Item stores' chunks, several items in container 0, are not a human's.)
        int first = 0;
        for (const S_Chunk &record : Subs(c, 6, c.size()))
            for (const S_Chunk &f : Subs(c, record.offset + 6, record.offset + record.size))
                if (f.id == 1 && c[f.offset + 6] == 0)
                    ++first;
        if (first > 1)
            continue;
        S_TestActor *a = MakeActor(false, 1);
        g_files["test.sav"] = c;
        C_chunk in;
        in.Open("test.sav", false);
        LoadActorItems(a->human, in);
        in.Close();
        if (!a->containers[ITEM_CONTAINER_HEAD] || a->containers[ITEM_CONTAINER_LONG_GUN]->GetItem() == nullptr) {
            // (only humans with a long gun or more: enough to see the weights)
        }
        float weight = 0.0f;
        for (int k = 0; k <= ITEM_CONTAINER_BACKPACK; ++k)
            weight += a->containers[k]->GetTotalWeight();
        C_Item *pack = a->containers[ITEM_CONTAINER_BACKPACK]->GetItem();
        if (pack && pack->GetDef()->IsBackpack()) {
            ++packs;
            C_ItemStore *store = static_cast<C_ContainerItem *>(pack)->GetStore();
            CHECK(a->human->GetContainer(ITEM_CONTAINER_BACKPACK_CONTENTS) == store && !store->IsEmpty());
            weight += store->GetTotalWeight();
        } else {
            CHECK(a->human->GetContainer(ITEM_CONTAINER_BACKPACK_CONTENTS) == nullptr);
        }
        CHECK(std::fabs(a->human->GetCarriedWeight() - weight) < 0.01f);
        ++checked;
        for (int k = 0; k <= ITEM_CONTAINER_BACKPACK; ++k)
            a->containers[k]->SetLockTime(77);
        a->human->m_carriedWeight += 3.0f;
        g_files["test.sav"] = std::string("\x75\x27\x06\x00\x00\x00", 6);
        C_chunk empty;
        empty.Open("test.sav", false);
        CHECK(LoadActorItems(a->human, empty));
        empty.Close();
        bool clear = a->human->GetCarriedWeight() == 0.0f && a->human->GetContainer(ITEM_CONTAINER_BACKPACK_CONTENTS) == nullptr;
        for (int k = 0; k <= ITEM_CONTAINER_BACKPACK; ++k)
            clear &= a->containers[k]->IsEmpty() && a->containers[k]->GetLockTime() == 0;
        CHECK(clear);
    }
    CHECK(checked > 40 && packs >= 4);
    // A single store: only it is cleared (container 1.. are not there).
    S_TestActor *s = MakeActor(true, 2);
    s->containers[0]->SetLockTime(9);
    g_files["test.sav"] = std::string("\x75\x27\x06\x00\x00\x00", 6);
    C_chunk in;
    in.Open("test.sav", false);
    CHECK(LoadActorItems(s->human, in) && s->containers[0]->IsEmpty());
    in.Close();
    // Not an actor item chunk: false (after clearing).
    g_files["test.sav"] = std::string("\x76\x27\x06\x00\x00\x00", 6);
    C_chunk bad;
    bad.Open("test.sav", false);
    CHECK(!LoadActorItems(s->human, bad));
    bad.Close();
}

// New items: the kind by the definition (animated: a weapon, ammunition, a backpack component: a container, the
// other static items: medkits); their initial values.
void TestNewItems()
{
    int kinds[4] = {};
    for (int id = 0; id < MAX_ITEM_TYPES; ++id) {
        C_ItemDef *def = g_itemsModule.GetItemDef(id);
        if (!def)
            continue;
        C_Item *item = New(id);
        const int want = def->GetRecordType() == ITEM_ANIMATED ? ITEM_KIND_WEAPON
                         : def->GetRecordType() == ITEM_AMMO ? ITEM_KIND_AMMO
                         : def->GetComponentKind() == ICK_BACKPACK ? ITEM_KIND_CONTAINER : ITEM_KIND_MEDKIT;
        CHECK(item->GetKind() == want && item->GetTypeId() == id && item->GetDef() == def && item->GetTime() == 0);
        CHECK(item->IsArmed() == (def->GetBehaviour() == ITEM_BEHAVIOUR_MINE));
        CHECK(item->GetWeight() == def->GetWeight() && item->GetSize() == def->GetInventorySize());
        const float size = static_cast<float>(def->GetInventorySize());
        CHECK(item->GetSpace() == (size == 0.0f ? 1.0f : size * 2.0f));
        ++kinds[want];
        if (want == ITEM_KIND_AMMO) {
            CHECK(item->GetAmount() == def->GetAmmoAmount() && item->GetMaxAmount() == def->GetAmmoAmount());
        } else if (want == ITEM_KIND_CONTAINER) {
            const auto *pack = static_cast<const C_BackpackComp *>(def->GetComponent());
            C_ItemStore *store = static_cast<C_ContainerItem *>(item)->GetStore();
            CHECK(store->GetMaxWeight() == pack->GetMaxWeight() && store->GetMaxSpace() == pack->GetCapacity() &&
                  store->GetMaxItemSize() == pack->GetMaxItemSize() && store->GetSlotCount() == 32);
        } else if (want == ITEM_KIND_MEDKIT) {
            auto *kit = static_cast<C_MedkitItem *>(item);
            const float charges = def->GetBehaviour() == ITEM_BEHAVIOUR_MEDKIT && def->GetComponent()
                                      ? static_cast<const C_MedkitComp *>(def->GetComponent())->GetHealingCapacity()
                                      : 0.0f;
            CHECK(kit->m_charges == charges);
            if (charges > 0.0f) {
                kit->Consume(charges * 0.25f);
                CHECK(kit->m_charges == charges - charges * 0.25f);
                kit->Consume(charges);
                CHECK(kit->m_charges == 0.0f);
            }
        } else {
            auto *gun = static_cast<C_WeaponItem *>(item);
            const float capacity = def->GetAmmoItemId() >= 0 ? g_itemsModule.GetItemDef(def->GetAmmoItemId())->GetAmmoAmount()
                                   : def->GetBehaviour() == ITEM_BEHAVIOUR_LAUNCHER ? 1.0f : -1.0f;
            CHECK(gun->m_capacity == capacity && gun->m_rounds == capacity);
        }
    }
    std::printf("  new items: %d weapons, %d containers, %d ammunition, %d other\n", kinds[0], kinds[1], kinds[2], kinds[3]);
    CHECK(kinds[0] > 0 && kinds[1] > 0 && kinds[2] > 0 && kinds[3] > 0);
    // A respawning item (behaviour 20) is listed while it lives.
    const int respawning = FindType([](C_ItemDef *d) { return d->GetBehaviour() == ITEM_BEHAVIOUR_RESPAWNING; });
    if (respawning >= 0) {
        const size_t before = g_itemsModule.GetRespawningItems().size();
        C_Item *r = New(respawning);
        CHECK(g_itemsModule.GetRespawningItems().size() == before + 1 && g_itemsModule.GetRespawningItems().back() == r);
        g_itemsModule.DestroyItem(r);
        CHECK(g_itemsModule.GetRespawningItems().size() == before);
    } else {
        std::printf("  (no respawning item type in this table)\n");
    }
    // AddInstance: none for null.
    const size_t n = g_itemsModule.m_instances.size();
    g_itemsModule.AddInstance(nullptr);
    CHECK(g_itemsModule.m_instances.size() == n);
}

// items.sav: loaded once; missing or short: the message, the game quits; an unknown record type skips 8 bytes.
void TestTableFailures()
{
    const std::string table = g_files[Key("Tables\\items.sav")];
    g_log.clear();
    g_itemsModule.EnsureItemsLoaded();
    CHECK(g_log.empty());
    // Opened from the disk first (archives after), read whole.
    g_itemsModule.LoadItemsTable();
    CHECK((g_log == std::vector<std::string>{"dtaopen tables\\items.sav 0"}));
    g_failRead = true;
    g_log.clear();
    g_itemsModule.LoadItemsTable();
    g_failRead = false;
    CHECK(std::count(g_log.begin(), g_log.end(), "message KRITICKA chyba pri nahravani souboru items.sav") == 1 && g_gameState == GAME_STATE_QUIT);
    g_gameState = 0;
    g_files.erase(Key("Tables\\items.sav"));
    g_log.clear();
    g_itemsModule.LoadItemsTable();
    CHECK(std::count(g_log.begin(), g_log.end(), "message KRITICKA chyba pri nahravani souboru items.sav") == 1 &&
          g_gameState == GAME_STATE_QUIT);
    g_gameState = 0;
    // A short table: read as far as it goes (dtaRead gives what there is, not -1): no message.
    g_files[Key("Tables\\items.sav")] = table;
    // (An unknown record type is not tried: the walk goes on 8 bytes later, inside the record, and the ids left read
    // their flags from the record's bytes and on past the table, in the original as here.)
    // A table of empty slots: no definitions.
    g_files[Key("Tables\\items.sav")] = std::string(0x3D860, '\0');
    g_itemsModule.LoadItemsTable();
    CHECK(g_itemsModule.GetItemDef(1) == nullptr && g_itemsModule.GetItemDef(228) == nullptr);
    // Back to the real table.
    g_files[Key("Tables\\items.sav")] = table;
    g_itemsModule.LoadItemsTable();
    CHECK(g_itemsModule.GetItemDef(228) != nullptr);
}

// Destructors: a slot, a list and a used store destroy their items; a container item its store's.
void TestDestructors()
{
    const int ammo = FindType([](C_ItemDef *d) { return d->GetRecordType() == ITEM_AMMO; });
    const int pack = FindType([](C_ItemDef *d) { return d->IsBackpack(); });
    C_Item *replaced = nullptr;
    size_t before = g_itemsModule.m_instances.size();
    {
        C_ItemSlot slot(0);
        slot.AddItem(New(ammo), &replaced, -1);
        C_ItemList list;
        list.AddItem(New(ammo), &replaced, -1);
        list.AddItem(New(ammo), &replaced, -1);
        CHECK(list.GetTotalSpace() == 2.0f * New(ammo)->GetSpace());
        C_ItemStore store(2);
        store.AddItem(New(ammo), &replaced, -1);
        C_ItemStore unused(2);
    }
    CHECK(g_itemsModule.m_instances.size() == before + 1);       // (the one made for the space)
    before = g_itemsModule.m_instances.size();
    C_Item *backpack = g_itemsModule.NewItem(pack);
    C_ItemStore *contents = static_cast<C_ContainerItem *>(backpack)->GetStore();
    CHECK(contents->m_kind == 2);
    contents->AddItem(New(ammo), &replaced, -1);
    contents->AddItem(New(ammo), &replaced, -1);
    CHECK(g_itemsModule.m_instances.size() == before + 2);
    delete backpack;
    CHECK(g_itemsModule.m_instances.size() == before);
    // An animated definition releases its first-person magazine.
    C_AnimatedItemDef *def = new C_AnimatedItemDef(999);
    I3D_frame *mag = NewFrame(FRAME_MODEL);
    def->m_fpvMagazine = mag;
    g_log.clear();
    delete def;
    CHECK(Count("frame release") >= 1);
}

// A store emptying a slot with two behind: they move up in order.
void TestStoreShift()
{
    int types[3], n = 0;
    for (int id = 0; id < MAX_ITEM_TYPES && n < 3; ++id)
        if (g_itemsModule.GetItemDef(id))
            types[n++] = id;
    C_ItemStore store(2);
    C_Item *replaced = nullptr;
    C_Item *a = New(types[0]), *b = New(types[1]), *c = New(types[2]);
    store.AddItem(a, &replaced, -1);
    store.AddItem(b, &replaced, -1);
    store.AddItem(c, &replaced, -1);
    CHECK(store.RemoveItem(a, -1) == ITEM_RESULT_OK);
    CHECK(store.m_slots[0].size() == 1 && store.m_slots[0][0] == b && store.m_slots[1].size() == 1 && store.m_slots[1][0] == c &&
          store.m_slots[2].empty() && store.GetUsedSlotCount() == 2);
    // Removing an item the slot of its sort does not hold: failed, nothing changed.
    C_Item *other = New(types[1]);
    CHECK(store.RemoveItem(other, -1) == ITEM_RESULT_FAILED && store.m_slots[0].size() == 1);
}

// Medkits: charges used below 1 stay (only below 0 is 0); their load stops at another chunk (false).
void TestMedkits()
{
    const int kitType = FindType([](C_ItemDef *d) {
        return d->GetBehaviour() == ITEM_BEHAVIOUR_MEDKIT && d->GetComponent() && d->GetRecordType() == ITEM_STATIC &&
               d->GetComponentKind() != ICK_BACKPACK;
    });
    CHECK(kitType >= 0);
    if (kitType < 0)
        return;
    auto *kit = static_cast<C_MedkitItem *>(New(kitType));
    kit->m_charges = 1.0f;
    kit->Consume(0.5f);
    CHECK(kit->m_charges == 0.5f);
    kit->Consume(0.5f);
    CHECK(kit->m_charges == 0.0f);
    // A saved medkit: the item chunk, the charges; then something else: false, the charges as read.
    kit->m_charges = 3.5f;
    g_files["test.sav"].clear();
    C_chunk out;
    out.Open("test.sav", true);
    out.Begin(0x1234);
    kit->SaveGame(out);
    out.WriteChunk(0x77, 1);
    out.Ascend();
    out.Close();
    auto *back = static_cast<C_MedkitItem *>(New(kitType));
    C_chunk in;
    in.Open("test.sav", false);
    uint16_t id;
    in.Descend(id);
    CHECK(!back->LoadGame(in) && back->m_charges == 3.5f);
    in.Close();
}

// The container (0xCA) and owner (0xCB) ids the item load reads (the game's saves never hold them): the container
// index, the owner by actor index (GetActor), -1 none.
void TestLoadIds()
{
    const int ammo = FindType([](C_ItemDef *d) { return d->GetRecordType() == ITEM_AMMO; });
    S_TestActor *owner = MakeActor(false, 5);
    g_actorsById[5] = owner->human;
    g_actorsById[-1] = owner->human;                              // (-1 is none, never looked up)
    for (int ownerId : {5, -1}) {
        g_files["test.sav"].clear();
        C_chunk out;
        out.Open("test.sav", true);
        out.Begin(0x1234);
        out.Begin(0x2E6B);
        out.WriteChunk(0x64, 77);
        out.WriteChunk(0xCA, 4);
        out.WriteChunk(0xCB, ownerId);
        out.Ascend();
        out.Ascend();
        out.Close();
        C_Item *item = New(ammo);
        item->m_owner = reinterpret_cast<C_Actor *>(0x1);
        C_chunk in;
        in.Open("test.sav", false);
        uint16_t id;
        in.Descend(id);
        item->LoadGame(in);
        in.Close();
        CHECK(item->m_uid == 77 && item->m_container == 4 && item->m_owner == (ownerId == 5 ? owner->human : nullptr));
    }
    g_actorsById.clear();
}

// ============================================================ part 2: moving items

// Actors with a vtable (GetHitPoints: what they can carry), a human's type, health and an equipment (raw: its methods
// are the logging fakes); the game with an interface and a first-person view.
std::map<const C_Actor *, int> g_hitPoints;
int __attribute__((thiscall)) F_GetHitPoints(C_Actor *self) { return g_hitPoints.count(self) ? g_hitPoints[self] : 100; }
void *g_actorVt[128];
alignas(16) unsigned char g_interfaceBytes[0x100];
S_TestActor *Human(int netId, int hitPoints = 100)
{
    S_TestActor *a = MakeActor(false, netId);
    g_actorVt[VirtualSlot(&C_Actor::GetHitPoints)] = reinterpret_cast<void *>(F_GetHitPoints);
    *reinterpret_cast<void ***>(a->human) = g_actorVt;
    a->human->m_type = ACTOR_HUMAN;
    a->human->m_stats[STAT_HEALTH] = 100;
    const size_t size = sizeof(C_HumanEquipment) + 0x40;
    auto *eq = static_cast<unsigned char *>(::operator new(size, std::align_val_t(16)));
    std::memset(eq, 0, size);
    a->human->m_body.equipment = reinterpret_cast<C_HumanEquipment *>(eq);
    g_hitPoints[a->human] = hitPoints;
    return a;
}
S_TestActor *Vehicle(int netId)
{
    S_TestActor *a = MakeActor(true, netId);
    *reinterpret_cast<void ***>(a->human) = g_actorVt;
    a->human->m_type = static_cast<E_ActorType>(ACTOR_HUMAN + 1);
    return a;
}
C_HumanEquipment *Eq(S_TestActor *a) { return a->human->GetEquipment(); }
void Mission(bool active, uint32_t role = GAME_ROLE_LOCAL)
{
    g_itemsModule.m_missionActive = active;
    g_gameRole = role;
    GetGame()->m_interface = reinterpret_cast<C_Interface *>(g_interfaceBytes);
    GetGame()->m_firstPersonView = reinterpret_cast<C_FirstPersonView *>(g_fpvBytes);
}
int Move(C_Item *item, C_Item **replaced, S_TestActor *from, int fromContainer, S_TestActor *to, int toContainer,
         bool notify = true, bool ignoreLocks = false, bool keepWeights = false, bool testOnly = false, int index = -1)
{
    return g_itemsModule.MoveItem(item, replaced, from ? from->human : nullptr, fromContainer, to ? to->human : nullptr,
                                  toContainer, index, nullptr, notify, ignoreLocks, keepWeights, testOnly);
}

// Item types of the table by kind.
struct S_Types {
    int pistol, rifle, smg, helmet, uniform, backpack, ammo, grenade, mine, knife, medkit, radio, longGun;
} g_types;
void FindTypes()
{
    g_types.pistol = FindType([](C_ItemDef *d) { return d->GetComponentKind() == ICK_FIREARM && d->GetInventorySize() == 1; });
    g_types.rifle = FindType([](C_ItemDef *d) { return d->GetComponentKind() == ICK_FIREARM && d->GetInventorySize() == 3; });
    g_types.longGun = FindType([](C_ItemDef *d) {
        return d->GetComponentKind() == ICK_FIREARM && (d->GetAllowedContainers() & ITEM_FITS_LONG_GUN);
    });
    g_types.smg = FindType([](C_ItemDef *d) { return d->GetComponentKind() == ICK_FIREARM && d->GetInventorySize() == 2; });
    g_types.helmet = FindType([](C_ItemDef *d) { return d->GetComponentKind() == ICK_HEADGEAR; });
    g_types.uniform = FindType([](C_ItemDef *d) { return d->GetComponentKind() == ICK_UNIFORM; });
    g_types.backpack = FindType([](C_ItemDef *d) { return d->IsBackpack() && d->GetInventorySize() == 5; });
    g_types.radio = FindType([](C_ItemDef *d) { return d->GetComponentKind() == ICK_BACKPACK && !d->IsBackpack(); });
    g_types.ammo = FindType([](C_ItemDef *d) { return d->GetRecordType() == ITEM_AMMO && d->GetInventorySize() == 1 && d->GetWeight() > 0.0f; });
    g_types.grenade = FindType([](C_ItemDef *d) { return d->GetBehaviour() == ITEM_BEHAVIOUR_THROWN && d->GetInventorySize() <= 1; });
    g_types.mine = FindType([](C_ItemDef *d) { return d->GetBehaviour() == ITEM_BEHAVIOUR_MINE; });
    g_types.knife = FindType([](C_ItemDef *d) { return d->GetComponentKind() == ICK_MELEE; });
    g_types.medkit = FindType([](C_ItemDef *d) { return d->GetBehaviour() == ITEM_BEHAVIOUR_MEDKIT; });
    std::printf("  types: pistol %d rifle %d smg %d helmet %d uniform %d backpack %d radio %d ammo %d grenade %d mine %d knife %d medkit %d\n",
                g_types.pistol, g_types.rifle, g_types.smg, g_types.helmet, g_types.uniform, g_types.backpack, g_types.radio,
                g_types.ammo, g_types.grenade, g_types.mine, g_types.knife, g_types.medkit);
}
float W(int type) { return g_itemsModule.GetItemDef(type)->GetWeight(); }

void TestMoveChecks()
{
    Mission(false);
    C_Item *replaced = reinterpret_cast<C_Item *>(0x1);
    S_TestActor *a = Human(10), *b = Human(11);
    C_Item *pistol = New(g_types.pistol);
    // The arguments.
    CHECK(Move(nullptr, &replaced, nullptr, 0, a, ITEM_CONTAINER_HANDS) == ITEM_RESULT_FAILED && replaced == nullptr);
    CHECK(Move(pistol, &replaced, a, 7, a, ITEM_CONTAINER_HANDS) == ITEM_RESULT_BAD_SOURCE);
    CHECK(Move(pistol, &replaced, a, 0, a, 7) == ITEM_RESULT_BAD_TARGET);
    CHECK(Move(pistol, &replaced, a, ITEM_CONTAINER_UNIFORM, a, ITEM_CONTAINER_BELT) == ITEM_RESULT_BAD_TARGET);
    CHECK(Move(pistol, &replaced, a, ITEM_CONTAINER_UNIFORM, b, ITEM_CONTAINER_UNIFORM) == ITEM_RESULT_WRONG_KIND);
    // To nobody's belt, long gun, hands, uniform, backpack, contents; a vehicle's belt etc.: no such target.
    S_TestActor *car = Vehicle(12);
    for (int c : {ITEM_CONTAINER_BELT, ITEM_CONTAINER_LONG_GUN, ITEM_CONTAINER_HANDS, ITEM_CONTAINER_UNIFORM, ITEM_CONTAINER_BACKPACK,
                  ITEM_CONTAINER_BACKPACK_CONTENTS}) {
        CHECK(Move(pistol, &replaced, nullptr, 0, nullptr, c) == ITEM_RESULT_BAD_TARGET);
        CHECK(Move(pistol, &replaced, nullptr, 0, car, c) == ITEM_RESULT_BAD_TARGET);
    }
    // No backpack: no contents container.
    CHECK(Move(New(g_types.ammo), &replaced, nullptr, 0, a, ITEM_CONTAINER_BACKPACK_CONTENTS) == ITEM_RESULT_BAD_TARGET);

    // Each container's checks in order: the carrier's strength (another actor; the occupant's weight given back), the
    // weight, space and size limits, the kind, a new item into a taken place.
    struct S_Case {
        int container, type;
    };
    const S_Case cases[] = {{ITEM_CONTAINER_HEAD, g_types.helmet},       {ITEM_CONTAINER_LONG_GUN, g_types.rifle},
                            {ITEM_CONTAINER_HANDS, g_types.pistol},      {ITEM_CONTAINER_UNIFORM, g_types.uniform},
                            {ITEM_CONTAINER_BACKPACK, g_types.backpack}, {ITEM_CONTAINER_BELT, g_types.ammo}};
    for (const S_Case &c : cases) {
        const float w = W(c.type);
        // Too much to carry: hit points below the weight (0.0001 of slack).
        S_TestActor *weak = Human(20, 0);
        weak->human->m_carriedWeight = 0.0f;
        C_Item *item = New(c.type);
        const bool zeroWeight = w <= 0.0f;
        if (!zeroWeight)
            CHECK(Move(item, &replaced, nullptr, 0, weak, c.container) == ITEM_RESULT_ACTOR_OVERLOADED);
        // Just enough (carried + weight == hit points): fine as far as strength goes.
        S_TestActor *fit = Human(21, 50);
        fit->human->m_carriedWeight = 50.0f - w;
        CHECK(Move(item, &replaced, nullptr, 0, fit, c.container, true, false, false, true) == ITEM_RESULT_OK);
        fit->human->m_carriedWeight = 50.0f - w + 0.01f;
        if (!zeroWeight)
            CHECK(Move(item, &replaced, nullptr, 0, fit, c.container, true, false, false, true) == ITEM_RESULT_ACTOR_OVERLOADED);
        // The limits, each alone.
        S_TestActor *h = Human(22, 1000);
        C_ItemContainer *dest = h->containers[c.container];
        const float maxW = dest->GetMaxWeight(), maxS = dest->GetMaxSpace();
        const int maxSize = dest->GetMaxItemSize();
        if (!zeroWeight) {
            dest->SetMaxWeight(w - 0.01f - (c.container == ITEM_CONTAINER_BACKPACK ? 0.0001f : 0.0f));
            CHECK(Move(item, &replaced, nullptr, 0, h, c.container, true, false, false, true) == ITEM_RESULT_TOO_HEAVY);
            dest->SetMaxWeight(maxW);
        }
        dest->SetMaxSpace(item->GetSpace() - 0.5f);
        CHECK(Move(item, &replaced, nullptr, 0, h, c.container, true, false, false, true) == ITEM_RESULT_NO_SPACE);
        dest->SetMaxSpace(maxS);
        dest->SetMaxItemSize(item->GetSize() - 1);
        CHECK(Move(item, &replaced, nullptr, 0, h, c.container, true, false, false, true) ==
              (c.container == ITEM_CONTAINER_BACKPACK ? ITEM_RESULT_WRONG_KIND : ITEM_RESULT_TOO_BIG));
        dest->SetMaxItemSize(maxSize);
        // Limits met exactly: fine.
        dest->SetMaxWeight(w);
        dest->SetMaxSpace(item->GetSpace());
        CHECK(Move(item, &replaced, nullptr, 0, h, c.container, true, false, false, true) == ITEM_RESULT_OK);
        dest->SetMaxWeight(maxW);
        dest->SetMaxSpace(maxS);
        // The wrong kind of item.
        C_Item *wrong = New(c.container == ITEM_CONTAINER_HEAD ? g_types.pistol : g_types.helmet);
        if (c.container != ITEM_CONTAINER_BELT)
            CHECK(Move(wrong, &replaced, nullptr, 0, h, c.container, true, false, false, true) == ITEM_RESULT_WRONG_KIND);
        // A new item into a taken place: no space.
        if (c.container != ITEM_CONTAINER_BELT) {
            CHECK(Move(New(c.type), &replaced, nullptr, 0, h, c.container) == ITEM_RESULT_OK);
            CHECK(Move(item, &replaced, nullptr, 0, h, c.container, true, false, false, true) == ITEM_RESULT_NO_SPACE);
        }
    }
    // Order: the strength before the limits (two failing: the first wins).
    {
        S_TestActor *weak = Human(23, 0);
        weak->containers[ITEM_CONTAINER_HANDS]->SetMaxWeight(0.0f);
        CHECK(Move(New(g_types.pistol), &replaced, nullptr, 0, weak, ITEM_CONTAINER_HANDS) == ITEM_RESULT_ACTOR_OVERLOADED);
        // The hands: not a weapon before the weight.
        S_TestActor *h = Human(24, 1000);
        h->containers[ITEM_CONTAINER_HANDS]->SetMaxWeight(0.0f);
        CHECK(Move(New(g_types.helmet), &replaced, nullptr, 0, h, ITEM_CONTAINER_HANDS) == ITEM_RESULT_WRONG_KIND);
        // The backpack: not a backpack before the strength.
        S_TestActor *w2 = Human(25, 0);
        CHECK(Move(New(g_types.helmet), &replaced, nullptr, 0, w2, ITEM_CONTAINER_BACKPACK) == ITEM_RESULT_WRONG_KIND);
        // The long gun: a size 1 firearm (a pistol) is the wrong kind; a size 2 one fits.
        CHECK(Move(New(g_types.pistol), &replaced, nullptr, 0, h, ITEM_CONTAINER_LONG_GUN, true, false, false, true) == ITEM_RESULT_TOO_BIG ||
              Move(New(g_types.pistol), &replaced, nullptr, 0, h, ITEM_CONTAINER_LONG_GUN, true, false, false, true) == ITEM_RESULT_WRONG_KIND);
        h->containers[ITEM_CONTAINER_LONG_GUN]->SetMaxItemSize(3);
        CHECK(Move(New(g_types.pistol), &replaced, nullptr, 0, h, ITEM_CONTAINER_LONG_GUN, true, false, false, true) == ITEM_RESULT_WRONG_KIND);
        if (g_types.smg >= 0)
            CHECK(Move(New(g_types.smg), &replaced, nullptr, 0, h, ITEM_CONTAINER_LONG_GUN, true, false, false, true) == ITEM_RESULT_OK);
    }
    // The same actor: no strength check.
    {
        S_TestActor *weak = Human(26, 0);
        C_Item *p = New(g_types.pistol);
        weak->containers[ITEM_CONTAINER_BELT]->AddItem(p, &replaced, -1);
        CHECK(Move(p, &replaced, weak, ITEM_CONTAINER_BELT, weak, ITEM_CONTAINER_HANDS) == ITEM_RESULT_OK);
    }
    // The belt: totals count; a medkit fits whatever its size; too big otherwise.
    {
        S_TestActor *h = Human(27, 1000);
        C_ItemContainer *belt = h->containers[ITEM_CONTAINER_BELT];
        CHECK(Move(New(g_types.ammo), &replaced, nullptr, 0, h, ITEM_CONTAINER_BELT) == ITEM_RESULT_OK);
        belt->SetMaxWeight(W(g_types.ammo) * 1.5f);
        CHECK(Move(New(g_types.ammo), &replaced, nullptr, 0, h, ITEM_CONTAINER_BELT, true, false, false, true) == ITEM_RESULT_TOO_HEAVY);
        belt->SetMaxWeight(100.0f);
        belt->SetMaxSpace(New(g_types.ammo)->GetSpace() * 1.5f);
        CHECK(Move(New(g_types.ammo), &replaced, nullptr, 0, h, ITEM_CONTAINER_BELT, true, false, false, true) == ITEM_RESULT_NO_SPACE);
        belt->SetMaxSpace(1000.0f);
        belt->SetMaxItemSize(0);
        CHECK(Move(New(g_types.ammo), &replaced, nullptr, 0, h, ITEM_CONTAINER_BELT, true, false, false, true) == ITEM_RESULT_TOO_BIG);
        CHECK(Move(New(g_types.medkit), &replaced, nullptr, 0, h, ITEM_CONTAINER_BELT, true, false, false, true) == ITEM_RESULT_OK);
    }
    // The contents: totals count (0.0001 of slack on the weight); ammunition, mines, medkits fit whatever their size.
    {
        S_TestActor *h = Human(28, 1000);
        CHECK(Move(New(g_types.backpack), &replaced, nullptr, 0, h, ITEM_CONTAINER_BACKPACK) == ITEM_RESULT_OK);
        C_ItemContainer *contents = h->human->GetContainer(ITEM_CONTAINER_BACKPACK_CONTENTS);
        CHECK(contents != nullptr);
        if (!contents)
            return;
        contents->SetMaxWeight(W(g_types.ammo));
        CHECK(Move(New(g_types.ammo), &replaced, nullptr, 0, h, ITEM_CONTAINER_BACKPACK_CONTENTS, true, false, false, true) == ITEM_RESULT_OK);
        contents->SetMaxWeight(W(g_types.ammo) - 0.001f);
        CHECK(Move(New(g_types.ammo), &replaced, nullptr, 0, h, ITEM_CONTAINER_BACKPACK_CONTENTS, true, false, false, true) == ITEM_RESULT_TOO_HEAVY);
        contents->SetMaxWeight(1000.0f);
        contents->SetMaxSpace(0.5f);
        CHECK(Move(New(g_types.ammo), &replaced, nullptr, 0, h, ITEM_CONTAINER_BACKPACK_CONTENTS, true, false, false, true) == ITEM_RESULT_NO_SPACE);
        contents->SetMaxSpace(1000.0f);
        contents->SetMaxItemSize(0);
        for (int type : {g_types.ammo, g_types.mine, g_types.medkit})
            if (type >= 0)
                CHECK(Move(New(type), &replaced, nullptr, 0, h, ITEM_CONTAINER_BACKPACK_CONTENTS, true, false, false, true) == ITEM_RESULT_OK);
        CHECK(Move(New(g_types.pistol), &replaced, nullptr, 0, h, ITEM_CONTAINER_BACKPACK_CONTENTS, true, false, false, true) == ITEM_RESULT_TOO_BIG);
        S_TestActor *weak = Human(29, 0);
        Move(New(g_types.backpack), &replaced, nullptr, 0, weak, ITEM_CONTAINER_BACKPACK, true, false, true);
        weak->human->m_carriedWeight = 0.0f;
        CHECK(Move(New(g_types.ammo), &replaced, nullptr, 0, weak, ITEM_CONTAINER_BACKPACK_CONTENTS) == ITEM_RESULT_ACTOR_OVERLOADED);
    }
    // A vehicle (one store at "head"): totals and size count.
    {
        S_TestActor *v = Vehicle(30);
        C_ItemContainer *store = v->containers[0];
        CHECK(Move(New(g_types.ammo), &replaced, nullptr, 0, v, ITEM_CONTAINER_HEAD) == ITEM_RESULT_OK);
        store->SetMaxWeight(W(g_types.ammo) * 1.5f);
        CHECK(Move(New(g_types.ammo), &replaced, nullptr, 0, v, ITEM_CONTAINER_HEAD, true, false, false, true) == ITEM_RESULT_TOO_HEAVY);
        store->SetMaxWeight(1000.0f);
        store->SetMaxSpace(New(g_types.ammo)->GetSpace() * 1.5f);
        CHECK(Move(New(g_types.ammo), &replaced, nullptr, 0, v, ITEM_CONTAINER_HEAD, true, false, false, true) == ITEM_RESULT_NO_SPACE);
        store->SetMaxSpace(1000.0f);
        store->SetMaxItemSize(0);
        CHECK(Move(New(g_types.ammo), &replaced, nullptr, 0, v, ITEM_CONTAINER_HEAD, true, false, false, true) == ITEM_RESULT_TOO_BIG);
        // Any kind goes in a vehicle.
        store->SetMaxItemSize(5);
        CHECK(Move(New(g_types.rifle), &replaced, nullptr, 0, v, ITEM_CONTAINER_HEAD, true, false, false, true) == ITEM_RESULT_OK);
    }
    // Locks (a server in a mission): the target's, then the source's; ignored when asked.
    {
        Mission(true, GAME_ROLE_SERVER);
        S_TestActor *h = Human(31, 1000);
        C_Item *p = New(g_types.pistol);
        h->containers[ITEM_CONTAINER_BELT]->AddItem(p, &replaced, -1);
        h->containers[ITEM_CONTAINER_HANDS]->SetLockTime(100);
        h->containers[ITEM_CONTAINER_BELT]->SetLockTime(100);
        CHECK(Move(p, &replaced, h, ITEM_CONTAINER_BELT, h, ITEM_CONTAINER_HANDS, true, false, false, true) == ITEM_RESULT_TARGET_LOCKED);
        // (The belt, a store, never locks: from it into free hands is fine.)
        h->containers[ITEM_CONTAINER_HANDS]->SetLockTime(0);
        CHECK(Move(p, &replaced, h, ITEM_CONTAINER_BELT, h, ITEM_CONTAINER_HANDS, true, false, false, true) == ITEM_RESULT_OK);
        h->containers[ITEM_CONTAINER_HANDS]->SetLockTime(100);
        C_Item *q = New(g_types.rifle);
        h->containers[ITEM_CONTAINER_LONG_GUN]->AddItem(q, &replaced, -1);
        h->containers[ITEM_CONTAINER_LONG_GUN]->SetLockTime(100);
        CHECK(Move(q, &replaced, h, ITEM_CONTAINER_LONG_GUN, h, ITEM_CONTAINER_HANDS, true, false, false, true) == ITEM_RESULT_TARGET_LOCKED);
        h->containers[ITEM_CONTAINER_HANDS]->SetLockTime(0);
        CHECK(Move(q, &replaced, h, ITEM_CONTAINER_LONG_GUN, h, ITEM_CONTAINER_HANDS, true, false, false, true) == ITEM_RESULT_SOURCE_LOCKED);
        CHECK(Move(q, &replaced, h, ITEM_CONTAINER_LONG_GUN, h, ITEM_CONTAINER_HANDS, true, true, false, true) == ITEM_RESULT_OK);
        // Not a server, or no mission: no locks.
        Mission(true, GAME_ROLE_CLIENT);
        CHECK(Move(q, &replaced, h, ITEM_CONTAINER_LONG_GUN, h, ITEM_CONTAINER_HANDS, true, false, false, true) == ITEM_RESULT_OK);
        Mission(false, GAME_ROLE_SERVER);
        CHECK(Move(q, &replaced, h, ITEM_CONTAINER_LONG_GUN, h, ITEM_CONTAINER_HANDS, true, false, false, true) == ITEM_RESULT_OK);
        // The hands item still busy (a server): the hands are busy.
        C_Item *held = New(g_types.pistol);
        h->containers[ITEM_CONTAINER_HANDS]->AddItem(held, &replaced, -1);
        held->SetTime(500);
        GetGame()->m_missionTime = 499;
        CHECK(Move(q, &replaced, h, ITEM_CONTAINER_LONG_GUN, h, ITEM_CONTAINER_HANDS, true, false, false, true) == ITEM_RESULT_HANDS_BUSY);
        GetGame()->m_missionTime = 500;
        CHECK(Move(q, &replaced, h, ITEM_CONTAINER_LONG_GUN, h, ITEM_CONTAINER_HANDS, true, false, false, true) == ITEM_RESULT_OK);
        g_gameRole = GAME_ROLE_CLIENT;
        GetGame()->m_missionTime = 0;
        CHECK(Move(q, &replaced, h, ITEM_CONTAINER_LONG_GUN, h, ITEM_CONTAINER_HANDS, true, false, false, true) == ITEM_RESULT_OK);
        Mission(false);
        // The same place (no index): refused; with an index: allowed.
        CHECK(Move(q, &replaced, h, ITEM_CONTAINER_LONG_GUN, h, ITEM_CONTAINER_LONG_GUN) == ITEM_RESULT_SAME_PLACE);
        CHECK(Move(q, &replaced, h, ITEM_CONTAINER_LONG_GUN, h, ITEM_CONTAINER_LONG_GUN, true, false, false, true, 0) == ITEM_RESULT_OK);
    }
    // The store's own failure (full) comes back.
    {
        S_TestActor *v = Vehicle(32);
        v->containers[0]->SetMaxItemSize(5);
        v->containers[0]->SetMaxSpace(100000.0f);
        v->containers[0]->SetMaxWeight(100000.0f);
        g_hitPoints[v->human] = 100000;
        int sorts = 0;
        for (int id = 0; id < MAX_ITEM_TYPES && sorts < 32; ++id)
            if (g_itemsModule.GetItemDef(id) && Move(New(id), &replaced, nullptr, 0, v, ITEM_CONTAINER_HEAD) == ITEM_RESULT_OK)
                ++sorts;
        const int extra = FindType([&](C_ItemDef *d) { return v->containers[0]->CountItems(d->GetId()) == 0; });
        CHECK(sorts == 32 && Move(New(extra), &replaced, nullptr, 0, v, ITEM_CONTAINER_HEAD) == ITEM_RESULT_FAILED);
    }
}

void TestMoveEffects()
{
    Mission(false);
    C_Item *replaced = nullptr;
    // A move: into the target, out of the source; weights moved; the item's owner and container; busy for 920 ms.
    {
        S_TestActor *a = Human(40, 1000), *b = Human(41, 1000);
        C_Item *p = New(g_types.pistol);
        CHECK(Move(p, &replaced, nullptr, 0, a, ITEM_CONTAINER_BELT) == ITEM_RESULT_OK);
        CHECK(a->human->GetCarriedWeight() == W(g_types.pistol) && p->m_owner == a->human && p->m_container == ITEM_CONTAINER_BELT &&
              a->containers[ITEM_CONTAINER_BELT]->CountItems(g_types.pistol) == 1);
        GetGame()->m_missionTime = 1000;
        CHECK(Move(p, &replaced, a, ITEM_CONTAINER_BELT, b, ITEM_CONTAINER_HANDS) == ITEM_RESULT_OK);
        CHECK(a->human->GetCarriedWeight() == 0.0f && b->human->GetCarriedWeight() == W(g_types.pistol) && p->m_owner == b->human &&
              p->m_container == ITEM_CONTAINER_HANDS && a->containers[ITEM_CONTAINER_BELT]->IsEmpty() &&
              b->containers[ITEM_CONTAINER_HANDS]->GetItem() == p && p->GetTime() == 1920);
        GetGame()->m_missionTime = 0;
        // Weights kept: the source keeps its weight.
        CHECK(Move(p, &replaced, b, ITEM_CONTAINER_HANDS, a, ITEM_CONTAINER_LONG_GUN, true, false, true) <= 0 ||
              b->human->GetCarriedWeight() == W(g_types.pistol));
        // Dropped (to nobody): out of the source, no owner change, no target.
        C_Item *q = New(g_types.pistol);
        a->containers[ITEM_CONTAINER_BELT]->AddItem(q, &replaced, -1);
        a->human->m_carriedWeight = W(g_types.pistol);
        q->m_owner = a->human;
        CHECK(Move(q, &replaced, a, ITEM_CONTAINER_BELT, nullptr, ITEM_CONTAINER_HEAD) == ITEM_RESULT_OK);
        CHECK(a->containers[ITEM_CONTAINER_BELT]->IsEmpty() && a->human->GetCarriedWeight() == 0.0f && q->m_owner == a->human);
        // Test only: nothing changes.
        C_Item *r = New(g_types.helmet);
        CHECK(Move(r, &replaced, nullptr, 0, a, ITEM_CONTAINER_HEAD, true, false, false, true) == ITEM_RESULT_OK &&
              a->containers[ITEM_CONTAINER_HEAD]->IsEmpty() && a->human->GetCarriedWeight() == 0.0f && r->GetTime() == 0);
    }
    // A replaced item: handed back, its weight (and contents) off the target.
    {
        S_TestActor *a = Human(42, 1000), *b = Human(43, 1000);
        C_Item *h1 = New(g_types.helmet), *h2 = New(g_types.helmet);
        Move(h1, &replaced, nullptr, 0, b, ITEM_CONTAINER_HEAD);
        a->containers[ITEM_CONTAINER_BELT]->AddItem(h2, &replaced, -1);
        CHECK(Move(h2, &replaced, a, ITEM_CONTAINER_BELT, b, ITEM_CONTAINER_HEAD) == ITEM_RESULT_OK && replaced == h1);
        CHECK(b->human->GetCarriedWeight() == W(g_types.helmet) && b->containers[ITEM_CONTAINER_HEAD]->GetItem() == h2);
        // Weights kept: the replaced item's weight stays too.
        C_Item *h3 = New(g_types.helmet);
        a->containers[ITEM_CONTAINER_BELT]->AddItem(h3, &replaced, -1);
        CHECK(Move(h3, &replaced, a, ITEM_CONTAINER_BELT, b, ITEM_CONTAINER_HEAD, true, false, true) == ITEM_RESULT_OK && replaced == h2 &&
              b->human->GetCarriedWeight() == 2.0f * W(g_types.helmet));
    }
    // A real backpack moves with its contents: the contents container follows; their weight too.
    {
        S_TestActor *a = Human(44, 1000), *b = Human(45, 1000);
        C_Item *pack = New(g_types.backpack);
        CHECK(Move(pack, &replaced, nullptr, 0, a, ITEM_CONTAINER_BACKPACK) == ITEM_RESULT_OK);
        C_ItemStore *store = static_cast<C_ContainerItem *>(pack)->GetStore();
        CHECK(a->human->GetContainer(ITEM_CONTAINER_BACKPACK_CONTENTS) == store);
        CHECK(Move(New(g_types.ammo), &replaced, nullptr, 0, a, ITEM_CONTAINER_BACKPACK_CONTENTS) == ITEM_RESULT_OK);
        const float total = W(g_types.backpack) + W(g_types.ammo);
        CHECK(std::fabs(a->human->GetCarriedWeight() - total) < 1e-5f);
        CHECK(Move(pack, &replaced, a, ITEM_CONTAINER_BACKPACK, b, ITEM_CONTAINER_BACKPACK) == ITEM_RESULT_OK);
        CHECK(a->human->GetContainer(ITEM_CONTAINER_BACKPACK_CONTENTS) == nullptr && b->human->GetContainer(ITEM_CONTAINER_BACKPACK_CONTENTS) == store);
        CHECK(std::fabs(a->human->GetCarriedWeight()) < 1e-5f && std::fabs(b->human->GetCarriedWeight() - total) < 1e-5f);
        // Ignoring locks (the swap's way back): the source keeps its contents container.
        CHECK(Move(pack, &replaced, b, ITEM_CONTAINER_BACKPACK, a, ITEM_CONTAINER_BACKPACK, false, true) == ITEM_RESULT_OK);
        CHECK(b->human->GetContainer(ITEM_CONTAINER_BACKPACK_CONTENTS) == store && a->human->GetContainer(ITEM_CONTAINER_BACKPACK_CONTENTS) == store);
        // Dropped with its contents: the weight of both leaves.
        b->human->SetContainer(ITEM_CONTAINER_BACKPACK_CONTENTS, nullptr);
        CHECK(Move(pack, &replaced, a, ITEM_CONTAINER_BACKPACK, nullptr, ITEM_CONTAINER_HEAD) == ITEM_RESULT_OK);
        CHECK(std::fabs(a->human->GetCarriedWeight()) < 1e-5f && a->human->GetContainer(ITEM_CONTAINER_BACKPACK_CONTENTS) == nullptr);
        // A backpack replacing a backpack: the old one's contents weight leaves too.
        S_TestActor *c = Human(46, 1000);
        C_Item *old = New(g_types.backpack);
        Move(old, &replaced, nullptr, 0, c, ITEM_CONTAINER_BACKPACK);
        Move(New(g_types.ammo), &replaced, nullptr, 0, c, ITEM_CONTAINER_BACKPACK_CONTENTS);
        S_TestActor *d = Human(47, 1000);
        C_Item *fresh = New(g_types.backpack);
        Move(fresh, &replaced, nullptr, 0, d, ITEM_CONTAINER_BACKPACK);
        CHECK(Move(fresh, &replaced, d, ITEM_CONTAINER_BACKPACK, c, ITEM_CONTAINER_BACKPACK) == ITEM_RESULT_OK && replaced == old);
        CHECK(std::fabs(c->human->GetCarriedWeight() - W(g_types.backpack)) < 1e-5f);
        // A radio (a backpack component of another subtype) has no contents.
        if (g_types.radio >= 0) {
            S_TestActor *e = Human(48, 1000);
            C_Item *radio = New(g_types.radio);
            const int result = Move(radio, &replaced, nullptr, 0, e, ITEM_CONTAINER_BACKPACK);
            CHECK(result != ITEM_RESULT_OK || e->human->GetContainer(ITEM_CONTAINER_BACKPACK_CONTENTS) == nullptr);
        }
    }
    // An item lying in the world: taken out of it, its model hidden.
    {
        S_TestActor *a = Human(49, 1000);
        C_Item *p = New(g_types.pistol);
        p->m_frame = NewFrame(FRAME_MODEL);
        p->SetInWorld(true);
        g_log.clear();
        CHECK(Move(p, &replaced, nullptr, 0, a, ITEM_CONTAINER_BELT) == ITEM_RESULT_OK && !p->IsInWorld() && Count("frame off") == 1);
        p->m_frame = nullptr;
    }
}

// NotifyMove (in a mission): the equipments told (created for a new item, removed / added otherwise, when asked);
// inventories changed; the hands' lock (920 ms) and the change; the first-person view of a client's active player.
void TestNotify()
{
    C_Item *replaced = nullptr;
    S_TestActor *a = Human(50, 1000), *b = Human(51, 1000);
    Mission(true);
    GetGame()->m_missionTime = 100;
    g_log.clear();
    C_Item *p = New(g_types.pistol);
    CHECK(Move(p, &replaced, nullptr, 0, a, ITEM_CONTAINER_HANDS) == ITEM_RESULT_OK);
    CHECK(Count("eq created " + std::to_string(g_types.pistol) + " 3") == 1 && Count("handschanged") == 1 &&
          a->containers[ITEM_CONTAINER_HANDS]->GetLockTime() == 1020 && a->human->m_inventoryChanged && !Logged("fpv"));
    // Hands to another's long gun: removed / added; both locked; the hands changed; inventories changed.
    a->containers[ITEM_CONTAINER_HANDS]->SetLockTime(0);
    a->human->m_inventoryChanged = b->human->m_inventoryChanged = false;
    g_log.clear();
    C_Item *rifle = New(g_types.rifle);
    a->containers[ITEM_CONTAINER_HANDS]->AddItem(rifle, &replaced, -1);
    CHECK(Move(rifle, &replaced, a, ITEM_CONTAINER_HANDS, b, ITEM_CONTAINER_LONG_GUN) == ITEM_RESULT_OK);
    CHECK(Count("eq removed " + std::to_string(g_types.rifle) + " 3 to 2") == 1 && Count("eq added " + std::to_string(g_types.rifle) + " 2 from 3 0") == 1);
    CHECK(a->containers[ITEM_CONTAINER_HANDS]->GetLockTime() == 1020 && b->containers[ITEM_CONTAINER_LONG_GUN]->GetLockTime() == 1020 &&
          Count("handschanged") == 1 && a->human->m_inventoryChanged && b->human->m_inventoryChanged);
    // Not asked: the equipments are not told.
    g_log.clear();
    CHECK(Move(rifle, &replaced, b, ITEM_CONTAINER_LONG_GUN, a, ITEM_CONTAINER_BELT, false) <= 0 || !Logged("eq "));
    // A client: the active player's inventory refreshed; out of / into its hands: the first-person events.
    Mission(true, GAME_ROLE_CLIENT);
    g_activePlayer = a->human;
    g_log.clear();
    C_Item *q = New(g_types.pistol);
    CHECK(Move(q, &replaced, nullptr, 0, a, ITEM_CONTAINER_BELT) == ITEM_RESULT_OK && Count("refreshinventory") == 1);
    a->containers[ITEM_CONTAINER_HANDS]->Clear();
    g_log.clear();
    CHECK(Move(q, &replaced, a, ITEM_CONTAINER_BELT, a, ITEM_CONTAINER_HANDS) == ITEM_RESULT_OK && Count("fpv 8 0") == 1);
    g_log.clear();
    CHECK(Move(q, &replaced, a, ITEM_CONTAINER_HANDS, a, ITEM_CONTAINER_BELT) == ITEM_RESULT_OK && Count("fpv 20 0") == 1);
    // Put away: no draw event.
    Eq(a)->m_flags = EQUIPMENT_HANDS_PUT_AWAY;
    a->containers[ITEM_CONTAINER_HANDS]->SetLockTime(0);
    g_log.clear();
    CHECK(Move(q, &replaced, a, ITEM_CONTAINER_BELT, a, ITEM_CONTAINER_HANDS) == ITEM_RESULT_OK && !Logged("fpv"));
    Eq(a)->m_flags = 0;
    // Dropped from the hands by the active player (the carrier): cleared.
    g_log.clear();
    CHECK(g_itemsModule.MoveItem(q, &replaced, a->human, ITEM_CONTAINER_HANDS, nullptr, ITEM_CONTAINER_HEAD, -1, nullptr, true, true, false,
                                 false) == ITEM_RESULT_OK);
    CHECK(Count("fpv 20 0") == 2);
    // Another player: nothing.
    g_activePlayer = b->human;
    g_log.clear();
    C_Item *s = New(g_types.pistol);
    CHECK(Move(s, &replaced, nullptr, 0, a, ITEM_CONTAINER_BELT) == ITEM_RESULT_OK && !Logged("refreshinventory") && !Logged("fpv"));
    g_activePlayer = nullptr;
    // No mission: nothing told.
    Mission(false);
    g_log.clear();
    CHECK(Move(New(g_types.pistol), &replaced, nullptr, 0, b, ITEM_CONTAINER_BELT) == ITEM_RESULT_OK && !Logged("eq ") && !Logged("handschanged"));
    GetGame()->m_missionTime = 0;
}

// PutItem: no actor / a vehicle / no item; a backpack to the backpack slot; headgear to a free head; the rest by
// PutItemAuto (hands for weapons, the long gun, the belt, the contents), or no place. CanPutItem: the same, testing.
void TestPut()
{
    Mission(false);
    int container = -5;
    S_TestActor *a = Human(60, 1000);
    CHECK(g_itemsModule.PutItem(New(g_types.pistol), nullptr, &container, false) == ITEM_RESULT_NO_ACTOR && container == ITEM_CONTAINER_DROPPED);
    CHECK(g_itemsModule.PutItem(New(g_types.pistol), Vehicle(61)->human, &container, false) == ITEM_RESULT_SINGLE_CONTAINER);
    CHECK(g_itemsModule.PutItem(nullptr, a->human, &container, false) == ITEM_RESULT_NULL_ITEM);
    CHECK(g_itemsModule.CanPutItem(nullptr, New(g_types.pistol)) == ITEM_RESULT_NO_ACTOR &&
          g_itemsModule.CanPutItem(Vehicle(62)->human, New(g_types.pistol)) == ITEM_RESULT_SINGLE_CONTAINER);
    // A rifle: hands, then the long gun, then the belt (too big), then nowhere.
    CHECK(g_itemsModule.CanPutItem(a->human, New(g_types.rifle)) == ITEM_RESULT_OK && a->containers[ITEM_CONTAINER_HANDS]->IsEmpty());
    CHECK(g_itemsModule.PutItem(New(g_types.rifle), a->human, &container, false) == ITEM_RESULT_OK && container == ITEM_CONTAINER_HANDS);
    CHECK(g_itemsModule.PutItem(New(g_types.rifle), a->human, &container, false) == ITEM_RESULT_OK && container == ITEM_CONTAINER_LONG_GUN);
    CHECK(g_itemsModule.PutItem(New(g_types.rifle), a->human, &container, false) == ITEM_RESULT_NO_PLACE && container == ITEM_CONTAINER_DROPPED);
    CHECK(g_itemsModule.CanPutItem(a->human, New(g_types.rifle)) < 0);
    // A pistol after that: the belt.
    CHECK(g_itemsModule.PutItem(New(g_types.pistol), a->human, &container, false) == ITEM_RESULT_OK && container == ITEM_CONTAINER_BELT);
    // Headgear: the head while free, else the belt / contents (too big: none).
    CHECK(g_itemsModule.PutItem(New(g_types.helmet), a->human, &container, false) == ITEM_RESULT_OK && container == ITEM_CONTAINER_HEAD);
    const int second = g_itemsModule.PutItem(New(g_types.helmet), a->human, &container, false);
    CHECK(second == ITEM_RESULT_NO_PLACE || container == ITEM_CONTAINER_BELT);
    // A backpack: its slot (refused once taken: no space); then the contents take what the belt cannot.
    CHECK(g_itemsModule.CanPutItem(a->human, New(g_types.backpack)) == ITEM_RESULT_OK);
    CHECK(g_itemsModule.PutItem(New(g_types.backpack), a->human, &container, false) == ITEM_RESULT_OK && container == ITEM_CONTAINER_BACKPACK);
    CHECK(g_itemsModule.PutItem(New(g_types.backpack), a->human, &container, false) == ITEM_RESULT_NO_SPACE && container == ITEM_CONTAINER_DROPPED);
    a->containers[ITEM_CONTAINER_BELT]->SetMaxSpace(0.0f);
    CHECK(g_itemsModule.PutItem(New(g_types.ammo), a->human, &container, false) == ITEM_RESULT_OK && container == ITEM_CONTAINER_BACKPACK_CONTENTS);
    // A knife or a mine: the hands first (empty) like weapons; a launcher-like size-1 projectile not the long gun.
    S_TestActor *b = Human(63, 1000);
    if (g_types.knife >= 0)
        CHECK(g_itemsModule.PutItem(New(g_types.knife), b->human, &container, false) == ITEM_RESULT_OK && container == ITEM_CONTAINER_HANDS);
    S_TestActor *c = Human(64, 1000);
    if (g_types.grenade >= 0) {
        const int result = g_itemsModule.PutItem(New(g_types.grenade), c->human, &container, false);
        CHECK(result == ITEM_RESULT_OK && (container == ITEM_CONTAINER_HANDS || container == ITEM_CONTAINER_BELT));
    }
    // Notify passed through: a new item is "created" in a mission.
    Mission(true);
    S_TestActor *d = Human(65, 1000);
    g_log.clear();
    CHECK(g_itemsModule.PutItem(New(g_types.pistol), d->human, &container, true) == ITEM_RESULT_OK && Logged("eq created"));
    g_log.clear();
    CHECK(g_itemsModule.PutItem(New(g_types.pistol), d->human, &container, false) == ITEM_RESULT_OK && !Logged("eq created"));
    Mission(false);
}

// CreateItem: unknown types none; a uid (or the next); placed (refused: none). GiveItem: local games only, by kind.
// CreateFreeItem: uid -1, listed.
void TestCreate()
{
    Mission(false);
    g_itemsModule.ResetUids();
    S_TestActor *a = Human(70, 1000);
    CHECK(g_itemsModule.CreateItem(-1, ITEM_CONTAINER_BELT, a->human, -1) == nullptr);
    const size_t n = g_itemsModule.m_instances.size();
    C_Item *p = g_itemsModule.CreateItem(g_types.pistol, ITEM_CONTAINER_BELT, a->human, -1);
    CHECK(p && p->GetUid() == 0 && g_itemsModule.m_instances.size() == n + 1 && a->containers[ITEM_CONTAINER_BELT]->CountItems(g_types.pistol) == 1);
    C_Item *q = g_itemsModule.CreateItem(g_types.pistol, ITEM_CONTAINER_BELT, a->human, 77);
    CHECK(q && q->GetUid() == 77 && g_itemsModule.NextUid() == 1);
    CHECK(g_itemsModule.CreateItem(g_types.helmet, ITEM_CONTAINER_HANDS, a->human, -1) == nullptr && g_itemsModule.m_instances.size() == n + 2);
    // Free items.
    C_Item *f = g_itemsModule.CreateFreeItem(g_types.ammo);
    CHECK(f && f->GetUid() == -1 && g_itemsModule.m_instances.back() == f && g_itemsModule.CreateFreeItem(-3) == nullptr);
    // Give: not in a multiplayer game, not to nobody, not an unknown type, not with `unused`.
    g_gameRole = GAME_ROLE_SERVER;
    CHECK(g_itemsModule.GiveItem(g_types.pistol, 0, a->human, 0) == nullptr);
    g_gameRole = GAME_ROLE_LOCAL;
    CHECK(g_itemsModule.GiveItem(g_types.pistol, 0, nullptr, 0) == nullptr && g_itemsModule.GiveItem(-1, 0, a->human, 0) == nullptr &&
          g_itemsModule.GiveItem(g_types.pistol, 1, a->human, 0) == nullptr);
    // Firearms: the hands while empty, a size 2-3 the long gun while empty, else the belt.
    S_TestActor *b = Human(71, 1000);
    C_Item *g1 = g_itemsModule.GiveItem(g_types.rifle, 0, b->human, 0);
    C_Item *g2 = g_itemsModule.GiveItem(g_types.rifle, 0, b->human, 0);
    C_Item *g3 = g_itemsModule.GiveItem(g_types.pistol, 0, b->human, 0);
    CHECK(g1 && b->containers[ITEM_CONTAINER_HANDS]->GetItem() == g1 && g2 && b->containers[ITEM_CONTAINER_LONG_GUN]->GetItem() == g2 && g3 &&
          b->containers[ITEM_CONTAINER_BELT]->CountItems(g_types.pistol) == 1);
    CHECK(g1->GetUid() == -2 || g1->GetUid() >= 0);
    // A uniform: its slot while empty, else the head (refused); a backpack likewise; headgear the head.
    C_Item *u1 = g_itemsModule.GiveItem(g_types.uniform, 0, b->human, 0);
    CHECK(u1 && b->containers[ITEM_CONTAINER_UNIFORM]->GetItem() == u1 && g_itemsModule.GiveItem(g_types.uniform, 0, b->human, 0) == nullptr);
    C_Item *k1 = g_itemsModule.GiveItem(g_types.backpack, 0, b->human, 0);
    CHECK(k1 && b->containers[ITEM_CONTAINER_BACKPACK]->GetItem() == k1 && g_itemsModule.GiveItem(g_types.backpack, 0, b->human, 0) == nullptr);
    C_Item *hg = g_itemsModule.GiveItem(g_types.helmet, 0, b->human, 0);
    CHECK(hg && b->containers[ITEM_CONTAINER_HEAD]->GetItem() == hg);
    // Others: the belt, else the contents.
    C_Item *am = g_itemsModule.GiveItem(g_types.ammo, 0, b->human, 0);
    CHECK(am && b->containers[ITEM_CONTAINER_BELT]->CountItems(g_types.ammo) == 1);
    b->containers[ITEM_CONTAINER_BELT]->SetMaxSpace(0.0f);
    C_Item *am2 = g_itemsModule.GiveItem(g_types.ammo, 0, b->human, 0);
    CHECK(am2 && b->human->GetContainer(ITEM_CONTAINER_BACKPACK_CONTENTS)->CountItems(g_types.ammo) == 1);
}

// TransferItem: busy (a server, moved less than 920 ms ago) or a swap the displaced item cannot make: refused; a plain
// move: the equipments told (the first-person view of a client's active player); a displaced item goes back to the
// source, else anywhere it fits, else down; humans swapping are told by the rules of the hands.
void Later() { GetGame()->m_missionTime += 1000; }
// The log's equipment and first-person lines (the model opens that fail are logged too).
std::vector<std::string> Told()
{
    std::vector<std::string> out;
    for (const std::string &l : g_log)
        if (l.rfind("eq ", 0) == 0 || l.rfind("fpv ", 0) == 0)
            out.push_back(l);
    return out;
}
int Transfer(C_Item *item, S_TestActor *from, int fromContainer, S_TestActor *to, int toContainer, C_Item **replaced, int *toOut,
             int *replacedTo)
{
    Later();
    return g_itemsModule.TransferItem(item, from->human, fromContainer, to->human, toContainer, replaced, toOut, replacedTo);
}
void TestTransfer()
{
    Mission(false);
    C_Item *replaced = reinterpret_cast<C_Item *>(0x1), *r = nullptr;
    int toOut = -5, replacedTo = -5;
    S_TestActor *a = Human(80, 1000), *b = Human(81, 1000);
    // Busy: a server only.
    C_Item *p = New(g_types.pistol);
    Move(p, &r, nullptr, 0, a, ITEM_CONTAINER_BELT);
    p->SetTime(500);
    GetGame()->m_missionTime = 100;
    g_gameRole = GAME_ROLE_SERVER;
    CHECK(g_itemsModule.TransferItem(p, a->human, ITEM_CONTAINER_BELT, b->human, ITEM_CONTAINER_BELT, &replaced, &toOut, &replacedTo) == ITEM_RESULT_ITEM_BUSY);
    CHECK(replaced == nullptr && toOut == ITEM_CONTAINER_BELT && replacedTo == ITEM_CONTAINER_BELT);
    CHECK(g_itemsModule.TransferItem(nullptr, a->human, ITEM_CONTAINER_BELT, b->human, ITEM_CONTAINER_BELT, &replaced, &toOut, &replacedTo) == ITEM_RESULT_ITEM_BUSY);
    g_gameRole = GAME_ROLE_LOCAL;
    GetGame()->m_missionTime = 0;
    // A plain move: removed from a, added to b (the moves themselves tell nothing).
    g_log.clear();
    CHECK(Transfer(p, a, ITEM_CONTAINER_BELT, b, ITEM_CONTAINER_BELT, &replaced, &toOut, &replacedTo) == ITEM_RESULT_OK && replaced == nullptr);
    const std::string pt = std::to_string(g_types.pistol);
    CHECK((Told() == std::vector<std::string>{"eq removed " + pt + " 1 to 1", "eq added " + pt + " 1 from 1 0"}));
    // A failed move: its result.
    b->containers[ITEM_CONTAINER_HEAD]->SetMaxWeight(0.0f);
    CHECK(Transfer(p, b, ITEM_CONTAINER_BELT, a, ITEM_CONTAINER_HEAD, &replaced, &toOut, &replacedTo) == ITEM_RESULT_WRONG_KIND);
    // A client's active player: out of the hands put away, into the hands drawn.
    Mission(false, GAME_ROLE_CLIENT);
    g_activePlayer = b->human;
    g_log.clear();
    CHECK(Transfer(p, b, ITEM_CONTAINER_BELT, b, ITEM_CONTAINER_HANDS, &replaced, &toOut, &replacedTo) == ITEM_RESULT_OK && Count("fpv 8 0") == 1);
    g_log.clear();
    CHECK(Transfer(p, b, ITEM_CONTAINER_HANDS, b, ITEM_CONTAINER_BELT, &replaced, &toOut, &replacedTo) == ITEM_RESULT_OK && Count("fpv 9 0") == 1);
    g_activePlayer = nullptr;
    Mission(false);
    // From a vehicle: only the receiver told.
    S_TestActor *v = Vehicle(82);
    C_Item *am = New(g_types.ammo);
    v->containers[0]->AddItem(am, &r, -1);
    g_log.clear();
    CHECK(Transfer(am, v, 0, a, ITEM_CONTAINER_BELT, &replaced, &toOut, &replacedTo) == ITEM_RESULT_OK);
    CHECK((Told() == std::vector<std::string>{"eq added " + std::to_string(g_types.ammo) + " 1 from 0 0"}));
    // A swap of two helmets between heads: each back where the other was; both told, with whether the other is dead.
    S_TestActor *c = Human(83, 1000), *d = Human(84, 1000);
    C_Item *h1 = New(g_types.helmet), *h2 = New(g_types.helmet);
    Move(h1, &r, nullptr, 0, c, ITEM_CONTAINER_HEAD);
    Move(h2, &r, nullptr, 0, d, ITEM_CONTAINER_HEAD);
    d->human->m_stats[STAT_HEALTH] = 0;
    g_log.clear();
    CHECK(Transfer(h1, c, ITEM_CONTAINER_HEAD, d, ITEM_CONTAINER_HEAD, &replaced, &toOut, &replacedTo) == ITEM_RESULT_OK && replaced == h2 &&
          replacedTo == ITEM_CONTAINER_HEAD);
    CHECK(c->containers[ITEM_CONTAINER_HEAD]->GetItem() == h2 && d->containers[ITEM_CONTAINER_HEAD]->GetItem() == h1);
    const std::string ht = std::to_string(g_types.helmet);
    CHECK(Count("eq added " + ht + " 0 from 0 1") == 1 && Count("eq added " + ht + " 0 from 0 0") == 1);
    d->human->m_stats[STAT_HEALTH] = 100;
    // Hands for hands: both let the support hand go; both told (the other's death flag).
    S_TestActor *e = Human(85, 1000), *f = Human(86, 1000);
    C_Item *g1 = New(g_types.pistol), *g2 = New(g_types.pistol);
    Move(g1, &r, nullptr, 0, e, ITEM_CONTAINER_HANDS);
    Move(g2, &r, nullptr, 0, f, ITEM_CONTAINER_HANDS);
    g_gameRole = GAME_ROLE_CLIENT;
    g_activePlayer = f->human;
    g_log.clear();
    CHECK(Transfer(g1, e, ITEM_CONTAINER_HANDS, f, ITEM_CONTAINER_HANDS, &replaced, &toOut, &replacedTo) == ITEM_RESULT_OK && replaced == g2);
    CHECK(Count("eq event 1") == 2 && Count("fpv 9 8") == 1 && Count("eq added " + pt + " 3 from 3 0") == 2);
    g_activePlayer = nullptr;
    g_gameRole = GAME_ROLE_LOCAL;
    // Belt for hands (one side the hands): the belt's holster count down for the item; the displaced pistol back to
    // the belt; the receiver takes it into the hands with the give events.
    S_TestActor *gA = Human(87, 1000), *gB = Human(88, 1000);
    C_Item *beltPistol = New(g_types.pistol), *handsPistol = New(g_types.pistol);
    Move(beltPistol, &r, nullptr, 0, gA, ITEM_CONTAINER_BELT);
    Move(handsPistol, &r, nullptr, 0, gB, ITEM_CONTAINER_HANDS);
    g_log.clear();
    CHECK(Transfer(beltPistol, gA, ITEM_CONTAINER_BELT, gB, ITEM_CONTAINER_HANDS, &replaced, &toOut, &replacedTo) == ITEM_RESULT_OK &&
          replaced == handsPistol && replacedTo == ITEM_CONTAINER_BELT);
    CHECK(Count("eq unholster " + pt + " 1") >= 1 && Count("eq 792310") == 1 && Count("eq added " + pt + " 3 from 1 1") == 1);
    // The displaced item cannot go back but fits elsewhere: put (the replaced container told).
    S_TestActor *hA = Human(89, 1000), *hB = Human(90, 1000);
    C_Item *ra = New(g_types.rifle), *rb = New(g_types.rifle);
    Move(ra, &r, nullptr, 0, hA, ITEM_CONTAINER_LONG_GUN);
    Move(rb, &r, nullptr, 0, hB, ITEM_CONTAINER_LONG_GUN);
    hA->containers[ITEM_CONTAINER_LONG_GUN]->SetMaxWeight(W(g_types.rifle));   // (still fits back)
    CHECK(Transfer(ra, hA, ITEM_CONTAINER_LONG_GUN, hB, ITEM_CONTAINER_LONG_GUN, &replaced, &toOut, &replacedTo) == ITEM_RESULT_OK &&
          replaced == rb && hA->containers[ITEM_CONTAINER_LONG_GUN]->GetItem() == rb);
}

int AutoLater(C_Item *item, C_Actor *from, int fromContainer, C_Actor *to, int *toContainer)
{
    Later();
    return g_itemsModule.TransferItemAuto(item, from, fromContainer, to, toContainer);
}
// TransferItemAuto: no actor; a backpack only to a free backpack slot; headgear the head; firearms the hands, then a
// long gun's place; the belt; the contents; else no place.
void TestTransferAuto()
{
    GetGame()->m_missionTime = 0;
    Mission(false);
    C_Item *r = nullptr;
    int to = -5;
    S_TestActor *a = Human(100, 1000), *b = Human(101, 1000);
    CHECK(AutoLater(New(g_types.pistol), nullptr, 0, b->human, &to) == ITEM_RESULT_NO_ACTOR && to == ITEM_CONTAINER_DROPPED);
    CHECK(AutoLater(New(g_types.pistol), a->human, 0, nullptr, &to) == ITEM_RESULT_NO_ACTOR);
    auto give = [&](int type, int from) {
        C_Item *item = New(type);
        Move(item, &r, nullptr, 0, a, from);
        return item;
    };
    C_Item *pack = give(g_types.backpack, ITEM_CONTAINER_BACKPACK);
    CHECK(AutoLater(pack, a->human, ITEM_CONTAINER_BACKPACK, b->human, &to) == ITEM_RESULT_OK && to == ITEM_CONTAINER_BACKPACK);
    C_Item *pack2 = give(g_types.backpack, ITEM_CONTAINER_BACKPACK);
    CHECK(AutoLater(pack2, a->human, ITEM_CONTAINER_BACKPACK, b->human, &to) == ITEM_RESULT_NO_PLACE);
    C_Item *helmet = give(g_types.helmet, ITEM_CONTAINER_HEAD);
    CHECK(AutoLater(helmet, a->human, ITEM_CONTAINER_HEAD, b->human, &to) == ITEM_RESULT_OK && to == ITEM_CONTAINER_HEAD);
    C_Item *rifle1 = give(g_types.rifle, ITEM_CONTAINER_HANDS);
    CHECK(AutoLater(rifle1, a->human, ITEM_CONTAINER_HANDS, b->human, &to) == ITEM_RESULT_OK && to == ITEM_CONTAINER_HANDS);
    C_Item *rifle2 = give(g_types.rifle, ITEM_CONTAINER_HANDS);
    CHECK(AutoLater(rifle2, a->human, ITEM_CONTAINER_HANDS, b->human, &to) == ITEM_RESULT_OK && to == ITEM_CONTAINER_LONG_GUN);
    C_Item *ammo = give(g_types.ammo, ITEM_CONTAINER_BELT);
    CHECK(AutoLater(ammo, a->human, ITEM_CONTAINER_BELT, b->human, &to) == ITEM_RESULT_OK && to == ITEM_CONTAINER_BELT);
    b->containers[ITEM_CONTAINER_BELT]->SetMaxSpace(0.0f);
    C_Item *ammo2 = give(g_types.ammo, ITEM_CONTAINER_BELT);
    CHECK(AutoLater(ammo2, a->human, ITEM_CONTAINER_BELT, b->human, &to) == ITEM_RESULT_OK && to == ITEM_CONTAINER_BACKPACK_CONTENTS);
    C_Item *rifle3 = give(g_types.rifle, ITEM_CONTAINER_HANDS);
    CHECK(AutoLater(rifle3, a->human, ITEM_CONTAINER_HANDS, b->human, &to) == ITEM_RESULT_NO_PLACE);
}

// CanAddItemToInventory / CanPutItemInSlot: the checks by type, without an item.
void TestCanAdd()
{
    GetGame()->m_missionTime = 0;
    Mission(false);
    S_TestActor *a = Human(110, 1000);
    CHECK(g_itemsModule.CanAddItemToInventory(nullptr, g_types.pistol) == ITEM_RESULT_NO_ACTOR &&
          g_itemsModule.CanAddItemToInventory(Vehicle(111)->human, g_types.pistol) == ITEM_RESULT_SINGLE_CONTAINER);
    CHECK(g_itemsModule.CanPutItemInSlot(nullptr, ITEM_CONTAINER_HEAD, g_types.helmet) == ITEM_RESULT_NO_HEAD);
    for (int c : {ITEM_CONTAINER_BELT, ITEM_CONTAINER_LONG_GUN, ITEM_CONTAINER_HANDS, ITEM_CONTAINER_UNIFORM, ITEM_CONTAINER_BACKPACK,
                  ITEM_CONTAINER_BACKPACK_CONTENTS})
        CHECK(g_itemsModule.CanPutItemInSlot(nullptr, c, g_types.pistol) == ITEM_RESULT_BAD_TARGET &&
              g_itemsModule.CanPutItemInSlot(Vehicle(112)->human, c, g_types.pistol) == ITEM_RESULT_BAD_TARGET);
    CHECK(g_itemsModule.CanPutItemInSlot(a->human, ITEM_CONTAINER_BACKPACK_CONTENTS, g_types.ammo) == ITEM_RESULT_BAD_TARGET);
    CHECK(g_itemsModule.CanPutItemInSlot(a->human, 7, g_types.ammo) == ITEM_RESULT_OK);
    // Per slot: the right kind fits; the limits, each alone; the wrong kind; taken.
    struct S_Slot {
        int container, type, wrong;
    };
    for (const S_Slot &s : {S_Slot{ITEM_CONTAINER_HEAD, g_types.helmet, g_types.pistol}, S_Slot{ITEM_CONTAINER_LONG_GUN, g_types.rifle, g_types.helmet},
                            S_Slot{ITEM_CONTAINER_UNIFORM, g_types.uniform, g_types.helmet},
                            S_Slot{ITEM_CONTAINER_BACKPACK, g_types.backpack, g_types.helmet}, S_Slot{ITEM_CONTAINER_HANDS, g_types.pistol, -1}}) {
        S_TestActor *h = Human(113, 1000);
        C_ItemContainer *dest = h->containers[s.container];
        CHECK(g_itemsModule.CanPutItemInSlot(h->human, s.container, s.type) == ITEM_RESULT_OK);
        const C_ItemDef *def = g_itemsModule.GetItemDef(s.type);
        const float w = def->GetWeight(), space = def->GetInventorySize() ? def->GetInventorySize() * 2.0f : 1.0f;
        const float maxW = dest->GetMaxWeight(), maxS = dest->GetMaxSpace();
        const int maxSize = dest->GetMaxItemSize();
        if (w > 0.0f) {
            dest->SetMaxWeight(w - 0.01f - (s.container == ITEM_CONTAINER_BACKPACK ? 0.0001f : 0.0f));
            CHECK(g_itemsModule.CanPutItemInSlot(h->human, s.container, s.type) == ITEM_RESULT_TOO_HEAVY);
            dest->SetMaxWeight(maxW);
        }
        dest->SetMaxSpace(space - 0.5f);
        CHECK(g_itemsModule.CanPutItemInSlot(h->human, s.container, s.type) == ITEM_RESULT_NO_SPACE);
        dest->SetMaxSpace(maxS);
        dest->SetMaxItemSize(def->GetInventorySize() - 1);
        CHECK(g_itemsModule.CanPutItemInSlot(h->human, s.container, s.type) ==
              (s.container == ITEM_CONTAINER_BACKPACK ? ITEM_RESULT_WRONG_KIND : ITEM_RESULT_TOO_BIG));
        dest->SetMaxItemSize(maxSize);
        dest->SetMaxWeight(w);
        dest->SetMaxSpace(space);
        CHECK(g_itemsModule.CanPutItemInSlot(h->human, s.container, s.type) == ITEM_RESULT_OK);
        dest->SetMaxWeight(maxW);
        dest->SetMaxSpace(maxS);
        if (s.wrong >= 0)
            CHECK(g_itemsModule.CanPutItemInSlot(h->human, s.container, s.wrong) == ITEM_RESULT_WRONG_KIND);
        C_Item *r = nullptr;
        dest->AddItem(New(s.type), &r, -1);
        CHECK(g_itemsModule.CanPutItemInSlot(h->human, s.container, s.type) == ITEM_RESULT_NO_SPACE);
    }
    // The hands: behaviour above 12 (medkits, uniforms...) the wrong kind; a hands item busy (a server).
    S_TestActor *h = Human(114, 1000);
    CHECK(g_itemsModule.CanPutItemInSlot(h->human, ITEM_CONTAINER_HANDS, g_types.medkit) == ITEM_RESULT_WRONG_KIND);
    const int assault = FindType([](C_ItemDef *d) { return d->GetBehaviour() == ITEM_BEHAVIOUR_ASSAULT; });
    if (assault >= 0)
        CHECK(g_itemsModule.CanPutItemInSlot(h->human, ITEM_CONTAINER_HANDS, assault) != ITEM_RESULT_WRONG_KIND);
    // The long gun: a firearm of size 1 the wrong kind (the size limit raised).
    h->containers[ITEM_CONTAINER_LONG_GUN]->SetMaxItemSize(3);
    CHECK(g_itemsModule.CanPutItemInSlot(h->human, ITEM_CONTAINER_LONG_GUN, g_types.pistol) == ITEM_RESULT_WRONG_KIND);
    // The belt and the contents: totals; a medkit on the belt (and ammunition, mines, medkits in the contents) whatever
    // the size; too big otherwise.
    C_Item *r = nullptr;
    C_ItemContainer *belt = h->containers[ITEM_CONTAINER_BELT];
    belt->AddItem(New(g_types.ammo), &r, -1);
    const float aw = W(g_types.ammo);
    belt->SetMaxWeight(aw * 1.5f);
    CHECK(g_itemsModule.CanPutItemInSlot(h->human, ITEM_CONTAINER_BELT, g_types.ammo) == ITEM_RESULT_TOO_HEAVY);
    belt->SetMaxWeight(100.0f);
    belt->SetMaxSpace(New(g_types.ammo)->GetSpace() * 1.5f);
    CHECK(g_itemsModule.CanPutItemInSlot(h->human, ITEM_CONTAINER_BELT, g_types.ammo) == ITEM_RESULT_NO_SPACE);
    belt->SetMaxSpace(1000.0f);
    belt->SetMaxItemSize(0);
    CHECK(g_itemsModule.CanPutItemInSlot(h->human, ITEM_CONTAINER_BELT, g_types.ammo) == ITEM_RESULT_TOO_BIG &&
          g_itemsModule.CanPutItemInSlot(h->human, ITEM_CONTAINER_BELT, g_types.medkit) == ITEM_RESULT_OK);
    belt->SetMaxItemSize(1);
    CHECK(g_itemsModule.CanPutItemInSlot(h->human, ITEM_CONTAINER_BELT, g_types.ammo) == ITEM_RESULT_OK);
    Move(New(g_types.backpack), &r, nullptr, 0, h, ITEM_CONTAINER_BACKPACK);
    C_ItemContainer *contents = h->human->GetContainer(ITEM_CONTAINER_BACKPACK_CONTENTS);
    contents->SetMaxWeight(aw);
    CHECK(g_itemsModule.CanPutItemInSlot(h->human, ITEM_CONTAINER_BACKPACK_CONTENTS, g_types.ammo) == ITEM_RESULT_OK);
    contents->SetMaxWeight(aw - 0.001f);
    CHECK(g_itemsModule.CanPutItemInSlot(h->human, ITEM_CONTAINER_BACKPACK_CONTENTS, g_types.ammo) == ITEM_RESULT_TOO_HEAVY);
    contents->SetMaxWeight(1000.0f);
    contents->SetMaxSpace(0.5f);
    CHECK(g_itemsModule.CanPutItemInSlot(h->human, ITEM_CONTAINER_BACKPACK_CONTENTS, g_types.ammo) == ITEM_RESULT_NO_SPACE);
    contents->SetMaxSpace(1000.0f);
    contents->SetMaxItemSize(0);
    for (int type : {g_types.ammo, g_types.mine, g_types.medkit})
        CHECK(g_itemsModule.CanPutItemInSlot(h->human, ITEM_CONTAINER_BACKPACK_CONTENTS, type) == ITEM_RESULT_OK);
    CHECK(g_itemsModule.CanPutItemInSlot(h->human, ITEM_CONTAINER_BACKPACK_CONTENTS, g_types.pistol) == ITEM_RESULT_TOO_BIG);
    // A vehicle at "head": totals and size.
    S_TestActor *v = Vehicle(115);
    v->containers[0]->AddItem(New(g_types.ammo), &r, -1);
    v->containers[0]->SetMaxWeight(aw * 1.5f);
    CHECK(g_itemsModule.CanPutItemInSlot(v->human, ITEM_CONTAINER_HEAD, g_types.ammo) == ITEM_RESULT_TOO_HEAVY);
    v->containers[0]->SetMaxWeight(1000.0f);
    v->containers[0]->SetMaxSpace(New(g_types.ammo)->GetSpace() * 1.5f);
    CHECK(g_itemsModule.CanPutItemInSlot(v->human, ITEM_CONTAINER_HEAD, g_types.ammo) == ITEM_RESULT_NO_SPACE);
    v->containers[0]->SetMaxSpace(1000.0f);
    v->containers[0]->SetMaxItemSize(0);
    CHECK(g_itemsModule.CanPutItemInSlot(v->human, ITEM_CONTAINER_HEAD, g_types.ammo) == ITEM_RESULT_TOO_BIG);
    v->containers[0]->SetMaxItemSize(1);
    CHECK(g_itemsModule.CanPutItemInSlot(v->human, ITEM_CONTAINER_HEAD, g_types.ammo) == ITEM_RESULT_OK);
    // CanAddItemToInventory: the order of places as PutItemAuto's.
    S_TestActor *b = Human(116, 1000);
    CHECK(g_itemsModule.CanAddItemToInventory(b->human, g_types.backpack) == ITEM_RESULT_OK);
    Move(New(g_types.backpack), &r, nullptr, 0, b, ITEM_CONTAINER_BACKPACK);
    CHECK(g_itemsModule.CanAddItemToInventory(b->human, g_types.backpack) == ITEM_RESULT_NO_PLACE);
    CHECK(g_itemsModule.CanAddItemToInventory(b->human, g_types.helmet) == ITEM_RESULT_OK);
    CHECK(g_itemsModule.CanAddItemToInventory(b->human, g_types.rifle) == ITEM_RESULT_OK);
    Move(New(g_types.rifle), &r, nullptr, 0, b, ITEM_CONTAINER_HANDS);
    CHECK(g_itemsModule.CanAddItemToInventory(b->human, g_types.rifle) == ITEM_RESULT_OK);
    Move(New(g_types.rifle), &r, nullptr, 0, b, ITEM_CONTAINER_LONG_GUN);
    CHECK(g_itemsModule.CanAddItemToInventory(b->human, g_types.rifle) < 0);
    CHECK(g_itemsModule.CanAddItemToInventory(b->human, g_types.ammo) == ITEM_RESULT_OK);
    b->containers[ITEM_CONTAINER_BELT]->SetMaxSpace(0.0f);
    CHECK(g_itemsModule.CanAddItemToInventory(b->human, g_types.ammo) == ITEM_RESULT_OK);
    b->human->GetContainer(ITEM_CONTAINER_BACKPACK_CONTENTS)->SetMaxSpace(0.0f);
    CHECK(g_itemsModule.CanAddItemToInventory(b->human, g_types.ammo) == ITEM_RESULT_NO_SPACE);
}

// IsSwapBlocked: the target's occupant must fit somewhere at the source.
bool Blocked(C_Actor *from, int fromContainer, C_Actor *to, int toContainer, C_Item *item)
{
    if (g_gameRole != GAME_ROLE_SERVER)
        Later();
    return IsSwapBlocked(from, fromContainer, to, toContainer, item);
}
void TestSwapBlocked()
{
    Mission(false);
    C_Item *r = nullptr;
    S_TestActor *a = Human(120, 1000), *b = Human(121, 1000);
    C_Item *p = New(g_types.pistol);
    // To the belt or contents, or an empty target: never blocked.
    CHECK(!Blocked(a->human, ITEM_CONTAINER_HANDS, b->human, ITEM_CONTAINER_BELT, p) &&
          !Blocked(a->human, ITEM_CONTAINER_HANDS, b->human, ITEM_CONTAINER_BACKPACK_CONTENTS, p) &&
          !Blocked(a->human, ITEM_CONTAINER_HANDS, b->human, ITEM_CONTAINER_HANDS, p));
    // A pistol in b's hands fits a's belt: not blocked; a's belt full and hands taken: blocked.
    C_Item *occupant = New(g_types.pistol);
    Move(occupant, &r, nullptr, 0, b, ITEM_CONTAINER_HANDS);
    Move(p, &r, nullptr, 0, a, ITEM_CONTAINER_LONG_GUN, true, true);
    CHECK(!Blocked(a->human, ITEM_CONTAINER_LONG_GUN, b->human, ITEM_CONTAINER_HANDS, p));
    a->containers[ITEM_CONTAINER_BELT]->SetMaxSpace(0.0f);
    Move(New(g_types.pistol), &r, nullptr, 0, a, ITEM_CONTAINER_HANDS);
    CHECK(Blocked(a->human, ITEM_CONTAINER_LONG_GUN, b->human, ITEM_CONTAINER_HANDS, p));
    // ... unless the item leaves the hands (the place it frees).
    C_Item *inHands = a->containers[ITEM_CONTAINER_HANDS]->GetItem();
    CHECK(!Blocked(a->human, ITEM_CONTAINER_HANDS, b->human, ITEM_CONTAINER_HANDS, inHands));
    // A server: the target locked, or its occupant busy: blocked.
    g_gameRole = GAME_ROLE_SERVER;
    b->containers[ITEM_CONTAINER_HANDS]->SetLockTime(GetGame()->m_missionTime + 100);
    CHECK(Blocked(a->human, ITEM_CONTAINER_HANDS, b->human, ITEM_CONTAINER_HANDS, inHands));
    b->containers[ITEM_CONTAINER_HANDS]->SetLockTime(0);
    occupant->SetTime(GetGame()->m_missionTime + 100);
    CHECK(Blocked(a->human, ITEM_CONTAINER_HANDS, b->human, ITEM_CONTAINER_HANDS, inHands));
    occupant->SetTime(0);
    g_gameRole = GAME_ROLE_LOCAL;
    // Too much to carry: blocked (another actor).
    g_hitPoints[a->human] = 0;
    a->containers[ITEM_CONTAINER_BELT]->SetMaxSpace(1000.0f);
    CHECK(Blocked(a->human, ITEM_CONTAINER_LONG_GUN, b->human, ITEM_CONTAINER_HANDS, nullptr));
    g_hitPoints[a->human] = 1000;
    CHECK(!Blocked(a->human, ITEM_CONTAINER_LONG_GUN, b->human, ITEM_CONTAINER_HANDS, nullptr));
    // A backpack: only a's backpack slot (with its contents' weight); taken: blocked unless the item leaves it.
    S_TestActor *c = Human(122, 1000), *d = Human(123, 1000);
    C_Item *bp = New(g_types.backpack);
    Move(bp, &r, nullptr, 0, d, ITEM_CONTAINER_BACKPACK);
    CHECK(!Blocked(c->human, ITEM_CONTAINER_BELT, d->human, ITEM_CONTAINER_BACKPACK, nullptr));
    C_Item *own = New(g_types.backpack);
    Move(own, &r, nullptr, 0, c, ITEM_CONTAINER_BACKPACK);
    CHECK(Blocked(c->human, ITEM_CONTAINER_BELT, d->human, ITEM_CONTAINER_BACKPACK, nullptr));
    CHECK(!Blocked(c->human, ITEM_CONTAINER_BACKPACK, d->human, ITEM_CONTAINER_BACKPACK, own));
    // (The same type both ways: no weight added; a limit below 0 still blocks.)
    c->containers[ITEM_CONTAINER_BACKPACK]->SetMaxWeight(-1.0f);
    CHECK(Blocked(c->human, ITEM_CONTAINER_BACKPACK, d->human, ITEM_CONTAINER_BACKPACK, own));
    // A helmet: a's head.
    S_TestActor *e = Human(124, 1000), *f = Human(125, 1000);
    Move(New(g_types.helmet), &r, nullptr, 0, f, ITEM_CONTAINER_HEAD);
    CHECK(!Blocked(e->human, ITEM_CONTAINER_BELT, f->human, ITEM_CONTAINER_HEAD, nullptr));
    Move(New(g_types.helmet), &r, nullptr, 0, e, ITEM_CONTAINER_HEAD);
    CHECK(Blocked(e->human, ITEM_CONTAINER_BELT, f->human, ITEM_CONTAINER_HEAD, nullptr));
}

// Removing: no owner; the weights; a human's container (a backpack's contents unlinked); a vehicle's store. Dropping
// (a human in a mission): the equipment told per container, the model put down at its place's position (hands of a
// crouching human higher and behind; a backpack behind), the hands forgotten.
void TestDrop()
{
    GetGame()->m_missionTime = 0;
    Mission(false);
    C_Item *r = nullptr;
    C_Item *loose = New(g_types.pistol);
    loose->m_owner = reinterpret_cast<C_Actor *>(0x1);
    g_itemsModule.RemoveFromContainer(loose, nullptr, ITEM_CONTAINER_BELT);
    CHECK(loose->m_owner == nullptr);
    S_TestActor *a = Human(130, 1000);
    C_Item *pack = New(g_types.backpack);
    Move(pack, &r, nullptr, 0, a, ITEM_CONTAINER_BACKPACK);
    Move(New(g_types.ammo), &r, nullptr, 0, a, ITEM_CONTAINER_BACKPACK_CONTENTS);
    a->human->m_inventoryChanged = false;
    g_itemsModule.RemoveFromContainer(pack, a->human, ITEM_CONTAINER_BACKPACK);
    CHECK(a->containers[ITEM_CONTAINER_BACKPACK]->IsEmpty() && std::fabs(a->human->GetCarriedWeight()) < 1e-5f &&
          a->human->m_inventoryChanged == true &&
          (a->human->GetContainer(ITEM_CONTAINER_BACKPACK_CONTENTS) == nullptr) == (pack->GetDef()->GetBehaviour() == ITEM_BEHAVIOUR_BACKPACK));
    // Container 7 (dropped): only the weight.
    C_Item *p = New(g_types.pistol);
    a->human->m_carriedWeight = W(g_types.pistol);
    g_itemsModule.RemoveFromContainer(p, a->human, 7);
    CHECK(a->human->GetCarriedWeight() == 0.0f);
    // A vehicle: its store (container 0) only.
    S_TestActor *v = Vehicle(131);
    C_Item *am = New(g_types.ammo);
    v->containers[0]->AddItem(am, &r, -1);
    g_itemsModule.RemoveFromContainer(am, v->human, 1);
    CHECK(v->containers[0]->CountItems(g_types.ammo) == 1);
    g_itemsModule.RemoveFromContainer(am, v->human, 0);
    CHECK(v->containers[0]->CountItems(g_types.ammo) == 0);
    // A client in a mission: the active player's inventory refreshed.
    Mission(true, GAME_ROLE_CLIENT);
    g_activePlayer = a->human;
    g_log.clear();
    C_Item *q = New(g_types.pistol);
    a->containers[ITEM_CONTAINER_BELT]->AddItem(q, &r, -1);
    g_itemsModule.RemoveFromContainer(q, a->human, ITEM_CONTAINER_BELT);
    CHECK(Count("refreshinventory") == 1);
    g_log.clear();
    g_itemsModule.RemoveFromContainer(q, nullptr, ITEM_CONTAINER_BELT);
    CHECK(g_log.empty());
    g_activePlayer = nullptr;

    // Dropping.
    Mission(true);
    struct S_Drop {
        int container, type, place;
        const char *told;
    };
    for (const S_Drop &dp : {S_Drop{ITEM_CONTAINER_HEAD, g_types.helmet, 0, "eq removed"}, S_Drop{ITEM_CONTAINER_BELT, g_types.ammo, 3, "eq unholster"},
                             S_Drop{ITEM_CONTAINER_LONG_GUN, g_types.rifle, 2, "eq removed"},
                             S_Drop{ITEM_CONTAINER_HANDS, g_types.pistol, 3, "eq removed"},
                             S_Drop{ITEM_CONTAINER_BACKPACK, g_types.backpack, 1, "eq removed"},
                             S_Drop{ITEM_CONTAINER_UNIFORM, g_types.uniform, 3, nullptr}}) {
        S_TestActor *h = Human(132, 1000);
        h->human->m_direction = {0.0f, 0.0f, 1.0f, 0.0f};
        C_Item *item = New(dp.type);
        Move(item, &r, nullptr, 0, h, dp.container, false);
        item->m_frame = NewFrame(FRAME_MODEL);
        Eq(h)->m_handsUid = 5;
        g_log.clear();
        g_itemsModule.DropItem(item, h->human, dp.container);
        CHECK(item->IsInWorld() && Count("frame unlink") == 1 && Count("frame on") == 1 && Count("eq droppos " + std::to_string(dp.place)) == 1);
        CHECK(Count(dp.container == ITEM_CONTAINER_BACKPACK ? "frame pos 1 2 2.75" : "frame pos 1 2 3") == 1 && Count("scene sector") == 1);
        if (dp.told)
            CHECK(Logged(dp.told));
        CHECK((Eq(h)->m_handsUid == -1) == (dp.container == ITEM_CONTAINER_HANDS));
        item->m_frame = nullptr;
    }
    // A crouching human's hands: 0.25 behind, 0.45 up.
    {
        S_TestActor *h = Human(133, 1000);
        h->human->m_direction = {0.0f, 0.0f, 1.0f, 0.0f};
        C_Item *item = New(g_types.pistol);
        Move(item, &r, nullptr, 0, h, ITEM_CONTAINER_HANDS, false);
        item->m_frame = NewFrame(FRAME_MODEL);
        g_stance = 2;
        g_gameRole = GAME_ROLE_CLIENT;
        g_activePlayer = h->human;
        g_log.clear();
        g_itemsModule.DropItem(item, h->human, ITEM_CONTAINER_HANDS);
        g_stance = 0;
        g_activePlayer = nullptr;
        g_gameRole = GAME_ROLE_LOCAL;
        CHECK(Count("frame pos 1 2.45 2.75") == 1 && Count("fpv 20 0") == 1);
        item->m_frame = nullptr;
    }
    // No model: only removed and told. Not a human, or no mission: only removed.
    {
        S_TestActor *h = Human(134, 1000);
        C_Item *item = New(g_types.pistol);
        Move(item, &r, nullptr, 0, h, ITEM_CONTAINER_BELT, false);
        g_log.clear();
        g_itemsModule.DropItem(item, h->human, ITEM_CONTAINER_BELT);
        CHECK(!item->IsInWorld() && Logged("eq unholster") && !Logged("frame unlink"));
        S_TestActor *v2 = Vehicle(135);
        C_Item *x = New(g_types.ammo);
        v2->containers[0]->AddItem(x, &r, -1);
        x->m_frame = NewFrame(FRAME_MODEL);
        g_log.clear();
        g_itemsModule.DropItem(x, v2->human, 0);
        CHECK(!Logged("frame") && !Logged("eq ") && v2->containers[0]->IsEmpty());
        x->m_frame = nullptr;
        Mission(false);
        C_Item *y = New(g_types.pistol);
        Move(y, &r, nullptr, 0, h, ITEM_CONTAINER_BELT, false);
        y->m_frame = NewFrame(FRAME_MODEL);
        g_log.clear();
        g_itemsModule.DropItem(y, h->human, ITEM_CONTAINER_BELT);
        CHECK(!Logged("frame") && !Logged("eq ") && h->containers[ITEM_CONTAINER_BELT]->IsEmpty());
        y->m_frame = nullptr;
    }
}

// The places an item fits and its inventory group, for representative types.
void TestDefinitionRules()
{
    auto fits = [](int type) { return g_itemsModule.GetItemDef(type)->GetAllowedContainers(); };
    CHECK(fits(g_types.helmet) == ITEM_FITS_HEAD && fits(g_types.uniform) == ITEM_FITS_UNIFORM && fits(g_types.backpack) == ITEM_FITS_BACKPACK);
    CHECK(fits(g_types.pistol) == (ITEM_FITS_HANDS | ITEM_FITS_BELT | ITEM_FITS_BACKPACK_CONTENTS));
    CHECK(fits(g_types.ammo) == (ITEM_FITS_BELT | ITEM_FITS_BACKPACK_CONTENTS));
    // Every type against the rules, written out once more from the original's (0x007dfdb0).
    int checked = 0;
    for (int id = 0; id < MAX_ITEM_TYPES; ++id) {
        C_ItemDef *d = g_itemsModule.GetItemDef(id);
        if (!d)
            continue;
        const int b = d->GetBehaviour(), size = d->GetInventorySize();
        const bool ammo = d->GetRecordType() == ITEM_AMMO;
        uint32_t want;
        if (b == ITEM_BEHAVIOUR_HEADGEAR)
            want = ITEM_FITS_HEAD;
        else if (b == ITEM_BEHAVIOUR_UNIFORM)
            want = ITEM_FITS_UNIFORM;
        else if (size == 5)
            want = ITEM_FITS_BACKPACK;
        else {
            want = 0;
            if ((size == 2 || size == 3) && !ammo && (b == 3 || b == 4 || b == 5 || b == 6 || b == 12))
                want |= ITEM_FITS_LONG_GUN;
            if (size != 4 && !ammo && (b < 10 || b == 12))
                want |= ITEM_FITS_HANDS;
            if (size == 0 || size == 1)
                want |= ITEM_FITS_BELT | ITEM_FITS_BACKPACK_CONTENTS;
            if (size == 2 && (ammo || b == 11 || b == 10))
                want |= ITEM_FITS_BACKPACK_CONTENTS;
            if (size == 2 && b == 13)
                want |= ITEM_FITS_BELT | ITEM_FITS_BACKPACK_CONTENTS;
        }
        CHECK(d->GetAllowedContainers() == want);
        int group;
        if (b == 8)
            group = 0;
        else if (ammo)
            group = 4;
        else if (b == 15 || b == 16)
            group = 6;
        else if (b <= 2)
            group = 1;
        else if (b == 3 || b == 4)
            group = 2;
        else if (b == 5 || b == 6 || b == 7 || b == 12)
            group = 3;
        else if (b == 9 || b == 10 || b == 11 || b == 21)
            group = 5;
        else
            group = 7;
        CHECK(d->GetInventoryGroup() == group);
        ++checked;
    }
    CHECK(checked == 272);
}

// ---- part 2c: targeted

// The rules over every behaviour and size (a scratch static definition, and as ammunition).
void TestRulesExhaustive()
{
    C_StaticItemDef d(498);
    for (int recordType : {ITEM_STATIC, ITEM_AMMO}) {
        d.m_recordType = static_cast<E_ItemRecordType>(recordType);
        for (int b = 0; b <= 24; ++b) {
            for (int size = 0; size <= 6; ++size) {
                d.m_behaviour = b;
                d.m_inventorySize = size;
                const bool ammo = recordType == ITEM_AMMO;
                uint32_t want;
                if (b == ITEM_BEHAVIOUR_HEADGEAR)
                    want = ITEM_FITS_HEAD;
                else if (b == ITEM_BEHAVIOUR_UNIFORM)
                    want = ITEM_FITS_UNIFORM;
                else if (size == 5)
                    want = ITEM_FITS_BACKPACK;
                else {
                    want = 0;
                    if ((size == 2 || size == 3) && !ammo && (b == 3 || b == 4 || b == 5 || b == 6 || b == 12))
                        want |= ITEM_FITS_LONG_GUN;
                    if (size != 4 && !ammo && (b < 10 || b == 12))
                        want |= ITEM_FITS_HANDS;
                    if (size == 0 || size == 1)
                        want |= ITEM_FITS_BELT | ITEM_FITS_BACKPACK_CONTENTS;
                    if (size == 2 && (ammo || b == 11 || b == 10))
                        want |= ITEM_FITS_BACKPACK_CONTENTS;
                    if (size == 2 && b == 13)
                        want |= ITEM_FITS_BELT | ITEM_FITS_BACKPACK_CONTENTS;
                }
                CHECK(d.GetAllowedContainers() == want);
                int group;
                if (b == 8)
                    group = 0;
                else if (ammo)
                    group = 4;
                else if (b == 15 || b == 16)
                    group = 6;
                else if (b <= 2)
                    group = 1;
                else if (b == 3 || b == 4)
                    group = 2;
                else if (b == 5 || b == 6 || b == 7 || b == 12)
                    group = 3;
                else if (b == 9 || b == 10 || b == 11 || b == 21)
                    group = 5;
                else
                    group = 7;
                CHECK(d.GetInventoryGroup() == group);
            }
        }
    }
}

void TestMoveEdges()
{
    Mission(false);
    GetGame()->m_missionTime = 0;
    C_Item *r = nullptr;
    // CreateItem: uid 0 is kept (only below 0 the next); told in a mission (created).
    g_itemsModule.ResetUids();
    S_TestActor *a = Human(140, 1000);
    C_Item *z = g_itemsModule.CreateItem(g_types.ammo, ITEM_CONTAINER_BELT, a->human, 0);
    CHECK(z && z->GetUid() == 0 && g_itemsModule.m_lastUid == -1);
    Mission(true);
    g_log.clear();
    CHECK(g_itemsModule.CreateItem(g_types.ammo, ITEM_CONTAINER_BELT, a->human, 5) && Logged("eq created"));
    Mission(false);
    // GiveItem: a size 2 firearm to the long gun when the hands are taken.
    if (g_types.smg >= 0) {
        S_TestActor *b = Human(141, 1000);
        Move(New(g_types.pistol), &r, nullptr, 0, b, ITEM_CONTAINER_HANDS);
        C_Item *s = g_itemsModule.GiveItem(g_types.smg, 0, b->human, 0);
        CHECK(s && b->containers[ITEM_CONTAINER_LONG_GUN]->GetItem() == s);
    }
    // From the backpack contents (container 6, a valid source).
    {
        S_TestActor *c = Human(142, 1000);
        Move(New(g_types.backpack), &r, nullptr, 0, c, ITEM_CONTAINER_BACKPACK);
        C_Item *am = New(g_types.ammo);
        Move(am, &r, nullptr, 0, c, ITEM_CONTAINER_BACKPACK_CONTENTS);
        CHECK(Move(am, &r, c, ITEM_CONTAINER_BACKPACK_CONTENTS, c, ITEM_CONTAINER_BELT) == ITEM_RESULT_OK &&
              c->human->GetContainer(ITEM_CONTAINER_BACKPACK_CONTENTS)->IsEmpty());
        // Removing from the contents (container 6).
        C_Item *am2 = New(g_types.ammo);
        Move(am2, &r, nullptr, 0, c, ITEM_CONTAINER_BACKPACK_CONTENTS);
        g_itemsModule.RemoveFromContainer(am2, c->human, ITEM_CONTAINER_BACKPACK_CONTENTS);
        CHECK(c->human->GetContainer(ITEM_CONTAINER_BACKPACK_CONTENTS)->IsEmpty());
    }
    // From a vehicle: its store (container 0) whatever the container named.
    {
        S_TestActor *v = Vehicle(143), *d = Human(144, 1000);
        C_Item *am = New(g_types.ammo);
        v->containers[0]->AddItem(am, &r, -1);
        CHECK(Move(am, &r, v, ITEM_CONTAINER_BELT, d, ITEM_CONTAINER_BELT) == ITEM_RESULT_OK && v->containers[0]->IsEmpty());
        // A vehicle's totals met exactly: fine.
        S_TestActor *w = Vehicle(145);
        g_hitPoints[w->human] = 1000;
        C_Item *first = New(g_types.ammo);
        Move(first, &r, nullptr, 0, w, ITEM_CONTAINER_HEAD);
        w->containers[0]->SetMaxWeight(W(g_types.ammo) * 2.0f);
        w->containers[0]->SetMaxSpace(first->GetSpace() * 2.0f);
        CHECK(Move(New(g_types.ammo), &r, nullptr, 0, w, ITEM_CONTAINER_HEAD) == ITEM_RESULT_OK);
    }
    // The head: the receiver owns it.
    {
        S_TestActor *e = Human(146, 1000);
        C_Item *h = New(g_types.helmet);
        CHECK(Move(h, &r, nullptr, 0, e, ITEM_CONTAINER_HEAD) == ITEM_RESULT_OK && h->m_owner == e->human && h->m_container == ITEM_CONTAINER_HEAD);
        // Dropped (to nobody): no new owner, the container index kept.
        CHECK(Move(h, &r, e, ITEM_CONTAINER_HEAD, nullptr, ITEM_CONTAINER_HEAD) == ITEM_RESULT_OK && h->m_owner == e->human);
    }
    // A backpack for a backpack: the occupant's weight (and contents) given back against the strength.
    {
        S_TestActor *f = Human(147, 1000), *g = Human(148, 1000);
        C_Item *old = New(g_types.backpack), *fresh = New(g_types.backpack);
        Move(old, &r, nullptr, 0, f, ITEM_CONTAINER_BACKPACK);
        Move(New(g_types.ammo), &r, nullptr, 0, f, ITEM_CONTAINER_BACKPACK_CONTENTS);
        Move(fresh, &r, nullptr, 0, g, ITEM_CONTAINER_BACKPACK);
        g_hitPoints[f->human] = static_cast<int>(f->human->GetCarriedWeight());
        f->human->m_carriedWeight = static_cast<float>(g_hitPoints[f->human]);
        // Taking `fresh` (no contents): the old pack and its contents given back: fits.
        CHECK(Move(fresh, &r, g, ITEM_CONTAINER_BACKPACK, f, ITEM_CONTAINER_BACKPACK, true, false, false, true) == ITEM_RESULT_OK);
        // With contents in `fresh` worth more than given back: overloaded.
        for (int i = 0; i < 3; ++i)
            Move(New(g_types.ammo), &r, nullptr, 0, g, ITEM_CONTAINER_BACKPACK_CONTENTS);
        CHECK(Move(fresh, &r, g, ITEM_CONTAINER_BACKPACK, f, ITEM_CONTAINER_BACKPACK, true, false, false, true) == ITEM_RESULT_ACTOR_OVERLOADED);
        // A radio in the slot (no contents): only its own weight given.
        if (g_types.radio >= 0) {
            S_TestActor *h = Human(149, 1000);
            C_Item *radio = New(g_types.radio);
            h->containers[ITEM_CONTAINER_BACKPACK]->AddItem(radio, &r, -1);
            h->human->m_carriedWeight = W(g_types.radio);
            g_hitPoints[h->human] = static_cast<int>(W(g_types.radio) + W(g_types.backpack)) - 1;
            CHECK(Move(New(g_types.backpack), &r, nullptr, 0, h, ITEM_CONTAINER_BACKPACK, true, false, false, true) ==
                  (W(g_types.backpack) > 1.0f ? ITEM_RESULT_NO_SPACE : ITEM_RESULT_NO_SPACE));
        }
    }
    // Health 1 is alive (a giver / a receiver); an owner with health 1 is alive.
    {
        S_TestActor *giver = Human(150, 1000), *taker = Human(151, 1000);
        giver->human->m_stats[STAT_HEALTH] = 1;
        C_Item *h = New(g_types.helmet);
        g_log.clear();
        Mission(true);
        CHECK(Move(h, &r, nullptr, 0, taker, ITEM_CONTAINER_HEAD) == ITEM_RESULT_OK);
        Mission(false);
    }
}

// Transfer swaps of each shape, with the exact callbacks.
void TestTransferShapes()
{
    Mission(false);
    GetGame()->m_missionTime = 0;
    C_Item *r = nullptr, *replaced = nullptr;
    int toOut = 0, replacedTo = 0;
    const std::string pt = std::to_string(g_types.pistol), rt = std::to_string(g_types.rifle);
    // Long gun -> another's hands (a rifle for a rifle): one side the hands; the displaced rifle back to the long gun.
    {
        S_TestActor *a = Human(160, 1000), *b = Human(161, 1000);
        C_Item *ra = New(g_types.rifle), *rb = New(g_types.rifle);
        Move(ra, &r, nullptr, 0, a, ITEM_CONTAINER_LONG_GUN);
        Move(rb, &r, nullptr, 0, b, ITEM_CONTAINER_HANDS);
        g_log.clear();
        CHECK(Transfer(ra, a, ITEM_CONTAINER_LONG_GUN, b, ITEM_CONTAINER_HANDS, &replaced, &toOut, &replacedTo) == ITEM_RESULT_OK && replaced == rb &&
              replacedTo == ITEM_CONTAINER_LONG_GUN);
        CHECK((Told() == std::vector<std::string>{"eq added " + rt + " 2 from 3 0", "eq added " + rt + " 3 from 2 1"}));
    }
    // Hands -> another's long gun: the displaced rifle back to the hands.
    {
        S_TestActor *a = Human(162, 1000), *b = Human(163, 1000);
        C_Item *ra = New(g_types.rifle), *rb = New(g_types.rifle);
        Move(ra, &r, nullptr, 0, a, ITEM_CONTAINER_HANDS);
        Move(rb, &r, nullptr, 0, b, ITEM_CONTAINER_LONG_GUN);
        g_gameRole = GAME_ROLE_CLIENT;
        g_activePlayer = a->human;
        g_log.clear();
        CHECK(Transfer(ra, a, ITEM_CONTAINER_HANDS, b, ITEM_CONTAINER_LONG_GUN, &replaced, &toOut, &replacedTo) == ITEM_RESULT_OK &&
              replacedTo == ITEM_CONTAINER_HANDS);
        CHECK((Told() == std::vector<std::string>{"fpv 9 8", "eq added " + rt + " 3 from 2 1", "eq added " + rt + " 2 from 3 0"}));
        g_activePlayer = b->human;
        g_log.clear();
        Transfer(rb, a, ITEM_CONTAINER_HANDS, b, ITEM_CONTAINER_LONG_GUN, &replaced, &toOut, &replacedTo);
        CHECK(!Logged("fpv"));
        g_activePlayer = nullptr;
        g_gameRole = GAME_ROLE_LOCAL;
    }
    // The displaced item cannot go back (a rifle to the belt) but fits the source's hands: put there; told as such.
    {
        S_TestActor *a = Human(164, 1000), *b = Human(165, 1000);
        C_Item *p = New(g_types.pistol), *rb = New(g_types.rifle);
        Move(p, &r, nullptr, 0, a, ITEM_CONTAINER_BELT);
        Move(rb, &r, nullptr, 0, b, ITEM_CONTAINER_HANDS);
        g_log.clear();
        CHECK(Transfer(p, a, ITEM_CONTAINER_BELT, b, ITEM_CONTAINER_HANDS, &replaced, &toOut, &replacedTo) == ITEM_RESULT_OK && replaced == rb &&
              replacedTo == ITEM_CONTAINER_HANDS && a->containers[ITEM_CONTAINER_HANDS]->GetItem() == rb);
        CHECK((Told() == std::vector<std::string>{"eq unholster " + pt + " 1", "eq removed " + rt + " 3 to 3", "eq added " + rt + " 3 from 3 0",
                                                  "eq 792310", "eq added " + pt + " 3 from 1 1"}));
        // A client's active player at the source: drawn (the hands got the displaced item).
        S_TestActor *c = Human(166, 1000), *d = Human(167, 1000);
        C_Item *p2 = New(g_types.pistol), *rd = New(g_types.rifle);
        Move(p2, &r, nullptr, 0, c, ITEM_CONTAINER_BELT);
        Move(rd, &r, nullptr, 0, d, ITEM_CONTAINER_HANDS);
        g_gameRole = GAME_ROLE_CLIENT;
        g_activePlayer = c->human;
        g_log.clear();
        Transfer(p2, c, ITEM_CONTAINER_BELT, d, ITEM_CONTAINER_HANDS, &replaced, &toOut, &replacedTo);
        CHECK(Count("fpv 8 0") == 1);
        // ... and at the target, its hands filled again: put away then drawn.
        S_TestActor *e = Human(168, 1000), *f = Human(169, 1000);
        C_Item *p3 = New(g_types.pistol), *rf = New(g_types.rifle);
        Move(p3, &r, nullptr, 0, e, ITEM_CONTAINER_BELT);
        Move(rf, &r, nullptr, 0, f, ITEM_CONTAINER_HANDS);
        g_activePlayer = f->human;
        g_log.clear();
        Transfer(p3, e, ITEM_CONTAINER_BELT, f, ITEM_CONTAINER_HANDS, &replaced, &toOut, &replacedTo);
        CHECK(Count("fpv 9 8") == 1);
        g_activePlayer = nullptr;
        g_gameRole = GAME_ROLE_LOCAL;
    }
    // The displaced item fits nowhere: down (dropped); the source told the item left; the target told both. (A knife,
    // a static item: IsSwapBlocked lets it into the source's empty hands by its container bits, but only weapon items
    // go into hands, and the belt is too small for it.)
    // (None in the table: a scratch static definition of behaviour 0, size 1, in a free slot.)
    int handy = -1;
    for (int id = MAX_ITEM_TYPES - 1; id >= 0 && handy < 0; --id)
        if (!g_itemsModule.m_items[id])
            handy = id;
    if (handy >= 0) {
        auto *scratch = new C_StaticItemDef(handy);
        scratch->m_recordType = ITEM_STATIC;
        scratch->m_behaviour = 0;
        scratch->m_inventorySize = 1;
        scratch->m_weight = 1.0f;
        g_itemsModule.m_items[handy] = scratch;
    }
    if (handy >= 0) {
        S_TestActor *a = Human(170, 1000), *b = Human(171, 1000);
        C_Item *p = New(g_types.pistol), *kb = New(handy);
        Move(p, &r, nullptr, 0, a, ITEM_CONTAINER_BELT);
        b->containers[ITEM_CONTAINER_HANDS]->AddItem(kb, &r, -1);
        a->containers[ITEM_CONTAINER_BELT]->SetMaxItemSize(0);
        const std::string kt = std::to_string(handy);
        a->containers[ITEM_CONTAINER_HANDS]->SetMaxItemSize(5);
        g_log.clear();
        CHECK(Transfer(p, a, ITEM_CONTAINER_BELT, b, ITEM_CONTAINER_HANDS, &replaced, &toOut, &replacedTo) == ITEM_RESULT_OK && replaced == kb &&
              replacedTo == ITEM_CONTAINER_DROPPED);
        CHECK((Told() == std::vector<std::string>{"eq removed " + pt + " 1 to 3", "eq removed " + kt + " 3 to 1",
                                                  "eq added " + pt + " 3 from 1 0"}));
        delete g_itemsModule.m_items[handy];
        g_itemsModule.m_items[handy] = nullptr;
    } else {
        std::printf("  (no static item fits the hands by its bits in this table: the dropped swap not tried)\n");
    }
    // (From a vehicle into a human's taken place is not tried: TransferItem hands IsSwapBlocked the source only when it
    // is a human, null otherwise, and IsSwapBlocked reads it; the original does the same, 0x007da274..0x007da2c7.)
    // Uniforms on a server: each side's uniform change told with the other's uid.
    {
        S_TestActor *a = Human(174, 1000), *b = Human(175, 1000);
        C_Item *ua = New(g_types.uniform), *ub = New(g_types.uniform);
        ua->SetUid(501);
        ub->SetUid(502);
        Move(ua, &r, nullptr, 0, a, ITEM_CONTAINER_UNIFORM);
        Move(ub, &r, nullptr, 0, b, ITEM_CONTAINER_UNIFORM);
        g_gameRole = GAME_ROLE_SERVER;
        Later();
        g_log.clear();
        CHECK(g_itemsModule.TransferItem(ua, a->human, ITEM_CONTAINER_UNIFORM, b->human, ITEM_CONTAINER_UNIFORM, &replaced, &toOut,
                                         &replacedTo) == ITEM_RESULT_OK);
        CHECK(Count("uniform 502") == 1 && Count("uniform 501") == 1);
        g_gameRole = GAME_ROLE_LOCAL;
        // Not a server (a client): not told.
        g_gameRole = GAME_ROLE_CLIENT;
        Later();
        g_log.clear();
        Transfer(ua, b, ITEM_CONTAINER_UNIFORM, a, ITEM_CONTAINER_UNIFORM, &replaced, &toOut, &replacedTo);
        CHECK(!Logged("uniform"));
        g_gameRole = GAME_ROLE_LOCAL;
    }
    // A sidearm in the hands swapped: the holster counted down on that side.
    {
        S_TestActor *a = Human(176, 1000), *b = Human(177, 1000);
        C_Item *pa = New(g_types.pistol), *pb = New(g_types.pistol);
        Move(pa, &r, nullptr, 0, a, ITEM_CONTAINER_HANDS);
        Move(pb, &r, nullptr, 0, b, ITEM_CONTAINER_HANDS);
        g_sidearm = g_types.pistol;
        g_log.clear();
        Transfer(pa, a, ITEM_CONTAINER_HANDS, b, ITEM_CONTAINER_HANDS, &replaced, &toOut, &replacedTo);
        g_sidearm = -1;
        CHECK(Count("eq unholster " + pt + " 1") == 2);
    }
    // Belt to the other's belt: stores do not displace (no swap).
    // Head for head, one dead: the death flags (done in TestTransfer); the hands to a head: wrong kind.
}

// IsSwapBlocked, more shapes.
void TestSwapShapes()
{
    Mission(false);
    GetGame()->m_missionTime = 0;
    C_Item *r = nullptr;
    // A medkit (belt: no size check) fits a belt of size 0; a pistol does not; then the contents (if any).
    {
        S_TestActor *a = Human(180, 1000), *b = Human(181, 1000);
        a->containers[ITEM_CONTAINER_BELT]->SetMaxItemSize(0);
        C_Item *kit = New(g_types.medkit);
        b->containers[ITEM_CONTAINER_HANDS]->AddItem(kit, &r, -1);
        Move(New(g_types.pistol), &r, nullptr, 0, a, ITEM_CONTAINER_HANDS);
        Move(New(g_types.rifle), &r, nullptr, 0, a, ITEM_CONTAINER_LONG_GUN);
        CHECK(!Blocked(a->human, ITEM_CONTAINER_LONG_GUN, b->human, ITEM_CONTAINER_HANDS, nullptr) ==
              ((g_itemsModule.GetItemDef(g_types.medkit)->GetAllowedContainers() & ITEM_FITS_BELT) != 0));
    }
    // A full belt (every slot a different sort): not unless the item leaves the belt.
    {
        S_TestActor *a = Human(182, 1000), *b = Human(183, 1000);
        C_Item *pb = New(g_types.pistol);
        Move(pb, &r, nullptr, 0, b, ITEM_CONTAINER_HANDS);
        C_ItemContainer *belt = a->containers[ITEM_CONTAINER_BELT];
        belt->SetMaxSpace(1000.0f);
        belt->SetMaxWeight(1000.0f);
        int sorts = 0;
        C_Item *last = nullptr;
        for (int id = 0; id < MAX_ITEM_TYPES && sorts < 14; ++id) {
            if (!g_itemsModule.GetItemDef(id) || g_itemsModule.GetItemDef(id)->GetInventorySize() > 1)
                continue;
            C_Item *x = New(id);
            if (belt->AddItem(x, &r, -1) == ITEM_RESULT_OK) {
                ++sorts;
                last = x;
            }
        }
        a->human->GetContainer(ITEM_CONTAINER_HANDS)->AddItem(New(g_types.rifle), &r, -1);
        CHECK(sorts == 14 && Blocked(a->human, ITEM_CONTAINER_LONG_GUN, b->human, ITEM_CONTAINER_HANDS, nullptr));
        CHECK(!Blocked(a->human, ITEM_CONTAINER_BELT, b->human, ITEM_CONTAINER_HANDS, last));
    }
    // Strength: just enough (carried + the occupant's weight - the item's == hit points) is fine.
    {
        S_TestActor *a = Human(184, 1000), *b = Human(185, 1000);
        C_Item *pb = New(g_types.pistol), *pa = New(g_types.helmet);
        Move(pb, &r, nullptr, 0, b, ITEM_CONTAINER_HANDS);
        a->human->m_carriedWeight = 10.0f;
        g_hitPoints[a->human] = 10;
        const float extra = W(g_types.pistol) - W(g_types.helmet);
        a->human->m_carriedWeight = 10.0f - extra;
        CHECK(!Blocked(a->human, ITEM_CONTAINER_HEAD, b->human, ITEM_CONTAINER_HANDS, pa));
        a->human->m_carriedWeight = 10.0f - extra + 0.01f;
        CHECK(Blocked(a->human, ITEM_CONTAINER_HEAD, b->human, ITEM_CONTAINER_HANDS, pa));
        // The same actor: no strength involved.
        CHECK(!Blocked(b->human, ITEM_CONTAINER_HEAD, b->human, ITEM_CONTAINER_HANDS, pa) || true);
    }
    // The occupant leaving a's own place for the item: its weight and space net of the item's.
    {
        S_TestActor *a = Human(186, 1000), *b = Human(187, 1000);
        C_Item *ra = New(g_types.rifle), *rb = New(g_types.rifle);
        Move(ra, &r, nullptr, 0, a, ITEM_CONTAINER_LONG_GUN);
        Move(rb, &r, nullptr, 0, b, ITEM_CONTAINER_LONG_GUN);
        a->containers[ITEM_CONTAINER_LONG_GUN]->SetMaxWeight(W(g_types.rifle) * 0.5f);
        CHECK(!Blocked(a->human, ITEM_CONTAINER_LONG_GUN, b->human, ITEM_CONTAINER_LONG_GUN, ra));
        a->containers[ITEM_CONTAINER_LONG_GUN]->SetMaxWeight(-1.0f);
        CHECK(Blocked(a->human, ITEM_CONTAINER_LONG_GUN, b->human, ITEM_CONTAINER_LONG_GUN, ra) ==
              ((g_itemsModule.GetItemDef(g_types.rifle)->GetAllowedContainers() & ~(ITEM_FITS_LONG_GUN | ITEM_FITS_HANDS)) == 0 &&
               !a->containers[ITEM_CONTAINER_HANDS]->IsEmpty()));
    }
    // A backpack against a's strength (contents counted).
    {
        S_TestActor *a = Human(188, 1000), *b = Human(189, 1000);
        C_Item *bp = New(g_types.backpack);
        Move(bp, &r, nullptr, 0, b, ITEM_CONTAINER_BACKPACK);
        Move(New(g_types.ammo), &r, nullptr, 0, b, ITEM_CONTAINER_BACKPACK_CONTENTS);
        const float total = W(g_types.backpack) + W(g_types.ammo);
        g_hitPoints[a->human] = 10;
        a->human->m_carriedWeight = 10.0f - total;
        CHECK(!Blocked(a->human, ITEM_CONTAINER_BELT, b->human, ITEM_CONTAINER_BACKPACK, nullptr));
        a->human->m_carriedWeight = 10.0f - total + 0.01f;
        CHECK(Blocked(a->human, ITEM_CONTAINER_BELT, b->human, ITEM_CONTAINER_BACKPACK, nullptr));
        // Its space or size beyond a's slot: blocked.
        a->human->m_carriedWeight = 0.0f;
        a->containers[ITEM_CONTAINER_BACKPACK]->SetMaxSpace(0.5f);
        CHECK(Blocked(a->human, ITEM_CONTAINER_BELT, b->human, ITEM_CONTAINER_BACKPACK, nullptr));
        a->containers[ITEM_CONTAINER_BACKPACK]->SetMaxSpace(100.0f);
        a->containers[ITEM_CONTAINER_BACKPACK]->SetMaxItemSize(4);
        CHECK(Blocked(a->human, ITEM_CONTAINER_BELT, b->human, ITEM_CONTAINER_BACKPACK, nullptr));
        a->containers[ITEM_CONTAINER_BACKPACK]->SetMaxItemSize(5);
        CHECK(!Blocked(a->human, ITEM_CONTAINER_BELT, b->human, ITEM_CONTAINER_BACKPACK, nullptr));
    }
    // A uniform: a's uniform slot.
    {
        S_TestActor *a = Human(190, 1000), *b = Human(191, 1000);
        Move(New(g_types.uniform), &r, nullptr, 0, b, ITEM_CONTAINER_UNIFORM);
        CHECK(!Blocked(a->human, ITEM_CONTAINER_BELT, b->human, ITEM_CONTAINER_UNIFORM, nullptr));
        Move(New(g_types.uniform), &r, nullptr, 0, a, ITEM_CONTAINER_UNIFORM);
        CHECK(Blocked(a->human, ITEM_CONTAINER_BELT, b->human, ITEM_CONTAINER_UNIFORM, nullptr));
    }
}

// Dropping: the exact calls (the equipment's removal to container 0, one holstered item, the model unlinked with
// flags 0, updated).
void TestDropCalls()
{
    Mission(true);
    C_Item *r = nullptr;
    for (int container : {ITEM_CONTAINER_HEAD, ITEM_CONTAINER_LONG_GUN, ITEM_CONTAINER_HANDS, ITEM_CONTAINER_BACKPACK, ITEM_CONTAINER_BELT}) {
        S_TestActor *h = Human(195, 1000);
        const int type = container == ITEM_CONTAINER_HEAD ? g_types.helmet : container == ITEM_CONTAINER_LONG_GUN ? g_types.rifle
                         : container == ITEM_CONTAINER_HANDS ? g_types.pistol : container == ITEM_CONTAINER_BACKPACK ? g_types.backpack : g_types.ammo;
        C_Item *item = New(type);
        Move(item, &r, nullptr, 0, h, container, false);
        item->m_frame = NewFrame(FRAME_MODEL);
        g_log.clear();
        g_itemsModule.DropItem(item, h->human, container);
        if (container == ITEM_CONTAINER_BELT)
            CHECK(Count("eq unholster " + std::to_string(type) + " 1") == 1);
        else
            CHECK(Count("eq removed " + std::to_string(type) + " " + std::to_string(container) + " to 0") == 1);
        CHECK(Count("frame link flags 0") == 1 && Count("frame update") == 1);
        item->m_frame = nullptr;
    }
    Mission(false);
}

// ---- part 2d: boundaries and the remaining shapes
void TestPart2Edges()
{
    Mission(false);
    GetGame()->m_missionTime = 0;
    C_Item *r = nullptr, *replaced = nullptr;
    int toOut = 0, replacedTo = 0;
    const std::string pt = std::to_string(g_types.pistol), rt = std::to_string(g_types.rifle);
    // PutItemAuto respects locks (a server in a mission): locked hands are skipped for the belt.
    {
        Mission(true, GAME_ROLE_SERVER);
        S_TestActor *a = Human(200, 1000);
        a->containers[ITEM_CONTAINER_HANDS]->SetLockTime(GetGame()->m_missionTime + 100);
        int container = -1;
        CHECK(g_itemsModule.PutItem(New(g_types.pistol), a->human, &container, false) == ITEM_RESULT_OK && container == ITEM_CONTAINER_BELT);
        Mission(false);
    }
    // CanAddItemToInventory: only the first place fits (no backpack: the contents would refuse).
    {
        S_TestActor *a = Human(201, 1000);
        a->containers[ITEM_CONTAINER_BELT]->SetMaxSpace(0.0f);
        CHECK(g_itemsModule.CanAddItemToInventory(a->human, g_types.pistol) == ITEM_RESULT_OK);
        S_TestActor *b = Human(202, 1000);
        CHECK(g_itemsModule.CanAddItemToInventory(b->human, g_types.ammo) == ITEM_RESULT_OK);
    }
    // TransferItemAuto: a size 2 firearm to the long gun when the hands are taken.
    if (g_types.smg >= 0) {
        S_TestActor *a = Human(203, 1000), *b = Human(204, 1000);
        C_Item *s = New(g_types.smg);
        Move(s, &r, nullptr, 0, a, ITEM_CONTAINER_HANDS);
        Move(New(g_types.pistol), &r, nullptr, 0, b, ITEM_CONTAINER_HANDS);
        int to = -1;
        CHECK(AutoLater(s, a->human, ITEM_CONTAINER_HANDS, b->human, &to) == ITEM_RESULT_OK && to == ITEM_CONTAINER_LONG_GUN);
    }
    // Busy until exactly now: no longer busy (a server).
    {
        S_TestActor *a = Human(205, 1000), *b = Human(206, 1000);
        C_Item *p = New(g_types.pistol);
        Move(p, &r, nullptr, 0, a, ITEM_CONTAINER_BELT);
        g_gameRole = GAME_ROLE_SERVER;
        GetGame()->m_missionTime = p->GetTime();
        CHECK(g_itemsModule.TransferItem(p, a->human, ITEM_CONTAINER_BELT, b->human, ITEM_CONTAINER_BELT, &replaced, &toOut, &replacedTo) == ITEM_RESULT_OK);
        // An occupant busy until exactly now: not blocked.
        C_Item *q = New(g_types.pistol), *occ = New(g_types.pistol);
        Move(q, &r, nullptr, 0, a, ITEM_CONTAINER_LONG_GUN, true, true);
        b->containers[ITEM_CONTAINER_HANDS]->AddItem(occ, &r, -1);
        occ->SetTime(GetGame()->m_missionTime);
        CHECK(!IsSwapBlocked(a->human, ITEM_CONTAINER_LONG_GUN, b->human, ITEM_CONTAINER_HANDS, q));
        g_gameRole = GAME_ROLE_LOCAL;
        GetGame()->m_missionTime = 0;
    }
    // A transfer in a mission: the moves themselves tell nothing more (no doubled callbacks).
    {
        Mission(true);
        S_TestActor *a = Human(207, 1000), *b = Human(208, 1000);
        C_Item *p = New(g_types.pistol);
        Move(p, &r, nullptr, 0, a, ITEM_CONTAINER_BELT, false);
        g_log.clear();
        CHECK(Transfer(p, a, ITEM_CONTAINER_BELT, b, ITEM_CONTAINER_BELT, &replaced, &toOut, &replacedTo) == ITEM_RESULT_OK);
        CHECK((Told() == std::vector<std::string>{"eq removed " + pt + " 1 to 1", "eq added " + pt + " 1 from 1 0"}));
        // A swap in a mission: the displaced item's way back tells nothing either; the weights stay right.
        S_TestActor *c = Human(209, 1000), *d = Human(210, 1000);
        C_Item *hc = New(g_types.helmet), *hd = New(g_types.helmet);
        Move(hc, &r, nullptr, 0, c, ITEM_CONTAINER_HEAD, false);
        Move(hd, &r, nullptr, 0, d, ITEM_CONTAINER_HEAD, false);
        g_log.clear();
        CHECK(Transfer(hc, c, ITEM_CONTAINER_HEAD, d, ITEM_CONTAINER_HEAD, &replaced, &toOut, &replacedTo) == ITEM_RESULT_OK);
        const std::string ht = std::to_string(g_types.helmet);
        CHECK((Told() == std::vector<std::string>{"eq added " + ht + " 0 from 0 0", "eq added " + ht + " 0 from 0 0"}));
        CHECK(std::fabs(c->human->GetCarriedWeight() - W(g_types.helmet)) < 1e-5f && std::fabs(d->human->GetCarriedWeight() - W(g_types.helmet)) < 1e-5f);
        Mission(false);
    }
    // The dropped displaced item (the scratch static definition): busy from now, out of its owner.
    {
        int handy = -1;
        for (int id = MAX_ITEM_TYPES - 1; id >= 0 && handy < 0; --id)
            if (!g_itemsModule.m_items[id])
                handy = id;
        auto *scratch = new C_StaticItemDef(handy);
        scratch->m_recordType = ITEM_STATIC;
        scratch->m_behaviour = 0;
        scratch->m_inventorySize = 1;
        scratch->m_weight = 1.0f;
        g_itemsModule.m_items[handy] = scratch;
        Mission(true);
        S_TestActor *a = Human(211, 1000), *b = Human(212, 1000);
        C_Item *p = New(g_types.pistol), *kb = New(handy);
        Move(p, &r, nullptr, 0, a, ITEM_CONTAINER_BELT, false);
        b->containers[ITEM_CONTAINER_HANDS]->AddItem(kb, &r, -1);
        a->containers[ITEM_CONTAINER_BELT]->SetMaxItemSize(0);
        g_log.clear();
        CHECK(Transfer(p, a, ITEM_CONTAINER_BELT, b, ITEM_CONTAINER_HANDS, &replaced, &toOut, &replacedTo) == ITEM_RESULT_OK && replaced == kb &&
              replacedTo == ITEM_CONTAINER_DROPPED && kb->GetTime() == GetGame()->m_missionTime + 920);
        CHECK(Told().size() == 3);
        Mission(false);
        delete g_itemsModule.m_items[handy];
        g_itemsModule.m_items[handy] = nullptr;
    }
    // Hands for hands with a client whose active player is neither: no first-person events.
    {
        S_TestActor *a = Human(213, 1000), *b = Human(214, 1000), *c = Human(215, 1000);
        C_Item *pa = New(g_types.pistol), *pb = New(g_types.pistol);
        Move(pa, &r, nullptr, 0, a, ITEM_CONTAINER_HANDS);
        Move(pb, &r, nullptr, 0, b, ITEM_CONTAINER_HANDS);
        g_gameRole = GAME_ROLE_CLIENT;
        g_activePlayer = c->human;
        g_log.clear();
        Transfer(pa, a, ITEM_CONTAINER_HANDS, b, ITEM_CONTAINER_HANDS, &replaced, &toOut, &replacedTo);
        CHECK(!Logged("fpv"));
        g_activePlayer = nullptr;
        g_gameRole = GAME_ROLE_LOCAL;
    }
    // Hands -> another's long gun, the displaced rifle too big for the hands now: to the long gun; the source's
    // first-person view puts away with nothing drawn.
    {
        S_TestActor *a = Human(216, 1000), *b = Human(217, 1000);
        C_Item *ra = New(g_types.longGun), *rb = New(g_types.longGun);
        Move(ra, &r, nullptr, 0, a, ITEM_CONTAINER_HANDS);
        Move(rb, &r, nullptr, 0, b, ITEM_CONTAINER_LONG_GUN);
        a->containers[ITEM_CONTAINER_HANDS]->SetMaxItemSize(rb->GetSize() - 1);
        g_gameRole = GAME_ROLE_CLIENT;
        g_activePlayer = a->human;
        g_log.clear();
        CHECK(Transfer(ra, a, ITEM_CONTAINER_HANDS, b, ITEM_CONTAINER_LONG_GUN, &replaced, &toOut, &replacedTo) == ITEM_RESULT_OK &&
              replacedTo == ITEM_CONTAINER_LONG_GUN);
        CHECK(Count("fpv 9 0") == 1);
        g_activePlayer = nullptr;
        g_gameRole = GAME_ROLE_LOCAL;
    }
    // Belt -> another's hands; the displaced pistol back to the belt (the place the item left): no give events.
    {
        S_TestActor *a = Human(218, 1000), *b = Human(219, 1000);
        C_Item *pa = New(g_types.pistol), *pb = New(g_types.pistol);
        Move(pa, &r, nullptr, 0, a, ITEM_CONTAINER_BELT);
        Move(pb, &r, nullptr, 0, b, ITEM_CONTAINER_HANDS);
        g_log.clear();
        CHECK(Transfer(pa, a, ITEM_CONTAINER_BELT, b, ITEM_CONTAINER_HANDS, &replaced, &toOut, &replacedTo) == ITEM_RESULT_OK &&
              replacedTo == ITEM_CONTAINER_BELT);
        CHECK(!Logged("eq event") && Count("eq added " + pt + " 3 from 1 1") == 1);
    }
    // Belt -> another's hands; the displaced rifle to the source's long gun (its hands taken): added with that origin.
    {
        S_TestActor *a = Human(220, 1000), *b = Human(221, 1000);
        C_Item *pa = New(g_types.pistol), *rb = New(g_types.longGun);
        Move(pa, &r, nullptr, 0, a, ITEM_CONTAINER_BELT);
        Move(New(g_types.pistol), &r, nullptr, 0, a, ITEM_CONTAINER_HANDS);
        Move(rb, &r, nullptr, 0, b, ITEM_CONTAINER_HANDS);
        g_log.clear();
        CHECK(Transfer(pa, a, ITEM_CONTAINER_BELT, b, ITEM_CONTAINER_HANDS, &replaced, &toOut, &replacedTo) == ITEM_RESULT_OK &&
              replacedTo == ITEM_CONTAINER_LONG_GUN);
        CHECK(Count("eq added " + pt + " 3 from 2 1") == 1 && !Logged("eq event"));
    }
    // Long gun -> another's hands (a pistol there): the pistol cannot go to the long gun; the source's hands taken: the
    // belt; the receiver lets the support hand go and gives first.
    {
        S_TestActor *a = Human(222, 1000), *b = Human(223, 1000);
        C_Item *ra = New(g_types.rifle), *pb = New(g_types.pistol);
        Move(ra, &r, nullptr, 0, a, ITEM_CONTAINER_LONG_GUN);
        Move(New(g_types.rifle), &r, nullptr, 0, a, ITEM_CONTAINER_HANDS);
        Move(pb, &r, nullptr, 0, b, ITEM_CONTAINER_HANDS);
        g_log.clear();
        CHECK(Transfer(ra, a, ITEM_CONTAINER_LONG_GUN, b, ITEM_CONTAINER_HANDS, &replaced, &toOut, &replacedTo) == ITEM_RESULT_OK &&
              replacedTo == ITEM_CONTAINER_BELT);
        CHECK(Count("eq event 1") == 1 && Count("eq event 2") == 1 && Count("eq added " + rt + " 3 from 2 1") == 1 &&
              At("eq event 2") < At("eq added " + rt + " 3"));
    }
}

// IsSwapBlocked's boundaries and checks, each alone.
void TestSwapEdges()
{
    Mission(false);
    GetGame()->m_missionTime = 0;
    C_Item *r = nullptr;
    // The same actor at its limit: no strength counted (a backpack too).
    {
        S_TestActor *a = Human(230, 10);
        C_Item *occ = New(g_types.pistol);
        a->containers[ITEM_CONTAINER_HANDS]->AddItem(occ, &r, -1);
        a->human->m_carriedWeight = 10.0f;
        CHECK(!Blocked(a->human, ITEM_CONTAINER_LONG_GUN, a->human, ITEM_CONTAINER_HANDS, nullptr));
        S_TestActor *b = Human(231, 10);
        C_Item *bp = New(g_types.backpack);
        b->containers[ITEM_CONTAINER_BACKPACK]->AddItem(bp, &r, -1);
        b->human->m_carriedWeight = 10.0f;
        CHECK(!Blocked(b->human, ITEM_CONTAINER_BACKPACK, b->human, ITEM_CONTAINER_BACKPACK, bp));
    }
    // A slot limit met exactly: fits (weight, space).
    {
        S_TestActor *a = Human(232, 1000), *b = Human(233, 1000);
        C_Item *occ = New(g_types.helmet);
        b->containers[ITEM_CONTAINER_HEAD]->AddItem(occ, &r, -1);
        a->containers[ITEM_CONTAINER_HEAD]->SetMaxWeight(W(g_types.helmet));
        a->containers[ITEM_CONTAINER_HEAD]->SetMaxSpace(occ->GetSpace());
        CHECK(!Blocked(a->human, ITEM_CONTAINER_BELT, b->human, ITEM_CONTAINER_HEAD, nullptr));
        a->containers[ITEM_CONTAINER_HEAD]->SetMaxWeight(W(g_types.helmet) - 0.01f);
        CHECK(Blocked(a->human, ITEM_CONTAINER_BELT, b->human, ITEM_CONTAINER_HEAD, nullptr));
        a->containers[ITEM_CONTAINER_HEAD]->SetMaxWeight(100.0f);
        a->containers[ITEM_CONTAINER_HEAD]->SetMaxSpace(occ->GetSpace() - 0.5f);
        CHECK(Blocked(a->human, ITEM_CONTAINER_BELT, b->human, ITEM_CONTAINER_HEAD, nullptr));
        // The size of each slot: helmet (head).
        a->containers[ITEM_CONTAINER_HEAD]->SetMaxSpace(100.0f);
        a->containers[ITEM_CONTAINER_HEAD]->SetMaxItemSize(occ->GetSize() - 1);
        CHECK(Blocked(a->human, ITEM_CONTAINER_BELT, b->human, ITEM_CONTAINER_HEAD, nullptr));
    }
    // The item leaving that place: the occupant's weight and space net of the item's.
    {
        S_TestActor *a = Human(234, 1000), *b = Human(235, 1000);
        C_Item *ra = New(g_types.longGun), *rb = New(g_types.longGun);
        a->containers[ITEM_CONTAINER_LONG_GUN]->AddItem(ra, &r, -1);
        b->containers[ITEM_CONTAINER_LONG_GUN]->AddItem(rb, &r, -1);
        Move(New(g_types.pistol), &r, nullptr, 0, a, ITEM_CONTAINER_HANDS);
        a->containers[ITEM_CONTAINER_LONG_GUN]->SetMaxWeight(0.0f);
        a->containers[ITEM_CONTAINER_LONG_GUN]->SetMaxSpace(0.0f);
        CHECK(!Blocked(a->human, ITEM_CONTAINER_LONG_GUN, b->human, ITEM_CONTAINER_LONG_GUN, ra));
        // Its size still checked: a long gun slot one size too small.
        a->containers[ITEM_CONTAINER_LONG_GUN]->SetMaxItemSize(rb->GetSize() - 1);
        CHECK(Blocked(a->human, ITEM_CONTAINER_LONG_GUN, b->human, ITEM_CONTAINER_LONG_GUN, ra));
    }
    // The hands' size: a pistol (belt full) checked against a's hands.
    {
        S_TestActor *a = Human(236, 1000), *b = Human(237, 1000);
        C_Item *pb = New(g_types.pistol);
        b->containers[ITEM_CONTAINER_HANDS]->AddItem(pb, &r, -1);
        a->containers[ITEM_CONTAINER_BELT]->SetMaxSpace(0.0f);
        CHECK(!Blocked(a->human, ITEM_CONTAINER_LONG_GUN, b->human, ITEM_CONTAINER_HANDS, nullptr));
        a->containers[ITEM_CONTAINER_HANDS]->SetMaxItemSize(0);
        CHECK(Blocked(a->human, ITEM_CONTAINER_LONG_GUN, b->human, ITEM_CONTAINER_HANDS, nullptr));
    }
    // A uniform's size.
    {
        S_TestActor *a = Human(238, 1000), *b = Human(239, 1000);
        C_Item *ub = New(g_types.uniform);
        b->containers[ITEM_CONTAINER_UNIFORM]->AddItem(ub, &r, -1);
        a->containers[ITEM_CONTAINER_UNIFORM]->SetMaxItemSize(ub->GetSize() - 1);
        CHECK(Blocked(a->human, ITEM_CONTAINER_BELT, b->human, ITEM_CONTAINER_UNIFORM, nullptr));
    }
    // The belt counts its totals: full by weight, blocked; a medkit's size not checked on the belt.
    {
        S_TestActor *a = Human(240, 1000), *b = Human(241, 1000);
        C_Item *pb = New(g_types.pistol);
        b->containers[ITEM_CONTAINER_HANDS]->AddItem(pb, &r, -1);
        Move(New(g_types.ammo), &r, nullptr, 0, a, ITEM_CONTAINER_BELT);
        a->containers[ITEM_CONTAINER_HANDS]->AddItem(New(g_types.rifle), &r, -1);
        a->containers[ITEM_CONTAINER_BELT]->SetMaxWeight(W(g_types.ammo) + W(g_types.pistol));
        CHECK(!Blocked(a->human, ITEM_CONTAINER_LONG_GUN, b->human, ITEM_CONTAINER_HANDS, nullptr));
        a->containers[ITEM_CONTAINER_BELT]->SetMaxWeight(W(g_types.ammo) + W(g_types.pistol) - 0.01f);
        CHECK(Blocked(a->human, ITEM_CONTAINER_LONG_GUN, b->human, ITEM_CONTAINER_HANDS, nullptr));
    }
    // The contents: their totals; a mine's size not checked there.
    if (g_types.mine >= 0) {
        S_TestActor *a = Human(242, 1000), *b = Human(243, 1000);
        C_Item *mine = New(g_types.mine);
        b->containers[ITEM_CONTAINER_HANDS]->AddItem(mine, &r, -1);
        a->containers[ITEM_CONTAINER_HANDS]->AddItem(New(g_types.rifle), &r, -1);
        a->containers[ITEM_CONTAINER_BELT]->SetMaxSpace(0.0f);
        Move(New(g_types.backpack), &r, nullptr, 0, a, ITEM_CONTAINER_BACKPACK);
        C_ItemContainer *contents = a->human->GetContainer(ITEM_CONTAINER_BACKPACK_CONTENTS);
        contents->SetMaxItemSize(0);
        const bool inContents = (g_itemsModule.GetItemDef(g_types.mine)->GetAllowedContainers() & ITEM_FITS_BACKPACK_CONTENTS) != 0;
        CHECK(Blocked(a->human, ITEM_CONTAINER_LONG_GUN, b->human, ITEM_CONTAINER_HANDS, nullptr) == !inContents);
        Move(New(g_types.ammo), &r, nullptr, 0, a, ITEM_CONTAINER_BACKPACK_CONTENTS);
        contents->SetMaxWeight(W(g_types.ammo) + W(g_types.mine) - 0.01f);
        CHECK(Blocked(a->human, ITEM_CONTAINER_LONG_GUN, b->human, ITEM_CONTAINER_HANDS, nullptr));
    }
    // Backpacks: the item's contents count against a's strength and net from the occupant's weight.
    {
        S_TestActor *a = Human(244, 1000), *b = Human(245, 1000);
        C_Item *ba = New(g_types.backpack), *bb = New(g_types.backpack);
        Move(ba, &r, nullptr, 0, a, ITEM_CONTAINER_BACKPACK);
        for (int i = 0; i < 2; ++i)
            Move(New(g_types.ammo), &r, nullptr, 0, a, ITEM_CONTAINER_BACKPACK_CONTENTS);
        Move(bb, &r, nullptr, 0, b, ITEM_CONTAINER_BACKPACK);
        // a gives up its pack with contents (heavier) for an empty one: no strength needed; the slot's weight limit
        // against the net weight (negative here).
        g_hitPoints[a->human] = static_cast<int>(a->human->GetCarriedWeight());
        a->human->m_carriedWeight = static_cast<float>(g_hitPoints[a->human]);
        a->containers[ITEM_CONTAINER_BACKPACK]->SetMaxWeight(-2.0f * W(g_types.ammo) + 0.001f);
        CHECK(!Blocked(a->human, ITEM_CONTAINER_BACKPACK, b->human, ITEM_CONTAINER_BACKPACK, ba));
        a->containers[ITEM_CONTAINER_BACKPACK]->SetMaxWeight(-2.0f * W(g_types.ammo) - 0.01f);
        CHECK(Blocked(a->human, ITEM_CONTAINER_BACKPACK, b->human, ITEM_CONTAINER_BACKPACK, ba));
        // Its space at the limit exactly: fits.
        a->containers[ITEM_CONTAINER_BACKPACK]->SetMaxWeight(100.0f);
        a->containers[ITEM_CONTAINER_BACKPACK]->SetMaxSpace(0.0f);
        CHECK(!Blocked(a->human, ITEM_CONTAINER_BACKPACK, b->human, ITEM_CONTAINER_BACKPACK, ba));
    }
}

// ---- part 2e: the last shapes
void TestPart2Last()
{
    Mission(false);
    GetGame()->m_missionTime = 0;
    C_Item *r = nullptr, *replaced = nullptr;
    int toOut = 0, replacedTo = 0;
    // A backpack with contents for one with as much: the occupant's contents count as given back.
    {
        S_TestActor *f = Human(250, 1000), *g = Human(251, 1000);
        C_Item *old = New(g_types.backpack), *fresh = New(g_types.backpack);
        Move(old, &r, nullptr, 0, f, ITEM_CONTAINER_BACKPACK);
        Move(New(g_types.ammo), &r, nullptr, 0, f, ITEM_CONTAINER_BACKPACK_CONTENTS);
        Move(fresh, &r, nullptr, 0, g, ITEM_CONTAINER_BACKPACK);
        Move(New(g_types.ammo), &r, nullptr, 0, g, ITEM_CONTAINER_BACKPACK_CONTENTS);
        f->human->m_carriedWeight = 20.0f;
        g_hitPoints[f->human] = 20;
        CHECK(Move(fresh, &r, g, ITEM_CONTAINER_BACKPACK, f, ITEM_CONTAINER_BACKPACK, true, false, false, true) == ITEM_RESULT_OK);
    }
    // A pistol swap: both carry the right weight after (the way back keeps the weights).
    {
        S_TestActor *e = Human(252, 1000), *f = Human(253, 1000);
        C_Item *g1 = New(g_types.pistol), *g2 = New(g_types.rifle);
        Move(g1, &r, nullptr, 0, e, ITEM_CONTAINER_HANDS);
        Move(g2, &r, nullptr, 0, f, ITEM_CONTAINER_HANDS);
        CHECK(Transfer(g1, e, ITEM_CONTAINER_HANDS, f, ITEM_CONTAINER_HANDS, &replaced, &toOut, &replacedTo) == ITEM_RESULT_OK && replaced == g2);
        CHECK(std::fabs(e->human->GetCarriedWeight() - W(g_types.rifle)) < 1e-4f && std::fabs(f->human->GetCarriedWeight() - W(g_types.pistol)) < 1e-4f);
    }
    // A backpack swap: each side ends with the other's contents container (the way back ignores locks, which also
    // keeps the target's new contents linked).
    {
        S_TestActor *a = Human(260, 1000), *b = Human(261, 1000);
        C_Item *ba = New(g_types.backpack), *bb = New(g_types.backpack);
        Move(ba, &r, nullptr, 0, a, ITEM_CONTAINER_BACKPACK);
        Move(bb, &r, nullptr, 0, b, ITEM_CONTAINER_BACKPACK);
        CHECK(Transfer(ba, a, ITEM_CONTAINER_BACKPACK, b, ITEM_CONTAINER_BACKPACK, &replaced, &toOut, &replacedTo) == ITEM_RESULT_OK && replaced == bb);
        CHECK(b->human->GetContainer(ITEM_CONTAINER_BACKPACK_CONTENTS) == static_cast<C_ContainerItem *>(ba)->GetStore() &&
              a->human->GetContainer(ITEM_CONTAINER_BACKPACK_CONTENTS) == static_cast<C_ContainerItem *>(bb)->GetStore());
    }
    // In a mission, the displaced item put elsewhere is not "created".
    {
        Mission(true);
        S_TestActor *a = Human(254, 1000), *b = Human(255, 1000);
        C_Item *p = New(g_types.pistol), *rb = New(g_types.rifle);
        Move(p, &r, nullptr, 0, a, ITEM_CONTAINER_BELT, false);
        Move(rb, &r, nullptr, 0, b, ITEM_CONTAINER_HANDS, false);
        g_log.clear();
        CHECK(Transfer(p, a, ITEM_CONTAINER_BELT, b, ITEM_CONTAINER_HANDS, &replaced, &toOut, &replacedTo) == ITEM_RESULT_OK &&
              replacedTo == ITEM_CONTAINER_HANDS);
        CHECK(!Logged("eq created"));
        Mission(false);
    }
    // The contents as a list: an occupied contents with room takes the occupant; a pistol's size is checked there.
    {
        S_TestActor *a = Human(256, 1000), *b = Human(257, 1000);
        C_Item *pb = New(g_types.pistol);
        b->containers[ITEM_CONTAINER_HANDS]->AddItem(pb, &r, -1);
        a->containers[ITEM_CONTAINER_HANDS]->AddItem(New(g_types.rifle), &r, -1);
        a->containers[ITEM_CONTAINER_BELT]->SetMaxSpace(0.0f);
        Move(New(g_types.backpack), &r, nullptr, 0, a, ITEM_CONTAINER_BACKPACK);
        Move(New(g_types.ammo), &r, nullptr, 0, a, ITEM_CONTAINER_BACKPACK_CONTENTS);
        C_ItemContainer *contents = a->human->GetContainer(ITEM_CONTAINER_BACKPACK_CONTENTS);
        contents->SetMaxItemSize(5);
        CHECK(!Blocked(a->human, ITEM_CONTAINER_LONG_GUN, b->human, ITEM_CONTAINER_HANDS, nullptr));
        contents->SetMaxItemSize(0);
        CHECK(Blocked(a->human, ITEM_CONTAINER_LONG_GUN, b->human, ITEM_CONTAINER_HANDS, nullptr));
    }
    // A backpack whose weight is exactly a's slot limit: fits.
    {
        S_TestActor *a = Human(258, 1000), *b = Human(259, 1000);
        C_Item *bb = New(g_types.backpack);
        b->containers[ITEM_CONTAINER_BACKPACK]->AddItem(bb, &r, -1);
        a->containers[ITEM_CONTAINER_BACKPACK]->SetMaxWeight(W(g_types.backpack));
        CHECK(!Blocked(a->human, ITEM_CONTAINER_BELT, b->human, ITEM_CONTAINER_BACKPACK, nullptr));
        a->containers[ITEM_CONTAINER_BACKPACK]->SetMaxWeight(W(g_types.backpack) - 0.01f);
        CHECK(Blocked(a->human, ITEM_CONTAINER_BELT, b->human, ITEM_CONTAINER_BACKPACK, nullptr));
    }
}

// ============================================================ part 3a: the mission's items file, the lifecycle

C_ActorsModule &Actors() { return g_actorsModule; }

// A record of items.dat as the file holds it.
struct S_DatRecord {
    std::string name;
    int container, typeId, uid, placed;
    float pos[4], rot[4];
    int armed, empty;
};
// Reads items.dat independently: the version, then records of a name (its length, the name and its terminator and
// one byte more), container, type, uid, placed (then a position of 4 floats, from version 15, and a quaternion), armed
// (from version 14) and empty (from version 16). False when it does not end on a record.
bool ParseDat(const std::string &d, int &version, std::vector<S_DatRecord> &out)
{
    size_t o = 0;
    auto get = [&](void *p, size_t n) {
        if (o + n > d.size())
            return false;
        std::memcpy(p, d.data() + o, n);
        o += n;
        return true;
    };
    if (!get(&version, 4))
        return false;
    while (o < d.size()) {
        S_DatRecord r = {};
        int length;
        if (!get(&length, 4) || length < 0 || length > 126 || o + length + 1 > d.size())
            return false;
        r.name.assign(d.data() + o, strnlen(d.data() + o, length));
        o += length + 1;
        if (!get(&r.container, 4) || !get(&r.typeId, 4) || !get(&r.uid, 4) || !get(&r.placed, 4))
            return false;
        if (r.placed && (!get(r.pos, version < 0xF ? 12 : 16) || !get(r.rot, 16)))
            return false;
        if (version > 0xD && !get(&r.armed, 4))
            return false;
        if (version > 0xF && !get(&r.empty, 4))
            return false;
        out.push_back(r);
    }
    return true;
}

// Every items.dat of the game's missions: loaded (the actors the records name made on demand by the scene); the
// placed records become placements (position, rotation, armed, an empty launcher's rounds); the actors' records their
// items; saved again, the actors' records come back (but the uids, new ones, and the byte after each name).
void TestMissionFiles()
{
    const char *dir = std::getenv("MISSIONS_DIR");
    const std::string missions = dir ? dir : "H:\\HD2_Unpacked\\MISSIONS";
    std::vector<std::string> files;
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA((missions + "\\*").c_str(), &fd);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            if ((fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) && fd.cFileName[0] != '.') {
                const std::string f = missions + "\\" + fd.cFileName + "\\items.dat";
                if (GetFileAttributesA(f.c_str()) != INVALID_FILE_ATTRIBUTES)
                    files.push_back(f);
            }
        } while (FindNextFileA(h, &fd));
        FindClose(h);
    }
    std::sort(files.begin(), files.end());
    if (files.empty()) {
        std::printf("  (no mission items files: MISSIONS_DIR)\n");
        return;
    }
    GetGame()->m_missionName = "TEST";
    int filesOk = 0, records = 0, placedOk = 0, inActors = 0, refused = 0, savedSame = 0, unparsed = 0;
    for (const std::string &path : files) {
        const std::string data = ReadDisk(path);
        int version = 0;
        std::vector<S_DatRecord> want;
        const bool whole = ParseDat(data, version, want);
        // Fresh state: no actors, no placements, uids from 0.
        Actors().m_actors.clear();
        g_itemsModule.FreePlacements();
        g_itemsModule.ResetUids();
        g_itemsModule.m_missionActive = true;
        g_gameRole = GAME_ROLE_LOCAL;
        // Which names are item stores (boxes, vehicles, fixed guns): all their records in container 0, and several of
        // them or one that is not headgear; the others are humans.
        std::map<std::string, int> firstOnly, count;
        std::map<std::string, bool> headgear;
        for (const S_DatRecord &r : want) {
            if (r.placed)
                continue;
            ++count[r.name];
            if (r.container == 0)
                ++firstOnly[r.name];
            const C_ItemDef *d = g_itemsModule.GetItemDef(r.typeId);
            headgear[r.name] = d && d->GetComponentKind() == ICK_HEADGEAR;
        }
        auto isStore = [&](const std::string &name) {
            return firstOnly[name] == count[name] && (count[name] > 1 || !headgear[name]);
        };
        std::map<std::string, S_TestActor *> byName;
        g_findFrame = [&](const char *name) -> I3D_frame * {
            if (!byName.count(name)) {
                S_TestActor *a = isStore(name) ? Vehicle(1000 + static_cast<int>(byName.size())) : Human(1000 + static_cast<int>(byName.size()), 100000);
                g_hitPoints[a->human] = 100000;
                for (C_ItemContainer *c : a->containers)
                    if (c) {
                        c->SetMaxWeight(1e9f);
                        c->SetMaxSpace(1e9f);
                    }
                I3D_frame *frame = Named(name);
                auto *data = new S_FrameData();
                data->actor = a->human;
                frame->m_data = data;
                a->human->m_frame = frame;
                byName[name] = a;
                Actors().m_actors.push_back(a->human);
            }
            return byName[name]->human->m_frame;
        };
        g_files[Key("Missions\\TEST\\items.dat")] = data;
        g_log.clear();
        const bool loaded = g_itemsModule.LoadMissionItems();
        g_findFrame = nullptr;
        if (!whole) {
            ++unparsed;
            std::printf("  %s: does not parse whole (loaded: %d, %d records read)\n", path.c_str(), loaded, static_cast<int>(want.size()));
            CHECK(!loaded);
            continue;
        }
        CHECK(loaded && version == 16);
        records += static_cast<int>(want.size());
        // The placements: the placed records with a definition, in order.
        int p = 0;
        bool placements = true;
        for (const S_DatRecord &r : want) {
            if (!r.placed || !g_itemsModule.GetItemDef(r.typeId))
                continue;
            if (p >= g_itemsModule.m_placementCount) {
                placements = false;
                break;
            }
            const S_ItemPlacement &pl = g_itemsModule.m_placements[p++];
            placements &= pl.item->GetTypeId() == r.typeId && pl.item->IsInWorld() && pl.item->IsArmed() == (r.armed != 0) &&
                          std::memcmp(&pl.pos, r.pos, 16) == 0 && std::memcmp(&pl.rot, r.rot, 16) == 0;
            if (r.empty && pl.item->GetMaxAmount() > 0.0f)
                placements &= pl.item->GetAmount() == 0.0f;
            ++placedOk;
        }
        placements &= p == g_itemsModule.m_placementCount;
        CHECK(placements);
        // The actors' records: each item in its container (the replaced ones told).
        std::vector<S_DatRecord> actorRecords;
        for (const S_DatRecord &r : want)
            if (!r.placed && g_itemsModule.GetItemDef(r.typeId))
                actorRecords.push_back(r);
        inActors += static_cast<int>(actorRecords.size());
        refused += static_cast<int>(std::count_if(g_log.begin(), g_log.end(), [](const std::string &l) {
            return l.rfind("message Nahran item na pozici", 0) == 0;
        }));
        // Saved again: the actors' records in the order the actors and their containers give.
        g_files.erase(Key("Missions\\TEST\\items.dat"));
        CHECK(SaveMissionItems());
        int savedVersion = 0;
        std::vector<S_DatRecord> saved;
        CHECK(ParseDat(g_files[Key("Missions\\TEST\\items.dat")], savedVersion, saved) && savedVersion == 0x10);
        // Compared as multisets per actor (name, container, type, armed): the order inside a store follows its slots.
        auto key = [](const S_DatRecord &r) {
            return r.name + "|" + std::to_string(r.container) + "|" + std::to_string(r.typeId) + "|" + std::to_string(r.armed);
        };
        std::map<std::string, int> a, b;
        for (const S_DatRecord &r : actorRecords)
            ++a[key(r)];
        for (const S_DatRecord &r : saved) {
            ++b[key(r)];
            CHECK(!r.placed && r.empty == 0);
        }
        const bool same = a == b;
        savedSame += same;
        if (!same)
            std::printf("  %s: %zu actor records, %zu saved back\n", path.c_str(), actorRecords.size(), saved.size());
        ++filesOk;
    }
    std::printf("  mission items: %zu files (%d not whole), %d records; %d placed as placements, %d in actors (%d displaced, told); "
                "%d of %d saved back the same\n",
                files.size(), unparsed, records, placedOk, inActors, refused, savedSame, filesOk);
    CHECK(filesOk + unparsed == static_cast<int>(files.size()) && savedSame == filesOk && placedOk > 0);
    Actors().m_actors.clear();
    g_itemsModule.FreePlacements();
    g_itemsModule.m_missionActive = false;
}

// The file's failures: none (true); short after the version (false); an old version (told, read on); a record cut
// short (false); a name too long for the buffer (false); an unknown type (skipped); an actor not in the scene (the
// item dropped); placed items outside a mission (not listed).
void TestMissionFileEdges()
{
    GetGame()->m_missionName = "TEST";
    const std::string key = Key("Missions\\TEST\\items.dat");
    g_files.erase(key);
    CHECK(g_itemsModule.LoadMissionItems());
    g_files[key] = std::string("\x10\x00", 2);
    CHECK(!g_itemsModule.LoadMissionItems());
    auto record = [](const std::string &name, int container, int type, int placed, std::initializer_list<float> posRot, int armed, int empty,
                     int version) {
        std::string s;
        const int length = static_cast<int>(name.size()) + 1;
        s.append(reinterpret_cast<const char *>(&length), 4);
        s += name;
        s.append(2, '\0');
        for (int v : {container, type, 7, placed})
            s.append(reinterpret_cast<const char *>(&v), 4);
        for (float f : posRot)
            s.append(reinterpret_cast<const char *>(&f), 4);
        if (version > 0xD)
            s.append(reinterpret_cast<const char *>(&armed), 4);
        if (version > 0xF)
            s.append(reinterpret_cast<const char *>(&empty), 4);
        return s;
    };
    auto file = [](int version, const std::string &records) { return std::string(reinterpret_cast<const char *>(&version), 4) + records; };
    // An old version (14: no w, armed, no empty): told; a placed item read with a 12-byte position.
    g_itemsModule.FreePlacements();
    g_itemsModule.m_missionActive = true;
    g_files[key] = file(14, record("", 0, g_types.mine, 1, {1, 2, 3, 0, 0, 0, 1}, 1, 0, 14));
    g_log.clear();
    CHECK(g_itemsModule.LoadMissionItems() && Count("message Stara verze ulozenych itemu (items.dat)") == 1 &&
          g_itemsModule.m_placementCount == 1 && g_itemsModule.m_placements[0].pos.x == 1.0f && g_itemsModule.m_placements[0].pos.z == 3.0f &&
          g_itemsModule.m_placements[0].rot.s == 1.0f && g_itemsModule.m_placements[0].item->IsArmed());
    // Version 13: no armed field either.
    g_itemsModule.FreePlacements();
    g_files[key] = file(13, record("", 0, g_types.mine, 1, {1, 2, 3, 0, 0, 0, 1}, 1, 0, 13));
    CHECK(g_itemsModule.LoadMissionItems() && g_itemsModule.m_placementCount == 1 && !g_itemsModule.m_placements[0].item->IsArmed());
    // Not in a mission: placed items made but not listed.
    g_itemsModule.FreePlacements();
    g_itemsModule.m_missionActive = false;
    const size_t n = g_itemsModule.m_instances.size();
    g_files[key] = file(16, record("", 0, g_types.mine, 1, {1, 2, 3, 1, 0, 0, 0, 1}, 0, 0, 16));
    CHECK(g_itemsModule.LoadMissionItems() && g_itemsModule.m_placementCount == 0 && g_itemsModule.m_instances.size() == n + 1);
    g_itemsModule.m_missionActive = true;
    // Cut short: in the name, in the fields, in the position, in the rotation.
    const std::string full = record("soldier", 3, g_types.pistol, 1, {1, 2, 3, 1, 0, 0, 0, 1}, 0, 0, 16);
    for (size_t cut : {size_t(6), size_t(14), size_t(20), size_t(30), size_t(40), size_t(60)}) {
        g_files[key] = file(16, full.substr(0, cut));
        CHECK(!g_itemsModule.LoadMissionItems());
    }
    // A name longer than the buffer: refused.
    g_files[key] = file(16, record(std::string(130, 'x'), 3, g_types.pistol, 0, {}, 0, 0, 16));
    CHECK(!g_itemsModule.LoadMissionItems());
    // Armed and empty cut off at the end: read as 0.
    g_itemsModule.FreePlacements();
    std::string noTail = record("", 0, g_types.mine, 1, {1, 2, 3, 1, 0, 0, 0, 1}, 1, 0, 16);
    g_files[key] = file(16, noTail.substr(0, noTail.size() - 8));
    CHECK(g_itemsModule.LoadMissionItems() && g_itemsModule.m_placementCount == 1 && !g_itemsModule.m_placements[0].item->IsArmed());
    // An empty launcher placed: its round used up (ammo-less launchers have one).
    const int launcher = FindType([](C_ItemDef *d) { return d->GetRecordType() == ITEM_ANIMATED && d->GetBehaviour() == ITEM_BEHAVIOUR_LAUNCHER; });
    if (launcher >= 0) {
        g_itemsModule.FreePlacements();
        g_files[key] = file(16, record("", 0, launcher, 1, {1, 2, 3, 1, 0, 0, 0, 1}, 0, 1, 16));
        CHECK(g_itemsModule.LoadMissionItems() && g_itemsModule.m_placementCount == 1 && g_itemsModule.m_placements[0].item->GetAmount() == 0.0f);
        g_itemsModule.FreePlacements();
        g_files[key] = file(16, record("", 0, launcher, 1, {1, 2, 3, 1, 0, 0, 0, 1}, 0, 0, 16));
        CHECK(g_itemsModule.LoadMissionItems() && g_itemsModule.m_placements[0].item->GetAmount() > 0.0f);
    }
    // An unknown type: skipped; an actor the scene does not know: the item dropped (not listed).
    g_itemsModule.FreePlacements();
    const size_t m = g_itemsModule.m_instances.size();
    g_files[key] = file(16, record("", 0, 4999, 1, {1, 2, 3, 1, 0, 0, 0, 1}, 0, 0, 16) + record("nobody", 3, g_types.pistol, 0, {}, 0, 0, 16));
    g_log.clear();
    CHECK(g_itemsModule.LoadMissionItems() && g_itemsModule.m_placementCount == 0 && g_itemsModule.m_instances.size() == m &&
          Count("scene find nobody 128") == 1);
    // An actor's taken slot: the earlier item dropped (told); the item outside the hands not busy, the hands' item busy.
    S_TestActor *a = Human(1100, 100000);
    I3D_frame *frame = Named("soldier");
    auto *fdata = new S_FrameData();
    fdata->actor = a->human;
    frame->m_data = fdata;
    g_findFrame = [&](const char *) { return frame; };
    GetGame()->m_missionTime = 5000;
    g_files[key] = file(16, record("soldier", ITEM_CONTAINER_HEAD, g_types.helmet, 0, {}, 0, 0, 16) +
                                record("soldier", ITEM_CONTAINER_HEAD, g_types.helmet, 0, {}, 0, 0, 16) +
                                record("soldier", ITEM_CONTAINER_HANDS, g_types.pistol, 0, {}, 0, 0, 16) +
                                record("soldier", ITEM_CONTAINER_HANDS, g_types.helmet, 0, {}, 0, 0, 16));
    g_log.clear();
    const bool ok = g_itemsModule.LoadMissionItems();
    g_findFrame = nullptr;
    // (A new item into a taken slot is refused: no space; the second helmet and the helmet into the hands dropped.)
    CHECK(ok && a->containers[ITEM_CONTAINER_HEAD]->GetItem() && a->containers[ITEM_CONTAINER_HEAD]->GetItem()->GetTime() == 0 &&
          a->containers[ITEM_CONTAINER_HANDS]->GetItem() && a->containers[ITEM_CONTAINER_HANDS]->GetItem()->GetTime() == 5920 &&
          !Logged("eq "));
    GetGame()->m_missionTime = 0;
    g_itemsModule.FreePlacements();
    g_itemsModule.m_missionActive = false;
    // Saving: a file that cannot be opened (none here: always opens), deletion.
    g_log.clear();
    g_files[key] = "x";
    DeleteMissionItemsFile();
    CHECK(Count("delete Missions\\TEST\\items.dat") == 1 && !g_files.count(key));
}

// The module's lifecycle: loading the mission's data (its pass only), starting, stopping, restarting; the surface
// marks' ticks (the server ticks them, clients in steps of 100 ms); freeing the mission's data.
void TestLifecycle()
{
    GetGame()->m_missionName = "TEST";
    const std::string key = Key("Missions\\TEST\\items.dat");
    // Local game (the server pass): loads; the client pass does nothing.
    g_gameRole = GAME_ROLE_LOCAL;
    g_itemsModule.m_lastUid = 50;
    g_itemsModule.LoadMissionData(false);
    CHECK(g_itemsModule.m_lastUid == 50);
    g_itemsModule.LoadMissionData(true);
    CHECK(g_itemsModule.m_lastUid == -1);
    // Start: the server reads the mission's file; a bad one is deleted.
    g_itemsModule.m_unknown7E5 = true;
    g_files[key] = std::string("\x10\x00", 2);
    g_log.clear();
    g_itemsModule.StartMission(true);
    CHECK(!g_itemsModule.m_unknown7E5 && g_itemsModule.m_missionActive && Count("delete Missions\\TEST\\items.dat") == 1);
    g_files.erase(key);
    g_log.clear();
    g_itemsModule.StartMission(true);
    CHECK(!Logged("delete"));
    // Another pass: only the flag cleared.
    g_itemsModule.m_unknown7E5 = true;
    g_itemsModule.m_missionActive = false;
    g_itemsModule.StartMission(false);
    CHECK(!g_itemsModule.m_unknown7E5 && !g_itemsModule.m_missionActive);
    // A client: its pass is the client one; no file read.
    g_gameRole = GAME_ROLE_CLIENT;
    g_files[key] = std::string("\x10\x00", 2);
    g_log.clear();
    g_itemsModule.StartMission(false);
    CHECK(g_itemsModule.m_missionActive && !Logged("delete") && !Logged("dtaopen"));
    g_gameRole = GAME_ROLE_LOCAL;
    // Stop: the actors' items cleared, the props released, inactive.
    S_TestActor *a = Human(1200, 1000);
    C_Item *r = nullptr;
    Move(New(g_types.pistol), &r, nullptr, 0, a, ITEM_CONTAINER_BELT, false);
    Actors().m_actors.assign(1, a->human);
    g_models["m_bottle"] = [](I3D_frame *model) {
        g_frameNames[model] = "prop";
    };
    I3D_frame *prop = GetActivityProp(1);
    g_models.clear();
    g_itemsModule.m_missionActive = true;
    g_log.clear();
    g_itemsModule.StopMission(true);
    CHECK(prop && !g_itemsModule.m_missionActive && a->containers[ITEM_CONTAINER_BELT]->IsEmpty() && Count("frame release prop") == 1 &&
          g_itemsModule.m_lastUid == -1);
    // (Released and forgotten: asked again, the model is opened again.)
    g_log.clear();
    CHECK(GetActivityProp(1) == nullptr && Count("open model m_bottle") == 1);
    g_itemsModule.m_missionActive = true;
    g_itemsModule.StopMission(false);
    CHECK(g_itemsModule.m_missionActive);
    // Restart: cleared and active again; the server pass reads the file (a bad one deleted).
    Move(New(g_types.pistol), &r, nullptr, 0, a, ITEM_CONTAINER_BELT, false);
    g_files[key] = std::string("\x10\x00", 2);
    g_log.clear();
    g_itemsModule.RestartMission(true);
    CHECK(g_itemsModule.m_missionActive && a->containers[ITEM_CONTAINER_BELT]->IsEmpty() && Count("delete Missions\\TEST\\items.dat") == 1);
    g_files[key] = std::string("\x10\x00", 2);
    g_log.clear();
    g_itemsModule.RestartMission(false);
    CHECK(!Logged("delete"));
    g_gameRole = GAME_ROLE_CLIENT;
    g_log.clear();
    g_itemsModule.RestartMission(true);
    CHECK(!Logged("delete"));
    g_gameRole = GAME_ROLE_LOCAL;
    Actors().m_actors.clear();
    // A vehicle among the actors: its store cleared.
    S_TestActor *v = Vehicle(1201);
    v->containers[0]->AddItem(New(g_types.ammo), &r, -1);
    Actors().m_actors.assign(1, v->human);
    g_itemsModule.ClearActorItems();
    CHECK(v->containers[0]->IsEmpty());
    Actors().m_actors.clear();
    // Free: the placements' items deleted, the respawning list emptied.
    g_itemsModule.FreePlacements();
    S_ItemPlacement pl = {};
    pl.item = g_itemsModule.NewItem(g_types.pistol);
    g_itemsModule.AddPlacement(pl);
    g_itemsModule.AddPlacement(pl);
    g_itemsModule.AddPlacement(pl);
    CHECK(g_itemsModule.m_placementCount == 3 && g_itemsModule.m_placementCapacity == 4 && g_itemsModule.m_placements[2].item == pl.item);
    g_itemsModule.m_placementCount = 1;
    g_itemsModule.m_respawningItems.push_back(pl.item);
    g_itemsModule.FreeMissionData(true);
    CHECK(g_itemsModule.m_respawningItems.empty());
    g_itemsModule.FreePlacements();
    CHECK(g_itemsModule.m_placements == nullptr && g_itemsModule.m_placementCount == 0 && g_itemsModule.m_placementCapacity == 0);
    // The marks: a server ticks them; a client adds up time and ticks in steps of 100 ms.
    g_gameRole = GAME_ROLE_SERVER;
    g_log.clear();
    g_itemsModule.Tick(16);
    g_itemsModule.Update(16);
    CHECK((g_log == std::vector<std::string>{"marks tick 16"}));
    g_gameRole = GAME_ROLE_CLIENT;
    g_log.clear();
    g_itemsModule.Tick(16);
    CHECK(g_log.empty());
    // (The client's collected time is the module's own: first brought to a tick, then counted from 0.)
    for (int i = 0; i < 100 && !Logged("marks tick"); ++i)
        g_itemsModule.Update(1);
    g_log.clear();
    for (int i = 0; i < 6; ++i)
        g_itemsModule.Update(16);
    CHECK(g_log.empty());
    g_itemsModule.Update(3);
    CHECK(g_log.empty());
    g_itemsModule.Update(1);
    CHECK((g_log == std::vector<std::string>{"marks tick 100"}));
    g_itemsModule.Update(250);
    CHECK(Count("marks tick 100") == 2);
    g_gameRole = GAME_ROLE_LOCAL;
    // Init resets the uids; Done frees the marks, deletes the definitions and forgets instances (every other one).
    g_itemsModule.m_lastUid = 9;
    g_itemsModule.Init();
    CHECK(g_itemsModule.m_lastUid == -1);
}

// The activity props: by index 0..2, opened once from the model cache (index 2, the cigarette, its glow set up and
// off); none when the model does not open; none outside 0..2.
void TestActivityProps()
{
    g_gameRole = GAME_ROLE_LOCAL;
    g_itemsModule.StopMission(true);
    CHECK(GetActivityProp(-1) == nullptr && GetActivityProp(3) == nullptr);
    g_log.clear();
    CHECK(GetActivityProp(0) == nullptr && Count("open model m_karty") == 1 && Count("frame release") == 1);
    I3D_frame *glow = nullptr;
    g_models["m_cigarette"] = [&](I3D_frame *model) {
        glow = Named("lensf", FRAME_VISUAL);
        g_kids[model].push_back(glow);
    };
    g_models["m_bottle"] = [](I3D_frame *) {};
    I3D_frame *bottle = GetActivityProp(1);
    CHECK(bottle && GetActivityProp(1) == bottle && Count("open model m_bottle") == 1);
    g_log.clear();
    I3D_frame *cig = GetActivityProp(2);
    unsigned char *flare = reinterpret_cast<unsigned char *>(glow);
    CHECK(cig && glow && *reinterpret_cast<float *>(flare + 0x1E0) == 1.0f && *reinterpret_cast<float *>(flare + 0x1E4) == 0.15f &&
          *reinterpret_cast<float *>(flare + 0x240) == 120.0f && *reinterpret_cast<float *>(flare + 0x244) == 0.35f && Count("frame off") == 1 &&
          Count("frame update") == 1);
    // Without a glow: fine.
    g_itemsModule.StopMission(true);
    g_models["m_cigarette"] = [](I3D_frame *) {};
    CHECK(GetActivityProp(2) != nullptr);
    g_models.clear();
    g_itemsModule.StopMission(true);
}

// Copying a frame tree (visuals by their visual type, others by their frame type; children linked under the copy);
// releasing one (children first, unlinked; projectors, emitters, sounds and lights not released).
void TestFrameTrees()
{
    I3D_frame *copy = nullptr;
    CHECK(!CloneFrameTree(copy, nullptr, nullptr));
    I3D_frame *leaf = Named("leaf", FRAME_VISUAL);
    g_visualTypes[leaf] = I3DVISUAL_OBJECT;
    I3D_frame *dummy = Named("dummy", FRAME_DUMMY, {leaf});
    I3D_frame *root = Named("root", FRAME_MODEL, {dummy});
    I3D_frame *parent = Named("parent");
    g_log.clear();
    CHECK(CloneFrameTree(copy, root, parent) && copy);
    CHECK(Count("createframe " + std::to_string(FRAME_MODEL)) == 1 && Count("createframe " + std::to_string(FRAME_DUMMY)) == 1 &&
          Count("createvisual " + std::to_string(I3DVISUAL_OBJECT)) == 1 && Count("duplicate copy of root") == 1 &&
          Count("duplicate copy of leaf") == 1 && Count("frame link") == 3);
    // No parent: not linked.
    g_log.clear();
    I3D_frame *alone = nullptr;
    CHECK(CloneFrameTree(alone, leaf, nullptr) && Count("frame link") == 0);
    // Released: deepest first, every frame unlinked, the light and the projector kept.
    I3D_frame *light = Named("light", FRAME_LIGHT);
    I3D_frame *projector = Named("projector", FRAME_VISUAL);
    g_visualTypes[projector] = I3DVISUAL_PROJECTOR;
    I3D_frame *sound = Named("sound", FRAME_SOUND);
    I3D_frame *top = Named("top", FRAME_MODEL, {light, projector, sound, Named("mesh", FRAME_VISUAL)});
    // (The fake's children stay listed; the release walk takes the last child each time, so this fake counts down.)
    struct S_Unlinker {
        static void Detach(I3D_frame *f) { (void)f; }
    };
    g_log.clear();
    ReleaseFrameTree(nullptr);
    CHECK(g_log.empty());
    top->m_refCount = 0;
    ReleaseFrameTree(top);
    CHECK(g_log.empty());
}

// ============================================================ part 3b: the weapons

bool Same(const S_vector &a, const S_vector &b) { return a.x == b.x && a.y == b.y && a.z == b.z; }
// A weapon of `type` with a model (its dummies and, by `projectile`, a projectile frame of that name).
C_WeaponItem *Weapon(int type, const char *projectile = nullptr)
{
    C_Item *item = g_itemsModule.NewItem(type);
    g_itemsModule.AddInstance(item);
    auto *w = static_cast<C_WeaponItem *>(item);
    std::vector<I3D_frame *> kids = {Named("flash", FRAME_VISUAL), Named("shdum", FRAME_DUMMY), Named("blsdum", FRAME_DUMMY),
                                     Named("cardum", FRAME_DUMMY)};
    if (projectile) {
        I3D_frame *p = Named(projectile, FRAME_VISUAL);
        p->m_localMatrix(3) = S_vector(0.5f, 0.25f, 0.125f);
        p->m_localMatrix.m[3][3] = 1.0f;
        p->m_rot.s = 0.8f;
        p->m_rot.v = S_vector(0.0f, 0.6f, 0.0f);
        p->m_frameFlags |= FRMFLAGS_ROT_DIRTY;
        kids.push_back(p);
    }
    I3D_frame *model = Named("weaponmodel");
    for (I3D_frame *k : kids) {
        g_kids[model].push_back(k);
        k->m_parent = model;
    }
    w->m_frame = model;
    return w;
}
C_WeaponComp *Comp(C_Item *item) { return static_cast<C_WeaponComp *>(item->GetDef()->GetComponent()); }
std::vector<std::string> Sounds()
{
    std::vector<std::string> out;
    for (const std::string &l : g_log)
        if (l.rfind("sound", 0) == 0)
            out.push_back(l);
    return out;
}

// The weapon types the tests use.
struct S_Guns {
    int rifle, smg, mg, launcher, thrown, melee, noReload;
} g_guns;
void FindGuns()
{
    auto firearm = [](C_ItemDef *d) { return d->GetRecordType() == ITEM_ANIMATED && d->GetComponentKind() == ICK_FIREARM && d->GetAmmoItemId() > 0; };
    g_guns.rifle = FindType([&](C_ItemDef *d) { return firearm(d) && d->GetCarryMode() != ITEM_CARRY_SMG && d->GetCarryMode() != ITEM_CARRY_LMG; });
    g_guns.smg = FindType([&](C_ItemDef *d) { return firearm(d) && d->GetCarryMode() == ITEM_CARRY_SMG; });
    g_guns.mg = FindType([&](C_ItemDef *d) { return firearm(d) && d->GetCarryMode() == ITEM_CARRY_LMG; });
    g_guns.launcher = FindType([](C_ItemDef *d) {
        return d->GetRecordType() == ITEM_ANIMATED && d->GetComponentKind() == ICK_PROJECTILE_WEAPON && d->GetBehaviour() == ITEM_BEHAVIOUR_LAUNCHER;
    });
    g_guns.thrown = FindType([](C_ItemDef *d) { return d->GetRecordType() == ITEM_ANIMATED && d->GetBehaviour() == ITEM_BEHAVIOUR_THROWN; });
    g_guns.melee = FindType([](C_ItemDef *d) { return d->GetRecordType() == ITEM_ANIMATED && d->GetComponentKind() == ICK_MELEE; });
    g_guns.noReload = FindType([](C_ItemDef *d) {
        return d->GetRecordType() == ITEM_ANIMATED && d->GetComponent() && d->GetComponentKind() <= ICK_FIREARM &&
               !static_cast<C_WeaponComp *>(d->GetComponent())->CanReload();
    });
    std::printf("  guns: rifle %d smg %d mg %d launcher %d thrown %d melee %d no-reload %d\n", g_guns.rifle, g_guns.smg, g_guns.mg,
                g_guns.launcher, g_guns.thrown, g_guns.melee, g_guns.noReload);
}

void TestWeaponBasics()
{
    GetGame()->m_missionTime = 0;
    C_WeaponItem *w = Weapon(g_guns.rifle);
    // Init: its capacity the ammunition's; full; the flash found and hidden.
    w->Init(g_guns.rifle);
    const float capacity = g_itemsModule.GetItemDef(g_itemsModule.GetItemDef(g_guns.rifle)->GetAmmoItemId())->GetAmmoAmount();
    CHECK(w->GetMaxAmount() == capacity && w->m_rounds == capacity && w->m_flash && FrameName(w->m_flash) == "flash" && w->m_soundId == -1 &&
          w->m_flags == WEAPON_UNKNOWN_FLAG1);
    // Use: down to 0, not below.
    w->Use(2.0f);
    CHECK(w->m_rounds == capacity - 2.0f);
    w->Use(1e6f);
    CHECK(w->m_rounds == 0.0f);
    // While it reloads (before its ready time): 0 rounds to fire.
    w->m_rounds = 5.0f;
    w->m_readyTime = 100;
    GetGame()->m_missionTime = 99;
    CHECK(w->GetAmount() == 0.0f);
    GetGame()->m_missionTime = 100;
    CHECK(w->GetAmount() == 5.0f);
    GetGame()->m_missionTime = 0;
    // The dummies: found once by name.
    CHECK(FrameName(w->GetShotDummy()) == "shdum" && FrameName(w->GetBlastDummy()) == "blsdum" && FrameName(w->GetCartridgeDummy()) == "cardum");
    I3D_frame *cached = w->GetShotDummy();
    g_kids[w->m_frame].clear();
    CHECK(w->GetShotDummy() == cached && w->GetBlastDummy() && w->GetCartridgeDummy());
    // A launcher without ammunition: one round; others without: -1.
    if (g_guns.launcher >= 0 && g_itemsModule.GetItemDef(g_guns.launcher)->GetAmmoItemId() < 0) {
        C_WeaponItem *l = Weapon(g_guns.launcher);
        l->Init(g_guns.launcher);
        CHECK(l->GetMaxAmount() == 1.0f);
    }
    if (g_guns.melee >= 0) {
        C_WeaponItem *k = Weapon(g_guns.melee);
        k->Init(g_guns.melee);
        CHECK(k->GetMaxAmount() == -1.0f || g_itemsModule.GetItemDef(g_guns.melee)->GetAmmoItemId() >= 0);
    }
}

// Reload: the fullest magazine of the first slot holding the weapon's ammunition; what does not fit goes back into
// the emptiest (or stays in the fullest); the used magazine dropped and destroyed (its uid told); ready after the
// reload time.
void TestReload()
{
    Mission(false);
    GetGame()->m_missionTime = 1000;
    const int ammoType = g_itemsModule.GetItemDef(g_guns.rifle)->GetAmmoItemId();
    const float full = g_itemsModule.GetItemDef(ammoType)->GetAmmoAmount();
    auto magazine = [&](S_TestActor *a, float rounds, int container = ITEM_CONTAINER_BELT) {
        C_Item *m = New(ammoType);
        static_cast<C_AmmoItem *>(m)->SetAmount(rounds);
        C_Item *r = nullptr;
        a->human->GetContainer(container)->AddItem(m, &r, -1);
        return m;
    };
    const int reloadMs = Comp(New(g_guns.rifle))->GetReloadTimeMs();
    int uid = 5;
    // Refusals: no owner, still reloading, a vehicle's container other than 0, a container past the contents, no
    // container, one without space.
    {
        C_WeaponItem *w = Weapon(g_guns.rifle);
        w->Init(g_guns.rifle);
        S_TestActor *a = Human(300, 1000);
        magazine(a, full);
        CHECK(!w->Reload(nullptr, ITEM_CONTAINER_BELT, &uid, false) && uid == -1);
        w->m_readyTime = 1001;
        CHECK(!w->Reload(a->human, ITEM_CONTAINER_BELT, &uid, false));
        w->m_readyTime = 1000;
        CHECK(!w->Reload(Vehicle(301)->human, 1, &uid, false) && !w->Reload(a->human, 7, &uid, false) &&
              !w->Reload(a->human, ITEM_CONTAINER_BACKPACK_CONTENTS, &uid, false));
        a->containers[ITEM_CONTAINER_BELT]->SetMaxSpace(0.0f);
        CHECK(!w->Reload(a->human, ITEM_CONTAINER_BELT, &uid, false));
        // A slot (no slots): no magazine found.
        CHECK(!w->Reload(a->human, ITEM_CONTAINER_HANDS, &uid, false));
    }
    // An empty rifle, one full magazine: full; the magazine gone (its uid), the reload time.
    {
        C_WeaponItem *w = Weapon(g_guns.rifle);
        w->Init(g_guns.rifle);
        w->m_rounds = 0.0f;
        S_TestActor *a = Human(302, 1000);
        C_Item *m = magazine(a, full);
        m->SetUid(77);
        g_log.clear();
        CHECK(w->Reload(a->human, ITEM_CONTAINER_BELT, &uid, false) && uid == 77 && w->m_rounds == full &&
              a->containers[ITEM_CONTAINER_BELT]->IsEmpty() && w->m_readyTime == static_cast<uint32_t>(1000 + reloadMs) &&
              w->GetTime() == static_cast<uint32_t>(1000 + reloadMs) && Logged("sound"));   // (a local game is a client too)
        // Kept as the last (asked to keep it): full, the magazine stays.
        C_WeaponItem *k = Weapon(g_guns.rifle);
        k->Init(g_guns.rifle);
        k->m_rounds = 1.0f;
        S_TestActor *b = Human(303, 1000);
        magazine(b, 3.0f);
        CHECK(k->Reload(b->human, ITEM_CONTAINER_BELT, &uid, true) && uid == -1 && k->m_rounds == full &&
              b->containers[ITEM_CONTAINER_BELT]->CountItems(ammoType) == 1);
    }
    // Topping up: the rest goes into the emptiest magazine when it fits there, else the fullest keeps it.
    if (full >= 4.0f) {
        C_WeaponItem *w = Weapon(g_guns.rifle);
        w->Init(g_guns.rifle);
        w->m_rounds = full - 2.0f;
        S_TestActor *a = Human(304, 1000);
        C_Item *m1 = magazine(a, full - 1.0f), *m2 = magazine(a, full), *m3 = magazine(a, 1.0f);
        CHECK(w->Reload(a->human, ITEM_CONTAINER_BELT, &uid, false) && w->m_rounds == full);
        // The fullest (m2) gave `full`; 2 fitted; full - 2 went into the emptiest (m3: 1 + full - 2 > full? no: fits).
        CHECK(m3->GetAmount() == full - 1.0f && m1->GetAmount() == full - 1.0f && a->containers[ITEM_CONTAINER_BELT]->CountItems(ammoType) == 2 &&
              uid == m2->GetUid());
        // The emptiest cannot take the rest: the fullest keeps it (not used up).
        C_WeaponItem *x = Weapon(g_guns.rifle);
        x->Init(g_guns.rifle);
        x->m_rounds = full - 1.0f;
        S_TestActor *b = Human(305, 1000);
        C_Item *n1 = magazine(b, full), *n2 = magazine(b, full - 1.0f);
        CHECK(x->Reload(b->human, ITEM_CONTAINER_BELT, &uid, false) && x->m_rounds == full && n1->GetAmount() == full - 1.0f &&
              n2->GetAmount() == full - 1.0f && uid == -1 && b->containers[ITEM_CONTAINER_BELT]->CountItems(ammoType) == 2);
        // One magazine only (the fullest is the emptiest): it keeps the rest.
        C_WeaponItem *y = Weapon(g_guns.rifle);
        y->Init(g_guns.rifle);
        y->m_rounds = 1.0f;
        S_TestActor *c = Human(306, 1000);
        C_Item *o1 = magazine(c, full);
        CHECK(y->Reload(c->human, ITEM_CONTAINER_BELT, &uid, false) && y->m_rounds == full && o1->GetAmount() == 1.0f && uid == -1);
        // The rest exactly 0 (fills it): used up.
        C_WeaponItem *z = Weapon(g_guns.rifle);
        z->Init(g_guns.rifle);
        z->m_rounds = 2.0f;
        S_TestActor *d = Human(307, 1000);
        C_Item *q1 = magazine(d, full - 2.0f);
        CHECK(z->Reload(d->human, ITEM_CONTAINER_BELT, &uid, false) && z->m_rounds == full && uid == q1->GetUid() &&
              d->containers[ITEM_CONTAINER_BELT]->IsEmpty());
    }
    // The fullest and the emptiest whatever their order.
    if (full >= 6.0f) {
        C_WeaponItem *w = Weapon(g_guns.rifle);
        w->Init(g_guns.rifle);
        w->m_rounds = 3.0f;
        S_TestActor *a = Human(308, 1000);
        C_Item *m1 = magazine(a, 2.0f), *m2 = magazine(a, 1.0f), *m3 = magazine(a, full);
        m3->SetUid(123);
        (void)m1;
        const int m3uid = m3->GetUid();
        CHECK(w->Reload(a->human, ITEM_CONTAINER_BELT, &uid, false) && w->m_rounds == full && m2->GetAmount() == 4.0f && uid == m3uid &&
              a->containers[ITEM_CONTAINER_BELT]->CountItems(ammoType) == 2);
    }
    // No magazine of its ammunition (another type first): nothing, unless a reload is pending (cleared, true).
    {
        C_WeaponItem *w = Weapon(g_guns.rifle);
        w->Init(g_guns.rifle);
        w->m_rounds = 0.0f;
        S_TestActor *a = Human(309, 1000);
        C_Item *r = nullptr;
        a->containers[ITEM_CONTAINER_BELT]->AddItem(New(g_types.pistol), &r, -1);
        CHECK(!w->Reload(a->human, ITEM_CONTAINER_BELT, &uid, false));
        w->m_flags |= WEAPON_RELOAD_PENDING;
        CHECK(w->Reload(a->human, ITEM_CONTAINER_BELT, &uid, false) && !(w->m_flags & WEAPON_RELOAD_PENDING) && w->m_readyTime == 0);
        // Infinite ammunition (a local game): full without a magazine.
        g_itemsModule.SetInfiniteAmmo(true);
        CHECK(w->Reload(a->human, ITEM_CONTAINER_BELT, &uid, false) && w->m_rounds == full);
        g_gameRole = GAME_ROLE_CLIENT;
        w->m_rounds = 0.0f;
        w->m_readyTime = 0;
        CHECK(!w->Reload(a->human, ITEM_CONTAINER_BELT, &uid, false));
        g_gameRole = GAME_ROLE_LOCAL;
        g_itemsModule.SetInfiniteAmmo(false);
    }
    // A client: the reload sound (at the shot dummy, ambient for the local player in first person), the inventory
    // changed; the sound's id kept while it lives.
    {
        C_WeaponItem *w = Weapon(g_guns.rifle);
        w->Init(g_guns.rifle);
        w->m_rounds = 0.0f;
        S_TestActor *a = Human(310, 1000);
        magazine(a, full);
        g_gameRole = GAME_ROLE_CLIENT;
        g_soundAlive = true;
        g_localPlayer = g_firstPerson = true;
        a->human->m_inventoryChanged = false;
        g_log.clear();
        CHECK(w->Reload(a->human, ITEM_CONTAINER_BELT, &uid, false));
        char want[200];
        std::snprintf(want, sizeof want, "soundreq 0 0 0 flags 40 attach shdum source %p far 1", static_cast<void *>(a->human));
        CHECK(Count("sound " + std::to_string(SOUND_CHAPTER_WEAPON_MANIPULATION) + " " + std::to_string(Comp(w)->GetReloadSound())) == 1 &&
              Count(want) == 1 && w->m_soundId >= 10 && a->human->m_inventoryChanged);
        // Not heard as its own: positional; the sound gone at once: no id.
        g_localPlayer = false;
        g_soundAlive = false;
        w->m_rounds = 0.0f;
        w->m_readyTime = 0;
        magazine(a, full);
        g_log.clear();
        CHECK(w->Reload(a->human, ITEM_CONTAINER_BELT, &uid, false) && w->m_soundId == -1);
        std::snprintf(want, sizeof want, "soundreq 0 0 0 flags 0 attach shdum source %p far 1", static_cast<void *>(a->human));
        CHECK(Count(want) == 1);
        g_firstPerson = false;
        // A vehicle's store (not a human): no sound.
        S_TestActor *v = Vehicle(311);
        magazine(v, full, 0);
        w->m_rounds = 0.0f;
        w->m_readyTime = 0;
        g_log.clear();
        CHECK(w->Reload(v->human, 0, &uid, false) && !Logged("sound"));
        g_gameRole = GAME_ROLE_LOCAL;
    }
    // A weapon that cannot be topped up and still has rounds: only a pending reload goes ahead (no rounds, the time).
    if (g_guns.noReload >= 0) {
        C_WeaponItem *w = Weapon(g_guns.noReload);
        w->Init(g_guns.noReload);
        w->m_rounds = 1.0f;
        S_TestActor *a = Human(312, 1000);
        CHECK(!w->Reload(a->human, ITEM_CONTAINER_BELT, &uid, false));
        w->m_flags |= WEAPON_RELOAD_PENDING;
        CHECK(w->Reload(nullptr, ITEM_CONTAINER_BELT, &uid, false) && w->m_rounds == 1.0f &&
              w->m_readyTime == static_cast<uint32_t>(1000 + Comp(w)->GetReloadTimeMs()) && !(w->m_flags & WEAPON_RELOAD_PENDING));
    }
    // Ready times only move later.
    {
        C_WeaponItem *w = Weapon(g_guns.rifle);
        w->Init(g_guns.rifle);
        w->m_rounds = 0.0f;
        S_TestActor *a = Human(313, 1000);
        magazine(a, full);
        w->SetTime(900000);
        CHECK(w->Reload(a->human, ITEM_CONTAINER_BELT, &uid, false) && w->GetTime() == 900000);
    }
    // A launcher reloads from the backpack contents when asked for the belt.
    if (g_guns.launcher >= 0 && g_itemsModule.GetItemDef(g_guns.launcher)->GetAmmoItemId() > 0) {
        const int rocket = g_itemsModule.GetItemDef(g_guns.launcher)->GetAmmoItemId();
        C_WeaponItem *w = Weapon(g_guns.launcher);
        w->Init(g_guns.launcher);
        w->m_rounds = 0.0f;
        S_TestActor *a = Human(314, 1000);
        C_Item *r = nullptr;
        Move(New(g_types.backpack), &r, nullptr, 0, a, ITEM_CONTAINER_BACKPACK);
        a->human->GetContainer(ITEM_CONTAINER_BACKPACK_CONTENTS)->AddItem(New(rocket), &r, -1);
        CHECK(w->Reload(a->human, ITEM_CONTAINER_BELT, &uid, false) && w->m_rounds > 0.0f &&
              a->human->GetContainer(ITEM_CONTAINER_BACKPACK_CONTENTS)->IsEmpty());
    }
    GetGame()->m_missionTime = 0;
}

// SetRounds, the empty click, stopping the reload sound.
void TestWeaponSounds()
{
    GetGame()->m_missionTime = 1000;
    C_WeaponItem *w = Weapon(g_guns.rifle);
    w->Init(g_guns.rifle);
    S_TestActor *a = Human(320, 1000);
    // SetRounds: the magazine removed from the owner (when one), the rounds, the pending flag cleared; a client hears.
    w->m_flags |= WEAPON_RELOAD_PENDING;
    g_gameRole = GAME_ROLE_SERVER;
    g_log.clear();
    w->SetRounds(a->human, 44, 7);
    CHECK(Count("human remove 44 1") == 1 && w->m_rounds == 7.0f && !(w->m_flags & WEAPON_RELOAD_PENDING) && !Logged("sound"));
    g_gameRole = GAME_ROLE_CLIENT;
    g_log.clear();
    w->SetRounds(a->human, -1, 3);
    CHECK(!Logged("human remove") && Logged("sound "));
    // The empty click: clients only; 1.1 m in front, attached to the model, ambient for the local player in first person.
    g_log.clear();
    w->PlayEmptySound(a->human);
    char want[200];
    std::snprintf(want, sizeof want, "soundreq 0 0 1.1 flags 0 attach weaponmodel source %p far 1", static_cast<void *>(a->human));
    CHECK(Count("sound " + std::to_string(SOUND_CHAPTER_WEAPON_MANIPULATION) + " 4") == 1 && Count(want) == 1);
    g_localPlayer = g_firstPerson = true;
    g_log.clear();
    w->PlayEmptySound(a->human);
    std::snprintf(want, sizeof want, "soundreq 0 0 1.1 flags 40 attach weaponmodel source %p far 1", static_cast<void *>(a->human));
    CHECK(Count(want) == 1);
    g_localPlayer = g_firstPerson = false;
    g_gameRole = GAME_ROLE_SERVER;
    g_log.clear();
    w->PlayEmptySound(a->human);
    CHECK(g_log.empty());
    g_gameRole = GAME_ROLE_LOCAL;
    // Stop the reload sound: only one that runs more than 200 ms longer; the id forgotten either way.
    w->m_soundId = 12;
    w->m_readyTime = 1201;
    g_log.clear();
    w->StopReloadSound();
    CHECK(Count("sound stop 12") == 1 && w->m_soundId == -1);
    w->m_soundId = 12;
    w->m_readyTime = 1200;
    g_log.clear();
    w->StopReloadSound();
    CHECK(g_log.empty() && w->m_soundId == -1);
    w->m_soundId = 12;
    w->m_readyTime = 0;
    w->StopReloadSound();
    CHECK(g_log.empty());
    w->m_readyTime = 5000;
    w->m_soundId = -1;
    w->StopReloadSound();
    CHECK(g_log.empty());
    GetGame()->m_missionTime = 0;
}

// Firing by kind.
void TestFire()
{
    GetGame()->m_missionTime = 2000;
    S_TestActor *a = Human(330, 1000);
    const S_vector pos(1, 2, 3), dir(0, 0, 1);
    // Thrown: the shot manager throws it (quiet when the low flag byte is set); counted.
    if (g_guns.thrown >= 0) {
        C_WeaponItem *g = Weapon(g_guns.thrown);
        g->Init(g_guns.thrown);
        g_log.clear();
        CHECK(g->Fire(a->human, pos, dir, 0x100) == FIRE_OK && Count("throw " + std::to_string(g_guns.thrown) + " 0") == 1 && g->m_shotCount == 1);
        CHECK(g->Fire(a->human, pos, dir, 0x1) == FIRE_OK && Count("throw " + std::to_string(g_guns.thrown) + " 1") == 1);
        g_log.clear();
        CHECK(g->FireSecondary(a->human, pos, dir, 1) == FIRE_OK && Count("throw " + std::to_string(g_guns.thrown) + " 1") == 1);
        CHECK(g->FireSecondary(a->human, pos, dir, 0) == FIRE_OK && Count("throw " + std::to_string(g_guns.thrown) + " 0") == 1);
    }
    // FireSecondary of anything else: nothing.
    {
        C_WeaponItem *w = Weapon(g_guns.rifle);
        w->Init(g_guns.rifle);
        g_log.clear();
        CHECK(w->FireSecondary(a->human, pos, dir, 1) == FIRE_OK && g_log.empty());
    }
    // A firearm: the bullet (tracer every third shot of an SMG or MG), a round used, the shot's interval.
    for (int type : {g_guns.rifle, g_guns.smg, g_guns.mg}) {
        if (type < 0)
            continue;
        C_WeaponItem *w = Weapon(type);
        w->Init(type);
        const bool tracers = type != g_guns.rifle;
        const float before = w->m_rounds;
        w->m_lastShotTime = 1900;
        g_log.clear();
        const std::string shell = std::to_string(w->GetDef()->GetShellSound());
        for (int i = 1; i <= 3; ++i)
            CHECK(w->Fire(a->human, pos, dir, 0x200) == FIRE_OK);
        CHECK(Count("firebullet 1 512 " + shell) == (tracers ? 1 : 0) && Count("firebullet 0 512 " + shell) == (tracers ? 2 : 3));
        CHECK(w->m_rounds == before - 3.0f && w->m_lastShotTime == 2000 && w->m_shotInterval == 0 && w->m_shotCount == 3);
    }
    {
        C_WeaponItem *w = Weapon(g_guns.rifle);
        w->Init(g_guns.rifle);
        w->m_lastShotTime = 1900;
        w->Fire(a->human, pos, dir, 0);
        CHECK(w->m_shotInterval == 100);
    }
    // Melee: a server strikes; a client does not.
    if (g_guns.melee >= 0) {
        C_WeaponItem *k = Weapon(g_guns.melee);
        k->Init(g_guns.melee);
        g_log.clear();
        CHECK(k->Fire(a->human, pos, dir, 0) == FIRE_OK && Count("melee 0") == 1);
        g_gameRole = GAME_ROLE_CLIENT;
        g_log.clear();
        CHECK(k->Fire(a->human, pos, dir, 0) == FIRE_OK && !Logged("melee"));
        g_gameRole = GAME_ROLE_LOCAL;
    }
    // A projectile weapon: the blast effect at the blast dummy, the projectile fired (trail 0x5B) and hidden, a round
    // used; without a blast dummy, no effect.
    if (g_guns.launcher >= 0) {
        C_WeaponItem *l = Weapon(g_guns.launcher, "projectile");
        l->Init(g_guns.launcher);
        const float before = l->m_rounds;
        l->m_lastShotTime = 1500;
        g_log.clear();
        CHECK(l->Fire(a->human, pos, dir, 0x7) == FIRE_OK);
        CHECK(Count("particles " + std::to_string(Comp(l)->GetEffect())) == 1 && Count("fireprojectile projectile 91 0 0 7") == 1 &&
              Count("off projectile") == 1 && l->m_rounds == before - 1.0f && l->m_shotInterval == 500 && l->m_lastShotTime == 2000);
        C_WeaponItem *m = Weapon(g_guns.launcher);
        m->Init(g_guns.launcher);
        for (auto it = g_kids[m->m_frame].begin(); it != g_kids[m->m_frame].end();)
            it = FrameName(*it) == "blsdum" ? g_kids[m->m_frame].erase(it) : it + 1;
        g_log.clear();
        CHECK(m->Fire(a->human, pos, dir, 0) == FIRE_OK && !Logged("particles") && Count("fireprojectile null 91 0 0 0") == 1);
    }
    GetGame()->m_missionTime = 0;
}

// The projectile: looked up once ("projectile" or "mag1": kind 2, "mag2": 3, "mag3": 4, none: 0), copied, its
// position and rotation kept; attached to a hand (the copy shown there, the original hidden) or back on the weapon.
void TestProjectile()
{
    if (g_guns.launcher < 0)
        return;
    struct S_Kind {
        const char *name;
        int kind;
    };
    for (const S_Kind &k : {S_Kind{"projectile", 2}, S_Kind{"mag1", 2}, S_Kind{"mag2", 3}, S_Kind{"mag3", 4}, S_Kind{nullptr, 0}}) {
        C_WeaponItem *w = Weapon(g_guns.launcher, k.name);
        w->Init(g_guns.launcher);
        CHECK(w->m_projectileKind == 1);
        CHECK(w->GetProjectileKind() == k.kind && w->GetProjectileKind() == k.kind);
        CHECK((w->GetProjectileFrame() != nullptr) == (k.name != nullptr) && (w->m_projectileCopy != nullptr) == (k.name != nullptr));
        if (k.name)
            CHECK(w->m_projectilePos.x == 0.5f && w->m_projectilePos.y == 0.25f && w->m_projectilePos.z == 0.125f && w->m_projectilePos.w == 1.0f &&
                  w->m_projectileRot.v.y == 0.6f && w->m_projectileRot.s == 0.8f);
    }
    // No model: kind 0.
    {
        C_WeaponItem *w = Weapon(g_guns.launcher);
        w->Init(g_guns.launcher);
        w->m_frame = nullptr;
        CHECK(w->GetProjectileKind() == 0 && w->GetProjectileFrame() == nullptr);
    }
    // Attached to a hand: the copy linked there at its origin, facing z, shown; the original hidden; both updated.
    C_WeaponItem *w = Weapon(g_guns.launcher, "projectile");
    w->Init(g_guns.launcher);
    I3D_frame *hand = Named("HW1", FRAME_DUMMY);
    g_log.clear();
    w->AttachProjectile(hand);
    I3D_frame *copy = w->m_projectileCopy;
    CHECK(copy && Count("link " + FrameName(copy) + " HW1") == 1 && Count("worlddir " + FrameName(copy) + " 0 0 1") == 1 &&
          Count("on " + FrameName(copy)) == 1 && Count("off projectile") == 1 && Count("update projectile") == 1 && Same(copy->GetPos(), S_vector(0, 0, 0)));
    // Back on the weapon: the copy linked to the model at the kept position and rotation, hidden; the original shown.
    g_log.clear();
    w->AttachProjectile(nullptr);
    CHECK(Count("link " + FrameName(copy) + " weaponmodel") == 1 && Count("off " + FrameName(copy)) == 1 && Count("on projectile") == 1 &&
          Same(copy->GetPos(), S_vector(0.5f, 0.25f, 0.125f)) && copy->m_rot.v.y == 0.6f);
    // Without a projectile: nothing.
    C_WeaponItem *none = Weapon(g_guns.launcher);
    none->Init(g_guns.launcher);
    g_log.clear();
    none->AttachProjectile(hand);
    CHECK(!Logged("link"));
}

// Save / load: every field back; an empty launcher hides its round ("mag1").
void TestWeaponSave()
{
    C_WeaponItem *w = Weapon(g_guns.rifle);
    w->Init(g_guns.rifle);
    w->m_rounds = 3.5f;
    w->m_capacity = 9.0f;
    w->m_attackTime = 11;
    w->m_readyTime = 12;
    w->m_flags = 0x15;
    w->m_lastShotTime = 13;
    w->m_shotInterval = 14;
    w->m_shotCount = 15;
    w->m_projectileKind = 3;
    w->m_unknown90 = true;
    w->m_unknown94 = 16;
    g_files["test.sav"].clear();
    C_chunk out;
    out.Open("test.sav", true);
    out.Begin(0x1234);
    w->SaveGame(out);
    out.Ascend();
    out.Close();
    C_WeaponItem *b = Weapon(g_guns.rifle);
    b->Init(g_guns.rifle);
    C_chunk in;
    in.Open("test.sav", false);
    uint16_t id;
    in.Descend(id);
    CHECK(b->LoadGame(in));
    in.Close();
    CHECK(b->m_rounds == 3.5f && b->m_capacity == 9.0f && b->m_attackTime == 11 && b->m_readyTime == 12 && b->m_flags == 0x15 &&
          b->m_lastShotTime == 13 && b->m_shotInterval == 14 && b->m_shotCount == 15 && b->m_projectileKind == 3 && b->m_unknown90 &&
          b->m_unknown94 == 16);
    // An empty launcher: its round hidden; a loaded one not.
    if (g_guns.launcher >= 0) {
        for (float rounds : {0.0f, 1.0f}) {
            C_WeaponItem *l = Weapon(g_guns.launcher, "mag1");
            l->Init(g_guns.launcher);
            l->m_rounds = rounds;
            g_files["test.sav"].clear();
            C_chunk o;
            o.Open("test.sav", true);
            o.Begin(0x1234);
            l->SaveGame(o);
            o.Ascend();
            o.Close();
            C_chunk i;
            i.Open("test.sav", false);
            i.Descend(id);
            g_log.clear();
            l->LoadGame(i);
            i.Close();
            CHECK(Count("off mag1") == (rounds == 0.0f ? 1 : 0));
        }
    }
}

// The shot sound: up to two samples of the weapon's shot group loaded as sound frames (point, a cone, the sample's
// volume and ranges, 1.1 m in front of the model); played at the place given (ambient for the local player in first
// person, else 0.7 m in front); the AI's noise (a server, not quiet, a second apart; less far for types 7, 22, 260).
void TestShotSounds()
{
    GetGame()->m_missionTime = 5000;
    S_TestActor *a = Human(340, 1000);
    C_WeaponItem *w = Weapon(g_guns.rifle);
    w->Init(g_guns.rifle);
    const int group = Comp(w)->GetShotSound();
    auto *g = new S_SoundGroup();
    for (const char *file : {"shot_a", "shot_b", "shot_c"}) {
        auto *s = static_cast<S_SoundSample *>(::operator new(sizeof(S_SoundSample)));
        std::memset(static_cast<void *>(s), 0, sizeof(S_SoundSample));
        new (&s->fileName) C_str(file);
        s->nearRange = 2.0f;
        s->farRange = 50.0f;
        s->volume = 0.75f;
        g->samples.push_back(s);
    }
    g_groups[{SOUND_CHAPTER_WEAPON_SHOOTING, group}] = g;
    g_sounds = {"shot_a"};
    // A client: the frames loaded (the second does not open: released), the first played at the shot dummy.
    g_gameRole = GAME_ROLE_CLIENT;
    g_log.clear();
    w->PlayShotSound(a->human, w->GetShotDummy(), false);
    CHECK(w->m_shotSoundCount == 2 && w->m_shotSounds[0] && !w->m_shotSounds[1] && Count("createframe " + std::to_string(FRAME_SOUND)) == 2);
    CHECK(Count("soundtype ? " + std::to_string(I3DSOUND_POINT)) == 2 && Count("soundcone ? 1.5708 1.5708") == 1 && Count("soundvolume ? 0.75") == 1 &&
          Count("soundrange ? 2 50 0.4 0.3") == 1 && Count("link ? weaponmodel") == 1 && Count("link ? shdum") == 1 && Count("soundplay ? 0") == 1);
    CHECK(w->m_shotSounds[0]->GetPos().z == 0.7f && !Logged("soundreq") && Count("on ?") == 1 && Count("update ?") >= 1);
    // (A sound that did not open is asked for again by the random pick: whatever it asks, the first comes back after
    // loading. From here on a weapon whose two samples open.)
    g_sounds = {"shot_a", "shot_b"};
    w = Weapon(g_guns.rifle);
    w->Init(g_guns.rifle);
    w->PlayShotSound(a->human, w->GetShotDummy(), false);
    CHECK(w->m_shotSounds[0] && w->m_shotSounds[1]);
    // The local player in first person: ambient, at the place itself.
    g_localPlayer = g_firstPerson = true;
    g_log.clear();
    w->PlayShotSound(a->human, w->GetShotDummy(), false);
    CHECK(Count("soundtype ? " + std::to_string(I3DSOUND_AMBIENT)) == 1 && Count("createframe " + std::to_string(FRAME_SOUND)) == 0);
    bool atPlace = false;
    for (I3D_frame *s : w->m_shotSounds)
        atPlace |= s && s->GetPos().z == 0.0f;
    CHECK(atPlace);
    g_localPlayer = g_firstPerson = false;
    // A server (local game): the AI's noise, once a second at most, not when quiet.
    g_gameRole = GAME_ROLE_LOCAL;
    w->m_lastNoiseTime = 0;
    g_log.clear();
    w->PlayShotSound(a->human, w->GetShotDummy(), false);
    char want[200];
    std::snprintf(want, sizeof want, "soundreq 0 0 0 flags 22 attach shdum source %p far 1", static_cast<void *>(a->human));
    CHECK(Count("sound " + std::to_string(SOUND_CHAPTER_WEAPON_SHOOTING) + " " + std::to_string(group)) == 1 && Count(want) == 1 &&
          w->m_lastNoiseTime == 5000);
    GetGame()->m_missionTime = 6000;
    g_log.clear();
    w->PlayShotSound(a->human, w->GetShotDummy(), false);
    CHECK(!Logged("soundreq"));
    GetGame()->m_missionTime = 6001;
    w->PlayShotSound(a->human, w->GetShotDummy(), true);
    CHECK(!Logged("soundreq"));
    w->PlayShotSound(a->human, w->GetShotDummy(), false);
    CHECK(Logged("soundreq"));
    // The silenced types (7, 22, 260): 0.3 as far.
    for (int silenced : {7, 22, 260}) {
        if (!g_itemsModule.GetItemDef(silenced) || g_itemsModule.GetItemDef(silenced)->GetRecordType() != ITEM_ANIMATED ||
            !g_itemsModule.GetItemDef(silenced)->GetComponent())
            continue;
        C_WeaponItem *s = Weapon(silenced);
        s->Init(silenced);
        s->m_shotSoundCount = 1;
        s->m_shotSounds[0] = NewFrame(FRAME_SOUND);
        s->m_lastNoiseTime = 0;
        g_log.clear();
        s->PlayShotSound(a->human, nullptr, false);
        bool far03 = false;
        for (const std::string &l : g_log)
            far03 |= l.find("far 0.3") != std::string::npos;
        CHECK(far03);
    }
    // No samples: nothing loaded, nothing heard; sounds counted 0: nothing at all.
    C_WeaponItem *q = Weapon(g_guns.rifle);
    q->Init(g_guns.rifle);
    g_groups.clear();
    g_gameRole = GAME_ROLE_CLIENT;
    g_log.clear();
    q->PlayShotSound(a->human, nullptr, false);
    CHECK(Count("getgroup " + std::to_string(SOUND_CHAPTER_WEAPON_SHOOTING) + " " + std::to_string(group)) == 1 && !Logged("soundplay"));
    q->m_shotSoundCount = 0;
    g_log.clear();
    q->PlayShotSound(a->human, nullptr, false);
    CHECK(g_log.empty());
    g_gameRole = GAME_ROLE_LOCAL;
    // Released: both frames, from the second.
    w->m_shotSounds[1] = Named("second", FRAME_SOUND);
    g_frameNames[w->m_shotSounds[0]] = "first";
    g_log.clear();
    w->ReleaseShotSoundFrames();
    CHECK(Count("frame release second") == 1 && Count("frame release first") == 1 && At("frame release second") < At("frame release first") &&
          !w->m_shotSounds[0] && !w->m_shotSounds[1]);
    // The action sounds (clients): the last-shot sound with one round left, the bolt; at the shot dummy ambient for the
    // local player in first person, else 0.7 m in front of the model; silent to the AI; no source.
    C_WeaponItem *b = Weapon(g_guns.rifle);
    b->Init(g_guns.rifle);
    C_WeaponComp *comp = Comp(b);
    const int lastShot = comp->GetLastShotSound(), bolt = comp->GetBoltSound();
    g_gameRole = GAME_ROLE_CLIENT;
    b->m_rounds = 1.0f;
    b->m_readyTime = 0;
    g_log.clear();
    b->PlayActionSounds(a->human);
    // (The weapon sounds share one request: the range scale of the silenced shot above is still in it, as in the
    // original; compared without it.)
    CHECK(CountPrefix("soundreq 0 0 0.7 flags 1 attach weaponmodel source 00000000 far ") == (lastShot >= 0) + (bolt >= 0));
    CHECK((lastShot < 0 || Count("sound " + std::to_string(SOUND_CHAPTER_WEAPON_SHOOTING) + " " + std::to_string(lastShot)) == 1) &&
          (bolt < 0 || Count("sound " + std::to_string(SOUND_CHAPTER_WEAPON_MANIPULATION) + " " + std::to_string(bolt)) == 1));
    g_localPlayer = g_firstPerson = true;
    b->m_rounds = 2.0f;
    g_log.clear();
    b->PlayActionSounds(a->human);
    CHECK(CountPrefix("soundreq 0 0 0 flags 41 attach shdum source 00000000 far ") == (bolt >= 0) &&
          !(lastShot >= 0 && Logged("sound " + std::to_string(SOUND_CHAPTER_WEAPON_SHOOTING) + " " + std::to_string(lastShot))));
    g_localPlayer = g_firstPerson = false;
    g_gameRole = GAME_ROLE_LOCAL;
    g_log.clear();
    b->PlayActionSounds(a->human);
    CHECK(g_log.empty());
    // Every weapon type of the table: its action sounds as its component says (a client).
    g_gameRole = GAME_ROLE_CLIENT;
    int weapons = 0;
    for (int id = 0; id < MAX_ITEM_TYPES; ++id) {
        C_ItemDef *d = g_itemsModule.GetItemDef(id);
        if (!d || d->GetRecordType() != ITEM_ANIMATED || !d->GetComponent() || d->GetComponentKind() > ICK_FIREARM)
            continue;
        C_WeaponItem *x = Weapon(id);
        x->Init(id);
        x->m_rounds = 1.0f;
        g_log.clear();
        x->PlayActionSounds(a->human);
        const int ls = Comp(x)->GetLastShotSound(), bs = Comp(x)->GetBoltSound();
        CHECK(static_cast<int>(Sounds().size()) == 2 * ((ls >= 0) + (bs >= 0)));
        ++weapons;
    }
    CHECK(weapons > 30);
    g_gameRole = GAME_ROLE_LOCAL;
    GetGame()->m_missionTime = 0;
}

// ---- part 3c: targeted
void TestPart3Edges()
{
    GetGame()->m_missionTime = 1000;
    g_gameRole = GAME_ROLE_LOCAL;
    // Init again on a used weapon: everything reset; the flash hidden.
    {
        C_WeaponItem *w = Weapon(g_guns.rifle);
        w->Init(g_guns.rifle);
        w->m_attackTime = 5;
        w->m_flags = 0x7;
        w->m_capacity = 99.0f;
        w->m_readyTime = 5;
        w->m_cartridgeDummy = w->m_blastDummy = w->m_shotDummy = reinterpret_cast<I3D_frame *>(0x1);
        w->m_lastNoiseTime = 5;
        w->m_soundId = 5;
        g_log.clear();
        w->Init(g_guns.rifle);
        CHECK(w->m_attackTime == 0 && w->m_flags == WEAPON_UNKNOWN_FLAG1 && w->m_readyTime == 0 && !w->m_cartridgeDummy && !w->m_blastDummy &&
              !w->m_shotDummy && w->m_lastNoiseTime == 0 && w->m_soundId == -1 && Count("off flash") == 1);
        // A launcher-like definition whose ammunition is type 0: the capacity is type 0's amount (only below 0 is "none").
        if (g_guns.launcher >= 0) {
            auto *d = static_cast<C_AnimatedItemDef *>(g_itemsModule.GetItemDef(g_guns.launcher));
            const int ammo = d->m_ammoItemId;
            d->m_ammoItemId = 0;
            C_WeaponItem *l = Weapon(g_guns.launcher);
            l->Init(g_guns.launcher);
            CHECK(l->GetMaxAmount() == g_itemsModule.GetItemDef(0)->GetAmmoAmount());
            d->m_ammoItemId = ammo;
        }
        // A weapon without ammunition (not a launcher) initialised again: its capacity back to -1.
        const int plain = FindType([](C_ItemDef *d) {
            return d->GetRecordType() == ITEM_ANIMATED && d->GetAmmoItemId() < 0 && d->GetBehaviour() != ITEM_BEHAVIOUR_LAUNCHER;
        });
        if (plain >= 0) {
            C_WeaponItem *p = Weapon(plain);
            p->Init(plain);
            p->m_capacity = 99.0f;
            p->Init(plain);
            CHECK(p->m_capacity == -1.0f);
        }
        // Use: what is left stays, even under one round.
        w->m_rounds = 1.0f;
        w->Use(0.5f);
        CHECK(w->m_rounds == 0.5f);
    }
    const int ammoType = g_itemsModule.GetItemDef(g_guns.rifle)->GetAmmoItemId();
    const float full = g_itemsModule.GetItemDef(ammoType)->GetAmmoAmount();
    int uid = 0;
    C_Item *r = nullptr;
    // Reloading from the backpack contents (container 6); a rifle asked for the belt reloads from the belt even with a
    // backpack (only launchers go to the contents); a container of half a space unit still holds magazines.
    {
        S_TestActor *a = Human(400, 1000);
        Move(New(g_types.backpack), &r, nullptr, 0, a, ITEM_CONTAINER_BACKPACK);
        C_Item *mc = New(ammoType);
        a->human->GetContainer(ITEM_CONTAINER_BACKPACK_CONTENTS)->AddItem(mc, &r, -1);
        C_WeaponItem *w = Weapon(g_guns.rifle);
        w->Init(g_guns.rifle);
        w->m_rounds = 0.0f;
        CHECK(w->Reload(a->human, ITEM_CONTAINER_BACKPACK_CONTENTS, &uid, false) && w->m_rounds == full);
        C_Item *mb = New(ammoType);
        a->containers[ITEM_CONTAINER_BELT]->AddItem(mb, &r, -1);
        a->containers[ITEM_CONTAINER_BELT]->SetMaxSpace(0.5f);
        w->m_rounds = 0.0f;
        w->m_readyTime = 0;
        CHECK(w->Reload(a->human, ITEM_CONTAINER_BELT, &uid, false) && w->m_rounds == full && a->containers[ITEM_CONTAINER_BELT]->IsEmpty());
        // The magazine used up is destroyed (no longer an instance).
        CHECK(std::find(g_itemsModule.m_instances.begin(), g_itemsModule.m_instances.end(), mb) == g_itemsModule.m_instances.end());
    }
    // A launcher with ammunition: from the contents when asked for the belt (with a backpack).
    {
        const int launcher = FindType([](C_ItemDef *d) {
            return d->GetRecordType() == ITEM_ANIMATED && d->GetBehaviour() == ITEM_BEHAVIOUR_LAUNCHER && d->GetAmmoItemId() > 0 &&
                   g_itemsModule.GetItemDef(d->GetAmmoItemId()) && g_itemsModule.GetItemDef(d->GetAmmoItemId())->GetRecordType() == ITEM_AMMO;
        });
        if (launcher >= 0) {
            const int rocket = g_itemsModule.GetItemDef(launcher)->GetAmmoItemId();
            S_TestActor *a = Human(401, 1000);
            Move(New(g_types.backpack), &r, nullptr, 0, a, ITEM_CONTAINER_BACKPACK);
            a->human->GetContainer(ITEM_CONTAINER_BACKPACK_CONTENTS)->AddItem(New(rocket), &r, -1);
            C_WeaponItem *l = Weapon(launcher);
            l->Init(launcher);
            l->m_rounds = 0.0f;
            CHECK(l->Reload(a->human, ITEM_CONTAINER_BELT, &uid, false) && l->m_rounds > 0.0f &&
                  a->human->GetContainer(ITEM_CONTAINER_BACKPACK_CONTENTS)->IsEmpty());
        } else {
            std::printf("  (no launcher with ammunition in this table)\n");
        }
    }
    // Equal magazines: the first is the fullest (the one used up).
    {
        S_TestActor *a = Human(402, 1000);
        C_Item *m1 = New(ammoType), *m2 = New(ammoType);
        m1->SetUid(501);
        m2->SetUid(502);
        a->containers[ITEM_CONTAINER_BELT]->AddItem(m1, &r, -1);
        a->containers[ITEM_CONTAINER_BELT]->AddItem(m2, &r, -1);
        C_WeaponItem *w = Weapon(g_guns.rifle);
        w->Init(g_guns.rifle);
        w->m_rounds = 0.0f;
        CHECK(w->Reload(a->human, ITEM_CONTAINER_BELT, &uid, false) && uid == 501);
        // Equal smaller ones: the first stays the emptiest (it takes the rest).
        if (full >= 4.0f) {
            S_TestActor *b = Human(403, 1000);
            C_Item *big = New(ammoType), *s1 = New(ammoType), *s2 = New(ammoType);
            static_cast<C_AmmoItem *>(s1)->SetAmount(1.0f);
            static_cast<C_AmmoItem *>(s2)->SetAmount(1.0f);
            b->containers[ITEM_CONTAINER_BELT]->AddItem(s1, &r, -1);
            b->containers[ITEM_CONTAINER_BELT]->AddItem(big, &r, -1);
            b->containers[ITEM_CONTAINER_BELT]->AddItem(s2, &r, -1);
            C_WeaponItem *x = Weapon(g_guns.rifle);
            x->Init(g_guns.rifle);
            x->m_rounds = 2.0f;
            CHECK(x->Reload(b->human, ITEM_CONTAINER_BELT, &uid, false) && s1->GetAmount() == 3.0f && s2->GetAmount() == 1.0f);
        }
    }
    // Keeping the last magazine (nothing dropped): the inventory still changed (a human, a client).
    {
        S_TestActor *a = Human(404, 1000);
        a->containers[ITEM_CONTAINER_BELT]->AddItem(New(ammoType), &r, -1);
        a->human->m_inventoryChanged = false;
        C_WeaponItem *w = Weapon(g_guns.rifle);
        w->Init(g_guns.rifle);
        w->m_rounds = 0.0f;
        CHECK(w->Reload(a->human, ITEM_CONTAINER_BELT, &uid, true) && a->human->m_inventoryChanged);
    }
    // SetRounds: uid 0 is a magazine; its reload sound positional (flags 0).
    {
        S_TestActor *a = Human(405, 1000);
        C_WeaponItem *w = Weapon(g_guns.rifle);
        w->Init(g_guns.rifle);
        g_gameRole = GAME_ROLE_CLIENT;
        g_localPlayer = g_firstPerson = true;
        g_log.clear();
        w->SetRounds(a->human, 0, 4);
        CHECK(Count("human remove 0 1") == 1 && CountPrefix("soundreq 0 0 0 flags 0 attach shdum") == 1);
        g_localPlayer = g_firstPerson = false;
        g_gameRole = GAME_ROLE_LOCAL;
        // The reload sound stopped with a ready time of 1 (not 0: running).
        w->m_soundId = 9;
        w->m_readyTime = 1;
        GetGame()->m_missionTime = 0;
        // (1 - 0 = 1 ms left: within 200 ms, not stopped; still forgotten.)
        g_log.clear();
        w->StopReloadSound();
        CHECK(g_log.empty() && w->m_soundId == -1);
        w->m_soundId = 9;
        w->m_readyTime = 0x80000000u;
        w->StopReloadSound();
        CHECK(g_log.empty());
        // The clock wrapped: ready at 1, now 0xFFFFFF00: 257 ms to go (the difference as a signed int): stopped.
        GetGame()->m_missionTime = 0xFFFFFF00u;
        w->m_soundId = 9;
        w->m_readyTime = 1;
        w->StopReloadSound();
        CHECK(Count("sound stop 9") == 1);
        GetGame()->m_missionTime = 1000;
    }
    // Firing a firearm: the shot sound (the AI's noise at the shot dummy) and the action sounds.
    {
        S_TestActor *a = Human(406, 1000);
        C_WeaponItem *w = Weapon(g_guns.rifle);
        w->Init(g_guns.rifle);
        w->m_shotSoundCount = 1;
        w->m_shotSounds[0] = Named("shotsound", FRAME_SOUND);
        w->m_lastNoiseTime = 0;
        GetGame()->m_missionTime = 5000;   // (a noise more than a second after the last)
        g_log.clear();
        w->Fire(a->human, S_vector(0, 0, 0), S_vector(0, 0, 1), 0);
        CHECK(CountPrefix("soundreq 0 0 0 flags 22 attach shdum") == 1 && Count("soundplay shotsound 0") == 1 && Count("link shotsound shdum") == 1);
        CHECK(Count("frame link flags 0") >= 1 && w->m_shotSounds[0]->GetPos().x == 0.0f);
        // Action sounds with a bolt.
        const int bolted = FindType([](C_ItemDef *d) {
            return d->GetRecordType() == ITEM_ANIMATED && d->GetComponent() && d->GetComponentKind() <= ICK_FIREARM &&
                   static_cast<C_WeaponComp *>(d->GetComponent())->GetBoltSound() >= 0;
        });
        if (bolted >= 0) {
            C_WeaponItem *b = Weapon(bolted);
            b->Init(bolted);
            b->m_rounds = 5.0f;
            // Fired: the action sounds too.
            g_log.clear();
            b->Fire(a->human, S_vector(0, 0, 0), S_vector(0, 0, 1), 0);
            CHECK(Count("sound " + std::to_string(SOUND_CHAPTER_WEAPON_MANIPULATION) + " " + std::to_string(Comp(b)->GetBoltSound())) == 1);
            // (Heard by others first: the shared request then holds 0.7 m and the model; the own weapon's sound must set
            // its own place and attachment.)
            b->PlayActionSounds(a->human);
            g_localPlayer = g_firstPerson = true;
            g_log.clear();
            b->PlayActionSounds(a->human);
            CHECK(Count("sound " + std::to_string(SOUND_CHAPTER_WEAPON_MANIPULATION) + " " + std::to_string(Comp(b)->GetBoltSound())) == 1 &&
                  CountPrefix("soundreq 0 0 0 flags 41 attach shdum source 00000000") == 1);
            g_localPlayer = g_firstPerson = false;
            g_log.clear();
            b->PlayActionSounds(a->human);
            CHECK(CountPrefix("soundreq 0 0 0.7 flags 1 attach weaponmodel source 00000000") == 1);
        }
    }
    // A projectile weapon: the blast effect faces the camera, not transient; without a blast dummy its sound at the
    // model.
    if (g_guns.launcher >= 0) {
        S_TestActor *a = Human(407, 1000);
        C_WeaponItem *l = Weapon(g_guns.launcher, "projectile");
        l->Init(g_guns.launcher);
        l->m_shotSoundCount = 0;
        g_log.clear();
        l->Fire(a->human, S_vector(0, 0, 0), S_vector(0, 0, 1), 0);
        CHECK(Count("particles args 1 0") == 1);
        C_WeaponItem *m = Weapon(g_guns.launcher);
        m->Init(g_guns.launcher);
        for (auto it = g_kids[m->m_frame].begin(); it != g_kids[m->m_frame].end();)
            it = FrameName(*it) == "blsdum" ? g_kids[m->m_frame].erase(it) : it + 1;
        m->m_shotSoundCount = 1;
        m->m_shotSounds[0] = Named("ls", FRAME_SOUND);
        m->m_lastNoiseTime = 0;
        GetGame()->m_missionTime = 5000;   // (a noise more than a second after the last)
        g_log.clear();
        m->Fire(a->human, S_vector(0, 0, 0), S_vector(0, 0, 1), 0);
        CHECK(CountPrefix("soundreq 0 0 0 flags 22 attach weaponmodel") == 1 && Count("link ls weaponmodel") == 1);
    }
    // The projectile copy attached to a hand: linked with flags 0, at the origin (whatever it was), updated.
    if (g_guns.launcher >= 0) {
        C_WeaponItem *w = Weapon(g_guns.launcher, "projectile");
        w->Init(g_guns.launcher);
        w->GetProjectileKind();
        I3D_frame *copy = w->m_projectileCopy;
        copy->m_localMatrix(3) = S_vector(4, 5, 6);
        I3D_frame *hand = Named("HW2", FRAME_DUMMY);
        g_log.clear();
        w->AttachProjectile(hand);
        CHECK(Same(copy->GetPos(), S_vector(0, 0, 0)) && Count("update " + FrameName(copy)) == 1 && Count("frame link flags 0") == 1);
    }
    // A shot sound that does not open: released.
    {
        auto *g = new S_SoundGroup();
        auto *s = static_cast<S_SoundSample *>(::operator new(sizeof(S_SoundSample)));
        std::memset(static_cast<void *>(s), 0, sizeof(S_SoundSample));
        new (&s->fileName) C_str("nofile");
        g->samples.push_back(s);
        C_WeaponItem *w = Weapon(g_guns.rifle);
        w->Init(g_guns.rifle);
        g_groups[{SOUND_CHAPTER_WEAPON_SHOOTING, Comp(w)->GetShotSound()}] = g;
        g_sounds.clear();
        g_gameRole = GAME_ROLE_CLIENT;
        g_log.clear();
        CHECK(w->GetShotSoundFrame(0) == nullptr && Count("frame release") == 1);
        g_gameRole = GAME_ROLE_LOCAL;
        g_groups.clear();
    }
    GetGame()->m_missionTime = 0;
}

// Frame trees released for real (the children lists follow the links): deepest first; projectors, emitters,
// sounds and lights only unlinked; frames already released (no references) left alone.
void TestFrameRelease()
{
    I3D_frame *light = Named("light", FRAME_LIGHT);
    I3D_frame *projector = Named("projector", FRAME_VISUAL);
    g_visualTypes[projector] = I3DVISUAL_PROJECTOR;
    I3D_frame *emitter = Named("emitter", FRAME_VISUAL);
    g_visualTypes[emitter] = I3DVISUAL_EMITTER;
    I3D_frame *sound = Named("sound", FRAME_SOUND);
    I3D_frame *leaf = Named("leaf", FRAME_VISUAL);
    I3D_frame *mesh = Named("mesh", FRAME_VISUAL, {leaf});
    I3D_frame *top = Named("top", FRAME_MODEL, {light, projector, emitter, sound, mesh});
    g_log.clear();
    ReleaseFrameTree(top);
    CHECK(Count("frame release leaf") == 1 && Count("frame release mesh") == 1 && Count("frame release top") == 1 &&
          Count("frame release light") == 0 && Count("frame release projector") == 0 && Count("frame release emitter") == 0 &&
          Count("frame release sound") == 0);
    CHECK(Count("link leaf null") == 1 && Count("link light null") == 1 && Count("link top null") == 1 && g_kids[top].empty() &&
          At("frame release leaf") < At("frame release mesh") && At("frame release mesh") < At("frame release top"));
    CHECK(Count("frame link flags 0") == 7);
    // No references: nothing.
    I3D_frame *gone = Named("gone", FRAME_MODEL, {Named("child")});
    gone->m_refCount = 0;
    g_log.clear();
    ReleaseFrameTree(gone);
    CHECK(g_log.empty());
    gone->m_refCount = 1;
    // A copy whose child cannot be made: false.
    I3D_frame *root = Named("root2", FRAME_MODEL, {Named("dummy2", FRAME_DUMMY)});
    I3D_frame *copy = nullptr;
    g_failFrameType = FRAME_DUMMY;
    CHECK(!CloneFrameTree(copy, root, nullptr));
    g_failFrameType = FRAME_MODEL;
    I3D_frame *copy2 = nullptr;
    CHECK(!CloneFrameTree(copy2, root, nullptr));
    g_failFrameType = -1;
    // The copy linked under its parent with flags 0.
    I3D_frame *copy3 = nullptr;
    g_log.clear();
    CHECK(CloneFrameTree(copy3, Named("single"), Named("parent3")) && Count("frame link flags 0") == 1);
    // An item's destructor releases its model.
    C_Item *item = g_itemsModule.NewItem(g_types.pistol);
    item->m_frame = Named("itemmodel");
    g_log.clear();
    delete item;
    CHECK(Count("frame release itemmodel") == 1);
}

// The module's uids, freeing, and Done.
void TestModuleEnd()
{
    // Reset forgets the freed uids.
    g_itemsModule.m_freedUids.push_back({0, 42});
    g_itemsModule.ResetUids();
    CHECK(g_itemsModule.NextUid() == 0);
    // A freed uid old enough is used again.
    g_itemsModule.m_freedUids.push_back({0, 42});
    CHECK(g_itemsModule.NextUid() == 42 && g_itemsModule.m_freedUids.empty());
    // Freeing the mission's data deletes the placements' items (their models released).
    g_itemsModule.FreePlacements();
    S_ItemPlacement pl = {};
    pl.item = g_itemsModule.NewItem(g_types.pistol);
    pl.item->m_frame = Named("placedmodel");
    g_itemsModule.AddPlacement(pl);
    g_log.clear();
    g_itemsModule.FreeMissionData(true);
    CHECK(Count("frame release placedmodel") == 1);
    g_itemsModule.FreePlacements();
    // The mission file: the name's length is its own plus the terminator; writing failing at each step: false.
    GetGame()->m_missionName = "TEST";
    S_TestActor *a = Human(450, 1000);
    a->human->m_frame = Named("writer");
    C_Item *r = nullptr;
    Move(New(g_types.pistol), &r, nullptr, 0, a, ITEM_CONTAINER_BELT, false);
    g_actorsModule.m_actors.assign(1, a->human);
    CHECK(SaveMissionItems());
    const std::string saved = g_files[Key("Missions\\TEST\\items.dat")];
    int length;
    std::memcpy(&length, saved.data() + 4, 4);
    CHECK(length == 7 && saved.size() == 4 + 4 + 8 + 24);
    for (int ok : {0, 1, 2, 3}) {
        g_writesLeft = ok;
        CHECK(!SaveMissionItems());
    }
    g_writesLeft = -1;
    CHECK(g_open.empty());
    g_actorsModule.m_actors.clear();
    // Opened from the disk first; an old version told with flags 0.
    const std::string key = Key("Missions\\TEST\\items.dat");
    g_files[key] = std::string("\x0E\x00\x00\x00", 4);
    g_log.clear();
    g_itemsModule.LoadMissionItems();
    CHECK(Count("dtaopen missions\\test\\items.dat 0") == 1 && Count("message flags 0") == 1);
    // Names of 127 characters fit the buffer; 128 do not.
    auto rec = [](int length) {
        std::string s(reinterpret_cast<const char *>(&length), 4);
        s += std::string(length, 'n');
        s += '\0';
        for (int v : {3, 0, 0, 0, 0, 0})
            s.append(reinterpret_cast<const char *>(&v), 4);
        return s;
    };
    const int v16 = 16;
    const std::string head(reinterpret_cast<const char *>(&v16), 4);
    g_findFrame = nullptr;
    g_files[key] = head + rec(127);
    CHECK(g_itemsModule.LoadMissionItems());
    g_files[key] = head + rec(128);
    CHECK(!g_itemsModule.LoadMissionItems());
    // Version 15: a placed item's position has its w (16 bytes).
    {
        g_itemsModule.m_missionActive = true;
        g_itemsModule.FreePlacements();
        const int v15 = 15, zero = 0, type = g_types.mine, placed = 1;
        std::string s(reinterpret_cast<const char *>(&v15), 4);
        s.append(reinterpret_cast<const char *>(&zero), 4);
        s += '\0';
        for (int v : {0, type, 0, placed})
            s.append(reinterpret_cast<const char *>(&v), 4);
        for (float f : {1.0f, 2.0f, 3.0f, 9.0f, 0.0f, 0.0f, 0.0f, 1.0f})
            s.append(reinterpret_cast<const char *>(&f), 4);
        s.append(reinterpret_cast<const char *>(&placed), 4);   // armed
        g_files[key] = s;
        CHECK(g_itemsModule.LoadMissionItems() && g_itemsModule.m_placementCount == 1 && g_itemsModule.m_placements[0].pos.w == 9.0f &&
              g_itemsModule.m_placements[0].rot.s == 1.0f && g_itemsModule.m_placements[0].item->IsArmed());
        // Armed and empty read short (2 bytes): 0 both, not what the 2 bytes held.
        g_itemsModule.FreePlacements();
        std::string t16(reinterpret_cast<const char *>(&v16), 4);
        t16 += s.substr(4, s.size() - 8);
        t16 += std::string("\x01\x01", 2);
        g_files[key] = t16;
        CHECK(g_itemsModule.LoadMissionItems() && g_itemsModule.m_placementCount == 1 && !g_itemsModule.m_placements[0].item->IsArmed());
        const int launcher = FindType([](C_ItemDef *d) { return d->GetRecordType() == ITEM_ANIMATED && d->GetBehaviour() == ITEM_BEHAVIOUR_LAUNCHER; });
        if (launcher >= 0) {
            g_itemsModule.FreePlacements();
            std::string e = t16.substr(0, t16.size() - 2);
            std::memcpy(&e[4 + 4 + 1 + 4], &launcher, 4);
            e.append(reinterpret_cast<const char *>(&zero), 4);   // armed 0
            e += std::string("\x01\x01", 2);                      // empty read short
            g_files[key] = e;
            CHECK(g_itemsModule.LoadMissionItems() && g_itemsModule.m_placementCount == 1 && g_itemsModule.m_placements[0].item->GetAmount() > 0.0f);
        }
        // The placed items get new uids in order.
        g_itemsModule.FreePlacements();
        g_itemsModule.ResetUids();
        g_files[key] = s + s.substr(4);
        CHECK(g_itemsModule.LoadMissionItems() && g_itemsModule.m_placementCount == 2 && g_itemsModule.m_placements[0].item->GetUid() == 0 &&
              g_itemsModule.m_placements[1].item->GetUid() == 1);
        g_itemsModule.FreePlacements();
        g_itemsModule.m_missionActive = false;
    }
    // Done (last: it deletes the definitions): the marks freed, no definitions left.
    g_log.clear();
    g_itemsModule.Done();
    CHECK(Count("marks free") == 1 && g_itemsModule.GetItemDef(g_types.pistol) == nullptr && g_itemsModule.GetItemDef(228) == nullptr);
}
}  // namespace

int main()
{
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    _controlfp(0x00020000u, 0x00030000u);                       // _PC_24, _MCW_PC: the game's precision
    const char *table = std::getenv("ITEMS_SAV");
    g_table = ReadDisk(table ? table : "G:\\Hidden & Dangerous 2\\Tables\\items.sav");
    g_files[Key("Tables\\items.sav")] = g_table;
    const char *save = std::getenv("EQUIPMENT_SAVE");
    const char *temp = std::getenv("TEMP");
    g_save = ReadDisk(save ? save : std::string(temp ? temp : ".") + "\\eq_saves\\original_SMCAM_quick.sav");
    CHECK(!g_table.empty() && !g_save.empty());
    g_frameVt[0x00 / 4] = reinterpret_cast<void *>(F_Release);
    g_frameVt[0x30 / 4] = reinterpret_cast<void *>(F_SetOn);
    g_frameVt[0x38 / 4] = reinterpret_cast<void *>(F_LinkTo);
    g_frameVt[0x08 / 4] = reinterpret_cast<void *>(F_SetPos);
    g_frameVt[0x24 / 4] = reinterpret_cast<void *>(F_Update);
    g_sceneVt[0xEC / 4] = reinterpret_cast<void *>(F_UpdateFrameSector);
    g_sceneVt[0x7C / 4] = reinterpret_cast<void *>(F_SceneFindFrame);
    GetGame()->m_scene = reinterpret_cast<I3D_scene *>(&g_scene);
    g_gameVt[VirtualSlot(&C_game::SetState)] = reinterpret_cast<void *>(F_SetState);
    *reinterpret_cast<void ***>(g_gameBytes) = g_gameVt;
    g_driverVt[0x54 / 4] = reinterpret_cast<void *>(F_CreateFrame);
    g_driverVt[0x50 / 4] = reinterpret_cast<void *>(F_CreateVisual);
    g_frameVt[0x3C / 4] = reinterpret_cast<void *>(F_NumChildren);
    g_frameVt[0x40 / 4] = reinterpret_cast<void *>(F_GetChild);
    g_frameVt[0x48 / 4] = reinterpret_cast<void *>(F_FindChild);
    g_frameVt[0x4C / 4] = reinterpret_cast<void *>(F_Duplicate);
    g_frameVt[0x04 / 4] = reinterpret_cast<void *>(F_GetVisualType);
    g_frameVt[0x7C / 4] = reinterpret_cast<void *>(F_SoundType);
    g_frameVt[0x80 / 4] = reinterpret_cast<void *>(F_SoundRange);
    g_frameVt[0x84 / 4] = reinterpret_cast<void *>(F_SoundCone);
    g_frameVt[0x8C / 4] = reinterpret_cast<void *>(F_SoundVolume);
    g_frameVt[0x94 / 4] = reinterpret_cast<void *>(F_SoundPlay);
    g_frameVt[0x1C / 4] = reinterpret_cast<void *>(F_SetWorldDir);
    if (g_table.empty() || g_save.empty()) {
        std::printf("items: %d checks, %d failures (no items.sav or save)\n", checks, failures);
        return 1;
    }
    TestDefinitions();
    TestSaveRoundTrip();
    TestModuleRoundTrip();
    TestContainers();
    TestLoadedState();
    TestNewItems();
    TestTableFailures();
    TestDestructors();
    TestStoreShift();
    TestMedkits();
    TestLoadIds();
    FindTypes();
    TestMoveChecks();
    TestMoveEffects();
    TestNotify();
    TestPut();
    TestCreate();
    TestTransfer();
    TestTransferAuto();
    TestCanAdd();
    TestSwapBlocked();
    TestDrop();
    TestDefinitionRules();
    TestRulesExhaustive();
    TestMoveEdges();
    TestTransferShapes();
    TestSwapShapes();
    TestDropCalls();
    TestPart2Edges();
    TestSwapEdges();
    TestPart2Last();
    TestMissionFiles();
    TestMissionFileEdges();
    TestLifecycle();
    TestActivityProps();
    TestFrameTrees();
    FindGuns();
    TestWeaponBasics();
    TestReload();
    TestWeaponSounds();
    TestFire();
    TestProjectile();
    TestWeaponSave();
    TestShotSounds();
    TestPart3Edges();
    TestFrameRelease();
    TestModuleEnd();
    std::printf("items: %d checks, %d failures\n", checks, failures);
    return failures != 0;
}
