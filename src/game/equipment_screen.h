// The campaign's equipment screen (vtable 0x00815db0, 0x590 bytes; the campaign screen kinds 3 and 4):
// the squad soldiers dressed from the operation's items (the items screen: the squad's list, the
// equipment panel, the store, the stats, the soldier types; automatic equipping) and the soldiers
// picked for the mission (the selection screen). Part 9f-i: the class, its construction, the menu
// state's slots (starting, stopping) and the item controls' slots; 9f-ii: the messages and the
// checks before the mission; 9f-iii: the screens' descriptions; 9f-iv: the soldier type picked; 9f-v:
// the automatic equipping.
#pragma once

#include <cstdint>
#include <utility>

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
// The deployed soldier an axis item counts with (CountCarried's; -1: none yet).
extern int g_requiredCarrier;                           // 0x00840d5c

class C_EquipmentMenu : public C_EquipmentMenuBase {
public:
    // The presets loaded, the missions' items set up. (Inline in C_GameApp::StartStep.)
    C_EquipmentMenu();
    // (The members' destructors.)
    ~C_EquipmentMenu() override;                        // 0x006c1e50 (0x006c1e30)

    int GetTypeId() const override { return GAME_STATE_TYPE_EQUIPMENT_MENU; }   // 0x1C 0x006c1e20

    // Escape: on the selection screen closed, else asked whether to go back to the campaign's previous
    // screen. Continue: every soldier deployed (the first campaign's missions 2 and 15 with more
    // than one soldier: the selection screen opened instead), the checks; the selection's accept: the
    // soldier picked deployed alone, the checks. The checks: the required items carried (else their
    // message), the weights within the soldiers' strength (else the overloaded message), someone
    // unarmed asked about; then done: no soldier left to equip automatically, the campaign's next
    // step, the profile's second stock reset.
    void OnMessage(S_ControlMessage *message) override; // 0x34 0x006740d0
    // The items screen (its squad first: four soldiers in a new one; the equipment panel, the squad's
    // list, the store, the stats panel, the buttons and texts; the soldiers to equip equipped) and
    // the selection screen described, made; the first opened, the stats, the item's popup. True.
    bool BuildScreens() override;                       // 0x38 0x00675c80
    // The popup deleted, the panel's soldier, the store's pages emptied.
    void OnStop() override;                             // 0x3C 0x00673b80
    // The equipment screen (kind 3) of a campaign screen: the profile's first stock reset, the
    // screen's items added (those marked: the item's ammunition instead). True.
    bool OnStart(void *arg) override;                   // 0x40 0x006739d0

    // The item controls' notifications: an item clicked twice (from the panel: back to the store, the
    // backpack with its contents, the cursor's input notified as left; from the store: taken, onto
    // the soldier where it goes, what it displaced back), the soldier type picked (the header: his items
    // the mission does not need back, false; a campaign preset of the line - 1: those back, its match's
    // items taken onto him (not its uniform; a hands' weapon unlike the preset's not, nor its
    // ammunition: "Item is not in the list of available items"), the panel, the store), the
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

    // The automatic equipping (all null / 0: only the soldiers to equip; the button's callback
    // CallAutoEquip, the original's registers it directly). No mission record: false. The required
    // items given out, each soldier (the last first) his kit's slot (AssignSlots); unarmed: his
    // unrequired items back, the slot's head gear, its preset's items (the button's: matched to his
    // uniform's side), no more to equip; the button's: replenished (then twice more, with a pistol's
    // ammunition). The panel, the store. True.
    bool AutoEquip(void *argument, C_Control *control, uint32_t notification, uint32_t value);   // 0x00678000
    static bool CallAutoEquip(void *menu, void *argument, C_Control *control, uint32_t notification,
                              uint32_t value);
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
    // The mission's required items each carried by the deployed soldiers (a uniform or the diving
    // gear: one per soldier at most, the record changed); one not: its message, false. (No record:
    // true.)
    bool HasRequiredItems();                            // 0x00678dc0
    // No squad soldier over his strength.
    static bool IsSquadWithinWeight();                  // 0x00674060
    // Every deployed soldier with a weapon (an animated item) in his hands, on his shoulder, his belt
    // or his backpack.
    static bool IsSquadArmed();                         // 0x006746f0
    // How many of an item type (`item.first`) the deployed soldiers carry (a place counts one, the
    // belt's and the backpack's items their counts; an axis item only with the soldier it counted with
    // first, g_requiredCarrier): fewer than `item.second`, 0; else all (`soldier` -1), or the soldier's
    // own, at most the surplus.
    static int CountCarried(const std::pair<int, int> &item, int soldier);   // 0x00679360
    // The screens' builders: the roster loaded when empty (tables\HumanStore.def); a new squad of the
    // first four profiles.
    static void LoadRoster();                           // 0x00673c30
    // An item type (`count`) taken from the store (`fill`: as TakeItem) and put at `slot` (not forced),
    // or dropped where it goes; what is left back. False: not taken, or given back whole.
    bool PutStoreItem(uint32_t slot, int type, int count, bool fill);   // 0x00677920
    bool DropStoreItem(int type, int count, bool fill); // 0x006779f0
    // A squad soldier's items back to the store (the head's with `withHead`; then the long gun, the
    // hands, the belt's, the backpack's, the backpack): those the mission does not need whole, those
    // it does the surplus the shown soldier carries (none: kept).
    void ReturnUnrequired(int soldierIndex, bool withHead);   // 0x00679790
    // The in-game menu's FindSubstitute and MatchPreset (the same scores; `axisOnly`: only the axis'
    // items).
    bool FindSubstitute(int &type, int ammo, bool axisOnly);   // 0x00679b50
    S_LoadoutPreset *MatchPreset(const S_LoadoutPreset &preset, bool axisOnly);   // 0x00679e80
    // The mission's required items listed in the essentials' text and given out round the squad (filled
    // from nothing; the weapons and wear from the first soldier), then the uniforms (the slot's, else
    // its preset's) on those without one; the panel, the store.
    void DistributeRequired();                          // 0x006785b0
    // How many of a type a squad soldier has (a place one, the belt's and the backpack's their counts).
    static int CountOwn(int type, int soldierIndex);    // 0x00679670 (__stdcall)
    // A soldier's stock-up (round 0, 1, 2; `extra` a pistol, -1: none): a backpack, the weapons'
    // ammunition (6, 13, 22 at most; the pistol's a sixth), medkits by his first aid, grenades (1, 4,
    // 9), item 9, a head gear (not with uniform 0x79); an axis uniform: the axis' items only.
    void Replenish(int soldierIndex, int round, int extra);   // 0x00677aa0
    // The record's kits' slots for the squad's `count` soldiers (`assigned` their slots): the best shot
    // a scoped rifle's, the second a rifle's, the others the heaviest they carry.
    void AssignSlots(const S_MissionItems &record, int *assigned, int count);   // 0x0067a3f0
    static void EnsureSquad();                          // 0x00673ca0
    // The equipment panel (as the in-game menu's: the model's "equippanel" its "P_Equipment", the
    // holsters' and backpack's groups, own meshes; the panel's description).
    bool BuildEquipmentPanel(S_ScreenDesc *desc, I3D_frame *model);   // 0x00674860
    // The squad's list: the tables and pictures off, the group "GB_Soldiers" (a dummy of the model) with
    // the template "photo03"; the name's text, the list (4 shown, 0.106 apart); the dead soldiers out of
    // the squad; a picture per soldier (the template's copy, its index, face, OnSoldierPicked); the
    // selector; the first soldier shown. False without the template.
    bool BuildSquadList(S_ScreenDesc *desc, I3D_frame *model);        // 0x006766b0
    // The store: the model's "itemselector01" off; its dummy "P_visualstore" over the filter and the
    // scroll bar, the entries' template "vi_template" with its count ("ammo") and information texts
    // (off); the list's description, its pages emptied, the stock (SetupStore). False without the
    // frames.
    bool BuildStore(S_ScreenDesc *desc, I3D_frame *model);            // 0x00677010
    // The stats panel "Panel02" (hidden, taking no input): per skill its name's title, its bar and
    // its value's title; "Soldier stats :". True.
    static bool BuildStatsPanel(S_ScreenDesc *desc, I3D_frame *model);   // 0x006764a0 (__stdcall)
    // The items screen's controls: the stats button, the soldier types (the campaign's presets), the
    // texts, the automatic equipping, continue and back; the description label (its id kept) when the
    // model has one; the mission essentials' frame off. True.
    bool BuildItemsScreen(S_ScreenDesc *desc, I3D_frame *model);      // 0x006758f0
    // The selection screen: its title, a picture per squad soldier (four at most; the others off), yes
    // (the pick accepted) and cancel; the selector. True.
    bool BuildSelectionScreen(S_ScreenDesc *desc, I3D_frame *model);  // 0x00676230

    uint8_t m_unknown1C4[0xC] = {};                     // +0x1C4 (not set by the original)
    C_EquipmentPanel m_panel;                           // +0x1D0
    C_EquipmentStoreList m_storeList;                   // +0x274 (vtable 0x00815e8c, set at 0x006c1d00)
    C_MissionItems m_missionItems;                      // +0x2AC
    C_LoadoutPresets m_presets;                         // +0x558
    // The selection screen's selector (BuildScreens' "selector") and the squad list's.
    I3D_frame *m_selector = nullptr;                    // +0x578 (not set by the original's constructor)
    I3D_frame *m_soldierSelector = nullptr;             // +0x57C
    // The squad soldier shown (the panel's) and the one picked on the selection screen.
    int m_soldier = 0;                                  // +0x580
    int m_selected = 0;                                 // +0x584
};
