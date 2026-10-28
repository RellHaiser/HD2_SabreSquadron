// C_Human: the seven item containers and everything done with the items in them - lookups,
// the inventory requests the controller sends to the server and their execution, dropping,
// supply boxes, uniforms and the equipment part of the soldier state (original source
// 0x005a2740..0x005a3a40, 0x005a66b0..0x005a7ed0, 0x005a8b50 and 0x005a9830).
#include "game/human.h"

#include <cstring>

#include "game/actors_module.h"
#include "game/game.h"
#include "game/human_internal.h"
#include "game/interface.h"
#include "game/item_defs.h"
#include "game/items.h"
#include "game/items_module.h"
#include "game/pending.h"
#include "game/world.h"

namespace {

// Container sizes of a human (inventory space units).
constexpr float HEAD_SPACE = 4.0f;
constexpr float HANDS_SPACE = 8.0f;
constexpr float UNIFORM_SPACE = 8.0f;
constexpr float BACKPACK_SPACE = 32.0f;
constexpr float CONTAINER_WEIGHT = 100.0f;

// Single-item containers in the order the lookups search them.
const int SLOTS_BY_UID[] = {ITEM_CONTAINER_HANDS, ITEM_CONTAINER_HEAD, ITEM_CONTAINER_LONG_GUN,
                            ITEM_CONTAINER_UNIFORM, ITEM_CONTAINER_BACKPACK};
const int SLOTS_BY_TYPE[] = {ITEM_CONTAINER_HEAD, ITEM_CONTAINER_LONG_GUN, ITEM_CONTAINER_HANDS,
                             ITEM_CONTAINER_UNIFORM, ITEM_CONTAINER_BACKPACK};

// Item type that single player soldier states leave out when it is empty (a one-shot
// launcher, inferred).
constexpr int ITEM_TYPE_38 = 0x26;
constexpr int ITEM_TYPE_DIVING_GEAR = 231;

// The item of a single-item container, null if it is empty.
C_Item *SlotItem(const C_Actor *actor, int container)
{
    C_ItemContainer *slot = actor->GetContainer(container);
    return slot->IsEmpty() ? nullptr : slot->GetItem();
}

// Slots of a multi-item container (belt / backpack contents) and their number.
struct S_Slots {
    std::vector<C_Item *> *slots;
    int count;
};
S_Slots StoreSlots(C_ItemContainer *store)
{
    return {store->GetSlots(), store->GetUsedSlotCount()};
}

// First item of `store` matching `match`, searching the slots in order and each slot from
// its end.
template <class F> C_Item *FindInStore(C_ItemContainer *store, F match)
{
    const S_Slots s = StoreSlots(store);
    for (int i = 0; i < s.count; ++i) {
        const std::vector<C_Item *> &slot = s.slots[i];
        for (size_t j = slot.size(); j-- > 0;) {
            if (match(slot[j]))
                return slot[j];
        }
    }
    return nullptr;
}

// Item with `uid` in container `container` of `actor` (any actor with containers).
C_Item *FindUidInContainer(const C_Actor *actor, int uid, int container)
{
    switch (container) {
    case ITEM_CONTAINER_HANDS:
    case ITEM_CONTAINER_HEAD:
    case ITEM_CONTAINER_LONG_GUN:
    case ITEM_CONTAINER_UNIFORM:
    case ITEM_CONTAINER_BACKPACK: {
        C_Item *item = SlotItem(actor, container);
        return item && item->GetUid() == uid ? item : nullptr;
    }
    case ITEM_CONTAINER_BELT:
        return FindInStore(actor->GetContainer(ITEM_CONTAINER_BELT), [uid](C_Item *item) { return item->GetUid() == uid; });
    case ITEM_CONTAINER_BACKPACK_CONTENTS:
        if (C_ItemContainer *contents = actor->GetContainer(ITEM_CONTAINER_BACKPACK_CONTENTS))
            return FindInStore(contents, [uid](C_Item *item) { return item->GetUid() == uid; });
        return nullptr;
    default:
        return nullptr;
    }
}

// Item with `uid` anywhere in the containers of `actor`.
C_Item *FindUid(const C_Actor *actor, int uid)
{
    // Fix: the original searches all seven containers even of actors that have only one
    // (vehicles), reading past their container array.
    if (actor->HasSingleContainer())
        return FindInStore(actor->GetContainer(0), [uid](C_Item *item) { return item->GetUid() == uid; });
    for (int container : SLOTS_BY_UID) {
        if (C_Item *item = SlotItem(actor, container); item && item->GetUid() == uid)
            return item;
    }
    if (C_Item *item = FindUidInContainer(actor, uid, ITEM_CONTAINER_BELT))
        return item;
    return FindUidInContainer(actor, uid, ITEM_CONTAINER_BACKPACK_CONTENTS);
}

// First item of behaviour `behaviour` in one container (0x005a7770).
C_Item *FindBehaviourInContainer(C_ItemContainer *container, int behaviour)
{
    if (!container)
        return nullptr;
    if (container->GetKind() == 0) {
        C_Item *item = container->GetItem();
        return item && item->GetDef()->GetBehaviour() == behaviour ? item : nullptr;
    }
    return FindInStore(container, [behaviour](C_Item *item) { return item->GetDef()->GetBehaviour() == behaviour; });
}

// Appends the items of behaviour `behaviour` in `container` (index `index`); true if any
// were added (0x005a7610).
bool CollectInContainer(C_ItemContainer *container, int index, int behaviour, std::vector<C_Item *> &items,
                        std::vector<int> &containers)
{
    if (!container)
        return false;
    const size_t before = items.size();
    if (container->GetKind() == 0) {
        C_Item *item = container->GetItem();
        if (item && item->GetDef()->GetBehaviour() == behaviour) {
            items.push_back(item);
            containers.push_back(index);
        }
    } else {
        const S_Slots s = StoreSlots(container);
        for (int i = 0; i < s.count; ++i) {
            const std::vector<C_Item *> &slot = s.slots[i];
            for (size_t j = slot.size(); j-- > 0;) {
                if (slot[j]->GetDef()->GetBehaviour() == behaviour) {
                    items.push_back(slot[j]);
                    containers.push_back(index);
                }
            }
        }
    }
    return items.size() != before;
}

// The first item of a belt slot that is not busy, searching the slots from the last.
template <class F> C_Item *FindReadyBeltItem(C_ItemContainer *belt, F match)
{
    const uint32_t now = GetGame()->GetMissionTime();
    const S_Slots s = StoreSlots(belt);
    for (int i = s.count; i-- > 0;) {
        C_Item *first = s.slots[i].front();
        if (first->GetTime() <= now && match(first))
            return first;
    }
    return nullptr;
}

// Random throw for dropped items: up and along `base`.
S_vector DropVelocity(const S_vector &base)
{
    const float strength = RandFloat() * 2.0f;
    const float a = RandFloat() * 0.2f;
    const float b = RandFloat() * 0.2f;
    const float c = RandFloat() * 0.2f;
    return S_vector(a * strength + base.x, b * strength + base.y, c * strength + base.z);
}

}  // namespace

// ------------------------------------------------------------------------- containers

void C_Human::Init()
{
    m_singleContainer = false;
    m_containers = new C_ItemContainer *[ITEM_CONTAINER_COUNT];
    const auto slot = [](float space, int itemSize) -> C_ItemContainer * {
        C_ItemContainer *container = new C_ItemSlot(0);
        container->SetMaxWeight(CONTAINER_WEIGHT);
        container->SetMaxSpace(space);
        container->SetMaxItemSize(itemSize);
        return container;
    };
    m_containers[ITEM_CONTAINER_HEAD] = slot(HEAD_SPACE, 2);
    m_containers[ITEM_CONTAINER_BELT] = new C_ItemStore(1);
    m_containers[ITEM_CONTAINER_BELT]->SetMaxWeight(CONTAINER_WEIGHT);
    m_containers[ITEM_CONTAINER_BELT]->SetMaxSpace(HUMAN_BELT_SPACE);
    m_containers[ITEM_CONTAINER_BELT]->SetMaxItemSize(1);
    m_containers[ITEM_CONTAINER_LONG_GUN] = slot(HUMAN_LONG_GUN_SPACE, 3);
    m_containers[ITEM_CONTAINER_HANDS] = slot(HANDS_SPACE, 3);
    m_containers[ITEM_CONTAINER_UNIFORM] = slot(UNIFORM_SPACE, 3);
    m_containers[ITEM_CONTAINER_BACKPACK] = slot(BACKPACK_SPACE, 5);
    m_containers[ITEM_CONTAINER_BACKPACK_CONTENTS] = nullptr;
}

void C_Human::ClearContainers()
{
    if (!m_containers)
        return;
    static const int ORDER[] = {ITEM_CONTAINER_HEAD, ITEM_CONTAINER_BELT, ITEM_CONTAINER_LONG_GUN, ITEM_CONTAINER_HANDS,
                                ITEM_CONTAINER_UNIFORM, ITEM_CONTAINER_BACKPACK_CONTENTS, ITEM_CONTAINER_BACKPACK};
    for (int container : ORDER) {
        if (m_containers[container])
            m_containers[container]->Clear();
    }
}

void C_Human::Done()
{
    if (!m_containers)
        return;
    // (The backpack contents container belongs to the backpack item.)
    for (int i = ITEM_CONTAINER_HEAD; i <= ITEM_CONTAINER_BACKPACK; ++i)
        delete m_containers[i];
    delete[] m_containers;
    m_containers = nullptr;
}

C_Item *C_Human::GetHandsItem() const
{
    return GetContainer(ITEM_CONTAINER_HANDS)->GetItem();
}

I3D_frame *C_Human::GetHandsItemFrame() const
{
    C_Item *item = GetHandsItem();
    return item ? item->GetFrame(0) : nullptr;
}

C_Item *C_Human::GetLongGunItem() const
{
    return GetContainer(ITEM_CONTAINER_LONG_GUN)->GetItem();
}

C_Item *C_Human::GetUniformItem() const
{
    return GetContainer(ITEM_CONTAINER_UNIFORM)->GetItem();
}

C_WeaponItem *C_Human::GetHandsWeapon() const
{
    C_Item *item = GetHandsItem();
    if (item && item->GetKind() == ITEM_KIND_WEAPON)
        return static_cast<C_WeaponItem *>(item);
    return nullptr;
}

void C_Human::OnHandsChanged()
{
    if (m_controller)
        m_controller->UpdateHandsItem();
}

void C_Human::OnUniformChanged(int uid)
{
    m_controller->OnUniformChanged(uid);
}

// ---------------------------------------------------------------------------- lookups

C_Item *C_Human::FindItemByUid(int uid) const
{
    return FindUid(this, uid);
}

C_Item *C_Human::FindItemInContainer(int uid, int container) const
{
    return FindUidInContainer(this, uid, container);
}

C_Item *C_Human::FindItem(int typeId, int *container) const
{
    for (int index : SLOTS_BY_TYPE) {
        if (C_Item *item = SlotItem(this, index); item && item->GetTypeId() == typeId) {
            if (container)
                *container = index;
            return item;
        }
    }
    const auto match = [typeId](C_Item *item) { return item->GetTypeId() == typeId; };
    if (C_Item *item = FindInStore(GetContainer(ITEM_CONTAINER_BELT), match)) {
        if (container)
            *container = ITEM_CONTAINER_BELT;
        return item;
    }
    if (C_ItemContainer *contents = GetContainer(ITEM_CONTAINER_BACKPACK_CONTENTS)) {
        if (C_Item *item = FindInStore(contents, match)) {
            if (container)
                *container = ITEM_CONTAINER_BACKPACK_CONTENTS;
            return item;
        }
    }
    return nullptr;
}

C_Item *C_Human::FindItemByBehaviour(int behaviour, int *container) const
{
    for (int index : SLOTS_BY_UID) {
        if (C_Item *item = SlotItem(this, index); item && item->GetDef()->GetBehaviour() == behaviour) {
            *container = index;
            return item;
        }
    }
    const auto match = [behaviour](C_Item *item) { return item->GetDef()->GetBehaviour() == behaviour; };
    if (C_Item *item = FindInStore(GetContainer(ITEM_CONTAINER_BELT), match)) {
        *container = ITEM_CONTAINER_BELT;
        return item;
    }
    if (C_ItemContainer *contents = GetContainer(ITEM_CONTAINER_BACKPACK_CONTENTS)) {
        if (C_Item *item = FindInStore(contents, match)) {
            *container = ITEM_CONTAINER_BACKPACK_CONTENTS;
            return item;
        }
    }
    *container = ITEM_CONTAINER_COUNT;
    return nullptr;
}

C_Item *C_Human::FindItemByRecordType(int recordType) const
{
    const auto match = [recordType](C_Item *item) { return item->GetDef()->GetRecordType() == recordType; };
    for (int index : SLOTS_BY_UID) {
        if (C_Item *item = SlotItem(this, index); item && match(item))
            return item;
    }
    if (C_Item *item = FindInStore(GetContainer(ITEM_CONTAINER_BELT), match))
        return item;
    if (C_ItemContainer *contents = GetContainer(ITEM_CONTAINER_BACKPACK_CONTENTS))
        return FindInStore(contents, match);
    return nullptr;
}

C_Item *C_Human::FindBeltItem(int behaviour) const
{
    return FindReadyBeltItem(GetContainer(ITEM_CONTAINER_BELT),
                             [behaviour](C_Item *item) { return item->GetDef()->GetBehaviour() == behaviour; });
}

C_Item *C_Human::GetMedkit() const
{
    C_Item *backpack = GetContainer(ITEM_CONTAINER_BACKPACK)->GetItem();
    if (backpack && backpack->GetDef()->GetBehaviour() == ITEM_BEHAVIOUR_MEDKIT)
        return GetContainer(ITEM_CONTAINER_BACKPACK)->GetItem();
    return FindBeltItem(ITEM_BEHAVIOUR_MEDKIT);
}

C_Item *C_Human::FindWireCutter() const
{
    return FindBeltItem(ITEM_BEHAVIOUR_WIRE_CUTTER);
}

C_Item *C_Human::FindContainerOfBehaviour(int behaviour, int *container) const
{
    for (int i = 0; i < ITEM_CONTAINER_COUNT; ++i) {
        if (C_Item *item = FindBehaviourInContainer(m_containers[i], behaviour)) {
            if (container)
                *container = i;
            return item;
        }
    }
    return nullptr;
}

bool C_Human::CollectItems(int behaviour, std::vector<C_Item *> &items, std::vector<int> &containers) const
{
    items.clear();
    containers.clear();
    for (int i = 0; i < ITEM_CONTAINER_COUNT; ++i)
        CollectInContainer(m_containers[i], i, behaviour, items, containers);
    return !items.empty();
}

int C_Human::CountItems(int typeId) const
{
    static const int SINGLE[] = {ITEM_CONTAINER_HEAD, ITEM_CONTAINER_LONG_GUN, ITEM_CONTAINER_HANDS,
                                 ITEM_CONTAINER_UNIFORM, ITEM_CONTAINER_BACKPACK};
    for (int index : SINGLE) {
        if (C_Item *item = SlotItem(this, index); item && item->GetTypeId() == typeId)
            return 1;
    }
    // The size of the first slot holding the type.
    const auto slotSize = [typeId](C_ItemContainer *store) -> int {
        const S_Slots s = StoreSlots(store);
        for (int i = 0; i < s.count; ++i) {
            const std::vector<C_Item *> &slot = s.slots[i];
            for (size_t j = slot.size(); j-- > 0;) {
                if (slot[j]->GetTypeId() == typeId)
                    return static_cast<int>(slot.size());
            }
        }
        return -1;
    };
    int count = slotSize(GetContainer(ITEM_CONTAINER_BELT));
    if (count >= 0)
        return count;
    if (C_ItemContainer *contents = GetContainer(ITEM_CONTAINER_BACKPACK_CONTENTS)) {
        count = slotSize(contents);
        if (count >= 0)
            return count;
    }
    return 0;
}

int C_Human::CountAmmo(int typeId, bool backpack) const
{
    int count = 0;
    if (backpack) {
        if (C_ItemContainer *contents = GetContainer(ITEM_CONTAINER_BACKPACK_CONTENTS)) {
            const S_Slots s = StoreSlots(contents);
            for (int i = s.count; i-- > 0;) {
                if (s.slots[i].front()->GetTypeId() == typeId)
                    count = static_cast<int>(s.slots[i].size());
            }
        }
    }
    const S_Slots s = StoreSlots(GetContainer(ITEM_CONTAINER_BELT));
    for (int i = s.count; i-- > 0;) {
        if (s.slots[i].front()->GetTypeId() == typeId)
            return static_cast<int>(s.slots[i].size()) + count;
    }
    return count;
}

int C_Human::CountThrownItems() const
{
    const uint32_t now = GetGame()->GetMissionTime();
    int count = 0;
    const S_Slots s = StoreSlots(GetContainer(ITEM_CONTAINER_BELT));
    for (int i = s.count; i-- > 0;) {
        C_Item *first = s.slots[i].front();
        if (first->GetTime() <= now && first->GetDef()->GetBehaviour() == ITEM_BEHAVIOUR_THROWN)
            count += static_cast<int>(s.slots[i].size());
    }
    return count;
}

bool C_Human::IsWearingDivingGear() const
{
    C_Item *backpack = GetContainer(ITEM_CONTAINER_BACKPACK)->GetItem();
    return backpack && backpack->GetTypeId() == ITEM_TYPE_DIVING_GEAR;
}

// -------------------------------------------------------------------------- removing

void C_Human::RemoveItem(int uid, bool destroy)
{
    C_Item *item = nullptr;
    int container = ITEM_CONTAINER_COUNT;
    if (C_Item *hands = SlotItem(this, ITEM_CONTAINER_HANDS); hands && hands->GetUid() == uid) {
        container = ITEM_CONTAINER_HANDS;
        item = hands;
        // Fix: the original calls this whatever kind of item is in the hands.
        if (item->GetKind() == ITEM_KIND_WEAPON)
            static_cast<C_WeaponItem *>(item)->StopReloadSound();
        PlayerSetViewMode(0);
    } else {
        for (int index : {ITEM_CONTAINER_HEAD, ITEM_CONTAINER_LONG_GUN, ITEM_CONTAINER_UNIFORM, ITEM_CONTAINER_BACKPACK}) {
            if (C_Item *slot = SlotItem(this, index); slot && slot->GetUid() == uid) {
                container = index;
                item = slot;
                break;
            }
        }
        if (!item) {
            for (int index : {ITEM_CONTAINER_BELT, ITEM_CONTAINER_BACKPACK_CONTENTS}) {
                if (C_Item *found = FindUidInContainer(this, uid, index)) {
                    container = index;
                    item = found;
                    break;
                }
            }
            if (!item)
                return;
        }
    }
    if (!item)
        return;
    g_itemsModule.DropItem(item, this, container);
    m_inventoryChanged = true;
    if (destroy)
        g_itemsModule.DestroyItem(item);
}

void C_Human::RemoveItemsFromStore(int typeId)
{
    while (C_Item *item = FindItem(typeId))
        RemoveItem(item->GetUid(), true);
    m_inventoryChanged = true;
}

void C_Human::CreateItemInStore(int typeId, int container)
{
    g_itemsModule.CreateItem(typeId, container, this, -1);
}

// ---------------------------------------------------------------------------- requests

bool C_Human::RequestMoveItem(C_Item *item, C_Actor *other, int fromContainer, int toContainer, int slot)
{
    const int otherId = other && other != this ? other->GetNetId() : -1;
    return m_controller->RequestMoveItem(item->GetUid(), otherId, fromContainer, toContainer, slot);
}

bool C_Human::RequestTransferItem(C_Item *item, int arg, C_Human *other, C_Item *otherItem, int container)
{
    int otherId = -1;
    if (other) {
        if (container == ITEM_CONTAINER_UNIFORM) {
            // Only uniforms of unconscious humans of the same group can be swapped.
            if (other->m_group != m_group)
                return false;
            if (!IsUnconscious() && !other->IsUnconscious())
                return false;
            if (m_unknown1a5 || other->m_unknown1a5)
                return false;
        }
        if (other != this)
            otherId = other->m_netId;
    }
    return m_controller->RequestTransferItem(item->GetUid(), arg, otherId, otherItem->GetUid(), container);
}

void C_Human::RequestThrowItem(C_Item *item, int /*arg*/, const S_vector &dir)
{
    const float a = RandFloat();
    const float b = RandFloat();
    const float c = RandFloat();
    const S_vector velocity((a * 0.2f + dir.x) * 2.5f, (b * 0.1f + dir.y) * 2.5f, (c * 0.2f + dir.z) * 2.5f);
    m_controller->RequestThrowItem(item->GetUid(), velocity);
    m_inventoryChanged = true;
}

void C_Human::RequestTakeFromBox(C_Actor *box, int index)
{
    m_controller->RequestTakeFromBox(box->GetNetId(), index);
    m_inventoryChanged = true;
}

void C_Human::RequestPickUpItem(C_Actor *itemActor)
{
    m_controller->RequestPickUpItem(itemActor->GetNetId(), ITEM_CONTAINER_COUNT);
    m_inventoryChanged = true;
}

bool C_Human::DrawLongGun()
{
    if (GetContainer(ITEM_CONTAINER_HANDS)->GetItem())
        return false;
    C_Item *longGun = GetContainer(ITEM_CONTAINER_LONG_GUN)->GetItem();
    if (!longGun)
        return false;
    const bool requested = RequestMoveItem(longGun, this, ITEM_CONTAINER_LONG_GUN, ITEM_CONTAINER_HANDS, 0);
    OnHandsChanged();
    return requested;
}

bool C_Human::DrawSidearm(int reason)
{
    if (GetContainer(ITEM_CONTAINER_HANDS)->GetItem())
        return false;
    if (reason != 2 && reason != 9 && reason != 10 && reason != 11)
        return false;
    const uint32_t now = GetGame()->GetMissionTime();
    C_Item *sidearm = nullptr;
    const S_Slots s = StoreSlots(GetContainer(ITEM_CONTAINER_BELT));
    if (s.count == 0)
        return false;
    for (int i = s.count; i-- > 0;) {
        C_Item *first = s.slots[i].front();
        if (first->GetTime() <= now && first->GetDef()->GetBehaviour() == ITEM_BEHAVIOUR_SIDEARM)
            sidearm = first;
    }
    if (!sidearm)
        return false;
    const bool requested = RequestMoveItem(sidearm, this, ITEM_CONTAINER_BELT, ITEM_CONTAINER_HANDS, 0);
    OnHandsChanged();
    return requested;
}

// -------------------------------------------------------- execution (server / client)

bool C_Human::ServerMoveItem(int uid, int fromActorId, int fromContainer, int *toContainer, int slot)
{
    C_Actor *from = fromActorId != -1 ? g_actorsModule.GetActorById(fromActorId) : this;
    C_Item *item = FindUidInContainer(from, uid, fromContainer);
    if (!item)
        return false;
    C_Item *replaced = nullptr;
    int result;
    if (*toContainer == ITEM_CONTAINER_COUNT)
        result = g_itemsModule.TransferItemAuto(item, from, fromContainer, this, toContainer);
    else
        result = g_itemsModule.MoveItem(item, &replaced, from, fromContainer, this, *toContainer, slot, nullptr, true,
                                        false, false, false);
    if (result != ITEM_RESULT_OK)
        return false;
    m_inventoryChanged = true;
    if (fromContainer == ITEM_CONTAINER_HANDS || *toContainer == ITEM_CONTAINER_HANDS)
        PlayerSetViewMode(0);
    return true;
}

void C_Human::ClientMoveItem(int uid, int fromActorId, int fromContainer, int toContainer, int slot)
{
    if (IsServer())
        return;
    C_Actor *from = fromActorId != -1 ? g_actorsModule.GetActorById(fromActorId) : this;
    C_Item *item = FindUid(from, uid);
    C_Item *replaced = nullptr;
    g_itemsModule.MoveItem(item, &replaced, from, fromContainer, this, toContainer, slot, nullptr, true, false, false,
                           false);
    m_inventoryChanged = true;
}

bool C_Human::ServerTransferItem(int uid, int container, int otherId, int otherUid, int otherContainer)
{
    C_Actor *from = otherId != -1 ? g_actorsModule.GetActorById(otherId) : this;
    C_Item *own = FindUidInContainer(this, uid, container);
    C_Item *item = FindUidInContainer(from, otherUid, otherContainer);
    if (!own || !item)
        return false;
    C_Item *replaced = own;
    int toContainer = ITEM_CONTAINER_COUNT;
    int replacedContainer = ITEM_CONTAINER_COUNT;
    if (g_itemsModule.TransferItem(item, from, otherContainer, this, container, &replaced, &toContainer,
                                   &replacedContainer) != ITEM_RESULT_OK)
        return false;
    m_inventoryChanged = true;
    // (The other actor is assumed to be a human.)
    static_cast<C_Human *>(from)->m_inventoryChanged = true;
    if (container == ITEM_CONTAINER_HANDS || otherContainer == ITEM_CONTAINER_HANDS)
        PlayerSetViewMode(0);
    return true;
}

void C_Human::ClientTransferItem(int uid, int container, int otherId, int otherUid, int otherContainer)
{
    if (IsServer())
        return;
    C_Actor *from = otherId != -1 ? g_actorsModule.GetActorById(otherId) : this;
    C_Item *replaced = FindUid(this, uid);
    C_Item *item = FindUid(from, otherUid);
    int toContainer = ITEM_CONTAINER_COUNT;
    int replacedContainer = ITEM_CONTAINER_COUNT;
    g_itemsModule.TransferItem(item, from, otherContainer, this, container, &replaced, &toContainer, &replacedContainer);
    m_inventoryChanged = true;
    static_cast<C_Human *>(from)->m_inventoryChanged = true;
}

void C_Human::DropItem(int uid, const S_vector &velocity)
{
    I3D_frame *hand = m_frame->FindChildFrame("j", FRAME_MASK_MODEL_CHILD);
    if (C_WeaponItem *weapon = GetHandsWeapon(); weapon && weapon->GetUid() == uid)
        weapon->StopReloadSound();
    const int actorId = g_temporaryActors.DropItem(this, uid, hand->GetWorldPos(), velocity, true);
    if (actorId < 0) {
        const C_fstr text("SvDropItem: failed - _TempActorIndex %d ActorIndex %d _ItemUID %d \n", actorId, m_netId, uid);
        OutputDebugStringA(text);
        return;
    }
    if (!IsLocalGame()) {
        const float speed = velocity.Magnitude();
        const S_vector dir = velocity.Normalized();
        m_controller->OnItemDropped(actorId, uid, &dir, speed);
    }
    m_inventoryChanged = true;
}

void C_Human::ClientDropItem(int tempActorId, int uid, const S_vector &velocity)
{
    if (IsServer())
        return;
    I3D_frame *hand = m_frame->FindChildFrame("j", FRAME_MASK_MODEL_CHILD);
    g_temporaryActors.ClientCreateItem(tempActorId, m_netId, -1, uid, hand->GetWorldPos(), velocity, nullptr);
    m_inventoryChanged = true;
}

void C_Human::DropAllItems()
{
    C_Item *hands = GetContainer(ITEM_CONTAINER_HANDS)->GetItem();
    // Thrown forwards and up, twice the unit direction.
    const S_vector &view = m_controller->GetViewDir();
    const S_vector base = S_vector(view.x, view.y + 1.0f, view.z).Normalized() * 2.0f;
    if (hands && hands->GetMaxAmount() > 0.0f) {
        // Dropped weapons are full. (Fix: the original writes the weapon field whatever
        // kind of item is in the hands.)
        if (hands->GetKind() == ITEM_KIND_WEAPON)
            static_cast<C_WeaponItem *>(hands)->SetLoadedRounds(hands->GetMaxAmount());
        DropItem(hands->GetUid(), DropVelocity(base));
    }
    if (C_ItemContainer *backpack = GetContainer(ITEM_CONTAINER_BACKPACK)) {
        C_Item *item = backpack->GetItem();
        if (item && item->GetDef()->GetBehaviour() == ITEM_BEHAVIOUR_MISSION_EXPLOSIVE)
            DropItem(item->GetUid(), DropVelocity(base));
    }
    // Multiplayer pick-ups in the stores.
    const auto dropPickups = [this, &base](C_ItemContainer *store) {
        for (int i = store->GetUsedSlotCount(); i-- > 0;) {
            const std::vector<C_Item *> &slot = store->GetSlots()[i];
            const int behaviour = slot.front()->GetDef()->GetBehaviour();
            if (behaviour != ITEM_BEHAVIOUR_RESPAWNING && behaviour != ITEM_BEHAVIOUR_MISSION_EXPLOSIVE)
                continue;
            // (Copied: dropping changes the slot.)
            const std::vector<C_Item *> items = slot;
            for (size_t j = items.size(); j-- > 0;)
                DropItem(items[j]->GetUid(), DropVelocity(base));
        }
    };
    if (C_ItemContainer *contents = GetContainer(ITEM_CONTAINER_BACKPACK_CONTENTS))
        dropPickups(contents);
    dropPickups(GetContainer(ITEM_CONTAINER_BELT));
}

bool C_Human::TakeFromSupplyBox(int boxId, int index, int *container, int *uid)
{
    C_SupplyBox *box = static_cast<C_SupplyBox *>(g_actorsModule.GetActorById(boxId));
    if (!box || box->GetCharges() == 0)
        return false;
    if (box->IsBusy() || !box->IsPlaced())
        return false;
    int typeId;
    if (!box->HasItemLists()) {
        // Ammunition for the weapon in the hands.
        C_WeaponItem *weapon = GetHandsWeapon();
        if (!weapon)
            return false;
        typeId = weapon->GetDef()->GetAmmoItemId();
    } else {
        typeId = box->GetItemList(GetTeam(false) != TEAM_AXIS)[index];
    }
    C_Item *item = g_itemsModule.CreateItem(typeId, 0, nullptr, -1);
    if (g_itemsModule.PutItem(item, this, container, true) != ITEM_RESULT_OK) {
        g_itemsModule.DestroyItem(item);
        return false;
    }
    if (!box->IsBusy() && box->IsPlaced())
        box->UseCharge();
    *uid = item->GetUid();
    m_inventoryChanged = true;
    return true;
}

void C_Human::ClientTakeFromSupplyBox(int boxId, int index, int container, int uid)
{
    if (IsServer())
        return;
    C_SupplyBox *box = static_cast<C_SupplyBox *>(g_actorsModule.GetActorById(boxId));
    if (!box)
        return;
    int typeId;
    if (!box->HasItemLists()) {
        C_WeaponItem *weapon = GetHandsWeapon();
        if (!weapon)
            return;
        typeId = weapon->GetDef()->GetAmmoItemId();
    } else {
        typeId = box->GetItemList(GetTeam(false) != TEAM_AXIS)[index];
    }
    g_itemsModule.CreateItem(typeId, container, this, uid);
    if (!box->IsBusy() && box->IsPlaced())
        box->UseCharge();
    m_inventoryChanged = true;
}

bool C_Human::PickUpItem(int tempActorId, int *container)
{
    C_ItemActor *actor = static_cast<C_ItemActor *>(g_temporaryActors.FindActor(tempActorId));
    if (!actor)
        return false;
    if (g_itemsModule.PutItem(actor->GetItem(), this, container, true) != ITEM_RESULT_OK)
        return false;
    actor->ReleaseItem();
    g_temporaryActors.DestroyActor(actor);
    m_inventoryChanged = true;
    return true;
}

void C_Human::ClientPickUpItem(int tempActorId, int container)
{
    if (IsServer())
        return;
    C_ItemActor *actor = static_cast<C_ItemActor *>(g_temporaryActors.FindActor(tempActorId));
    if (!actor)
        return;
    C_Item *replaced = nullptr;
    g_itemsModule.MoveItem(actor->GetItem(), &replaced, nullptr, 0, this, container, -1, nullptr, true, false, false,
                           false);
    actor->ReleaseItem();
    g_temporaryActors.DestroyActor(actor);
    m_inventoryChanged = true;
}

// --------------------------------------------------------------------------- uniforms

bool C_Human::PutOnUniform(int uid)
{
    C_Item *item = FindItemByUid(uid);
    if (!item)
        return false;
    // Once the mission runs, a uniform made for this model is already worn.
    if (GetGame()->GetMissionTime() != 0 && std::strstr(item->GetDef()->GetModelName(), static_cast<I3D_model *>(m_frame)->GetFileName()))
        return true;
    ReleaseHumanSpeech(this);
    GetGEffects()->DressUniform(this, item->GetTypeId());
    m_bodyFrame = m_frame->FindChildFrame("f", FRAME_MASK_MODEL_CHILD);
    if (m_sightTargetId != -1) {
        // (The original looks the sight target up by the observer id; null crashes it.)
        if (C_SightTarget *target = g_sightModule.GetTarget(m_sightId))
            target->Refresh();
    }
    m_inventoryChanged = true;
    return true;
}

void C_Human::ClientPutOnUniform(int uid)
{
    if (IsServer())
        return;
    C_Item *item = FindItemByUid(uid);
    if (!item)
        return;
    if (GetGame()->GetMissionTime() != 0 && std::strstr(item->GetDef()->GetModelName(), static_cast<I3D_model *>(m_frame)->GetFileName()))
        return;
    ReleaseHumanSpeech(this);
    GetGEffects()->DressUniform(this, item->GetTypeId());
    m_bodyFrame = m_frame->FindChildFrame("f", FRAME_MASK_MODEL_CHILD);
    m_inventoryChanged = true;
}

// ---------------------------------------------------------------------- soldier state

void C_Human::GetEquipmentState(S_SoldierState &state) const
{
    const auto put = [](S_SoldierItem &entry, const C_Item *item) {
        entry.present = 1;
        entry.typeId = static_cast<int16_t>(item->GetTypeId());
        if (!IsLocalGame())
            entry.uid = static_cast<int16_t>(item->GetUid());
    };
    if (C_Item *item = GetContainer(ITEM_CONTAINER_HEAD)->GetItem())
        put(state.head, item);
    if (C_Item *item = GetContainer(ITEM_CONTAINER_UNIFORM)->GetItem())
        put(state.uniform, item);
    if (C_Item *item = GetContainer(ITEM_CONTAINER_BACKPACK)->GetItem())
        put(state.backpack, item);
    // Single player: an empty item 38 is left behind.
    const auto keep = [](C_Item *item) {
        return item->GetTypeId() != ITEM_TYPE_38 || item->GetAmount() != 0.0f || !IsLocalGame();
    };
    if (C_Item *item = GetContainer(ITEM_CONTAINER_LONG_GUN)->GetItem(); item && keep(item))
        put(state.longGun, item);
    if (C_Item *item = GetContainer(ITEM_CONTAINER_HANDS)->GetItem(); item && keep(item))
        put(state.hands, item);

    const auto collect = [](C_ItemContainer *store, std::vector<S_SoldierItem> &list) {
        S_SoldierItem entry;
        for (int i = 0; i < store->GetUsedSlotCount(); ++i) {
            for (C_Item *item : store->GetSlots()[i]) {
                entry.present = 1;
                if (!IsLocalGame())
                    entry.uid = static_cast<int16_t>(item->GetUid());
                entry.typeId = static_cast<int16_t>(item->GetTypeId());
                list.push_back(entry);
            }
        }
    };
    collect(GetContainer(ITEM_CONTAINER_BELT), state.belt);
    if (C_ItemContainer *contents = GetContainer(ITEM_CONTAINER_BACKPACK_CONTENTS))
        collect(contents, state.backpackContents);
}

void C_Human::GetSoldierState(S_SoldierState &state) const
{
    if (!IsLocalGame() && m_controller->GetType() == HUMAN_CONTROLLER_PLAYER) {
        state.vip = static_cast<C_PlayerController *>(m_controller)->IsVip();
        state.spectating = m_controller ? m_controller->IsSpectating() : false;
    }
    state.health = m_stats[STAT_HEALTH];
    state.team = GetTeam(false);
    state.profileIndex = m_profileIndex;
    GetEquipmentState(state);
    state.progress = m_progress;
}

// --------------------------------------------------------------------- carried bodies

void C_Human::DropCarried()
{
    if (!IsLocalGame() || !m_carried)
        return;
    S_HumanCommand command;
    command.category = HUMAN_COMMAND_VEHICLE;
    command.type = HUMAN_COMMAND_LEAVE_VEHICLE;
    command.param1 = -1;
    command.param2 = 0;
    IssueCommand(command);
    GetGame()->GetInterface()->OnActorEvent(INTERFACE_ACTOR_DROPPED, m_carried);
}

bool C_Human::GetActionProgress(int *time, int *total, int *textId) const
{
    if (C_Actor *vehicle = m_vehicle) {
        switch (vehicle->GetType()) {
        case ACTOR_CAR:
        case ACTOR_SUBMARINE:
        case ACTOR_STATIC_WEAPON:
        case ACTOR_JUNKERS88:
            if (vehicle->HasSeatWeapon(m_seat))
                return vehicle->UnknownC8(m_seat, time, total, textId);
            break;
        default:
            break;
        }
    }
    if (m_actionTime != 0) {
        *time = m_actionTime;
        *total = m_actionTotal;
        *textId = m_actionText;
        return true;
    }
    if (IsLocalGame())
        return false;
    const int zone = GetActiveMPZone();
    if (zone < 0)
        return false;
    return static_cast<C_MPZone *>(g_actorsModule.GetActorById(zone))->GetProgress(time, total, textId);
}

int C_Human::GetActiveMPZone() const
{
    C_Actor *actor = g_actorsModule.GetActorById(m_mpZone);
    if (actor && actor->GetType() == ACTOR_MP_ZONE && static_cast<C_MPZone *>(actor)->IsRunning())
        return m_mpZone;
    return -1;
}
