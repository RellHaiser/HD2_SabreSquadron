// The campaign's equipment screen (vtable 0x00815db0, 0x590 bytes; the campaign screen kinds 3 and 4):
// the squad soldiers dressed from the operation's items (the items screen: the squad's list, the
// equipment panel, the store, the stats, the soldier types; automatic equipping) and the soldiers
// picked for the mission (the selection screen). Part 9f-i: the class, its construction, the menu
// state's slots (starting, stopping) and the item controls' slots.
#pragma once

#include <cstdint>

#include "game/equipment_menu.h"
#include "game/equipment_panel.h"
#include "game/loadout_presets.h"
#include "game/soldier.h"
#include "game/store_list.h"

class C_Control;
class I3D_frame;
struct S_ControlMessage;

// The stats shown instead of the panel (OnStatsToggle's state).
extern bool g_equipmentStatsShown;                      // 0x008ae61a

class C_EquipmentMenu : public C_EquipmentMenuBase {
public:
    // The presets loaded, the missions' items set up. (Inline in C_GameApp::StartStep.)
    C_EquipmentMenu();
    // (The members' destructors.)
    ~C_EquipmentMenu() override;                        // 0x006c1e50 (0x006c1e30)

    int GetTypeId() const override { return GAME_STATE_TYPE_EQUIPMENT_MENU; }   // 0x1C 0x006c1e20

    // Part 9f-ii.
    void OnMessage(S_ControlMessage *message) override; // 0x34 0x006740d0
    // Part 9f-iii.
    bool BuildScreens() override;                       // 0x38 0x00675c80
    // The popup deleted, the panel's soldier, the store's pages emptied.
    void OnStop() override;                             // 0x3C 0x00673b80
    // The equipment screen (kind 3) of a campaign screen: the profile's first stock reset, the
    // screen's items added (those marked: the item's ammunition instead). True.
    bool OnStart(void *arg) override;                   // 0x40 0x006739d0

    // The item controls' notifications: an item clicked twice (from the panel: back to the store, the
    // backpack with its contents, the cursor's input notified as left; from the store: taken, onto
    // the soldier where it goes, what it displaced back), (part 9f-iv: the soldier type picked), the
    // drag's start (an item of a type the control has: one of it; the places it may go shown, its
    // visual's copy over it), its move, its drop (the copy deleted, the places hidden; DropItem on the
    // control under the cursor); the hover (not the store's items): the popup's text the item's name
    // (shown), hidden when left.
    bool OnItemNotify13(void *argument, C_Control *control, uint32_t notification, uint32_t value) override;   // 0x5C 0x006731d0
    bool OnItemNotify60(void *argument, C_Control *control, uint32_t notification, uint32_t value) override;   // 0x60 0x00678fe0
    bool OnItemNotify0A(void *argument, C_Control *control, uint32_t notification, uint32_t value) override;   // 0x64 0x00673450
    bool OnItemNotify08(void *argument, C_Control *control, uint32_t notification, uint32_t value) override;   // 0x68 0x006735f0
    bool OnItemNotify09(void *argument, C_Control *control, uint32_t notification, uint32_t value) override;   // 0x6C 0x00673690
    bool OnItemHover(void *argument, C_Control *control, uint32_t notification, uint32_t value) override;      // 0x70 0x00673080
    // The stats button: the stats shown instead of the panel (its texts and the soldier types hidden;
    // the button's text 0xAAF) or the panel again (refreshed; 0xAB0). True.
    virtual bool OnStatsToggle(void *argument, C_Control *control, uint32_t notification, uint32_t value);     // 0x74 0x006755f0
    // The selection screen's soldier picked: its selector moved onto the picture (its own depth kept),
    // the soldier kept (the picture's index). True.
    virtual bool OnSelectionPicked(void *argument, C_Control *control, uint32_t notification, uint32_t value); // 0x78 0x006773a0

    // The controls' callbacks for the slots 0x74 and 0x78 (thunks 0x00677880, 0x00677890).
    static bool CallStatsToggle(void *menu, void *argument, C_Control *control, uint32_t notification,
                                uint32_t value);
    static bool CallSelectionPicked(void *menu, void *argument, C_Control *control, uint32_t notification,
                                    uint32_t value);

private:
    // The dragged item dropped on the control `target` (0: none): from the panel onto another of its
    // controls, moved there (what it displaced back to the store); then, the cursor right of the
    // screen's middle, back to the store (the backpack with its contents; the place's count). From
    // the store onto the panel: taken and put there (what it displaced back, unless of its type).
    // True.
    bool DropItem(C_Control *control, uint32_t target); // 0x006771e0
    // The squad soldier shown's stats on the stats panel (its name, its skills' bars and values; no
    // profile: the bars empty, the values and a text of the selection screen blank).
    void ShowSoldierStats();                            // 0x00674d60
    // The squad list's soldier picked: its selector moved onto the picture (its own depth kept), the
    // soldier shown (the picture's index; the panel refreshed, the stats). True.
    static bool OnSoldierPicked(void *menu, void *argument, C_Control *control, uint32_t notification,
                                uint32_t value);        // 0x00677490 (__thiscall)
    // The store's stock: not a single mission, the profile's first (kind 3) or second (kind 4); a
    // single mission, the first, and the squad's items taken from it (TakeSquadItems).
    void SetupStore();                                  // 0x00675c00 (__fastcall)
    // Every squad soldier's items taken from the store's stock (the last soldier first; those it has
    // not: dropped; the backpack's with its contents; the belt's and the backpack's the last first),
    // their loads.
    void TakeSquadItems();                              // 0x00673730 (__fastcall)

    uint8_t m_unknown1C4[0xC] = {};                     // +0x1C4 (not set by the original)
    C_EquipmentPanel m_panel;                           // +0x1D0
    C_StoreList m_storeList;                            // +0x274
    C_MissionItems m_missionItems;                      // +0x2AC
    C_LoadoutPresets m_presets;                         // +0x558
    // The selection screen's selector (BuildScreens' "selector") and the squad list's.
    I3D_frame *m_selector = nullptr;                    // +0x578 (not set by the original's constructor)
    I3D_frame *m_soldierSelector = nullptr;             // +0x57C
    // The squad soldier shown (the panel's) and the one picked on the selection screen.
    int m_soldier = 0;                                  // +0x580
    int m_selected = 0;                                 // +0x584
};
