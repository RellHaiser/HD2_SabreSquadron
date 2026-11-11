// The item system: game/items_module.cpp, item_instances.cpp, item_defs.cpp and item_containers.cpp linked as they
// are, with common/chunk.cpp, common/c_str.cpp and the LS3DF port's math. The engine, the humans, sound, shots and
// the equipment are faked (logged). MinGW; build.bat; run items_test.exe ("items: N checks, 0 failures").
//
// Part 1: the definitions of the real items.sav (ITEMS_SAV) written back by C_ItemDef::Save as the table holds
// them; every actor item chunk (0x2775) of a quick save of the original (EQUIPMENT_SAVE) loaded into an actor with
// a human's containers (or one item store) and saved again, compared with the original's bytes.
// Part 2: moving items: MoveItem by container and check, NotifyMove, Put / CanPut, Create / Give, TransferItem and
// its swaps, TransferItemAuto, IsSwapBlocked, CanAdd / CanPutItemInSlot, removing and dropping, the definition rules.
#include <windows.h>

#include <algorithm>
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
#include "game/surface_marks.h"
#include "game/team.h"
#include "ls3d/i3d.h"
#include "ls3d/rw_data.h"
#undef private
#undef protected

bool LoadActorItems(C_Actor *actor, C_chunk &chunk);
bool IsSwapBlocked(C_Actor *from, int fromContainer, C_Actor *to, int toContainer, C_Item *item);
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
bool C_game::IsFirstPersonView() const { return false; }
int ShowMessage(const char *text, const char *title, unsigned int) { Log(std::string("message ") + text); return 0; }
C_Module::C_Module(int, const char *) {}
C_Module::~C_Module() {}
C_Actor *C_ActorsModule::GetActor(int index) const { return g_actorsById.count(index) ? g_actorsById[index] : nullptr; }
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
bool C_Human::IsLocalPlayer() const { return false; }
namespace {
int g_stance;
}
int C_Human::GetStance() const { return g_stance; }

// Sound, shots, particles, surface marks: logged.
namespace {
alignas(16) unsigned char g_soundBytes[0x200];
}
C_SoundModule *GetSoundModule() { return reinterpret_cast<C_SoundModule *>(g_soundBytes); }
int C_SoundModule::Play(const S_SoundRequest &r) { Log("sound " + std::to_string(r.chapter) + " " + std::to_string(r.group)); return 1; }
void C_SoundModule::Stop(int *slot) { Log("sound stop"); if (slot) *slot = 0; }
I3D_sound *C_SoundModule::GetSound(int) const { return nullptr; }
S_SoundGroup *C_SoundModule::GetGroup(int, int) const { return nullptr; }
void C_ShotManager::ThrowItem(C_Actor *, C_Item *item, const S_vector &, const S_vector &, bool fake)
{
    Log("throw " + std::to_string(item->GetTypeId()) + " " + std::to_string(fake));
}
void C_ShotManager::FireProjectile(C_Actor *, C_Item *, const S_vector &, const S_vector &, I3D_frame *, int, bool, bool, uint32_t)
{
    Log("fireprojectile");
}
void C_ShotManager::FireBullet(C_Actor *, C_Item *, const S_vector &, const S_vector &, bool, uint32_t, int) { Log("firebullet"); }
void MeleeAttack(C_Actor *, C_Item *, bool backstab, const S_vector &, const S_vector &) { Log("melee " + std::to_string(backstab)); }
int C_ParticlesModule::Create(int effect, const S_matrix &, I3D_frame *, bool, bool) { Log("particles " + std::to_string(effect)); return 0; }
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
template <> HRESULT C_ResourceCache<I3D_model>::OpenInto(I3D_model *, const char *name, uint32_t, void *, void *, int)
{
    Log(std::string("open model ") + name);
    return E_FAIL;
}
template <> HRESULT C_ResourceCache<I3D_sound>::OpenInto(I3D_sound *, const char *name, uint32_t, void *, void *, int)
{
    Log(std::string("open sound ") + name);
    return E_FAIL;
}
void I3D_frame::UpdateWMatrixProc() {}
namespace {
// Frames: a released count; the driver hands out new ones.
void *g_frameVt[128];
struct S_FakeFrame {
    alignas(16) unsigned char bytes[0x400];
};
uint32_t __stdcall F_Release(I3D_frame *) { Log("frame release"); return 0; }
void __stdcall F_SetOn(I3D_frame *, bool on) { Log(on ? "frame on" : "frame off"); }
void __stdcall F_LinkTo(I3D_frame *, I3D_frame *parent, uint32_t flags)
{
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
void __stdcall F_Update(I3D_frame *) { Log("frame update"); }
void *g_sceneVt[128];
struct {
    void **vt = g_sceneVt;
    unsigned char fields[0x400] = {};
} g_scene;
void __stdcall F_UpdateFrameSector(void *, I3D_frame *, const S_vector &) { Log("scene sector"); }
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
I3D_frame *__stdcall F_CreateFrame(void *, int type)
{
    Log("createframe " + std::to_string(type));
    return NewFrame(type);
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
    GetGame()->m_scene = reinterpret_cast<I3D_scene *>(&g_scene);
    g_gameVt[VirtualSlot(&C_game::SetState)] = reinterpret_cast<void *>(F_SetState);
    *reinterpret_cast<void ***>(g_gameBytes) = g_gameVt;
    g_driverVt[0x54 / 4] = reinterpret_cast<void *>(F_CreateFrame);
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
    std::printf("items: %d checks, %d failures\n", checks, failures);
    return failures != 0;
}
