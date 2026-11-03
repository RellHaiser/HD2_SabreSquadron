// The campaign's equipment screen (original 0x00673080..0x0067a3f0, 0x0067c570; part 9f-i: the class,
// its construction, starting and stopping, the item controls' slots and the stats; 9f-ii: the messages
// and the checks before the mission).
#include "game/equipment_screen.h"

#include <algorithm>
#include <cstring>
#include <vector>

#include "common/c_str.h"
#include "game/campaign.h"
#include "game/controls.h"
#include "game/cursor.h"
#include "game/item_defs.h"
#include "game/items_module.h"
#include "game/menu_state.h"
#include "game/pending.h"
#include "game/profile.h"
#include "game/screen.h"
#include "ls3d/i3d.h"

bool g_equipmentStatsShown;                             // 0x008ae61a
int g_requiredCarrier = -1;                            // 0x00840d5c

namespace {

// The items screen's controls by their first field: the equipment panel's (1st), the store's (4th);
// the panel's backpack place (SlotOfControl's); the dragged item's copy above its control.
constexpr uint32_t CONTROL_INDEX_MASK = 0x3F00000;
constexpr uint32_t PANEL_CONTROLS = 0x100000, STORE_CONTROLS = 0x400000;
constexpr uint32_t BACKPACK_SLOT = 8;
constexpr float DRAG_HEIGHT = 0.1f;
// The cursor's x past which an item dragged off the panel goes back to the store.
constexpr float STORE_SIDE = 0.5f;
// The hover's popup: the item's name's style.
constexpr uint32_t POPUP_NAME_STYLE = 3;
// The items screen's controls: the equipment panel, the stats panel (a skill's bar and value 1..3 + 3
// per skill), the stats button, the inventory's text, the soldier types; the stats' name (the second
// control's first child) and, without a profile, the selection screen's first control.
constexpr uint32_t PANEL = 0x04100000, STATS_PANEL = 0x04500000, STATS_BUTTON = 0x04600000,
                   INVENTORY_TEXT = 0x04700000, SOLDIER_TYPES = 0x04800000;
constexpr uint32_t NAME_TEXT = 0x04201000, SELECTION_TEXT = 0x08100000;
constexpr uint32_t BUTTON_STYLE = 0xC, TEXT_STATS = 0xAAF, TEXT_PANEL = 0xAB0;
constexpr uint32_t NAME_STYLE = 0xD, VALUE_STYLE = 0x17, BLANK_STYLE = 0xB, TEXT_BLANK = 0x7D2;
constexpr int SKILL_ROWS = 7;
// The screens (their indices: the ids' top 6 bits): the items screen, the selection screen.
constexpr int SCREEN_INDEX_SHIFT = 26;
constexpr uint32_t SCREEN_INDEX_MASK = 0x3F;
constexpr uint32_t SELECTION_SCREEN = 2;
// The screen's own messages (target 0): continue (the button), the selection's accept, the checks,
// done (the unarmed question's yes too).
enum : uint32_t {
    EQUIPMENT_MSG_CONTINUE = 0x200000B,
    EQUIPMENT_MSG_SELECTED = 0x200006A,
    EQUIPMENT_MSG_CHECK = 0x20000C1,
    EQUIPMENT_MSG_DONE = 0x20000C6,
};
// "ARE YOU SURE?", "Some soldier is overloaded, ...", "Some of your soldiers are unarmed. Do you
// wish to continue?", "At least one player has to have item '%s' in inventory".
constexpr uint32_t TEXT_BACK_QUESTION = 0x7D4, TEXT_OVERLOADED = 0x7E7, TEXT_UNARMED_QUESTION = 0x2531;
constexpr int TEXT_ITEM_MISSING = 0x804;
// The required items' kinds counted once per soldier at most: the uniforms, the diving gear; the
// items of one carrier: the axis' (their team).
constexpr int BEHAVIOUR_UNIFORM = ITEM_BEHAVIOUR_UNIFORM, DIVING_GEAR = 231, ITEM_TEAM_AXIS = 1;

void Post(uint32_t target, uint32_t id, uint32_t value, uintptr_t value2 = 0, const C_str &text = C_str())
{
    S_ControlMessage message;
    message.target = target;
    message.id = id;
    message.value = value;
    message.value2 = value2;
    message.text = text;
    PostControlMessage(message);
}

// A float as a message's value2.
uintptr_t FloatValue(float value)
{
    uint32_t bits;
    std::memcpy(&bits, &value, sizeof bits);
    return bits;
}

// The squad's soldier `index` (none: null).
S_SquadSoldier *SquadSoldier(int index)
{
    const std::vector<S_SquadSoldier *> &squad = g_gameProfile.m_missionSquad->states;
    return index >= 0 && static_cast<size_t>(index) < squad.size() ? squad[static_cast<size_t>(index)] : nullptr;
}

} // namespace

// ---- the menu

C_EquipmentMenu::C_EquipmentMenu()
{
    m_presets.Load();
    m_missionItems.Setup();
}

C_EquipmentMenu::~C_EquipmentMenu() = default;

bool C_EquipmentMenu::OnStart(void *arg)
{
    // Dropped: the copy protection's bookkeeping (the game window's handle and values of it).
    C_ItemStock *stock = g_gameProfile.m_itemStock18;
    if (!arg)
        return true;
    g_itemsModule.EnsureItemsLoaded();
    if (m_mode != C_CampaignScreen::KIND_EQUIPMENT)
        return true;
    stock->Reset();
    for (const S_CampaignItem &item : static_cast<const C_CampaignScreen *>(arg)->GetItems()) {
        if (!item.ammunition) {
            stock->Add(item.itemId, item.count);
        } else if (C_ItemDef *def = GetItemDef(item.itemId)) {
            stock->Add(def->GetAmmoItemId(), item.count);
        }
    }
    return true;
}

void C_EquipmentMenu::OnStop()
{
    if (m_popup) {
        delete m_popup;
        m_popup = nullptr;
    }
    m_panel.DeleteSoldier();
    m_storeList.ClearPages();
    // Dropped: the copy protection (from the fourth mission, a cracked copy's difficulty set to 4 by
    // chance).
}

void C_EquipmentMenu::SetupStore()
{
    g_itemsModule.EnsureItemsLoaded();
    if (!g_gameProfile.m_playMode->singleMission) {
        if (m_mode == C_CampaignScreen::KIND_EQUIPMENT)
            m_storeList.SetStock(g_gameProfile.m_itemStock18);
        // (The original loads the items again.)
        if (m_mode == 4)
            m_storeList.SetStock(g_gameProfile.m_itemStock1C);
        return;
    }
    m_storeList.SetStock(g_gameProfile.m_itemStock18);
    TakeSquadItems();
}

void C_EquipmentMenu::TakeSquadItems()
{
    // A place's item the stock has not: dropped.
    const auto take = [this](S_SoldierItem &item) {
        if (item.typeId == -1 || m_storeList.TakeItem(item, false))
            return true;
        item.uid = -1;
        item.typeId = -1;
        item.present = 0;
        return false;
    };
    const auto takeAll = [this](S_SquadSoldier *soldier, std::vector<S_SoldierItem> &items, uint8_t slot) {
        for (size_t i = items.size(); i-- > 0;) {
            if (!m_storeList.TakeItem(items[i], false))
                soldier->RemoveItem(slot, items[i]);
        }
    };
    const std::vector<S_SquadSoldier *> &squad = g_gameProfile.m_missionSquad->states;
    for (size_t i = squad.size(); i-- > 0;) {
        S_SquadSoldier *soldier = squad[i];
        take(soldier->head);
        take(soldier->uniform);
        take(soldier->longGun);
        take(soldier->hands);
        takeAll(soldier, soldier->belt, SQUAD_SLOT_BELT);
        if (!take(soldier->backpack))
            soldier->backpackContents.clear();
        takeAll(soldier, soldier->backpackContents, SQUAD_SLOT_BACKPACK_CONTENTS);
        soldier->UpdateLoad();
    }
}

// ---- the item controls' slots

bool C_EquipmentMenu::OnItemNotify13(void *, C_Control *control, uint32_t, uint32_t)
{
    const int type = control->GetIndex();
    if (type < 0)
        return true;
    S_SoldierItem item;
    item.typeId = static_cast<int16_t>(type);
    item.present = 1;
    if ((control->GetId() & CONTROL_INDEX_MASK) != PANEL_CONTROLS) {
        if (!m_storeList.TakeItem(item, false))
            return false;
        const S_SoldierItem displaced = m_panel.DropItem(item, false);
        if (displaced.typeId != -1)
            m_storeList.AddItem(displaced.typeId == item.typeId ? item : displaced);
        return true;
    }
    if (C_EquipmentPanel::SlotOfControl(control->GetId()) == BACKPACK_SLOT) {
        m_panel.ReturnBackpack(m_storeList.GetStock());
        m_storeList.UpdateAll();
    } else if (m_panel.RemoveItemAt(control->GetId(), item) != 0) {
        m_storeList.AddItem(item);
    }
    // The cursor's input: left the control, then 05.
    Post(0, CONTROL_MSG_NOTIFY, CONTROL_NOTIFY_LEAVE);
    Post(0, CONTROL_MSG_NOTIFY, CONTROL_NOTIFY_05);
    return true;
}

bool C_EquipmentMenu::OnItemNotify0A(void *, C_Control *control, uint32_t, uint32_t)
{
    S_ControlMessage index;
    index.id = CONTROL_MSG_GET_INDEX;
    control->HandleMessage(&index);
    const auto type = static_cast<int>(index.value);
    if (type < 0)
        return true;
    m_dragType = type;
    m_dragItem.typeId = static_cast<int16_t>(type);
    m_dragItem.present = 1;
    m_panel.ShowAllowed(m_dragItem, (control->GetId() & CONTROL_INDEX_MASK) == PANEL_CONTROLS);
    // The dragged copy: the control's visual, above it.
    I3D_frame *frame = control->GetFrame();
    m_dragVisual = I3DGetDriver()->CreateVisual(frame->GetVisualType());
    if (!m_dragVisual)
        return true;
    m_dragVisual->Duplicate(frame);
    m_dragVisual->LinkTo(m_scene->GetPrimarySector(), 0);
    m_dragVisual->SetDir(S_vector(0.0f, 0.0f, 1.0f), 0.0f);
    const S_vector &at = frame->GetWorldPos();
    m_dragVisual->SetPos(S_vector(at.x, at.y, at.z + DRAG_HEIGHT));
    m_dragVisual->SetOn(true);
    m_dragVisual->Update();
    return true;
}

bool C_EquipmentMenu::OnItemNotify08(void *, C_Control *, uint32_t, uint32_t)
{
    if (m_dragType == -1)
        return true;
    // (Quirk kept: the copy's depth the move's.)
    const S_vector &at = m_dragVisual->GetWorldPos();
    S_vector move;
    m_cursor->GetMove(&move);
    m_dragVisual->SetPos(S_vector(at.x + move.x, at.y + move.y, move.z));
    m_dragVisual->Update();
    return true;
}

bool C_EquipmentMenu::OnItemNotify09(void *, C_Control *control, uint32_t, uint32_t)
{
    if (m_dragVisual)
        m_dragVisual->Release();
    m_dragVisual = nullptr;
    if (m_dragType == -1)
        return true;
    S_SoldierItem none;
    none.typeId = -1;
    m_panel.ShowAllowed(none, false);
    I3D_frame *picked = PickFrame();
    DropItem(control, picked ? static_cast<uint32_t>(reinterpret_cast<uintptr_t>(picked->GetData())) : 0);
    m_dragItem.typeId = -1;
    m_dragItem.present = 0;
    m_dragType = -1;
    return true;
}

bool C_EquipmentMenu::DropItem(C_Control *control, uint32_t target)
{
    S_SoldierItem item = m_dragItem;
    const uint32_t from = control->GetId();
    const bool onPanel = target != 0 && (target & CONTROL_INDEX_MASK) == PANEL_CONTROLS;
    if ((from & CONTROL_INDEX_MASK) == PANEL_CONTROLS) {
        if (onPanel) {
            const S_SoldierItem displaced = m_panel.MoveItem(item, from, target);
            if (displaced.typeId != -1)
                m_storeList.AddItem(displaced);
        }
        // (Moved or not.)
        S_vector cursor;
        m_cursor->GetPosition(&cursor);
        if (!(cursor.x > STORE_SIDE))
            return true;
        if (C_EquipmentPanel::SlotOfControl(from) == BACKPACK_SLOT) {
            m_panel.ReturnBackpack(m_storeList.GetStock());
            m_storeList.UpdateAll();
            return true;
        }
        item.present = static_cast<int16_t>(m_panel.CountItems(from, item));
        if (m_panel.RemoveItemAt(from, item) != 0)
            m_storeList.AddItem(item);
        return true;
    }
    if (onPanel) {
        const S_SoldierItem displaced = m_panel.PutItemAt(target, item);
        if (displaced.typeId == -1) {
            m_storeList.TakeItem(item, false);
        } else if (displaced.typeId != item.typeId) {
            m_storeList.TakeItem(item, false);
            m_storeList.AddItem(displaced);
        }
    }
    return true;
}

bool C_EquipmentMenu::OnItemHover(void *, C_Control *control, uint32_t notification, uint32_t)
{
    if (!m_popup || !control || (control->GetId() & CONTROL_INDEX_MASK) == STORE_CONTROLS)
        return true;
    S_ControlMessage message;
    if (notification == CONTROL_NOTIFY_ENTER) {
        C_ItemDef *def = GetItemDef(control->GetIndex());
        if (!def)
            return true;
        message.id = CONTROL_MSG_SET_TEXT_ID;
        message.value = POPUP_NAME_STYLE;
        message.value2 = static_cast<uintptr_t>(def->GetTextId());
        m_popup->HandleMessage(&message);
        // (The original's show carries the text id as its value2.)
        message.id = CONTROL_MSG_SHOW;
        message.value = 0;
    } else if (notification == CONTROL_NOTIFY_LEAVE) {
        message.id = CONTROL_MSG_HIDE;
    } else {
        return true;
    }
    m_popup->HandleMessage(&message);
    return true;
}

// ---- the stats and the soldiers picked

void C_EquipmentMenu::ShowSoldierStats()
{
    // (Quirk kept: the soldier read unchecked.)
    const S_SoldierProfile *profile = g_gameProfile.m_humanStore->Get(SquadSoldier(m_soldier)->profileIndex);
    // A skill's row (1..7): its bar and its value's text.
    const auto bar = [](int row) { return STATS_PANEL | static_cast<uint32_t>(3 * row - 1) << 12; };
    if (!profile) {
        for (int row = 1; row <= SKILL_ROWS; ++row) {
            Post(bar(row), CONTROL_MSG_SET_BAR_VALUE, 0, FloatValue(0.0f));
            Post(bar(row) + 0x1000, CONTROL_MSG_SET_TEXT_ID, BLANK_STYLE, TEXT_BLANK);
        }
        Post(SELECTION_TEXT, CONTROL_MSG_SET_TEXT_ID, NAME_STYLE, TEXT_BLANK);
        return;
    }
    Post(NAME_TEXT, CONTROL_MSG_SET_TEXT, NAME_STYLE, 0,
         profile->customName.IsEmpty() ? profile->fullName : profile->customName);
    const auto row = [&bar](int skill, float fill, const char *format, float value) {
        Post(bar(skill), CONTROL_MSG_SET_BAR_VALUE, 0, FloatValue(fill));
        Post(bar(skill) + 0x1000, CONTROL_MSG_SET_TEXT, VALUE_STYLE, 0, C_fstr(format, static_cast<double>(value)));
    };
    const float *stats = profile->stats;
    row(1, stats[STAT_SHOOTING] * 0.01f, "%3.0f %%", stats[STAT_SHOOTING]);
    row(2, stats[STAT_STRENGTH] * 0.01f, "%3.0f Kg", stats[STAT_STRENGTH]);
    row(3, 1.0f, "%3.0f", stats[STAT_HEALTH]);
    row(4, stats[STAT_ENDURANCE] * 0.01f, "%3.0f", stats[STAT_ENDURANCE]);
    row(5, stats[STAT_STEALTH] * 0.01f, "%3.0f %%", stats[STAT_STEALTH]);
    row(6, stats[STAT_FIRST_AID] * 0.01f, "%3.0f %%", stats[STAT_FIRST_AID]);
    row(7, stats[STAT_LOCKPICK] * 0.01f, "%3.0f %%", stats[STAT_LOCKPICK]);
}

bool C_EquipmentMenu::OnStatsToggle(void *, C_Control *, uint32_t, uint32_t)
{
    g_equipmentStatsShown = !g_equipmentStatsShown;
    if (g_equipmentStatsShown) {
        ShowSoldierStats();
        Post(STATS_PANEL, CONTROL_MSG_SHOW, 0);
        Post(PANEL, CONTROL_MSG_HIDE, 0);
        Post(STATS_BUTTON, CONTROL_MSG_SET_TEXT_ID, BUTTON_STYLE, TEXT_STATS);
        Post(INVENTORY_TEXT, CONTROL_MSG_HIDE, 0);
        Post(SOLDIER_TYPES, CONTROL_MSG_HIDE, 0);
    } else {
        m_panel.Refresh();
        Post(PANEL, CONTROL_MSG_SHOW, 0);
        Post(STATS_PANEL, CONTROL_MSG_HIDE, 0);
        Post(STATS_BUTTON, CONTROL_MSG_SET_TEXT_ID, BUTTON_STYLE, TEXT_PANEL);
        Post(INVENTORY_TEXT, CONTROL_MSG_SHOW, 0);
        Post(SOLDIER_TYPES, CONTROL_MSG_SHOW, 0);
    }
    return true;
}

bool C_EquipmentMenu::OnSelectionPicked(void *, C_Control *control, uint32_t, uint32_t)
{
    const S_vector &at = control->GetFrame()->GetWorldPos();
    m_selector->SetPos(S_vector(at.x, at.y, m_selector->GetWorldPos().z));
    S_ControlMessage index;
    index.id = CONTROL_MSG_GET_INDEX;
    control->HandleMessage(&index);
    m_selected = static_cast<int>(index.value);
    return true;
}

bool C_EquipmentMenu::OnSoldierPicked(void *menu, void *, C_Control *control, uint32_t, uint32_t)
{
    auto *self = static_cast<C_EquipmentMenu *>(menu);
    const S_vector &at = control->GetFrame()->GetWorldPos();
    self->m_soldierSelector->SetPos(S_vector(at.x, at.y, self->m_soldierSelector->GetWorldPos().z));
    S_ControlMessage index;
    index.id = CONTROL_MSG_GET_INDEX;
    control->HandleMessage(&index);
    self->m_soldier = static_cast<int>(index.value);
    self->m_panel.SelectSoldier(self->m_soldier);
    self->m_panel.Refresh();
    self->ShowSoldierStats();
    return true;
}

bool C_EquipmentMenu::CallStatsToggle(void *menu, void *argument, C_Control *control, uint32_t notification,
                                      uint32_t value)
{
    return static_cast<C_EquipmentMenu *>(menu)->OnStatsToggle(argument, control, notification, value);
}

bool C_EquipmentMenu::CallSelectionPicked(void *menu, void *argument, C_Control *control, uint32_t notification,
                                          uint32_t value)
{
    return static_cast<C_EquipmentMenu *>(menu)->OnSelectionPicked(argument, control, notification, value);
}

// ---- the messages

void C_EquipmentMenu::OnMessage(S_ControlMessage *message)
{
    std::vector<S_SquadSoldier *> &squad = g_gameProfile.m_missionSquad->states;
    switch (message->id & CONTROL_MSG_MASK) {
    case MENU_MSG_ESCAPE:
        if (g_menuScreens.empty())
            return;
        // On the selection screen: closed; else asked whether to go back to the campaign's previous
        // screen.
        if ((g_menuScreens.back()->GetId() >> SCREEN_INDEX_SHIFT & SCREEN_INDEX_MASK) != SELECTION_SCREEN) {
            ShowMessageId(MESSAGE_BOX_YES_NO, TEXT_BACK_QUESTION, MENU_MSG_PREVIOUS, 0);
            return;
        }
        Post(0, CONTROL_NOTIFY_CLICK | MENU_MSG_CLOSE_POPUP, 0);
        return;
    case EQUIPMENT_MSG_CONTINUE: {
        // The first campaign's second and fifteenth missions with more than one soldier: the one to
        // play picked on the selection screen; else every soldier goes.
        C_Campaign *campaign = g_gameProfile.m_campaign;
        const int index = campaign->GetCurrentIndex();
        const int mission = campaign->GetMissionNumber(index);
        if (index == 0 && (mission == 2 || mission == 15) && squad.size() > 1) {
            Post(0, MENU_MSG_OPEN_POPUP, SELECTION_SCREEN);
            return;
        }
        for (S_SquadSoldier *soldier : squad)
            soldier->deployed = true;
        Post(0, EQUIPMENT_MSG_CHECK, 0);
        return;
    }
    case EQUIPMENT_MSG_SELECTED: {
        for (S_SquadSoldier *soldier : squad)
            soldier->deployed = false;
        if (m_selected >= static_cast<int>(squad.size()))
            return;
        // (Fix: a negative pick ignored; the original wrote through null.)
        S_SquadSoldier *picked = SquadSoldier(m_selected);
        if (!picked)
            return;
        picked->deployed = true;
        Post(0, EQUIPMENT_MSG_CHECK, 0);
        return;
    }
    case EQUIPMENT_MSG_CHECK:
        if (!HasRequiredItems())
            return;
        if (!IsSquadWithinWeight()) {
            ShowMessageId(MESSAGE_BOX_OK, TEXT_OVERLOADED, 0, 0);
            return;
        }
        if (!IsSquadArmed()) {
            ShowMessageId(MESSAGE_BOX_YES_NO, TEXT_UNARMED_QUESTION, EQUIPMENT_MSG_DONE, 0);
            return;
        }
        Post(0, EQUIPMENT_MSG_DONE, 0);
        return;
    case EQUIPMENT_MSG_DONE:
        for (S_SquadSoldier *soldier : squad)
            soldier->autoEquip = false;
        // Dropped: the copy protection (a cracked copy's belt and backpack items of no record type
        // removed; from the twelfth mission back to the previous screen).
        Post(0, MENU_MSG_NEXT, 0);
        g_gameProfile.m_itemStock1C->Reset();
        return;
    default:
        return;
    }
}

// ---- the checks before the mission

bool C_EquipmentMenu::IsSquadWithinWeight()
{
    for (S_SquadSoldier *soldier : g_gameProfile.m_missionSquad->states) {
        if (soldier->load.GetWeightFill() > 1.0f)
            return false;
    }
    return true;
}

bool C_EquipmentMenu::IsSquadArmed()
{
    // (Quirk kept: an item's definition read unchecked.)
    const auto armed = [](const S_SoldierItem &item) {
        return item.typeId != -1 && GetItemDef(item.typeId)->GetRecordType() == ITEM_ANIMATED;
    };
    const auto anyArmed = [&armed](const std::vector<S_SoldierItem> &items) {
        return std::any_of(items.rbegin(), items.rend(), armed);
    };
    const std::vector<S_SquadSoldier *> &squad = g_gameProfile.m_missionSquad->states;
    for (size_t i = squad.size(); i-- > 0;) {
        const S_SquadSoldier *soldier = squad[i];
        if (!soldier->deployed)
            continue;
        if (!armed(soldier->hands) && !armed(soldier->longGun) && !anyArmed(soldier->belt) &&
            !anyArmed(soldier->backpackContents))
            return false;
    }
    return true;
}

int C_EquipmentMenu::CountCarried(const std::pair<int, int> &item, int soldier)
{
    // (Quirk kept: the item's definition read unchecked.)
    const bool anyCarrier = GetItemDef(item.first)->GetTeam() != ITEM_TEAM_AXIS;
    int total = 0, own = 0;
    const std::vector<S_SquadSoldier *> &squad = g_gameProfile.m_missionSquad->states;
    for (int i = 0; i < static_cast<int>(squad.size()); ++i) {
        const S_SquadSoldier *carrier = squad[static_cast<size_t>(i)];
        if (!carrier->deployed)
            continue;
        // An axis item counts with its first carrier only (the one kept).
        const auto counts = [&](const S_SoldierItem &place) {
            if (place.typeId != item.first)
                return false;
            if (!anyCarrier) {
                if (g_requiredCarrier != -1 && g_requiredCarrier != i)
                    return false;
                g_requiredCarrier = i;
            }
            return true;
        };
        // A place counts once, the belt's and the backpack's items by their counts.
        for (const S_SoldierItem *place : {&carrier->head, &carrier->uniform, &carrier->backpack, &carrier->longGun,
                                           &carrier->hands}) {
            if (counts(*place)) {
                ++total;
                if (i == soldier)
                    own += place->present;
            }
        }
        for (const std::vector<S_SoldierItem> *items : {&carrier->belt, &carrier->backpackContents}) {
            for (const S_SoldierItem &held : *items) {
                if (counts(held)) {
                    total += held.present;
                    if (i == soldier)
                        own += held.present;
                }
            }
        }
    }
    if (total < item.second)
        return 0;
    if (soldier < 0)
        return total;
    return std::min(own, total - item.second);
}

bool C_EquipmentMenu::HasRequiredItems()
{
    C_Campaign *campaign = g_gameProfile.m_campaign;
    S_MissionItems *record = m_missionItems.GetMissionRecord(campaign->GetMissionNumber(campaign->GetCurrentIndex()));
    g_requiredCarrier = -1;
    // (Fix: no record, nothing required; the original read it unchecked.)
    if (!record)
        return true;
    const int squadSize = static_cast<int>(g_gameProfile.m_missionSquad->states.size());
    for (std::pair<int, int> &item : record->required) {
        // (Quirk kept: the item's definition read unchecked.) A uniform or the diving gear: one per
        // soldier at most (the record changed).
        C_ItemDef *def = GetItemDef(item.first);
        if ((def->GetBehaviour() == BEHAVIOUR_UNIFORM || item.first == DIVING_GEAR) && squadSize < item.second)
            item.second = squadSize;
        if (CountCarried(item, -1) == 0) {
            const C_str name = g_texts.Get(def->GetTextId());
            ShowMessage(MESSAGE_BOX_OK, C_fstr(g_texts.Get(TEXT_ITEM_MISSING).c_str(), name.c_str()), 0, 0);
            return false;
        }
    }
    return true;
}
