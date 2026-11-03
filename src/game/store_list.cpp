// The store's item lists of the equipment menus (original 0x0067aa70, 0x0067b070..0x0067c390).
#include "game/store_list.h"

#include <cstring>
#include <malloc.h>

#include "game/control.h"
#include "game/controls.h"
#include "game/equipment_menu.h"
#include "game/item_defs.h"
#include "game/items_module.h"
#include "game/menu_state.h"
#include "game/profile.h"
#include "game/screen.h"
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

// The page's frames in the screen's model, and the entries' kinds.
const char *const STORE_ANCHOR = "P_visualstore";
const char *const STORE_HIGHLIGHT = "selitem08";
const char *const STORE_TEMPLATE = "vi_template";
const char *const STORE_AMMO_FRAME = "ammo";
constexpr uint32_t FIND_VISUALS = 1;
constexpr uint32_t FIND_DUMMIES = 0x20;
constexpr int DESC_TYPE_PAGE = 0x23;
constexpr int DESC_TYPE_ENTRY = 0x1D;
// A page's horizontal step between its entries.
constexpr float PAGE_ACROSS_STEP = 0.081f;
// The teams of the items no store has.
constexpr int TEAM_NONE = 3;
// The inventory group whose head gear (behaviour 16) comes with the uniforms, not on its own.
constexpr int GROUP_UNIFORMS = 6;
constexpr int BEHAVIOUR_HEADGEAR_ITEM = 16;
constexpr int FIRST_GROUP = 1;
// The description's controls: the group, its tabs (their header's and their lines' styles, the
// groups' texts: none for group 4) and its scroll bar; the pages by their names' and groups.
constexpr int DESC_TYPE_GROUP = 5, DESC_TYPE_TABS = 0x11, DESC_TYPE_SCROLL_BAR = 8;
constexpr uint32_t TABS_HEADER_STYLE = 0xC, TAB_STYLE = 0x13;
constexpr uint32_t TAB_TEXTS[] = {0xA9F, 0xAA0, 0xAA1, 0xAA3, 0xAA4, 0xAA5};
struct S_StorePage {
    const char *name;
    int group;
};
constexpr S_StorePage STORE_PAGE_GROUPS[] = {{"GR_SmallWeapons", 1}, {"GR_MiddleWeapons", 2}, {"GR_HeavyWeapons", 3},
                                             {"GR_Explosives", 5},   {"GR_Dresses", 6},       {"GR_Other", 7}};

uintptr_t FloatValue(float value)
{
    uint32_t bits;
    std::memcpy(&bits, &value, sizeof bits);
    return bits;
}

// A visual copy with its own mesh.
void OwnMesh(I3D_frame *frame)
{
    auto *visual = static_cast<I3D_object_visual *>(frame);
    visual->DuplicateMesh();
    visual->GetVisualMesh()->Unknown18();
}

void Post(uint32_t target, uint32_t id, uint32_t value = 0)
{
    S_ControlMessage message;
    message.target = target;
    message.id = id;
    message.value = value;
    PostControlMessage(message);
}

} // namespace

void C_StoreList::BuildDescription(S_ScreenDesc *desc, I3D_frame *model)
{
    Describe(desc, model, "bscroll", STORE_HIGHLIGHT, true);
}

void C_StoreList::BuildEquipmentDescription(S_ScreenDesc *desc, I3D_frame *model)
{
    Describe(desc, model, "scroll01", "itemselector", false);
}

void C_StoreList::Describe(S_ScreenDesc *desc, I3D_frame *model, const char *scrollBar, const char *highlight,
                           bool inactive)
{
    m_screenIndex = desc->id >> 26;
    S_ControlDesc *group = desc->AddControl(DESC_TYPE_GROUP, STORE_ANCHOR);
    m_controlIndex = static_cast<int>(group->id >> 20 & 0x3F);
    S_ControlDesc *tabs = group->AddChild(DESC_TYPE_TABS, "filter");
    AddDescMessage(tabs, 0, CONTROL_MSG_ADD_CALLBACK, CONTROL_NOTIFY_0E, reinterpret_cast<uintptr_t>(this),
                   reinterpret_cast<uintptr_t>(&C_StoreList::OnTab));
    AddDescMessage(tabs, 0, CONTROL_MSG_SET_HEADER_TEXT_ID, TABS_HEADER_STYLE, TAB_TEXTS[0]);
    for (uint32_t text : TAB_TEXTS)
        AddDescMessage(tabs, 0, CONTROL_MSG_ADD_TEXT_ID, TAB_STYLE, text);
    if (inactive)
        AddDescMessage(tabs, 0, CONTROL_MSG_DEACTIVATE, 0);
    AddDescMessage(group->AddChild(DESC_TYPE_SCROLL_BAR, scrollBar), 0, CONTROL_MSG_SET_VALUE,
                   BaseId() | STORE_FIRST_PAGE << 12);
    for (int i = 0; i < MAX_ITEM_TYPES; ++i)
        m_entries.push_back(0);
    // (Their results not checked.)
    for (const S_StorePage &page : STORE_PAGE_GROUPS)
        BuildPage(group, model, C_fstr(page.name), page.group);
    // (Quirk kept: the highlight is not checked.)
    m_highlight = model->FindChildFrame(highlight, FIND_VISUALS);
    m_highlight->LinkTo(nullptr, 1);
    m_highlight->SetPosInline(S_vector(0.0f, 0.0f, 0.0f));
}

bool C_StoreList::BuildPage(S_ControlDesc *parent, I3D_frame *model, C_str name, int group)
{
    I3D_frame *dummy = I3DGetDriver()->CreateFrame(FRAME_DUMMY);
    I3D_frame *anchor = model->FindChildFrame(STORE_ANCHOR, FIND_DUMMIES);
    I3D_frame *highlight = model->FindChildFrame(STORE_HIGHLIGHT, FIND_VISUALS);
    m_highlight = model->FindChildFrame(STORE_HIGHLIGHT, FIND_VISUALS);
    m_template = model->FindChildFrame(STORE_TEMPLATE, FIND_VISUALS);
    // (The dummy is kept without the frames: leaked, as the original.)
    if (!m_template || !dummy || !highlight || !anchor)
        return false;
    // The page's dummy under the anchor, with the highlight's copy and the template.
    dummy->LinkTo(anchor, 0);
    static_cast<I3D_model *>(model)->AddModelFrame(dummy);
    dummy->SetPos(S_vector(0.0f, 0.0f, 0.0f));
    dummy->SetName(name.c_str());
    m_menu->GetModels().push_back(dummy);
    I3D_frame *copy = nullptr;
    CloneFrameTree(copy, highlight, nullptr);
    m_menu->GetFrames().push_back(copy);
    m_template->LinkTo(dummy, 1);
    m_template->SetOn(true);
    dummy->SetOn(true);

    S_ControlDesc *page = parent->AddChild(DESC_TYPE_PAGE, name.c_str());
    AddDescMessage(page, 0, CONTROL_MSG_SET_VALUE, BaseId() | STORE_PAGES);
    AddDescMessage(page, 0, CONTROL_MSG_SET_VISIBLE, 4);
    AddDescMessage(page, 0, CONTROL_MSG_SET_ACROSS_STEP, 0, FloatValue(PAGE_ACROSS_STEP));
    // The first entry's place: the template's (the page's control frees it).
    auto *origin = static_cast<S_vector4 *>(_aligned_malloc(sizeof(S_vector4), 16));
    if (origin)
        std::memcpy(origin, &m_template->GetPos(), sizeof(S_vector4));
    AddDescMessage(page, 0, CONTROL_MSG_SET_ORIGIN, 0, reinterpret_cast<uintptr_t>(origin));
    AddDescMessage(page, 0, CONTROL_MSG_HIDE, 0);
    if (group == FIRST_GROUP)
        m_shownPage = page->id;
    // (The menus size the entries for every type.)
    if (m_entries.size() < static_cast<size_t>(MAX_ITEM_TYPES))
        m_entries.resize(MAX_ITEM_TYPES);

    // An entry per item type of the group.
    for (int type = 0; type < MAX_ITEM_TYPES; ++type) {
        C_ItemDef *def = GetItemDef(type);
        if (!def || def->GetTeam() == TEAM_NONE || def->GetInventoryGroup() != group)
            continue;
        if (group == GROUP_UNIFORMS && def->GetBehaviour() == BEHAVIOUR_HEADGEAR_ITEM)
            continue;
        I3D_frame *visual = nullptr;
        CloneFrameTree(visual, m_template, nullptr);
        // Fix: the original kept the last type's copy in the menu's frames (a null the first time,
        // the last copy never: leaked).
        m_menu->GetFrames().push_back(visual);
        OwnMesh(visual);
        if (I3D_frame *ammo = visual->FindChildFrame(STORE_AMMO_FRAME, FIND_VISUALS))
            OwnMesh(ammo);
        const C_str frameName = C_fstr("frame_%d", type);
        visual->SetName(frameName.c_str());
        visual->LinkTo(dummy, 0);
        S_ControlDesc *entry = page->AddChild(DESC_TYPE_ENTRY, frameName.c_str());
        AddDescMessage(entry, 0, CONTROL_MSG_SET_ITEM, static_cast<uint32_t>(type));
        AddDescMessage(entry, 0, CONTROL_MSG_SET_INDEX, static_cast<uint32_t>(type));
        AddDescMessage(entry, 0, CONTROL_MSG_ADD_CALLBACK, CONTROL_NOTIFY_ANY, reinterpret_cast<uintptr_t>(this),
                       reinterpret_cast<uintptr_t>(&C_StoreList::OnItem));
        m_entries[static_cast<size_t>(type)] = entry->id;
    }
    AddDescMessage(page, 0, CONTROL_MSG_HIDE_ALL, 0);
    AddDescMessage(page, 0, CONTROL_MSG_REFRESH, 0);
    m_template->SetOn(false);
    m_template->Update();
    return true;
}

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
