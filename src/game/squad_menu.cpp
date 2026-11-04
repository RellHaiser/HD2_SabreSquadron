// The campaign's squad screen (game/squad_menu.h, part 9g-ix).
#include "game/squad_menu.h"

#include <utility>
#include <vector>

#include "game/campaign.h"
#include "game/controls.h"
#include "game/pending.h"
#include "game/profile.h"
#include "game/screen.h"
#include "game/soldier.h"
#include "ls3d/i3d.h"

const int g_operationMedalSoldiers[8][10] = {
    {-1},
    {-1},
    {7, 9, 11, 16, 17, -1},
    {2, 5, 8, 12, 19, 22, -1},
    {4, 10, 23, 24, 28, 29, -1},
    {3, 6, 13, 21, 25, -1},
    {14, 15, 18, 20, -1},
    {1, 26, 27, -1},
};

namespace {

// The squad screen's messages (target 0).
enum : uint32_t {
    SQUAD_MSG_CONTINUE = 0x200006A,
    SQUAD_MSG_SELECT_FREE = 0x2000072,
};
// The screens (their ids' top 6 bits): the squad's, the selection's.
constexpr uint32_t SQUAD_SCREEN = 1, SELECTION_SCREEN = 2;
constexpr uint32_t SCREEN_SHIFT = 26, SCREEN_MASK = 0x3F;
// The roster's soldiers' controls on the squad screen: its first child's children (the soldier's
// index + 1 in bits 4-11).
constexpr uint32_t ROSTER_CONTROLS = 0x4101000;
constexpr int ROSTER_LIMIT = 30;
// "ARE YOU SURE?"; a blank.
constexpr uint32_t TEXT_SURE = 0x7D4, TEXT_BLANK = 0x7D2;
constexpr int MESSAGE_QUESTION = 1;
constexpr uint32_t NAME_STYLE = 0xC, RANK_STYLE = 2, BLANK_STYLE = 0xB;
// The campaign screen kinds of the squad screen: the squad picked (its soldiers equipped by the
// equipment screen), the operation's squad.
constexpr int KIND_PICK = 1, KIND_OPERATION = 2;
// The Defence Medal; the first campaign's missions of one soldier.
constexpr int DEFENCE_MEDAL = 6;
constexpr int SPEECH_SETS = 4;

void Post(uint32_t target, uint32_t id, uint32_t value, uintptr_t value2 = 0)
{
    S_ControlMessage message;
    message.target = target;
    message.id = id;
    message.value = value;
    message.value2 = value2;
    PostControlMessage(message);
}

std::vector<S_SquadSoldier *> &Squad() { return g_gameProfile.m_missionSquad->states; }

}   // namespace

const C_str &GetProfileName(const S_SoldierProfile *profile)
{
    return profile->customName.IsEmpty() ? profile->fullName : profile->customName;
}

// ---- the slots -----------------------------------------------------------------

void S_SquadSlot::SetSoldier(int soldier, bool lock)
{
    profile = soldier;
    locked = lock;
    IImage *face = soldier >= 0 ? g_gameProfile.m_humanStore->Get(soldier)->faceImage : nullptr;
    Post(picture, CONTROL_MSG_SET_TEXTURE, 0, reinterpret_cast<uintptr_t>(face));
    Post(picture, CONTROL_MSG_SET_INDEX, static_cast<uint32_t>(soldier));
    ShowTexts();
}

void S_SquadSlot::ShowTexts() const
{
    const S_SoldierProfile *soldier = g_gameProfile.m_humanStore->Get(profile);
    if (!soldier) {
        Post(name, CONTROL_MSG_SET_TEXT_ID, BLANK_STYLE, TEXT_BLANK);
        Post(rank, CONTROL_MSG_SET_TEXT_ID, BLANK_STYLE, TEXT_BLANK);
        return;
    }
    if (profile < 0)
        return;
    {
        S_ControlMessage message;
        message.target = name;
        message.id = CONTROL_MSG_SET_TEXT;
        message.value = NAME_STYLE;
        message.text = GetProfileName(soldier);
        PostControlMessage(message);
    }
    Post(rank, CONTROL_MSG_SET_TEXT_ID, RANK_STYLE, static_cast<uintptr_t>(g_rankNames[soldier->unknown24].textId2));
}

bool S_SquadSlot::HasControl(uint32_t id) const
{
    return picture == id || name == id || rank == id;
}

// ---- the menu state ------------------------------------------------------------

bool C_SquadMenu::IsOneSoldier()
{
    const S_PlayMode *mode = g_gameProfile.m_playMode;
    if (mode->mode == PLAY_MODE_LONE_WOLF)
        return true;
    if (mode->singleMission != 1)
        return false;
    C_Campaign *campaign = g_gameProfile.m_campaign;
    const int current = campaign->GetCurrentIndex();
    const int mission = campaign->GetMissionNumber(current);
    return current == 0 && (mission == 2 || mission == 15);
}

bool C_SquadMenu::OnStart(void *)
{
    C_HumanStore *store = g_gameProfile.m_humanStore;
    if (store->GetCount() == 0 && !store->Load(C_str("tables\\HumanStore.def")))
        return false;
    if (g_gameProfile.m_playMode->singleMission == 0 && m_mode == KIND_OPERATION) {
        C_Campaign *campaign = g_gameProfile.m_campaign;
        const int operation = campaign->GetOperationIndex(campaign->GetCurrentIndex());
        for (int i = 0; i < 10; ++i) {
            const int index = g_operationMedalSoldiers[operation][i];
            if (index < 0)
                break;
            S_SoldierProfile *profile = store->Get(index);
            if (!profile->dead && !profile->HasUnknown28(DEFENCE_MEDAL))
                profile->unknown28.push_back(DEFENCE_MEDAL);
        }
        for (S_SquadSoldier *soldier : Squad())
            soldier->ReturnItems();
    }
    return true;
}

void C_SquadMenu::OnStop()
{
    if (m_popup) {
        delete m_popup;
        m_popup = nullptr;
    }
    // Dropped: the copy protection (a value of the 0x008caf40 table recomputed).
}

void C_SquadMenu::OnMessage(S_ControlMessage *message)
{
    switch (message->id & CONTROL_MSG_MASK) {
    case MENU_MSG_ESCAPE:
        if (g_menuScreens.empty())
            return;
        // On the selection screen: closed; else asked whether to go back to the campaign's previous
        // screen.
        if ((g_menuScreens.back()->GetId() >> SCREEN_SHIFT & SCREEN_MASK) != SELECTION_SCREEN) {
            ShowMessageId(MESSAGE_QUESTION, TEXT_SURE, MENU_MSG_PREVIOUS, 0);
            return;
        }
        Post(0, CONTROL_NOTIFY_CLICK | MENU_MSG_CLOSE_POPUP, 0);
        return;
    case SQUAD_MSG_CONTINUE: {
        const int selected = m_selectedSlot;
        if (selected > 3 || selected < 0)
            return;
        // The selected slot's soldier into the first slot, the first's into it.
        const bool locked = m_slots[0].locked;
        const int profile = m_slots[0].profile;
        m_slots[0].SetSoldier(m_slots[selected].profile, m_slots[selected].locked);
        m_slots[m_selectedSlot].SetSoldier(profile, locked);
        SyncSquad(this, nullptr, nullptr, 0, 0);
        Post(0, MENU_MSG_NEXT, 0);
        return;
    }
    case SQUAD_MSG_SELECT_FREE:
        SelectFreeSoldier(static_cast<int>(message->value));
        return;
    default:
        return;
    }
}

bool C_SquadMenu::IsInSlots(int profile) const
{
    for (const S_SquadSlot &slot : m_slots)
        if (slot.profile == profile)
            return true;
    return false;
}

int C_SquadMenu::FirstFreeSpeechSet()
{
    const std::vector<S_SquadSoldier *> &squad = Squad();
    for (int set = 1; set <= SPEECH_SETS; ++set) {
        bool taken = false;
        for (size_t i = squad.size(); i-- > 0;) {
            if (squad[i] && squad[i]->speechSet == set) {
                taken = true;
                break;
            }
        }
        if (!taken)
            return set;
    }
    return -1;
}

bool C_SquadMenu::SyncSquad(void *menu, void *, C_Control *, uint32_t, uint32_t)
{
    auto *self = static_cast<C_SquadMenu *>(menu);
    std::vector<S_SquadSoldier *> &squad = Squad();
    // Those of no slot freed.
    for (size_t i = squad.size(); i-- > 0;) {
        S_SquadSoldier *soldier = squad[i];
        if (soldier && soldier->profileIndex >= 0 && !self->IsInSlots(soldier->profileIndex))
            soldier->profileIndex = -1;
    }
    // The slots' soldiers given one: a freed one (the last first), else a new one.
    for (const S_SquadSlot &slot : self->m_slots) {
        if (slot.profile < 0)
            break;
        if (g_gameProfile.FindSoldierState(slot.profile))
            continue;
        S_SquadSoldier *soldier = nullptr;
        for (size_t i = squad.size(); i-- > 0;) {
            if (squad[i] && squad[i]->profileIndex < 0) {
                soldier = squad[i];
                break;
            }
        }
        if (!soldier)
            soldier = g_gameProfile.m_missionSquad->AddSoldier();
        soldier->SetProfile(slot.profile, 0);
        soldier->autoEquip = self->m_mode == KIND_PICK;
    }
    // The squad ordered as the slots. (The original's search past the squad's end wraps around the
    // indexes; every slot's soldier is in the squad by now.)
    for (size_t i = 0; i < 4; ++i) {
        const int profile = self->m_slots[i].profile;
        if (profile < 0)
            break;
        if (i >= squad.size() || !squad[i] || squad[i]->profileIndex == profile)
            continue;
        for (size_t j = i + 1; j < squad.size(); ++j) {
            if (squad[j] && squad[j]->profileIndex == profile) {
                std::swap(squad[i], squad[j]);
                break;
            }
        }
    }
    // The freed deleted (the last first).
    for (size_t i = squad.size(); i-- > 0;) {
        if (squad[i] && squad[i]->profileIndex < 0) {
            delete squad[i];
            squad.erase(squad.begin() + static_cast<long>(i));
        }
    }
    // Speech sets for those without.
    for (S_SquadSoldier *soldier : squad)
        if (soldier && soldier->speechSet < 0)
            soldier->speechSet = FirstFreeSpeechSet();
    return true;
}

void C_SquadMenu::SelectFreeSoldier(int index)
{
    if (index < 0)
        return;
    const int count = g_gameProfile.m_humanStore->GetCount();
    int found = -1;
    for (int i = index; i < count && i < ROSTER_LIMIT; ++i) {
        if (!IsInSlots(i)) {
            found = i;
            break;
        }
    }
    if (found < 0) {
        for (int i = index; i-- > 0;) {
            if (!IsInSlots(i)) {
                found = i;
                break;
            }
        }
        if (found < 0)
            return;
    }
    // (Fix: no screen shown, nothing; the original looked the control up from null.)
    if (g_menuScreens.empty())
        return;
    C_Control *top = g_menuScreens.back();
    const uint32_t id = ROSTER_CONTROLS | (static_cast<uint32_t>(found + 1) & 0xFF) << 4;
    if (C_Control *control = top->FindControl(id, true))
        OnSoldierControl(this, this, control, 0, 0);
}

// ---------------------------------------------------------------------------
// Part 9g-x: the screens

namespace {

// The controls: the selection screen's pictures (photo03 .. 06; their ids by the slot), the edit box of
// the selected soldier's name.
constexpr uint32_t SELECTION_FIRST_PICTURE = 0x8200000, SELECTION_PICTURE_STEP = 0x100000;
constexpr int CONTROL_EDIT = 3, CONTROL_PICTURE = 4, CONTROL_TEXT = 0x13, CONTROL_BUTTON = 0x14;
// "Select one soldier", "YES", "CANCEL", "Selected soldier info", the selected soldier's stats' title;
// the styles.
constexpr uint32_t TEXT_SELECT_ONE = 0xAB2, TEXT_YES = 0x7D1, TEXT_CANCEL = 0x7D8, TEXT_SOLDIER_INFO = 0xA69,
                   TEXT_SOLDIER_STATS = 0xA6B;
constexpr uint32_t SELECT_STYLE = 0xC, BUTTON_STYLE = 0xD, STATS_STYLE = 0xB, EDIT_STYLE = 0xC;
// The name's edit box: its margin, at most 17 characters.
constexpr uint32_t EDIT_MARGIN = 1, EDIT_MAX_LENGTH = 0x11;

// A visual's own copy of its mesh (equipment_screen.cpp's).
void OwnMesh(I3D_frame *frame)
{
    auto *visual = static_cast<I3D_object_visual *>(frame);
    visual->DuplicateMesh();
    visual->GetVisualMesh()->Unknown18();
}

uintptr_t Callback(ControlCallback function) { return reinterpret_cast<uintptr_t>(function); }

}   // namespace

bool C_SquadMenu::BuildScreens()
{
    auto *menu = new S_MenuDesc();
    menu->AddScreen("team members selection", "team members selection");
    C_Campaign *campaign = g_gameProfile.m_campaign;
    if (campaign->GetOperationIndex(campaign->GetCurrentIndex()) == 0 && g_gameProfile.m_playMode->mode != PLAY_MODE_LONE_WOLF)
        menu->AddScreen("SELECTION SCREEN", "selection screen");
    RemoveDeadSoldiers();
    for (S_ScreenDesc *desc : menu->screens) {
        I3D_frame *model = LoadModel(desc->name.c_str(), desc->file.c_str());
        if (!model)
            continue;
        if ((desc->id & 0xFC000000) == SQUAD_SCREEN << SCREEN_SHIFT)
            DescribeSquadScreen(desc, model);
        if ((desc->id & 0xFC000000) == SELECTION_SCREEN << SCREEN_SHIFT)
            DescribeSelectionScreen(desc, model);
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
    open.value = SQUAD_SCREEN;
    HandleMessage(&open);
    FillSlots();
    SelectFreeSoldier(0);
    // The medal popup.
    if (I3D_frame *model = LoadModel("DESCRIPTION", "description")) {
        m_popup = new C_MedalDescriptionScreen(m_scene, model);
        m_popup->Build(nullptr);
    }
    return true;
}

void C_SquadMenu::RemoveDeadSoldiers()
{
    if (g_gameProfile.m_playMode->singleMission == 1)
        return;
    std::vector<S_SquadSoldier *> &squad = Squad();
    for (size_t i = squad.size(); i-- > 0;) {
        S_SquadSoldier *soldier = squad[i];
        if (soldier->profileIndex < 0 || !g_gameProfile.m_humanStore->Get(soldier->profileIndex)->dead)
            continue;
        soldier->ReturnItems();
        delete soldier;
        squad.erase(squad.begin() + static_cast<long>(i));
    }
}

void C_SquadMenu::FillSlots()
{
    const std::vector<S_SquadSoldier *> &squad = Squad();
    for (size_t i = 0; i < squad.size(); ++i) {
        const int profile = squad[i]->profileIndex;
        if (profile < 0)
            continue;
        // His roster control hidden, the slot his (locked unless a kind 1 screen's soldier not deployed).
        Post(ROSTER_CONTROLS, CONTROL_MSG_HIDE_ITEM, ROSTER_CONTROLS | (static_cast<uint32_t>(profile + 1) & 0xFF) << 4);
        m_slots[i].SetSoldier(profile, m_mode != KIND_PICK || !squad[i]->deployed);
    }
}

bool C_SquadMenu::DescribeSquadScreen(S_ScreenDesc *desc, I3D_frame *model)
{
    const auto self = reinterpret_cast<uintptr_t>(this);
    DescribeRoster(desc, model);
    DescribeStats(desc, model);
    DescribePanel(desc, model);
    DescribeMedals(desc, model);
    AddScreenMessage(desc, 0, CONTROL_MSG_ADD_CALLBACK, CONTROL_NOTIFY_SCREEN_TICK, self, Callback(&C_SquadMenu::OnScreenTick));
    AddScreenMessage(desc, 0, CONTROL_MSG_ADD_CALLBACK, CONTROL_NOTIFY_HIDDEN, self, Callback(&C_SquadMenu::OnScreenHidden));
    // The selected soldier's name (edited: renamed), inactive.
    S_ControlDesc *name = desc->AddControl(CONTROL_EDIT, "bnamexx");
    AddDescMessage(name, 0, CONTROL_MSG_SET_TEXT_ID, EDIT_STYLE, TEXT_BLANK);
    AddDescMessage(name, 0, CONTROL_MSG_SET_MARGIN, EDIT_MARGIN);
    AddDescMessage(name, 0, CONTROL_MSG_SET_MAX_LENGTH, EDIT_MAX_LENGTH);
    AddDescMessage(name, 0, CONTROL_MSG_ADD_CALLBACK, CONTROL_NOTIFY_RETURN, self, Callback(&C_SquadMenu::OnNameEdited));
    AddDescMessage(name, 0, CONTROL_MSG_DEACTIVATE, 0);
    S_ControlDesc *info = desc->AddControl(CONTROL_BUTTON, "bselected soldier info");
    AddDescMessage(info, 0, CONTROL_MSG_SET_TEXT_ID, BUTTON_STYLE, TEXT_SOLDIER_INFO);
    AddDescMessage(info, 0, CONTROL_MSG_ADD_CALLBACK, CONTROL_NOTIFY_CLICK, self, Callback(&C_SquadMenu::OnSoldierInfo));
    AddDescMessage(desc->AddControl(CONTROL_TEXT, "text_selected soldier stats"), 0, CONTROL_MSG_SET_TEXT_ID, STATS_STYLE,
                   TEXT_SOLDIER_STATS);
    DescribeSelected(desc, model);
    DescribeButtons(desc, model);
    return true;
}

bool C_SquadMenu::DescribeSelectionScreen(S_ScreenDesc *desc, I3D_frame *model)
{
    const auto self = reinterpret_cast<uintptr_t>(this);
    AddScreenMessage(desc, 0, CONTROL_MSG_ADD_CALLBACK, CONTROL_NOTIFY_SHOWN, self, Callback(&C_SquadMenu::OnSelectionShown));
    AddDescMessage(desc->AddControl(CONTROL_TEXT, "text_select one soldier"), 0, CONTROL_MSG_SET_TEXT_ID, SELECT_STYLE,
                   TEXT_SELECT_ONE);
    // The slots' pictures (their own meshes, the slot's index; picked: selected).
    const char *const pictures[] = {"photo03", "photo04", "photo05", "photo06"};
    for (int i = 0; i < 4; ++i) {
        I3D_frame *frame = model->FindChildFrame(pictures[i], FRAME_MASK_VISUAL);
        if (!frame)
            continue;
        frame->SetOn(true);
        OwnMesh(frame);
        S_ControlDesc *picture = desc->AddControl(CONTROL_PICTURE, pictures[i]);
        AddDescMessage(picture, 0, CONTROL_MSG_SET_INDEX, static_cast<uint32_t>(i));
        AddDescMessage(picture, 0, CONTROL_MSG_ADD_CALLBACK, CONTROL_NOTIFY_CLICK, self, Callback(&C_SquadMenu::OnSelectionPicked));
    }
    // "YES": continued with the slot picked first, the screen closed; "CANCEL": closed.
    S_ControlDesc *yes = desc->AddControl(CONTROL_BUTTON, "button01");
    AddDescMessage(yes, 0, CONTROL_MSG_SET_TEXT_ID, BUTTON_STYLE, TEXT_YES);
    AddDescMessage(yes, 0, CONTROL_NOTIFY_CLICK | SQUAD_MSG_CONTINUE, 0);
    AddDescMessage(yes, 0, CONTROL_NOTIFY_CLICK | MENU_MSG_CLOSE_POPUP, 0);
    S_ControlDesc *cancel = desc->AddControl(CONTROL_BUTTON, "button00");
    AddDescMessage(cancel, 0, CONTROL_MSG_SET_TEXT_ID, BUTTON_STYLE, TEXT_CANCEL);
    AddDescMessage(cancel, 0, CONTROL_NOTIFY_CLICK | MENU_MSG_CLOSE_POPUP, 0);
    // (Not checked.)
    m_selector = model->FindChildFrame("selector", FRAME_MASK_VISUAL);
    return true;
}

bool C_SquadMenu::OnSelectionPicked(void *menu, void *, C_Control *control, uint32_t, uint32_t)
{
    auto *self = static_cast<C_SquadMenu *>(menu);
    const S_vector &at = control->GetFrame()->GetWorldPos();
    self->m_selector->SetPos(S_vector(at.x, at.y, self->m_selector->GetWorldPos().z));
    S_ControlMessage index;
    index.id = CONTROL_MSG_GET_INDEX;
    control->HandleMessage(&index);
    self->m_selectedSlot = static_cast<int>(index.value);
    return true;
}

bool C_SquadMenu::OnSelectionShown(void *menu, void *, C_Control *, uint32_t, uint32_t)
{
    auto *self = static_cast<C_SquadMenu *>(menu);
    // The slots' faces on their pictures; an empty slot's hidden.
    for (int i = 0; i < 4; ++i) {
        const uint32_t picture = SELECTION_FIRST_PICTURE + static_cast<uint32_t>(i) * SELECTION_PICTURE_STEP;
        const int profile = self->m_slots[i].profile;
        if (profile < 0)
            Post(picture, CONTROL_MSG_HIDE, 0);
        else
            Post(picture, CONTROL_MSG_SET_TEXTURE, 0,
                 reinterpret_cast<uintptr_t>(g_gameProfile.m_humanStore->Get(profile)->faceImage));
    }
    return true;
}
