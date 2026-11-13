// C_ItemsModule, the "Items" module (original source 0x007d7ad0..0x007df2f0).
#include "game/first_person_view.h"
#include "game/interface.h"
#include "game/items_module.h"

#include <windows.h>

#include <cstdio>
#include <cstring>
#include <malloc.h>
#include <string>

#include "common/chunk.h"
#include "game/actor.h"
#include "game/actors_module.h"
#include "game/game.h"
#include "game/pending.h"
#include "game/resource_cache.h"
#include "game/surface_marks.h"
#include "game/world.h"
#include "ls3d/i3d.h"
#include "ls3d/rw_data.h"

C_ItemsModule g_itemsModule(MODULE_ITEMS, "Items");

namespace {

// Tables\items.sav: 500 slots; a slot is an int (0 = empty) or 508 bytes (the int, the
// record type, the definition's 500-byte stream).
constexpr int ITEMS_TABLE_SIZE = 0x3D860;
constexpr int ITEM_SLOT_SIZE = 508;
constexpr int ITEM_RECORD_HEADER = 8;
constexpr int ITEM_RECORD_STREAM = 500;

// Missions\<mission>\items.dat.
constexpr int MISSION_ITEMS_VERSION = 0x10;

// Mission time an item stays busy after a move (and a slot locked after a hands change).
constexpr uint32_t ITEM_BUSY_TIME = 920;
// Released uids are reused after this long (us).
constexpr uint32_t UID_REUSE_DELAY = 10000;
// Clients advance the surface marks in steps of this many ms.
constexpr int CLIENT_MARKS_STEP = 100;

// Saved-game chunks.
enum : uint16_t {
    CHUNK_ITEMS_MODULE = 0xB256,
    CHUNK_ITEMS_UIDS = 0xD575,
    CHUNK_ITEMS_LAST_UID = 0,
    CHUNK_ACTOR_ITEMS = 0x2775,
    CHUNK_ITEM_RECORD = 0xAD2C,
    CHUNK_ITEM_RECORD_TYPE = 0,
    CHUNK_ITEM_RECORD_CONTAINER = 1,
    CHUNK_ITEM_RECORD_SLOT = 2,
    CHUNK_ITEM_RECORD_INDEX = 3,
    CHUNK_ITEM_RECORD_STATE = 0x2CC,
    CHUNK_ITEM_RECORD_CONTENTS = 0x268,
};

// Models of the idle activity props.
const char *const ACTIVITY_PROP_FILES[3] = {"m_karty", "m_bottle", "m_cigarette"};
I3D_frame *g_activityProps[3];                          // 0x0098b868..0x0098b870
// Time collected by clients for the surface marks.
int g_clientMarksTime;                                  // 0x0098b874

bool IsServer() { return (g_gameRole & GAME_ROLE_SERVER) != 0; }
bool IsClient() { return (g_gameRole & GAME_ROLE_CLIENT) != 0; }
bool IsLocalGame() { return (g_gameRole & GAME_ROLE_LOCAL) == GAME_ROLE_LOCAL; }
// The module only acts in the pass that matches the machine's role.
bool IsOwnPass(bool server) { return server == IsServer(); }

uint32_t Now() { return GetGame()->GetMissionTime(); }

C_Human *ActivePlayer() { return g_teamManager.GetPlayerTeam()->GetActivePlayer(); }

C_FirstPersonView *Hud() { return GetGame()->GetFirstPersonView(); }

// Actors with the full set of containers are humans.
C_Human *AsHuman(C_Actor *actor) { return static_cast<C_Human *>(actor); }
C_HumanEquipment *EquipmentOf(C_Actor *human) { return AsHuman(human)->GetEquipment(); }

// Weight of the contents of a real backpack (0 for other items).
float ContentsWeight(C_Item *item)
{
    return item->GetDef()->IsBackpack() ? static_cast<C_ContainerItem *>(item)->GetStore()->GetTotalWeight() : 0.0f;
}

// Inventory space of an item type: twice its size, at least 1.
float SpaceOf(const C_ItemDef *def)
{
    const float size = static_cast<float>(def->GetInventorySize());
    return size == 0.0f ? 1.0f : size + size;
}

// Weight of the item (type) currently in a single-item container.
float OccupantWeight(C_ItemContainer *container)
{
    C_Item *occupant = container->GetItem();
    return occupant ? occupant->GetDef()->GetWeight() : 0.0f;
}

// Can `actor` take `extra` more weight while giving up `given`?
bool CanCarry(C_Actor *actor, float extra, float given)
{
    return static_cast<float>(actor->GetHitPoints()) + 0.0001f >= extra + actor->GetCarriedWeight() - given;
}

// Path of the current mission's items.dat.
std::string MissionItemsPath()
{
    return std::string("Missions\\") + GetGame()->GetMissionName() + "\\items.dat";
}

int ReadInt(C_chunk &chunk)
{
    int value = 0;
    chunk.Read(value);
    chunk.Ascend();
    return value;
}

// Reads and enters the next sub-chunk; false at the end.
bool DescendId(C_chunk &chunk, uint16_t &id)
{
    return chunk.RemainingSize() > 0 && chunk.Descend(id);
}

} // namespace

C_ItemDef *GetItemDef(int typeId)
{
    return g_itemsModule.GetItemDef(typeId);
}

// ---------------------------------------------------------------------------
// frames

bool CloneFrameTree(I3D_frame *&copy, I3D_frame *source, I3D_frame *parent)
{
    if (!source)
        return false;
    if (source->GetType() == FRAME_VISUAL)
        copy = I3DGetDriver()->CreateVisual(source->GetVisualType());
    else
        copy = I3DGetDriver()->CreateFrame(source->GetType());
    if (!copy)
        return false;
    copy->Duplicate(source);
    if (parent)
        copy->LinkTo(parent, 0);
    const int children = source->NumChildren();
    for (int i = 0; i < children; ++i) {
        I3D_frame *child = nullptr;
        if (!CloneFrameTree(child, source->GetChild(i), copy))
            return false;
    }
    return true;
}

void ReleaseFrameTree(I3D_frame *frame)
{
    if (!frame || frame->GetRefCount() <= 0)
        return;
    for (int count = frame->NumChildren(); count > 0; count = frame->NumChildren())
        ReleaseFrameTree(frame->GetChild(count - 1));
    frame->LinkTo(nullptr, 0);
    // (Projectors, emitters, sounds and lights are only unlinked.)
    const uint32_t visual = frame->GetVisualType();
    if (visual != I3DVISUAL_PROJECTOR && visual != I3DVISUAL_EMITTER && frame->GetType() != FRAME_SOUND &&
        frame->GetType() != FRAME_LIGHT)
        frame->Release();
}

I3D_frame *GetActivityProp(int index)
{
    if (index < 0 || index > 2)
        return nullptr;
    if (g_activityProps[index])
        return g_activityProps[index];
    I3D_frame *model = I3DGetDriver()->CreateFrame(FRAME_MODEL);
    if (g_modelCache.OpenInto(static_cast<I3D_model *>(model), ACTIVITY_PROP_FILES[index]) != I3D_OK) {
        model->Release();
        return g_activityProps[index];
    }
    g_activityProps[index] = model;
    if (index == 2) {
        // The cigarette's glow.
        if (I3D_frame *glow = model->FindChildFrame("lensf", FRAME_MASK_VISUAL)) {
            static_cast<I3D_lensflare_visual *>(glow)->SetUnknownParams(1.0f, 0.15f, 120.0f, 0.35f);
            glow->SetOn(false);
            glow->Update();
        }
    }
    return g_activityProps[index];
}

// ---------------------------------------------------------------------------
// module

C_ItemsModule::C_ItemsModule(int id, const char *name) : C_Module(id, name)
{
    for (I3D_frame *&prop : g_activityProps)
        prop = nullptr;
}

C_ItemsModule::~C_ItemsModule()
{
    FreePlacements();
}

void C_ItemsModule::ResetUids()
{
    m_lastUid = -1;
    m_freedUids.clear();
}

void C_ItemsModule::Init()
{
    ResetUids();
}

void C_ItemsModule::Done()
{
#ifndef HD2_DEDICATED   // no surface marks on the server
    SurfaceMarks_Free();
#endif
    for (C_ItemDef *&def : m_items) {
        delete def;
        def = nullptr;
    }
    // (The original's loop forgets only every other instance; the rest stay listed.)
    for (size_t i = 0; i < m_instances.size(); ++i) {
        m_instances[i] = m_instances.back();
        m_instances.pop_back();
    }
}

void C_ItemsModule::Tick(int timeDelta)
{
    if (IsServer())
        SurfaceMarks_Tick(timeDelta);
}

void C_ItemsModule::Update(int timeDelta)
{
    if (IsServer())
        return;
    g_clientMarksTime += timeDelta;
    if (g_clientMarksTime >= CLIENT_MARKS_STEP) {
#ifndef HD2_DEDICATED   // no surface marks on the server
        SurfaceMarks_Tick(CLIENT_MARKS_STEP);
#endif
        g_clientMarksTime -= CLIENT_MARKS_STEP;
    }
}

void C_ItemsModule::LoadMissionData(bool server)
{
    if (!IsOwnPass(server))
        return;
    EnsureItemsLoaded();
    ResetUids();
}

void C_ItemsModule::FreeMissionData(bool)
{
    for (int i = 0; i < m_placementCount; ++i)
        delete m_placements[i].item;
    m_respawningItems.clear();
}

void C_ItemsModule::StartMission(bool server)
{
    m_unknown7E5 = false;
    if (!IsOwnPass(server))
        return;
    EnsureItemsLoaded();
    m_missionActive = true;
    ResetUids();
    if (IsServer() && !LoadMissionItems())
        DeleteMissionItemsFile();
}

void C_ItemsModule::StopMission(bool server)
{
    if (!IsOwnPass(server))
        return;
    ResetUids();
    m_missionActive = false;
    ClearActorItems();
    for (I3D_frame *&prop : g_activityProps) {
        if (prop)
            prop->Release();
        prop = nullptr;
    }
}

void C_ItemsModule::RestartMission(bool server)
{
    m_missionActive = false;
    ClearActorItems();
    m_missionActive = true;
    ResetUids();
    if (IsServer() && server && !LoadMissionItems())
        DeleteMissionItemsFile();
}

bool C_ItemsModule::SaveGame(C_chunk &chunk)
{
    if (!chunk.Begin(CHUNK_ITEMS_MODULE))
        return false;
    if (!chunk.Begin(CHUNK_ITEMS_UIDS))
        return false;
    chunk.WriteChunk(CHUNK_ITEMS_LAST_UID, m_lastUid);
    chunk.Ascend();
#ifndef HD2_DEDICATED   // no surface marks on the server
    SurfaceMarks_Save(chunk);
#endif
    chunk.Ascend();
    return true;
}

bool C_ItemsModule::LoadGame(C_chunk &chunk)
{
    ResetUids();
    m_missionActive = true;
    uint16_t id;
    if (!chunk.Descend(id))
        return false;
    if (id == CHUNK_ITEMS_MODULE && chunk.Descend(id) && id == CHUNK_ITEMS_UIDS && chunk.Descend(id) &&
        id == CHUNK_ITEMS_LAST_UID) {
        m_lastUid = ReadInt(chunk);
        chunk.Ascend();
#ifndef HD2_DEDICATED   // no surface marks on the server
        SurfaceMarks_Load(chunk);
#endif
        chunk.Ascend();
        return true;
    }
    // (Only the innermost chunk entered is left.)
    chunk.Ascend();
    return false;
}

// ---------------------------------------------------------------------------
// definitions

void C_ItemsModule::EnsureItemsLoaded()
{
    if (m_itemsLoaded)
        return;
    m_itemsLoaded = true;
    LoadItemsTable();
}

C_ItemDef *C_ItemsModule::GetItemDef(int typeId) const
{
    return typeId >= 0 && typeId < MAX_ITEM_TYPES ? m_items[typeId] : nullptr;
}

void C_ItemsModule::LoadItemsTable()
{
    const C_fstr path("Tables\\items.sav");
    const int file = dtaOpen(path, 0);
    if (file == DTA_INVALID_HANDLE) {
        ShowMessage("KRITICKA chyba pri nahravani souboru items.sav", "I/O ERROR", 0);
        dtaClose(file);
        GetGame()->SetState(GAME_STATE_QUIT);
        return;
    }
    std::vector<uint8_t> data(ITEMS_TABLE_SIZE);
    if (dtaRead(file, data.data(), ITEMS_TABLE_SIZE) == -1) {
        ShowMessage("KRITICKA chyba pri nahravani souboru items.sav", "I/O ERROR", 0);
        dtaClose(file);
        GetGame()->SetState(GAME_STATE_QUIT);
        return;
    }
    dtaClose(file);

    const uint8_t *cursor = data.data();
    for (int id = 0; id < MAX_ITEM_TYPES; ++id) {
        int present;
        std::memcpy(&present, cursor, sizeof(int));
        if (!present) {
            m_items[id] = nullptr;
            cursor += sizeof(int);
            continue;
        }
        int type;
        std::memcpy(&type, cursor + sizeof(int), sizeof(int));
        C_ItemDef *def = nullptr;
        if (type == ITEM_AMMO)
            def = new C_AmmoItemDef(id);
        else if (type == ITEM_ANIMATED)
            def = new C_AnimatedItemDef(id);
        else if (type == ITEM_STATIC)
            def = new C_StaticItemDef(id);
        m_items[id] = def;
        if (!def) {
            // (An unknown record type only skips 8 bytes and desynchronises the rest.)
            cursor += ITEM_RECORD_HEADER;
            continue;
        }
        int size = ITEM_RECORD_STREAM;
        def->Load(&size, cursor + ITEM_RECORD_HEADER);
        cursor += ITEM_SLOT_SIZE;
    }
}

// ---------------------------------------------------------------------------
// instances

C_Item *C_ItemsModule::NewItem(int typeId) const
{
    const C_ItemDef *def = m_items[typeId];
    C_Item *item;
    if (def->GetRecordType() == ITEM_ANIMATED)
        item = new C_WeaponItem;                        // 0x007dd9c0
    else if (def->GetRecordType() == ITEM_AMMO)
        item = new C_AmmoItem;                          // 0x007dd860 (DS 0x00818a10)
    else if (def->GetComponentKind() != ICK_BACKPACK)
        item = new C_MedkitItem;                        // 0x007dd950 (DS 0x00818b50) (every other static item)
    else
        item = new C_ContainerItem;                     // 0x007dd8e0
    item->Init(typeId);
    return item;
}

void C_ItemsModule::AddInstance(C_Item *item)
{
    if (item)
        m_instances.push_back(item);
}

void C_ItemsModule::DestroyItem(C_Item *item)
{
    if (!item)
        return;
    for (size_t i = 0; i < m_instances.size(); ++i) {
        if (m_instances[i] == item) {
            m_instances[i] = m_instances.back();
            m_instances.pop_back();
            delete item;
            return;
        }
    }
}

void C_ItemsModule::AddRespawningItem(C_Item *item)
{
    m_respawningItems.push_back(item);
}

void C_ItemsModule::RemoveRespawningItem(C_Item *item)
{
    for (size_t i = 0; i < m_respawningItems.size(); ++i) {
        if (m_respawningItems[i] == item) {
            m_respawningItems[i] = m_respawningItems.back();
            m_respawningItems.pop_back();
            return;
        }
    }
}

int C_ItemsModule::NextUid()
{
    if (!m_freedUids.empty()) {
        LARGE_INTEGER frequency, counter;
        QueryPerformanceFrequency(&frequency);
        QueryPerformanceCounter(&counter);
        const uint32_t now = static_cast<uint32_t>(counter.QuadPart * 1000000 / frequency.QuadPart);
        if (now - m_freedUids.front().time > UID_REUSE_DELAY) {
            const int uid = m_freedUids.front().uid;
            m_freedUids.pop_front();
            return uid;
        }
    }
    return ++m_lastUid;
}

C_Item *C_ItemsModule::CreateItem(int typeId, int container, C_Actor *owner, int uid)
{
    if (!GetItemDef(typeId))
        return nullptr;
    C_Item *item = NewItem(typeId);
    item->SetUid(uid < 0 ? NextUid() : uid);
    C_Item *replaced = nullptr;
    if (MoveItem(item, &replaced, nullptr, 0, owner, container, -1, nullptr, true, false, false, false) < 1) {
        delete item;
        return nullptr;
    }
    if (replaced) {
        // (This moves the new item once more rather than the one it displaced, which
        // stays without a container.)
        if (MoveItem(item, &replaced, nullptr, 0, nullptr, 0, -1, nullptr, uid == -2, false, false, false) < 1 &&
            replaced)
            delete replaced;
    }
    AddInstance(item);
    return item;
}

C_Item *C_ItemsModule::GiveItem(int typeId, int unused, C_Actor *actor, int /*unused2*/)
{
    if (!IsLocalGame() || !actor || !GetItemDef(typeId) || unused != 0)
        return nullptr;
    const C_ItemDef *def = m_items[typeId];
    int container = ITEM_CONTAINER_HEAD;
    switch (def->GetComponentKind()) {
    case ICK_PROJECTILE_WEAPON:
    case ICK_FIREARM:
        if (actor->GetContainer(ITEM_CONTAINER_HANDS)->IsEmpty())
            container = ITEM_CONTAINER_HANDS;
        else if ((def->GetInventorySize() == 2 || def->GetInventorySize() == 3) &&
                 actor->GetContainer(ITEM_CONTAINER_LONG_GUN)->IsEmpty())
            container = ITEM_CONTAINER_LONG_GUN;
        else
            container = ITEM_CONTAINER_BELT;
        break;
    case ICK_UNIFORM:
        // (A uniform with the uniform slot taken goes to the head slot, and fails.)
        if (actor->GetContainer(ITEM_CONTAINER_UNIFORM)->IsEmpty())
            container = ITEM_CONTAINER_UNIFORM;
        break;
    case ICK_BACKPACK:
        if (actor->GetContainer(ITEM_CONTAINER_BACKPACK)->IsEmpty())
            container = ITEM_CONTAINER_BACKPACK;
        break;
    case ICK_HEADGEAR:
        break;
    default:
        if (C_Item *item = CreateItem(typeId, ITEM_CONTAINER_BELT, actor, -2))
            return item;
        container = ITEM_CONTAINER_BACKPACK_CONTENTS;
        break;
    }
    return CreateItem(typeId, container, actor, -2);
}

C_Item *C_ItemsModule::CreateFreeItem(int typeId)
{
    if (!GetItemDef(typeId))
        return nullptr;
    C_Item *item = NewItem(typeId);
    item->SetUid(-1);
    AddInstance(item);
    return item;
}

// ---------------------------------------------------------------------------
// moving items

void C_ItemsModule::NotifyMove(C_Item *item, C_Actor *fromHuman, C_ItemContainer *source, int fromContainer,
                               C_Actor *target, C_ItemContainer *dest, int toContainer, bool fromNew, bool notify)
{
    if (fromHuman && EquipmentOf(fromHuman) && notify)
        EquipmentOf(fromHuman)->OnItemRemoved(item, fromContainer, target, toContainer);
    if (target && EquipmentOf(target) && notify) {
        if (fromNew)
            EquipmentOf(target)->OnItemCreated(item, toContainer);
        else
            EquipmentOf(target)->OnItemAdded(item, toContainer, fromHuman, fromContainer, false);
    }
    if (IsClient() && target == ActivePlayer())
        GetGame()->GetInterface()->RefreshInventory();
    if (target)
        AsHuman(target)->SetInventoryChanged(true);
    if (fromHuman) {
        AsHuman(fromHuman)->SetInventoryChanged(true);
        if (fromContainer == ITEM_CONTAINER_HANDS) {
            if (source)
                source->SetLockTime(Now() + ITEM_BUSY_TIME);
            if (toContainer == ITEM_CONTAINER_LONG_GUN)
                dest->SetLockTime(Now() + ITEM_BUSY_TIME);
            AsHuman(fromHuman)->OnHandsChanged();
            if (IsClient() && ActivePlayer() == fromHuman && notify)
                Hud()->OnInventoryEvent(FPV_EVENT_CLEARED, 0);
        }
    }
    if (target && toContainer == ITEM_CONTAINER_HANDS) {
        dest->SetLockTime(Now() + ITEM_BUSY_TIME);
        AsHuman(target)->OnHandsChanged();
        C_HumanEquipment *equipment = EquipmentOf(target);
        if (IsClient() && ActivePlayer() == target && equipment && !(equipment->GetFlags() & EQUIPMENT_HANDS_PUT_AWAY) && notify)
            Hud()->OnInventoryEvent(FPV_EVENT_DRAW, 0);
    }
}

int C_ItemsModule::MoveItem(C_Item *item, C_Item **replaced, C_Actor *from, int fromContainer, C_Actor *to,
                            int toContainer, int index, C_Actor *carrier, bool notify, bool ignoreLocks,
                            bool keepWeights, bool testOnly)
{
    *replaced = nullptr;
    if (!item)
        return ITEM_RESULT_FAILED;
    if (fromContainer > ITEM_CONTAINER_BACKPACK_CONTENTS)
        return ITEM_RESULT_BAD_SOURCE;
    if (toContainer > ITEM_CONTAINER_BACKPACK_CONTENTS)
        return ITEM_RESULT_BAD_TARGET;
    // A uniform only leaves its slot for another uniform slot.
    if (fromContainer == ITEM_CONTAINER_UNIFORM && toContainer != ITEM_CONTAINER_UNIFORM)
        return ITEM_RESULT_BAD_TARGET;
    if (!carrier && from)
        carrier = from;
    I3D_frame *model = item->GetFrame(0);

    const bool fromNew = from == nullptr;
    C_Actor *fromHuman = nullptr;                       // the source, if it has the full set
    C_ItemContainer *source = nullptr;
    if (from) {
        if (!from->HasSingleContainer()) {
            fromHuman = from;
            source = from->GetContainer(fromContainer);
        } else {
            source = from->GetContainer(0);
        }
    }

    C_ItemDef *def = item->GetDef();
    const float weight = item->GetWeight();
    bool dropped = false;
    bool withContents = false;                          // a real backpack moves with its contents
    C_ItemContainer *dest = nullptr;
    C_Actor *receiver = nullptr;
    switch (toContainer) {
    case ITEM_CONTAINER_HEAD:                           // also the only container of vehicles
        if (!to) {
            withContents = def->IsBackpack();
            dropped = true;
            break;
        }
        dest = to->GetContainer(0);
        if (to != from && !CanCarry(to, weight, OccupantWeight(dest)))
            return ITEM_RESULT_ACTOR_OVERLOADED;
        if (!to->HasSingleContainer()) {
            if (dest->GetMaxWeight() < weight)
                return ITEM_RESULT_TOO_HEAVY;
            if (dest->GetMaxSpace() < item->GetSpace())
                return ITEM_RESULT_NO_SPACE;
            if (dest->GetMaxItemSize() < item->GetSize())
                return ITEM_RESULT_TOO_BIG;
            if (def->GetComponentKind() != ICK_HEADGEAR)
                return ITEM_RESULT_WRONG_KIND;
            receiver = to;
            if (fromNew && !dest->IsEmpty())
                return ITEM_RESULT_NO_SPACE;
        } else {
            if (dest->GetMaxWeight() < dest->GetTotalWeight() + weight)
                return ITEM_RESULT_TOO_HEAVY;
            if (dest->GetMaxSpace() < dest->GetTotalSpace() + item->GetSpace())
                return ITEM_RESULT_NO_SPACE;
            if (dest->GetMaxItemSize() < item->GetSize())
                return ITEM_RESULT_TOO_BIG;
        }
        break;
    case ITEM_CONTAINER_BELT:
        if (!to || to->HasSingleContainer())
            return ITEM_RESULT_BAD_TARGET;
        if (to != from && !CanCarry(to, weight, 0.0f))
            return ITEM_RESULT_ACTOR_OVERLOADED;
        dest = to->GetContainer(ITEM_CONTAINER_BELT);
        if (dest->GetMaxWeight() < dest->GetTotalWeight() + weight)
            return ITEM_RESULT_TOO_HEAVY;
        if (dest->GetMaxSpace() < dest->GetTotalSpace() + item->GetSpace())
            return ITEM_RESULT_NO_SPACE;
        receiver = to;
        if (def->GetBehaviour() != ITEM_BEHAVIOUR_MEDKIT && dest->GetMaxItemSize() < item->GetSize())
            return ITEM_RESULT_TOO_BIG;
        break;
    case ITEM_CONTAINER_LONG_GUN:
        if (!to || to->HasSingleContainer())
            return ITEM_RESULT_BAD_TARGET;
        dest = to->GetContainer(ITEM_CONTAINER_LONG_GUN);
        if (to != from && !CanCarry(to, weight, OccupantWeight(dest)))
            return ITEM_RESULT_ACTOR_OVERLOADED;
        if (dest->GetMaxWeight() < weight)
            return ITEM_RESULT_TOO_HEAVY;
        if (dest->GetMaxSpace() < item->GetSpace())
            return ITEM_RESULT_NO_SPACE;
        if (dest->GetMaxItemSize() < item->GetSize())
            return ITEM_RESULT_TOO_BIG;
        if ((def->GetComponentKind() != ICK_FIREARM && def->GetComponentKind() != ICK_PROJECTILE_WEAPON) ||
            (item->GetSize() != 3 && item->GetSize() != 2))
            return ITEM_RESULT_WRONG_KIND;
        receiver = to;
        if (fromNew && !dest->IsEmpty())
            return ITEM_RESULT_NO_SPACE;
        break;
    case ITEM_CONTAINER_HANDS:
        if (!to || to->HasSingleContainer())
            return ITEM_RESULT_BAD_TARGET;
        receiver = to;
        dest = to->GetContainer(ITEM_CONTAINER_HANDS);
        if (to != from && !CanCarry(to, weight, OccupantWeight(dest)))
            return ITEM_RESULT_ACTOR_OVERLOADED;
        if (item->GetKind() != ITEM_KIND_WEAPON)
            return ITEM_RESULT_WRONG_KIND;
        if (dest->GetMaxWeight() < weight)
            return ITEM_RESULT_TOO_HEAVY;
        if (dest->GetMaxSpace() < item->GetSpace())
            return ITEM_RESULT_NO_SPACE;
        if (dest->GetMaxItemSize() < item->GetSize())
            return ITEM_RESULT_TOO_BIG;
        if (fromNew && !dest->IsEmpty())
            return ITEM_RESULT_NO_SPACE;
        if (IsServer() && dest->GetItem() && Now() < dest->GetItem()->GetTime())
            return ITEM_RESULT_HANDS_BUSY;
        break;
    case ITEM_CONTAINER_UNIFORM:
        if (!to || to->HasSingleContainer())
            return ITEM_RESULT_BAD_TARGET;
        dest = to->GetContainer(ITEM_CONTAINER_UNIFORM);
        if (to != from && !CanCarry(to, weight, OccupantWeight(dest)))
            return ITEM_RESULT_ACTOR_OVERLOADED;
        if (dest->GetMaxWeight() < weight)
            return ITEM_RESULT_TOO_HEAVY;
        if (dest->GetMaxSpace() < item->GetSpace())
            return ITEM_RESULT_NO_SPACE;
        if (dest->GetMaxItemSize() < item->GetSize())
            return ITEM_RESULT_TOO_BIG;
        if (def->GetComponentKind() != ICK_UNIFORM)
            return ITEM_RESULT_WRONG_KIND;
        receiver = to;
        if (fromNew && !dest->IsEmpty())
            return ITEM_RESULT_NO_SPACE;
        break;
    case ITEM_CONTAINER_BACKPACK: {
        if (!to || to->HasSingleContainer())
            return ITEM_RESULT_BAD_TARGET;
        if (def->GetComponentKind() != ICK_BACKPACK)
            return ITEM_RESULT_WRONG_KIND;
        dest = to->GetContainer(ITEM_CONTAINER_BACKPACK);
        float given = 0.0f;
        if (C_Item *occupant = dest->GetItem()) {
            given = occupant->GetDef()->GetWeight();
            if (occupant->GetDef()->IsBackpack())
                given += ContentsWeight(occupant);
        }
        float taken = weight;
        withContents = def->IsBackpack();
        if (withContents)
            taken += ContentsWeight(item);
        if (to != from && !CanCarry(to, taken, given))
            return ITEM_RESULT_ACTOR_OVERLOADED;
        receiver = to;
        if (dest->GetMaxWeight() + 0.0001f < weight)
            return ITEM_RESULT_TOO_HEAVY;
        if (dest->GetMaxSpace() < item->GetSpace())
            return ITEM_RESULT_NO_SPACE;
        if (dest->GetMaxItemSize() != item->GetSize())
            return ITEM_RESULT_WRONG_KIND;
        if (fromNew && !dest->IsEmpty())
            return ITEM_RESULT_NO_SPACE;
        break;
    }
    case ITEM_CONTAINER_BACKPACK_CONTENTS: {
        if (!to || to->HasSingleContainer())
            return ITEM_RESULT_BAD_TARGET;
        if (to != from && !CanCarry(to, weight, 0.0f))
            return ITEM_RESULT_ACTOR_OVERLOADED;
        receiver = to;
        dest = to->GetContainer(ITEM_CONTAINER_BACKPACK_CONTENTS);
        if (!dest)
            return ITEM_RESULT_BAD_TARGET;
        if (dest->GetMaxWeight() + 0.0001f < dest->GetTotalWeight() + weight)
            return ITEM_RESULT_TOO_HEAVY;
        if (dest->GetMaxSpace() < dest->GetTotalSpace() + item->GetSpace())
            return ITEM_RESULT_NO_SPACE;
        // Ammo, mines and behaviour 11 items fit whatever their size.
        const int behaviour = def->GetBehaviour();
        if (def->GetRecordType() != ITEM_AMMO && behaviour != ITEM_BEHAVIOUR_STICKY_CHARGE && behaviour != ITEM_BEHAVIOUR_MINE &&
            behaviour != ITEM_BEHAVIOUR_MEDKIT && dest->GetMaxItemSize() < item->GetSize())
            return ITEM_RESULT_TOO_BIG;
        break;
    }
    default:
        break;
    }

    if (m_missionActive && IsServer()) {
        if (dest && dest->IsLocked() && !ignoreLocks)
            return ITEM_RESULT_TARGET_LOCKED;
        if (source && source->IsLocked() && !ignoreLocks)
            return ITEM_RESULT_SOURCE_LOCKED;
    }
    if (source && source == dest && index < 0)
        return ITEM_RESULT_SAME_PLACE;
    if (testOnly)
        return ITEM_RESULT_OK;

    int result = ITEM_RESULT_OK;
    if (dest) {
        result = dest->AddItem(item, replaced, index);
        if (result < 1)
            return result;
    }
    if (to)
        to->AddCarriedWeight(weight);
    if (from && !keepWeights)
        from->AddCarriedWeight(-weight);
    if (*replaced && !keepWeights) {
        to->AddCarriedWeight(-(*replaced)->GetWeight());
        to->AddCarriedWeight(-ContentsWeight(*replaced));
    }
    if (withContents) {
        C_ItemStore *contents = static_cast<C_ContainerItem *>(item)->GetStore();
        if (to) {
            to->SetContainer(ITEM_CONTAINER_BACKPACK_CONTENTS, contents);
            to->AddCarriedWeight(contents->GetTotalWeight());
        }
        if (from && !keepWeights)
            from->AddCarriedWeight(-contents->GetTotalWeight());
    }
    if (receiver) {
        item->SetOwner(receiver);
        item->SetContainerIndex(toContainer);
    }
    if (source)
        source->RemoveItem(item, -1);
    if (item->IsInWorld() && model) {
        item->SetInWorld(false);
        model->SetOn(false);
    }
    if (withContents && source && from && fromContainer == ITEM_CONTAINER_BACKPACK && !ignoreLocks)
        from->SetContainer(ITEM_CONTAINER_BACKPACK_CONTENTS, nullptr);
    if (m_missionActive)
        NotifyMove(item, fromHuman, source, fromContainer, receiver, dest, toContainer, fromNew, notify);
    if (dropped && IsClient() && carrier == ActivePlayer() && fromContainer == ITEM_CONTAINER_HANDS)
        Hud()->OnInventoryEvent(FPV_EVENT_CLEARED, 0);
    item->SetTime(Now() + ITEM_BUSY_TIME);
    return result;
}

int C_ItemsModule::PutItemAuto(C_Item *item, C_Actor *actor, int *container, bool notify, bool testOnly)
{
    C_Item *replaced = nullptr;
    auto tryPut = [&](int target) {
        const int result = MoveItem(item, &replaced, nullptr, 0, actor, target, -1, nullptr, notify, false, false, testOnly);
        if (result > 0)
            *container = target;
        return result;
    };
    const E_ItemComponentKind kind = item->GetDef()->GetComponentKind();
    const int size = item->GetSize();
    int result;
    if (kind == ICK_FIREARM || kind == ICK_PROJECTILE_WEAPON || kind == ICK_MELEE || kind == ICK_MINE) {
        if (actor->GetContainer(ITEM_CONTAINER_HANDS)->IsEmpty() && (result = tryPut(ITEM_CONTAINER_HANDS)) > 0)
            return result;
        if ((kind == ICK_FIREARM ||
             (kind == ICK_PROJECTILE_WEAPON && (size == 2 || size == 3) &&
              actor->GetContainer(ITEM_CONTAINER_LONG_GUN)->IsEmpty())) &&
            (result = tryPut(ITEM_CONTAINER_LONG_GUN)) > 0)
            return result;
    }
    if ((result = tryPut(ITEM_CONTAINER_BELT)) > 0)
        return result;
    return tryPut(ITEM_CONTAINER_BACKPACK_CONTENTS);
}

int C_ItemsModule::PutItem(C_Item *item, C_Actor *actor, int *container, bool notify)
{
    *container = ITEM_CONTAINER_DROPPED;
    if (!actor)
        return ITEM_RESULT_NO_ACTOR;
    if (actor->HasSingleContainer())
        return ITEM_RESULT_SINGLE_CONTAINER;
    if (!item)
        return ITEM_RESULT_NULL_ITEM;
    C_Item *replaced = nullptr;
    const E_ItemComponentKind kind = item->GetDef()->GetComponentKind();
    if (kind == ICK_BACKPACK) {
        // (Tried whether the backpack slot is free or not.)
        const int result = MoveItem(item, &replaced, nullptr, 0, actor, ITEM_CONTAINER_BACKPACK, -1, nullptr, notify,
                                    false, false, false);
        if (result > 0)
            *container = ITEM_CONTAINER_BACKPACK;
        return result;
    }
    if (kind == ICK_HEADGEAR && actor->GetContainer(ITEM_CONTAINER_HEAD)->IsEmpty()) {
        const int result = MoveItem(item, &replaced, nullptr, 0, actor, ITEM_CONTAINER_HEAD, -1, nullptr, notify,
                                    false, false, false);
        if (result > 0) {
            *container = ITEM_CONTAINER_HEAD;
            return result;
        }
    }
    const int result = PutItemAuto(item, actor, container, notify, false);
    return result > 0 ? result : ITEM_RESULT_NO_PLACE;
}

int C_ItemsModule::CanPutItem(C_Actor *actor, C_Item *item)
{
    if (!actor)
        return ITEM_RESULT_NO_ACTOR;
    if (actor->HasSingleContainer())
        return ITEM_RESULT_SINGLE_CONTAINER;
    C_Item *replaced = nullptr;
    const E_ItemComponentKind kind = item->GetDef()->GetComponentKind();
    if (kind == ICK_BACKPACK && actor->GetContainer(ITEM_CONTAINER_BACKPACK)->IsEmpty())
        return MoveItem(item, &replaced, nullptr, 0, actor, ITEM_CONTAINER_BACKPACK, -1, nullptr, true, false, false, true);
    if (kind == ICK_HEADGEAR && actor->GetContainer(ITEM_CONTAINER_HEAD)->IsEmpty()) {
        const int result =
            MoveItem(item, &replaced, nullptr, 0, actor, ITEM_CONTAINER_HEAD, -1, nullptr, true, false, false, true);
        if (result > 0)
            return result;
    }
    int container;
    return PutItemAuto(item, actor, &container, true, true);
}

int C_ItemsModule::TransferItemAuto(C_Item *item, C_Actor *from, int fromContainer, C_Actor *to, int *toContainer)
{
    *toContainer = ITEM_CONTAINER_DROPPED;
    if (!from || !to)
        return ITEM_RESULT_NO_ACTOR;
    C_Item *replaced = nullptr;
    int replacedContainer = 0;
    auto tryTransfer = [&](int target) {
        return TransferItem(item, from, fromContainer, to, target, &replaced, toContainer, &replacedContainer);
    };
    const E_ItemComponentKind kind = item->GetDef()->GetComponentKind();
    const int size = item->GetSize();
    int result;
    if (kind == ICK_BACKPACK) {
        if (to->GetContainer(ITEM_CONTAINER_BACKPACK)->IsEmpty())
            return tryTransfer(ITEM_CONTAINER_BACKPACK);
        return ITEM_RESULT_NO_PLACE;
    }
    if (kind == ICK_HEADGEAR && to->GetContainer(ITEM_CONTAINER_HEAD)->IsEmpty() &&
        (result = tryTransfer(ITEM_CONTAINER_HEAD)) > 0)
        return result;
    if (kind == ICK_FIREARM || kind == ICK_PROJECTILE_WEAPON) {
        if (to->GetContainer(ITEM_CONTAINER_HANDS)->IsEmpty() && (result = tryTransfer(ITEM_CONTAINER_HANDS)) > 0)
            return result;
        if ((size == 2 || size == 3) && to->GetContainer(ITEM_CONTAINER_LONG_GUN)->IsEmpty() &&
            (result = tryTransfer(ITEM_CONTAINER_LONG_GUN)) > 0)
            return result;
    }
    if ((result = tryTransfer(ITEM_CONTAINER_BELT)) > 0)
        return result;
    if ((result = tryTransfer(ITEM_CONTAINER_BACKPACK_CONTENTS)) > 0)
        return result;
    return ITEM_RESULT_NO_PLACE;
}

int C_ItemsModule::TransferItem(C_Item *item, C_Actor *from, int fromContainer, C_Actor *to, int toContainer,
                                C_Item **replacedOut, int *toContainerOut, int *replacedContainer)
{
    *replacedOut = nullptr;
    *toContainerOut = toContainer;
    *replacedContainer = fromContainer;
    C_Actor *fromHuman = from->GetType() == ACTOR_HUMAN ? from : nullptr;
    C_Actor *toHuman = to->GetType() == ACTOR_HUMAN ? to : nullptr;
    if (!item || (Now() < item->GetTime() && IsServer()))
        return ITEM_RESULT_ITEM_BUSY;
    if (IsSwapBlocked(fromHuman, fromContainer, toHuman, toContainer, item))
        return ITEM_RESULT_SWAP_BLOCKED;

    C_Item *replaced = nullptr;
    const int result = MoveItem(item, &replaced, from, fromContainer, to, toContainer, -1, nullptr, false, false, false, false);
    if (result < 1)
        return result;

    if (!replaced) {
        if (fromHuman) {
            EquipmentOf(fromHuman)->OnItemRemoved(item, fromContainer, toHuman, toContainer);
            if (IsClient() && fromHuman == ActivePlayer() && fromContainer == ITEM_CONTAINER_HANDS)
                Hud()->OnInventoryEvent(FPV_EVENT_PUT_AWAY, 0);
        }
        if (toHuman) {
            EquipmentOf(toHuman)->OnItemAdded(item, toContainer, fromHuman, fromContainer, false);
            if (IsClient() && toHuman == ActivePlayer() && toContainer == ITEM_CONTAINER_HANDS)
                Hud()->OnInventoryEvent(FPV_EVENT_DRAW, 0);
        }
        return ITEM_RESULT_OK;
    }

    // The displaced item goes back to the source: into the place the item left, any
    // place it fits, or onto the ground.
    *replacedOut = replaced;
    int replacedTo = fromContainer;
    C_Item *unused = nullptr;
    bool backToSource = true;
    if (MoveItem(replaced, &unused, to, toContainer, from, fromContainer, -1, nullptr, false, true, true, false) < 1) {
        const int put = PutItem(replaced, from, &replacedTo, false);
        *replacedContainer = replacedTo;
        if (put < 1) {
            MoveItem(replaced, &unused, nullptr, ITEM_CONTAINER_BACKPACK_CONTENTS, nullptr, 0, -1, from, false, false,
                     false, false);
            *replacedContainer = ITEM_CONTAINER_DROPPED;
            // (The original does not check for a source that is not a human.)
            if (fromHuman)
                EquipmentOf(fromHuman)->OnItemRemoved(item, fromContainer, nullptr, toContainer);
            backToSource = false;
        }
    }
    if (!backToSource || !fromHuman) {
        if (toHuman) {
            EquipmentOf(toHuman)->OnItemRemoved(replaced, toContainer, nullptr, fromContainer);
            EquipmentOf(toHuman)->OnItemAdded(item, toContainer, nullptr, fromContainer, false);
            if (IsClient() && toHuman == ActivePlayer() && toContainer == ITEM_CONTAINER_HANDS)
                Hud()->OnInventoryEvent(FPV_EVENT_DRAW, 0);
        }
        return ITEM_RESULT_OK;
    }

    // Both are humans that swapped two items.
    C_HumanEquipment *fromEquipment = EquipmentOf(fromHuman);
    C_HumanEquipment *toEquipment = EquipmentOf(toHuman);
    if (fromContainer == ITEM_CONTAINER_BELT ||
        (fromContainer == ITEM_CONTAINER_HANDS && C_HumanEquipment::GetHolsterSlot(item->GetTypeId()) == HOLSTER_SLOT_SIDEARM))
        fromEquipment->RemoveFromHolster(item->GetTypeId(), 1);
    else if (fromContainer == ITEM_CONTAINER_UNIFORM && IsServer())
        AsHuman(fromHuman)->OnUniformChanged(replaced->GetUid());
    if (toContainer == ITEM_CONTAINER_BELT ||
        (toContainer == ITEM_CONTAINER_HANDS && C_HumanEquipment::GetHolsterSlot(replaced->GetTypeId()) == HOLSTER_SLOT_SIDEARM))
        toEquipment->RemoveFromHolster(replaced->GetTypeId(), 1);
    else if (toContainer == ITEM_CONTAINER_UNIFORM && IsServer())
        AsHuman(toHuman)->OnUniformChanged(item->GetUid());

    if (fromContainer == ITEM_CONTAINER_HANDS && toContainer == ITEM_CONTAINER_HANDS) {
        fromEquipment->OnAnimationEvent(EQUIPMENT_EVENT_1);
        toEquipment->OnAnimationEvent(EQUIPMENT_EVENT_1);
        if (IsClient() && (fromHuman == ActivePlayer() || toHuman == ActivePlayer()))
            Hud()->OnInventoryEvent(FPV_EVENT_PUT_AWAY, FPV_EVENT_DRAW);
    }
    if ((fromContainer == ITEM_CONTAINER_HANDS) == (toContainer == ITEM_CONTAINER_HANDS)) {
        fromEquipment->OnItemAdded(replaced, replacedTo, toHuman, toContainer, AsHuman(toHuman)->IsDead());
        toEquipment->OnItemAdded(item, toContainer, fromHuman, fromContainer, AsHuman(fromHuman)->IsDead());
        return ITEM_RESULT_OK;
    }

    // One side is the hands.
    if (IsClient()) {
        if (fromContainer == ITEM_CONTAINER_HANDS) {
            if (fromHuman == ActivePlayer())
                Hud()->OnInventoryEvent(FPV_EVENT_PUT_AWAY, replacedTo == ITEM_CONTAINER_HANDS ? FPV_EVENT_DRAW : 0);
        } else if (replacedTo == ITEM_CONTAINER_HANDS && fromHuman == ActivePlayer()) {
            Hud()->OnInventoryEvent(FPV_EVENT_DRAW, 0);
        }
        if (toContainer == ITEM_CONTAINER_HANDS && toHuman == ActivePlayer()) {
            if (toHuman->GetContainer(ITEM_CONTAINER_HANDS)->GetItem())
                Hud()->OnInventoryEvent(FPV_EVENT_PUT_AWAY, FPV_EVENT_DRAW);
            else
                Hud()->OnInventoryEvent(FPV_EVENT_DRAW, 0);
        }
    }
    if (fromContainer == ITEM_CONTAINER_HANDS) {
        fromEquipment->OnItemAdded(replaced, replacedTo, toHuman, toContainer, true);
    } else {
        const auto onBody = [](int c) { return c == ITEM_CONTAINER_HEAD || c == ITEM_CONTAINER_LONG_GUN || c == ITEM_CONTAINER_BACKPACK; };
        if (fromContainer != replacedTo && !onBody(replacedTo) && !onBody(fromContainer))
            toEquipment->OnItemRemoved(replaced, toContainer, nullptr, replacedTo);
        fromEquipment->OnItemAdded(replaced, replacedTo, toHuman, toContainer, false);
        if (fromContainer == ITEM_CONTAINER_BELT)
            toEquipment->Unknown792310();
    }
    if (toContainer != ITEM_CONTAINER_HANDS) {
        toEquipment->OnItemAdded(item, toContainer, fromHuman, fromContainer, false);
    } else if (fromContainer == replacedTo) {
        toEquipment->OnItemAdded(item, ITEM_CONTAINER_HANDS, fromHuman, fromContainer, true);
    } else if (replacedTo == ITEM_CONTAINER_HEAD || replacedTo == ITEM_CONTAINER_LONG_GUN ||
               replacedTo == ITEM_CONTAINER_BACKPACK) {
        toEquipment->OnItemAdded(item, ITEM_CONTAINER_HANDS, fromHuman, replacedTo, true);
    } else {
        if (replacedTo == ITEM_CONTAINER_BELT || replacedTo == ITEM_CONTAINER_BACKPACK_CONTENTS) {
            toEquipment->OnAnimationEvent(EQUIPMENT_EVENT_1);
            toEquipment->OnAnimationEvent(EQUIPMENT_EVENT_GIVE);
        }
        toEquipment->OnItemAdded(item, ITEM_CONTAINER_HANDS, fromHuman, fromContainer, true);
    }
    return ITEM_RESULT_OK;
}

// ---------------------------------------------------------------------------
// checks by item type

int C_ItemsModule::CanAddItemToInventory(C_Actor *actor, int itemId)
{
    if (!actor)
        return ITEM_RESULT_NO_ACTOR;
    if (actor->HasSingleContainer())
        return ITEM_RESULT_SINGLE_CONTAINER;
    // (The original reads through a null definition for unknown ids.)
    const C_ItemDef *def = GetItemDef(itemId);
    const int size = def->GetInventorySize();
    const E_ItemComponentKind kind = def->GetComponentKind();
    int result;
    if (kind == ICK_BACKPACK) {
        if (actor->GetContainer(ITEM_CONTAINER_BACKPACK)->IsEmpty())
            return CanPutItemInSlot(actor, ITEM_CONTAINER_BACKPACK, itemId);
        return ITEM_RESULT_NO_PLACE;
    }
    if (kind == ICK_HEADGEAR && actor->GetContainer(ITEM_CONTAINER_HEAD)->IsEmpty() &&
        (result = CanPutItemInSlot(actor, ITEM_CONTAINER_HEAD, itemId)) > 0)
        return result;
    if (kind == ICK_FIREARM || kind == ICK_PROJECTILE_WEAPON || kind == ICK_MELEE || kind == ICK_MINE) {
        if (actor->GetContainer(ITEM_CONTAINER_HANDS)->IsEmpty() &&
            (result = CanPutItemInSlot(actor, ITEM_CONTAINER_HANDS, itemId)) > 0)
            return result;
        if ((kind == ICK_FIREARM ||
             (kind == ICK_PROJECTILE_WEAPON && (size == 2 || size == 3) &&
              actor->GetContainer(ITEM_CONTAINER_LONG_GUN)->IsEmpty())) &&
            (result = CanPutItemInSlot(actor, ITEM_CONTAINER_LONG_GUN, itemId)) > 0)
            return result;
    }
    if ((result = CanPutItemInSlot(actor, ITEM_CONTAINER_BELT, itemId)) > 0)
        return result;
    return CanPutItemInSlot(actor, ITEM_CONTAINER_BACKPACK_CONTENTS, itemId);
}

int C_ItemsModule::CanPutItemInSlot(C_Actor *actor, int container, int itemId)
{
    // (The original reads through a null definition for unknown ids.)
    C_ItemDef *def = GetItemDef(itemId);
    const float weight = def->GetWeight();
    const float space = SpaceOf(def);
    const int size = def->GetInventorySize();
    C_ItemContainer *dest;
    switch (container) {
    case ITEM_CONTAINER_HEAD:
        if (!actor)
            return ITEM_RESULT_NO_HEAD;
        dest = actor->GetContainer(0);
        if (!actor->HasSingleContainer()) {
            if (dest->GetMaxWeight() < weight)
                return ITEM_RESULT_TOO_HEAVY;
            if (dest->GetMaxSpace() < space)
                return ITEM_RESULT_NO_SPACE;
            if (dest->GetMaxItemSize() < size)
                return ITEM_RESULT_TOO_BIG;
            if (def->GetComponentKind() != ICK_HEADGEAR)
                return ITEM_RESULT_WRONG_KIND;
            return dest->IsEmpty() ? ITEM_RESULT_OK : ITEM_RESULT_NO_SPACE;
        }
        if (dest->GetMaxWeight() < dest->GetTotalWeight() + weight)
            return ITEM_RESULT_TOO_HEAVY;
        if (dest->GetMaxSpace() < dest->GetTotalSpace() + space)
            return ITEM_RESULT_NO_SPACE;
        break;
    case ITEM_CONTAINER_BELT:
        if (!actor || actor->HasSingleContainer())
            return ITEM_RESULT_BAD_TARGET;
        dest = actor->GetContainer(ITEM_CONTAINER_BELT);
        if (dest->GetMaxWeight() < dest->GetTotalWeight() + weight)
            return ITEM_RESULT_TOO_HEAVY;
        if (dest->GetMaxSpace() < dest->GetTotalSpace() + space)
            return ITEM_RESULT_NO_SPACE;
        if (def->GetBehaviour() == ITEM_BEHAVIOUR_MEDKIT)
            return ITEM_RESULT_OK;
        break;
    case ITEM_CONTAINER_LONG_GUN:
        if (!actor || actor->HasSingleContainer())
            return ITEM_RESULT_BAD_TARGET;
        dest = actor->GetContainer(ITEM_CONTAINER_LONG_GUN);
        if (dest->GetMaxWeight() < weight)
            return ITEM_RESULT_TOO_HEAVY;
        if (dest->GetMaxSpace() < space)
            return ITEM_RESULT_NO_SPACE;
        if (dest->GetMaxItemSize() < size)
            return ITEM_RESULT_TOO_BIG;
        if (def->GetComponentKind() != ICK_FIREARM && def->GetComponentKind() != ICK_PROJECTILE_WEAPON)
            return ITEM_RESULT_WRONG_KIND;
        if (size != 3 && size != 2)
            return ITEM_RESULT_WRONG_KIND;
        return dest->IsEmpty() ? ITEM_RESULT_OK : ITEM_RESULT_NO_SPACE;
    case ITEM_CONTAINER_HANDS:
        if (!actor || actor->HasSingleContainer())
            return ITEM_RESULT_BAD_TARGET;
        dest = actor->GetContainer(ITEM_CONTAINER_HANDS);
        if (def->GetBehaviour() > 12)
            return ITEM_RESULT_WRONG_KIND;
        if (dest->GetMaxWeight() < weight)
            return ITEM_RESULT_TOO_HEAVY;
        if (dest->GetMaxSpace() < space)
            return ITEM_RESULT_NO_SPACE;
        if (dest->GetMaxItemSize() < size)
            return ITEM_RESULT_TOO_BIG;
        if (!dest->IsEmpty())
            return ITEM_RESULT_NO_SPACE;
        if (IsServer() && dest->GetItem() && Now() < dest->GetItem()->GetTime())
            return ITEM_RESULT_HANDS_BUSY;
        return ITEM_RESULT_OK;
    case ITEM_CONTAINER_UNIFORM:
        if (!actor || actor->HasSingleContainer())
            return ITEM_RESULT_BAD_TARGET;
        dest = actor->GetContainer(ITEM_CONTAINER_UNIFORM);
        if (dest->GetMaxWeight() < weight)
            return ITEM_RESULT_TOO_HEAVY;
        if (dest->GetMaxSpace() < space)
            return ITEM_RESULT_NO_SPACE;
        if (dest->GetMaxItemSize() < size)
            return ITEM_RESULT_TOO_BIG;
        if (def->GetComponentKind() != ICK_UNIFORM)
            return ITEM_RESULT_WRONG_KIND;
        return dest->IsEmpty() ? ITEM_RESULT_OK : ITEM_RESULT_NO_SPACE;
    case ITEM_CONTAINER_BACKPACK:
        if (!actor || actor->HasSingleContainer())
            return ITEM_RESULT_BAD_TARGET;
        if (def->GetComponentKind() != ICK_BACKPACK)
            return ITEM_RESULT_WRONG_KIND;
        dest = actor->GetContainer(ITEM_CONTAINER_BACKPACK);
        if (dest->GetMaxWeight() + 0.0001f < weight)
            return ITEM_RESULT_TOO_HEAVY;
        if (dest->GetMaxSpace() < space)
            return ITEM_RESULT_NO_SPACE;
        if (dest->GetMaxItemSize() != size)
            return ITEM_RESULT_WRONG_KIND;
        return dest->IsEmpty() ? ITEM_RESULT_OK : ITEM_RESULT_NO_SPACE;
    case ITEM_CONTAINER_BACKPACK_CONTENTS: {
        if (!actor || actor->HasSingleContainer())
            return ITEM_RESULT_BAD_TARGET;
        dest = actor->GetContainer(ITEM_CONTAINER_BACKPACK_CONTENTS);
        if (!dest)
            return ITEM_RESULT_BAD_TARGET;
        if (dest->GetMaxWeight() + 0.0001f < dest->GetTotalWeight() + weight)
            return ITEM_RESULT_TOO_HEAVY;
        if (dest->GetMaxSpace() < dest->GetTotalSpace() + space)
            return ITEM_RESULT_NO_SPACE;
        const int behaviour = def->GetBehaviour();
        if (def->GetRecordType() == ITEM_AMMO || behaviour == ITEM_BEHAVIOUR_STICKY_CHARGE || behaviour == ITEM_BEHAVIOUR_MINE ||
            behaviour == ITEM_BEHAVIOUR_MEDKIT)
            return ITEM_RESULT_OK;
        break;
    }
    default:
        return ITEM_RESULT_OK;
    }
    return dest->GetMaxItemSize() < size ? ITEM_RESULT_TOO_BIG : ITEM_RESULT_OK;
}

bool IsSwapBlocked(C_Actor *from, int fromContainer, C_Actor *to, int toContainer, C_Item *item)   // 0x007daa40
{
    if (toContainer == ITEM_CONTAINER_BELT || toContainer == ITEM_CONTAINER_BACKPACK_CONTENTS)
        return false;
    C_ItemContainer *target = to->GetContainer(toContainer);
    C_Item *occupant = target->GetItem();
    if (!occupant)
        return false;
    const uint32_t fits = occupant->GetDef()->GetAllowedContainers();
    if (IsServer() && (target->IsLocked() || Now() < occupant->GetTime()))
        return true;

    const bool sameActor = from == to;
    const int occupantBehaviour = occupant->GetDef()->GetBehaviour();
    // Does the occupant fit into container `c` of `from`? `list`: the container holds
    // several items (their totals count).
    auto fitsIn = [&](int c, bool list, bool checkSize) {
        C_ItemContainer *place = from->GetContainer(c);
        if (!place)
            return false;
        float weight = occupant->GetWeight();
        float space = occupant->GetSpace();
        float carried = sameActor ? 0.0f : weight - (item ? item->GetWeight() : 0.0f);
        const bool itemStays = !item || fromContainer != c;
        if (!itemStays) {
            weight -= item->GetWeight();
            space -= item->GetSpace();
        }
        const float totalWeight = list ? place->GetTotalWeight() : 0.0f;
        const float totalSpace = list ? place->GetTotalSpace() : 0.0f;
        if (totalWeight + weight > place->GetMaxWeight() || totalSpace + space > place->GetMaxSpace())
            return false;
        if (checkSize && place->GetMaxItemSize() < occupant->GetSize())
            return false;
        if (from->GetCarriedWeight() + carried > static_cast<float>(from->GetHitPoints()))
            return false;
        if (list)
            return place->GetUsedSlotCount() < place->GetSlotCount() || !itemStays;
        return place->IsEmpty() || !itemStays;
    };

    if ((fits & ITEM_FITS_BELT) && fitsIn(ITEM_CONTAINER_BELT, true, occupantBehaviour != ITEM_BEHAVIOUR_MEDKIT))
        return false;
    if ((fits & ITEM_FITS_BACKPACK_CONTENTS) &&
        fitsIn(ITEM_CONTAINER_BACKPACK_CONTENTS, true,
               occupantBehaviour != ITEM_BEHAVIOUR_MEDKIT && occupantBehaviour != ITEM_BEHAVIOUR_MINE &&
                   occupantBehaviour != ITEM_BEHAVIOUR_STICKY_CHARGE && occupant->GetDef()->GetRecordType() != ITEM_AMMO))
        return false;
    if ((fits & ITEM_FITS_LONG_GUN) && fitsIn(ITEM_CONTAINER_LONG_GUN, false, true))
        return false;
    if ((fits & ITEM_FITS_HANDS) && fitsIn(ITEM_CONTAINER_HANDS, false, true))
        return false;
    if ((fits & ITEM_FITS_UNIFORM) && fitsIn(ITEM_CONTAINER_UNIFORM, false, true))
        return false;
    if ((fits & ITEM_FITS_HEAD) && fitsIn(ITEM_CONTAINER_HEAD, false, true))
        return false;
    if (!(fits & ITEM_FITS_BACKPACK))
        return true;

    // A backpack counts with its contents.
    C_ItemContainer *place = from->GetContainer(ITEM_CONTAINER_BACKPACK);
    float weight = occupant->GetWeight() + ContentsWeight(occupant);
    float space = occupant->GetSpace();
    float carried = 0.0f;
    if (!sameActor)
        carried = weight - (item ? item->GetWeight() + ContentsWeight(item) : 0.0f);
    const bool itemStays = !item || fromContainer != ITEM_CONTAINER_BACKPACK;
    if (!itemStays) {
        weight -= item->GetWeight() + ContentsWeight(item);
        space -= item->GetSpace();
    }
    if (weight > place->GetMaxWeight() || space > place->GetMaxSpace() || occupant->GetSize() > place->GetMaxItemSize())
        return true;
    if (from->GetCarriedWeight() + carried > static_cast<float>(from->GetHitPoints()))
        return true;
    return !(place->IsEmpty() || !itemStays);
}

// ---------------------------------------------------------------------------
// dropping

void C_ItemsModule::RemoveFromContainer(C_Item *item, C_Actor *owner, int container)
{
    item->SetOwner(nullptr);
    if (IsClient() && m_missionActive) {
        if (!owner)
            return;
        if (owner == ActivePlayer())
            GetGame()->GetInterface()->RefreshInventory();
    }
    if (!owner)
        return;
    if (owner->GetType() == ACTOR_HUMAN)
        AsHuman(owner)->SetInventoryChanged(true);
    owner->AddCarriedWeight(-item->GetWeight());
    owner->AddCarriedWeight(-ContentsWeight(item));
    if (!owner->HasSingleContainer()) {
        if (container <= ITEM_CONTAINER_BACKPACK_CONTENTS) {
            owner->GetContainer(container)->RemoveItem(item, -1);
            if (container == ITEM_CONTAINER_BACKPACK && item->GetDef()->GetBehaviour() == ITEM_BEHAVIOUR_BACKPACK)
                owner->SetContainer(ITEM_CONTAINER_BACKPACK_CONTENTS, nullptr);
        }
    } else if (container == 0) {
        owner->GetContainer(0)->RemoveItem(item, -1);
    }
}

void C_ItemsModule::DropItem(C_Item *item, C_Actor *owner, int container)
{
    C_Actor *human = owner && owner->GetType() == ACTOR_HUMAN ? owner : nullptr;
    I3D_frame *model = item->GetFrame(0);
    RemoveFromContainer(item, owner, container);
    if (!human || !m_missionActive)
        return;
    C_HumanEquipment *equipment = EquipmentOf(human);
    // Where the model is put down (C_HumanEquipment::GetDropPosition places).
    int place = 3;
    switch (container) {
    case ITEM_CONTAINER_HEAD:
        equipment->OnItemRemoved(item, ITEM_CONTAINER_HEAD, nullptr, 0);
        place = 0;
        break;
    case ITEM_CONTAINER_BELT:
        equipment->RemoveFromHolster(item->GetTypeId(), 1);
        break;
    case ITEM_CONTAINER_LONG_GUN:
        equipment->OnItemRemoved(item, ITEM_CONTAINER_LONG_GUN, nullptr, 0);
        place = 2;
        break;
    case ITEM_CONTAINER_HANDS:
        if (IsClient() && human == ActivePlayer())
            Hud()->OnInventoryEvent(FPV_EVENT_CLEARED, 0);
        equipment->OnItemRemoved(item, ITEM_CONTAINER_HANDS, nullptr, 0);
        break;
    case ITEM_CONTAINER_BACKPACK:
        equipment->OnItemRemoved(item, ITEM_CONTAINER_BACKPACK, nullptr, 0);
        place = 1;
        break;
    default:
        break;
    }
    if (model) {
        item->SetInWorld(true);
        model->LinkTo(nullptr, 0);
        model->SetOn(true);
        S_vector pos = equipment->GetDropPosition(place);
        // Hands and backpack contents go down a little behind the human (crouching: higher).
        if (container == ITEM_CONTAINER_HANDS && AsHuman(human)->GetStance() == 2)
            pos = pos - human->GetDirection() * 0.25f + S_vector(0.0f, 0.45f, 0.0f);
        else if (container == ITEM_CONTAINER_BACKPACK)
            pos = pos - human->GetDirection() * 0.25f;
        model->SetPos(pos);
        model->Update();
        GetGame()->GetScene()->UpdateFrameSector(model, model->GetWorldPos());
    }
    if (container == ITEM_CONTAINER_HANDS)
        equipment->ResetHands();
}

// ---------------------------------------------------------------------------
// actors' items

void ClearActorContainers(C_Actor *actor)
{
    const int last = actor->HasSingleContainer() ? 0 : ITEM_CONTAINER_BACKPACK;
    if (!actor->HasSingleContainer())
        actor->SetContainer(ITEM_CONTAINER_BACKPACK_CONTENTS, nullptr);
    for (int i = 0; i <= last; ++i) {
        if (C_ItemContainer *container = actor->GetContainer(i)) {
            container->Clear();
            container->SetLockTime(0);
        }
    }
    actor->SetCarriedWeight(0.0f);
}

void C_ItemsModule::ClearActorItems()
{
    for (int i = 0; i < g_actorsModule.GetActorCount(); ++i) {
        if (C_Actor *actor = g_actorsModule.GetActor(i))
            ClearActorContainers(actor);
    }
    FreePlacements();
    m_respawningItems.clear();
}

void C_ItemsModule::FreePlacements()
{
    _aligned_free(m_placements);
    m_placements = nullptr;
    m_placementCount = 0;
    m_placementCapacity = 0;
}

bool C_ItemsModule::SaveItem(C_Item *item, int container, int slot, int index, C_chunk &chunk)
{
    if (!chunk.Begin(CHUNK_ITEM_RECORD))
        return false;
    chunk.WriteChunk(CHUNK_ITEM_RECORD_TYPE, item->GetTypeId());
    chunk.WriteChunk(CHUNK_ITEM_RECORD_CONTAINER, container);
    chunk.WriteChunk(CHUNK_ITEM_RECORD_SLOT, slot);
    chunk.WriteChunk(CHUNK_ITEM_RECORD_INDEX, index);
    chunk.Begin(CHUNK_ITEM_RECORD_STATE);
    item->SaveGame(chunk);
    chunk.Ascend();
    chunk.Begin(CHUNK_ITEM_RECORD_CONTENTS);
    if (item->GetKind() == ITEM_KIND_CONTAINER) {
        C_ItemStore *store = static_cast<C_ContainerItem *>(item)->GetStore();
        const std::vector<C_Item *> *slots = store->GetSlots();
        for (int s = 0; s < store->GetSlotCount(); ++s) {
            for (size_t i = 0; i < slots[s].size(); ++i) {
                if (slots[s][i])
                    SaveItem(slots[s][i], 0, s, static_cast<int>(i), chunk);
            }
        }
    }
    chunk.Ascend();
    chunk.Ascend();
    return false;
}

C_Item *C_ItemsModule::LoadItem(int *container, int *slot, int *index, C_chunk &chunk, C_Actor *owner)
{
    uint16_t id;
    if (!chunk.Descend(id))
        return nullptr;
    if (id != CHUNK_ITEM_RECORD) {
        chunk.Ascend();
        return nullptr;
    }
    if (!chunk.Descend(id) || id != CHUNK_ITEM_RECORD_TYPE) {
        chunk.Ascend();
        chunk.Ascend();
        return nullptr;
    }
    const int typeId = ReadInt(chunk);
    if (!chunk.Descend(id) || id != CHUNK_ITEM_RECORD_CONTAINER) {
        chunk.Ascend();
        chunk.Ascend();
        return nullptr;
    }
    *container = ReadInt(chunk);
    // Failures leave the record chunk; an unexpected sub-chunk is left as well.
    if (!chunk.Descend(id)) {
        chunk.Ascend();
        return nullptr;
    }
    if (id != CHUNK_ITEM_RECORD_SLOT) {
        chunk.Ascend();
        chunk.Ascend();
        return nullptr;
    }
    *slot = ReadInt(chunk);
    if (!chunk.Descend(id)) {
        chunk.Ascend();
        return nullptr;
    }
    if (id != CHUNK_ITEM_RECORD_INDEX) {
        chunk.Ascend();
        chunk.Ascend();
        return nullptr;
    }
    *index = ReadInt(chunk);
    if (!GetItemDef(typeId)) {
        chunk.Ascend();
        return nullptr;
    }
    C_Item *item = NewItem(typeId);
    // (The original leaks the item when the chunks that follow are not as expected.)
    if (chunk.Descend(id) && id != CHUNK_ITEM_RECORD_STATE) {
        chunk.Ascend();
        chunk.Ascend();
        return nullptr;
    }
    item->LoadGame(chunk);
    chunk.Ascend();
    AddInstance(item);
    if (!chunk.Descend(id))
        return nullptr;
    if (id != CHUNK_ITEM_RECORD_CONTENTS) {
        chunk.Ascend();
        chunk.Ascend();
        return nullptr;
    }
    int c, s, i;
    while (chunk.RemainingSize() > 0) {
        C_Item *content = LoadItem(&c, &s, &i, chunk, owner);
        if (!content)
            break;
        if (item->GetKind() == ITEM_KIND_CONTAINER) {
            C_Item *replaced = nullptr;
            static_cast<C_ContainerItem *>(item)->GetStore()->AddItem(content, &replaced, -1);
            if (owner)
                owner->AddCarriedWeight(content->GetWeight());
        }
    }
    chunk.Ascend();
    chunk.Ascend();
    return item;
}

bool SaveActorItems(C_Actor *actor, C_chunk &chunk)
{
    if (!chunk.Begin(CHUNK_ACTOR_ITEMS))
        return false;
    // (The backpack contents are saved with the backpack.)
    const int last = actor->HasSingleContainer() ? 0 : ITEM_CONTAINER_BACKPACK;
    for (int c = 0; c <= last; ++c) {
        C_ItemContainer *container = actor->GetContainer(c);
        if (!container)
            continue;
        const int slotCount = container->GetSlotCount();
        if (slotCount == 1) {
            if (C_Item *item = container->GetItem())
                g_itemsModule.SaveItem(item, c, 0, 0, chunk);
            continue;
        }
        const std::vector<C_Item *> *slots = container->GetSlots();
        for (int s = 0; s < slotCount; ++s) {
            const int count = slots ? static_cast<int>(slots[s].size()) : 0;
            for (int i = 0; i < count; ++i) {
                if (C_Item *item = slots[s][i])
                    g_itemsModule.SaveItem(item, c, s, i, chunk);
            }
        }
    }
    chunk.Ascend();
    return true;
}

bool LoadActorItems(C_Actor *actor, C_chunk &chunk)
{
    ClearActorContainers(actor);
    uint16_t id;
    if (!chunk.Descend(id))
        return false;
    if (id != CHUNK_ACTOR_ITEMS) {
        chunk.Ascend();
        return false;
    }
    while (chunk.RemainingSize() > 0) {
        int container = 0, slot = 0, index = 0;
        C_Item *item = g_itemsModule.LoadItem(&container, &slot, &index, chunk, actor);
        if (!item)
            break;
        C_ItemContainer *place = actor->GetContainer(container);
        C_Item *replaced = nullptr;
        place->AddItem(item, &replaced, -1);
        actor->AddCarriedWeight(item->GetWeight());
        if (place->GetSlotCount() == 1 && container == ITEM_CONTAINER_BACKPACK && item->GetDef()->IsBackpack())
            actor->SetContainer(ITEM_CONTAINER_BACKPACK_CONTENTS, static_cast<C_ContainerItem *>(item)->GetStore());
    }
    chunk.Ascend();
    return true;
}

// ---------------------------------------------------------------------------
// Missions\<mission>\items.dat

void DeleteMissionItemsFile()
{
    dtaDelete(MissionItemsPath().c_str());
}

bool SaveMissionItems()
{
    const int file = dtaOpenWrite(MissionItemsPath().c_str(), 0x10000);
    if (file == DTA_INVALID_HANDLE)
        return false;
    const int version = MISSION_ITEMS_VERSION;
    if (dtaWrite(file, &version, 4) != 4) {
        dtaClose(file);
        return false;
    }
    for (int a = 0; a < g_actorsModule.GetActorCount(); ++a) {
        C_Actor *actor = g_actorsModule.GetActor(a);
        if (!actor)
            continue;
        const int last = actor->HasSingleContainer() ? 0 : ITEM_CONTAINER_BACKPACK_CONTENTS;
        for (int c = 0; c <= last; ++c) {
            C_ItemContainer *container = actor->GetContainer(c);
            if (!container)
                continue;
            const int slotCount = container->GetSlotCount();
            const std::vector<C_Item *> *slots = slotCount == 1 ? nullptr : container->GetSlots();
            for (int s = 0; s < slotCount; ++s) {
                const int count = slotCount == 1 ? 1 : (slots ? static_cast<int>(slots[s].size()) : 0);
                for (int i = 0; i < count; ++i) {
                    C_Item *item = slotCount == 1 ? container->GetItem() : slots[s][i];
                    if (!item)
                        continue;
                    const char *name = actor->GetFrame()->GetName();
                    const int nameLength = static_cast<int>(std::strlen(name)) + 1;
                    // (One byte more than the name and its terminator is written.)
                    const int record[6] = {c, item->GetTypeId(), item->GetUid(), 0, item->IsArmed() ? 1 : 0, 0};
                    if (dtaWrite(file, &nameLength, 4) != 4 || dtaWrite(file, name, nameLength + 1) != nameLength + 1 ||
                        dtaWrite(file, record, sizeof(record)) != sizeof(record)) {
                        dtaClose(file);
                        return false;
                    }
                }
            }
        }
    }
    return dtaClose(file);
}

void C_ItemsModule::AddPlacement(const S_ItemPlacement &placement)
{
    if (m_placementCount >= m_placementCapacity) {
        m_placementCapacity += m_placementGrowBy;
        auto *grown = static_cast<S_ItemPlacement *>(_aligned_malloc(m_placementCapacity * sizeof(S_ItemPlacement), 16));
        for (int i = 0; i < m_placementCount; ++i)
            grown[i] = m_placements[i];
        _aligned_free(m_placements);
        m_placements = grown;
    }
    m_placements[m_placementCount++] = placement;
}

bool C_ItemsModule::LoadMissionItems()
{
    const int file = dtaOpen(MissionItemsPath().c_str(), 0);
    if (file == DTA_INVALID_HANDLE)
        return true;
    int version = -1;
    if (dtaRead(file, &version, 4) != 4) {
        dtaClose(file);
        return false;
    }
    if (version != MISSION_ITEMS_VERSION)
        ShowMessage("Stara verze ulozenych itemu (items.dat)", "I/O ERROR", 0);

    int nameLength;
    while (dtaRead(file, &nameLength, 4) == 4) {
        // (A name longer than the buffer overflows it in the original.)
        char name[128] = {};
        int container, typeId, unused, placed;
        if (nameLength + 1 > static_cast<int>(sizeof(name)) || dtaRead(file, name, nameLength + 1) != nameLength + 1 ||
            dtaRead(file, &container, 4) != 4 || dtaRead(file, &typeId, 4) != 4 || dtaRead(file, &unused, 4) != 4) {
            dtaClose(file);
            return false;
        }
        if (dtaRead(file, &placed, 4) != 4) {
            dtaClose(file);
            return false;
        }
        S_ItemPlacement placement = {};
        if (placed) {
            // Version 14 and older store the position without its w.
            const int posSize = version < 0xF ? 12 : 16;
            if (dtaRead(file, &placement.pos, posSize) != posSize ||
                dtaRead(file, &placement.rot, sizeof(S_quat)) != sizeof(S_quat)) {
                dtaClose(file);
                return false;
            }
        }
        int armed = 0, empty = 0;
        if (version > 0xD && dtaRead(file, &armed, 4) != 4)
            armed = 0;
        if (version > 0xF && dtaRead(file, &empty, 4) != 4)
            empty = 0;

        const C_ItemDef *def = GetItemDef(typeId);
        if (!def)
            continue;   // (the original builds an error message here but never shows it)
        C_Item *item = NewItem(typeId);
        if (def->GetRecordType() == ITEM_ANIMATED && empty && item->GetMaxAmount() > 0.0f)
            item->Use(item->GetAmount());
        item->SetUid(NextUid());
        item->SetArmed(armed != 0);

        if (placed) {
            if (m_missionActive) {
                item->SetInWorld(true);
                placement.item = item;
                AddPlacement(placement);
            }
            AddInstance(item);
            continue;
        }
        I3D_frame *frame = GetGame()->GetScene()->FindFrame(name, FRAME_MASK_USER);
        C_Actor *actor = GetFrameActor(frame);
        if (!actor) {
            delete item;
            continue;
        }
        // Put in without telling the equipment / HUD.
        const bool active = m_missionActive;
        m_missionActive = false;
        C_Item *replaced = nullptr;
        const int result = MoveItem(item, &replaced, nullptr, 0, actor, container, -1, nullptr, true, false, false, false);
        m_missionActive = active;
        if (container != ITEM_CONTAINER_HANDS)
            item->SetTime(0);
        if (result < 1) {
            delete item;   // (the original builds an error message here but never shows it)
            continue;
        }
        if (replaced) {
            const std::string message =
                std::string("Nahran item na pozici, na ktere uz nejaky item je - puvodni bude zrusen (frame : ") + name + ")";
            ShowMessage(message.c_str(), "ERROR", 0);
            delete replaced;
        }
        AddInstance(item);
    }
    dtaClose(file);
    return true;
}
