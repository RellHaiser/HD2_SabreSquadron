// The in-game menu (vtable 0x00815750, 0x5C0 bytes): opened over the game (C_game::OpenGameMenu,
// which it wraps and keeps running by the network roles), its screens the mission's objectives, the
// options, saving and loading, and in the network games the equipment and the squad. Part 9e-v:
// the class, its construction and the menu state's slots (starting, stopping, the input, the
// update, the tick, the info screen drawn); 9e-vi: the update's helpers and the drawing; 9e-vii:
// the menu's messages; 9e-ix: the screens' descriptions (the main screen's); 9e-xiii: the player
// screen's (its panels and the item slots later).
#pragma once

#include <cstdint>

#include "game/equipment_menu.h"
#include "game/equipment_panel.h"
#include "game/loadout_presets.h"
#include "game/options_handler.h"
#include "game/soldier.h"
#include "game/store_list.h"

class C_InfoScreen;
class C_ItemStock;
struct S_ControlMessage;

// The menu time (OnTick's sum; nothing reads it) and the in-game menu's message boxes open
// (ShowMessage's count; nothing reads it).
extern int g_gameMenuTime;                              // 0x008ae62c
extern int g_gameMenuMessages;                          // 0x008ae628
// The soldiers panel's last profile picked in each list (the first 30 profiles; the next 30).
extern int g_pickedSoldierA;                            // 0x008ae634
extern int g_pickedSoldierX;                            // 0x008410ac (30 at first)

class C_GameMenu : public C_EquipmentMenuBase {
public:
    // The presets loaded (the original's presets constructor); single player: the missions' items
    // set up.
    C_GameMenu();                                       // 0x0067eb40
    // (The game behind is not deleted; the members' destructors.)
    ~C_GameMenu() override;                             // 0x0067f4a0 (0x0067f480)

    int GetTypeId() const override { return GAME_STATE_TYPE_GAME_MENU; }   // 0x1C 0x0067f470

    // The options handler's first. Escape: the info screen hidden, else back from the screens 2 /
    // 3 (not in the warmup's menu), else back to the game (single player: not with the objectives
    // decided; a network game: the team changed first, else the squad's edited soldier dropped).
    // The squad's soldier applied (and the spawn asked for) or dropped, then escape; the quit and
    // restart questions and their answers (the app's commands, the mission ended).
    void OnMessage(S_ControlMessage *message) override; // 0x34 0x0067f850
    // The screens described (the main and options screens; a network game's player screen), their
    // models loaded (none: no screen), the builders' controls added, the screens made; then the
    // first opened (the warmup's menu: the player screen). True.
    bool BuildScreens() override;                       // 0x38 0x0067fe00
    // The volume restored, the options handler, the popup and the network game's info screen
    // deleted, the panel's soldier, the two stocks emptied and deleted.
    void OnStop() override;                             // 0x3C 0x0067f760
    // Whether the player's team has a member in the player slots, the options handler, the sound
    // muted; the network games: the info screen. True.
    bool OnStart(void *arg) override;                   // 0x40 0x0067f650
    // The game's input (none). True.
    bool OnInput(float *held, float *pressed) override; // 0x44 0x00680960
    // The network games: the panel's soldier, the load's button, the game's status; a client: the
    // game's update.
    void OnUpdate(int timeDelta) override;              // 0x48 0x006808d0
    // The menu time; the server: the game's tick.
    void OnTick(int timeDelta) override;                // 0x4C 0x00680920
    // By the top screen: the equipment screen's (index 3, or the message box over it) or the game's
    // behind the menu; no screen: the game's scene alone (the game behind; else false).
    bool OnRender() override;                           // 0x50 0x006824f0
    // The info screen drawn when shown; false.
    bool OnDrawScreens() override;                      // 0x54 0x00682770

    // The item controls' notifications: an item clicked twice (from the panel: back to the store, the
    // backpack with its contents; from the store: taken, onto the soldier where it goes, a large
    // weapon not with another; what it displaced back; the store not having it: false), (not
    // ported yet), the drag's start (an item of a type the control has: one of it; from the store
    // the store's when it has one, a large weapon not with another; the places it may go shown, its
    // visual's copy over it), its move (the copy by the cursor's move), its drop (the copy deleted,
    // the places hidden; DropItem on the control under the cursor); the hover (not ported yet).
    bool OnItemNotify13(void *argument, C_Control *control, uint32_t notification, uint32_t value) override;   // 0x5C 0x00681ee0
    bool OnItemNotify60(void *argument, C_Control *control, uint32_t notification, uint32_t value) override;   // 0x60 0x00686e10
    bool OnItemNotify0A(void *argument, C_Control *control, uint32_t notification, uint32_t value) override;   // 0x64 0x00682020
    bool OnItemNotify08(void *argument, C_Control *control, uint32_t notification, uint32_t value) override;   // 0x68 0x00682210
    bool OnItemNotify09(void *argument, C_Control *control, uint32_t notification, uint32_t value) override;   // 0x6C 0x006822b0
    bool OnItemHover(void *argument, C_Control *control, uint32_t notification, uint32_t value) override;      // 0x70 0x00680ea0

    void SetGame(C_GameState *game) { m_game = game; }
    C_GameState *GetGame() const { return m_game; }
    // The message box with the text (counted), unless one is on top; `textId` below 1: the box
    // closed (the count down). (The second argument is not used; the game passes -1.)
    void ShowMessage(int textId, int unused);           // 0x00682aa0
    // The warmup ended without a uniform: applying the squad's soldier asks for the spawn.
    void SetRespawnOnApply(bool on) { m_respawnOnApply = on; }

private:
    // The main screen's buttons (single player: the tutorial's end, back or the mission's end, the
    // restart, the quick save and load, the options, the quit; a network game: back, the options, the
    // player screen, the quit) and the version's text. True. (The model is not used.)
    bool BuildMainScreen(S_ScreenDesc *desc, I3D_frame *model);   // 0x00680980
    // The player screen's controls: the panels (the soldiers, the equipment, the store's items, the
    // skills), the screen's shown / hidden callbacks; the soldier types' combo box (its id kept, its
    // items filled), the presets' save and rename buttons, the warmup's and the time's texts (a
    // cooperative game: the version's and the equipment's), accept and back, the chat input, the
    // texts; the game info's button (the info screen shown). A cooperative game: the player status's
    // text and the description popup (the menu's popup, its model "DESCRIPTION"); else the weapons'
    // information label when the model has one (its id kept). The panel's soldier in the model's
    // "3Dview". True.
    bool BuildPlayerScreen(S_ScreenDesc *desc, I3D_frame *model);   // 0x00682c80
    // The dragged item dropped on the control `target` (0: none): from the panel onto another
    // control, back to the store (the backpack with its contents; the place's count), else moved
    // there (what it displaced back to the store); from the store onto the panel, taken and put
    // there (what it displaced back, unless of its type).
    bool DropItem(C_Control *control, uint32_t target);   // 0x00682350
    // The soldiers panel (none without profiles: false): the model's lists' frames made under
    // "soldierspanel" (GB_Soldiers_a, GB_Soldiers_x), "photo03" (the soldiers' pictures' template,
    // in the first) and "selector" (its copy in the second) moved into them, the tables hidden (no
    // photo, selector or list frame: false); the panel's group, its scroll bar and its two hiding
    // lists of the profiles' pictures (the first 30, then up to 60; their faces, picked:
    // OnSoldierPicked); not co-op: the allies' and axis' marks (OnSide). The template hidden; true.
    bool BuildSoldiersPanel(S_ScreenDesc *desc, I3D_frame *model);   // 0x006838e0
    // The equipment panel (the model's dummy "equippanel" renamed "P_Equipment"; none, or a group's
    // dummy not made: false): the holsters' and backpack's groups (dummies under it, kept in the
    // models), the soldier's places' visuals given meshes of their own, the scroll bars and their
    // texts under the panel, the holsters' and backpack's items (own meshes) under their groups; the
    // panel's description (the menu its). True.
    bool BuildEquipmentPanel(S_ScreenDesc *desc, I3D_frame *model);  // 0x00684690
    // The store's items panel: the stocks set up; the model's dummy "allitemspanel" renamed
    // "P_visualstore", "soldier type01" (the filter, renamed) and the dummy "bscroll" (not checked)
    // moved under it, the visuals item10..15 and selitem08..15 hidden; "item08" (renamed
    // "vi_template") and "item09" (under it, off, renamed "ammo") (a frame missing: false); the store
    // list's description (the menu its). True.
    bool BuildItemsPanel(S_ScreenDesc *desc, I3D_frame *model);      // 0x00684b90
    // The skills panel "Panel02" (taking no input): per skill its name's title (text 0xA78 + the
    // skill), its bar (along z, empty) and its value's title. True. (The model is not used.)
    static bool BuildSkillsPanel(S_ScreenDesc *desc, I3D_frame *model);   // 0x00684df0 (__stdcall)
    // The two stocks made (every type -1), the profile's stock reset; the map's items (a known type
    // of a team) in the stocks the team's soldiers take from (the second for the items not of team
    // 0, the first for those not of team 1), with their counts (the list's, else 1; the
    // inventory group 5's 10) and their seconds' (a weapon's ammunition, 10; a uniform's head gear,
    // 1; the list's ammunition count when given), a type not set yet only. True.
    bool SetupStocks();                                 // 0x00685170 (__fastcall)
    // The soldier types' combo box filled: emptied, its header and first line text 500; the user
    // presets of the squad's team, then the presets the map allows (their names in its list;
    // counted); a preset's line its name's text, else its name.
    void UpdateSoldierTypes();                          // 0x00685da0 (__fastcall)
    // The squad soldier's profile and places kept in the user preset (the team's): its belt's and
    // backpack's items counted by type, the last first (13 and 31 types at most).
    void StorePreset(int index);                        // 0x00687bc0
    // The player screen's callbacks: shown (the squad's soldiers made as the slots need: one, a
    // second as a copy of the first for a player slot member; the panel's soldier the first or the
    // second; its team set, none: asked for, the allies in co-op, else 2), hidden (true), the soldier type's
    // header edited (the user preset renamed, stored and saved; an empty name: edited again), the
    // preset saved (stored; a preset named by its text renamed "<the profile's name><its number>",
    // the list refilled, its header the name; the presets saved), the game info's click (the info
    // screen shown; true),
    // the chat input's line sent ("/t": to the team, "/a": to all, either skipped with the spaces
    // after; the net game's chat; true).
    static bool OnPlayerScreenShown(void *menu, void *argument, C_Control *control, uint32_t notification,
                                    uint32_t value);    // 0x00684fd0 (__thiscall)
    static bool OnPlayerScreenHidden(void *menu, void *argument, C_Control *control, uint32_t notification,
                                     uint32_t value);   // 0x00684fc0
    static bool OnSoldierType(void *menu, void *argument, C_Control *control, uint32_t notification,
                              uint32_t value);          // 0x00687dd0 (__thiscall)
    static bool OnSavePreset(void *menu, void *argument, C_Control *control, uint32_t notification,
                             uint32_t value);           // 0x00687f50 (__thiscall)
    static bool OnGameInfo(void *menu, void *argument, C_Control *control, uint32_t notification,
                           uint32_t value);             // 0x006861b0 (__thiscall)
    static bool OnChat(void *menu, void *argument, C_Control *control, uint32_t notification,
                       uint32_t value);                 // 0x00682a20 (__thiscall)
    // A soldier's picture clicked: its profile (the control's index) kept for its list, its stats
    // shown, the squad soldier's profile set (level 0), the panel refreshed and its face; true.
    static bool OnSoldierPicked(void *menu, void *argument, C_Control *control, uint32_t notification,
                                uint32_t value);        // 0x00685420 (__thiscall)
    // A side's mark clicked (not co-op; else false): the allies' or the axis' (another control:
    // false; none: no side), the team asked for when it is not the squad's; true.
    static bool OnSide(void *menu, void *argument, C_Control *control, uint32_t notification,
                       uint32_t value);                 // 0x006854e0 (__thiscall)
    // A profile's stats on the skills panel (its skills at the local player's rank; no profile: the
    // defaults): the bars (a hundredth each; health full) and their values' texts (shooting,
    // endurance, stealth, first aid, lockpick in per cent, strength in kg, health as it is).
    static void ShowSoldierStats(int profile);          // 0x006863c0 (__stdcall)
    // The squad's team set (2: nothing): the message box closed; the team change posted and now
    // the first soldier's: the escape posted; the same team: nothing more. Not co-op: its side's mark
    // checked, the other's not (another team: nothing more). No team before: the store's list on,
    // the soldier types on; else the load's button off, the panel's items back to the store's
    // stock. The store's pages emptied; the team kept (the panel's soldier's too), its stock the
    // store's (the allies' the first, the axis' the second); its soldiers' list, the store's items
    // taken, the soldier types.
    void SetTeam(int team);                             // 0x00685550
    // The team's soldiers' list shown (the allies' first list, the axis' second; the scroll bar
    // its, its last pick selected) and its last pick the squad soldier's profile (another team:
    // the first profile), its stats, the panel refreshed, its face and dress.
    void ShowTeamSoldiers(int team);                    // 0x00685ab0
    // The squad soldier's items taken from the store's stock (those it has not: dropped; the
    // backpack's with its contents; the belt's and the backpack's the last first), its load.
    void TakeSoldierItems();                            // 0x00685880 (__fastcall)
    // Control 0x0CA00000 on while the squad soldier has a uniform and his load is within his
    // strength (posted when it changes).
    void UpdateLoadButton();                            // 0x006869a0 (__fastcall)
    // The network game's status: the cooperative game's players (their ranks and names, their
    // humans' states); else the teams' players, the respawn's (occupation) and the round's time.
    static void UpdateNetStatus();                      // 0x006802a0
    // The game behind (without its render callback; the game's states 5 / 6 cleared / set), then
    // the menu's scene (under a popup twice: the screen under it, then the top); true. No game: false.
    bool RenderGame();                                  // 0x006825d0
    // The menu's scene, then (the info screen not shown) the panel's soldier and the popup's popup over
    // the top; true.
    bool RenderEquipment();                             // 0x00682790

    int m_unknown1C4 = 0;                               // +0x1C4 (not set by the original)
    int m_unknown1C8 = 0;                               // +0x1C8 (not set by the original)
    int m_unknown1CC = 0;                               // +0x1CC (not set by the original)
    bool m_teamChangePosted = false;                    // +0x1D0 (OnMessage's escape)
    bool m_respawnOnApply = false;                      // +0x1D1 (SetRespawnOnApply)
    int m_team = -1;                                    // +0x1D4 the squad's team to keep (-1: none; inferred)
    int m_unknown1D8 = -1;                              // +0x1D8
    int m_soldierTypes = -1;                            // +0x1DC the soldier types' combo box's id
    bool m_loadButtonOn = false;                        // +0x1E0 (UpdateLoadButton's)
    C_ItemStock *m_stock1E4 = nullptr;                  // +0x1E4
    C_ItemStock *m_stock1E8 = nullptr;                  // +0x1E8
    C_OptionsHandler *m_options = nullptr;              // +0x1EC (OnStart .. OnStop)
    C_MissionItems m_missionItems;                      // +0x1F0
    C_LoadoutPresets m_presets;                         // +0x49C
    C_InfoScreen *m_infoScreen = nullptr;               // +0x4BC the network games' (OnStart .. OnStop)
    int m_mapPresets = 0;                               // +0x4C0 the map's presets listed (UpdateSoldierTypes)
    int m_userPreset = -1;                              // +0x4C4 the user preset edited (-1: none)
    uint8_t m_unknown4C8[0x10] = {};                    // +0x4C8 (not set by the original)
    bool m_unknown4D8 = false;                          // +0x4D8
    bool m_unknown4D9 = false;                          // +0x4D9
    bool m_hasPlayerSlotMember = false;                 // +0x4DA (OnStart's)
    C_EquipmentPanel m_panel;                           // +0x4DC
    C_StoreList m_storeList;                            // +0x580
    C_GameState *m_game = nullptr;                      // +0x5B8 the game behind the menu
};
