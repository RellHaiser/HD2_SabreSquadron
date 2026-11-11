// The item system: game/items_module.cpp, item_instances.cpp, item_defs.cpp and item_containers.cpp linked as they
// are, with common/chunk.cpp, common/c_str.cpp and the LS3DF port's math. The engine, the humans, sound, shots and
// the equipment are faked (logged). MinGW; build.bat; run items_test.exe ("items: N checks, 0 failures").
//
// Part 1: the definitions of the real items.sav (ITEMS_SAV) written back by C_ItemDef::Save as the table holds
// them; every actor item chunk (0x2775) of a quick save of the original (EQUIPMENT_SAVE) loaded into an actor with
// a human's containers (or one item store) and saved again, compared with the original's bytes.
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
int C_HumanEquipment::GetHolsterSlot(int) { return HOLSTER_SLOT_COUNT; }
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
int C_Human::GetStance() const { return 0; }

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
    std::printf("items: %d checks, %d failures\n", checks, failures);
    return failures != 0;
}
