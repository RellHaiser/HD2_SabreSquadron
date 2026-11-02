// The store's item lists of the equipment menus (original 0x0067b070..0x0067c390).
#include "game/store_list.h"

#include "game/control.h"
#include "game/controls.h"
#include "game/equipment_menu.h"
#include "game/item_defs.h"
#include "game/profile.h"
#include "ls3d/i3d.h"

namespace {

// The list's controls by their ids' bits 12-19: the tabs (1), the pages' group (2), the pages
// (3 + the page).
constexpr uint32_t STORE_TABS = 0x1000;
constexpr uint32_t STORE_PAGES = 0x2000;
constexpr int STORE_FIRST_PAGE = 3;
constexpr int STORE_PAGE_COUNT = 6;

// The behaviours whose entries show another type's count: the head gear (16) of uniforms (15).
constexpr int BEHAVIOUR_UNIFORM = 15;
constexpr int BEHAVIOUR_HEADGEAR = 16;

// Where the highlight sits on an item's frame.
const S_vector HIGHLIGHT_POS(0.0f, 0.0f, -0.1f);

void Post(uint32_t target, uint32_t id, uint32_t value = 0)
{
    S_ControlMessage message;
    message.target = target;
    message.id = id;
    message.value = value;
    PostControlMessage(message);
}

} // namespace

int C_StoreList::GetCount(int typeId) const
{
    return m_stock ? m_stock->Get(typeId) : 0;
}

void C_StoreList::ShowPage(int page)
{
    if (page < 0)
        return;
    Post(m_shownPage, CONTROL_MSG_HIDE);
    m_shownPage = BaseId() | (static_cast<uint32_t>(page) + STORE_FIRST_PAGE & 0xFF) << 12;
    Post(m_shownPage, CONTROL_MSG_SHOW);
    Post(BaseId() | STORE_PAGES, CONTROL_MSG_SET_VALUE, m_shownPage);
    Post(m_shownPage, CONTROL_MSG_REFRESH);
}

bool C_StoreList::OnTab(void *object, void *, C_Control *control, uint32_t, uint32_t)
{
    if (control) {
        // (The tab control answers its chosen tab in the value.)
        S_ControlMessage get;
        get.id = CONTROL_MSG_GET_TEXT;
        control->HandleMessage(&get);
        static_cast<C_StoreList *>(object)->ShowPage(static_cast<int>(get.value));
    }
    return true;
}

bool C_StoreList::OnItem(void *object, void *argument, C_Control *control, uint32_t notification, uint32_t value)
{
    auto *list = static_cast<C_StoreList *>(object);
    C_EquipmentMenuBase *menu = list->m_menu;
    if (!menu)
        return true;
    switch (notification) {
    case CONTROL_NOTIFY_09:
        menu->OnItemNotify09(argument, control, notification, value);
        break;
    case CONTROL_NOTIFY_ENTER:
        if (control) {
            list->m_highlight->LinkTo(control->GetFrame(), 0);
            list->m_highlight->SetPosInline(HIGHLIGHT_POS);
            list->m_highlight->SetOn(true);
            list->m_highlight->Update();
        }
        menu->OnItemHover(argument, control, notification, value);
        break;
    case CONTROL_NOTIFY_LEAVE:
        menu->OnItemHover(argument, control, notification, value);
        break;
    case CONTROL_NOTIFY_08:
        menu->OnItemNotify08(argument, control, notification, value);
        break;
    case CONTROL_NOTIFY_0A:
        menu->OnItemNotify0A(argument, control, notification, value);
        break;
    case CONTROL_NOTIFY_13:
        menu->OnItemNotify13(argument, control, notification, value);
        break;
    default:
        break;
    }
    return true;
}

bool C_StoreList::UpdateType(int typeId)
{
    C_ItemDef *def = GetItemDef(typeId);
    if (!def)
        return true;
    // (The stock asked only for a message.)
    const auto count = [&] { return static_cast<uint32_t>(m_stock->Get(typeId)); };
    if (def->GetRecordType() == ITEM_AMMO) {
        // The weapons it loads show it as their second count.
        for (size_t i = m_entries.size(); i-- > 0;) {
            C_ItemDef *weapon = GetItemDef(static_cast<int>(i));
            if (weapon && weapon->GetRecordType() == ITEM_ANIMATED && weapon->GetAmmoItemId() == typeId && m_entries[i] != 0)
                Post(m_entries[i], CONTROL_MSG_SET_SECOND_COUNT, count());
        }
    } else if (def->GetBehaviour() == BEHAVIOUR_HEADGEAR) {
        // The uniforms that come with it.
        for (size_t i = m_entries.size(); i-- > 0;) {
            C_ItemDef *uniform = GetItemDef(static_cast<int>(i));
            if (uniform && uniform->GetBehaviour() == BEHAVIOUR_UNIFORM && uniform->GetHeadgearItemId() == typeId &&
                m_entries[i] != 0)
                Post(m_entries[i], CONTROL_MSG_SET_SECOND_COUNT, count());
        }
    } else if (m_entries[static_cast<size_t>(typeId)] != 0) {
        Post(m_entries[static_cast<size_t>(typeId)], CONTROL_MSG_SET_ITEM_COUNT, count());
    }
    return true;
}

void C_StoreList::UpdateAll()
{
    for (int i = 0; i < MAX_ITEM_TYPES; ++i)
        UpdateType(i);
}

bool C_StoreList::UpdateAllIfStock()
{
    if (!m_stock)
        return false;
    UpdateAll();
    return true;
}

void C_StoreList::SetStock(C_ItemStock *stock)
{
    m_stock = stock;
    UpdateAllIfStock();
}

void C_StoreList::AddItem(const S_SoldierItem &item)
{
    m_stock->Add(item.typeId, item.present);
    UpdateType(item.typeId);
}

bool C_StoreList::TakeItem(S_SoldierItem &item, bool fill)
{
    if (fill) {
        const int have = m_stock->Get(item.typeId);
        if (have < item.present)
            m_stock->Add(item.typeId, item.present - have);
    }
    const int taken = m_stock->Take(item.typeId, item.present);
    if (taken == 0)
        return false;
    item.present = static_cast<int16_t>(taken);
    return UpdateType(item.typeId);
}

void C_StoreList::ClearPages()
{
    for (int page = 0; page < STORE_PAGE_COUNT; ++page)
        Post(BaseId() | static_cast<uint32_t>(page + STORE_FIRST_PAGE) << 12, CONTROL_MSG_HIDE_ALL);
}

void C_StoreList::SetActive(bool on)
{
    Post(BaseId() | STORE_TABS, on ? CONTROL_MSG_ACTIVATE : CONTROL_MSG_DEACTIVATE);
    Post(m_shownPage, on ? CONTROL_MSG_SHOW : CONTROL_MSG_HIDE);
}
