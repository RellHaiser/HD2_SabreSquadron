// The campaign's squad screen (game/squad_menu.h, part 9g-ix).
#include "game/squad_menu.h"

#include <cstdlib>
#include <cstring>
#include <malloc.h>
#include <utility>
#include <vector>

#include "game/campaign.h"
#include "game/controls.h"
#include "game/cursor.h"
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

void S_SquadSlot::SetSoldier(int soldier, bool canRemove)
{
    profile = soldier;
    removable = canRemove;
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
        const bool removable = m_slots[0].removable;
        const int profile = m_slots[0].profile;
        m_slots[0].SetSoldier(m_slots[selected].profile, m_slots[selected].removable);
        m_slots[m_selectedSlot].SetSoldier(profile, removable);
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
        // His roster control hidden, the slot his (removable but a kind 1 screen's deployed soldier).
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

// ---------------------------------------------------------------------------
// Part 9g-xi: the roster and the medals

namespace {

constexpr int CONTROL_GROUP = 5, CONTROL_SCROLL_BAR = 8, CONTROL_HIDING_LIST = 0x23;
// The roster's list: its scroll bar's id, 5 shown, across by 0.0876; the scroll bar's list.
constexpr uint32_t ROSTER_SCROLL = 0x4102000;
constexpr uint32_t ROSTER_ROWS = 5;
constexpr float ROSTER_ACROSS_STEP = 0.0876f;
// The one soldier missions' tables and members dimmed (half opaque).
constexpr float DIMMED_ALPHA = 0.5f;

// A float as a message's value2.
uintptr_t FloatValue(float value)
{
    uint32_t bits;
    std::memcpy(&bits, &value, sizeof bits);
    return bits;
}
// The medals' frames (their ids' order): the place's and the held ones'.
const char *const MEDAL_FRAMES[SQUAD_MEDALS][2] = {
    {"DSO_a", "DSO_b"},
    {"DCM_a", "DCM_b"},
    {"MILIT_MEDAL_a", "MILIT_MEDAL_b"},
    {"GSM_AFRICA_a", "GSM_AFRICA_b"},
    {"GSM_a", "GSM_b"},
    {"WAR_MEDAL_a", "WAR_MEDAL_b"},
    {"DEFENCE_MEDAL_a", "DEFENCE_MEDAL_b"},
    {"MILITARY_CROSS_a", "MILITARY_CROSS_b"},
    {"VICTORIA_CROSS_a", "VICTORIA_CROSS_b"},
    {"GERM_FRANCE_STAR_a", "GERM_FRANCE_STAR_b"},
    {"AFRICA_STAR_a", "AFRICA_STAR_b"},
    {"BURMA_STAR_a", "BURMA_STAR_b"},
};

}   // namespace

bool C_SquadMenu::DescribeRoster(S_ScreenDesc *desc, I3D_frame *model)
{
    for (const char *name : {"photo03", "photo04", "photo05", "photo06", "photo07"}) {
        if (I3D_frame *frame = model->FindChildFrame(name, FRAME_MASK_VISUAL))
            frame->SetOn(false);
    }
    // One soldier: the other places dimmed.
    if (IsOneSoldier()) {
        for (const char *name : {"table19", "table20", "table21", "member01", "member02", "member03"}) {
            if (I3D_frame *frame = model->FindChildFrame(name, FRAME_MASK_VISUAL))
                static_cast<I3D_object_visual *>(frame)->SetAlpha(DIMMED_ALPHA);
        }
    }
    I3D_frame *panel = model->FindChildFrame("panel01", FRAME_MASK_DUMMY);
    // The pictures: the dead's (with the "death" frame on it) and the living's.
    I3D_frame *deadPhoto = model->FindChildFrame("photo03", FRAME_MASK_VISUAL);
    I3D_frame *photo = model->FindChildFrame("photo04", FRAME_MASK_VISUAL);
    I3D_frame *scroll = model->FindChildFrame("scroll00", FRAME_MASK_VISUAL);
    I3D_frame *death = model->FindChildFrame("death", FRAME_MASK_VISUAL);
    I3D_frame *list = I3DGetDriver()->CreateFrame(FRAME_DUMMY);
    m_rosterSelector = model->FindChildFrame("selector", FRAME_MASK_VISUAL);
    // (The frame made is leaked without the others; the panel, the living's picture and the death frame are
    // not checked.)
    if (!m_rosterSelector || !deadPhoto || !list || !scroll)
        return false;
    m_rosterSelector->SetOn(false);
    m_rosterSelector->SetPosInline(S_vector(0.0f, 0.0f, 0.0f));
    list->LinkTo(panel, 0);
    list->SetPos(S_vector(0.0f, 0.0f, 0.0f));
    list->SetName("GB_Soldiers");
    m_models.push_back(list);
    death->LinkTo(deadPhoto, 1);
    deadPhoto->LinkTo(list, 1);
    deadPhoto->SetOn(true);
    list->SetOn(true);
    deadPhoto->Update();
    list->Update();

    S_ControlDesc *group = desc->AddControl(CONTROL_GROUP, "Panel01");
    S_ControlDesc *soldiers = group->AddChild(CONTROL_HIDING_LIST, "GB_Soldiers");
    AddDescMessage(soldiers, 0, CONTROL_MSG_SET_VALUE, ROSTER_SCROLL);
    AddDescMessage(soldiers, 0, CONTROL_MSG_SET_VISIBLE, ROSTER_ROWS);
    AddDescMessage(soldiers, 0, CONTROL_MSG_SET_ACROSS_STEP, 0, FloatValue(ROSTER_ACROSS_STEP));
    // (The list frees it.)
    auto *origin = static_cast<S_vector4 *>(_aligned_malloc(sizeof(S_vector4), 16));
    if (origin)
        std::memcpy(origin, &deadPhoto->GetPos(), sizeof(S_vector4));
    AddDescMessage(soldiers, 0, CONTROL_MSG_SET_ORIGIN, 0, reinterpret_cast<uintptr_t>(origin));
    // A roster soldier's picture (the first 30): the template's copy under the list's frame, his index, face,
    // click; the living's dragged and dropped too.
    const auto self = reinterpret_cast<uintptr_t>(this);
    C_HumanStore *store = g_gameProfile.m_humanStore;
    for (int i = 0; i < store->GetCount() && i < ROSTER_LIMIT; ++i) {
        const C_str name = C_fstr("Soldier%02d", i);
        CloneVisual(name, list, store->Get(i)->dead ? deadPhoto : photo);
        S_ControlDesc *picture = soldiers->AddChild(CONTROL_PICTURE, name.c_str());
        AddDescMessage(picture, 0, CONTROL_MSG_SET_INDEX, static_cast<uint32_t>(i));
        AddDescMessage(picture, 0, CONTROL_MSG_SET_TEXTURE, 0, reinterpret_cast<uintptr_t>(store->Get(i)->faceImage));
        AddDescMessage(picture, 0, CONTROL_MSG_ADD_CALLBACK, CONTROL_NOTIFY_CLICK, self, Callback(&C_SquadMenu::OnSoldierControl));
        if (store->Get(i)->dead)
            continue;
        AddDescMessage(picture, 0, CONTROL_MSG_ADD_CALLBACK, CONTROL_NOTIFY_0A, self, Callback(&C_SquadMenu::OnDragStart));
        AddDescMessage(picture, 0, CONTROL_MSG_ADD_CALLBACK, CONTROL_NOTIFY_08, self, Callback(&C_SquadMenu::OnDragMove));
        AddDescMessage(picture, 0, CONTROL_MSG_ADD_CALLBACK, CONTROL_NOTIFY_09, self, Callback(&C_SquadMenu::OnDrop));
        AddDescMessage(picture, 0, CONTROL_MSG_ADD_CALLBACK, CONTROL_NOTIFY_13, self, Callback(&C_SquadMenu::OnSoldierNotify13));
    }
    AddDescMessage(soldiers, 0, CONTROL_MSG_REFRESH, 0);
    AddDescMessage(group->AddChild(CONTROL_SCROLL_BAR, "scroll00"), 0, CONTROL_MSG_SET_VALUE, ROSTER_CONTROLS);
    deadPhoto->SetOn(false);
    deadPhoto->Update();
    return true;
}

void C_SquadMenu::DescribeMedals(S_ScreenDesc *desc, I3D_frame *model)
{
    I3D_frame *panel = I3DGetDriver()->CreateFrame(FRAME_DUMMY);
    if (!panel)
        return;
    panel->LinkTo(model, 0);
    panel->SetPos(S_vector(0.0f, 0.0f, 0.0f));
    panel->SetName("medals_panel");
    m_models.push_back(panel);
    S_ControlDesc *group = desc->AddControl(CONTROL_GROUP, "medals_panel");
    const auto self = reinterpret_cast<uintptr_t>(this);
    // Each medal's two frames under the panel, their pictures (the medal's index; hovered: the popup); its
    // place (medal01 ..).
    for (int i = 0; i < SQUAD_MEDALS; ++i) {
        for (int k = 0; k < 2; ++k) {
            // (Quirk kept: the frames are not checked.)
            I3D_frame *frame = model->FindChildFrame(MEDAL_FRAMES[i][k], FRAME_MASK_VISUAL);
            m_medalFrames[i][k] = frame;
            frame->LinkTo(panel, 1);
            S_ControlDesc *picture = group->AddChild(CONTROL_PICTURE, MEDAL_FRAMES[i][k]);
            AddDescMessage(picture, 0, CONTROL_MSG_SET_INDEX, static_cast<uint32_t>(i));
            AddDescMessage(picture, 0, CONTROL_MSG_ADD_CALLBACK, CONTROL_NOTIFY_ENTER, self, Callback(&C_SquadMenu::OnMedalHover));
            AddDescMessage(picture, 0, CONTROL_MSG_ADD_CALLBACK, CONTROL_NOTIFY_LEAVE, self, Callback(&C_SquadMenu::OnMedalHover));
        }
        m_medalSlots[i] = model->FindChildFrame(C_fstr("medal%02d", i + 1), FRAME_MASK_DUMMY);
    }
}

bool C_SquadMenu::OnMedalHover(void *menu, void *, C_Control *control, uint32_t notification, uint32_t)
{
    auto *self = static_cast<C_SquadMenu *>(menu);
    if (!self->m_popup || !control)
        return true;
    if (notification == CONTROL_NOTIFY_ENTER) {
        S_ControlMessage show;
        show.id = CONTROL_MSG_SHOW;
        self->m_popup->HandleMessage(&show);
        // The medal's first frame copied for the popup.
        const int medal = control->GetIndex();
        S_ControlMessage medalShown;
        medalShown.id = MEDAL_POPUP_MSG_SHOW;
        medalShown.value = static_cast<uint32_t>(medal);
        medalShown.value2 = reinterpret_cast<uintptr_t>(self->CloneFrame(C_fstr("Medal"), nullptr, self->m_medalFrames[medal][0]));
        self->m_popup->HandleMessage(&medalShown);
    } else if (notification == CONTROL_NOTIFY_LEAVE) {
        S_ControlMessage hide;
        hide.id = CONTROL_MSG_HIDE;
        self->m_popup->HandleMessage(&hide);
    }
    return true;
}

// ---------------------------------------------------------------------------
// Part 9g-xii: the squad screen's stats, panel, places and buttons

bool g_squadInfoShown = false;

namespace {

constexpr int CONTROL_SKILL_BAR = 0x16, CONTROL_INFO_TEXT = 0x1A;
// The squad screen's top controls (their ids): the stats' panel, the other panel, the info button, the stats'
// title, the selected soldiers' places, the assign button.
constexpr uint32_t STATS_PANEL = 0x4200000, INFO_PANEL = 0x4300000, INFO_BUTTON = 0x4600000, STATS_TITLE = 0x4700000,
                   SELECTED_PANEL = 0x4800000, ASSIGN_BUTTON = 0x4900000;
// The controls' second index field (bits 20-25): the roster's group, the selected soldiers'.
constexpr uint32_t CONTROL_INDEX_MASK = 0x3F00000, ROSTER_GROUP_INDEX = 0x100000, SELECTED_GROUP_INDEX = 0x800000;
// The skills' names (0xA6E + the skill), the bars' axis; "SOLDIER STATISTICS" / "Soldier info:" (the info
// button's and the title's, the other panel shown); "ADD" / "REMOVE"; "AUTOMATIC SELECTION", "CONTINUE",
// "BACK", the screen's titles ("TEAM MEMBER SELECTION", "REPLENISH TEAM"), the draft's ("Soldier selection:").
constexpr uint32_t TEXT_SKILLS = 0xA6E, BAR_AXIS_Z = 2;
constexpr uint32_t TEXT_STATS_INFO = 0xA6A, TEXT_INFO_TITLE = 0xA6C;
constexpr uint32_t TEXT_ASSIGN = 0xA61, TEXT_DETACH = 0xA62, TEXT_AUTO_SELECT = 0xA5E, TEXT_CONTINUE = 0xA5D, TEXT_BACK = 0xA5F,
                   TEXT_TEAM_SELECTION = 0xA5B, TEXT_REPLENISH = 0xA5C, TEXT_VERSION = 0x7D6, TEXT_DRAFT = 0xA63;
constexpr uint32_t SKILL_STYLE = 0xB, ASSIGN_STYLE = 0xC, TITLE_STYLE = 0xA, VERSION_STYLE = 0x28;
// The selector over a picked picture, before it.
constexpr float SELECTOR_DEPTH = -0.1f;

}   // namespace

bool C_SquadMenu::DescribeStats(S_ScreenDesc *desc, I3D_frame *)
{
    // The skills' rows: their names' and values' texts, their bars.
    static const char *const SKILLS[][3] = {
        {"text_shooting", "hlcpct00", "text_shooting01"},     {"text_strenght", "hlcpct01", "text_strenght01"},
        {"text_health", "hlcpct02", "text_health01"},         {"text_endurance", "hlcpct03", "text_endurance01"},
        {"text_stealth", "hlcpct04", "text_stealth01"},       {"text_first aid", "hlcpct05", "text_first aid01"},
        {"text_lockpick", "hlcpct06", "text_lockpick01"}};
    S_ControlDesc *panel = desc->AddControl(CONTROL_GROUP, "Panel02");
    AddDescMessage(panel, 0, CONTROL_MSG_IGNORE_INPUT, 0);
    for (int i = 0; i < 7; ++i) {
        S_ControlDesc *name = panel->AddChild(CONTROL_TEXT, SKILLS[i][0]);
        AddDescMessage(name, 0, CONTROL_MSG_SET_MARGIN, 0);
        AddDescMessage(name, 0, CONTROL_MSG_SET_TEXT_ID, SKILL_STYLE, TEXT_SKILLS + static_cast<uint32_t>(i));
        S_ControlDesc *bar = panel->AddChild(CONTROL_SKILL_BAR, SKILLS[i][1]);
        AddDescMessage(bar, 0, CONTROL_MSG_SET_BAR_AXIS, BAR_AXIS_Z);
        AddDescMessage(bar, 0, CONTROL_MSG_SET_BAR_VALUE, 0, 0);
        S_ControlDesc *value = panel->AddChild(CONTROL_TEXT, SKILLS[i][2]);
        AddDescMessage(value, 0, CONTROL_MSG_SET_MARGIN, 0);
        AddDescMessage(value, 0, CONTROL_MSG_SET_TEXT_ID, SKILL_STYLE, TEXT_BLANK);
    }
    return true;
}

bool C_SquadMenu::DescribePanel(S_ScreenDesc *desc, I3D_frame *)
{
    S_ControlDesc *panel = desc->AddControl(CONTROL_GROUP, "Panel03");
    AddDescMessage(panel, 0, CONTROL_MSG_IGNORE_INPUT, 0);
    AddDescMessage(panel, 0, CONTROL_MSG_HIDE, 0);
    S_ControlDesc *text = panel->AddChild(CONTROL_INFO_TEXT, "text_05");
    AddDescMessage(text, 0, CONTROL_MSG_SET_MARGIN, 1);
    AddDescMessage(text, 0, CONTROL_MSG_IGNORE_INPUT, 0);
    return true;
}

bool C_SquadMenu::DescribeSelected(S_ScreenDesc *desc, I3D_frame *model)
{
    // The slots' name, rank and picture.
    static const char *const PLACES[4][3] = {{"text_name00", "text_team member00", "member00"},
                                            {"text_name01", "text_team member01", "photo00"},
                                            {"text_name02", "text_team member02", "photo01"},
                                            {"text_name03", "text_team member03", "photo02"}};
    I3D_frame *panel = I3DGetDriver()->CreateFrame(FRAME_DUMMY);
    if (!panel)
        return false;
    panel->LinkTo(model, 0);
    panel->SetPos(S_vector(0.0f, 0.0f, 0.0f));
    panel->SetName("P_SelectedSol");
    m_models.push_back(panel);
    // Their frames under the panel's (the picture its own mesh).
    for (const auto &place : PLACES) {
        for (int k = 0; k < 3; ++k) {
            if (k == 2) {
                if (I3D_frame *picture = model->FindChildFrame(place[2], FRAME_MASK_VISUAL))
                    OwnMesh(picture);
            }
            if (I3D_frame *frame = model->FindChildFrame(place[k], FRAME_MASK_VISUAL))
                frame->LinkTo(panel, 1);
        }
    }
    S_ControlDesc *group = desc->AddControl(CONTROL_GROUP, "P_SelectedSol");
    const auto self = reinterpret_cast<uintptr_t>(this);
    for (int i = 0; i < 4; ++i) {
        S_SquadSlot &slot = m_slots[i];
        slot.profile = -1;
        S_ControlDesc *name = group->AddChild(CONTROL_TEXT, PLACES[i][0]);
        AddDescMessage(name, 0, CONTROL_MSG_SET_TEXT_ID, SKILL_STYLE, TEXT_BLANK);
        slot.name = name->id;
        S_ControlDesc *rank = group->AddChild(CONTROL_TEXT, PLACES[i][1]);
        AddDescMessage(rank, 0, CONTROL_MSG_SET_TEXT_ID, SKILL_STYLE, TEXT_BLANK);
        slot.rank = rank->id;
        S_ControlDesc *picture = group->AddChild(CONTROL_PICTURE, PLACES[i][2]);
        slot.picture = picture->id;
        AddDescMessage(picture, 0, CONTROL_MSG_SET_INDEX, 0xFFFFFFFF);
        AddDescMessage(picture, 0, CONTROL_MSG_SET_TEXTURE, 0, 0);
        AddDescMessage(picture, 0, CONTROL_MSG_ADD_CALLBACK, CONTROL_NOTIFY_CLICK, self, Callback(&C_SquadMenu::OnSoldierControl));
        AddDescMessage(picture, 0, CONTROL_MSG_ADD_CALLBACK, CONTROL_NOTIFY_0A, self, Callback(&C_SquadMenu::OnDragStart));
        AddDescMessage(picture, 0, CONTROL_MSG_ADD_CALLBACK, CONTROL_NOTIFY_08, self, Callback(&C_SquadMenu::OnDragMove));
        AddDescMessage(picture, 0, CONTROL_MSG_ADD_CALLBACK, CONTROL_NOTIFY_09, self, Callback(&C_SquadMenu::OnDrop));
        AddDescMessage(picture, 0, CONTROL_MSG_ADD_CALLBACK, CONTROL_NOTIFY_13, self, Callback(&C_SquadMenu::OnSoldierNotify13));
    }
    return true;
}

bool C_SquadMenu::DescribeButtons(S_ScreenDesc *desc, I3D_frame *)
{
    const auto self = reinterpret_cast<uintptr_t>(this);
    S_ControlDesc *assign = desc->AddControl(CONTROL_BUTTON, "bassign - detach");
    AddDescMessage(assign, 0, CONTROL_MSG_SET_TEXT_ID, BUTTON_STYLE, TEXT_ASSIGN);
    AddDescMessage(assign, 0, CONTROL_MSG_ADD_CALLBACK, CONTROL_NOTIFY_CLICK, self, Callback(&C_SquadMenu::OnAssign));
    // (The model's frames' names swapped with their buttons': "baccept and continue" selects automatically.)
    S_ControlDesc *autoSelect = desc->AddControl(CONTROL_BUTTON, "baccept and continue");
    AddDescMessage(autoSelect, 0, CONTROL_MSG_SET_TEXT_ID, BUTTON_STYLE, TEXT_AUTO_SELECT);
    AddDescMessage(autoSelect, 0, CONTROL_MSG_ADD_CALLBACK, CONTROL_NOTIFY_CLICK, self, Callback(&C_SquadMenu::OnAutoSelect));
    // Continue: the first operation (but the lone wolf's): the selection screen; else the squad kept, the
    // next step.
    S_ControlDesc *autoAssign = desc->AddControl(CONTROL_BUTTON, "bauto assign");
    AddDescMessage(autoAssign, 0, CONTROL_MSG_SET_TEXT_ID, BUTTON_STYLE, TEXT_CONTINUE);
    C_Campaign *campaign = g_gameProfile.m_campaign;
    if (campaign->GetCurrentIndex() == 0 && campaign->GetOperationIndex(0) == 0 &&
        g_gameProfile.m_playMode->mode != PLAY_MODE_LONE_WOLF) {
        AddDescMessage(autoAssign, 0, CONTROL_NOTIFY_CLICK | MENU_MSG_OPEN_POPUP, SELECTION_SCREEN);
    } else {
        AddDescMessage(autoAssign, 0, CONTROL_MSG_ADD_CALLBACK, CONTROL_NOTIFY_CLICK, self, Callback(&C_SquadMenu::SyncSquad));
        AddDescMessage(autoAssign, 0, CONTROL_NOTIFY_CLICK | MENU_MSG_NEXT, 0);
    }
    S_ControlDesc *back = desc->AddControl(CONTROL_BUTTON, "bback");
    AddDescMessage(back, 0, CONTROL_MSG_SET_TEXT_ID, BUTTON_STYLE, TEXT_BACK);
    AddDescMessage(back, 0, CONTROL_NOTIFY_CLICK | MENU_MSG_ESCAPE, 0);
    AddDescMessage(desc->AddControl(CONTROL_TEXT, "text_screen"), 0, CONTROL_MSG_SET_TEXT_ID, TITLE_STYLE,
                   m_mode == KIND_OPERATION ? TEXT_TEAM_SELECTION : TEXT_REPLENISH);
    AddDescMessage(desc->AddControl(CONTROL_TEXT, "text_h&dversion"), 0, CONTROL_MSG_SET_TEXT_ID, VERSION_STYLE, TEXT_VERSION);
    AddDescMessage(desc->AddControl(CONTROL_TEXT, "text_military service draft"), 0, CONTROL_MSG_SET_TEXT_ID, SKILL_STYLE,
                   TEXT_DRAFT);
    return true;
}

bool C_SquadMenu::OnSoldierInfo(void *, void *, C_Control *, uint32_t, uint32_t)
{
    // The stats or the other panel, the button's and the title's texts.
    if (!g_squadInfoShown) {
        Post(STATS_PANEL, CONTROL_MSG_HIDE, 0);
        Post(INFO_PANEL, CONTROL_MSG_SHOW, 0);
        Post(INFO_BUTTON, CONTROL_MSG_SET_TEXT_ID, ASSIGN_STYLE, TEXT_STATS_INFO);
        Post(STATS_TITLE, CONTROL_MSG_SET_TEXT_ID, SKILL_STYLE, TEXT_INFO_TITLE);
    } else {
        Post(STATS_PANEL, CONTROL_MSG_SHOW, 0);
        Post(INFO_PANEL, CONTROL_MSG_HIDE, 0);
        Post(INFO_BUTTON, CONTROL_MSG_SET_TEXT_ID, ASSIGN_STYLE, TEXT_SOLDIER_INFO);
        Post(STATS_TITLE, CONTROL_MSG_SET_TEXT_ID, SKILL_STYLE, TEXT_SOLDIER_STATS);
    }
    g_squadInfoShown = !g_squadInfoShown;
    return true;
}

bool C_SquadMenu::OnSoldierControl(void *menu, void *argument, C_Control *control, uint32_t, uint32_t)
{
    auto *self = static_cast<C_SquadMenu *>(menu);
    self->m_selectedSoldier = control ? control->GetIndex() : -1;
    if (self->m_selectedSoldier == -1)
        return false;
    self->ShowStats();
    if (self->m_selectedSoldier == -1)
        return false;
    // The roster's picture: "Assign"; a slot's: "Detach"; another: none selected.
    switch (control->GetId() & CONTROL_INDEX_MASK) {
    case ROSTER_GROUP_INDEX:
        self->m_rosterSelected = true;
        Post(ASSIGN_BUTTON, CONTROL_MSG_SET_TEXT_ID, ASSIGN_STYLE, TEXT_ASSIGN);
        break;
    case SELECTED_GROUP_INDEX:
        self->m_rosterSelected = false;
        Post(ASSIGN_BUTTON, CONTROL_MSG_SET_TEXT_ID, ASSIGN_STYLE, TEXT_DETACH);
        break;
    default:
        self->m_selectedSoldier = -1;
        return false;
    }
    PlaceRosterSelector(self, argument, control, 0, 0);
    return true;
}

bool C_SquadMenu::PlaceRosterSelector(void *menu, void *, C_Control *control, uint32_t, uint32_t)
{
    I3D_frame *selector = static_cast<C_SquadMenu *>(menu)->m_rosterSelector;
    if (!selector)
        return true;
    selector->LinkTo(control->GetFrame(), 0);
    selector->SetPosInline(S_vector(0.0f, 0.0f, SELECTOR_DEPTH));
    selector->SetOn(true);
    selector->Update();
    return true;
}

// ---------------------------------------------------------------------------
// Part 9g-xiii: a soldier assigned or detached

namespace {

// A roster soldier's picture in the roster's list, shown again (the hiding list's message).
constexpr uint32_t CONTROL_MSG_SHOW_ITEM = CONTROL_MSG_ITEM_COUNTED;

uint32_t RosterPicture(int profile) { return ROSTER_CONTROLS | (static_cast<uint32_t>(profile + 1) & 0xFF) << 4; }

}   // namespace

bool C_SquadMenu::OnAssign(void *menu, void *argument, C_Control *control, uint32_t, uint32_t)
{
    auto *self = static_cast<C_SquadMenu *>(menu);
    const int selected = self->m_selectedSoldier;
    if (selected == -1)
        return false;
    if (g_gameProfile.m_humanStore->Get(selected)->dead)
        return true;
    S_SquadSlot *slots = self->m_slots;
    if (IsOneSoldier()) {
        // One soldier: the first slot only.
        if (!self->m_rosterSelected) {
            if (selected != slots[0].profile || !slots[0].removable)
                return true;
            slots[0].SetSoldier(-1, true);
            Post(ROSTER_CONTROLS, CONTROL_MSG_SHOW_ITEM, RosterPicture(selected));
            Post(0, SQUAD_MSG_SELECT_FREE, 0);
        } else if (slots[0].profile < 0) {
            slots[0].SetSoldier(selected, true);
            Post(ROSTER_CONTROLS, CONTROL_MSG_HIDE_ITEM, RosterPicture(selected));
            Post(0, SQUAD_MSG_SELECT_FREE, static_cast<uint32_t>(selected));
        } else {
            // (Quirk kept: the fourth slot's soldier's picture shown, not the first's.)
            Post(ROSTER_CONTROLS, CONTROL_MSG_SHOW_ITEM, RosterPicture(slots[3].profile));
        }
        return true;
    }
    if (!self->m_rosterSelected) {
        // A slot's soldier detached: his roster picture back, the slots after it moved up; the slot's new
        // soldier selected, else the one before's (the first: a free roster soldier).
        for (int i = 0; i < 4; ++i) {
            if (selected != slots[i].profile || !slots[i].removable)
                continue;
            Post(ROSTER_CONTROLS, CONTROL_MSG_SHOW_ITEM, RosterPicture(selected));
            for (int j = i; j < 3; ++j)
                slots[j].SetSoldier(slots[j + 1].profile, slots[j + 1].removable);
            slots[3].SetSoldier(-1, true);
            if (slots[i].profile >= 0) {
                PlaceRosterSelector(self, argument, control->FindControl(slots[i].picture, true), 0, 0);
                self->m_selectedSoldier = slots[i].profile;
                self->ShowStats();
            } else if (i < 1) {
                Post(0, SQUAD_MSG_SELECT_FREE, 0);
            } else {
                OnSoldierControl(self, self, control->FindControl(slots[i - 1].picture, true), 0, 0);
            }
            break;
        }
        return true;
    }
    // A roster soldier assigned: his roster picture hidden, into the first empty slot (all taken: the fourth,
    // unless its soldier stays; that one's picture back); the next free roster soldier selected.
    Post(ROSTER_CONTROLS, CONTROL_MSG_HIDE_ITEM, RosterPicture(selected));
    int next = selected;
    int target = 0;
    while (target < 3 && slots[target].profile >= 0)
        ++target;
    if (target == 3 && slots[3].profile >= 0) {
        if (!slots[3].removable) {
            Post(ROSTER_CONTROLS, CONTROL_MSG_SHOW_ITEM, RosterPicture(selected));
            return true;
        }
        Post(ROSTER_CONTROLS, CONTROL_MSG_SHOW_ITEM, RosterPicture(slots[3].profile));
        next = slots[3].profile;
    }
    slots[target].SetSoldier(selected, true);
    Post(0, SQUAD_MSG_SELECT_FREE, static_cast<uint32_t>(next));
    return true;
}

// ---------------------------------------------------------------------------
// Part 9g-xiv: a soldier's picture dragged and dropped

namespace {

// The screen's halves: the roster's (the left), the slots' (the right).
constexpr float SCREEN_MIDDLE = 0.5f;
// The dragged copy above its picture.
constexpr float DRAG_HEIGHT = 0.1f;

// The slot of a picture (its id), from the last; -1: none.
int SlotOfPicture(const S_SquadSlot *slots, uint32_t id)
{
    for (int i = 4; i-- > 0;)
        if (slots[i].picture == id)
            return i;
    return -1;
}

// The slot one of whose controls is `id`, from the last; -1: none.
int SlotOfControl(const S_SquadSlot *slots, uint32_t id)
{
    for (int i = 4; i-- > 0;)
        if (slots[i].HasControl(id))
            return i;
    return -1;
}

float CursorX(C_Cursor *cursor)
{
    S_vector pos;
    cursor->GetPosition(&pos);
    return pos.x;
}

}   // namespace

bool C_SquadMenu::OnDragStart(void *menu, void *argument, C_Control *control, uint32_t notification, uint32_t value)
{
    auto *self = static_cast<C_SquadMenu *>(menu);
    if (!control)
        return false;
    // Another soldier's picture: picked first.
    if (self->m_selectedSoldier == -1 || control->GetIndex() != self->m_selectedSoldier) {
        if (!OnSoldierControl(menu, argument, control, notification, value)) {
            self->m_selectedSoldier = -1;
            return false;
        }
    }
    // A slot's soldier who stays: not dragged.
    const int slot = SlotOfPicture(self->m_slots, control->GetId());
    if (slot >= 0 && !self->m_slots[slot].removable) {
        self->m_selectedSoldier = -1;
        return false;
    }
    // Dragged from the slots' half: to be detached; from the roster's: assigned.
    if (self->m_rosterSelected) {
        if (CursorX(self->m_cursor) > SCREEN_MIDDLE) {
            self->m_rosterSelected = false;
            Post(ASSIGN_BUTTON, CONTROL_MSG_SET_TEXT_ID, ASSIGN_STYLE, TEXT_DETACH);
        }
    } else if (CursorX(self->m_cursor) < SCREEN_MIDDLE) {
        self->m_rosterSelected = true;
        Post(ASSIGN_BUTTON, CONTROL_MSG_SET_TEXT_ID, ASSIGN_STYLE, TEXT_ASSIGN);
    }
    // The dragged copy above the picture; the picture hidden.
    I3D_frame *frame = control->GetFrame();
    self->m_dragged = I3DGetDriver()->CreateVisual(frame->GetVisualType());
    if (!self->m_dragged)
        return true;
    self->m_dragged->Duplicate(frame);
    self->m_dragged->LinkTo(self->m_scene->GetPrimarySector(), 0);
    self->m_dragged->SetDir(S_vector(0.0f, 0.0f, 1.0f), 0.0f);
    const S_vector &at = frame->GetWorldPos();
    self->m_dragged->SetPos(S_vector(at.x, at.y, at.z + DRAG_HEIGHT));
    self->m_dragged->SetOn(true);
    self->m_dragged->Update();
    Post(control->GetId(), CONTROL_MSG_HIDE, 0);
    return true;
}

bool C_SquadMenu::OnDragMove(void *menu, void *, C_Control *, uint32_t, uint32_t)
{
    auto *self = static_cast<C_SquadMenu *>(menu);
    if (self->m_selectedSoldier < 0)
        return false;
    // (Quirk kept: the copy's depth the move's.)
    const S_vector &at = self->m_dragged->GetWorldPos();
    S_vector move;
    self->m_cursor->GetMove(&move);
    self->m_dragged->SetPos(S_vector(at.x + move.x, at.y + move.y, move.z));
    self->m_dragged->Update();
    return true;
}

bool C_SquadMenu::OnDrop(void *menu, void *argument, C_Control *control, uint32_t notification, uint32_t value)
{
    auto *self = static_cast<C_SquadMenu *>(menu);
    if (self->m_selectedSoldier < 0)
        return false;
    if (self->m_dragged)
        self->m_dragged->Release();
    self->m_dragged = nullptr;
    S_SquadSlot *slots = self->m_slots;
    const int selected = self->m_selectedSoldier;
    // Shown again: the picture dragged.
    const auto showBack = [control]() { Post(control->GetId(), CONTROL_MSG_SHOW, 0); };
    if (CursorX(self->m_cursor) > SCREEN_MIDDLE) {
        // On the slots' half: onto the slot under the cursor.
        const uint32_t hovered = self->m_cursor->GetHoveredId();
        if (!self->m_rosterSelected) {
            // A slot's soldier: the two slots swapped, the new slot's picture clicked.
            int from = -1;
            for (int i = 4; i-- > 0;) {
                if (slots[i].profile == selected) {
                    from = i;
                    break;
                }
            }
            const int to = SlotOfControl(slots, hovered);
            if (from < 0 || to < 0 || from == to) {
                showBack();
                return true;
            }
            const bool fromRemovable = slots[from].removable;
            slots[from].SetSoldier(slots[to].profile, slots[to].removable);
            slots[to].SetSoldier(selected, fromRemovable);
            self->m_selectedSoldier = -1;
            Post(slots[to].picture, CONTROL_MSG_NOTIFY, CONTROL_NOTIFY_CLICK);
            Post(0, CONTROL_MSG_NOTIFY, CONTROL_NOTIFY_05);
            return true;
        }
        // A roster soldier onto a slot's soldier who may go: replaced.
        const int to = SlotOfControl(slots, hovered);
        if (to >= 0 && slots[to].profile != -1 && slots[to].removable) {
            Post(ROSTER_CONTROLS, CONTROL_MSG_HIDE_ITEM, RosterPicture(selected));
            Post(ROSTER_CONTROLS, CONTROL_MSG_SHOW_ITEM, RosterPicture(slots[to].profile));
            Post(0, SQUAD_MSG_SELECT_FREE, static_cast<uint32_t>(selected));
            slots[to].SetSoldier(selected, true);
            self->m_selectedSoldier = -1;
            Post(0, CONTROL_MSG_NOTIFY, CONTROL_NOTIFY_05);
            return true;
        }
    } else if (self->m_rosterSelected) {
        // A roster soldier back on the roster's half: his picture shown again.
        if (CursorX(self->m_cursor) < SCREEN_MIDDLE) {
            showBack();
            return true;
        }
    } else {
        // A slot's soldier who stays: his picture shown again.
        const int slot = SlotOfPicture(slots, control->GetId());
        if (slot >= 0 && !slots[slot].removable) {
            showBack();
            return false;
        }
    }
    // Assigned (a roster soldier) or detached (a slot's, dropped on the roster's half).
    if (self->m_rosterSelected || !(CursorX(self->m_cursor) > SCREEN_MIDDLE)) {
        OnAssign(menu, argument, control, notification, value);
        self->m_selectedSoldier = -1;
        Post(0, CONTROL_MSG_NOTIFY, CONTROL_NOTIFY_05);
        return true;
    }
    showBack();
    return true;
}

bool C_SquadMenu::OnSoldierNotify13(void *menu, void *argument, C_Control *control, uint32_t notification, uint32_t value)
{
    auto *self = static_cast<C_SquadMenu *>(menu);
    if (self->m_selectedSoldier == -1)
        return false;
    // On the slots' half, a slot's soldier who stays: his picture shown again.
    if (CursorX(self->m_cursor) > SCREEN_MIDDLE) {
        const int slot = SlotOfPicture(self->m_slots, control->GetId());
        if (slot >= 0 && !self->m_slots[slot].removable) {
            Post(control->GetId(), CONTROL_MSG_SHOW, 0);
            return false;
        }
    }
    OnAssign(menu, argument, control, notification, value);
    Post(0, CONTROL_MSG_NOTIFY, CONTROL_NOTIFY_05);
    self->m_selectedSoldier = -1;
    return true;
}

// ---------------------------------------------------------------------------
// Part 9g-xv: the automatic selection

namespace {

// The candidates kept for a slot: the best five.
constexpr int AUTO_CANDIDATES = 5;

}   // namespace

void C_SquadMenu::KeepBest(int *ids, int *scores, int *count, int id, int score)
{
    if (*count < AUTO_CANDIDATES) {
        ids[*count] = id;
        scores[*count] = score;
        ++*count;
        return;
    }
    // Full: the worst of those scoring less replaced.
    int worst = -1, worstScore = -1;
    for (int i = 0; i < *count; ++i) {
        if (scores[i] < score && (worst < 0 || scores[i] < worstScore)) {
            worstScore = scores[i];
            worst = i;
        }
    }
    if (worst >= 0) {
        scores[worst] = score;
        ids[worst] = id;
    }
}

bool C_SquadMenu::OnAutoSelect(void *menu, void *, C_Control *, uint32_t, uint32_t)
{
    auto *self = static_cast<C_SquadMenu *>(menu);
    C_HumanStore *store = g_gameProfile.m_humanStore;
    S_SquadSlot *slots = self->m_slots;
    if (IsOneSoldier()) {
        // One soldier: a living one of the first 30 at random. (Quirk kept: the roster not checked.)
        if (slots[0].profile < 0) {
            int pick;
            do {
                do
                    pick = std::rand() % ROSTER_LIMIT;
                while (store->Get(pick)->dead);
            } while (self->IsInSlots(pick));
            Post(ROSTER_CONTROLS, CONTROL_MSG_HIDE_ITEM, RosterPicture(pick));
            slots[0].SetSoldier(pick, true);
        }
        self->SelectFreeSoldier(0);
        return true;
    }
    // Each empty slot from the first: one of the five best of the living free roster soldiers at random
    // (the first and third slots by stat 4, the second by stat 7, the fourth by stat 1 less a tenth of
    // stat 4). (Quirk kept: the first 30 roster soldiers read unchecked.)
    int slot = 0;
    while (slot < 4 && slots[slot].profile > -1)
        ++slot;
    for (; slot < 4; ++slot) {
        int ids[AUTO_CANDIDATES], scores[AUTO_CANDIDATES], count = 0;
        for (int i = 0; i < ROSTER_LIMIT; ++i) {
            const S_SoldierProfile *profile = store->Get(i);
            if (profile->dead || self->IsInSlots(i))
                continue;
            int score;
            if (slot == 0 || slot == 2)
                score = static_cast<int>(profile->stats[4]);
            else if (slot == 1)
                score = static_cast<int>(profile->stats[7]);
            else
                score = static_cast<int>((profile->stats[1] - profile->stats[4] * 0.1f) * 10.0f);
            KeepBest(ids, scores, &count, i, score);
        }
        if (count <= 0)
            continue;
        const int pick = ids[std::rand() % count];
        Post(ROSTER_CONTROLS, CONTROL_MSG_HIDE_ITEM, RosterPicture(pick));
        slots[slot].SetSoldier(pick, true);
    }
    self->SelectFreeSoldier(0);
    return true;
}

// ---------------------------------------------------------------------------
// Part 9g-xvi: the selected soldier's stats, medals and name; the screen's tick and hiding

namespace {

// The stats' panel's rows (their bars' and values' ids: the row's 3rd and 4th controls); the other panel's
// text; the name's edit box, the continue button.
constexpr uint32_t STATS_ROW_STEP = 0x3000, STATS_FIRST_BAR = 0x4202000, STATS_FIRST_VALUE = 0x4203000;
constexpr uint32_t INFO_TEXT = 0x4301000, NAME_EDIT = 0x4500000, CONTINUE_BUTTON = 0x4B00000;
constexpr uint32_t VALUE_STYLE = 0x17, INFO_STYLE = 1, NAME_STYLE_EDIT = 0xC;
// The medals' places: the first six for the oldest ribbons, the last six for the medals shown.
constexpr int MEDAL_RIBBON_PLACES = 6;

void PostBar(uint32_t bar, float value)
{
    S_ControlMessage message;
    message.target = bar;
    message.id = CONTROL_MSG_SET_BAR_VALUE;
    message.value2 = FloatValue(value);
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

// A frame put in a place (its position its parent's), shown.
void PlaceFrame(I3D_frame *frame, I3D_frame *place)
{
    frame->LinkTo(place, 1);
    frame->SetPosInline(S_vector(0.0f, 0.0f, 0.0f));
    frame->SetOn(true);
}

}   // namespace

void C_SquadMenu::ShowStats()
{
    const S_SoldierProfile *profile = g_gameProfile.m_humanStore->Get(m_selectedSoldier);
    if (!profile) {
        // Nobody: the bars and values emptied, the text, the name's box inactive and blank.
        for (uint32_t i = 0; i < 7; ++i) {
            PostBar(STATS_FIRST_BAR + i * STATS_ROW_STEP, 0.0f);
            Post(STATS_FIRST_VALUE + i * STATS_ROW_STEP, CONTROL_MSG_SET_TEXT_ID, SKILL_STYLE, TEXT_BLANK);
        }
        Post(INFO_TEXT, CONTROL_MSG_SET_TEXT_ID, INFO_STYLE, TEXT_BLANK);
        Post(NAME_EDIT, CONTROL_MSG_DEACTIVATE, 0);
        Post(NAME_EDIT, CONTROL_MSG_SET_TEXT_ID, NAME_STYLE_EDIT, TEXT_BLANK);
    } else {
        // The rows: shooting, strength (Kg), health (its bar full), endurance, stealth, first aid, lockpick.
        const float *stats = profile->stats;
        const auto row = [](uint32_t i, float bar, const C_str &value) {
            PostBar(STATS_FIRST_BAR + i * STATS_ROW_STEP, bar);
            PostText(STATS_FIRST_VALUE + i * STATS_ROW_STEP, VALUE_STYLE, value);
        };
        row(0, stats[4] * 0.01f, C_fstr("%3.0f %%", static_cast<double>(stats[4])));
        row(1, stats[1] * 0.01f, C_fstr("%3.0f Kg", static_cast<double>(stats[1])));
        row(2, 1.0f, C_fstr("%3.0f", static_cast<double>(stats[0])));
        row(3, stats[2] * 0.01f, C_fstr("%3.0f", static_cast<double>(stats[2])));
        row(4, stats[6] * 0.01f, C_fstr("%3.0f %%", static_cast<double>(stats[6])));
        row(5, stats[7] * 0.01f, C_fstr("%3.0f %%", static_cast<double>(stats[7])));
        row(6, stats[8] * 0.01f, C_fstr("%3.0f %%", static_cast<double>(stats[8])));
        // His biography's text, his name (editable).
        Post(INFO_TEXT, CONTROL_MSG_SET_TEXT_ID, INFO_STYLE, static_cast<uintptr_t>(profile->unknown44));
        Post(NAME_EDIT, CONTROL_MSG_ACTIVATE, 0);
        PostText(NAME_EDIT, NAME_STYLE_EDIT, GetProfileName(profile));
    }
    ShowMedals(profile);
}

void C_SquadMenu::ShowMedals(const S_SoldierProfile *profile)
{
    // (Fix: no medals' frames (their panel not made): nothing; the original reads them unchecked.)
    if (!m_medalFrames[0][0])
        return;
    for (int i = SQUAD_MEDALS; i-- > 0;) {
        m_medalFrames[i][0]->SetOn(false);
        m_medalFrames[i][1]->SetOn(false);
    }
    if (!profile)
        return;
    const std::vector<int> &medals = profile->unknown28;
    if (medals.size() <= MEDAL_RIBBON_PLACES) {
        // Up to six: each medal's first frame in the last six places.
        for (size_t i = 0; i < medals.size(); ++i) {
            I3D_frame *const *pair = m_medalFrames[medals[i]];
            PlaceFrame(pair[0], m_medalSlots[MEDAL_RIBBON_PLACES + i]);
            pair[1]->SetOn(false);
            pair[1]->Update();
            pair[0]->Update();
        }
        return;
    }
    // More: the oldest as their second frames (ribbons) in the first places, the last six's first frames in
    // the last six.
    size_t shown = 0;
    for (size_t i = 0; i < medals.size(); ++i) {
        I3D_frame *const *pair = m_medalFrames[medals[i]];
        if (medals.size() - i > MEDAL_RIBBON_PLACES) {
            PlaceFrame(pair[1], m_medalSlots[i]);
            pair[0]->SetOn(false);
        } else {
            PlaceFrame(pair[0], m_medalSlots[MEDAL_RIBBON_PLACES + shown++]);
            pair[1]->SetOn(false);
        }
        pair[1]->Update();
        pair[0]->Update();
    }
}

bool C_SquadMenu::OnNameEdited(void *menu, void *, C_Control *control, uint32_t, uint32_t)
{
    auto *self = static_cast<C_SquadMenu *>(menu);
    // The box's text the selected soldier's own name; his slots' texts and his stats again.
    S_ControlMessage text;
    text.id = CONTROL_MSG_GET_TEXT;
    control->HandleMessage(&text);
    g_gameProfile.m_humanStore->Get(self->m_selectedSoldier)->SetCustomName(text.text);
    for (int i = 4; i-- > 0;)
        if (self->m_slots[i].profile == self->m_selectedSoldier)
            self->m_slots[i].ShowTexts();
    self->ShowStats();
    return true;
}

bool C_SquadMenu::OnScreenTick(void *menu, void *, C_Control *, uint32_t, uint32_t)
{
    // Continuing only with the first slot taken.
    Post(CONTINUE_BUTTON, static_cast<C_SquadMenu *>(menu)->m_slots[0].profile == -1 ? CONTROL_MSG_DEACTIVATE : CONTROL_MSG_ACTIVATE, 0);
    return true;
}

bool C_SquadMenu::OnScreenHidden(void *, void *, C_Control *, uint32_t, uint32_t)
{
    // The names the player gave kept.
    g_gameProfile.m_humanStore->SaveCustomNames();
    return true;
}
