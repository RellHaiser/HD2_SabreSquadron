// Item containers (original source 0x007d6af0..0x007d7a90).
#include "game/items.h"

#include <utility>

#include "game/game.h"
#include "game/items_module.h"
#include "game/pending.h"

namespace {

// Removes `item` from `items` by moving the last element into its place; false if absent.
bool SwapRemove(std::vector<C_Item *> &items, C_Item *item)
{
    for (size_t i = 0; i < items.size(); ++i) {
        if (items[i] == item) {
            items[i] = items.back();
            items.pop_back();
            return true;
        }
    }
    return false;
}

// Destroys the items from the back.
void DestroyAll(std::vector<C_Item *> &items)
{
    while (!items.empty()) {
        C_Item *item = items.back();
        items.pop_back();
        g_itemsModule.DestroyItem(item);
    }
}

// Same type and instance kind: such items share a store slot.
bool SameSort(const C_Item *a, const C_Item *b)
{
    return a->GetTypeId() == b->GetTypeId() && a->GetKind() == b->GetKind();
}

} // namespace

C_ItemContainer::C_ItemContainer(int kind) : m_kind(kind) {}

// ---------------------------------------------------------------------------
// C_ItemSlot

C_ItemSlot::~C_ItemSlot()
{
    if (m_item)
        C_ItemSlot::Clear();
}

float C_ItemSlot::GetTotalWeight()
{
    return m_item ? m_item->GetWeight() : 0.0f;
}

float C_ItemSlot::GetTotalSpace()
{
    return m_item ? m_item->GetSpace() : 0.0f;
}

int C_ItemSlot::CountItems(int typeId)
{
    return m_item && m_item->GetTypeId() == typeId ? 1 : 0;
}

int C_ItemSlot::AddItem(C_Item *item, C_Item **replaced, int)
{
    if (!item)
        return ITEM_RESULT_NO_ITEM;
    // (`*replaced` is left alone when the slot was empty.)
    if (m_item)
        *replaced = m_item;
    m_item = item;
    return ITEM_RESULT_OK;
}

int C_ItemSlot::RemoveItem(C_Item *item, int)
{
    if (item != m_item)
        return ITEM_RESULT_FAILED;
    m_item = nullptr;
    return ITEM_RESULT_OK;
}

void C_ItemSlot::Clear()
{
    if (m_item)
        g_itemsModule.DestroyItem(m_item);
    m_lockTime = 0;
    m_item = nullptr;
}

bool C_ItemSlot::IsLocked()
{
    return GetGame()->GetMissionTime() < m_lockTime;
}

// ---------------------------------------------------------------------------
// C_ItemList

C_ItemList::~C_ItemList()
{
    C_ItemList::Clear();
}

float C_ItemList::GetTotalWeight()
{
    float total = 0.0f;
    for (C_Item *item : m_items)
        total += item->GetWeight();
    return total;
}

float C_ItemList::GetTotalSpace()
{
    float total = 0.0f;
    for (C_Item *item : m_items)
        total += item->GetSpace();
    return total;
}

int C_ItemList::AddItem(C_Item *item, C_Item **replaced, int)
{
    if (!item)
        return ITEM_RESULT_NO_ITEM;
    *replaced = nullptr;
    m_items.push_back(item);
    return ITEM_RESULT_OK;
}

int C_ItemList::RemoveItem(C_Item *item, int)
{
    return SwapRemove(m_items, item) ? ITEM_RESULT_OK : ITEM_RESULT_FAILED;
}

void C_ItemList::Clear()
{
    DestroyAll(m_items);
}

// ---------------------------------------------------------------------------
// C_ItemStore

C_ItemStore::C_ItemStore(int kind) : C_ItemContainer(kind), m_slots(kind == 1 ? 14 : 32) {}

C_ItemStore::~C_ItemStore()
{
    if (m_usedSlots != 0)
        C_ItemStore::Clear();
}

float C_ItemStore::GetTotalWeight()
{
    float total = 0.0f;
    for (const std::vector<C_Item *> &slot : m_slots) {
        if (!slot.empty())
            total += slot.front()->GetWeight() * static_cast<float>(slot.size());
    }
    return total;
}

float C_ItemStore::GetTotalSpace()
{
    float total = 0.0f;
    for (const std::vector<C_Item *> &slot : m_slots) {
        if (!slot.empty())
            total += slot.front()->GetSpace() * static_cast<float>(slot.size());
    }
    return total;
}

int C_ItemStore::CountItems(int typeId)
{
    for (const std::vector<C_Item *> &slot : m_slots) {
        if (!slot.empty() && slot.front()->GetTypeId() == typeId)
            return static_cast<int>(slot.size());
    }
    return 0;
}

int C_ItemStore::AddItem(C_Item *item, C_Item **, int)
{
    if (!item)
        return ITEM_RESULT_NO_ITEM;
    int freeSlot = -1;
    for (size_t i = 0; i < m_slots.size(); ++i) {
        std::vector<C_Item *> &slot = m_slots[i];
        if (slot.empty()) {
            if (freeSlot == -1)
                freeSlot = static_cast<int>(i);
        } else if (SameSort(item, slot.front())) {
            slot.push_back(item);
            return ITEM_RESULT_OK;
        }
    }
    if (freeSlot < 0)
        return ITEM_RESULT_FAILED;
    m_slots[freeSlot].push_back(item);
    ++m_usedSlots;
    return ITEM_RESULT_OK;
}

int C_ItemStore::RemoveItem(C_Item *item, int)
{
    if (!item)
        return ITEM_RESULT_FAILED;
    const int count = GetSlotCount();
    for (int i = 0; i < count; ++i) {
        std::vector<C_Item *> &slot = m_slots[i];
        if (slot.empty() || !SameSort(item, slot.front()))
            continue;
        // Only the first slot of the item's sort is searched.
        if (!SwapRemove(slot, item))
            return ITEM_RESULT_FAILED;
        if (slot.empty()) {
            // Move the emptied slot behind the used ones.
            for (int j = i + 1; j < count; ++j)
                std::swap(m_slots[j - 1], m_slots[j]);
            --m_usedSlots;
        }
        return ITEM_RESULT_OK;
    }
    return ITEM_RESULT_FAILED;
}

void C_ItemStore::Clear()
{
    for (std::vector<C_Item *> &slot : m_slots)
        DestroyAll(slot);
    m_usedSlots = 0;
}
