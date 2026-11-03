// The equipment panel of the in-game menu and the equipment screen (original 0x0066f160..0x00672a30).
#include "game/equipment_panel.h"

#include <cstring>

#include "common/c_str.h"
#include "game/control.h"
#include "game/controls.h"
#include "game/equipment_menu.h"
#include "game/item_defs.h"
#include "game/menu_soldier.h"
#include "game/pending.h"
#include "game/profile.h"
#include "game/screen.h"
#include "ls3d/i3d.h"
#include "ls3d/igraph.h"

namespace {

// The highlights' frames, in m_highlights' order.
const char *const HIGHLIGHT_FRAMES[] = {
    "selhead", "seluniform", "selbck", "selshoulder", "selhands", "seldrop", "selholsters", "selbackpack",
};
// FindChildFrame's type mask for them (as the original passes it).
constexpr uint32_t HIGHLIGHT_MASK = 1;

// The kinds of the panel's controls (their ids' bits 12-19).
enum : uint32_t {
    PANEL_KIND_HEAD = 1,
    PANEL_KIND_UNIFORM = 2,
    PANEL_KIND_BACKPACK = 3,
    PANEL_KIND_LONG_GUN = 4,
    PANEL_KIND_HANDS = 5,
    PANEL_KIND_BELT = 7,                                // the group (its items: kind 8)
    PANEL_KIND_BELT_ITEM = 8,
    PANEL_KIND_BACKPACK_CONTENTS = 9,                   // the group (its items: kind 10)
    PANEL_KIND_BACKPACK_ITEM = 10,
};
uint32_t KindOf(uint32_t id) { return id >> 12 & 0xFF; }
uint32_t ChildOf(uint32_t id) { return id >> 4 & 0xFF; }

// The item that cannot be held in the hands (a type the hands place is masked off for).
constexpr int16_t ITEM_NOT_IN_HANDS = 0x105;

// Texts and styles.
constexpr int TEXT_WEIGHT = 0xAB4;                      // "Weight" (its "%s: %3.1f / %d Kg")
constexpr int TEXT_NO_BACKPACK = 0xAA7;
constexpr int TEXT_NO_ITEMS = 0x7D2;                    // an item control's count when none is left
constexpr uint32_t STYLE_LOAD = 0xB;
constexpr uint32_t STYLE_COUNT = 0x12;

// An item's inventory size as the belt and the backpack count it (0: 1, else twice).
float ItemVolume(const C_ItemDef *def)
{
    const float size = static_cast<float>(def->GetInventorySize());
    return size == 0.0f ? 1.0f : size + size;
}

uintptr_t FloatValue(float value)
{
    uint32_t bits;
    std::memcpy(&bits, &value, sizeof bits);
    return bits;
}

void Post(uint32_t target, uint32_t id, uint32_t value = 0, uintptr_t value2 = 0)
{
    S_ControlMessage message;
    message.target = target;
    message.id = id;
    message.value = value;
    message.value2 = value2;
    PostControlMessage(message);
}

void PostText(uint32_t target, uint32_t style, const C_str &text)
{
    S_ControlMessage message;
    message.target = target;
    message.id = CONTROL_MSG_SET_TEXT;
    message.value = style;
    message.text = text;
    PostControlMessage(message);
}

// The description's controls (kinds as CreateChild takes them).
enum : int {
    DESC_TYPE_BUTTON = 2,
    DESC_TYPE_ITEM = 4,
    DESC_TYPE_GROUP = 5,
    DESC_TYPE_SLIDER = 7,
    DESC_TYPE_PANE = 0xB,
    DESC_TYPE_TEXT = 0x13,
    DESC_TYPE_DESCRIPTION = 0x15,
    DESC_TYPE_BAR = 0x16,
};
// The places' controls, in their ids' kinds' order (1..5), and the drop place's.
const char *const PLACE_CONTROLS[] = {"head", "uniform", "back", "shoulder", "hands"};
constexpr int PLACE_UNIFORM = 1;
const char *const BELT_ITEMS[] = {"item00", "item01", "item02", "item03"};
const char *const BACKPACK_ITEMS[] = {"item04", "item05", "item06", "item07"};
// The item controls' texture to replace (its face group).
const char *const ITEM_TEXTURE = "X_SQUARE.BMP";
// The labels and their texts.
struct S_PanelLabel {
    const char *name;
    int textId;
};
const S_PanelLabel PANEL_LABELS[] = {
    {"text_back", 0xA96}, {"text_head", 0xA97}, {"text_drop", 0xA98}, {"text_hands", 0xA99},
    {"text_uniform", 0xA9A}, {"text_shoulder", 0xA9B}, {"text_holsters", 0xA9C}, {"text_backpack", 0xA9D},
};
// The bars' first values (the backpack's and the weight's 0.8).
constexpr float BELT_BAR_START = 0.5f;
constexpr float LOAD_BAR_START = 0.8f;
// FindChildFrame type masks the description's frames are looked for with.
constexpr uint32_t FIND_VISUALS = 1;
constexpr uint32_t FIND_DUMMIES = 0x20;

// An item control's description: its texture to replace and (with a menu) the menu's slots as its
// callbacks (not the uniform's first four).
void AddItemCallbacks(S_ControlDesc *item, C_EquipmentMenuBase *menu, bool all)
{
    AddDescMessage(item, 0, CONTROL_MSG_FIND_FACE_GROUP, 0, C_str(ITEM_TEXTURE));
    if (!menu)
        return;
    const auto object = reinterpret_cast<uintptr_t>(menu);
    const auto add = [&](uint32_t notification, ControlCallback function) {
        AddDescMessage(item, 0, CONTROL_MSG_ADD_CALLBACK, notification, object, reinterpret_cast<uintptr_t>(function));
    };
    if (all) {
        add(CONTROL_NOTIFY_13, &C_EquipmentMenuBase::CallItemNotify13);
        add(CONTROL_NOTIFY_0A, &C_EquipmentMenuBase::CallItemNotify0A);
        add(CONTROL_NOTIFY_08, &C_EquipmentMenuBase::CallItemNotify08);
        add(CONTROL_NOTIFY_09, &C_EquipmentMenuBase::CallItemNotify09);
    }
    add(CONTROL_NOTIFY_ENTER, &C_EquipmentMenuBase::CallItemHover);
    add(CONTROL_NOTIFY_LEAVE, &C_EquipmentMenuBase::CallItemHover);
}

// A scroll group of items: its bar, the arrows (off; their presses OnScroll), its caption, its slider
// (hidden).
void AddScrollGroup(S_ControlDesc *group, C_EquipmentPanel *panel, const char *bar, float start, const char *up,
                    const char *down, const char *caption, int captionText, const char *slider)
{
    S_ControlDesc *fill = group->AddChild(DESC_TYPE_BAR, bar);
    AddDescMessage(fill, 0, CONTROL_MSG_SET_BAR_AXIS, 2);
    AddDescMessage(fill, 0, CONTROL_MSG_SET_BAR_VALUE, 0, FloatValue(start));
    for (const char *arrow : {up, down}) {
        S_ControlDesc *button = group->AddChild(DESC_TYPE_BUTTON, arrow);
        AddDescMessage(button, 0, CONTROL_MSG_DEACTIVATE, 0);
        AddDescMessage(button, 0, CONTROL_MSG_ADD_CALLBACK, CONTROL_NOTIFY_PRESS, reinterpret_cast<uintptr_t>(panel),
                       reinterpret_cast<uintptr_t>(&C_EquipmentPanel::OnScroll));
    }
    S_ControlDesc *text = group->AddChild(DESC_TYPE_TEXT, caption);
    AddDescMessage(text, 0, CONTROL_MSG_SET_TEXT_ID, STYLE_LOAD, static_cast<uintptr_t>(captionText));
    AddDescMessage(group->AddChild(DESC_TYPE_SLIDER, slider), 0, CONTROL_MSG_HIDE, 0);
}

// An empty item (the "none" the putting functions give back).
S_SoldierItem NoItem()
{
    S_SoldierItem item;
    item.uid = -1;
    item.typeId = -1;
    item.present = 0;
    return item;
}

// The squad soldier's item at a single place, its type id -1 when empty.
const S_SoldierItem &PlaceItem(const S_SquadSoldier &soldier, uint32_t slot)
{
    switch (slot) {
    case ITEM_FITS_HEAD:
        return soldier.head;
    case ITEM_FITS_UNIFORM:
        return soldier.uniform;
    case ITEM_FITS_HANDS:
        return soldier.hands;
    case ITEM_FITS_BACKPACK:
        return soldier.backpack;
    default:
        return soldier.longGun;
    }
}

} // namespace

// ---- the description

void C_EquipmentPanel::BuildDescription(S_ScreenDesc *screen, I3D_frame *model)
{
    S_ControlDesc *panel = screen->AddControl(DESC_TYPE_GROUP, "P_Equipment");
    // The places (kinds 1..5) and the drop place (6).
    for (int i = 0; i < 5; ++i)
        AddItemCallbacks(panel->AddChild(DESC_TYPE_ITEM, PLACE_CONTROLS[i]), m_menu, i != PLACE_UNIFORM);
    panel->AddChild(DESC_TYPE_ITEM, "drop");
    // The belt: its scroll group (7) and items (8), the backpack's (9, 10).
    AddScrollGroup(panel->AddChild(DESC_TYPE_GROUP, "scroll13"), this, "hlcpct00", BELT_BAR_START, "barrow_up23",
                   "barrow_down23", "text_hlstrscap", 0xA9C, "slider12");
    S_ControlDesc *belt = panel->AddChild(DESC_TYPE_PANE, "GB_Holsters");
    AddDescMessage(belt, 0, CONTROL_MSG_SET_VISIBLE, 4);
    for (const char *name : BELT_ITEMS)
        AddItemCallbacks(belt->AddChild(DESC_TYPE_ITEM, name), m_menu, true);
    AddScrollGroup(panel->AddChild(DESC_TYPE_GROUP, "scroll14"), this, "hlcpct01", LOAD_BAR_START, "barrow_up24",
                   "barrow_down24", "text_bkpckcap", 0xA9D, "slider13");
    S_ControlDesc *backpack = panel->AddChild(DESC_TYPE_PANE, "GR_BackPack");
    AddDescMessage(backpack, 0, CONTROL_MSG_SET_VISIBLE, 4);
    for (const char *name : BACKPACK_ITEMS)
        AddItemCallbacks(backpack->AddChild(DESC_TYPE_ITEM, name), m_menu, true);
    // The capacity's text (11), the weight bar (12), the overweight bar (when the model has its frame).
    AddDescMessage(panel->AddChild(DESC_TYPE_TEXT, "text_cpcty"), 0, CONTROL_MSG_SET_TEXT_ID, STYLE_LOAD, TEXT_NO_ITEMS);
    S_ControlDesc *weight = panel->AddChild(DESC_TYPE_BAR, "hlcpct02");
    AddDescMessage(weight, 0, CONTROL_MSG_SET_BAR_AXIS, 2);
    AddDescMessage(weight, 0, CONTROL_MSG_SET_BAR_VALUE, 0, FloatValue(LOAD_BAR_START));
    m_weightBar = weight->id;
    if (model->FindChildFrame("overload", FIND_VISUALS)) {
        S_ControlDesc *overweight = panel->AddChild(DESC_TYPE_BAR, "overload");
        AddDescMessage(overweight, 0, CONTROL_MSG_SET_BAR_AXIS, 2);
        AddDescMessage(overweight, 0, CONTROL_MSG_SET_BAR_VALUE, 0, FloatValue(0.0f));
        m_overweightBar = overweight->id;
    }
    // The description text: its frame moved under the panel's dummy, hidden.
    I3D_frame *description = model->FindChildFrame("description", FIND_VISUALS);
    if (description) {
        if (I3D_frame *dummy = model->FindChildFrame("P_Equipment", FIND_DUMMIES)) {
            description->LinkTo(dummy, 1);
            description->SetOn(false);
            S_ControlDesc *text = panel->AddChild(DESC_TYPE_DESCRIPTION, "description");
            AddDescMessage(text, 0, CONTROL_MSG_SET_TEXT_ID, STYLE_LOAD, TEXT_NO_ITEMS);
            AddDescMessage(text, 0, CONTROL_MSG_HIDE, 0);
            m_descriptionText = text->id;
        }
    }
    for (const S_PanelLabel &label : PANEL_LABELS)
        AddDescMessage(panel->AddChild(DESC_TYPE_TEXT, label.name), 0, CONTROL_MSG_SET_TEXT_ID, STYLE_LOAD,
                       static_cast<uintptr_t>(label.textId));

    // The ids: the panel's with the kinds (bits 12-19) and children (bits 4-11) above.
    m_squadSoldier = nullptr;
    const uint32_t id = panel->id;
    m_headSlot = id | 0x1000;
    m_uniformSlot = id | 0x2000;
    m_backpackSlot = id | 0x3000;
    m_longGunSlot = id | 0x4000;
    m_handsSlot = id | 0x5000;
    m_dropSlot = id | 0x6000;
    m_beltBar = id | 0x7010;
    m_beltUp = id | 0x7020;
    m_beltDown = id | 0x7030;
    m_beltText = id | 0x7040;
    for (uint32_t i = 0; i < SLOT_COUNT; ++i)
        m_beltSlots[i] = id | 0x8000 | (i + 1) << 4;
    m_backpackBar = id | 0x9010;
    m_backpackUp = id | 0x9020;
    m_backpackDown = id | 0x9030;
    m_backpackText = id | 0x9040;
    for (uint32_t i = 0; i < SLOT_COUNT; ++i)
        m_backpackSlots[i] = id | 0xA000 | (i + 1) << 4;
    m_weightText = id | 0xB000;
    FindHighlights(model);
}

// ---- the menu's slots as the item controls' callbacks

bool C_EquipmentMenuBase::CallItemNotify13(void *menu, void *argument, C_Control *control, uint32_t notification,
                                           uint32_t value)
{
    return static_cast<C_EquipmentMenuBase *>(menu)->OnItemNotify13(argument, control, notification, value);
}

bool C_EquipmentMenuBase::CallItemNotify60(void *menu, void *argument, C_Control *control, uint32_t notification,
                                           uint32_t value)
{
    return static_cast<C_EquipmentMenuBase *>(menu)->OnItemNotify60(argument, control, notification, value);
}

bool C_EquipmentMenuBase::CallItemNotify0A(void *menu, void *argument, C_Control *control, uint32_t notification,
                                           uint32_t value)
{
    return static_cast<C_EquipmentMenuBase *>(menu)->OnItemNotify0A(argument, control, notification, value);
}

bool C_EquipmentMenuBase::CallItemNotify08(void *menu, void *argument, C_Control *control, uint32_t notification,
                                           uint32_t value)
{
    return static_cast<C_EquipmentMenuBase *>(menu)->OnItemNotify08(argument, control, notification, value);
}

bool C_EquipmentMenuBase::CallItemNotify09(void *menu, void *argument, C_Control *control, uint32_t notification,
                                           uint32_t value)
{
    return static_cast<C_EquipmentMenuBase *>(menu)->OnItemNotify09(argument, control, notification, value);
}

bool C_EquipmentMenuBase::CallItemHover(void *menu, void *argument, C_Control *control, uint32_t notification,
                                        uint32_t value)
{
    return static_cast<C_EquipmentMenuBase *>(menu)->OnItemHover(argument, control, notification, value);
}

// ---- the menu soldier

void C_EquipmentPanel::TickSoldier(int time)
{
    if (m_soldier)
        m_soldier->Tick(time);
}

void C_EquipmentPanel::RenderSoldier()
{
    if (m_soldier)
        m_soldier->Render();
}

void C_EquipmentPanel::CreateSoldier(I3D_visual *area)
{
    if (!area)
        return;
    // The visual's world bound in the menu's units (x 0..1 across, y 0..-0.75 down) to pixels; the
    // rectangle's top is the bound's top (its max y). (The pixels go through unsigned.)
    const I3D_bbox bound = area->GetWorldBound();
    const float scaleY = -4.0f / 3.0f;
    const auto left = static_cast<uint32_t>(static_cast<int>(static_cast<float>(GetIGraph()->ScrnX()) * bound.min.x));
    const auto bottom =
        static_cast<uint32_t>(static_cast<int>(static_cast<float>(GetIGraph()->ScrnY()) * bound.min.y * scaleY));
    const auto right = static_cast<uint32_t>(static_cast<int>(static_cast<float>(GetIGraph()->ScrnX()) * bound.max.x));
    const auto top =
        static_cast<uint32_t>(static_cast<int>(static_cast<float>(GetIGraph()->ScrnY()) * bound.max.y * scaleY));
    m_soldier = new C_MenuSoldier();                    // _aligned_malloc(0x210, 16) in the original
    m_soldier->Setup(static_cast<float>(left), static_cast<float>(top), static_cast<float>(right),
                     static_cast<float>(bottom));
    area->SetOn(false);
}

void C_EquipmentPanel::DeleteSoldier()
{
    if (m_soldier) {
        m_soldier->Release();
        m_soldier->ReleaseDivingGear();
        delete m_soldier;                               // _aligned_free in the original
    }
    m_soldier = nullptr;
}

void C_EquipmentPanel::UpdateFace()
{
    if (!m_soldier || !m_squadSoldier || m_squadSoldier->profileIndex < 0)
        return;
    const S_SoldierProfile *profile = g_gameProfile.m_humanStore->Get(m_squadSoldier->profileIndex);
    m_soldier->SetFace(profile->faceTexture.c_str());
}

void C_EquipmentPanel::AddToSoldier(uint32_t place, const S_SoldierItem &item)
{
    if (item.typeId == -1)
        return;
    for (int i = item.present; i != 0; --i)
        m_soldier->AddItem(place, item.typeId);
}

void C_EquipmentPanel::DressSoldier()
{
    if (!m_soldier || !m_squadSoldier)
        return;
    AddToSoldier(MENU_SOLDIER_HEAD, m_squadSoldier->head);
    AddToSoldier(MENU_SOLDIER_BODY, m_squadSoldier->uniform);
    AddToSoldier(MENU_SOLDIER_HANDS, m_squadSoldier->hands);
    AddToSoldier(MENU_SOLDIER_BACKPACK, m_squadSoldier->backpack);
    AddToSoldier(MENU_SOLDIER_LONG_GUN, m_squadSoldier->longGun);
    // The belt's and the backpack's, the last first.
    for (size_t i = m_squadSoldier->belt.size(); i-- > 0;)
        AddToSoldier(MENU_SOLDIER_HOLSTERS, m_squadSoldier->belt[i]);
    for (size_t i = m_squadSoldier->backpackContents.size(); i-- > 0;)
        AddToSoldier(MENU_SOLDIER_BACKPACK_CONTENTS, m_squadSoldier->backpackContents[i]);
}

// ---- the squad soldier's items

void C_EquipmentPanel::SelectSoldier(int index)
{
    const std::vector<S_SquadSoldier *> &states = g_gameProfile.m_missionSquad->states;
    if (index >= static_cast<int>(states.size())) {
        m_squadSoldier = nullptr;
        m_soldierIndex = -1;
        return;
    }
    m_squadSoldier = index < 0 ? nullptr : states[index];
    m_soldierIndex = index;
    // Fix: the original went on with a negative index's null soldier.
    if (!m_squadSoldier)
        return;
    m_beltScroll = 0;
    Post(m_beltUp, CONTROL_MSG_DEACTIVATE);
    Post(m_beltDown, m_squadSoldier->belt.size() < SLOT_COUNT + 1 ? CONTROL_MSG_DEACTIVATE : CONTROL_MSG_ACTIVATE);
    m_backpackScroll = 0;
    Post(m_backpackUp, CONTROL_MSG_DEACTIVATE);
    Post(m_backpackDown,
         m_squadSoldier->backpackContents.size() < SLOT_COUNT + 1 ? CONTROL_MSG_DEACTIVATE : CONTROL_MSG_ACTIVATE);
}

uint32_t C_EquipmentPanel::SlotOfControl(uint32_t id)
{
    switch (KindOf(id)) {
    case PANEL_KIND_HEAD:
        return ITEM_FITS_HEAD;
    case PANEL_KIND_UNIFORM:
        return ITEM_FITS_UNIFORM;
    case PANEL_KIND_BACKPACK:
        return ITEM_FITS_BACKPACK;
    case PANEL_KIND_LONG_GUN:
        return ITEM_FITS_LONG_GUN;
    case PANEL_KIND_HANDS:
        return ITEM_FITS_HANDS;
    case PANEL_KIND_BELT:
        return (id & 0xFF0) == 0 ? ITEM_FITS_BELT : 0;
    case PANEL_KIND_BELT_ITEM:
        return ChildOf(id) != 0 && ChildOf(id) < 5 ? ITEM_FITS_BELT : 0;
    case PANEL_KIND_BACKPACK_CONTENTS:
        return (id & 0xFF0) == 0 ? ITEM_FITS_BACKPACK_CONTENTS : 0;
    case PANEL_KIND_BACKPACK_ITEM:
        return ChildOf(id) != 0 && ChildOf(id) < 5 ? ITEM_FITS_BACKPACK_CONTENTS : 0;
    default:
        return 0;
    }
}

int C_EquipmentPanel::CountItems(uint32_t control, const S_SoldierItem &item) const
{
    if (!m_squadSoldier)
        return 0;
    return m_squadSoldier->CountItems(static_cast<uint8_t>(SlotOfControl(control)), item);
}

int C_EquipmentPanel::RemoveItem(uint32_t slot, S_SoldierItem &item)
{
    if (!m_squadSoldier)
        return 0;
    const int removed = m_squadSoldier->RemoveItem(static_cast<uint8_t>(slot), item);
    if (m_soldier) {
        for (int i = removed; i != 0; --i)
            m_soldier->RemoveItem(slot, item.typeId, false);
    }
    return removed;
}

int C_EquipmentPanel::RemoveItemAt(uint32_t control, S_SoldierItem &item)
{
    if (!m_squadSoldier)
        return 0;
    const uint32_t slot = SlotOfControl(control);
    if (slot == 0)
        return 0;
    const int removed = RemoveItem(slot, item);
    OnScroll(this, nullptr, nullptr, 0, 0);
    return removed;
}

S_SoldierItem C_EquipmentPanel::PutItem(uint32_t slot, const S_SoldierItem &item)
{
    S_SoldierItem displaced;
    m_squadSoldier->PutItem(&displaced, static_cast<uint8_t>(slot), item);
    if (m_soldier) {
        if (displaced.typeId == -1) {
            for (int i = item.present; i != 0; --i)
                m_soldier->AddItem(slot, item.typeId);
        } else if (displaced.typeId == item.typeId) {
            // The same type: the more now shown.
            if (displaced.present < item.present) {
                for (int i = item.present - displaced.present; i != 0; --i)
                    m_soldier->AddItem(slot, item.typeId);
            }
        } else {
            // (The uniform's model is replaced whole.)
            if (slot != ITEM_FITS_UNIFORM) {
                for (int i = displaced.present; i != 0; --i)
                    m_soldier->RemoveItem(slot, displaced.typeId, false);
            }
            for (int i = item.present; i != 0; --i)
                m_soldier->AddItem(slot, item.typeId);
        }
    }
    return displaced;
}

S_SoldierItem C_EquipmentPanel::PutItemChecked(uint32_t slot, const S_SoldierItem &item, bool force)
{
    if (!m_squadSoldier)
        return item;
    C_ItemDef *def = GetItemDef(item.typeId);
    if (!def)
        return NoItem();
    if ((slot & def->GetAllowedContainers()) == 0)
        return item;
    if (!force && (slot == ITEM_FITS_HEAD || slot == ITEM_FITS_UNIFORM || slot == ITEM_FITS_HANDS ||
                   slot == ITEM_FITS_BACKPACK || slot == ITEM_FITS_LONG_GUN) &&
        PlaceItem(*m_squadSoldier, slot).typeId != -1)
        return item;
    const S_SoldierItem displaced = PutItem(slot, item);
    OnScroll(this, nullptr, nullptr, 0, 0);
    return displaced;
}

S_SoldierItem C_EquipmentPanel::PutItemAt(uint32_t control, const S_SoldierItem &item)
{
    return PutItemChecked(SlotOfControl(control), item, true);
}

S_SoldierItem C_EquipmentPanel::DropItem(S_SoldierItem &item, bool force)
{
    if (!m_squadSoldier)
        return item;
    C_ItemDef *def = GetItemDef(item.typeId);
    if (!def)
        return item;
    // Too heavy: as many as the free weight carries, the rest given back at the end.
    int16_t rest = 0;
    const float weight = def->GetWeight();
    if (static_cast<float>(item.present) * weight > m_squadSoldier->GetFreeWeight()) {
        const int fits = static_cast<int>(m_squadSoldier->GetFreeWeight() / weight);
        rest = static_cast<int16_t>(item.present - fits);
        if (fits < 1) {
            if (!force)
                return item;
            rest = 0;
        } else {
            item.present = static_cast<int16_t>(fits);
        }
    }
    uint32_t allowed = def->GetAllowedContainers();
    if (item.typeId == ITEM_NOT_IN_HANDS)
        allowed &= ~ITEM_FITS_HANDS;
    const auto putAt = [&](uint32_t slot) {
        S_SoldierItem left = PutItem(slot, item);
        OnScroll(this, nullptr, nullptr, 0, 0);
        left.present = static_cast<int16_t>(left.present + rest);
        return left;
    };
    if ((allowed & ITEM_FITS_HEAD) && (m_squadSoldier->head.typeId == -1 || force))
        return putAt(ITEM_FITS_HEAD);
    if ((allowed & ITEM_FITS_UNIFORM) && (m_squadSoldier->uniform.typeId == -1 || force))
        return putAt(ITEM_FITS_UNIFORM);
    if ((allowed & ITEM_FITS_BELT) && item.present > 0) {
        const S_SoldierItem left = PutItem(ITEM_FITS_BELT, item);
        OnScroll(this, nullptr, nullptr, 0, 0);
        // Another type displaced: that is given back (the rest is lost: quirk kept).
        if (item.typeId != left.typeId)
            return left;
        item.present = left.present;
    }
    if ((allowed & ITEM_FITS_HANDS) && (m_squadSoldier->hands.typeId == -1 || force))
        return putAt(ITEM_FITS_HANDS);
    if ((allowed & ITEM_FITS_BACKPACK) && (m_squadSoldier->backpack.typeId == -1 || force))
        return putAt(ITEM_FITS_BACKPACK);
    if ((allowed & ITEM_FITS_LONG_GUN) && (m_squadSoldier->longGun.typeId == -1 || force))
        return putAt(ITEM_FITS_LONG_GUN);
    if ((allowed & ITEM_FITS_BACKPACK_CONTENTS) && item.present > 0) {
        const S_SoldierItem left = PutItem(ITEM_FITS_BACKPACK_CONTENTS, item);
        OnScroll(this, nullptr, nullptr, 0, 0);
        if (item.typeId != left.typeId)
            return left;
        item.present = left.present;
    }
    item.present = static_cast<int16_t>(item.present + rest);
    return item;
}

S_SoldierItem C_EquipmentPanel::MoveItem(S_SoldierItem &item, uint32_t from, uint32_t to)
{
    S_SoldierItem left = NoItem();
    if (SlotOfControl(from) == SlotOfControl(to))
        return left;                                    // (the original's count: whatever DX held)
    if (RemoveItemAt(from, item) <= 0)
        return left;
    if (to == m_dropSlot)
        return item;
    left = PutItemAt(to, item);
    if (left.typeId != -1) {
        // What the target displaced goes back to the source, else anywhere.
        left = PutItemAt(from, left);
        if (left.typeId != -1)
            left = DropItem(left, false);
    }
    return left;
}

bool C_EquipmentPanel::OnScroll(void *object, void *, C_Control *control, uint32_t, uint32_t)
{
    auto *panel = static_cast<C_EquipmentPanel *>(object);
    S_SquadSoldier *soldier = panel->m_squadSoldier;
    if (!soldier)
        return false;
    if (control) {
        const uint32_t id = control->GetId();
        if (id == panel->m_beltUp) {
            if (panel->m_beltScroll > 0)
                --panel->m_beltScroll;
        } else if (id == panel->m_beltDown) {
            if (static_cast<size_t>(panel->m_beltScroll) + SLOT_COUNT < soldier->belt.size())
                ++panel->m_beltScroll;
        } else if (id == panel->m_backpackUp) {
            if (panel->m_backpackScroll > 0)
                --panel->m_backpackScroll;
        } else if (id == panel->m_backpackDown) {
            if (static_cast<size_t>(panel->m_backpackScroll) + SLOT_COUNT < soldier->backpackContents.size())
                ++panel->m_backpackScroll;
        }
    }
    // The arrows on only when there is more to scroll to (the scroll kept within the items).
    const size_t belt = soldier->belt.size();
    if (belt < SLOT_COUNT + 1) {
        panel->m_beltScroll = 0;
        Post(panel->m_beltUp, CONTROL_MSG_DEACTIVATE);
        Post(panel->m_beltDown, CONTROL_MSG_DEACTIVATE);
    } else {
        Post(panel->m_beltUp, panel->m_beltScroll != 0 ? CONTROL_MSG_ACTIVATE : CONTROL_MSG_DEACTIVATE);
        if (belt < static_cast<size_t>(panel->m_beltScroll) + SLOT_COUNT)
            panel->m_beltScroll = static_cast<int>(belt) - SLOT_COUNT;
        Post(panel->m_beltDown, static_cast<size_t>(panel->m_beltScroll) + SLOT_COUNT == belt ? CONTROL_MSG_DEACTIVATE
                                                                                               : CONTROL_MSG_ACTIVATE);
    }
    const size_t contents = soldier->backpackContents.size();
    if (contents < SLOT_COUNT + 1) {
        panel->m_backpackScroll = 0;
        Post(panel->m_backpackUp, CONTROL_MSG_DEACTIVATE);
        Post(panel->m_backpackDown, CONTROL_MSG_DEACTIVATE);
    } else {
        Post(panel->m_backpackUp, panel->m_backpackScroll == 0 ? CONTROL_MSG_DEACTIVATE : CONTROL_MSG_ACTIVATE);
        if (contents < static_cast<size_t>(panel->m_backpackScroll) + SLOT_COUNT)
            panel->m_backpackScroll = static_cast<int>(contents) - SLOT_COUNT;
        Post(panel->m_backpackDown, static_cast<size_t>(panel->m_backpackScroll) + SLOT_COUNT < contents
                                        ? CONTROL_MSG_ACTIVATE
                                        : CONTROL_MSG_DEACTIVATE);
    }
    panel->Refresh();
    return true;
}

void C_EquipmentPanel::ShowItem(uint32_t control, const S_SoldierItem &item)
{
    C_ItemDef *def = GetItemDef(item.typeId);
    if (!def) {
        Post(control, CONTROL_MSG_RESTORE_TEXTURE);
        Post(control, CONTROL_MSG_SET_INDEX, static_cast<uint32_t>(-1));
    } else {
        Post(control, CONTROL_MSG_SET_TEXTURE, 0, reinterpret_cast<uintptr_t>(def->GetImage()));
        Post(control, CONTROL_MSG_SET_INDEX, static_cast<uint32_t>(item.typeId));
    }
    // The belt's and the backpack's items show their count too.
    const uint32_t kind = KindOf(control);
    if (kind != PANEL_KIND_BELT_ITEM && kind != PANEL_KIND_BACKPACK_ITEM)
        return;
    C_str text;
    if (!def || def->GetRecordType() != ITEM_AMMO) {
        text = C_fstr("%d ", item.present);
    } else {
        // Ammunition: the rounds and the magazines.
        const int amount = static_cast<int>(def->GetAmmoAmount());
        const int rounds = amount < 1 ? item.present : item.present * amount;
        text = C_fstr("%d/%d ", rounds, item.present);
    }
    if (item.present == 0)
        Post(control, CONTROL_MSG_SET_TEXT_ID, STYLE_COUNT, TEXT_NO_ITEMS);
    else
        PostText(control, STYLE_COUNT, text);
}

void C_EquipmentPanel::SetBackpackInactive(bool inactive)
{
    const uint32_t id = inactive ? CONTROL_MSG_DEACTIVATE : CONTROL_MSG_ACTIVATE;
    for (uint32_t slot : m_backpackSlots)
        Post(slot, id);
}

void C_EquipmentPanel::Refresh()
{
    if (!m_squadSoldier)
        return;
    ShowItem(m_headSlot, m_squadSoldier->head);
    ShowItem(m_uniformSlot, m_squadSoldier->uniform);
    ShowItem(m_longGunSlot, m_squadSoldier->longGun);
    ShowItem(m_handsSlot, m_squadSoldier->hands);
    ShowItem(m_backpackSlot, m_squadSoldier->backpack);
    SetBackpackInactive(m_squadSoldier->load.backpackMaxWeight == 0.0f);
    // The shown items of the belt and the backpack (past them: none).
    S_SoldierItem none;
    none.uid = -1;
    none.typeId = -1;
    none.present = 0;
    for (int i = 0; i < SLOT_COUNT; ++i) {
        const size_t at = static_cast<size_t>(m_beltScroll + i);
        ShowItem(m_beltSlots[i], at < m_squadSoldier->belt.size() ? m_squadSoldier->belt[at] : none);
    }
    for (int i = 0; i < SLOT_COUNT; ++i) {
        const size_t at = static_cast<size_t>(m_backpackScroll + i);
        ShowItem(m_backpackSlots[i],
                 at < m_squadSoldier->backpackContents.size() ? m_squadSoldier->backpackContents[at] : none);
    }
    UpdateLoad();
}

void C_EquipmentPanel::ReturnBackpack(C_ItemStock *stock)
{
    if (!m_squadSoldier || !stock)
        return;
    // The contents, the last first; then the backpack itself.
    for (size_t i = m_squadSoldier->backpackContents.size(); i-- > 0;) {
        S_SoldierItem item = m_squadSoldier->backpackContents[i];
        const int removed = RemoveItem(ITEM_FITS_BACKPACK_CONTENTS, item);
        stock->Add(item.typeId, removed);
    }
    S_SoldierItem backpack = m_squadSoldier->backpack;
    const int removed = RemoveItem(ITEM_FITS_BACKPACK, backpack);
    stock->Add(backpack.typeId, removed);
    Refresh();
}

void C_EquipmentPanel::ReturnItems(C_ItemStock *stock)
{
    if (!m_squadSoldier || !stock)
        return;
    m_squadSoldier->ReturnItemsTo(stock);
    Refresh();
    if (m_soldier)
        m_soldier->ResetItems();
}

void C_EquipmentPanel::FindHighlights(I3D_frame *model)
{
    for (int i = 0; i < HIGHLIGHT_COUNT; ++i) {
        m_highlights[i] = model->FindChildFrame(HIGHLIGHT_FRAMES[i], HIGHLIGHT_MASK);
        if (m_highlights[i])
            m_highlights[i]->SetOn(false);
    }
}

bool C_EquipmentPanel::IsLargeWeapon(int typeId)
{
    C_ItemDef *def = GetItemDef(typeId);
    return def && def->GetRecordType() == ITEM_ANIMATED && def->GetInventorySize() > 1 && def->GetBehaviour() != 6;
}

bool C_EquipmentPanel::HasLargeWeapon() const
{
    if (!m_squadSoldier)
        return false;
    if (m_squadSoldier->hands.typeId != -1 && m_squadSoldier->hands.present != 0 &&
        IsLargeWeapon(m_squadSoldier->hands.typeId))
        return true;
    return m_squadSoldier->longGun.typeId != -1 && m_squadSoldier->longGun.present != 0 &&
           IsLargeWeapon(m_squadSoldier->longGun.typeId);
}

// ---- the load and the highlights

void C_EquipmentPanel::UpdateLoad()
{
    if (!m_squadSoldier)
        return;
    const S_SoldierLoad &load = m_squadSoldier->load;
    // Over the capacity: the overweight bar shows it instead (when the panel has one).
    if (m_overweightBar == 0 || load.GetWeightFill() <= 1.0f) {
        Post(m_weightBar, CONTROL_MSG_SET_BAR_VALUE, 0, FloatValue(load.GetWeightFill()));
        Post(m_overweightBar, CONTROL_MSG_SET_BAR_VALUE, 0, FloatValue(0.0f));
    } else {
        Post(m_weightBar, CONTROL_MSG_SET_BAR_VALUE, 0, FloatValue(0.0f));
        Post(m_overweightBar, CONTROL_MSG_SET_BAR_VALUE, 0, FloatValue(load.GetWeightFill()));
    }
    const C_str weight = C_fstr("%s: %3.1f / %d Kg", g_texts.Get(TEXT_WEIGHT).c_str(),
                                static_cast<double>(load.weight), load.capacity);
    PostText(m_weightText, STYLE_LOAD, weight);
    Post(m_beltBar, CONTROL_MSG_SET_BAR_VALUE, 0, FloatValue(load.GetBeltFill()));
    PostText(m_beltText, STYLE_LOAD, C_fstr("%2.1f%%", static_cast<double>(load.GetBeltFill() * 100.0f)));
    if (load.backpackMaxWeight == 0.0f) {
        Post(m_backpackBar, CONTROL_MSG_SET_BAR_VALUE, 0, FloatValue(0.0f));
        Post(m_backpackText, CONTROL_MSG_SET_TEXT_ID, STYLE_LOAD, TEXT_NO_BACKPACK);
    } else {
        Post(m_backpackBar, CONTROL_MSG_SET_BAR_VALUE, 0, FloatValue(load.GetBackpackFill()));
        PostText(m_backpackText, STYLE_LOAD, C_fstr("%2.1f%%", static_cast<double>(load.GetBackpackFill() * 100.0f)));
    }
}

void C_EquipmentPanel::ShowAllowed(const S_SoldierItem &item, bool ignoreWeight)
{
    if (!m_squadSoldier)
        return;
    C_ItemDef *def = GetItemDef(item.typeId);
    uint32_t allowed = 0;
    if (def && (ignoreWeight || !(m_squadSoldier->GetFreeWeight() < def->GetWeight())))
        allowed = def->GetAllowedContainers();
    m_highlights[0]->SetOn((allowed & ITEM_FITS_HEAD) != 0);
    m_highlights[1]->SetOn((allowed & ITEM_FITS_UNIFORM) != 0);
    m_highlights[2]->SetOn((allowed & ITEM_FITS_BACKPACK) != 0);
    m_highlights[3]->SetOn((allowed & ITEM_FITS_LONG_GUN) != 0);
    m_highlights[4]->SetOn((allowed & ITEM_FITS_HANDS) != 0);
    // The belt and the backpack only with room (the drop place's highlight is left alone).
    m_highlights[6]->SetOn((allowed & ITEM_FITS_BELT) != 0 &&
                           ItemVolume(def) <= static_cast<float>(m_squadSoldier->GetFreeBeltVolume()));
    m_highlights[7]->SetOn((allowed & ITEM_FITS_BACKPACK_CONTENTS) != 0 &&
                           m_squadSoldier->load.backpackMaxWeight > 0.0f &&
                           ItemVolume(def) <= static_cast<float>(m_squadSoldier->GetFreeBackpackVolume()));
}
