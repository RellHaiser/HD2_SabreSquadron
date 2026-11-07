// The campaign's squad screen (vtable 0x00815d50, 0x2B0 bytes; the campaign screen kinds 1 and 2): the
// soldiers of the roster picked for the operation into the squad's four slots (the first the
// player's), their stats and medals, the selection screen. Part 9g-ix: the class, the slots, starting,
// stopping, the messages, the squad kept to the slots; 9g-x: building the screens, the selection screen;
// 9g-xi: the roster and the medals; 9g-xii: the stats, the places, the buttons, a soldier picked; 9g-xiii: a
// soldier assigned or detached; 9g-xiv: the dragging; 9g-xv: the automatic selection; 9g-xvi: the stats and
// medals shown, the name edited, the screen's tick and hiding.
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
// The medals the squad screen shows (S_SoldierProfile::unknown28's ids).
constexpr int SQUAD_MEDALS = 12;

// The squad screen's other panel shown instead of the stats (the info button's; not reset with the screen).
extern bool g_squadInfoShown;                          // 0x008ae692
// A soldier's shown name: his own (the player's), else the full one.
const C_str &GetProfileName(const S_SoldierProfile *profile);   // 0x006755d0 (__fastcall)

// One of the squad's places on the screen: the roster soldier in it, his picture, name and rank.
struct S_SquadSlot {                                    // 0x14 bytes
    // The soldier `profile` (negative: none) put in, removable or not: his face, the picture's index, the
    // texts.
    void SetSoldier(int profile, bool removable);       // 0x00690ef0
    // His name (style 0xC) and his rank's (style 2); no such soldier: both blank (style 0xB); a
    // negative index of an existing one: nothing.
    void ShowTexts() const;                             // 0x006910d0 (__fastcall)
    // `id` one of the slot's controls.
    bool HasControl(uint32_t id) const;                 // 0x00690ec0

    bool removable = true;                              // +0x00 the player may detach or replace him
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
    // A soldier's picture picked: his index selected (none: false), his stats shown; the roster's: "Assign", a
    // slot's: "Detach" (the assign button's text), the roster's selector over it; another control's: none
    // selected, false.
    static bool OnSoldierControl(void *menu, void *argument, C_Control *control, uint32_t notification,
                                 uint32_t value);       // 0x00694bc0
    // The roster's selector (if any) on the control's frame, just before it, shown. True.
    static bool PlaceRosterSelector(void *menu, void *argument, C_Control *control, uint32_t notification,
                                    uint32_t value);    // 0x00695d30

    // A campaign's dead squad soldiers (the last first): their items back, deleted.
    static void RemoveDeadSoldiers();                   // 0x00694810
    // The squad's soldiers into the slots in turn: their roster controls hidden, removable only on a kind 2
    // screen or when not deployed.
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

    // The roster: the pictures' templates (photo03 .. 07) off; one soldier: the other places (table19 .. 21,
    // member01 .. 03) half opaque; the list's frame "GB_Soldiers" under "panel01" (the dead's template, with
    // the "death" frame on it, moved in), the selector off at the origin; the group "Panel01": the list (its
    // scroll bar, 5 shown, across by 0.0876 from the template's place) of the roster's first 30 (a copy of
    // the dead's template or the living's, photo04: the index, the face, picked: OnSoldierControl; the
    // living's dragged and dropped), the scroll bar "scroll00"; the template off. A frame missing (the
    // selector, the dead's template, the scroll bar; the list's not made): false.
    bool DescribeRoster(S_ScreenDesc *desc, I3D_frame *model);       // 0x00692100
    // The medals: their panel's frame "medals_panel" under the model (kept with the models), the group;
    // each medal's two frames (DSO_a, DSO_b, ..: not checked) under it, their pictures (the medal's index;
    // hovered: OnMedalHover), its place ("medal01" ..). (No frame made: nothing.)
    void DescribeMedals(S_ScreenDesc *desc, I3D_frame *model);       // 0x00692c40
    // A medal's picture hovered: the popup shown with a copy of the medal's first frame ("Medal"), its name
    // and description; left: hidden. (No popup, no control: nothing.) True.
    static bool OnMedalHover(void *menu, void *argument, C_Control *control, uint32_t notification,
                             uint32_t value);           // 0x00690d50
    // A soldier's picture dragged (CONTROL_NOTIFY_0A): another's picked first (failing: none selected,
    // false); a staying slot's soldier: none selected, false; from the slots' half (x > 0.5): to be detached,
    // from the roster's: to be assigned (the assign button's text); a copy of the picture above it (its own
    // visual, in the scene's primary sector), the picture hidden. True.
    static bool OnDragStart(void *menu, void *argument, C_Control *control, uint32_t notification,
                            uint32_t value);            // 0x00693ae0
    // Moved (CONTROL_NOTIFY_08): the copy by the cursor's move (none selected: false).
    static bool OnDragMove(void *menu, void *argument, C_Control *control, uint32_t notification,
                           uint32_t value);             // 0x00693e50
    // Dropped (CONTROL_NOTIFY_09): the copy released. On the slots' half: a roster soldier onto a removable
    // slot's soldier replaces him; a slot's soldier swaps places with the slot under the cursor (its
    // picture clicked; none, itself: shown again). Else assigned (a roster soldier; back on the roster's
    // half: shown again) or detached (a slot's soldier, unless he stays: shown again, false); the cursor's
    // notification 05. (None selected: false.)
    static bool OnDrop(void *menu, void *argument, C_Control *control, uint32_t notification,
                       uint32_t value);                 // 0x00693f00
    // CONTROL_NOTIFY_13 (as the equipment screen's items' 13): assigned or detached by OnAssign, the
    // notification 05; a staying slot's soldier on the slots' half: shown again, false. (None selected:
    // false.)
    static bool OnSoldierNotify13(void *menu, void *argument, C_Control *control, uint32_t notification,
                                  uint32_t value);      // 0x006945c0

    // The stats' panel "Panel02" (taking no input): the skills' rows (their names 0xA6E + the skill, bars
    // hlcpct00 .. 06, values). True.
    bool DescribeStats(S_ScreenDesc *desc, I3D_frame *model);        // 0x00693320
    // The other panel "Panel03" (hidden, taking no input; its text "text_05"). True.
    bool DescribePanel(S_ScreenDesc *desc, I3D_frame *model);        // 0x006934f0
    // The selected soldiers' places: their frames (each slot's name, rank and picture; the pictures' own
    // meshes) under a frame "P_SelectedSol" of the model's (kept with the models); the group: each slot's
    // texts and picture (its controls' ids the slot's; no soldier; picked: OnSoldierControl; dragged and
    // dropped). No frame made: false.
    bool DescribeSelected(S_ScreenDesc *desc, I3D_frame *model);     // 0x00693560
    // The buttons (their frames' names not their texts'): "ADD" / "REMOVE" (OnAssign), "AUTOMATIC SELECTION"
    // on "baccept and continue" (OnAutoSelect), "CONTINUE" on "bauto assign" (the first operation, not the
    // lone wolf's: the selection screen opened; else the squad kept, the next step), "BACK" (Escape); the
    // screen's title ("TEAM MEMBER SELECTION", a kind 1 screen's "REPLENISH TEAM"), the version, the
    // draft's text. True.
    bool DescribeButtons(S_ScreenDesc *desc, I3D_frame *model);      // 0x00693880
    // The assign / detach button (and a drop): nobody selected: false; a dead soldier: nothing. One soldier:
    // the first slot only. A slot's soldier picked: detached (his roster picture back; the slots after his
    // moved up), the slot's new soldier selected, else the one before's (the first slot: a free roster
    // soldier). A roster soldier: into the first empty slot (his picture hidden; all taken: the fourth's
    // soldier replaced, his picture back, unless he stays), the next free roster soldier selected. True.
    static bool OnAssign(void *menu, void *argument, C_Control *control, uint32_t notification,
                         uint32_t value);               // 0x00694f30
    // The automatic selection: one soldier: a living free one of the first 30 roster soldiers at random (the
    // first slot taken: none). Else each slot from the first empty one on: one of the five best living free
    // ones (the first and third slots by stat 4, the second by stat 7, the fourth by stat 1 less a tenth of
    // stat 4) at random, his roster picture hidden. Then the first free roster soldier selected. True.
    static bool OnAutoSelect(void *menu, void *argument, C_Control *control, uint32_t notification,
                             uint32_t value);           // 0x00695740
    // A candidate kept among the best five (`ids` and `scores` of `count`): appended while fewer, else in
    // place of the worst of those scoring less (none: dropped).
    static void KeepBest(int *ids, int *scores, int *count, int id, int score);   // 0x006956d0 (__fastcall)
    // The selected soldier's stats: the rows' bars and values (shooting %, strength Kg, health (its bar
    // full), endurance, stealth %, first aid %, lockpick %), his biography's text (S_SoldierProfile +0x44),
    // his name in its box (active); his medals. Nobody: all emptied, the box inactive.
    void ShowStats();                                   // 0x00696140 (__fastcall)
    // His medals: all frames off; up to six held: their first frames in the last six places; more: the
    // oldest as their second frames (ribbons) in the first places, the last six's first frames in the last
    // six. (No profile: off only.)
    void ShowMedals(const S_SoldierProfile *profile);   // 0x00696a30
    // The squad screen's tick: "CONTINUE" active only with the first slot taken. True.
    static bool OnScreenTick(void *menu, void *argument, C_Control *control, uint32_t notification,
                             uint32_t value);           // 0x00696d60
    // Hidden: the names the player gave kept (C_HumanStore::SaveCustomNames). True.
    static bool OnScreenHidden(void *menu, void *argument, C_Control *control, uint32_t notification,
                               uint32_t value);         // 0x00696ef0
    // The name's box (Return): its text the selected soldier's own name, his slots' texts, his stats again.
    // True.
    static bool OnNameEdited(void *menu, void *argument, C_Control *control, uint32_t notification,
                             uint32_t value);           // 0x00696c90
    // The info button: the stats' panel hidden and the other shown (or back), the button's and the title's
    // texts; g_squadInfoShown toggled. True.
    static bool OnSoldierInfo(void *menu, void *argument, C_Control *control, uint32_t notification,
                              uint32_t value);          // 0x00694930

    int m_selectedSlot = 0;                             // +0x1B0 the slot picked (0 .. 3)
    int m_selectedSoldier = -1;                         // +0x1B4 the roster soldier picked (-1: none)
    bool m_rosterSelected = true;                       // +0x1B8 the roster's (else a slot's)
    I3D_frame *m_dragged = nullptr;                     // +0x1BC the dragged picture's copy
    I3D_frame *m_rosterSelector = nullptr;              // +0x1C0 the roster's selector
    I3D_frame *m_selector = nullptr;                    // +0x1C4 the selection screen's (over the slot picked)
    S_SquadSlot m_slots[4];                             // +0x1C8
    I3D_frame *m_medalFrames[SQUAD_MEDALS][2] = {};     // +0x218 the medals' frames (DSO_a, DSO_b, ..)
    I3D_frame *m_medalSlots[SQUAD_MEDALS] = {};         // +0x278 their places (medal01 ..)
    uint8_t m_unknown2A8[8] = {};                       // +0x2A8
};
