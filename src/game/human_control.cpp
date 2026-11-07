// C_Human: the controller (creation and the thin wrappers around its methods), the HUMAN_*
// script commands, names, squad formations and small accessors (original source
// 0x005a3de0..0x005a45f0, 0x005a5c70..0x005a6480, 0x005a6600..0x005a6710, 0x005a80d0..0x005a8820,
// 0x005a88b0, 0x005a9100..0x005a9980, 0x005aa790..0x005aa7b0 and 0x005af150..0x005af780).
#include "game/human.h"

#include "game/actors_module.h"
#include "game/game.h"
#include "game/human_body.h"
#include "game/human_internal.h"
#include "game/item_defs.h"
#include "game/items.h"
#include "game/pending.h"
#include "game/squad_commander.h"
#include "game/world.h"

// --------------------------------------------------------------------------- controller

void C_Human::CreateController(E_HumanController type)
{
    C_HumanController *previous = m_controller;
    if (previous && previous->GetType() == type)
        return;
    switch (type) {
    case HUMAN_CONTROLLER_PLAYER:
        m_controller = new C_PlayerController();
        break;
    case HUMAN_CONTROLLER_SQUAD:
        m_controller = C_SquadController::Create();
        break;
    case HUMAN_CONTROLLER_ENEMY:
        m_controller = new C_EnemyController();
        break;
    case HUMAN_CONTROLLER_AI:
        m_controller = new C_ScriptedController();
        break;
    case HUMAN_CONTROLLER_NEUTRAL:
        m_controller = new C_NeutralController();
        break;
    case HUMAN_CONTROLLER_REMOTE:
        m_controller = new C_RemoteController();
        break;
    case HUMAN_CONTROLLER_SNIPER:
        m_controller = new C_SniperController();
        break;
    default:
        m_controller = nullptr;
        break;
    }
    // Fix: the original attaches without checking (an unknown type crashes).
    if (m_controller)
        m_controller->Attach(this, previous);
    if (previous) {
        previous->Detach();
        delete previous;
    }
}

void C_Human::AssignPlayerController(bool active)
{
    if (!IsLocalGame()) {
        if (m_netId == g_localPlayerSlot) {
            CreateController(HUMAN_CONTROLLER_PLAYER);
            m_body.SetPlayerControlled(true);
            return;
        }
        // The server runs every player's input; clients only show the others.
        CreateController(IsServer() ? HUMAN_CONTROLLER_PLAYER : HUMAN_CONTROLLER_REMOTE);
        m_body.SetPlayerControlled(false);
        return;
    }
    if (active) {
        if (m_controller->GetType() == HUMAN_CONTROLLER_SQUAD)
            static_cast<C_SquadController *>(m_controller)->CancelOrders(false);
        CreateController(HUMAN_CONTROLLER_PLAYER);
        g_teamManager.GetPlayerTeam()->SelectMember(this);
        m_body.SetPlayerControlled(active);
        return;
    }
    CreateController(HUMAN_CONTROLLER_SQUAD);
    m_body.SetPlayerControlled(false);
}

void C_Human::SetScripted(bool on)
{
    if (!m_controller)
        return;
    if (on) {
        if (m_controller->GetType() == HUMAN_CONTROLLER_SQUAD)
            static_cast<C_SquadController *>(m_controller)->CancelOrders(false);
        m_body.SetPlayerControlled(false);
        CreateController(HUMAN_CONTROLLER_AI);
        return;
    }
    AssignPlayerController(g_teamManager.GetPlayerTeam()->GetActivePlayer() == this);
}

void C_Human::StartScriptedController()
{
    CreateController(HUMAN_CONTROLLER_AI);
    if (m_controller->GetType() == HUMAN_CONTROLLER_AI)
        GetScriptedController()->StartScript();
}

int C_Human::GetControllerType() const
{
    return m_controller->GetType();
}

void C_Human::IssueCommand(const S_HumanCommand &command)
{
    m_controller->IssueCommand(command);
}

bool C_Human::HasOrder() const
{
    return m_controller->HasOrder();
}

const S_vector &C_Human::GetFacingDir() const
{
    return m_controller->GetFacingDir();
}

const S_vector &C_Human::GetViewDir() const
{
    return m_controller->GetViewDir();
}

void C_Human::SetFacingDir(const S_vector4 &dir)
{
    m_controller->SetFacingDir(dir);
}

void C_Human::SetViewDir(const S_vector4 &dir)
{
    m_controller->SetViewDir(dir);
}

float C_Human::GetAccuracy() const
{
    C_WeaponItem *weapon = GetHandsWeapon();
    if (!weapon)
        return 0.0f;
    C_ItemDef *def = weapon->GetDef();
    if (def->GetCarryMode() == ITEM_CARRY_EQUIPMENT)
        return 0.0f;
    const float dispersion = static_cast<C_WeaponComp *>(def->GetComponent())->GetAimedDispersion();
    return m_controller->GetAimBonus() + dispersion * 0.01f;
}

uint32_t C_Human::GetMoveMode() const
{
    return m_controller->GetMoveFlags() & MOVE_MOVING_MASK;
}

uint32_t C_Human::GetStateFlags() const
{
    return m_controller->GetStateFlags();
}

uint32_t C_Human::GetAIModeFlags() const
{
    return m_controller->GetMoveFlags() & MOVE_AI_MODE_MASK;
}

uint32_t C_Human::GetStanceFlags() const
{
    return m_controller->GetMoveFlags() & MOVE_STANCE_MASK;
}

int C_Human::GetMoveSpeedClass() const
{
    const uint32_t flags = m_controller->GetMoveFlags();
    if ((flags & MOVE_MOVING_MASK) == 0)
        return -1;
    const int state = m_body.GetWorkState();
    if (state == 5) {
        if (flags & 0x08)
            return 5;
        return (flags & 0x10) != 0 ? 1 : 2;
    }
    if (state == 2 && (flags & 0x10) != 0)
        return 1;
    return state;
}

int C_Human::GetStance() const
{
    return m_body.GetStance();
}

void C_Human::SetControllerAIMode(int mode)
{
    m_controller->SetAIMode(mode);
}

void C_Human::MakeNoise()
{
    if (IsLocalGame() && IsLocalPlayer() && m_controller->GetQuietTime() > 1000)
        GetSquadCommander()->OnPlayerNoise();
    m_controller->ResetQuietTime();
}

bool C_Human::IsUnconscious() const
{
    if (m_stats[STAT_HEALTH] <= 0 || !m_controller || m_role == HUMAN_ROLE_PLAYER)
        return false;
    return (m_controller->GetMoveFlags() & MOVE_UNCONSCIOUS) != 0;
}

bool C_Human::IsInPlay() const
{
    if (IsLocalGame())
        return true;
    if (!m_controller)
        return false;
    if (m_role != HUMAN_ROLE_PLAYER)
        return true;
    return m_inPlay;
}

void C_Human::DetachFromWorld()
{
    if (m_vehicle)
        m_vehicle->LeaveSeat(m_seat);
    if (m_sightId != -1) {
        g_sightModule.RemoveObserver(m_sightId);
        m_sightId = -1;
    }
    if (m_sightTargetId != -1) {
        g_sightModule.RemoveTarget(m_sightTargetId);
        m_sightTargetId = -1;
    }
    if (m_soundListener != -1) {
        GetSoundModule()->RemoveListener(m_soundListener);
        m_soundListener = -1;
    }
    if (m_periodicLink != -1) {
        PeriodicLinks_Remove(m_periodicLink);
        m_periodicLink = -1;
    }
    SetBodyDetached(true);
    if (m_controller)
        m_controller->FlushMessages(IsServer());
    m_inPlay = false;
}

bool C_Human::IsSpectating() const
{
    if (IsLocalGame() || !m_controller || m_role != HUMAN_ROLE_PLAYER)
        return false;
    return m_controller->IsSpectating();
}

bool C_Human::IsRemote() const
{
    return m_controller && m_controller->GetType() == HUMAN_CONTROLLER_REMOTE;
}

float C_Human::GetMoveSpeed() const
{
    if ((m_controller->GetMoveFlags() & MOVE_MOVING_MASK) != 0)
        return m_body.GetSpeed();
    return 0.0f;
}

bool C_Human::IsInWater() const
{
    return (m_controller->GetMoveFlags() & MOVE_SWIMMING) != 0;
}

bool C_Human::IsSurrendered() const
{
    if (m_role != HUMAN_ROLE_ENEMY || IsUnconscious() || !m_controller)
        return false;
    return m_controller->IsSurrendered();
}

bool C_Human::IsAISuspended() const
{
    if (!m_controller || m_controller->GetType() != HUMAN_CONTROLLER_AI)
        return false;
    return GetScriptedController()->IsSuspended();
}

void C_Human::OnMessage(C_BitStream *msg)
{
    m_controller->OnMessage(msg);
}

void C_Human::Unknown20(C_BitStream *msg)
{
    m_controller->OnMessage2(msg);
}

void C_Human::HandleMessage(C_Human *human, C_BitStream *msg)
{
    if (human && human->m_controller &&
        (human->m_controller->GetType() == HUMAN_CONTROLLER_PLAYER || human->m_controller->GetType() == HUMAN_CONTROLLER_REMOTE)) {
        human->OnMessage(msg);
        return;
    }
    C_PlayerController::HandleMessage(msg);
}

void C_Human::HandleMessage2(C_Human *human, C_BitStream *msg)
{
    if (human && human->m_controller && human->m_controller->GetType() == HUMAN_CONTROLLER_PLAYER) {
        human->Unknown20(msg);
        return;
    }
    C_PlayerController::HandleMessage2(msg);
}

// Thin wrappers of controller slots.
void C_Human::TestSightOfMeleeTracker(C_Actor *meleeTracker, int *status, float *visibility)
{
    if (!IsLocalPlayer()) {
        m_controller->TestSightOfMeleeTracker(meleeTracker, status, visibility);
        return;
    }
    // (`visibility` is left as it was; the sight module then reads its own uninitialised
    // local.)
    *status = 500;
}

void C_Human::TestSightOfHuman(C_Human *target, int *status, float *visibility)
{
    m_controller->TestSightOfHuman(target, status, visibility);
}

void C_Human::SetMoveFlags(uint32_t flags)
{
    m_controller->SetMoveFlags(flags);
}

void C_Human::SetUnconscious(C_Human * /*by*/)
{
    m_controller->SetUnconscious();
}

bool C_Human::AdjustToPathHeight(S_vector *pos) const
{
    return m_controller ? m_controller->AdjustToPathHeight(pos) : false;
}

bool C_Human::ControllerUnknown5C(float *intensity, float *reference) const
{
    return m_controller ? m_controller->Unknown5C(intensity, reference) : false;
}

void C_Human::RequestLeaveVehicle()
{
    m_controller->RequestLeaveVehicle();
}

void C_Human::SendRemoteRequest(int request)
{
    if (m_controller)
        m_controller->SendRemoteRequest(request);
}

bool C_Human::ControllerUnknown104() const
{
    return m_controller ? m_controller->Unknown104() : false;
}

int C_Human::GetWeaponStance() const
{
    return m_controller->GetWeaponStance();
}

void C_Human::GetAIPathPoints(uint16_t *current, uint16_t *next)
{
    // Fix: the original reads the type of a missing controller.
    if (!m_controller)
        return;
    if (m_controller->GetType() != HUMAN_CONTROLLER_ENEMY && m_controller->GetType() != HUMAN_CONTROLLER_AI)
        return;
    static_cast<C_AIController *>(m_controller)->GetPathPoints(current, next);
}

// Player controller members.
void C_Human::HandlePlayerInput(float *held, float *pressed)
{
    static_cast<C_PlayerController *>(m_controller)->HandleInput(held, pressed);
}

void C_Human::PlayerRequestEquipment(int humanId)
{
    static_cast<C_PlayerController *>(m_controller)->RequestEquipment(humanId);
}

void C_Human::PlayerSendActionCommand(const S_ActionCommand &command)
{
    static_cast<C_PlayerController *>(m_controller)->SendActionCommand(command);
}

void C_Human::PlayerSetViewMode(int arg)
{
    if (IsLocalPlayer())
        static_cast<C_PlayerController *>(m_controller)->SetViewMode(arg);
}

void C_Human::PlayerRequestDamage(int type, int damage, int attackerId, int zone)
{
    if (!IsLocalGame() && m_role == HUMAN_ROLE_PLAYER)
        static_cast<C_PlayerController *>(m_controller)->RequestDamage(type, damage, attackerId, zone);
}

bool C_Human::IsPlayerHoldingBreath() const
{
    if (m_controller->GetType() != HUMAN_CONTROLLER_PLAYER)
        return false;
    return static_cast<C_PlayerController *>(m_controller)->IsHoldingBreath();
}

int C_Human::GetPlayerViewMode() const
{
    if (!IsLocalPlayer())
        return 0;
    return static_cast<C_PlayerController *>(m_controller)->GetViewMode();
}

// Squad orders (the queue of a squad member's AI).
void C_Human::AddOrder(S_HumanOrder *order)
{
    static_cast<C_SquadController *>(m_controller)->AddOrder(order);
}

void C_Human::InsertOrder(S_HumanOrder *order, int index)
{
    static_cast<C_SquadController *>(m_controller)->InsertOrder(order, index);
}

void C_Human::SetOrder(int index, S_HumanOrder *order)
{
    static_cast<C_SquadController *>(m_controller)->SetOrder(index, order);
}

void C_Human::CancelOrders()
{
    static_cast<C_SquadController *>(m_controller)->CancelOrders(true);
}

void C_Human::RemoveOrder(int index)
{
    static_cast<C_SquadController *>(m_controller)->RemoveOrder(index, true);
}

bool C_Human::HaveOrdersChanged() const
{
    switch (m_controller->GetType()) {
    case HUMAN_CONTROLLER_SQUAD:
        return static_cast<C_SquadController *>(m_controller)->HaveOrdersChanged();
    case HUMAN_CONTROLLER_PLAYER:
        return static_cast<C_PlayerController *>(m_controller)->GetOrdersChanged();
    default:
        return false;
    }
}

int C_Human::GetOrderCount() const
{
    if (m_controller->GetType() != HUMAN_CONTROLLER_SQUAD)
        return 0;
    return static_cast<C_SquadController *>(m_controller)->GetOrderCount();
}

S_HumanOrder *C_Human::GetOrder(int index) const
{
    return static_cast<C_SquadController *>(m_controller)->GetOrder(index);
}

int C_Human::GetSquadStatusIcon() const
{
    if (!m_controller || m_controller->GetType() != HUMAN_CONTROLLER_SQUAD)
        return -1;
    return static_cast<C_SquadController *>(m_controller)->GetStatusIcon();
}

int C_Human::GetSquadModeIcon() const
{
    if (!m_controller || m_controller->GetType() != HUMAN_CONTROLLER_SQUAD)
        return -1;
    return static_cast<C_SquadController *>(m_controller)->GetModeIcon();
}

// ------------------------------------------------------------------------------- body

void C_Human::ExecuteBodyCommand(const S_BodyCommand &command)
{
    m_body.Execute(command);
}

void C_Human::SetCarryMode(int mode)
{
    m_body.SetCarryMode(mode);
}

void C_Human::PlayAnimation(const char *name, int a, int b, int c)
{
    m_body.PlayAnimation(name, a, b, c);
}

bool C_Human::IsLeftFootForward() const
{
    return m_body.IsLeftFootForward();
}

uint32_t C_Human::GetEnvironmentFlags() const
{
    if (!m_vehicle) {
        if (const uint32_t *ground = m_body.GetGround())
            return *ground & 0x6000;
    } else if (m_vehicle->GetType() == ACTOR_CAR) {
        return static_cast<C_Car *>(m_vehicle)->GetEnvironmentFlags();
    }
    return 0;
}

void C_Human::SetUnknown94(float value)
{
    if (m_unknown94 == value)
        return;
    m_unknown94 = value;
    if (m_body.movement)
        m_body.movement->SetUnknown94(value);
}

// ------------------------------------------------------------------------------ names

const char *C_Human::GetPlayerName() const
{
    return g_netGame.GetPlayerName(m_netId);
}

const char *C_Human::GetLastName() const
{
    if (!IsLocalGame() && m_role == HUMAN_ROLE_PLAYER)
        return GetPlayerName();
    if (!m_profile)
        return m_frame->GetName();
    const C_str &name = m_profile->customName.IsEmpty() ? m_profile->lastName : m_profile->customName;
    return name.c_str();
}

const char *C_Human::GetFirstName() const
{
    if (!IsLocalGame() && m_role == HUMAN_ROLE_PLAYER)
        return GetPlayerName();
    if (!m_profile)
        return m_frame->GetName();
    return m_profile->firstName.c_str();
}

const char *C_Human::GetShortName() const
{
    if (!IsLocalGame() && m_role == HUMAN_ROLE_PLAYER)
        return GetPlayerName();
    if (!m_profile)
        return const_cast<C_Human *>(this)->GetName()->c_str();
    const C_str &name = m_profile->customName.IsEmpty() ? m_profile->shortName : m_profile->customName;
    return name.c_str();
}

const char *C_Human::GetFullName() const
{
    if (!IsLocalGame() && m_role == HUMAN_ROLE_PLAYER)
        return GetPlayerName();
    if (!m_profile)
        return m_frame->GetName();
    const C_str &name = m_profile->customName.IsEmpty() ? m_profile->fullName : m_profile->customName;
    return name.c_str();
}

int C_Human::GetProfileUnknown24() const
{
    if (!IsLocalGame()) {
        // A network game: the rank the round's score gave the player, not the profile's.
        // (Fix: the original reads past the players for a human without a slot.)
        const S_NetPlayerState *players = g_netGame.LockPlayers();
        const int rank = m_netId >= 0 && m_netId < g_netGame.GetPlayerCount() ? players[m_netId].stats[NET_STAT_RANK] : 0;
        g_netGame.UnlockPlayers();
        return rank;
    }
    return m_profile ? m_profile->unknown24 : 0;
}

// ------------------------------------------------------------------------ formations

void C_Human::SetLeader(C_Human *leader)
{
    if (leader == m_leader)
        return;
    if (m_leader) {
        if (C_FormationShape *formation = m_leader->m_formation) {
            formation->RemoveMember(this);
            // A formation of one is dissolved.
            if (m_leader->m_formation->GetMemberCount() < 2)
                m_leader->DisbandFormation();
        }
        m_leader = nullptr;
    }
    if (!leader)
        return;
    if (!leader->m_formation) {
        C_FormationShape *formation = new C_FormationShape();
        leader->m_formation = formation;
        formation->AddMember(leader);
        leader->m_formation->AddMember(this);
        leader->m_formation->SetLeader(leader);
        leader->m_formation->SetType(FORMATION_FOLLOW);
        leader->m_formation->SetScale(g_defaultFormationScale);
    } else {
        leader->m_formation->AddMember(this);
    }
    m_leader = leader;
}

void C_Human::DisbandFormation()
{
    if (!m_formation)
        return;
    if (m_formation->HasSlots()) {
        // Releasing a member may dissolve the formation (and delete it) underway.
        for (int slot = C_FormationShape::SLOT_COUNT; slot-- > 0;) {
            if (!m_formation)
                break;
            C_Human *member = m_formation->GetSlotMember(slot);
            if (member && member != this)
                member->SetLeader(nullptr);
        }
    }
    delete m_formation;
    m_formation = nullptr;
}

int C_Human::GetFormationType() const
{
    return m_formation ? m_formation->GetType() : FORMATION_NONE;
}

// --------------------------------------------------------------------- script commands

void C_Human::SetAlarmType(uint32_t flags, bool set)
{
    if (m_controller->GetType() == HUMAN_CONTROLLER_AI)
        GetScriptedController()->SetAlarmType(flags, set);
}

uint32_t C_Human::GetAlarmType() const
{
    if (m_controller->GetType() == HUMAN_CONTROLLER_AI)
        return GetScriptedController()->GetAlarmType();
    return 0;
}

void C_Human::SetAlarm(bool on)
{
    if (m_controller->GetType() == HUMAN_CONTROLLER_AI)
        GetScriptedController()->SetAlarm(on);
}

void C_Human::AttackMoveTo(int checkpoint, int label)
{
    if (m_controller->GetType() == HUMAN_CONTROLLER_AI)
        GetScriptedController()->AttackMoveTo(static_cast<int16_t>(checkpoint), label);
}

void C_Human::MoveTo(int checkpoint, int label)
{
    if (m_controller->GetType() == HUMAN_CONTROLLER_AI)
        GetScriptedController()->MoveTo(static_cast<int16_t>(checkpoint), label);
}

int C_Human::FindCheckpoint(const char *name) const
{
    if (m_controller->GetType() == HUMAN_CONTROLLER_AI)
        return GetScriptedController()->FindCheckpoint(name);
    return 0xFFFF;
}

void C_Human::AttackActor(C_Actor *target, int time, bool fake, int label)
{
    if (m_controller->GetType() == HUMAN_CONTROLLER_AI)
        GetScriptedController()->AttackActor(target, time, fake, label);
}

void C_Human::AttackFrame(I3D_frame *target, int time, bool fake, int label)
{
    if (m_controller->GetType() == HUMAN_CONTROLLER_AI)
        GetScriptedController()->AttackFrame(target, time, fake, label);
}

void C_Human::SetMoveModeRun()
{
    if (m_controller->GetType() == HUMAN_CONTROLLER_AI)
        GetScriptedController()->SetMoveModeRun();
}

void C_Human::SetMoveModeWalk()
{
    if (m_controller->GetType() == HUMAN_CONTROLLER_AI)
        GetScriptedController()->SetMoveModeWalk();
}

void C_Human::SetMoveModeSilence()
{
    if (m_controller->GetType() == HUMAN_CONTROLLER_AI)
        GetScriptedController()->SetMoveModeSilence();
}

void C_Human::SetMoveModeGuard()
{
    if (m_controller->GetType() == HUMAN_CONTROLLER_AI)
        GetScriptedController()->SetMoveModeGuard();
}

void C_Human::SetMoveModeLie()
{
    if (m_controller->GetType() == HUMAN_CONTROLLER_AI)
        GetScriptedController()->SetMoveModeLie();
}

void C_Human::SetMoveModeCrouch()
{
    if (m_controller->GetType() == HUMAN_CONTROLLER_AI)
        GetScriptedController()->SetMoveModeCrouch();
}

void C_Human::SetMoveModeStand()
{
    if (m_controller->GetType() == HUMAN_CONTROLLER_AI)
        GetScriptedController()->SetMoveModeStand();
}

void C_Human::SetMoveModeStandFast()
{
    if (m_controller->GetType() == HUMAN_CONTROLLER_AI)
        GetScriptedController()->SetMoveModeStandFast();
}

void C_Human::Sit(I3D_frame *seat)
{
    if (m_controller->GetType() == HUMAN_CONTROLLER_AI)
        GetScriptedController()->Sit(seat);
}

void C_Human::ActivityTurk()
{
    if (m_controller->GetType() == HUMAN_CONTROLLER_AI)
        GetScriptedController()->ActivityTurk();
}

void C_Human::DisableEvents()
{
    if (m_controller->GetType() == HUMAN_CONTROLLER_AI)
        GetScriptedController()->DisableEvents();
}

void C_Human::EnableEvents()
{
    if (m_controller->GetType() == HUMAN_CONTROLLER_AI)
        GetScriptedController()->EnableEvents();
}

void C_Human::SetAIMode(int mode)
{
    if (m_controller->GetType() == HUMAN_CONTROLLER_AI)
        GetScriptedController()->SetMode(mode);
}

void C_Human::StopAction()
{
    if (m_controller->GetType() == HUMAN_CONTROLLER_AI)
        GetScriptedController()->StopAction();
}

void C_Human::ResetAI()
{
    if (m_controller->GetType() == HUMAN_CONTROLLER_AI)
        GetScriptedController()->ResetAI();
}

void C_Human::PickObject(I3D_frame *object, bool oneHand)
{
    if (m_controller->GetType() == HUMAN_CONTROLLER_AI)
        GetScriptedController()->PickObject(object, oneHand);
}

void C_Human::DropObject()
{
    if (m_controller->GetType() == HUMAN_CONTROLLER_AI)
        GetScriptedController()->DropObject();
}

void C_Human::TakeWeapon()
{
    if (m_controller->GetType() == HUMAN_CONTROLLER_AI)
        GetScriptedController()->TakeWeapon();
}

void C_Human::HolsterWeapon()
{
    if (m_controller->GetType() == HUMAN_CONTROLLER_AI)
        GetScriptedController()->HolsterWeapon();
}

void C_Human::Reload()
{
    if (m_controller->GetType() == HUMAN_CONTROLLER_AI)
        GetScriptedController()->Reload();
}

int C_Human::PlayActivity(int activity)
{
    if (m_controller->GetType() == HUMAN_CONTROLLER_AI)
        return GetScriptedController()->PlayActivity(activity);
    return 0;
}

int C_Human::PlayActivity(int activity, I3D_frame *target)
{
    if (m_controller->GetType() == HUMAN_CONTROLLER_AI)
        return GetScriptedController()->PlayActivity(activity, target);
    return 0;
}

void C_Human::SetAIOptimized(int mode)
{
    if (m_controller->GetType() == HUMAN_CONTROLLER_AI)
        GetScriptedController()->SetOptimized(mode);
}

void C_Human::SetWaitForPlayer(bool on, float stopDist2, float resumeDist2, bool watch)
{
    if (m_controller->GetType() == HUMAN_CONTROLLER_AI)
        GetScriptedController()->SetWaitForPlayer(on, stopDist2, resumeDist2, watch);
}

bool C_Human::SeesEnemy() const
{
    if (m_controller->GetType() == HUMAN_CONTROLLER_AI)
        return GetScriptedController()->SeesEnemy();
    return false;
}

bool C_Human::SeesFriend() const
{
    if (m_controller->GetType() == HUMAN_CONTROLLER_AI)
        return GetScriptedController()->SeesFriend();
    return false;
}

void C_Human::TurnAt(I3D_frame *target)
{
    if (m_controller->GetType() == HUMAN_CONTROLLER_AI)
        GetScriptedController()->TurnAt(target);
}

void C_Human::LookAround(int type)
{
    if (m_controller->GetType() == HUMAN_CONTROLLER_AI)
        static_cast<C_AIController *>(m_controller)->LookAround(type);
}

void C_Human::MoveToAlarm(int label)
{
    if (m_controller->GetType() == HUMAN_CONTROLLER_AI)
        GetScriptedController()->MoveToAlarm(label);
}

void C_Human::SetSniper(bool on, float radius)
{
    m_events->SetSniper(on, radius, m_frame->GetWorldPos());
}

void C_Human::ScriptedAdvanceScript(int a, int b)
{
    if (m_controller->GetType() == HUMAN_CONTROLLER_AI)
        GetScriptedController()->AdvanceScript(a, b);
}

bool C_Human::ScriptedHasAIMode()
{
    if (m_controller->GetType() == HUMAN_CONTROLLER_AI)
        return GetScriptedController()->HasAIMode();
    return false;
}

void C_Human::ScriptedPlaySpeech(int arg)
{
    if (m_controller->GetType() == HUMAN_CONTROLLER_AI)
        GetScriptedController()->PlaySpeech(arg);
}

void C_Human::ScriptedLookAround1(int arg)
{
    if (m_controller->GetType() == HUMAN_CONTROLLER_AI)
        GetScriptedController()->LookAround1(arg);
}

void C_Human::RaiseWeapon()
{
    m_controller->RaiseWeapon();
}

void C_Human::LowerWeapon()
{
    m_controller->LowerWeapon();
}

void C_Human::SetAnimation(const char *name, int blendTime, int cancelTime, bool loop)
{
    if (m_controller->GetType() == HUMAN_CONTROLLER_AI)
        GetScriptedController()->SetAnimation(name, blendTime, cancelTime, loop);
}
