// The squad's commands (C_SquadCommander, the object 0x0089ad40): the HUD's commands to the
// player's soldiers, the commands on actors, the manoeuvres, the groups and the idle reports.
#include "game/squad_commander.h"

#include <algorithm>
#include <cstdlib>
#include <vector>

#include "game/actor.h"
#include "game/checkpoints.h"
#include "game/collision.h"
#include "game/controller_internal.h"
#include "game/controller_messages.h"
#include "game/game.h"
#include "game/human.h"
#include "game/human_controller.h"
#include "game/item_defs.h"
#include "game/items.h"
#include "game/items_module.h"
#include "game/pending.h"
#include "game/squad_internal.h"
#include "game/team.h"
#include "game/world.h"
#include "ls3d/i3d.h"
#include "ls3d/math.h"

namespace {

// The members' idle reports waiting to be said (a vector of its own at 0x0089ad30, outside
// the object): who, and the game time to say it at.
struct S_IdleReport {
    C_Human *member;
    int time;
};
std::vector<S_IdleReport> g_idleReports;                // 0x0089ad30

// Places around a point, in the frame of a direction there (z ahead): MoveNear's (0x0089ac30,
// 6), a set nothing reads (0x0089ac90, 5) and TakeCover's wedge of slots (0x0089ace0, 5). The
// three tables lie one after the other; MoveNear reads on into the next ones for its 7th
// member and on.
const S_vector4 PLACES[16] = {
    {0.0f, 0.0f, 0.0f, 0.0f},   {-4.0f, 0.0f, -2.0f, 0.0f}, {4.0f, 0.0f, -2.0f, 0.0f},
    {-2.0f, 0.0f, -8.0f, 0.0f}, {2.0f, 0.0f, -8.0f, 0.0f},  {0.0f, 0.0f, 0.0f, 0.0f},
    {-7.0f, 0.0f, 10.0f, 0.0f}, {7.0f, 0.0f, 10.0f, 0.0f},  {4.0f, 0.0f, 3.0f, 0.0f},
    {-7.0f, 0.0f, -6.0f, 0.0f}, {7.0f, 0.0f, -6.0f, 0.0f},
    {-10.0f, 0.0f, 15.0f, 0.0f}, {10.0f, 0.0f, 15.0f, 0.0f}, {4.0f, 0.0f, 3.0f, 0.0f},
    {-10.0f, 0.0f, -6.0f, 0.0f}, {10.0f, 0.0f, -6.0f, 0.0f},
};
const S_vector4 *const COVER_SLOTS = PLACES + 11;       // 0x0089ace0
constexpr int COVER_SLOT_COUNT = 5;

// The item the wire cut command needs (inferred: wire cutters).
constexpr int ITEM_WIRE_CUTTERS = 0x3B;

const S_vector UP(0.0f, 1.0f, 0.0f);

C_Human *SelectedMember()
{
    return g_teamManager.GetPlayerTeam()->GetSelectedMember();
}

C_SquadController *SquadOf(const C_Human *human)
{
    return static_cast<C_SquadController *>(human->GetController());
}

bool IsSquadControlled(const C_Human *human)
{
    return human->GetControllerType() == HUMAN_CONTROLLER_SQUAD;
}

// Where the manoeuvres measure from: the selected soldier, else the camera.
S_vector ViewerPos()
{
    if (C_Human *selected = SelectedMember())
        return selected->GetPosition();
    return GetGame()->GetScene()->GetActiveCamera()->GetWorldPos();
}

// The door is open or opening (C_Door::GetState 1 / 2).
bool IsDoorOpening(const C_Door *door)
{
    const int state = door->GetState();
    return state == 1 || state == 2;
}

// The uniform's disguise side is the axis (C_UniformComp).
bool IsAxisUniform(const C_Item *uniform)
{
    return static_cast<const C_UniformComp *>(uniform->GetDef()->GetComponent())->GetDisguiseSide() == 1;
}

// `member` carries a silenced weapon with rounds in it or ammunition for it.
bool HasSilencedWeapon(const C_Human *member)
{
    // (The original looks the three types up first, then checks them in this order.)
    for (int typeId : {ITEM_SILENCED_16, ITEM_SILENCED_7, ITEM_SILENCED_104}) {
        C_Item *weapon = member->FindItem(typeId);
        if (!weapon)
            continue;
        if (weapon->GetAmount() != 0.0f)
            return true;
        if (member->CountAmmo(weapon->GetDef()->GetAmmoItemId(), false) != 0)
            return true;
    }
    return false;
}

} // namespace

C_SquadCommander g_squadCommander;                      // 0x0089ad40

C_SquadCommander *GetSquadCommander()                   // 0x004563e0
{
    return &g_squadCommander;
}

C_SquadCommander::C_SquadCommander()                    // 0x00456780
{
    Reset();
}

void C_SquadCommander::UpdateScale()                    // 0x004563f0
{
    const float scale = static_cast<float>(m_spacing) * 0.75f + 1.5f;
    m_formationScale.x = scale;
    m_formationScale.y = scale;
    m_formationScale.z = scale;
    // (w: the original stores a word of its stack.)
}

void C_SquadCommander::Reset()                          // 0x00456440
{
    m_flag3D = false;
    m_flag3F = false;
    m_flag42 = false;
    m_flag3E = false;
    m_flag41 = false;
    m_flag40 = false;
    m_flag3C = false;
    m_unknown38 = 0;
    m_timer40 = 0;
    m_timer3C = 0;
    m_squadTarget = nullptr;
    m_squadTargetTime = 0;
    m_manoeuvre = MANOEUVRE_NONE;
    m_stage = 0;
    m_spacing = 3;
    UpdateScale();
    m_whispering = false;
    m_unknown44 = 5000;
    // Fix: the groups and the idle reports of the last mission go too (the original keeps
    // them; the actors module has deleted those humans when a mission stops).
    for (std::vector<C_Human *> &group : m_groups)
        group.clear();
    g_idleReports.clear();
}

void C_SquadCommander::Tick(int timeDelta)              // 0x004564a0
{
    if (m_timer3C != 0) {
        m_timer3C -= timeDelta;
        if (m_timer3C < 1) {
            m_timer3C = 0;
            m_flag3C = false;
        }
    }
    if (m_timer40 != 0) {
        m_timer40 -= timeDelta;
        if (m_timer40 < 1) {
            m_timer40 = 0;
            m_flag40 = false;
        }
    }
    if (m_squadTargetTime != 0) {
        m_squadTargetTime -= timeDelta;
        if (m_squadTargetTime < 1) {
            m_squadTargetTime = 0;
            m_squadTarget = nullptr;
        }
    }

    for (std::vector<C_Human *> &group : m_groups)
        group.clear();
    C_Team *team = g_teamManager.GetPlayerTeam();
    C_Human *selected = team->GetSelectedMember();
    if (!selected)
        return;
    for (int i = team->GetMemberCount(); i-- > 0;) {
        C_Human *member = team->GetMember(i);
        if (!member->IsAlive() || member == selected || !IsSquadControlled(member))
            continue;
        const S_vector &from = selected->GetFrame()->GetWorldPos();
        const S_vector &at = member->GetFrame()->GetWorldPos();
        const float dx = at.x - from.x;
        const float dy = at.y - from.y;
        const float dz = at.z - from.z;
        const float square = (dz * dz + dy * dy) + dx * dx;
        const C_SquadController *squad = SquadOf(member);
        const bool busy = !squad->m_orders.empty();
        const bool idle = !busy && squad->m_action.finished;
        const bool close = square <= 1600.0f;
        if (close || busy)
            m_groups[GROUP_NEAR].push_back(member);
        if (idle && close)
            m_groups[GROUP_IDLE_NEAR].push_back(member);
        if (busy)
            m_groups[GROUP_BUSY].push_back(member);
    }
    // (A debug hook, 0x00456ef0, empty in the release build, ran here while a manoeuvre went
    // on.)

    // The idle reports that are due.
    const int now = static_cast<int>(GetGame()->GetTime()) + timeDelta;
    for (size_t i = g_idleReports.size(); i-- > 0;) {
        if (g_idleReports[i].time > now)
            continue;
        C_Human *member = g_idleReports[i].member;
        // Fix: only by a member the squad AI still controls (the original takes the
        // controller of one the player took over meanwhile for a squad controller).
        if (IsSquadControlled(member))
            SquadOf(member)->SayReply(SENTENCE_IDLE, true, 300, false);
        g_idleReports.erase(g_idleReports.begin() + i);
    }
}

void C_SquadCommander::OnMemberIdle(C_Human *member)    // 0x00455af0
{
    if (!member)
        return;
    // After the reports waiting, 300..500 ms more for each (a member says it once).
    int delay = 0;
    for (size_t i = g_idleReports.size(); i-- > 0;) {
        if (g_idleReports[i].member == member)
            return;
        delay = static_cast<int>(RandFloat() * 200.0f + static_cast<float>(delay) + 300.0f);
    }
    g_idleReports.push_back({member, static_cast<int>(GetGame()->GetTime()) + delay});
}

// ------------------------------------------------------------------------------ the groups

void C_SquadCommander::CollectGroup(std::vector<C_Human *> &out, int group)   // 0x004568d0
{
    out.clear();
    if (group == GROUP_IDLE) {
        C_Human *selected = SelectedMember();
        const std::vector<C_Human *> &nearGroup = m_groups[GROUP_NEAR];
        for (size_t i = nearGroup.size(); i-- > 0;) {
            C_Human *member = nearGroup[i];
            if (member->IsAlive() && member != selected && member->GetOrderCount() == 0)
                out.push_back(member);
        }
        return;
    }
    if (group == GROUP_ALL) {
        C_Team *team = g_teamManager.GetPlayerTeam();
        C_Human *selected = team->GetSelectedMember();
        for (int i = team->GetMemberCount(); i-- > 0;) {
            C_Human *member = team->GetMember(i);
            if (member->IsAlive() && member != selected)
                out.push_back(member);
        }
        return;
    }
    out = m_groups[group];
}

bool C_SquadCommander::HasGroupMembers(int group)       // 0x00456b80
{
    if (group != GROUP_ALL)
        return !m_groups[group].empty();
    C_Team *team = g_teamManager.GetPlayerTeam();
    C_Human *selected = team->GetSelectedMember();
    for (int i = team->GetMemberCount(); i-- > 0;) {
        C_Human *member = team->GetMember(i);
        if (member->IsAlive() && member != selected)
            return true;
    }
    return false;
}

bool C_SquadCommander::IsInGroup(const C_Human *human, int group)   // 0x00456890
{
    // Fix: the stored groups only (the original reads past them for GROUP_ALL).
    if (group < 0 || group >= STORED_GROUPS)
        return false;
    const std::vector<C_Human *> &members = m_groups[group];
    for (size_t i = members.size(); i-- > 0;) {
        if (members[i] == human)
            return true;
    }
    return false;
}

void C_SquadCommander::CollectFor(std::vector<C_Human *> &out, int command, C_Actor *actor, int /*value*/)   // 0x00456c20
{
    C_Team *team = g_teamManager.GetPlayerTeam();
    C_Human *selected = team->GetSelectedMember();
    out.clear();
    if (!actor)
        return;
    if (command != HUMAN_COMMAND_LEAVE_VEHICLE) {
        // The idle members within 40 m of the selected soldier whose action is over.
        for (int i = 0; i < team->GetMemberCount(); ++i) {
            C_Human *member = team->GetMember(i);
            if (selected) {
                if (member == selected)
                    continue;
                const S_vector &from = selected->GetFrame()->GetWorldPos();
                const S_vector &at = member->GetFrame()->GetWorldPos();
                const float dx = at.x - from.x;
                const float dy = at.y - from.y;
                const float dz = at.z - from.z;
                if (!(MagnitudeBy((dz * dz + dy * dy) + dx * dx) <= 40.0f))
                    continue;
            }
            // Fix: only members the squad AI controls (the original also lists the team's
            // allied NPCs, whose controllers RemoveBusy and the commands take for squad ones).
            if (member->IsAlive() && member->GetOrderCount() == 0 && IsSquadControlled(member))
                out.push_back(member);
        }
        RemoveBusy(out);
    }
    switch (command) {
    case HUMAN_COMMAND_ENTER_VEHICLE:
        switch (actor->GetType()) {
        case ACTOR_CAR:
        case ACTOR_LADDER:
        case ACTOR_SUBMARINE:
        case ACTOR_STATIC_WEAPON:
            return;
        default:
            break;
        }
        [[fallthrough]];
    default:
        // Nobody (the original asks a picker that returns null, 0x004592d0, with the
        // members, the command, the actor and `value`).
        out.clear();
        return;
    case HUMAN_COMMAND_LEAVE_VEHICLE:
        // The squad's soldiers in the vehicle's seats.
        for (int seat = actor->GetSeatCount(); seat-- > 0;) {
            C_Human *occupant = static_cast<C_Human *>(actor->GetSeatOccupant(seat));
            if (occupant && occupant->IsAlive() && occupant != selected && occupant->GetRole() == HUMAN_ROLE_PLAYER)
                out.push_back(occupant);
        }
        return;
    case MANOEUVRE_THROUGH_DOOR:
    case SQUAD_ACTION_SEARCH_BODY:
        return;
    }
}

void C_SquadCommander::RemoveBusy(std::vector<C_Human *> &members)   // 0x004592e0
{
    for (size_t i = members.size(); i-- > 0;) {
        const S_SquadAction &action = SquadOf(members[i])->m_action;
        if (!action.finished && !action.completed) {
            members[i] = members.back();
            members.pop_back();
        }
    }
}

// --------------------------------------------------------------------- the squad's members

void C_SquadCommander::OnFriendlyFire(C_Actor *attacker)   // 0x00459500
{
    // (`attacker` is a human: the squad controller's TakeHit checks.)
    const int target = static_cast<C_Human *>(attacker)->GetSightTargetId();
    m_squadTarget = attacker;
    m_flag40 = false;
    C_Team *team = g_teamManager.GetPlayerTeam();
    for (int i = team->GetMemberCount(); i-- > 0;) {
        C_Human *member = team->GetMember(i);
        if (!member->IsAlive() || member == attacker)
            continue;
        if (!g_sightModule.TestNow(member->GetSightId(), target))
            continue;
        if (member->GetOrderCount() != 0)
            member->CancelOrders();
        // Fix: only a squad controller's action (see CollectFor).
        if (IsSquadControlled(member) && !SquadOf(member)->m_action.finished)
            SquadOf(member)->DropAction();
    }
    m_squadTargetTime = 4000;
}

void C_SquadCommander::DelayFormationChecks()           // 0x00459490
{
    C_Team *team = g_teamManager.GetPlayerTeam();
    C_Human *selected = team->GetSelectedMember();
    for (int i = team->GetMemberCount(); i-- > 0;) {
        C_Human *member = team->GetMember(i);
        // Fix: squad controllers only (the original writes the field into any member's
        // controller: the team's allied NPCs have others).
        if (!member->IsAlive() || member == selected || !IsSquadControlled(member))
            continue;
        C_SquadController *squad = SquadOf(member);
        if (squad->m_formationTime == 0)
            squad->m_formationTime = 2200;
    }
}

void C_SquadCommander::OnPlayerNoise()                  // 0x00459360
{
    C_Team *team = g_teamManager.GetPlayerTeam();
    C_Human *selected = team->GetSelectedMember();
    for (int i = team->GetMemberCount(); i-- > 0;) {
        C_Human *member = team->GetMember(i);
        // Fix: squad controllers only (see DelayFormationChecks).
        if (!member->IsAlive() || member == selected || !IsSquadControlled(member))
            continue;
        C_SquadController *squad = SquadOf(member);
        if (squad->m_combatMode == 0) {
            squad->m_combatMode = 1;
            squad->UpdateModeIcon();
        }
    }
}

void C_SquadCommander::ReleaseFollowers(C_Human *leader)   // 0x004593e0
{
    C_Team *team = g_teamManager.GetPlayerTeam();
    for (int i = 0; i < team->GetMemberCount(); ++i) {
        C_Human *member = team->GetMember(i);
        if (member && member->GetLeader() == leader && IsSquadControlled(member)) {
            C_SquadController *squad = SquadOf(member);
            squad->SetFollow(false, squad->m_quiet);
            member->SetLeader(nullptr);
        }
    }
}

// ------------------------------------------------------------------------ the HUD's commands

void C_SquadCommander::ApplyOrder(C_Human *member, const S_ActionCommand &command)   // 0x00455d40
{
    C_Human *selected = SelectedMember();
    C_SquadController *squad = SquadOf(member);
    const bool idle = squad->GetOrderCount() == 0;
    const int action = command.action;
    switch (action) {
    case SQUAD_COMMAND_FOLLOW:
    case SQUAD_COMMAND_FOLLOW_QUIET:
        if (idle) {
            member->SetLeader(selected);
            squad->SetFollow(true, action == SQUAD_COMMAND_FOLLOW_QUIET);
        }
        return;
    case SQUAD_COMMAND_STOP:
    case SQUAD_COMMAND_STOP_QUIET:
        if (idle) {
            squad->DropAction();
            squad->m_quiet = action == SQUAD_COMMAND_STOP_QUIET;
            squad->FinishAction(SQUAD_ACTION_FOLLOW);
        }
        return;
    case SQUAD_COMMAND_BOUND:
    case SQUAD_COMMAND_BOUND_QUIET:
        if (idle)
            squad->Bound(command.dir, action == SQUAD_COMMAND_BOUND_QUIET);
        return;
    case SQUAD_COMMAND_HOLD:
    case SQUAD_COMMAND_HOLD_QUIET:
        if (idle)
            squad->HoldPosition(action == SQUAD_COMMAND_HOLD_QUIET, true);
        return;
    case SQUAD_ORDER_MODE0:
    case SQUAD_ORDER_MODE0_ALT:
    case SQUAD_ORDER_MODE2:
    case SQUAD_ORDER_MODE2_ALT:
    case SQUAD_ORDER_MODE1:
    case SQUAD_ORDER_MODE1_ALT:
        if (idle) {
            squad->SetCombatOrder(action);
            squad->NotifyIfIdle();
        }
        return;
    case SQUAD_COMMAND_ENGAGE:
        if (idle) {
            squad->Engage(true, command.dir);
            squad->NotifyIfIdle();
        }
        return;
    case SQUAD_COMMAND_THROW:
        if (idle)
            squad->ThrowAt(command.dir);
        return;
    case SQUAD_COMMAND_FIRE_LAUNCHER:
        if (idle)
            squad->FireLauncherAt(command.dir);
        return;
    case SQUAD_COMMAND_FOLLOW_ME:
        if (idle)
            squad->FollowSelected(false);
        return;
    // The order queue's (any member).
    case SQUAD_COMMAND_END_ORDER_6:
        squad->EndOrderKind6();
        return;
    case SQUAD_COMMAND_END_ORDER_7:
        squad->EndOrderKind7();
        return;
    case SQUAD_COMMAND_HOLD_ORDERS:
        squad->HoldOrders(true);
        return;
    case SQUAD_COMMAND_RESUME_ORDERS:
        squad->HoldOrders(false);
        return;
    case SQUAD_COMMAND_CANCEL_ORDERS:
        squad->CancelOrders(true);
        squad->DropAction();
        squad->SetFollow(false, false);
        return;
    case SQUAD_COMMAND_FOLLOW_ME_ORDER:
        squad->FollowSelected(true);
        if (member->GetLeader())
            squad->SetFollow(true, squad->m_quiet);
        return;
    default:
        return;
    }
}

void C_SquadCommander::ExecuteActionCommand(const S_ActionCommand &command)   // 0x00456020
{
    C_Human *selected = SelectedMember();
    const int action = command.action;
    switch (action) {
    case SQUAD_COMMAND_TAKE_COVER:
        TakeCover(command, false);
        return;
    case SQUAD_COMMAND_TAKE_COVER_QUIET:
        TakeCover(command, true);
        return;
    case SQUAD_COMMAND_MOVE_NEAR:
        MoveNear(command, false);
        return;
    case SQUAD_COMMAND_MOVE_NEAR_QUIET:
        MoveNear(command, true);
        return;
    case SQUAD_COMMAND_FORMATION_0:
    case SQUAD_COMMAND_FORMATION_1:
    case SQUAD_COMMAND_FORMATION_FOLLOW:
        if (selected && selected->GetFormation()) {
            const int type = action == SQUAD_COMMAND_FORMATION_0 ? 0 : action == SQUAD_COMMAND_FORMATION_1 ? 1 : FORMATION_FOLLOW;
            selected->GetFormation()->SetType(static_cast<E_FormationType>(type));
            selected->GetFormation()->SetScale(XYZ(m_formationScale));
        }
        return;
    case SQUAD_COMMAND_SPACING_LESS:
    case SQUAD_COMMAND_SPACING_MORE:
        if (action == SQUAD_COMMAND_SPACING_LESS ? m_spacing > 0 : m_spacing < MAX_SPACING) {
            m_spacing += action == SQUAD_COMMAND_SPACING_LESS ? -1 : 1;
            UpdateScale();
        }
        if (selected && selected->GetFormation()) {
            C_FormationShape *formation = selected->GetFormation();
            formation->SetScale(XYZ(m_formationScale));
            formation->SetType(formation->GetType());
        }
        return;
    case SQUAD_COMMAND_END_ORDER_6:
    case SQUAD_COMMAND_END_ORDER_7:
    case SQUAD_COMMAND_HOLD_ORDERS:
    case SQUAD_COMMAND_RESUME_ORDERS:
    case SQUAD_COMMAND_CANCEL_ORDERS:
    case SQUAD_COMMAND_FOLLOW_ME_ORDER: {
        // The members with orders.
        std::vector<C_Human *> members;
        if (command.member)
            members.push_back(command.member);
        else
            CollectGroup(members, GROUP_NEAR);
        for (size_t i = members.size(); i-- > 0;) {
            if (members[i]->GetOrderCount() != 0)
                ApplyOrder(members[i], command);
        }
        return;
    }
    case SQUAD_COMMAND_ON_ACTOR:
        ExecuteCommand(command.command, command.actor, command.member, false, command.value);
        return;
    case SQUAD_COMMAND_ON_ACTOR_QUIET:
        ExecuteCommand(command.command, command.actor, command.member, true, command.value);
        return;
    default: {
        // The squad's combat orders are also the player's soldier's (for when it is handed to
        // the squad AI).
        if (!command.member && action > SQUAD_COMMAND_MOVE_NEAR_QUIET && action < SQUAD_COMMAND_ENGAGE && selected
            && selected->GetControllerType() == HUMAN_CONTROLLER_PLAYER)
            static_cast<C_PlayerController *>(selected->GetController())->SetSquadCombatOrder(action);
        // The members without orders.
        std::vector<C_Human *> members;
        if (command.member)
            members.push_back(command.member);
        else
            CollectGroup(members, GROUP_IDLE);
        for (size_t i = members.size(); i-- > 0;) {
            if (members[i]->GetOrderCount() == 0)
                ApplyOrder(members[i], command);
        }
        return;
    }
    }
}

void C_SquadCommander::TakeCover(const S_ActionCommand &command, bool quiet)   // 0x00459740
{
    C_Human *selected = SelectedMember();
    std::vector<C_Human *> members;
    if (command.member)
        members.push_back(command.member);
    else
        CollectGroup(members, GROUP_IDLE);
    if (members.empty())
        return;
    // Fix: no selected soldier (the original reads its frame through null).
    if (!selected)
        return;

    // The wedge of slots 2 m ahead of the selected soldier, facing the direction (flat).
    S_vector dir(command.dir.x, 0.0f, command.dir.z);
    dir.Normalize();
    const S_vector &own = selected->GetFrame()->GetWorldPos();
    S_matrix frame = IdentityMatrix();
    frame.SetDir(dir);
    frame(3) = (dir + dir) + own;
    const S_vector center = frame(3);
    // Fix: five slots at most (the original's tables and arrays have five; more members ran
    // past them).
    const size_t slotCount = std::min<size_t>(members.size(), COVER_SLOT_COUNT);
    S_vector slots[COVER_SLOT_COUNT];
    for (size_t k = slotCount; k-- > 0;)
        slots[k] = XYZ(COVER_SLOTS[k]) * frame;

    struct S_Slot {
        S_Checkpoint *point;
        float square;                                   // to the slot
    };
    S_Slot best[COVER_SLOT_COUNT];
    for (S_Slot &entry : best)
        entry = {nullptr, 10000.0f};
    std::vector<S_Checkpoint *> spares;
    C_Checkpoints *network = GetGame()->GetCheckpoints();
    I3D_sector *sector = GetGame()->GetScene()->GetSector(own);
    // The ground the covers must be on (2 m up or down; Fix: none within 10 m of the wedge,
    // which the original reads through null: no cover).
    const S_Checkpoint *ground = network->Get(network->FindNearest(center, 10.0f, -1.0f));

    // The cover points within 20 m: each to the slot nearest to it; a farther one, or the one
    // it displaces, is a spare.
    std::vector<uint16_t> points;
    network->FindCoverWithin(center, 20.0f, &points);
    for (size_t i = points.size(); i-- > 0;) {
        S_Checkpoint *point = network->Get(points[i]);
        if (!point)
            continue;
        // A low point (its first value of set 0 under 0.2), or one that gives cover lying
        // down against the direction.
        float low = 0.0f;
        float unused = 0.0f;
        const bool lowPoint = point->GetValuePair(0, &low, &unused) && !(0.2f <= low);
        if (!lowPoint) {
            if (!point->GetCoverMask(MOVE_STANCE_MASK))
                continue;
            if (!(point->GetCoverStances(S_Checkpoint::GetDirectionBit(dir), MOVE_STANCE_MASK) & MOVE_LYING))
                continue;
        }
        const S_vector at = point->pos;
        if (GetGame()->GetScene()->GetSector(at) != sector)
            continue;
        const float r = static_cast<float>(point->radius) * 0.0625f;
        if (!lowPoint) {
            // Something to hide behind: a ray from 0.3 m up across the point and 0.7 m on
            // hits a surface that is not too flat.
            const S_vector from(at.x, at.y + 0.3f, at.z);
            const S_vector ray(dir.x * 0.7f + dir.x * r, dir.y * r + dir.y * 0.7f, dir.z * r + dir.z * 0.7f);
            S_vector normal;
            if (!g_collisionWorld.RayTest(from, ray, nullptr, &normal, 0xFFFFFFFF, 0x10000) || normal.y > 0.6f)
                continue;
        }
        const S_vector cover(at.x + dir.x * r, r * dir.y + at.y, at.z + dir.z * r);
        if (!ground)
            continue;
        const float rise = ground->pos.y - cover.y;
        if (!(rise <= 2.0f && -2.0f <= rise))
            continue;
        int slot = -1;
        float nearest = 10000.0f;
        for (size_t k = slotCount; k-- > 0;) {
            const float dx = slots[k].x - cover.x;
            const float dy = slots[k].y - cover.y;
            const float dz = slots[k].z - cover.z;
            const float square = (dx * dx + dy * dy) + dz * dz;
            if (square < nearest) {
                nearest = square;
                slot = static_cast<int>(k);
            }
        }
        // Fix: no slot within 100 m (the original writes before its array).
        if (slot < 0)
            continue;
        S_Slot &entry = best[slot];
        if (!entry.point) {
            entry = {point, nearest};
        } else if (entry.square <= nearest) {
            spares.push_back(point);
        } else {
            spares.push_back(entry.point);
            entry = {point, nearest};
        }
    }

    std::vector<S_vector> covers;
    for (size_t k = slotCount; k-- > 0;) {
        if (const S_Checkpoint *point = best[k].point) {
            const float r = static_cast<float>(point->radius) * 0.0625f;
            covers.push_back(S_vector(point->pos.x + dir.x * r, point->pos.y + dir.y * r, point->pos.z + dir.z * r));
        }
    }
    if (2 * covers.size() <= members.size()) {
        // Covers for half of the members at most: nobody takes cover.
        for (size_t k = slotCount; k-- > 0;)
            best[k].point = nullptr;
    } else {
        // The spares within 2 m of a cover go.
        for (size_t c = covers.size(); c-- > 0;) {
            for (size_t s = spares.size(); s-- > 0;) {
                const S_vector d = spares[s]->pos - covers[c];
                if ((d.z * d.z + d.y * d.y) + d.x * d.x < 4.0f)
                    spares.erase(spares.begin() + s);
            }
        }
        // The empty slots take the spare nearest to them (100 m).
        if (!spares.empty()) {
            for (size_t k = slotCount; k-- > 0;) {
                if (best[k].point)
                    continue;
                int index = -1;
                float nearest = 10000.0f;
                for (size_t s = spares.size(); s-- > 0;) {
                    const S_vector d = slots[k] - spares[s]->pos;
                    const float square = (d.x * d.x + d.z * d.z) + d.y * d.y;
                    if (square < nearest) {
                        nearest = square;
                        index = static_cast<int>(s);
                    }
                }
                // Fix: none within 100 m (the original takes the word before the list).
                if (index < 0)
                    continue;
                best[k] = {spares[index], nearest};
                spares.erase(spares.begin() + index);
                if (spares.empty())
                    break;
            }
        }
        // The slots still empty: the walking point nearest to them (10 m) in the sector.
        for (size_t k = slotCount; k-- > 0;) {
            if (best[k].point)
                continue;
            S_Checkpoint *point = network->Get(network->FindNearest(slots[k], 10.0f, -1.0f));
            // Fix: none within 10 m (the original reads through null).
            if (point && GetGame()->GetScene()->GetSector(point->pos) == sector)
                best[k].point = point;
        }
    }

    // Each slot's place to the member nearest to it (100 m), facing 30 m along the direction
    // or along the cover; low places lying down unless the point covers lying that way.
    const S_vector aim(center.x + dir.x * 30.0f, center.y + dir.y * 30.0f, center.z + dir.z * 30.0f);
    for (size_t k = 0; k < slotCount; ++k) {
        const S_Checkpoint *point = best[k].point;
        if (!point)
            continue;
        int index = -1;
        float nearest = 10000.0f;
        for (size_t m = members.size(); m-- > 0;) {
            const S_vector &at = members[m]->GetFrame()->GetWorldPos();
            const float dx = point->pos.x - at.x;
            const float dy = point->pos.y - at.y;
            const float dz = point->pos.z - at.z;
            const float square = (dx * dx + dy * dy) + dz * dz;
            if (square < nearest) {
                nearest = square;
                index = static_cast<int>(m);
            }
        }
        // Fix: nobody within 100 m (the original orders the word before the list).
        if (index < 0)
            continue;
        const float r = static_cast<float>(point->radius) * 0.0625f;
        const S_vector4 pos = {dir.x * r + point->pos.x, r * dir.y + point->pos.y, point->pos.z + dir.z * r, 0.0f};
        S_vector face;
        const uint8_t standing = point->GetCoverMask(MOVE_STANDING);
        const uint8_t bit = S_Checkpoint::GetDirectionBit(dir);
        if (!(standing & bit)) {
            face = aim - XYZ(pos);
        } else {
            // It covers standing that way: turn bit by bit, away from the wedge's middle line,
            // to the first direction it does not cover.
            uint8_t bits = bit;
            do {
                const float side = ((pos.x - center.x) * dir.z + (pos.z - center.z) * -dir.x) + (pos.y - center.y) * 0.0f;
                if (side <= 0.0f)
                    bits = bits == 0x80 ? 1 : static_cast<uint8_t>(bits << 1);
                else
                    bits = bits == 1 ? 0x80 : static_cast<uint8_t>(bits >> 1);
            } while ((standing & bits) && bits != bit);
            face = XYZ(*S_Checkpoint::GetDirectionVector(bits));
        }
        face.Normalize();
        float low = 0.0f;
        float unused = 0.0f;
        bool lie = point->GetValuePair(0, &low, &unused) && !(0.2f <= low);
        if (point->GetCoverMask(MOVE_STANCE_MASK)
            && (point->GetCoverStances(S_Checkpoint::GetDirectionBit(dir), MOVE_STANCE_MASK) & MOVE_LYING))
            lie = false;
        SquadOf(members[index])->MoveTo(pos, ToVector4(face, 0.0f), quiet, lie ? MOVE_LYING : MOVE_CROUCHING);
        members.erase(members.begin() + index);
    }
    // The others stay where they are, facing that way.
    for (size_t m = members.size(); m-- > 0;) {
        const S_vector4 pos = WorldPos4Of(members[m]->GetFrame());
        S_vector face(aim.x - pos.x, aim.y - pos.y, aim.z - pos.z);
        face.Normalize();
        SquadOf(members[m])->MoveTo(pos, ToVector4(face, 0.0f), quiet, std::rand() % 3 ? MOVE_STANDING : MOVE_CROUCHING);
    }
}

void C_SquadCommander::MoveNear(const S_ActionCommand &command, bool quiet)   // 0x0045b890
{
    C_Human *selected = SelectedMember();
    std::vector<C_Human *> members;
    if (command.member)
        members.push_back(command.member);
    else
        CollectGroup(members, GROUP_IDLE);
    if (members.empty())
        return;
    // Fix: no selected soldier (the original reads its view through null).
    if (!selected)
        return;
    // The places around the point in the frame of the selected soldier's view.
    S_matrix frame = IdentityMatrix();
    frame.SetDir(selected->GetViewDir());
    frame(3) = XYZ(command.dir);
    // Out of the static guns; into the driver's seat of a vehicle nobody drives.
    for (C_Human *member : members) {
        C_Actor *vehicle = member->GetVehicle();
        if (!vehicle)
            continue;
        if (vehicle->GetType() == ACTOR_STATIC_WEAPON)
            member->IssueCommand(MakeCommand(HUMAN_COMMAND_LEAVE_VEHICLE, 0, 0));
        else if (!vehicle->GetSeatOccupant(0))
            member->IssueCommand(MakeCommand(HUMAN_COMMAND_ENTER_VEHICLE, vehicle->GetNetId(), 0));
    }
    // The members on foot and the drivers go.
    size_t place = 0;
    for (C_Human *member : members) {
        if (member->GetVehicle() && member->GetVehicleSeat() != 0)
            continue;
        // Fix: not past the third table (the original goes on into the idle reports).
        const S_vector at = XYZ(PLACES[std::min<size_t>(place++, 15)]) * frame;
        // (A debug hook, empty in the release build, marked the place.)
        SquadOf(member)->MoveNear(ToVector4(at, 0.0f), quiet);
    }
}

// ------------------------------------------------------------------- commands on actors

void C_SquadCommander::ExecuteCommand(int command, C_Actor *actor, C_Human *member, bool quiet, int value)   // 0x00458e50
{
    std::vector<C_Human *> members;
    if (member)
        members.push_back(member);
    else
        CollectGroup(members, GROUP_IDLE);
    C_Human *first = members.empty() ? nullptr : members[0];
    FilterMembers(members, command, actor, value);
    if (members.empty()) {
        // Nobody can: the first of them says so.
        if (first)
            SquadOf(first)->SayReply(SENTENCE_CANNOT, true, 300, false);
        return;
    }
    if (command == MANOEUVRE_GRENADE_ROOM || command == MANOEUVRE_THROUGH_DOOR
        || (command == MANOEUVRE_LADDER && actor->GetType() == ACTOR_LADDER)) {
        StartManoeuvre(command, actor, quiet, members);
        return;
    }
    for (size_t i = members.size(); i-- > 0;) {
        C_SquadController *squad = SquadOf(members[i]);
        squad->m_quiet = quiet;
        switch (command) {
        case HUMAN_COMMAND_LOCK_DOOR:
            squad->LockDoor(actor);
            break;
        case HUMAN_COMMAND_UNLOCK_DOOR:
            squad->UnlockDoor(actor);
            break;
        case HUMAN_COMMAND_DEFUSE_MINE:
            squad->DefuseMine(actor);
            break;
        case HUMAN_COMMAND_ENTER_VEHICLE:
            squad->UseActor(actor);
            break;
        case HUMAN_COMMAND_PLANT_EXPLOSIVE:
            squad->PlantExplosive(actor);
            break;
        case HUMAN_COMMAND_LEAVE_VEHICLE:
            squad->OrderLeaveVehicle(actor);
            break;
        case HUMAN_COMMAND_PICK_UP_BODY:
            squad->PickUp(actor, value);
            break;
        case 0x08:                                      // into a seat (the command's number)
        case 0x09:
        case 0x0A:
        case 0x0B:
        case 0x0C:
        case 0x0D:
            squad->EnterVehicle(actor, command, actor->GetPosition4());
            break;
        case HUMAN_COMMAND_PICK_LOCK:
            squad->PickLock(actor);
            break;
        case HUMAN_COMMAND_HEAL:
            squad->Heal(actor);
            break;
        case HUMAN_COMMAND_CUT_WIRE:
            squad->CutWire(actor);
            break;
        case HUMAN_COMMAND_TIE_UP:
            squad->TieUp(actor);
            break;
        case SQUAD_ACTION_ATTACK_VEHICLE:
            squad->AttackVehicle(actor);
            break;
        case SQUAD_ACTION_SEARCH_BODY:
            squad->SearchBody(actor);
            break;
        case SQUAD_ACTION_SWAP_UNIFORM:
            squad->SwapUniform(actor);
            break;
        case SQUAD_ACTION_ATTACK:
            if (quiet)
                squad->AttackSilently(actor);
            else
                squad->Attack(actor);
            break;
        case SQUAD_ACTION_ATTACK_SILENTLY:
            squad->AttackSilently(actor);
            break;
        case HUMAN_COMMAND_OPEN_DOOR:
            squad->OpenDoor(actor);
            break;
        case HUMAN_COMMAND_CLOSE_DOOR:
            squad->CloseDoor(actor);
            break;
        case SQUAD_ACTION_FOLLOW:
            squad->Follow(static_cast<C_Human *>(actor));
            break;
        case SQUAD_ACTION_21:
            squad->Act21(actor);
            break;
        case HUMAN_COMMAND_DELIVER_DOCUMENTS:
            squad->DeliverDocuments(actor);
            break;
        case SQUAD_ACTION_THROW:
            squad->ThrowGrenadeAt(actor);
            break;
        case HUMAN_COMMAND_DISARM_EXPLOSIVE:
            squad->DisarmExplosive(actor);
            break;
        default:
            break;
        }
    }
}

void C_SquadCommander::FilterMembers(std::vector<C_Human *> &members, int command, C_Actor *actor, int value)   // 0x0045bb20
{
    switch (command) {
    case HUMAN_COMMAND_ENTER_VEHICLE:
        // A ladder: all of them climb.
        if (actor->GetType() == ACTOR_LADDER)
            return;
        // A car: those not in it yet. (The next member after one removed is not looked at:
        // kept.)
        if (actor->GetType() == ACTOR_CAR) {
            for (size_t i = 0; i < members.size(); ++i) {
                if (members[i]->GetVehicle() == actor)
                    members.erase(members.begin() + i);
            }
            return;
        }
        [[fallthrough]];
    case HUMAN_COMMAND_PICK_UP_BODY:
        // A supply box without item lists (ammunition for all): all of them.
        if (actor->GetType() == ACTOR_SUPPLY_BOX && !static_cast<C_SupplyBox *>(actor)->HasItemLists())
            return;
        break;
    case HUMAN_COMMAND_LEAVE_VEHICLE:
        // Those carrying the body / in the vehicle.
        for (size_t i = members.size(); i-- > 0;) {
            const C_Human *human = members[i];
            const bool keep = actor->GetType() == ACTOR_HUMAN ? human->GetCarried() == actor : human->GetVehicle() == actor;
            if (!keep)
                members.erase(members.begin() + i);
        }
        return;
    case MANOEUVRE_THROUGH_DOOR:
    case SQUAD_ACTION_SEARCH_BODY:
    case SQUAD_ACTION_FOLLOW:
        return;
    default:
        break;
    }
    C_Human *picked = PickMember(members, command, actor, value);
    members.clear();
    if (picked)
        members.push_back(picked);
}

void C_SquadCommander::SortByDistance(std::vector<C_Human *> &members, const S_vector &pos)   // 0x00459180
{
    std::vector<float> squares;
    for (C_Human *member : members) {
        const S_vector &at = member->GetFrame()->GetWorldPos();
        const float dx = pos.x - at.x;
        const float dy = pos.y - at.y;
        const float dz = pos.z - at.z;
        squares.push_back((dz * dz + dy * dy) + dx * dx);
    }
    // A selection sort that swaps the members but not their distances: the nearest comes
    // first, the order after it is off once a swap happened (kept).
    for (size_t i = 0; i < members.size(); ++i) {
        float nearest = squares[i];
        size_t index = i;
        for (size_t j = i; j < members.size(); ++j) {
            if (squares[j] < nearest) {
                nearest = squares[j];
                index = j;
            }
        }
        std::swap(members[i], members[index]);
    }
}

C_Human *C_SquadCommander::PickMember(std::vector<C_Human *> &members, int command, C_Actor *actor, int /*value*/)   // 0x0045bd10
{
    if (members.empty())
        return nullptr;
    SortByDistance(members, actor->GetFrame()->GetWorldPos());
    switch (command) {
    case HUMAN_COMMAND_LOCK_DOOR:
    case HUMAN_COMMAND_UNLOCK_DOOR: {
        // The one with the key.
        const int key = static_cast<C_Door *>(actor)->GetLockKey();
        for (C_Human *member : members) {
            if (member->FindItem(key))
                return member;
        }
        return nullptr;
    }
    case HUMAN_COMMAND_DEFUSE_MINE:
    case HUMAN_COMMAND_LEAVE_VEHICLE:
    case 0x08:
    case 0x09:
    case 0x0A:
    case 0x0B:
    case 0x0C:
    case 0x0D:
    case HUMAN_COMMAND_PICK_LOCK:
    case HUMAN_COMMAND_TIE_UP:
    case HUMAN_COMMAND_OPEN_DOOR:
    case HUMAN_COMMAND_CLOSE_DOOR:
    case SQUAD_ACTION_21:
    case HUMAN_COMMAND_DISARM_EXPLOSIVE:
        return members[0];
    case HUMAN_COMMAND_ENTER_VEHICLE:
        // Not a static gun somebody mans.
        if (actor->GetType() != ACTOR_STATIC_WEAPON || !actor->GetSeatOccupant(0))
            return members[0];
        return nullptr;
    case HUMAN_COMMAND_PLANT_EXPLOSIVE: {
        const int explosive = static_cast<C_ExplosivePlace *>(actor)->GetItemId();
        for (C_Human *member : members) {
            if (member->FindItem(explosive))
                return member;
        }
        return nullptr;
    }
    case HUMAN_COMMAND_PICK_UP_BODY:
        switch (actor->GetType()) {
        case ACTOR_HUMAN:
            // One not carrying a body already.
            for (C_Human *member : members) {
                if (!member->GetCarried())
                    return member;
            }
            return nullptr;
        case ACTOR_ITEM:
            // One with room for the item.
            for (C_Human *member : members) {
                if (g_itemsModule.CanPutItem(member, static_cast<C_ItemActor *>(actor)->GetItem()) != 0)
                    return member;
            }
            return nullptr;
        case ACTOR_SUPPLY_BOX:
            return members[0];
        default:
            return nullptr;
        }
    case HUMAN_COMMAND_HEAL:
        // Not the patient; one with a medkit.
        for (C_Human *member : members) {
            if (member != actor && member->GetMedkit())
                return member;
        }
        return nullptr;
    case HUMAN_COMMAND_CUT_WIRE:
        for (C_Human *member : members) {
            if (member->FindItem(ITEM_WIRE_CUTTERS))
                return member;
        }
        return nullptr;
    case MANOEUVRE_GRENADE_ROOM:
        // One with a grenade on the belt.
        for (C_Human *member : members) {
            if (member->FindBeltItem(ITEM_BEHAVIOUR_THROWN))
                return member;
        }
        return nullptr;
    case SQUAD_ACTION_ATTACK_VEHICLE:
        // An armoured target wants a projectile seat weapon or a launcher.
        for (C_Human *member : members) {
            if (!IsArmoured(actor))
                return member;
            C_SquadController *squad = SquadOf(member);
            if (squad->HasSeatWeapon() && squad->HasProjectileSeatWeapon())
                return member;
            int container = 0;
            if (squad->FindWeapon(ITEM_BEHAVIOUR_LAUNCHER, false, false, &container))
                return member;
        }
        return nullptr;
    case SQUAD_ACTION_SWAP_UNIFORM: {
        // One without a uniform, or with one of the other side.
        const C_Item *uniform = actor->GetContainer(ITEM_CONTAINER_UNIFORM)->GetItem();
        if (!uniform)
            return nullptr;
        const bool axis = IsAxisUniform(uniform);
        for (C_Human *member : members) {
            const C_Item *own = member->GetContainer(ITEM_CONTAINER_UNIFORM)->GetItem();
            if (!own || IsAxisUniform(own) != axis)
                return member;
        }
        return nullptr;
    }
    case SQUAD_ACTION_ATTACK:
    case SQUAD_ACTION_ATTACK_SILENTLY:
    case SQUAD_ACTION_THROW:
        // A human target: one that saw it or was seen by it first; then anybody. Silently:
        // with a silenced weapon.
        if (actor->GetType() == ACTOR_HUMAN) {
            const C_Human *target = static_cast<C_Human *>(actor);
            for (C_Human *member : members) {
                if ((member->HasSeenEnemy(target) || target->HasSeenEnemy(member))
                    && (command != SQUAD_ACTION_ATTACK_SILENTLY || HasSilencedWeapon(member)))
                    return member;
            }
        }
        for (C_Human *member : members) {
            if (command != SQUAD_ACTION_ATTACK_SILENTLY || HasSilencedWeapon(member))
                return member;
        }
        return nullptr;
    case HUMAN_COMMAND_DELIVER_DOCUMENTS: {
        const int documents = static_cast<C_DocumentPlace *>(actor)->GetItemId();
        for (C_Human *member : members) {
            if (member->FindItem(documents))
                return member;
        }
        return nullptr;
    }
    default:
        return nullptr;
    }
}

// --------------------------------------------------------------------------- manoeuvres

void C_SquadCommander::StartManoeuvre(int kind, C_Actor *actor, bool quiet, const std::vector<C_Human *> &members)   // 0x00457820
{
    m_manoeuvreMembers = members;
    if (m_manoeuvreMembers.empty())
        return;
    m_manoeuvre = kind;
    C_Checkpoints *network = GetGame()->GetCheckpoints();
    std::vector<uint16_t> points;
    if (kind != MANOEUVRE_LADDER) {
        // A door: one member throws, two go through.
        if (kind == MANOEUVRE_GRENADE_ROOM) {
            if (m_manoeuvreMembers.size() > 1)
                m_manoeuvreMembers.resize(1);
        } else if (kind != MANOEUVRE_THROUGH_DOOR) {
            return;
        }
        if (m_manoeuvreMembers.size() > 2)
            m_manoeuvreMembers.resize(2);
        m_stage = 0;
        m_waiting = static_cast<int>(m_manoeuvreMembers.size());
        m_manoeuvreActor = actor;
        // Its walking points on either side (without the two the members wait for nothing).
        network->GetActorPoints(actor, points);
        if (points.size() != 2)
            return;
        const S_vector p0 = network->Get(points[0])->pos;
        const S_vector p1 = network->Get(points[1])->pos;
        const S_vector viewer = ViewerPos();
        const bool second = !((viewer - p0).Square() < (viewer - p1).Square());
        const S_vector &nearPos = second ? p1 : p0;
        const S_vector &farPos = second ? p0 : p1;
        // `way`: from the far side to the near one; `side` across it.
        S_vector way = nearPos - farPos;
        NormalizeBy(way, (way.x * way.x + way.z * way.z) + way.y * way.y);
        S_vector side = way.Cross(UP);
        NormalizeBy(side, (side.z * side.z + side.y * side.y) + side.x * side.x);
        m_doorPos = ToVector4(farPos, 0.0f);
        m_doorDir = ToVector4(-way, 0.0f);
        if (kind == MANOEUVRE_THROUGH_DOOR) {
            // Beside the door on the near side.
            for (size_t i = m_manoeuvreMembers.size(); i-- > 0;) {
                C_SquadController *squad = SquadOf(m_manoeuvreMembers[i]);
                squad->m_quiet = quiet;
                S_vector to;
                if (i == 0)
                    to = nearPos + way;
                else if (i == 1)
                    to = (side + way) * 1.5f + nearPos;
                else if (i == 2)
                    to = (way - side) * 1.5f + nearPos;
                else
                    continue;
                squad->OrderMove(ToVector4(to, 0.0f), 0, true);
            }
        } else {
            m_waiting = std::min(m_waiting, 2);
            for (size_t i = m_manoeuvreMembers.size(); i-- > 0;) {
                C_SquadController *squad = SquadOf(m_manoeuvreMembers[i]);
                squad->m_quiet = quiet;
                if (i == 0) {
                    // Close to the door, a little aside.
                    squad->OrderMove(ToVector4(side * 0.6f + (way * 0.5f + nearPos), 0.0f), 0, true);
                    squad->m_action.radius = 0.2f;
                } else if (i == 1) {
                    squad->OrderMove(ToVector4(nearPos + way * 1.5f, 0.0f), 0, true);
                }
            }
        }
        return;
    }

    // A ladder: to the foot of it, then up.
    m_stage = 0;
    m_waiting = static_cast<int>(m_manoeuvreMembers.size());
    network->GetActorPoints(actor, points);
    // Fix: a ladder without its two walking points (the original reads them anyway): no
    // manoeuvre.
    if (points.size() < 2) {
        m_manoeuvre = MANOEUVRE_NONE;
        return;
    }
    const S_vector p0 = network->Get(points[0])->pos;
    const S_vector p1 = network->Get(points[1])->pos;
    const S_vector viewer = ViewerPos();
    const bool second = !((viewer - p0).Square() < (viewer - p1).Square());
    const S_vector &nearPos = second ? p1 : p0;
    const S_vector &farPos = second ? p0 : p1;
    const S_vector diff = nearPos - farPos;
    S_vector way(diff.x, 0.0f, diff.z);
    way.Normalize();
    S_vector side = way.Cross(UP);
    NormalizeBy(side, (side.z * side.z + side.y * side.y) + side.x * side.x);
    m_ladderDir = ToVector4(-way, 0.0f);
    m_ladderPos = ToVector4(farPos, 0.0f);
    // (Only three members get a place; with more the manoeuvre waits for ever: kept.)
    for (size_t i = m_manoeuvreMembers.size(); i-- > 0;) {
        C_SquadController *squad = SquadOf(m_manoeuvreMembers[i]);
        squad->m_quiet = quiet;
        S_vector to;
        if (i == 0)
            to = side * 1.6f + (way * 1.5f + nearPos);
        else if (i == 1)
            to = way * 2.5f + nearPos;
        else if (i == 2)
            to = (way - side) * 2.5f + nearPos;
        else
            continue;
        squad->OrderMove(ToVector4(to, 0.0f), 0, true);
    }
}

void C_SquadCommander::OnOrderMoveDone()                // 0x00456f00
{
    switch (m_manoeuvre) {
    case MANOEUVRE_LADDER: {
        if (m_stage != 0 || --m_waiting != 0)
            return;
        // All are at the foot: up and on beyond the top, one after the other.
        m_stage = 1;
        const S_vector &dir = XYZ(m_ladderDir);
        const S_vector &pos = XYZ(m_ladderPos);
        const S_vector side = dir.Cross(UP);
        for (size_t i = m_manoeuvreMembers.size(); i-- > 0;) {
            C_SquadController *squad = SquadOf(m_manoeuvreMembers[i]);
            if (i == 0) {
                squad->OrderMove(ToVector4((dir + dir) + pos, 0.0f), 0, false);
            } else if (i == 1) {
                const S_vector step = side + dir;
                squad->OrderMove(ToVector4((step + step) + pos, 0.0f), 4000, false);
            } else if (i == 2) {
                const S_vector step = dir - side;
                squad->OrderMove(ToVector4((step + step) + pos, 0.0f), 8000, false);
            }
        }
        break;
    }
    case MANOEUVRE_GRENADE_ROOM: {
        if (m_stage != 0 || --m_waiting != 0)
            return;
        m_manoeuvre = MANOEUVRE_NONE;
        const C_Door *door = static_cast<C_Door *>(m_manoeuvreActor);
        for (size_t i = m_manoeuvreMembers.size(); i-- > 0;) {
            C_Human *member = m_manoeuvreMembers[i];
            C_SquadController *squad = SquadOf(member);
            if (i > 1)
                continue;
            if (i == 0) {
                squad->m_action.finished = true;
                if (!IsDoorOpening(door)) {
                    // Still shut: it opens the door first (and the module hears when).
                    squad->m_action.Set(m_manoeuvreActor, HUMAN_COMMAND_OPEN_DOOR, S_vector4{}, member->GetDirection4(), false, 0.5f);
                    squad->m_action.arrived = true;
                    squad->m_action.notifyModule = true;
                    m_manoeuvre = MANOEUVRE_GRENADE_ROOM;
                    m_waiting = 1;
                    return;
                }
            } else if (!IsDoorOpening(door)) {
                squad->SayReply(SENTENCE_CANNOT, true, 300, false);
                continue;
            }
            // A grenade 7 m on through the door.
            squad->m_action.finished = true;
            const S_vector4 &pos = member->GetPosition4();
            const S_vector &at = door->GetFrame()->GetWorldPos();
            S_vector flat(at.x - pos.x, 0.0f, at.z - pos.z);
            flat.Normalize();
            squad->ThrowAt({pos.x + flat.x * 7.0f, pos.y + flat.y * 7.0f, pos.z + flat.z * 7.0f, 0.0f});
            if (i == 0)
                return;
        }
        return;
    }
    case MANOEUVRE_THROUGH_DOOR: {
        if (m_stage != 0 || --m_waiting != 0)
            return;
        // Both are beside the door: through it, the second 3 s later.
        m_stage = 1;
        const S_vector &dir = XYZ(m_doorDir);
        const S_vector &pos = XYZ(m_doorPos);
        const S_vector side = dir.Cross(UP);
        for (size_t i = m_manoeuvreMembers.size(); i-- > 0;) {
            C_SquadController *squad = SquadOf(m_manoeuvreMembers[i]);
            if (i == 0)
                squad->OrderMove(ToVector4(dir * 1.5f + pos, 0.0f), 0, false);
            else if (i == 1)
                squad->OrderMove(ToVector4((side + dir) * 1.5f + pos, 0.0f), 3000, false);
            else if (i == 2)
                squad->OrderMove(ToVector4((dir - side) * 1.5f + pos, 0.0f), 4000, false);
        }
        break;
    }
    default:
        return;
    }
    m_manoeuvre = MANOEUVRE_NONE;
}
