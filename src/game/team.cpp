// Teams, formations and the team manager (see team.h).
#include "game/first_person_view.h"
#include "game/interface.h"
#include "game/team.h"

#include <cstdlib>
#include <utility>

#include "common/chunk.h"
#include "game/actors_module.h"
#include "game/game.h"
#include "game/game_internal.h"
#include "game/human.h"
#include "game/human_controller.h"
#include "game/pending.h"
#include "game/squad_commander.h"
#include "game/world.h"
#include "ls3d/i3d.h"

C_TeamManager g_teamManager(MODULE_TEAM, "TeamMgr");    // 0x0089ae78 (static init 0x00460a50)

namespace {

// Saved game chunks.
enum : uint16_t {
    CHUNK_TEAM_MANAGER = 0x2694,
    CHUNK_TEAM_MANAGER_PLAYER_TEAM = 1000,
    CHUNK_TEAM_MANAGER_FORMATION = 2000,                // the leader's actor id, then the formation
    CHUNK_TEAM = 0xDFB,
    CHUNK_TEAM_MEMBERS = 1000,                          // C_TeamBase
    CHUNK_TEAM_BASE = 0x9A4,
    CHUNK_TEAM_BASE_COUNT = 1000,                       // the members' actor ids follow the chunk
    CHUNK_TEAM_BASE_NAME = 2000,
    CHUNK_FORMATION = 9999,
    CHUNK_FORMATION_SHAPE = 1000,
};

// Single player: a member is in a restricted area.
constexpr int TEXT_SQUAD_RESTRICTED_AREA = 0x1CEF;

// Squad members within this distance (squared, m) of a human react to it.
constexpr float RELEASE_DISTANCE2 = 30.0f * 30.0f;

// The vehicle seat `human` sits in hides it (cars, submarines, static weapons, the Junkers).
bool IsHiddenInSeat(const C_Human *human)
{
    const C_Actor *vehicle = human->GetVehicle();
    if (!vehicle)
        return false;
    switch (vehicle->GetType()) {
    case ACTOR_CAR:
    case ACTOR_SUBMARINE:
    case ACTOR_STATIC_WEAPON:
    case ACTOR_JUNKERS88:
        return (const_cast<C_Actor *>(vehicle)->GetSeatFlags(human->GetVehicleSeat()) & SEAT_FLAG_HIDDEN) != 0;
    default:
        return false;
    }
}

// Shows the model of the member the player leaves, unless its seat hides it.
void ShowLeftMember(C_Human *human)                     // 0x0045db90
{
    if (!human)
        return;
    human->GetFrame()->SetOn(!IsHiddenInSeat(human));
}

float Distance2(const C_Human *a, const C_Human *b)
{
    return (a->GetFrame()->GetWorldPos() - b->GetFrame()->GetWorldPos()).Square();
}

} // namespace

// ---------------------------------------------------------------------------
// C_EnemyRecords

C_EnemyRecords::~C_EnemyRecords()
{
    Clear();
}

void C_EnemyRecords::Clear()
{
    for (size_t i = m_records.size(); i-- > 0;)
        delete m_records[i];
    m_records.clear();
    m_deadCount = 0;
    m_aliveCount = 0;
    m_tacticalView = false;
}

bool C_EnemyRecords::Contains(const C_Human *enemy) const
{
    for (size_t i = m_records.size(); i-- > 0;) {
        if (m_records[i]->enemy == enemy)
            return true;
    }
    return false;
}

void C_EnemyRecords::Add(C_Human *enemy)                // 0x0045e410 (DS 0x00479640)
{
    if (IsClient())
        GetGame()->GetInterface()->ShowEnemy(enemy, true);
    if (m_tacticalView && enemy->IsFadedOut())
        enemy->FadeIn(FADE_TIME);
    for (size_t i = 0; i < m_records.size(); ++i) {
        S_EnemyRecord *record = m_records[i];
        if (record->enemy == enemy) {
            record->forgetTime = 0;
            ++record->seenBy;
            // (Also when the record is already a dead one: the counts drift.)
            if (!enemy->IsAlive())
                MarkDead(enemy);
            return;
        }
    }
    // New records go first.
    m_records.insert(m_records.begin(), new S_EnemyRecord{enemy, 1, true, 0});
    ++m_aliveCount;
    if (!enemy->IsAlive())
        MarkDead(enemy);
    if (m_tacticalView)
        enemy->FadeIn(FADE_TIME);
    m_changed = true;
}

void C_EnemyRecords::MarkDead(C_Human *enemy)           // 0x0045e3b0
{
    for (int i = 0; i < static_cast<int>(m_records.size()); ++i) {
        if (m_records[i]->enemy != enemy)
            continue;
        m_records[i]->alive = false;
        --m_aliveCount;
        ++m_deadCount;
        if (i < m_aliveCount)
            std::swap(m_records[i], m_records[m_aliveCount]);
        return;
    }
}

void C_EnemyRecords::Remove(C_Human *enemy)             // 0x0045e540 (DS 0x004797a0)
{
    if (IsClient())
        GetGame()->GetInterface()->ShowEnemy(enemy, false);
    if (!enemy->IsAlive() || enemy->IsUnconscious())
        return;
    for (S_EnemyRecord *record : m_records) {
        if (record->enemy != enemy)
            continue;
        if (record->seenBy > 0)
            --record->seenBy;
        if (record->seenBy == 0) {
            record->forgetTime = FORGET_TIME;
            if (m_tacticalView)
                enemy->FadeOut(FADE_TIME);
        }
        return;
    }
}

void C_EnemyRecords::Update(int timeDelta)              // 0x0045e2f0
{
    m_changed = false;
    for (size_t i = m_records.size(); i-- > 0;) {
        S_EnemyRecord *record = m_records[i];
        if (record->forgetTime == 0)                    // 0x0045e2c0
            continue;
        record->forgetTime -= timeDelta;
        if (record->forgetTime >= 1)
            continue;
        delete record;
        m_records.erase(m_records.begin() + static_cast<ptrdiff_t>(i));
        m_changed = true;
        // (Counted off the living ones also when it was recorded dead.)
        --m_aliveCount;
    }
}

void C_EnemyRecords::UpdateOnServer(int timeDelta)      // 0x0045e380
{
    if (!IsClient())
        Update(timeDelta);
}

// ---------------------------------------------------------------------------
// C_TeamBase

C_TeamBase::C_TeamBase(int id)                          // 0x004617f0
    : m_id(id)
{
}

C_TeamBase::~C_TeamBase()                               // 0x00461930
{
    C_TeamBase::Clear();
}

void C_TeamBase::Clear()                                // 0x004619c0
{
    m_members.clear();
}

bool C_TeamBase::AddMember(C_Human *human)              // 0x00461aa0
{
    if (!human || IsMember(human))
        return false;
    m_members.push_back(human);
    return true;
}

bool C_TeamBase::RemoveMember(C_Human *human, int *index)   // 0x00461c70
{
    if (!human)
        return false;
    const int i = IndexOf(human);
    if (i < 0)
        return false;
    m_members[i] = m_members.back();
    m_members.pop_back();
    if (index)
        *index = i;
    return true;
}

void C_TeamBase::RestartMission(bool /*server*/)        // 0x00461d30
{
    if (IsLocalGame())
        Clear();
}

bool C_TeamBase::SaveGame(C_chunk &chunk)               // 0x00461d50
{
    chunk.Begin(CHUNK_TEAM_BASE);
    const int count = GetMemberCount();
    chunk.WriteChunk(CHUNK_TEAM_BASE_COUNT, count);
    for (const C_Human *member : m_members)
        chunk.Write(member->GetNetId());
    chunk.WriteStringChunk(CHUNK_TEAM_BASE_NAME, m_name.c_str());
    chunk.Ascend();
    return true;
}

bool C_TeamBase::LoadGame(C_chunk &chunk)               // 0x00461f30
{
    uint16_t id = 0;
    // (The chunk's id is not checked.)
    if (!chunk.Descend(id))
        return true;
    while (chunk.RemainingSize() > 0) {
        chunk.Descend(id);
        switch (id) {
        case CHUNK_TEAM_BASE_COUNT: {
            int count = 0;
            chunk.Read(count);
            chunk.Ascend();
            for (; count > 0; --count) {
                int actorId = -1;
                chunk.Read(actorId);
                AddMember(static_cast<C_Human *>(g_actorsModule.GetActorById(actorId)));
            }
            break;
        }
        case CHUNK_TEAM_BASE_NAME: {
            char name[1000];
            const int remaining = chunk.RemainingSize();
            const int size = remaining < 0 ? 0 : remaining < 1000 ? remaining : 999;
            chunk.Read(name, size);
            name[size] = '\0';
            m_name = name;
            chunk.Ascend();
            break;
        }
        default:
            chunk.Ascend();
            break;
        }
    }
    chunk.Ascend();
    return true;
}

bool C_TeamBase::IsMember(const C_Human *human) const   // 0x00461a20
{
    return IndexOf(human) >= 0;
}

int C_TeamBase::IndexOf(const C_Human *human) const     // 0x00461a60
{
    for (int i = GetMemberCount(); i-- > 0;) {
        if (m_members[i] == human)
            return i;
    }
    return -1;
}

int C_TeamBase::GetMemberCount() const                  // 0x00462300
{
    return static_cast<int>(m_members.size());
}

C_Human *C_TeamBase::GetMember(int index) const         // 0x004619f0
{
    if (index < 0 || index >= GetMemberCount())
        return nullptr;
    return m_members[index];
}

int C_TeamBase::GetAliveCount() const                   // 0x004622c0
{
    int count = 0;
    for (const C_Human *member : m_members) {
        if (member && member->IsAlive())
            ++count;
    }
    return count;
}

// ---------------------------------------------------------------------------
// C_Team

C_Team::C_Team()                                        // 0x0045d1f0
    : C_TeamBase(-1)
{
    Clear();
}

C_Team::~C_Team()                                       // 0x0045d2e0
{
    Clear();
}

void C_Team::Clear()                                    // 0x0045d3a0
{
    C_TeamBase::Clear();
    m_selected = -1;
    m_active = -1;
    m_commanded = -1;
    m_watchedHuman = nullptr;
    m_enemyRecords.Clear();
    for (size_t i = m_volumes.size(); i-- > 0;)
        delete m_volumes[i];
    m_volumes.clear();
    m_seenMeleeTrackers.clear();
    m_environment = 0;
}

bool C_Team::AddMember(C_Human *human)                  // 0x0045e5d0
{
    const bool added = C_TeamBase::AddMember(human);
    if (GetMemberCount() == 1) {
        m_active = 0;
        GetGame()->GetFirstPersonView()->Refresh();
        UpdateCameraTarget();
    }
    // (Also for a human that is a member already: the panel of the last slot gets it.)
    GetGame()->GetInterface()->AddSquadMember(human, GetMemberCount() - 1);
    if (added)
        CollectVolumes(GetMemberCount() - 1);
    return added;
}

bool C_Team::RemoveMember(C_Human *human)               // 0x0045e670
{
    int index = -1;
    if (!C_TeamBase::RemoveMember(human, &index))
        return false;
    GetGame()->GetInterface()->RemoveSquadMember(human, index);
    if (m_active == index) {
        // The camera's soldier may have been hidden (first person view).
        // Fix: the original showed the member now in slot `index` (the last one moves there),
        // and read past the members when the removed one was the last.
        human->GetFrame()->SetOn(true);
        m_active = -1;
    } else if (m_active >= GetMemberCount()) {
        // (The index of the member that moved is not followed.)
        m_active = -1;
    }
    if (m_active == -1 && GetMemberCount() != 0)
        m_active = 0;
    if (m_selected == index)
        m_selected = -1;
    if (m_commanded == index)
        m_commanded = -1;
    delete m_volumes[index];
    m_volumes[index] = m_volumes.back();
    m_volumes.pop_back();
    return true;
}

void C_Team::Update(int timeDelta)                      // 0x0045ddc0
{
    C_TeamBase::Update(timeDelta);
    m_enemyRecords.Update(timeDelta);
}

void C_Team::Tick(int timeDelta)                        // 0x0045dd90
{
    C_TeamBase::Tick(timeDelta);
    m_enemyRecords.UpdateOnServer(timeDelta);
    // (The original also counts the living members here and drops the count.)
    CheckEnvironment();
}

void C_Team::RestartMission(bool server)                // 0x0045dde0
{
    // Single player: Clear() drops the members too.
    C_TeamBase::RestartMission(server);
    m_selected = -1;
    m_commanded = -1;
    if (GetMemberCount() == 1)
        m_active = 0;
    m_watchedHuman = nullptr;
    m_enemyRecords.Clear();
    m_seenMeleeTrackers.clear();
    m_environment = 0;
}

bool C_Team::SaveGame(C_chunk &chunk)                   // 0x0045de90
{
    chunk.Begin(CHUNK_TEAM);
    chunk.Begin(CHUNK_TEAM_MEMBERS);
    C_TeamBase::SaveGame(chunk);
    chunk.Ascend();
    chunk.Ascend();
    return true;
}

bool C_Team::LoadGame(C_chunk &chunk)                   // 0x0045e040
{
    uint16_t id = 0;
    if (!chunk.Descend(id))
        return true;
    while (chunk.RemainingSize() > 0) {
        chunk.Descend(id);
        if (id == CHUNK_TEAM_MEMBERS)
            C_TeamBase::LoadGame(chunk);
        chunk.Ascend();
    }
    chunk.Ascend();
    return true;
}

void C_Team::SetNPCMember(C_Human *npc, bool member)    // 0x0045d100
{
    if (member)
        AddMember(npc);
    else
        RemoveMember(npc);
}

C_Human *C_Team::GetActivePlayer() const                // 0x0045d1d0
{
    if (m_watchedHuman)
        return m_watchedHuman;
    return m_active == -1 ? nullptr : m_members[m_active];
}

C_Human *C_Team::GetSelectedMember() const              // 0x0045d160
{
    if (m_selected == -1)
        return nullptr;
    C_Human *member = m_members[m_selected];
    if (!IsLocalGame() && member->IsSpectating())
        return nullptr;
    return member;
}

C_Human *C_Team::GetCommandedMember() const             // 0x0045d1b0
{
    return m_commanded == -1 ? nullptr : m_members[m_commanded];
}

void C_Team::SelectMember(C_Human *human)               // 0x0045da60
{
    const int previous = m_active;
    for (int i = GetMemberCount(); i-- > 0;) {
        if (m_members[i] != human)
            continue;
        m_active = i;
        m_selected = i;
        if (i != previous)
            ::GetCameraModule()->SelectView(0);
        m_commanded = -1;
        return;
    }
}

bool C_Team::SwitchTo(int index)                        // 0x0045dc20
{
    if (index < 0 || index >= GetMemberCount())
        return false;
    C_Human *member = m_members[index];
    if (!member->IsAlive() || member->GetRole() != HUMAN_ROLE_PLAYER)
        return false;
    ShowLeftMember(m_members[m_active]);
    m_members[m_active]->AssignPlayerController(false);
    m_active = index;
    m_commanded = index;
    m_selected = -1;
    GetGame()->GetFirstPersonView()->Refresh();
    UpdateCameraTarget();
    GetGame()->Print("You are switched to %s", member->GetFullName());
    return true;
}

int C_Team::GetNextControllable() const                 // 0x0045d660
{
    const int count = GetMemberCount();
    if (count == 0)
        return 0;
    int index = m_active + 1;
    for (int step = 0; step < GetMemberCount(); ++step, ++index) {
        if (index >= GetMemberCount())
            index = 0;
        if (index == m_active)
            return 0;
        const C_Human *member = m_members[index];
        if (member->IsAlive() && member->GetRole() == HUMAN_ROLE_PLAYER)
            return index;
    }
    return index;
}

int C_Team::GetPreviousControllable() const             // 0x0045d9d0
{
    if (GetMemberCount() == 0)
        return 0;
    int index = m_active - 1;
    for (int step = 0; step < GetMemberCount(); ++step, --index) {
        if (index < 0)
            index = GetMemberCount() - 1;
        if (index == m_active)
            return 0;
        const C_Human *member = m_members[index];
        if (member->IsAlive() && member->GetRole() == HUMAN_ROLE_PLAYER)
            return index;
    }
    return index;
}

bool C_Team::SetCommandedIndex(int index)               // 0x0045dcc0
{
    if (m_commanded == index)
        return true;
    if (index == -1) {
        m_commanded = -1;
        return true;
    }
    if (index < 0 || index >= GetMemberCount())
        return false;
    const C_Human *member = m_members[index];
    if (!member->IsAlive() || member->GetType() != ACTOR_HUMAN)
        return false;
    m_commanded = index;
    if (m_selected == index) {
        m_members[m_active]->AssignPlayerController(false);
        m_selected = -1;
    }
    return true;
}

bool C_Team::SetCommanded(C_Human *human)               // 0x0045dd40
{
    if (human) {
        for (int i = GetMemberCount(); i-- > 0;) {
            if (m_members[i] == human)
                return SetCommandedIndex(i);
        }
    }
    m_commanded = -1;
    return true;
}

void C_Team::ReturnControl()                            // 0x0045da40
{
    if (m_active != -1)
        m_members[m_active]->AssignPlayerController(true);
}

void C_Team::UpdateCameraTarget()                       // 0x0045dac0
{
    C_Human *active = GetActivePlayer();
    if (!active)
        return;
    ::GetCameraModule()->SelectView(0);
    if (IsHiddenInSeat(active)) {
        active->GetFrame()->SetOn(false);
        return;
    }
    // Hidden behind the first person weapon view.
    const C_game *game = GetGame();
    active->GetFrame()->SetOn(!game->IsFirstPersonView() && !game->IsBinocularsView());
}

void C_Team::SetWatchedHuman(C_Human *human)            // 0x0045e8f0
{
    m_watchedHuman = human;
    ::GetCameraModule()->SelectView(0);
    GetGame()->GetFirstPersonView()->Refresh();
}

void C_Team::CollectVolumes(int index)                  // 0x0045d710
{
    if (index == static_cast<int>(m_volumes.size()))
        m_volumes.push_back(new std::vector<I3D_frame *>());
    std::vector<I3D_frame *> *list = m_volumes[index];
    list->clear();
    GetMember(index)->GetFrame()->EnumFrames(AddVolume, list, FRAME_MASK_VOLUME, 0);
}

uint32_t __stdcall C_Team::AddVolume(I3D_frame *frame, void *list)   // 0x0045d7c0
{
    static_cast<std::vector<I3D_frame *> *>(list)->push_back(frame);
    return 0;
}

I3D_frame *C_Team::GetRandomVolume(int index) const     // 0x0045d6d0
{
    // (The member's model has to have a volume.)
    const std::vector<I3D_frame *> &list = *m_volumes[index];
    return list[static_cast<unsigned>(std::rand()) % list.size()];
}

void C_Team::OnMemberDied(C_Human *human)               // 0x0045e790 (DS 0x00479a40)
{
    if (!IsMember(human))
        return;
    const std::vector<C_Human *> &seen = human->GetSeenEnemies();
    for (size_t i = seen.size(); i-- > 0;)
        m_enemyRecords.Remove(seen[i]);
#ifndef HD2_DEDICATED   // the server has no map
    GetGame()->GetInterface()->RemoveFromMap(human);
#endif
    if (human == GetCommandedMember())
        SetCommandedIndex(-1);
    GetGame()->Print("%s is dead !!!", human->GetFullName());
    // Allied NPCs do not keep the mission going.
    for (int i = GetMemberCount(); i-- > 0;) {
        const C_Human *member = m_members[i];
        if (member->IsAlive() && member->GetRole() != HUMAN_ROLE_NPC_TEAM)
            return;
    }
    if (IsLocalGame())
        GetGame()->FailMission(false);
}

void C_Team::CheckEnvironment()                         // 0x0045ef00
{
    if (!IsLocalGame())
        return;
    if (const C_MissionDef *mission = GetGame()->GetMission()) {
        if (mission->AreObjectivesDecided())
            return;
    }
    uint32_t flags = 0;
    for (int i = GetMemberCount(); i-- > 0;) {
        const C_Human *member = GetMember(i);
        if (member && member->IsAlive())
            flags |= member->GetEnvironmentFlags();
    }
    if (m_environment == flags)
        return;
    m_environment = flags;
    if (flags & HUMAN_ENV_DEADLY) {
        GetGame()->FailMission(true);
        return;
    }
    GetGame()->GetInterface()->ShowMessage((flags & HUMAN_ENV_RESTRICTED) ? TEXT_SQUAD_RESTRICTED_AREA : -1);
}

bool C_Team::HasItem(int itemId) const                  // 0x0045e9a0
{
    for (int i = GetMemberCount(); i-- > 0;) {
        const C_Human *member = GetMember(i);
        if (member->IsAlive() && member->FindItem(itemId))
            return true;
    }
    return false;
}

bool C_Team::HasMedkitBesides(const C_Human *human) const   // 0x0045ea00
{
    for (int i = GetMemberCount(); i-- > 0;) {
        const C_Human *member = GetMember(i);
        if (member == human || !member->IsAlive())
            continue;
        if (member->GetMedkit())
            return true;
    }
    return false;
}

void C_Team::NotifyGrenadeThrown(const S_vector &pos, const S_vector &dir, C_Actor *thrower)   // 0x0045eab0
{
    for (int i = GetMemberCount(); i-- > 0;) {
        C_Human *member = GetMember(i);
        if (!member->IsAlive() || member->GetControllerType() != HUMAN_CONTROLLER_SQUAD || member == thrower)
            continue;
        static_cast<C_SquadController *>(member->GetController())->OnGrenadeThrown(pos, dir, thrower);
    }
}

void C_Team::HealMembers()                              // 0x0045f020
{
    // (Not at the hardest difficulty.)
    if (!IsLocalGame() || g_gameProfile.GetDifficulty() == DIFFICULTY_NO_SAVES)
        return;
    for (int i = GetMemberCount(); i-- > 0;) {
        C_Human *member = GetMember(i);
        if (member && member->IsAlive())
            member->SetHealthPercent(100);
    }
}

void C_Team::ReleaseHoldingMembers(const C_Human *human)   // 0x0045f110
{
    for (int i = GetMemberCount(); i-- > 0;) {
        C_Human *member = GetMember(i);
        if (!member || !member->IsAlive() || member->GetControllerType() != HUMAN_CONTROLLER_SQUAD)
            continue;
        auto *squad = static_cast<C_SquadController *>(member->GetController());
        int order;
        switch (squad->GetCombatOrder()) {
        case SQUAD_ORDER_MODE2:
            order = SQUAD_ORDER_MODE1;
            break;
        case SQUAD_ORDER_MODE2_ALT:
            order = SQUAD_ORDER_MODE1_ALT;
            break;
        default:
            continue;
        }
        if (Distance2(human, member) < RELEASE_DISTANCE2)
            squad->SetCombatOrder(order);
    }
}

bool C_Team::HasMemberInPlayerSlots() const             // 0x0045d120
{
    for (int i = 0; i < GetMemberCount(); ++i) {
        if (m_members[i]->GetNetId() < C_ActorsModule::PLAYER_SLOTS)
            return true;
    }
    return false;
}

bool C_Team::IsEnemyKnown(const C_Human *enemy) const   // 0x0045e920
{
    return m_enemyRecords.Contains(enemy);
}

void C_Team::GetSeenEnemies(std::vector<C_Human *> &enemies) const   // 0x0045eb30
{
    enemies.clear();
    for (int i = m_enemyRecords.GetCount(); i-- > 0;) {
        const S_EnemyRecord *record = m_enemyRecords.GetRecord(i);
        if (record->seenBy != 0)
            enemies.push_back(record->enemy);
    }
}

void C_Team::GetSeenLivingEnemies(std::vector<C_Human *> &enemies) const   // 0x0045ed10
{
    enemies.clear();
    for (int i = m_enemyRecords.GetCount(); i-- > 0;) {
        const S_EnemyRecord *record = m_enemyRecords.GetRecord(i);
        if (record->seenBy != 0 && record->enemy->IsAlive() && !record->enemy->IsUnconscious())
            enemies.push_back(record->enemy);
    }
}

void C_Team::RefreshSight()                             // 0x0045f080
{
    for (int i = GetMemberCount(); i-- > 0;) {
        const C_Human *member = GetMember(i);
        if (!member || !member->IsAlive())
            continue;
        for (int id = g_actorsModule.GetActorCount(); id-- > 0;) {
            const auto *other = static_cast<const C_Human *>(g_actorsModule.GetHuman(id));
            if (!other || other->GetRole() == HUMAN_ROLE_PLAYER || other->GetRole() == HUMAN_ROLE_NPC_TEAM ||
                other == member)
                continue;
            g_sightModule.TestNow(member->GetSightId(), other->GetSightTargetId());
        }
    }
}

C_Human *C_Team::GetEnemySeenNearby(const C_Human *human) const   // 0x0045f280
{
    if (!human)
        return nullptr;
    for (int i = GetMemberCount(); i-- > 0;) {
        const C_Human *member = GetMember(i);
        if (member == human || !member->IsAlive())
            continue;
        const std::vector<C_Human *> &seen = member->GetSeenEnemies();
        if (Distance2(member, human) <= NEARBY_DISTANCE * NEARBY_DISTANCE && !seen.empty())
            return seen[static_cast<unsigned>(std::rand()) % seen.size()];
    }
    return nullptr;
}

C_Human *C_Team::GetEnemyUnseenBy(const C_Human *human, bool skipCarCrew) const   // 0x0045f3a0
{
    if (!human)
        return nullptr;
    std::vector<C_Human *> candidates;
    for (int i = GetMemberCount(); i-- > 0;) {
        const C_Human *member = GetMember(i);
        if (member == human || !member->IsAlive())
            continue;
        if (Distance2(member, human) > NEARBY_DISTANCE * NEARBY_DISTANCE)
            continue;
        const std::vector<C_Human *> &seen = member->GetSeenEnemies();
        for (size_t k = seen.size(); k-- > 0;) {
            C_Human *enemy = seen[k];
            if (skipCarCrew) {
                const C_Actor *vehicle = enemy->GetVehicle();
                if (vehicle && vehicle->GetType() == ACTOR_CAR &&
                    static_cast<const C_Car *>(vehicle)->GetUnknown5E0() != 0)
                    continue;
            }
            if (!human->HasSeenEnemy(enemy))
                candidates.push_back(enemy);
        }
    }
    if (candidates.empty())
        return nullptr;
    return candidates[static_cast<unsigned>(std::rand()) % candidates.size()];
}

void C_Team::AddSeenMeleeTracker(C_MeleeTracker *meleeTracker)                     // 0x0045d4a0
{
    for (size_t i = m_seenMeleeTrackers.size(); i-- > 0;) {
        if (m_seenMeleeTrackers[i] == meleeTracker)
            return;
    }
    m_seenMeleeTrackers.push_back(meleeTracker);
}

// ---------------------------------------------------------------------------
// C_CoopTeam

C_CoopTeam::C_CoopTeam()                                // 0x0045fb90
    : C_TeamBase(-1)
{
    Clear();
}

C_CoopTeam::~C_CoopTeam()                               // 0x0045fc10
{
    Clear();
}

void C_CoopTeam::Clear()                                // 0x0045fc80
{
    C_TeamBase::Clear();
    m_enemyRecords.Clear();
    m_unknown48 = 0;
}

void C_CoopTeam::Tick(int timeDelta)                    // 0x0045fd50
{
    C_TeamBase::Tick(timeDelta);
    m_enemyRecords.UpdateOnServer(timeDelta);
}

void C_CoopTeam::RestartMission(bool /*server*/)        // 0x0045fd80
{
    m_enemyRecords.Clear();
    m_unknown48 = 0;
}

bool C_CoopTeam::RemoveMember(C_Human *human)           // 0x0045fdf0
{
    int index = -1;
    return C_TeamBase::RemoveMember(human, &index);
}

void C_CoopTeam::OnMemberDied(C_Human *human)           // 0x0045fe10
{
    if (!IsMember(human))
        return;
    const std::vector<C_Human *> &seen = human->GetSeenEnemies();
    for (size_t i = seen.size(); i-- > 0;)
        m_enemyRecords.Remove(seen[i]);
}

// ---------------------------------------------------------------------------
// C_Formation

C_Formation::C_Formation(C_Human *leader, int id)       // 0x004608a0
    : C_TeamBase(id), m_shape(new C_FormationShape())
{
    m_shape->SetUnknown8C(true);
    C_Formation::AddMember(leader);
    m_shape->SetLeader(leader);
    m_shape->SetType(FORMATION_ARROW_SHARP);
}

C_Formation::~C_Formation()                             // 0x00460980
{
    for (int i = GetMemberCount(); i-- > 0;)
        C_Formation::RemoveMember(m_members[i], nullptr);
    delete m_shape;
}

bool C_Formation::AddMember(C_Human *human)             // 0x004603c0
{
    if (human->GetControllerType() != HUMAN_CONTROLLER_AI || !C_TeamBase::AddMember(human))
        return false;
    m_shape->AddMember(human);
    static_cast<C_ScriptedController *>(human->GetController())->SetFormation(m_shape);
    return true;
}

bool C_Formation::RemoveMember(C_Human *human, int *index)   // 0x00460850
{
    if (!C_TeamBase::RemoveMember(human, index))
        return false;
    m_shape->RemoveMember(human);
    if (human->GetControllerType() == HUMAN_CONTROLLER_AI)
        static_cast<C_ScriptedController *>(human->GetController())->SetFormation(nullptr);
    return true;
}

void C_Formation::Tick(int timeDelta)                   // 0x00460a10
{
    m_shape->Tick(timeDelta);
}

bool C_Formation::SaveGame(C_chunk &chunk)              // 0x00460410
{
    chunk.Begin(CHUNK_FORMATION);
    chunk.Begin(CHUNK_FORMATION_SHAPE);
    m_shape->SaveGame(chunk);
    chunk.Ascend();
    chunk.Ascend();
    return true;
}

bool C_Formation::LoadGame(C_chunk &chunk)              // 0x004605c0
{
    uint16_t id = 0;
    if (!chunk.Descend(id))
        return false;
    while (chunk.RemainingSize() > 0) {
        chunk.Descend(id);
        if (id == CHUNK_FORMATION_SHAPE)
            m_shape->LoadGame(chunk);
        chunk.Ascend();
    }
    chunk.Ascend();
    return true;
}

C_Human *C_Formation::GetLeader() const
{
    return m_shape->GetLeader();
}

// ---------------------------------------------------------------------------
// C_TeamManager

C_TeamManager::C_TeamManager(int id, const char *name)  // 0x00461430
    : C_Module(id, name)
{
}

C_TeamManager::~C_TeamManager()                         // 0x004614e0
{
    // (The formations left are not freed.)
}

void C_TeamManager::Tick(int timeDelta)                 // 0x00460ae0
{
    if (!IsClient())
        return;
    GetSquadCommander()->Tick(timeDelta);
    for (size_t i = m_formations.size(); i-- > 0;)
        m_formations[i]->Tick(timeDelta);
}

void C_TeamManager::StartMission(bool server)           // 0x00460aa0
{
    if (!server)
        m_clientRunning = true;
    if (IsLocalGame() && server)
        GetSquadCommander()->Reset();
}

void C_TeamManager::StopMission(bool server)            // 0x00460b30
{
    if (!server) {
        if (IsClient())
            m_playerTeam.RestartMission(false);
        m_clientRunning = false;
        return;
    }
    m_unknown135 = false;
    // (The formations are dropped without being freed: their destructors would reach members
    // that the actors module, which stops first, has deleted.)
    m_formations.clear();
    if (!IsLocalGame() && g_gameProfile.IsCooperativeGame())
        m_coopTeam.Clear();
}

void C_TeamManager::RestartMission(bool server)         // 0x00460c20
{
    if (IsClient())
        m_playerTeam.RestartMission(server);
    if (!IsLocalGame() && IsServer() && g_gameProfile.IsCooperativeGame())
        m_coopTeam.RestartMission(server);
    for (size_t i = m_formations.size(); i-- > 0;)
        delete m_formations[i];
    m_formations.clear();
    GetSquadCommander()->Reset();
}

bool C_TeamManager::SaveGame(C_chunk &chunk)            // 0x00460cd0
{
    chunk.Begin(CHUNK_TEAM_MANAGER);
    chunk.Begin(CHUNK_TEAM_MANAGER_PLAYER_TEAM);
    m_playerTeam.SaveGame(chunk);
    chunk.Ascend();
    for (size_t i = m_formations.size(); i-- > 0;) {
        C_Formation *formation = m_formations[i];
        if (!formation)
            continue;
        chunk.Begin(CHUNK_TEAM_MANAGER_FORMATION);
        // (A formation without a leader leaves the chunk empty.)
        if (formation->GetShape() && formation->GetLeader()) {
            chunk.Write(formation->GetLeader()->GetNetId());
            formation->SaveGame(chunk);
        }
        chunk.Ascend();
    }
    chunk.Ascend();
    return true;
}

bool C_TeamManager::LoadGame(C_chunk &chunk)            // 0x00460fb0
{
    uint16_t id = 0;
    if (!chunk.Descend(id))
        return true;
    while (chunk.RemainingSize() > 0) {
        chunk.Descend(id);
        if (id == CHUNK_TEAM_MANAGER_PLAYER_TEAM) {
            m_playerTeam.LoadGame(chunk);
        } else if (id == CHUNK_TEAM_MANAGER_FORMATION) {
            // Fix: the original read the leader's id also from the empty chunk of a formation
            // without a leader, and created a formation for whatever actor it found (a crash
            // for none).
            int leaderId = -1;
            C_Human *leader = chunk.Read(leaderId)
                                  ? static_cast<C_Human *>(g_actorsModule.GetActorById(leaderId))
                                  : nullptr;
            if (leader)
                GetFormation(leader, true)->LoadGame(chunk);
        }
        chunk.Ascend();
    }
    chunk.Ascend();
    return true;
}

bool C_TeamManager::RemoveFromFormation(C_Human *member)   // 0x00461240
{
    if (!member)
        return false;
    for (size_t i = m_formations.size(); i-- > 0;) {
        if (m_formations[i]->IsMember(member))
            return m_formations[i]->RemoveMember(member, nullptr);
    }
    return false;
}

C_Formation *C_TeamManager::GetFormation(C_Human *leader, bool create)   // 0x00461290
{
    for (size_t i = m_formations.size(); i-- > 0;) {
        if (m_formations[i]->GetLeader() == leader)
            return m_formations[i];
    }
    if (!create)
        return nullptr;
    m_formations.push_back(new C_Formation(leader, static_cast<int>(m_formations.size())));
    return m_formations.back();
}

void C_TeamManager::DestroyFormation(C_Formation *formation)   // 0x00461390
{
    for (size_t i = m_formations.size(); i-- > 0;) {
        if (m_formations[i] != formation)
            continue;
        delete formation;
        m_formations[i] = m_formations.back();
        m_formations.pop_back();
    }
}

bool C_CoopTeam::HasItem(int itemId) const
{
    for (int i = GetMemberCount(); i-- > 0;) {
        const C_Human *member = GetMember(i);
        if (member->IsAlive() && member->FindItem(itemId))
            return true;
    }
    return false;
}

void PrintEnemyKilled(const C_Human *killer)            // 0x0045e8a0 (DS 0x00479b80)
{
    GetGame()->PrintColored(S_vector(1.0f, 0.0f, 0.0f), "%s killed an enemy soldier!!!", killer->GetFullName());
}
