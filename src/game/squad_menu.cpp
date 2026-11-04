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
        OnSoldierControl(this, control);
}
