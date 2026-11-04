// The campaign's squad screen (vtable 0x00815d50, 0x2B0 bytes; the campaign screen kinds 1 and 2): the
// soldiers of the roster picked for the operation into the squad's four slots (the first the
// player's), their stats and medals, the selection screen. Part 9g-ix: the class, the slots, starting,
// stopping, the messages, the squad kept to the slots; 9g-x: building the screens, the selection screen;
// 9g-xi and on: the squad screen's parts.
#pragma once

#include <cstdint>

#include "game/menu_state.h"

class C_Control;
class I3D_frame;
struct S_ScreenDesc;
struct S_SoldierProfile;

// The soldiers each operation of the first campaign gives the Defence Medal (medal 6) on its squad
// screen of kind 2 (roster indexes; a negative one ends them).
extern const int g_operationMedalSoldiers[8][10];      // 0x00841cb8

// A soldier's shown name: his own (the player's), else the full one.
const C_str &GetProfileName(const S_SoldierProfile *profile);   // 0x006755d0 (__fastcall)

// One of the squad's places on the screen: the roster soldier in it, his picture, name and rank.
struct S_SquadSlot {                                    // 0x14 bytes
    // The soldier `profile` (negative: none) put in, locked or not: his face, the picture's index, the
    // texts.
    void SetSoldier(int profile, bool locked);          // 0x00690ef0
    // His name (style 0xC) and his rank's (style 2); no such soldier: both blank (style 0xB); a
    // negative index of an existing one: nothing.
    void ShowTexts() const;                             // 0x006910d0 (__fastcall)
    // `id` one of the slot's controls.
    bool HasControl(uint32_t id) const;                 // 0x00690ec0

    bool locked = true;                                 // +0x00 (not to be changed by the player)
    int profile = -1;                                   // +0x04 the roster soldier (-1: none)
    uint32_t picture = 0xFFFFFFFF;                      // +0x08 the controls' ids
    uint32_t name = 0xFFFFFFFF;                         // +0x0C
    uint32_t rank = 0xFFFFFFFF;                         // +0x10
};

class C_SquadMenu : public C_MenuState {
public:
    // (Inline in C_GameApp::StartStep; the destructor is the base's: 0x006c1bb0.)
    C_SquadMenu() = default;

    int GetTypeId() const override { return GAME_STATE_TYPE_SQUAD_MENU; }   // 0x1C 0x006c1ba0

    // Escape: on the selection screen closed, else asked whether to go back to the campaign's previous
    // screen. SQUAD_MSG_CONTINUE: the selected slot's soldier swapped into the first (the player's),
    // the squad kept to the slots, the next step. SQUAD_MSG_SELECT_FREE: SelectFreeSoldier.
    void OnMessage(S_ControlMessage *message) override; // 0x34 0x00691310
    // The squad screen ("team members selection") and, for the first operation but the lone wolf's, the
    // selection screen ("SELECTION SCREEN"): described, made; the dead taken off the squad before, the
    // squad screen opened, the slots filled, the first free soldier selected; the medal popup
    // ("DESCRIPTION") made. True.
    bool BuildScreens() override;                       // 0x38 0x00691570
    // The description popup deleted.
    void OnStop() override;                             // 0x3C 0x00693a50
    // The roster loaded when empty (tables\HumanStore.def; failing: false). A campaign's kind 2 screen:
    // the operation's soldiers given the Defence Medal (the living without it), the squad's items back
    // to the stock. True.
    bool OnStart(void *arg) override;                   // 0x40 0x00690be0

    // One soldier only: the lone wolf mode, or the first campaign's missions 2 and 15 played on their
    // own.
    static bool IsOneSoldier();                         // 0x00690ba0

protected:
    // The roster soldier `profile` in one of the slots.
    bool IsInSlots(int profile) const;                  // 0x006956a0
    // The first speech set 1 .. 4 no squad soldier has (all taken: -1).
    static int FirstFreeSpeechSet();                    // 0x00695dc0
    // The squad kept to the slots: those of no slot freed (no roster soldier), the slots' soldiers
    // given a squad soldier (a freed one, else a new one; equipped by the equipment screen on a kind 1
    // screen), the squad ordered as the slots, the freed deleted, speech sets given. True. (Also a
    // button's callback, part 9g-xii.)
    static bool SyncSquad(void *menu, void *argument, C_Control *control, uint32_t notification,
                          uint32_t value);              // 0x00695e20
    // The first roster soldier from `index` on (at most the 30th) of no slot selected, else the last
    // before it (its control on the top screen); a negative `index`: nothing.
    void SelectFreeSoldier(int index);                  // 0x00694dd0
    // A soldier's control picked (the roster's or a slot's): selected, his stats shown, the title.
    // (Part 9g-xii.)
    static bool OnSoldierControl(void *menu, void *argument, C_Control *control, uint32_t notification,
                                 uint32_t value);       // 0x00694bc0

    // A campaign's dead squad soldiers (the last first): their items back, deleted.
    static void RemoveDeadSoldiers();                   // 0x00694810
    // The squad's soldiers into the slots in turn: their roster controls hidden, locked (a kind 1
    // screen's not deployed soldier: not).
    void FillSlots();                                   // 0x00695b90 (__fastcall)
    // The squad screen: the roster, the stats, the panel, the medals; its tick and hidden callbacks; the
    // selected soldier's name (an edit box, inactive; Return: renamed), his info button, the stats' title;
    // the selected soldier, the buttons. True.
    bool DescribeSquadScreen(S_ScreenDesc *desc, I3D_frame *model);   // 0x00691fa0
    // The selection screen: its shown callback, the title, the slots' pictures (photo03 .. 06: their own
    // meshes, the slot's index; picked: OnSelectionPicked), yes (SQUAD_MSG_CONTINUE, closed) and cancel
    // (closed); the selector. True.
    bool DescribeSelectionScreen(S_ScreenDesc *desc, I3D_frame *model);   // 0x00691b30
    // A slot's picture picked: the selector over it (its own depth), its index the slot selected. True.
    static bool OnSelectionPicked(void *menu, void *argument, C_Control *control, uint32_t notification,
                                  uint32_t value);      // 0x00691cc0
    // The selection screen shown: the slots' faces on their pictures, an empty slot's hidden. True.
    static bool OnSelectionShown(void *menu, void *argument, C_Control *control, uint32_t notification,
                                 uint32_t value);       // 0x00691db0

    // (Parts 9g-xi .. 9g-xiii.) The squad screen's roster, stats, panel, medals, selected soldier and
    // buttons; its callbacks.
    bool DescribeRoster(S_ScreenDesc *desc, I3D_frame *model);       // 0x00692100
    bool DescribeStats(S_ScreenDesc *desc, I3D_frame *model);        // 0x00693320
    bool DescribePanel(S_ScreenDesc *desc, I3D_frame *model);        // 0x006934f0
    void DescribeMedals(S_ScreenDesc *desc, I3D_frame *model);       // 0x00692c40
    bool DescribeSelected(S_ScreenDesc *desc, I3D_frame *model);     // 0x00693560
    bool DescribeButtons(S_ScreenDesc *desc, I3D_frame *model);      // 0x00693880
    static bool OnScreenTick(void *menu, void *argument, C_Control *control, uint32_t notification,
                             uint32_t value);           // 0x00696d60
    static bool OnScreenHidden(void *menu, void *argument, C_Control *control, uint32_t notification,
                               uint32_t value);         // 0x00696ef0
    static bool OnNameEdited(void *menu, void *argument, C_Control *control, uint32_t notification,
                             uint32_t value);           // 0x00696c90
    static bool OnSoldierInfo(void *menu, void *argument, C_Control *control, uint32_t notification,
                              uint32_t value);          // 0x00694930

    int m_selectedSlot = 0;                             // +0x1B0 the slot picked (0 .. 3)
    int m_selectedSoldier = -1;                         // +0x1B4 the roster soldier picked (-1: none)
    bool m_rosterSelected = true;                       // +0x1B8 the roster's (else a slot's)
    int m_unknown1BC = 0;                               // +0x1BC
    int m_unknown1C0 = 0;                               // +0x1C0
    I3D_frame *m_selector = nullptr;                    // +0x1C4 the selection screen's (over the slot picked)
    S_SquadSlot m_slots[4];                             // +0x1C8
    uint8_t m_unknown218[0x98] = {};                    // +0x218
};
