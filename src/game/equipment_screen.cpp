// The campaign's equipment screen (original 0x00673080..0x0067a3f0, 0x0067c570; part 9f-i: the class,
// its construction, starting and stopping, the item controls' slots and the stats; 9f-ii: the messages
// and the checks before the mission; 9f-iii: the screens' descriptions; 9f-iv: the soldier type picked;
// 9f-v: the automatic equipping).
#include "game/equipment_screen.h"

#include <algorithm>
#include <climits>
#include <cstring>
#include <vector>

#include "common/c_str.h"
#include "game/campaign.h"
#include "game/controls.h"
#include "game/cursor.h"
#include "game/item_defs.h"
#include "game/items.h"
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
// The screens' descriptions: the items screen (index 1); the controls' kinds; the texts' styles (the
// buttons', the lists' headers, the soldier types' lines, the screen's title, the version's) and texts
// (the version, the soldier types, the skills' names 0xA6E + the skill, "Soldier stats :").
constexpr uint32_t ITEMS_SCREEN = 1;
constexpr int CONTROL_PICTURE = 4, CONTROL_GROUP = 5, CONTROL_LIST = 0xB, CONTROL_COMBO = 0x11, CONTROL_LABEL = 0x12,
              CONTROL_TEXT = 0x13, CONTROL_BUTTON = 0x14, CONTROL_DESCRIPTION = 0x15, CONTROL_BAR = 0x16;
constexpr uint32_t BUTTON_TEXT_STYLE = 0xD, LIST_STYLE = 0xC, LIST_ITEM_STYLE = 0x13, SCREEN_TITLE_STYLE = 0xA,
                   VERSION_STYLE = 0x28;
constexpr uint32_t TEXT_VERSION = 0x7D6, TEXT_SOLDIER_TYPES = 500, TEXT_SKILLS = 0xA6E, TEXT_SOLDIER_STATS = 0xAB3;
constexpr uint32_t BAR_AXIS_Z = 2;
// The soldier types: the presets of the campaign (the user's kind).
constexpr uint32_t CAMPAIGN_PRESETS = LOADOUT_USER | 1;
// A new squad's soldiers (the first profiles); the squad list's pictures (4 shown, 0.106 apart); the
// selection screen's.
constexpr int DEFAULT_SQUAD = 4, SQUAD_LIST_ROWS = 4, SELECTION_PICTURES = 4;
constexpr float SQUAD_ACROSS_STEP = 0.106f;
// The substitutes' scoring (the in-game menu's): the items whose ids pair up, the behaviour never
// matched, the scope's reticles' aim modes, the small behaviours; a preset's belt's and backpack's
// item types as matched (13 and 31); "Item is not in the list of available items".
constexpr int PAIRED_ITEMS[] = {0x16, 7, 0x104};
constexpr int UNMATCHED_BEHAVIOUR = 0x13, RETICLE_MODES = 4, SMALL_BEHAVIOURS = 7, BEHAVIOUR_12 = 12;
constexpr int PRESET_BELT_TYPES = 13, PRESET_BACKPACK_TYPES = 31;
constexpr uint32_t TEXT_NOT_AVAILABLE = 0x2532;
// The automatic equipping: the mission essentials' text (the screen's 9th control) and its styles,
// "Necessary items for mission:"; the behaviours (the medkits: the first 255 types, the grenades: the
// first 100), item type 9, the uniform worn bare-headed (0x79); the kits' weapons (a rifle, a
// scoped one).
constexpr uint32_t ESSENTIALS_TEXT = 0x04900000, ESSENTIALS_TITLE_STYLE = 2, ESSENTIALS_STYLE = 0xF, TEXT_ESSENTIALS = 0xAA9;
constexpr int BEHAVIOUR_KITS = ITEM_BEHAVIOUR_MEDKIT, BEHAVIOUR_GRENADE = ITEM_BEHAVIOUR_THROWN,
              BEHAVIOUR_HEADGEAR = ITEM_BEHAVIOUR_HEADGEAR, BEHAVIOUR_BACKPACK = ITEM_BEHAVIOUR_BACKPACK,
              BEHAVIOUR_20 = ITEM_BEHAVIOUR_RESPAWNING;
constexpr int KIT_TYPES = 255, GRENADE_TYPES = 100, SPECIAL_ITEM = 9, BARE_HEAD_UNIFORM = 0x79;
constexpr int RIFLE = 2, SCOPED = 1;

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

// A visual copy with its own mesh.
void OwnMesh(I3D_frame *frame)
{
    auto *visual = static_cast<I3D_object_visual *>(frame);
    visual->DuplicateMesh();
    visual->GetVisualMesh()->DuplicateMaterials();
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

// ---- the screens' descriptions

bool C_EquipmentMenu::BuildScreens()
{
    S_MenuDesc *menu = new S_MenuDesc();
    menu->AddScreen("Items selection", "items selection");
    menu->AddScreen("SELECTION SCREEN", "selection screen");
    for (S_ScreenDesc *desc : menu->screens) {
        I3D_frame *model = LoadModel(desc->name.c_str(), desc->file.c_str());
        if (!model)
            continue;
        const uint32_t index = desc->id >> SCREEN_INDEX_SHIFT;
        if (index == ITEMS_SCREEN) {
            EnsureSquad();
            BuildEquipmentPanel(desc, model);
            BuildSquadList(desc, model);
            BuildStore(desc, model);
            BuildStatsPanel(desc, model);
            BuildItemsScreen(desc, model);
            // The soldiers to equip equipped.
            AutoEquip(nullptr, nullptr, 0, 0);
        }
        if (index == SELECTION_SCREEN)
            BuildSelectionScreen(desc, model);
        auto *screen = new C_Screen(m_scene, model);
        // (Quirk kept: a screen not built is leaked.)
        if (screen->Build(desc)) {
            screen->HandleMessages(desc->messages);
            m_screens.push_back(screen);
        }
    }
    delete menu;
    S_ControlMessage open;
    open.id = MENU_MSG_OPEN_SCREEN;
    open.value = ITEMS_SCREEN;
    HandleMessage(&open);
    ShowSoldierStats();
    // The item's popup.
    if (I3D_frame *popup = LoadModel("DESCRIPTION", "description")) {
        m_popup = new C_DescriptionScreen(m_scene, popup);
        m_popup->Build(nullptr);
    }
    return true;
}

void C_EquipmentMenu::LoadRoster()
{
    C_HumanStore *store = g_gameProfile.m_humanStore;
    if (store->GetCount() == 0)
        store->Load(C_fstr("tables\\HumanStore.def"));
}

void C_EquipmentMenu::EnsureSquad()
{
    S_MissionSquad *squad = g_gameProfile.m_missionSquad;
    if (!squad->states.empty())
        return;
    LoadRoster();
    for (int profile = 0; profile < DEFAULT_SQUAD; ++profile)
        squad->AddSoldier()->SetProfile(profile, 0);
}

bool C_EquipmentMenu::BuildEquipmentPanel(S_ScreenDesc *desc, I3D_frame *model)
{
    I3D_frame *panel = model->FindChildFrame("equippanel", FRAME_MASK_DUMMY);
    I3D_frame *holsters = I3DGetDriver()->CreateFrame(FRAME_DUMMY);
    I3D_frame *backpack = I3DGetDriver()->CreateFrame(FRAME_DUMMY);
    // (The frames made are leaked without the panel's.)
    if (!panel || !holsters || !backpack)
        return false;
    panel->SetName("P_Equipment");
    const auto group = [panel](I3D_frame *frame, const char *name) {
        frame->LinkTo(panel, 0);
        frame->SetPos(S_vector(0.0f, 0.0f, 0.0f));
        frame->SetName(name);
    };
    group(holsters, "GB_Holsters");
    group(backpack, "GR_BackPack");
    for (I3D_frame *frame : {panel, holsters, backpack})
        m_models.push_back(frame);
    // The visuals the soldier's items are shown on get meshes of their own; the slots' visuals moved
    // under their groups, the scroll bars' and their texts' under the panel.
    const auto own = [panel](const char *name) {
        auto *visual = static_cast<I3D_object_visual *>(panel->FindChildFrame(name, FRAME_MASK_VISUAL));
        if (visual)
            OwnMesh(visual);
        return visual;
    };
    const auto move = [panel](const char *name, I3D_frame *parent) {
        I3D_frame *frame = panel->FindChildFrame(name, FRAME_MASK_VISUAL);
        if (frame)
            frame->LinkTo(parent, 1);
        return frame;
    };
    for (const char *name : {"back", "head", "drop", "hands", "uniform", "shoulder"})
        own(name);
    // (Quirk kept: the texts are moved under a missing scroll bar too, unlinked.)
    I3D_frame *scroll = move("scroll13", panel);
    move("text_hlstrscap", scroll);
    move("hlcpct00", scroll);
    for (const char *name : {"item00", "item01", "item02", "item03"}) {
        if (I3D_frame *item = own(name))
            item->LinkTo(holsters, 1);
    }
    scroll = move("scroll14", panel);
    move("text_bkpckcap", scroll);
    move("hlcpct01", scroll);
    for (const char *name : {"item04", "item05", "item06", "item07"}) {
        if (I3D_frame *item = own(name))
            item->LinkTo(backpack, 1);
    }
    m_panel.SetMenu(this);
    m_panel.BuildDescription(desc, model);
    return true;
}

bool C_EquipmentMenu::BuildSquadList(S_ScreenDesc *desc, I3D_frame *model)
{
    for (const char *name : {"table11", "table13", "table14", "table15", "photo03", "photo04", "photo05", "photo06",
                             "photo07"}) {
        if (I3D_frame *frame = model->FindChildFrame(name, FRAME_MASK_VISUAL))
            frame->SetOn(false);
    }
    I3D_frame *list = I3DGetDriver()->CreateFrame(FRAME_DUMMY);
    I3D_frame *photo = model->FindChildFrame("photo03", FRAME_MASK_VISUAL);
    // (The frame made is leaked without the picture.)
    if (!photo || !list)
        return false;
    list->LinkTo(model, 0);
    list->SetPos(S_vector(0.0f, 0.0f, 0.0f));
    list->SetName("GB_Soldiers");
    m_models.push_back(list);
    photo->LinkTo(list, 1);
    photo->SetOn(true);
    list->SetOn(true);
    photo->Update();
    list->Update();
    AddDescMessage(desc->AddControl(CONTROL_TEXT, "name- capacity"), 0, CONTROL_MSG_SET_TEXT_ID, BLANK_STYLE, TEXT_BLANK);
    S_ControlDesc *group = desc->AddControl(CONTROL_LIST, "GB_Soldiers");
    AddDescMessage(group, 0, CONTROL_MSG_SET_VISIBLE, SQUAD_LIST_ROWS);
    AddDescMessage(group, 0, CONTROL_MSG_SET_ACROSS_STEP, 0, FloatValue(SQUAD_ACROSS_STEP));
    // The dead soldiers out of the squad (the last first).
    std::vector<S_SquadSoldier *> &squad = g_gameProfile.m_missionSquad->states;
    for (size_t i = squad.size(); i-- > 0;) {
        // (Quirk kept: the profile read unchecked.)
        if (!g_gameProfile.m_humanStore->Get(squad[i]->profileIndex)->dead)
            continue;
        delete squad[i];
        squad.erase(squad.begin() + static_cast<std::ptrdiff_t>(i));
    }
    // A soldier's picture: the template's copy (its own mesh) across from it, its index, face, click.
    const auto self = reinterpret_cast<uintptr_t>(this);
    for (int i = 0; i < static_cast<int>(squad.size()); ++i) {
        const C_str name = C_fstr("Soldier%02d", i);
        I3D_frame *copy = nullptr;
        CloneFrameTree(copy, photo, nullptr);
        m_frames.push_back(copy);
        OwnMesh(copy);
        copy->SetName(name.c_str());
        copy->LinkTo(list, 0);
        const S_vector &at = photo->GetPos();
        copy->SetPosInline(S_vector(static_cast<float>(i) * SQUAD_ACROSS_STEP + at.x, at.y, at.z));
        copy->SetOn(true);
        copy->Update();
        copy->GetWorldPos();
        S_ControlDesc *picture = group->AddChild(CONTROL_PICTURE, name.c_str());
        AddDescMessage(picture, 0, CONTROL_MSG_SET_INDEX, static_cast<uint32_t>(i));
        AddDescMessage(picture, 0, CONTROL_MSG_SET_TEXTURE, 0,
                       reinterpret_cast<uintptr_t>(g_gameProfile.m_humanStore->Get(squad[static_cast<size_t>(i)]->profileIndex)->faceImage));
        AddDescMessage(picture, 0, CONTROL_MSG_ADD_CALLBACK, CONTROL_NOTIFY_CLICK, self,
                       reinterpret_cast<uintptr_t>(&C_EquipmentMenu::OnSoldierPicked));
    }
    AddDescMessage(group, 0, CONTROL_MSG_REFRESH, 0);
    photo->SetOn(false);
    photo->Update();
    // (Quirk kept: the selector is not checked.)
    m_soldierSelector = model->FindChildFrame("selector", FRAME_MASK_VISUAL);
    m_soldier = 0;
    m_panel.SelectSoldier(0);
    m_panel.Refresh();
    return true;
}

bool C_EquipmentMenu::BuildStore(S_ScreenDesc *desc, I3D_frame *model)
{
    // (Quirk kept: the selector is not checked.)
    model->FindChildFrame("itemselector01", FRAME_MASK_VISUAL)->SetOn(false);
    I3D_frame *panel = I3DGetDriver()->CreateFrame(FRAME_DUMMY);
    I3D_frame *filter = model->FindChildFrame("filter", FRAME_MASK_VISUAL);
    I3D_frame *scroll = model->FindChildFrame("scroll01", FRAME_MASK_VISUAL);
    if (!panel || !filter || !scroll)
        return false;
    panel->LinkTo(model, 0);
    panel->SetPos(S_vector(0.0f, 0.0f, 0.0f));
    panel->SetName("P_visualstore");
    m_models.push_back(panel);
    filter->LinkTo(panel, 1);
    scroll->LinkTo(panel, 1);
    // The store's entries' template, its count's and its information's texts.
    I3D_frame *entry = model->FindChildFrame("item08", FRAME_MASK_VISUAL);
    I3D_frame *count = model->FindChildFrame("item09", FRAME_MASK_VISUAL);
    I3D_frame *info = model->FindChildFrame("text_info00", FRAME_MASK_VISUAL);
    if (!entry || !count || !info)
        return false;
    entry->SetName("vi_template");
    info->LinkTo(entry, 1);
    count->LinkTo(entry, 1);
    count->SetName("ammo");
    info->SetOn(false);
    count->SetOn(false);
    m_storeList.SetMenu(this);
    m_storeList.BuildEquipmentDescription(desc, model);
    m_storeList.ClearPages();
    SetupStore();
    return true;
}

bool C_EquipmentMenu::BuildStatsPanel(S_ScreenDesc *desc, I3D_frame *)
{
    // The skills' rows: their names' and values' titles, their bars.
    static const char *const SKILLS[][3] = {
        {"text_shooting", "hlcpct07", "text_shooting01"},     {"text_strenght", "hlcpct08", "text_strenght01"},
        {"text_health", "hlcpct09", "text_health01"},         {"text_endurance", "hlcpct03", "text_endurance01"},
        {"text_stealth", "hlcpct04", "text_stealth01"},       {"text_first aid", "hlcpct05", "text_first aid01"},
        {"text_lockpick", "hlcpct06", "text_lockpick01"}};
    S_ControlDesc *panel = desc->AddControl(CONTROL_GROUP, "Panel02");
    AddDescMessage(panel, 0, CONTROL_MSG_IGNORE_INPUT, 0);
    AddDescMessage(panel, 0, CONTROL_MSG_HIDE, 0);
    for (int i = 0; i < SKILL_ROWS; ++i) {
        S_ControlDesc *name = panel->AddChild(CONTROL_TEXT, SKILLS[i][0]);
        AddDescMessage(name, 0, CONTROL_MSG_SET_MARGIN, 0);
        AddDescMessage(name, 0, CONTROL_MSG_SET_TEXT_ID, BLANK_STYLE, TEXT_SKILLS + static_cast<uint32_t>(i));
        S_ControlDesc *bar = panel->AddChild(CONTROL_BAR, SKILLS[i][1]);
        AddDescMessage(bar, 0, CONTROL_MSG_SET_BAR_AXIS, BAR_AXIS_Z);
        AddDescMessage(bar, 0, CONTROL_MSG_SET_BAR_VALUE, 0, 0);
        S_ControlDesc *value = panel->AddChild(CONTROL_TEXT, SKILLS[i][2]);
        AddDescMessage(value, 0, CONTROL_MSG_SET_MARGIN, 0);
        AddDescMessage(value, 0, CONTROL_MSG_SET_TEXT_ID, BLANK_STYLE, TEXT_BLANK);
    }
    AddDescMessage(panel->AddChild(CONTROL_TEXT, "text_soldier stats"), 0, CONTROL_MSG_SET_TEXT_ID, BLANK_STYLE,
                   TEXT_SOLDIER_STATS);
    return true;
}

bool C_EquipmentMenu::BuildItemsScreen(S_ScreenDesc *desc, I3D_frame *model)
{
    const auto self = reinterpret_cast<uintptr_t>(this);
    const auto text = [desc](int type, const char *name, uint32_t style, uint32_t textId) {
        S_ControlDesc *control = desc->AddControl(type, name);
        AddDescMessage(control, 0, CONTROL_MSG_SET_TEXT_ID, style, textId);
        return control;
    };
    const auto callback = [self](S_ControlDesc *control, uint32_t notification, ControlCallback function) {
        AddDescMessage(control, 0, CONTROL_MSG_ADD_CALLBACK, notification, self, reinterpret_cast<uintptr_t>(function));
    };
    callback(text(CONTROL_BUTTON, "bassign to all", BUTTON_TEXT_STYLE, TEXT_PANEL), CONTROL_NOTIFY_CLICK,
             &C_EquipmentMenu::CallStatsToggle);
    text(CONTROL_TEXT, "text_inventory", BLANK_STYLE, 0xAA8);
    // The soldier types: the presets' (of the user's kind) names' texts.
    S_ControlDesc *types = desc->AddControl(CONTROL_COMBO, "soldier type");
    callback(types, CONTROL_NOTIFY_0E, &C_EquipmentMenuBase::CallItemNotify60);
    AddDescMessage(types, 0, CONTROL_MSG_SET_HEADER_TEXT_ID, LIST_STYLE, TEXT_SOLDIER_TYPES);
    AddDescMessage(types, 0, CONTROL_MSG_ADD_TEXT_ID, LIST_ITEM_STYLE, TEXT_SOLDIER_TYPES);
    for (int i = 0; S_LoadoutPreset *preset = m_presets.GetPreset(i, CAMPAIGN_PRESETS); ++i)
        AddDescMessage(types, 0, CONTROL_MSG_ADD_TEXT_ID, LIST_ITEM_STYLE, static_cast<uintptr_t>(preset->textId));
    desc->AddControl(CONTROL_LABEL, "text_mission essentials");
    // "INVENTORY SELECTION" (the equipment kind), else "INVENTORY CHANGE".
    const bool equipment = m_mode == C_CampaignScreen::KIND_EQUIPMENT;
    text(CONTROL_TEXT, "text_screen", SCREEN_TITLE_STYLE, equipment ? 0xA8D : 0xA8E);
    text(CONTROL_TEXT, "text_military storages", BLANK_STYLE, 0xA93);
    text(CONTROL_TEXT, "text_team", BLANK_STYLE, 0xA92);
    // "RECOMMENDED SELECTION" (else "AUTOMATIC REPLENISHMENT"): the automatic equipping; "CONTINUE",
    // "BACK".
    callback(text(CONTROL_BUTTON, "baccept and continue", BUTTON_TEXT_STYLE, equipment ? 0xA90 : 0xAB1), CONTROL_NOTIFY_CLICK,
             &C_EquipmentMenu::CallAutoEquip);
    AddDescMessage(text(CONTROL_BUTTON, "bauto equip", BUTTON_TEXT_STYLE, 0xA8F), 0, CONTROL_NOTIFY_CLICK | EQUIPMENT_MSG_CONTINUE, 0);
    AddDescMessage(text(CONTROL_BUTTON, "bback", BUTTON_TEXT_STYLE, 0xA91), 0, CONTROL_NOTIFY_CLICK | MENU_MSG_ESCAPE, 0);
    text(CONTROL_TEXT, "text_h&dversion", VERSION_STYLE, TEXT_VERSION);
    text(CONTROL_TEXT, "text_assortment", BLANK_STYLE, 0xAA6);
    if (model->FindChildFrame("description", FRAME_MASK_VISUAL))
        m_weaponsInfo = static_cast<int>(text(CONTROL_DESCRIPTION, "description", BLANK_STYLE, TEXT_BLANK)->id);
    if (I3D_frame *essentials = model->FindChildFrame("text_mission essentials", FRAME_MASK_VISUAL))
        essentials->SetOn(false);
    return true;
}

bool C_EquipmentMenu::BuildSelectionScreen(S_ScreenDesc *desc, I3D_frame *model)
{
    AddDescMessage(desc->AddControl(CONTROL_TEXT, "text_select one soldier"), 0, CONTROL_MSG_SET_TEXT_ID, LIST_STYLE,
                   0xAB2);
    const auto self = reinterpret_cast<uintptr_t>(this);
    // A squad soldier's picture (its own mesh, its index, face, click); past the squad's off.
    const char *const pictures[] = {"photo03", "photo04", "photo05", "photo06"};
    for (int i = 0; i < SELECTION_PICTURES; ++i) {
        I3D_frame *frame = model->FindChildFrame(pictures[i], FRAME_MASK_VISUAL);
        if (!frame)
            continue;
        S_SquadSoldier *soldier = SquadSoldier(i);
        if (!soldier) {
            frame->SetOn(false);
            continue;
        }
        frame->SetOn(true);
        OwnMesh(frame);
        S_ControlDesc *picture = desc->AddControl(CONTROL_PICTURE, pictures[i]);
        AddDescMessage(picture, 0, CONTROL_MSG_SET_INDEX, static_cast<uint32_t>(i));
        AddDescMessage(picture, 0, CONTROL_MSG_SET_TEXTURE, 0,
                       reinterpret_cast<uintptr_t>(g_gameProfile.m_humanStore->Get(soldier->profileIndex)->faceImage));
        AddDescMessage(picture, 0, CONTROL_MSG_ADD_CALLBACK, CONTROL_NOTIFY_CLICK, self,
                       reinterpret_cast<uintptr_t>(&C_EquipmentMenu::CallSelectionPicked));
    }
    // "YES": the pick accepted, the screen closed; "CANCEL": closed.
    S_ControlDesc *yes = desc->AddControl(CONTROL_BUTTON, "button01");
    AddDescMessage(yes, 0, CONTROL_MSG_SET_TEXT_ID, BUTTON_TEXT_STYLE, 0x7D1);
    AddDescMessage(yes, 0, CONTROL_NOTIFY_CLICK | EQUIPMENT_MSG_SELECTED, 0);
    AddDescMessage(yes, 0, CONTROL_NOTIFY_CLICK | MENU_MSG_CLOSE_POPUP, 0);
    S_ControlDesc *cancel = desc->AddControl(CONTROL_BUTTON, "button00");
    AddDescMessage(cancel, 0, CONTROL_MSG_SET_TEXT_ID, BUTTON_TEXT_STYLE, 0x7D8);
    AddDescMessage(cancel, 0, CONTROL_NOTIFY_CLICK | MENU_MSG_CLOSE_POPUP, 0);
    // (Not checked.)
    m_selector = model->FindChildFrame("selector", FRAME_MASK_VISUAL);
    return true;
}

bool C_EquipmentMenu::CallAutoEquip(void *menu, void *argument, C_Control *control, uint32_t notification,
                                    uint32_t value)
{
    return static_cast<C_EquipmentMenu *>(menu)->AutoEquip(argument, control, notification, value);
}

// ---- the soldier type picked

bool C_EquipmentMenu::OnItemNotify60(void *, C_Control *control, uint32_t, uint32_t)
{
    if (!control)
        return true;
    S_ControlMessage picked;
    picked.id = CONTROL_MSG_GET_TEXT;
    control->HandleMessage(&picked);
    const int line = static_cast<int>(picked.value) - 1;
    // The header: the soldier's items the mission does not need back.
    if (line == -1) {
        m_panel.SelectSoldier(m_soldier);
        ReturnUnrequired(m_soldier, true);
        m_panel.Refresh();
        m_panel.SelectSoldier(m_soldier);
        m_storeList.UpdateAll();
        return false;
    }
    if (line < 0)
        return false;
    m_panel.SelectSoldier(m_soldier);
    if (const S_LoadoutPreset *preset = m_presets.GetPreset(line, CAMPAIGN_PRESETS)) {
        const int oldHands = preset->hands;
        ReturnUnrequired(m_soldier, true);
        S_LoadoutPreset *match = MatchPreset(*preset, false);
        if (match->head != -1)
            PutStoreItem(ITEM_FITS_HEAD, match->head, 1, false);
        // A hands' weapon unlike the preset's (in its kind, the paired ids, a scope's reticle): not
        // taken, nor its ammunition; the message.
        int skippedAmmo = -1;
        if (match->hands != -1) {
            const auto paired = [](const C_ItemDef *d) {
                return std::find(std::begin(PAIRED_ITEMS), std::end(PAIRED_ITEMS), d->GetId()) != std::end(PAIRED_ITEMS);
            };
            const auto reticle = [](C_ItemDef *d) {
                const C_ItemComponent *scope = d->GetSecondaryComponent();
                return d->GetBehaviour() == 4 && scope && scope->GetKind() == ICK_SCOPE &&
                       static_cast<const C_ScopeComp *>(scope)->GetAimMode() < RETICLE_MODES;
            };
            // (Quirk kept: the definitions read unchecked.)
            C_ItemDef *old = GetItemDef(oldHands), *now = GetItemDef(match->hands);
            if (oldHands == -1 ||
                ((old->GetBehaviour() == now->GetBehaviour() || paired(old) || paired(now)) && paired(old) == paired(now) &&
                 reticle(old) == reticle(now))) {
                PutStoreItem(ITEM_FITS_HANDS, match->hands, 1, false);
            } else {
                skippedAmmo = now->GetAmmoItemId();
                ShowMessageId(MESSAGE_BOX_OK, TEXT_NOT_AVAILABLE, 0, 0);
            }
        }
        if (match->longGun != -1)
            PutStoreItem(ITEM_FITS_LONG_GUN, match->longGun, 1, false);
        if (match->backpack != -1)
            PutStoreItem(ITEM_FITS_BACKPACK, match->backpack, 1, false);
        for (int i = 0; i < match->beltCount; ++i) {
            if (match->belt[i].typeId != skippedAmmo)
                PutStoreItem(ITEM_FITS_BELT, match->belt[i].typeId, static_cast<int16_t>(match->belt[i].count), false);
        }
        for (int i = 0; i < match->backpackCount; ++i) {
            if (match->backpackItems[i].typeId != skippedAmmo)
                PutStoreItem(ITEM_FITS_BACKPACK_CONTENTS, match->backpackItems[i].typeId,
                             static_cast<int16_t>(match->backpackItems[i].count), false);
        }
        delete match;
    }
    m_panel.Refresh();
    m_storeList.UpdateAll();
    return true;
}

bool C_EquipmentMenu::PutStoreItem(uint32_t slot, int type, int count, bool fill)
{
    if (type < 0)
        return false;
    S_SoldierItem item;
    item.typeId = static_cast<int16_t>(type);
    item.present = static_cast<int16_t>(count);
    if (!m_storeList.TakeItem(item, fill))
        return false;
    const S_SoldierItem left = m_panel.PutItemChecked(slot, item, false);
    if (left.typeId == -1 || left.present == 0)
        return true;
    m_storeList.AddItem(left);
    return left.typeId != type;
}

bool C_EquipmentMenu::DropStoreItem(int type, int count, bool fill)
{
    S_SoldierItem item;
    item.typeId = static_cast<int16_t>(type);
    item.present = static_cast<int16_t>(count);
    if (!m_storeList.TakeItem(item, fill))
        return false;
    const S_SoldierItem left = m_panel.DropItem(item, fill);
    if (left.typeId == -1 || left.present == 0)
        return true;
    m_storeList.AddItem(left);
    return left.typeId != type;
}

void C_EquipmentMenu::ReturnUnrequired(int soldierIndex, bool withHead)
{
    C_Campaign *campaign = g_gameProfile.m_campaign;
    const S_MissionItems *record = m_missionItems.GetMissionRecord(campaign->GetMissionNumber(campaign->GetCurrentIndex()));
    m_panel.SelectSoldier(soldierIndex);
    S_SquadSoldier *soldier = m_panel.GetSquadSoldier();
    if (!soldier)
        return;
    // (Fix: no record, nothing required; the original read it unchecked.)
    const auto required = [record](int type) { return record ? record->GetRequiredCount(type) : 0; };
    // What of an item goes back: all of an item not required, else the shown soldier's surplus of it
    // (quirk kept: m_soldier's, not the soldier's; none: kept).
    const auto giveBack = [this, &required](const S_SoldierItem &item, S_SoldierItem &back) {
        const int count = required(item.typeId);
        int surplus = 0;
        if (count != 0) {
            surplus = CountCarried(std::make_pair(static_cast<int>(item.typeId), count), m_soldier);
            if (surplus < 1)
                return false;
        }
        back = item;
        if (surplus != 0)
            back.present = static_cast<int16_t>(surplus);
        return true;
    };
    const auto place = [&](const S_SoldierItem &item, uint8_t slot) {
        S_SoldierItem back;
        if (item.typeId < 0 || !giveBack(item, back))
            return;
        soldier->RemoveItem(slot, back);
        m_storeList.AddItem(back);
    };
    // The belt's and the backpack's: an item given back stays at its place for the next one.
    const auto items = [&](std::vector<S_SoldierItem> &held, uint8_t slot) {
        size_t at = 0;
        for (size_t n = held.size(); n > 0; --n) {
            S_SoldierItem back;
            if (!giveBack(held[at], back)) {
                ++at;
                continue;
            }
            soldier->RemoveItem(slot, back);
            m_storeList.AddItem(back);
        }
    };
    if (withHead)
        place(soldier->head, SQUAD_SLOT_HEAD);
    place(soldier->longGun, SQUAD_SLOT_LONG_GUN);
    place(soldier->hands, SQUAD_SLOT_HANDS);
    items(soldier->belt, SQUAD_SLOT_BELT);
    items(soldier->backpackContents, SQUAD_SLOT_BACKPACK_CONTENTS);
    place(soldier->backpack, SQUAD_SLOT_BACKPACK);
}

bool C_EquipmentMenu::FindSubstitute(int &type, int ammo, bool axisOnly)
{
    // (Quirk kept: the type's definition read unchecked.)
    C_ItemDef *def = GetItemDef(type);
    const int behaviour = def->GetBehaviour();
    C_ItemStock *stock = m_storeList.GetStock();
    if (def->GetRecordType() == ITEM_AMMO)
        return false;
    const auto paired = [](const C_ItemDef *d) {
        return std::find(std::begin(PAIRED_ITEMS), std::end(PAIRED_ITEMS), d->GetId()) != std::end(PAIRED_ITEMS);
    };
    const auto reticle = [](C_ItemDef *d) {
        const C_ItemComponent *scope = d->GetSecondaryComponent();
        return d->GetBehaviour() == 4 && scope && scope->GetKind() == ICK_SCOPE &&
               static_cast<const C_ScopeComp *>(scope)->GetAimMode() < RETICLE_MODES;
    };
    int best = -1, bestScore = INT_MIN;
    for (int candidate = 0; candidate < MAX_ITEM_TYPES; ++candidate) {
        if (stock->Get(candidate) <= 0)
            continue;
        C_ItemDef *other = GetItemDef(candidate);
        if (!other || (axisOnly && other->GetTeam() != ITEM_TEAM_AXIS))
            continue;
        int score = candidate == type ? 0x33 : 0;
        const int otherBehaviour = other->GetBehaviour();
        if (behaviour == otherBehaviour && behaviour != UNMATCHED_BEHAVIOUR) {
            if (reticle(def) && reticle(other))
                score += 150;
            else if (paired(other) && paired(def))
                score += 160;
            else
                score += 100;
        } else if ((behaviour < SMALL_BEHAVIOURS || behaviour == BEHAVIOUR_12) &&
                   (otherBehaviour < SMALL_BEHAVIOURS || otherBehaviour == BEHAVIOUR_12)) {
            switch (otherBehaviour) {
            case 2:
                score += 30;
                break;
            case 3:
            case 4:
                score += 60;
                break;
            case 5:
                score += behaviour == BEHAVIOUR_12 ? 111 : 50;
                break;
            case BEHAVIOUR_12:
                score += 50;
                break;
            default:
                score += 10;
                break;
            }
            if (paired(other) && paired(def))
                score += 110;
        }
        if (other->GetBehaviour() <= BEHAVIOUR_12 && other->GetAmmoItemId() >= 0) {
            const int have = stock->Get(other->GetAmmoItemId());
            if (ammo > 0) {
                score += ammo <= have ? 30 : (have - ammo) * 10;
                if (have == 0)
                    score -= 100;
            }
        }
        if (score > bestScore && score > 0) {
            bestScore = score;
            best = candidate;
        }
    }
    if (type == best)
        return false;
    type = best;
    return best >= 0;
}

S_LoadoutPreset *C_EquipmentMenu::MatchPreset(const S_LoadoutPreset &preset, bool axisOnly)
{
    auto *match = new S_LoadoutPreset();
    // Each type's replacement in the belt and the backpack (a swapped weapon's ammunition).
    int remap[MAX_ITEM_TYPES];
    for (int i = 0; i < MAX_ITEM_TYPES; ++i)
        remap[i] = i;
    // A place: its substitute, else its own type (none: none).
    const auto place = [&](int from, int &to) {
        int type = from;
        if (FindSubstitute(type, 0, axisOnly))
            to = type;
        else if (type >= 0)
            to = from;
    };
    if (preset.head != -1)
        place(preset.head, match->head);
    // The weapons: their ammunition in the preset asked (quirk kept: a weapon without a definition
    // or ammunition keeps the last one's).
    int ammo = 0, ammoType = -1;
    const auto weapon = [&](int from, int &to) {
        if (C_ItemDef *def = GetItemDef(from)) {
            if (def->GetAmmoItemId() < 0) {
                ammo = 0;
            } else {
                ammoType = def->GetAmmoItemId();
                ammo = preset.CountItems(ammoType);
            }
        }
        int type = from;
        if (!FindSubstitute(type, ammo, axisOnly)) {
            if (type >= 0)
                to = from;
            return;
        }
        to = type;
        if (ammoType >= 0 && type >= 0 && GetItemDef(type))
            remap[ammoType] = GetItemDef(type)->GetAmmoItemId();
    };
    if (preset.hands != -1)
        weapon(preset.hands, match->hands);
    if (preset.longGun != -1)
        weapon(preset.longGun, match->longGun);
    if (preset.uniform != -1)
        place(preset.uniform, match->uniform);
    if (preset.backpack != -1)
        place(preset.backpack, match->backpack);
    // The belt's and the backpack's items: merged by type, their own type unless swapped.
    const auto add = [](S_PresetItem *items, int &n, int max, int type, int count) {
        if (n >= max)
            return;
        for (int i = 0; i < n; ++i) {
            if (items[i].typeId == type) {
                items[i].count += count;
                return;
            }
        }
        items[n].typeId = type;
        items[n].count = count;
        ++n;
    };
    const auto items = [&](const S_PresetItem *from, int count, S_PresetItem *to, int &n, int max) {
        for (int i = 0; i < count; ++i) {
            int type = remap[from[i].typeId];
            if (type == from[i].typeId && !FindSubstitute(type, 0, axisOnly)) {
                if (type >= 0)
                    add(to, n, max, from[i].typeId, from[i].count);
            } else {
                add(to, n, max, type, from[i].count);
            }
        }
    };
    items(preset.belt, preset.beltCount, match->belt, match->beltCount, PRESET_BELT_TYPES);
    items(preset.backpackItems, preset.backpackCount, match->backpackItems, match->backpackCount, PRESET_BACKPACK_TYPES);
    return match;
}

// ---- the automatic equipping

bool C_EquipmentMenu::AutoEquip(void *argument, C_Control *control, uint32_t notification, uint32_t value)
{
    C_Campaign *campaign = g_gameProfile.m_campaign;
    S_MissionItems *record = m_missionItems.GetMissionRecord(campaign->GetMissionNumber(campaign->GetCurrentIndex()));
    // The menu's own call (all none): only the soldiers to equip.
    const bool onlyNew = !argument && !control && notification == 0 && value == 0;
    if (!record)
        return false;
    DistributeRequired();
    std::vector<S_SquadSoldier *> &squad = g_gameProfile.m_missionSquad->states;
    int assigned[S_MissionItems::SLOTS] = {};
    AssignSlots(*record, assigned, static_cast<int>(squad.size()));
    for (size_t i = squad.size(); i-- > 0;) {
        S_SquadSoldier *soldier = squad[i];
        const bool fresh = soldier->autoEquip;
        if (onlyNew && !fresh)
            continue;
        m_panel.SelectSoldier(static_cast<int>(i));
        const S_MissionItems::S_Slot &slot = record->slots[assigned[i]];
        S_SquadSoldier *dressed = m_panel.GetSquadSoldier();
        // Unarmed: the slot's head gear and preset (the full equipping: its match for the uniform's side).
        if (dressed->hands.typeId == -1 && dressed->longGun.typeId == -1) {
            ReturnUnrequired(static_cast<int>(i), false);
            if (slot.b != -1 && slot.b != dressed->head.typeId)
                PutStoreItem(ITEM_FITS_HEAD, slot.b, 1, fresh);
            const std::vector<S_LoadoutPreset *> presets = m_presets.GetPresets();
            if (slot.a >= 0 && static_cast<size_t>(slot.a) < presets.size()) {
                S_LoadoutPreset *preset = presets[static_cast<size_t>(slot.a)];
                if (!onlyNew) {
                    const C_ItemDef *uniform = GetItemDef(dressed->uniform.typeId);
                    preset = MatchPreset(*preset, uniform && uniform->GetTeam() == ITEM_TEAM_AXIS);
                }
                if (preset->head != -1 && slot.b == -1)
                    PutStoreItem(ITEM_FITS_HEAD, preset->head, 1, fresh);
                if (preset->hands != -1)
                    PutStoreItem(ITEM_FITS_HANDS, preset->hands, 1, fresh);
                if (preset->backpack != -1)
                    PutStoreItem(ITEM_FITS_BACKPACK, preset->backpack, 1, fresh);
                if (preset->longGun != -1)
                    PutStoreItem(ITEM_FITS_LONG_GUN, preset->longGun, 1, fresh);
                for (int k = 0; k < preset->beltCount; ++k)
                    PutStoreItem(ITEM_FITS_BELT, preset->belt[k].typeId, static_cast<int16_t>(preset->belt[k].count), fresh);
                for (int k = 0; k < preset->backpackCount; ++k)
                    PutStoreItem(ITEM_FITS_BACKPACK_CONTENTS, preset->backpackItems[k].typeId,
                                 static_cast<int16_t>(preset->backpackItems[k].count), fresh);
                if (!onlyNew)
                    delete preset;
            }
        }
        // (Quirk kept: the list's soldier, not the dressed one.)
        SquadSoldier(static_cast<int>(i))->autoEquip = false;
        if (!onlyNew)
            Replenish(static_cast<int>(i), 0, -1);
    }
    if (!onlyNew) {
        // Two more rounds for every soldier: a pistol on his belt gets its ammunition too.
        for (int round = 1; round <= 2; ++round) {
            for (int i = 0; i < static_cast<int>(squad.size()); ++i) {
                m_panel.SelectSoldier(i);
                int pistol = -1;
                for (const S_SoldierItem &item : m_panel.GetSquadSoldier()->belt) {
                    C_ItemDef *def = GetItemDef(item.typeId);
                    if (def && def->GetBehaviour() == ITEM_BEHAVIOUR_SIDEARM) {
                        pistol = item.typeId;
                        break;
                    }
                }
                Replenish(i, round, pistol);
            }
        }
    }
    m_panel.SelectSoldier(m_soldier);
    m_panel.Refresh();
    m_storeList.UpdateAll();
    return true;
}

void C_EquipmentMenu::DistributeRequired()
{
    C_Campaign *campaign = g_gameProfile.m_campaign;
    S_MissionItems *record = m_missionItems.GetMissionRecord(campaign->GetMissionNumber(campaign->GetCurrentIndex()));
    if (!record)
        return;
    // The mission essentials' list: "Necessary items for mission:" and each item ("<count>x <name>, ",
    // the last ". ").
    Post(ESSENTIALS_TEXT, CONTROL_MSG_CLEAR, 0);
    if (!record->required.empty())
        Post(ESSENTIALS_TEXT, CONTROL_MSG_ADD_TEXT_ID, ESSENTIALS_TITLE_STYLE, TEXT_ESSENTIALS);
    std::vector<S_SquadSoldier *> &squad = g_gameProfile.m_missionSquad->states;
    const int squadSize = static_cast<int>(squad.size());
    // The soldier the next one goes to (kept from one item to the next but for the weapons and wear)
    // and the tries in a row that failed (quirk kept: kept from one item to the next too).
    int next = 0, failed = 0;
    for (size_t k = 0; k < record->required.size(); ++k) {
        std::pair<int, int> &item = record->required[k];
        // (Quirk kept: the item's definition read unchecked.)
        C_ItemDef *def = GetItemDef(item.first);
        if ((def->GetBehaviour() == BEHAVIOUR_UNIFORM || item.first == DIVING_GEAR) && squadSize < item.second)
            item.second = squadSize;
        Post(ESSENTIALS_TEXT, CONTROL_MSG_ADD_TEXT, ESSENTIALS_STYLE, 0, C_fstr("%ix ", item.second));
        Post(ESSENTIALS_TEXT, CONTROL_MSG_ADD_TEXT_ID, ESSENTIALS_STYLE, static_cast<uintptr_t>(def->GetTextId()));
        Post(ESSENTIALS_TEXT, CONTROL_MSG_ADD_TEXT, ESSENTIALS_STYLE, 0, C_fstr(k + 1 < record->required.size() ? ", " : ". "));
        int missing = item.second - CountCarried(item, -1);
        if (missing < 1)
            continue;
        const int behaviour = def->GetBehaviour();
        if (behaviour == BEHAVIOUR_UNIFORM || behaviour == BEHAVIOUR_HEADGEAR || behaviour == BEHAVIOUR_20 ||
            behaviour < BEHAVIOUR_KITS)
            next = 0;
        // One at a time round the squad (filled from nothing), until none missing or a round without one.
        while (true) {
            m_panel.SelectSoldier(next);
            const bool given = DropStoreItem(item.first, 1, true);
            next = next + 1 < squadSize ? next + 1 : 0;
            if (given) {
                failed = 0;
                if (--missing < 1)
                    break;
            } else if (++failed >= squadSize) {
                break;
            }
        }
    }
    // The uniforms: the slot's, else its preset's, on a soldier without one.
    for (size_t i = squad.size(); i-- > 0;) {
        m_panel.SelectSoldier(static_cast<int>(i));
        const S_MissionItems::S_Slot &slot = record->slots[i];
        S_SquadSoldier *dressed = m_panel.GetSquadSoldier();
        const bool fresh = SquadSoldier(static_cast<int>(i))->autoEquip;
        if (slot.c != -1) {
            if (dressed->uniform.typeId == -1)
                PutStoreItem(ITEM_FITS_UNIFORM, slot.c, 1, fresh);
        } else if (slot.a != -1) {
            const std::vector<S_LoadoutPreset *> presets = m_presets.GetPresets();
            if (slot.a >= 0 && static_cast<size_t>(slot.a) < presets.size() &&
                presets[static_cast<size_t>(slot.a)]->uniform != -1 && dressed->uniform.typeId == -1)
                PutStoreItem(ITEM_FITS_UNIFORM, presets[static_cast<size_t>(slot.a)]->uniform, 1, fresh);
        }
    }
    m_panel.SelectSoldier(m_soldier);
    m_panel.Refresh();
    m_storeList.UpdateAll();
}

int C_EquipmentMenu::CountOwn(int type, int soldierIndex)
{
    // (Quirk kept: the soldier read unchecked.)
    const S_SquadSoldier *soldier = SquadSoldier(soldierIndex);
    int count = 0;
    for (const S_SoldierItem *place : {&soldier->head, &soldier->uniform, &soldier->backpack, &soldier->longGun, &soldier->hands})
        count += place->typeId == type;
    for (const std::vector<S_SoldierItem> *items : {&soldier->belt, &soldier->backpackContents}) {
        for (const S_SoldierItem &item : *items) {
            if (item.typeId == type)
                count += item.present;
        }
    }
    return count;
}

void C_EquipmentMenu::Replenish(int soldierIndex, int round, int extra)
{
    C_ItemStock *stock = m_storeList.GetStock();
    const int maxAmmo = (round + 6) * round + 6;
    const int maxGrenades = (round + 2) * round + 1;
    m_panel.SelectSoldier(soldierIndex);
    S_SquadSoldier *soldier = m_panel.GetSquadSoldier();
    // An axis uniform: the axis' items only. (Quirk kept: its definition read unchecked.)
    bool axis = false;
    if (soldier->uniform.typeId >= 0 && GetItemDef(soldier->uniform.typeId)->GetTeam() == ITEM_TEAM_AXIS)
        axis = true;
    const auto usable = [axis](const C_ItemDef *def) { return !axis || def->GetTeam() == ITEM_TEAM_AXIS; };
    // The first of a behaviour in the stock he has none of, put at `slot`.
    const auto first = [&](int behaviour, uint32_t slot) {
        for (int type = 0; type < MAX_ITEM_TYPES; ++type) {
            C_ItemDef *def = GetItemDef(type);
            if (stock->Get(type) > 0 && def && def->GetBehaviour() == behaviour && usable(def) &&
                CountOwn(type, soldierIndex) == 0 && PutStoreItem(slot, type, 1, false))
                return true;
        }
        return false;
    };
    // A weapon's ammunition up to `limit` (what he has counted).
    const auto ammo = [&](int type, int limit) {
        // (Quirk kept: the weapon's definition read unchecked.)
        C_ItemDef *def = GetItemDef(type);
        if (def->GetBehaviour() > BEHAVIOUR_12 || def->GetAmmoItemId() < 0 || !usable(def))
            return;
        const int have = stock->Get(def->GetAmmoItemId());
        const int own = CountOwn(def->GetAmmoItemId(), soldierIndex);
        if (have > 0 && own < limit)
            DropStoreItem(def->GetAmmoItemId(), std::min(have, limit - own), false);
    };
    // Every type of a behaviour up to `limit` each.
    const auto upTo = [&](int types, int behaviour, int limit) {
        for (int type = 0; type < types; ++type) {
            C_ItemDef *def = GetItemDef(type);
            if (stock->Get(type) <= 0 || !def || def->GetBehaviour() != behaviour || !usable(def))
                continue;
            const int have = stock->Get(type);
            const int own = CountOwn(type, soldierIndex);
            if (own < limit)
                DropStoreItem(type, std::min(have, limit - own), false);
        }
    };
    if (soldier->backpack.typeId == -1)
        first(BEHAVIOUR_BACKPACK, ITEM_FITS_BACKPACK);
    if (soldier->hands.typeId != -1)
        ammo(soldier->hands.typeId, maxAmmo);
    if (extra != -1)
        ammo(extra, maxAmmo / 6);
    if (soldier->longGun.typeId != -1)
        ammo(soldier->longGun.typeId, maxAmmo);
    // (Quirk kept: the profile read unchecked.)
    const float firstAid = g_gameProfile.m_humanStore->Get(soldier->profileIndex)->stats[STAT_FIRST_AID];
    upTo(KIT_TYPES, BEHAVIOUR_KITS, static_cast<int>(firstAid * 0.02f * static_cast<float>(round)));
    upTo(GRENADE_TYPES, BEHAVIOUR_GRENADE, maxGrenades);
    if (stock->Get(SPECIAL_ITEM) > 0 && GetItemDef(SPECIAL_ITEM) && CountOwn(SPECIAL_ITEM, soldierIndex) < 1)
        DropStoreItem(SPECIAL_ITEM, 1, false);
    if (soldier->head.typeId == -1 && soldier->uniform.typeId != BARE_HEAD_UNIFORM)
        first(BEHAVIOUR_HEADGEAR, ITEM_FITS_HEAD);
}

void C_EquipmentMenu::AssignSlots(const S_MissionItems &record, int *assigned, int count)
{
    // Each slot: its kit's weight, its weapons' kinds (bit 1 a rifle, bit 0 a scoped one), taken.
    struct S_Kit {
        float weight = 0.0f;
        int flags = 0;
        bool taken = false;
    } kits[S_MissionItems::SLOTS];
    const auto weight = [](int type) {
        const C_ItemDef *def = type != -1 ? GetItemDef(type) : nullptr;
        return def ? def->GetWeight() : 0.0f;
    };
    const auto reticle = [](C_ItemDef *d) {
        const C_ItemComponent *scope = d->GetSecondaryComponent();
        return d->GetBehaviour() == ITEM_BEHAVIOUR_RIFLE && scope && scope->GetKind() == ICK_SCOPE &&
               static_cast<const C_ScopeComp *>(scope)->GetAimMode() < RETICLE_MODES;
    };
    const std::vector<S_LoadoutPreset *> presets = m_presets.GetPresets();
    int scoped = 0;
    for (int k = 0; k < count; ++k) {
        const S_MissionItems::S_Slot &slot = record.slots[k];
        S_Kit &kit = kits[k];
        if (slot.a < 0 || static_cast<size_t>(slot.a) >= presets.size()) {
            kit.weight = weight(slot.c);
            kit.weight += weight(slot.b);
            continue;
        }
        const S_LoadoutPreset *preset = presets[static_cast<size_t>(slot.a)];
        kit.weight = weight(slot.c != -1 ? slot.c : preset->uniform);
        kit.weight += weight(slot.b != -1 ? slot.b : preset->head);
        // The weapons: a rifle (scoped: counted).
        const auto weapon = [&](int type, bool keep) {
            C_ItemDef *def = type != -1 ? GetItemDef(type) : nullptr;
            if (!def)
                return;
            kit.weight += def->GetWeight();
            if (def->GetBehaviour() != ITEM_BEHAVIOUR_RIFLE)
                return;
            const int before = keep ? kit.flags : 0;
            kit.flags = before | RIFLE;
            if (reticle(def)) {
                kit.flags = before | RIFLE | SCOPED;
                ++scoped;
            }
        };
        weapon(preset->hands, false);
        weapon(preset->longGun, true);
        kit.weight += weight(preset->backpack);
        for (int i = 0; i < preset->beltCount; ++i)
            kit.weight += weight(preset->belt[i].typeId) * static_cast<float>(preset->belt[i].count);
        for (int i = 0; i < preset->backpackCount; ++i)
            kit.weight += weight(preset->backpackItems[i].typeId) * static_cast<float>(preset->backpackItems[i].count);
    }
    // The two best shots.
    int best = -1, bestIndex = -1, second = -1, secondIndex = -1;
    for (int i = 0; i < count; ++i) {
        // (Quirk kept: the profile read unchecked.)
        const float shooting = g_gameProfile.m_humanStore->Get(SquadSoldier(i)->profileIndex)->stats[STAT_SHOOTING];
        if (static_cast<float>(best) < shooting) {
            second = best;
            secondIndex = bestIndex;
            best = static_cast<int>(shooting);
            bestIndex = i;
        } else if (static_cast<float>(second) < shooting) {
            second = static_cast<int>(shooting);
            secondIndex = i;
        }
    }
    // Each soldier his slot: the best shot a scoped rifle's (a rifle's when none scoped), the second a
    // rifle's (a scoped one's when two), else the heaviest he carries but a scoped rifle's (60 less his
    // spare strength; a rifle's 30 less); the first free at worst.
    for (int s = 0; s < count; ++s) {
        const float strength = g_gameProfile.m_humanStore->Get(SquadSoldier(s)->profileIndex)->stats[STAT_STRENGTH];
        int chosen = -1, score = -1;
        for (int k = 0; k < count; ++k) {
            const S_Kit &kit = kits[k];
            if (kit.taken)
                continue;
            if (chosen == -1) {
                score = 0;
                chosen = k;
            }
            const bool fits = kit.weight <= strength;
            if (s == bestIndex && ((kit.flags & SCOPED) || ((kit.flags & RIFLE) && scoped == 0)) && fits) {
                score = 100;
                chosen = k;
            } else if (s == secondIndex && (kit.flags & RIFLE) && (!(kit.flags & SCOPED) || scoped > 1) && fits) {
                score = 90;
                chosen = k;
            } else if (fits && !(kit.flags & SCOPED)) {
                int value = static_cast<int>(60.0f - (strength - kit.weight));
                if (kit.flags & RIFLE)
                    value -= 30;
                if (score < value) {
                    score = value;
                    chosen = k;
                }
            }
        }
        kits[chosen].taken = true;
        assigned[s] = count == 1 ? 0 : chosen;
    }
}
