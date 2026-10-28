// C_EnemyController's life cycle, events, the choice of its task and its firing
// (game/human_controller.h; original source 0x005478c0..0x00548840, 0x00549360, 0x0054ab00,
// 0x0054c000..0x0054cb70, 0x005518f0..0x00551ba0, 0x00558110..0x00558e20,
// 0x00559360..0x00559760, 0x005a4460..0x005a44fd).
#include "game/human_controller.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <vector>

#include "common/chunk.h"
#include "game/actor.h"
#include "game/actors_module.h"
#include "game/ai_parts.h"
#include "game/checkpoints.h"
#include "game/controller_internal.h"
#include "game/enemy_internal.h"
#include "game/game.h"
#include "game/human.h"
#include "game/human_body.h"
#include "game/human_movement.h"
#include "game/item_defs.h"
#include "game/items.h"
#include "game/pending.h"
#include "game/soldier.h"
#include "ls3d/i3d.h"
#include "script/script.h"

namespace {

// The saved game's chunks.
enum : uint16_t {
    CHUNK_ENEMY_AI = 0x139D,                            // the AI base's
    CHUNK_ENEMY_STATE = 0x13B1,
    CHUNK_ENEMY_TARGET = 0x13B2,                        // actor id
    CHUNK_ENEMY_EVENT = 0x13B4,                         // a search task's event
    CHUNK_ENEMY_GUN = 0x13B5,                           // actor id of a guard task's gun
    CHUNK_ENEMY_SURRENDER_OLD = 0x13B6,                 // (an older layout: the surrender time alone)
    CHUNK_ENEMY_TIMES = 0x13B7,
    CHUNK_ENEMY_TARGET_CHANGED = 0x13B8,
};
// Positions are saved without w.
constexpr int SAVED_VECTOR_SIZE = 12;
// The item type of a single-shot launcher, thrown away when empty (inferred).
constexpr int ITEM_TYPE_SINGLE_LAUNCHER = 0x26;
// The bursts and pauses by the weapon's behaviour (0x0083c578..0x0083c5f8): the burst far /
// near, the squared distances of near and far, the pause near / far, their squared distances.
struct S_BurstTable {
    int burstFar, burstNear, burstNear2, burstFar2;
    int pauseNear, pauseFar, pauseNear2, pauseFar2;
};
constexpr S_BurstTable BURST_RIFLE = {95, 380, 25, 2500, 580, 1480, 25, 2500};       // behaviour 3
constexpr S_BurstTable BURST_SIDEARM = {50, 110, 25, 625, 480, 1000, 25, 400};       // behaviour 2
constexpr S_BurstTable BURST_4 = {93, 280, 25, 2500, 480, 1780, 100, 10000};         // behaviour 4
constexpr S_BurstTable BURST_5 = {380, 480, 100, 10000, 1400, 2520, 100, 10000};     // behaviours 5, 12

template <class T>
void ReadIfFits(C_chunk &chunk, T &value)
{
    if (chunk.RemainingSize() >= static_cast<int>(sizeof(T)))
        chunk.Read(value);
}

// Where `human` will be in `seconds` (its frame and velocity; w: a stale local in the
// original). 0x00558390.
S_vector4 PredictPos(const C_Human *human, float seconds)
{
    const S_vector &v = human->GetVelocity();
    const S_vector &p = human->GetFrame()->GetWorldPos();
    return {seconds * v.x + p.x, seconds * v.y + p.y, seconds * v.z + p.z, 0.0f};
}

}  // namespace

// ------------------------------------------------------------------------- life cycle

C_EnemyController::~C_EnemyController()
{
    // Fix: a search task's event went back to no pool when the controller was deleted.
    if (m_event && m_task && m_task->type == 2)
        ReleaseHumanEvent(m_event);
    m_event = nullptr;
}

void C_EnemyController::Attach(C_Human *human, C_HumanController *previous)
{
    C_AIController::Attach(human, previous);
    m_fireWait = 0;
    m_task = nullptr;
    m_event = nullptr;
    m_gun = nullptr;
    m_unknown150 = 0;
    m_enemyFlags = 0;
    m_unknown14C = 0;
    m_homeCheckpoint = -1;
    m_target = nullptr;
    m_unknown154 = false;
    m_alertTime = 0;
    m_grenadeTime = static_cast<int>(static_cast<float>(std::rand()) * (1.0f / 32767.0f) * 8000.0f);
    m_gunCheckTime = 0;
    m_moraleCheckTime = 5000;
    m_unarmedTime = 0;
    m_surrenderTime = -1;
    m_enemySeenTime = 0;
    m_targetChanged = false;
    m_homePos = WorldPos4Of(m_human->GetFrame());
    // (Off the walking network the original reads STAT_HEALTH and drops it.)
    if (previous) {
        // The events the previous controller did not handle become stimuli, else its sounds
        // and alarm.
        if (m_events->HasPending()) {
            while (m_events->HasPending())
                m_events->AddStimulus(10000, m_events->PopFront(), true);
        } else {
            TakeOver(previous);
        }
    }
    if (m_moveFlags & MOVE_STANDING)
        m_aimHeight = 0.45f;
    else if (m_moveFlags & MOVE_CROUCHING)
        m_aimHeight = 0.59f;
    else if (m_moveFlags & MOVE_LYING)
        m_aimHeight = 0.0f;
    if (!(m_moveFlags & MOVE_AI_MODE_MASK))
        m_moveFlags |= MOVE_AI_DEFENSIVE;
    if (m_moveFlags & MOVE_AI_AGGRESSIVE)
        SetupAggressive();
    else if (m_moveFlags & MOVE_AI_DEFENSIVE)
        SetupDefensive();
    else if (m_moveFlags & MOVE_AI_ZOMBIE)
        SetupZombie();
    // Nothing to go back to without the script's OnAlarmDone block.
    const S_FrameData *data = m_human->GetFrame()->GetData();
    if (!data || !data->script || !data->script->HasOnAlarmDone())
        m_enemyFlags |= FLAG_NO_ALARM_DONE;
    if (m_human->GetStat(STAT_MORALE) >= 90)
        m_enemyFlags |= FLAG_BRAVE;
    else if (m_human->GetStat(STAT_MORALE) >= 75)
        m_enemyFlags |= FLAG_STEADY;
    LookAround(0);
}

bool C_EnemyController::SetupAggressive()
{
    m_alertRange = 100000;
    m_alertTime = 10000;
    return true;
}

bool C_EnemyController::SetupDefensive()
{
    // A post of 20..30 m; alert within half the sight range (at least 50 m).
    m_homeRadius = static_cast<float>(std::rand()) * (1.0f / 32767.0f) * 10.0f + 20.0f;
    const float range = static_cast<float>(m_human->GetStat(STAT_SIGHT_RANGE)) * 0.5f;
    m_alertRange = static_cast<int>(range > 50.0f ? range : 50.0f);
    m_alertTime = 0;
    if (m_homeCheckpoint < 0)
        m_homeCheckpoint = m_checkpoints->FindNearest(m_human->GetFrame()->GetWorldPos(), 5.0f, -1.0f);
    FindHomeNeighbours();
    FindHomeCover();
    FindGuns();
    return true;
}

bool C_EnemyController::SetupZombie()
{
    // A post of 7..12 m; alert within a third of the sight range (at least 25 m).
    m_homeRadius = static_cast<float>(std::rand()) * (1.0f / 32767.0f) * 5.0f + 7.0f;
    const float range = static_cast<float>(m_human->GetStat(STAT_SIGHT_RANGE)) * 0.33333334f;
    m_alertRange = static_cast<int>(range > 25.0f ? range : 25.0f);
    m_alertTime = 0;
    if (m_homeCheckpoint < 0)
        m_homeCheckpoint = m_checkpoints->FindNearest(m_human->GetFrame()->GetWorldPos(), 5.0f, -1.0f);
    FindHomeNeighbours();
    FindHomeCover();
    FindGuns();
    return true;
}

bool C_EnemyController::TakeOver(C_HumanController *previous)
{
    if (!previous)
        return false;
    // What the human heard.
    const std::vector<S_RememberedSound *> &sounds = m_human->m_heardSounds;
    for (size_t i = sounds.size(); i-- > 0;)
        OnSoundHeard(m_human->m_heardSounds[i]);
    // The script's alarm (its position and kind) as a stimulus.
    if (previous->GetType() == HUMAN_CONTROLLER_AI) {
        C_ScriptedController *scripted = static_cast<C_ScriptedController *>(previous);
        const S_vector4 pos = scripted->GetAlarmPos();
        if (pos.y > 100000.0f || pos.y < -100000.0f)
            return false;
        uint32_t type;
        switch (scripted->GetAlarmType()) {
        case C_ScriptedController::ALARM_SHOT_AT: type = AI_EVENT_SHOT_AT; break;
        case C_ScriptedController::ALARM_GUNFIRE: type = AI_EVENT_SOUND_2; break;
        case C_ScriptedController::ALARM_FOOTSTEPS: type = AI_EVENT_SOUND_0; break;
        case C_ScriptedController::ALARM_OBJECT_NOISE: type = AI_EVENT_SOUND_1; break;
        case C_ScriptedController::ALARM_EXPLOSION: type = AI_EVENT_SOUND_6; break;
        case C_ScriptedController::ALARM_ENEMY_SEEN: type = AI_EVENT_ENEMY_SEEN; break;
        case C_ScriptedController::ALARM_HIT: type = AI_EVENT_HIT_5; break;
        case C_ScriptedController::ALARM_FRIEND_HEARD: type = AI_EVENT_FRIEND_HEARD; break;
        case C_ScriptedController::ALARM_BODY_SEEN: type = AI_EVENT_BODY_SEEN; break;
        default: return true;
        }
        S_HumanEvent *event = AllocHumanEvent();
        event->type = type;
        event->age = 0;
        event->timeLeft = 10000;
        event->actorId = ActorToId(nullptr);
        event->sourceId = ActorToId(nullptr);
        event->unknown30 = 0;
        event->pos = pos;
        m_events->AddStimulus(10000, event, true);
    }
    return true;
}

void C_EnemyController::FindHomeNeighbours()
{
    m_homePoints.clear();
    m_checkpoints->GetNeighbours(static_cast<uint16_t>(m_homeCheckpoint), m_homeRadius, &m_homePoints);
}

void C_EnemyController::FindHomeCover()
{
    m_homeCover.clear();
    m_checkpoints->GetNeighbourCover(static_cast<uint16_t>(m_homeCheckpoint), m_homeRadius, &m_homeCover);
}

void C_EnemyController::FindGuns()
{
    // The actors of the points of type 11 (static machine guns, inferred) at the post.
    m_guns.clear();
    for (size_t i = m_homePoints.size(); i-- > 0;) {
        const S_Checkpoint *point = m_checkpoints->Get(m_homePoints[i]);
        if (point->type != CHECKPOINT_TYPE_GUN)
            continue;
        if (C_Actor *gun = m_checkpoints->GetActor(m_homePoints[i]))
            m_guns.push_back(gun);
    }
}

void C_EnemyController::Detach()
{
    C_AIController::Detach();
    m_homePoints.clear();
    m_homeCover.clear();
    m_guns.clear();
}

void C_EnemyController::ReturnToScript()
{
    ChangeMorale(100);
    ReleaseTask();
    m_human->StartScriptedController();
}

void C_EnemyController::ReleaseTask()
{
    if (m_event && m_task && m_task->type == 2)
        ReleaseHumanEvent(m_event);
    m_event = nullptr;
    m_gun = nullptr;
    C_AIController::ReleaseTask();
}

void C_EnemyController::Update(int frameTime)
{
    if (!(m_moveFlags & (MOVE_DEAD | MOVE_UNCONSCIOUS)))
        UpdateNetwork(frameTime);
    FlushMessages(true);
}

void C_EnemyController::Tick(int timeDelta)
{
    // Network games: nothing to send while the movement is idle.
    if (m_netObject) {
        const C_HumanMovement *movement = m_human->m_body.movement;
        m_netObject->SetSendMask(movement->m_unknown1AC == 0 && movement->m_unknown9C0 == 0 ? 0 : 7);
    }
    if (m_moveFlags & (MOVE_DEAD | MOVE_UNCONSCIOUS)) {
        FlushMessages(true);
        return;
    }
    UpdateTimers(timeDelta);
    if (UpdateTask(timeDelta)) {
        RunTask(timeDelta);
        C_Actor *vehicle = m_human->m_vehicle;
        if (!vehicle) {
            ApplyAlertState();
            UpdateMove(timeDelta);
            UpdateFiring(timeDelta);
            UpdateLook(timeDelta);
            ApplyMoveControls();
        } else {
            switch (vehicle->GetType()) {
            case ACTOR_CAR:
            case ACTOR_SUBMARINE:
            case ACTOR_STATIC_WEAPON:
            case ACTOR_JUNKERS88:
                UpdateLook(timeDelta);
                ControlVehicle();
                break;
            default:
                break;
            }
        }
        ApplyWeaponControls(timeDelta, 0);
    }
    FlushMessages(true);
    if (!m_task || (m_enemyFlags & FLAG_DONE))
        ReturnToScript();
}

void C_EnemyController::UpdateTimers(int timeDelta)
{
    // A target that is gone.
    if (m_target && g_actorsModule.GetActor(m_target->GetNetId()) != m_target) {
        if (m_fireOrder)
            m_fireOrder->SetTarget(nullptr);
        m_target = nullptr;
    }
    std::fill(std::begin(g_aiAxes), std::end(g_aiAxes), 0.0f);
    std::fill(std::begin(g_aiButtons), std::end(g_aiButtons), 0.0f);
    m_axes = g_aiAxes;
    m_buttons = g_aiButtons;
    m_grenadeTime += timeDelta;
    C_AIController::UpdateTimers(timeDelta);
    if (m_fireWait != 0) {
        m_fireWait -= timeDelta;
        if (m_fireWait < 0)
            m_fireWait = 0;
    }
    if (!(m_moveFlags & MOVE_AI_AGGRESSIVE)) {
        if (m_alertTime != 0) {
            m_alertTime -= timeDelta;
            if (m_alertTime < 0)
                m_alertTime = 0;
        }
        if (SeesEnemies() && m_alertTime < 1000)
            CheckThreat();
        if (m_gunCheckTime != 0) {
            m_gunCheckTime -= timeDelta;
            if (m_gunCheckTime < 0)
                m_gunCheckTime = 0;
        }
    }
    if (!m_handsItem)
        m_unarmedTime += timeDelta;
    else
        m_unarmedTime = 0;
    // Every 5 s the morale rises by 2 and falls by the enemies in view.
    m_moraleCheckTime -= timeDelta;
    if (m_moraleCheckTime < 1) {
        m_moraleCheckTime = 5000;
        ChangeMorale(m_events->GetMoraleGain() - static_cast<int>(m_human->m_seenEnemies.size()));
    }
    if (!SeesEnemies())
        m_enemySeenTime = 0;
    else
        m_enemySeenTime += timeDelta;
    if (m_task) {
        m_task->unknown0C += timeDelta;
        m_task->unknown10 -= timeDelta;
        if (m_task->unknown10 < 0)
            m_task->unknown10 = 0;
    }
}

void C_EnemyController::CheckThreat()
{
    // Out of the alert range: only an enemy in view facing the human counts.
    if (!(GetNearestEnemyDistance() <= static_cast<float>(m_alertRange))) {
        const std::vector<C_Human *> &enemies = m_human->m_seenEnemies;
        bool faced = false;
        for (size_t i = enemies.size(); i-- > 0;) {
            if (enemies[i]->GetController()->IsFacing(m_human)) {
                faced = true;
                break;
            }
        }
        if (!faced)
            return;
    }
    if (m_moveFlags & MOVE_AI_DEFENSIVE)
        m_alertTime = static_cast<int>(static_cast<float>(std::rand()) * (1.0f / 32767.0f) * 5000.0f + 7000.0f);
    if (m_moveFlags & MOVE_AI_ZOMBIE)
        m_alertTime = static_cast<int>(static_cast<float>(std::rand()) * (1.0f / 32767.0f) * 4000.0f + 5000.0f);
}

// ------------------------------------------------------------------------- events

S_HumanEvent *C_EnemyController::HandleEvent(S_HumanEvent *event)
{
    HandleTaskEvent(event);
    return HandleCommonEvent(event);
}

S_HumanEvent *C_EnemyController::HandleTaskEvent(S_HumanEvent *event)
{
    if (!m_task)
        return event;
    switch (m_task->type) {
    case 1: GuardEvent(event); break;
    case 2: SearchEvent(event); break;
    case 3: CombatEvent(event); break;
    case 5: BrokenEvent(event); break;
    case 6: GunnerEvent(event); break;
    default: break;
    }
    return event;
}

S_HumanEvent *C_EnemyController::HandleCommonEvent(S_HumanEvent *event)
{
    // The morale goes with what happens; most events are tracked as stimuli. (The speech
    // sentences 0x76 / 0x79 / 0x7A are said by humans that did not surrender.)
    switch (event->type) {
    case AI_EVENT_BODY_SEEN:
    case AI_EVENT_BODY_HEARD:
        ChangeMorale(-5);
        return event;
    case AI_EVENT_ENEMY_LOST:
        m_events->AddStimulus(30000, event, false);
        return nullptr;
    case AI_EVENT_KILLED_ENEMY:
        ChangeMorale(10);
        if (!IsSurrendered())
            Say(0x76, 8000, 1, false);
        return event;
    case AI_EVENT_ENEMY_SEEN:
        if (!IsSurrendered())
            Say(0x79, 10000, 1, false);
        return event;
    case AI_EVENT_SOUND_0:
    case AI_EVENT_SOUND_2:
    case AI_EVENT_SOUND_1:
    case AI_EVENT_SOUND_8:
        m_events->AddStimulus(20000, event, false);
        return nullptr;
    case AI_EVENT_SHOT_AT:
    case AI_EVENT_LOUD_SOUND:
        ChangeMorale(-2);
        m_events->AddStimulus(10000, event, false);
        if (!IsSurrendered())
            Say(0x7A, 5000, 1, false);
        return nullptr;
    case AI_EVENT_HIT_5:
        ExtendBurst(static_cast<uint32_t>(static_cast<int>(static_cast<float>(std::rand()) * (1.0f / 32767.0f) * 300.0f + 100.0f)));
        ChangeMorale(-15);
        m_events->AddStimulus(10000, event, false);
        return nullptr;
    default:
        return event;
    }
}

// ------------------------------------------------------------------------- the task

void C_EnemyController::ChangeMorale(int delta)
{
    m_human->ChangeMorale(delta);
}

void C_EnemyController::UpdateMode()
{
    // Crippled: slow to react.
    if (!(m_moveFlags & MOVE_AI_ZOMBIE) && m_human->GetMoveSpeedClass() == 3) {
        m_moveFlags = (m_moveFlags & ~(MOVE_AI_AGGRESSIVE | MOVE_AI_DEFENSIVE)) | MOVE_AI_ZOMBIE;
        ReleaseTask();
        SetupZombie();
        return;
    }
    // Hurt: no longer aggressive.
    if ((m_moveFlags & MOVE_AI_AGGRESSIVE) && m_human->GetHealthPercent() < 50) {
        m_moveFlags = (m_moveFlags & ~MOVE_AI_AGGRESSIVE) | MOVE_AI_DEFENSIVE;
        ReleaseTask();
        SetupDefensive();
    }
}

void C_EnemyController::SetAIMode(int mode)
{
    switch (mode) {
    case MOVE_AI_AGGRESSIVE:
        if (!(m_moveFlags & MOVE_AI_AGGRESSIVE) && m_human->GetMoveSpeedClass() != 3 && m_human->GetHealthPercent() > 49) {
            m_moveFlags = (m_moveFlags & ~(MOVE_AI_DEFENSIVE | MOVE_AI_ZOMBIE)) | MOVE_AI_AGGRESSIVE;
            SetupAggressive();
        }
        break;
    case MOVE_AI_DEFENSIVE:
        if (!(m_moveFlags & MOVE_AI_DEFENSIVE) && m_human->GetMoveSpeedClass() != 3) {
            m_moveFlags = (m_moveFlags & ~(MOVE_AI_AGGRESSIVE | MOVE_AI_ZOMBIE)) | MOVE_AI_DEFENSIVE;
            SetupDefensive();
        }
        break;
    case MOVE_AI_ZOMBIE:
        if (!(m_moveFlags & MOVE_AI_ZOMBIE)) {
            m_moveFlags = (m_moveFlags & ~(MOVE_AI_AGGRESSIVE | MOVE_AI_DEFENSIVE)) | MOVE_AI_ZOMBIE;
            SetupZombie();
        }
        break;
    default:
        break;
    }
}

void C_EnemyController::ChooseTask()
{
    if (m_enemyFlags & FLAG_DONE)
        return;
    TakeFirearm();
    if (HasSeatWeapon()) {
        m_enemyFlags |= FLAG_GUNNER;
        StartGunner();
        return;
    }
    if (!SeesEnemies())
        StartSearch();
    else if (m_moveFlags & MOVE_AI_AGGRESSIVE)
        StartCombat();
    else
        StartGuard();
}

bool C_EnemyController::HasSeatWeapon()
{
    C_Actor *vehicle = m_human->m_vehicle;
    if (!vehicle)
        return false;
    switch (vehicle->GetType()) {
    case ACTOR_CAR:
    case ACTOR_SUBMARINE:
    case ACTOR_STATIC_WEAPON:
    case ACTOR_JUNKERS88:
        return vehicle->HasSeatWeapon(m_human->m_seat);
    default:
        return false;
    }
}

S_AITask *C_EnemyController::UpdateTask(int timeDelta)
{
    m_taskTime += timeDelta;
    UpdateMode();
    if (!m_task) {
        ChooseTask();
        return m_task;
    }
    m_human->ProcessEvents();
    if (m_human->m_body.GetClimbState() != 0)
        return m_task;
    switch (m_task->type) {
    case 5:
        // Broken: brave enough again with a weapon at hand, the human stands up and fights.
        if (m_human->GetStat(STAT_MORALE) > 40 && DrawWeapon()) {
            m_human->ExecuteBodyCommand(MakeBodyCommand(BODY_COMMAND_WORK_ANIMATION, WORK_ANIMATION_END_ACTIVITY));
            if ((g_gameRole & GAME_ROLE_LOCAL) != GAME_ROLE_LOCAL)
                SendMessage23(false);
            if (!SeesEnemies())
                StartSearch();
            else if (m_moveFlags & MOVE_AI_AGGRESSIVE)
                StartCombat();
            else
                StartGuard();
        }
        break;
    case 6:
        // Out of the gun's seat: back to fighting on foot.
        if (!HasSeatWeapon()) {
            m_enemyFlags &= ~FLAG_CANNON;
            if (!SeesEnemies())
                StartSearch();
            else if (m_moveFlags & MOVE_AI_AGGRESSIVE)
                StartCombat();
            else
                StartGuard();
        }
        break;
    default:
        if (HasSeatWeapon()) {
            StartGunner();
            break;
        }
        // Low morale or no weapon to fight with: broken.
        if (m_human->GetStat(STAT_MORALE) >= 20 || (m_enemyFlags & FLAG_BRAVE)) {
            if (m_handsItem)
                break;
            if (HasWeapon() && m_unarmedTime < 1000) {
                DrawWeapon();
                break;
            }
        }
        StartBroken();
        break;
    }
    return m_task;
}

void C_EnemyController::RunTask(int timeDelta)
{
    if (!m_task)
        return;
    switch (m_task->type) {
    case 1: UpdateGuard(timeDelta); break;
    case 2: UpdateSearch(timeDelta); break;
    case 3: UpdateCombat(timeDelta); break;
    case 5: UpdateBroken(timeDelta); break;
    case 6: UpdateGunner(timeDelta); break;
    default: break;
    }
}

void C_EnemyController::StartSearch()
{
    m_taskTime = 0;
    m_fireOrder->SetTarget(nullptr);
    m_fireWait = 0;
    if (m_moveFlags & MOVE_LYING)
        SetEnemyStance(MOVE_CROUCHING);
    S_HumanEvent *strongest;
    int kind;
    const I3D_frame *source;
    if (!m_task) {
        // The strongest stimulus where the human stands (by its source: an attack 0, else 4).
        strongest = m_events->GetStrongestStimulus(m_human->GetFrame()->GetWorldPos());
        if (!strongest)
            goto none;
        C_Actor *actor = IdToActor(strongest->sourceId);
        if (actor) {
            kind = (strongest->type & HUMAN_EVENT_PRIORITY_MASK) == 0x40000000 ? 0 : 4;
            source = IdToActor(strongest->sourceId)->GetFrame();
            goto found;
        }
    } else {
        ReleaseTask();
        strongest = m_events->GetStrongestStimulus(m_human->GetFrame()->GetWorldPos());
        if (!strongest)
            goto none;
        if (IdToActor(strongest->sourceId)) {
            kind = 6;
            source = IdToActor(strongest->sourceId)->GetFrame();
            goto found;
        }
    }
    // A body found or heard of a friend: where its attacker is.
    if (strongest->type != AI_EVENT_BODY_HEARD) {
        if (strongest->type != AI_EVENT_BODY_SEEN || !IdToActor(strongest->actorId)
            || !m_human->IsFriendOf(IdToActor(strongest->actorId)))
            goto none;
    }
    {
        // Fix: a body heard of nobody known read a missing human.
        C_Actor *body = IdToActor(strongest->actorId);
        C_Actor *attacker = body ? static_cast<C_Human *>(body)->m_events->GetAttacker() : nullptr;
        if (!attacker)
            goto none;
        m_task = AllocTask2(strongest->pos, WorldPos4Of(attacker->GetFrame()), 4);
        m_event = CopyEvent(*strongest);
        m_event->sourceId = ActorToId(attacker);
        return;
    }
found:
    m_task = AllocTask2(strongest->pos, WorldPos4Of(source), kind);
    m_event = CopyEvent(*strongest);
    return;
none:
    // Nothing to search for: waits 5 s.
    m_event = nullptr;
    const S_vector4 zero = {0.0f, 0.0f, 0.0f, 0.0f};    // (w: stale locals in the original)
    m_task = AllocTask2(zero, zero, SEARCH_IDLE);
    m_task->unknown10 = 5000;
}

void C_EnemyController::StartCombat()
{
    ReleaseTask();
    m_move->flags |= S_AIMoveOrder::FLAG_REROUTE;
    const S_vector4 zero = {0.0f, 0.0f, 0.0f, 0.0f};    // (w: a stale local in the original)
    m_task = AllocTask3(0, nullptr, zero);
    m_coverTime = 0;
    m_taskTime = 0;
}

void C_EnemyController::StartGunner()
{
    ReleaseTask();
    m_move->flags |= S_AIMoveOrder::FLAG_REROUTE;
    S_AITask6 *task = AllocTask6();
    m_task = task;
    m_taskTime = 0;
    task->unknown08 = GUNNER_START;
}

// ------------------------------------------------------------------------- weapons

void C_EnemyController::SetEnemyStance(int stance)
{
    switch (stance) {
    case MOVE_STANDING:
        if (m_moveFlags & MOVE_STANDING)
            return;
        m_aimHeight = 0.45f;
        break;
    case MOVE_CROUCHING:
        if (m_moveFlags & MOVE_CROUCHING)
            return;
        m_aimHeight = 0.59f;
        break;
    case MOVE_LYING:
        if (m_moveFlags & MOVE_LYING)
            return;
        m_aimHeight = 0.0f;
        break;
    default:
        return;
    }
    m_unknown10C = 0;
    SetStance(stance);
}

bool C_EnemyController::HasWeapon()
{
    UpdateHandsItem();
    if (m_handsItem)
        return true;
    if (m_human->GetContainer(ITEM_CONTAINER_LONG_GUN)->GetItem())
        return true;
    return m_human->FindBeltItem(ITEM_BEHAVIOUR_SIDEARM) != nullptr;
}

bool C_EnemyController::DrawWeapon()
{
    UpdateHandsItem();
    if (m_handsItem)
        return true;
    if (m_human->GetContainer(ITEM_CONTAINER_HANDS)->IsLocked())
        return false;
    // A launcher on the back: the sidearm first.
    if (C_Item *longGun = m_human->GetContainer(ITEM_CONTAINER_LONG_GUN)->GetItem()) {
        if (BehaviourOf(longGun) == ITEM_BEHAVIOUR_LAUNCHER && m_human->DrawSidearm(2))
            return true;
        if (m_human->DrawLongGun())
            return true;
    }
    return m_human->DrawSidearm(2);
}

bool C_EnemyController::TakeFirearm()
{
    UpdateHandsItem();
    if (m_handsItem && BehaviourOf(m_handsItem) != ITEM_BEHAVIOUR_LAUNCHER)
        return true;
    // The long gun (not a launcher), else the sidearm, into the hands.
    C_Item *longGun = m_human->GetContainer(ITEM_CONTAINER_LONG_GUN)->GetItem();
    if (longGun && BehaviourOf(longGun) != ITEM_BEHAVIOUR_LAUNCHER) {
        const bool ok = m_handsItem
                            ? m_human->RequestTransferItem(m_handsItem, ITEM_CONTAINER_HANDS, m_human, longGun, ITEM_CONTAINER_LONG_GUN)
                            : m_human->RequestMoveItem(longGun, m_human, ITEM_CONTAINER_LONG_GUN, ITEM_CONTAINER_HANDS, 0);
        if (ok)
            return true;
    }
    if (C_Item *sidearm = m_human->FindBeltItem(ITEM_BEHAVIOUR_SIDEARM)) {
        const bool ok = m_handsItem
                            ? m_human->RequestTransferItem(m_handsItem, ITEM_CONTAINER_HANDS, m_human, sidearm, ITEM_CONTAINER_BELT)
                            : m_human->RequestMoveItem(sidearm, m_human, ITEM_CONTAINER_BELT, ITEM_CONTAINER_HANDS, 0);
        if (ok)
            return true;
    }
    return false;
}

bool C_EnemyController::HasLauncherInHands()
{
    return m_handsItem && BehaviourOf(m_handsItem) == ITEM_BEHAVIOUR_LAUNCHER;
}

bool C_EnemyController::TakeLauncher()
{
    C_Item *hands = m_human->GetContainer(ITEM_CONTAINER_HANDS)->GetItem();
    C_Item *longGun = m_human->GetContainer(ITEM_CONTAINER_LONG_GUN)->GetItem();
    if (hands && BehaviourOf(hands) == ITEM_BEHAVIOUR_LAUNCHER)
        return true;
    if (longGun && BehaviourOf(longGun) == ITEM_BEHAVIOUR_LAUNCHER) {
        const bool ok = hands ? m_human->RequestTransferItem(hands, ITEM_CONTAINER_HANDS, m_human, longGun, ITEM_CONTAINER_LONG_GUN)
                              : m_human->RequestMoveItem(longGun, m_human, ITEM_CONTAINER_LONG_GUN, ITEM_CONTAINER_HANDS, 0);
        if (ok)
            return true;
    }
    return false;
}

bool C_EnemyController::HasLauncher()
{
    if (m_handsItem && BehaviourOf(m_handsItem) == ITEM_BEHAVIOUR_LAUNCHER)
        return true;
    C_Item *longGun = m_human->GetContainer(ITEM_CONTAINER_LONG_GUN)->GetItem();
    return longGun && BehaviourOf(longGun) == ITEM_BEHAVIOUR_LAUNCHER;
}

bool IsShieldedInVehicle(const C_Human *human)
{
    C_Actor *vehicle = human ? human->GetVehicle() : nullptr;
    if (!vehicle || (vehicle->GetType() != ACTOR_CAR && vehicle->GetType() != ACTOR_STATIC_WEAPON))
        return false;
    return (vehicle->GetSeatFlags(human->GetVehicleSeat()) & SEAT_FLAG_HIDDEN) != 0;
}

C_Human *C_EnemyController::NearestTargetOnFoot()
{
    C_Human *best = nullptr;
    float bestDistance = 100000.0f;
    const std::vector<C_Human *> &enemies = m_human->m_seenEnemies;
    for (size_t i = enemies.size(); i-- > 0;) {
        C_Human *enemy = enemies[i];
        if (enemy == m_target || IsShieldedInVehicle(enemy))
            continue;
        const float distance = GetDistanceTo(enemy);
        if (distance < bestDistance) {
            bestDistance = distance;
            best = enemy;
        }
    }
    return best;
}

bool C_EnemyController::SeesEnemy(const C_Human *target) const
{
    return target && m_human->HasSeenEnemy(target);
}

void C_EnemyController::TestSightNow(const C_Human *target)
{
    if (target)
        g_sightModule.TestNow(m_human->GetSightId(), target->GetSightTargetId());
}

bool C_EnemyController::SeesEnemies() const
{
    return !m_human->m_seenEnemies.empty();
}

bool C_EnemyController::IsFightingOn() const
{
    return m_task && m_task->type == 5 && (m_task->unknown08 & BROKEN_FIGHT_KIND);
}

C_Human *C_EnemyController::NearestEnemy()
{
    C_Human *best = nullptr;
    float bestDistance2 = 1e+07f;
    const std::vector<C_Human *> &enemies = m_human->m_seenEnemies;
    for (size_t i = enemies.size(); i-- > 0;) {
        const S_vector &own = m_human->GetFrame()->GetWorldPos();
        const S_vector &other = enemies[i]->GetFrame()->GetWorldPos();
        const float dx = other.x - own.x;
        const float dy = other.y - own.y;
        const float dz = other.z - own.z;
        const float distance2 = dy * dy + dx * dx + dz * dz;
        if (distance2 < bestDistance2) {
            best = enemies[i];
            bestDistance2 = distance2;
        }
    }
    return best;
}

bool C_EnemyController::LookAtNearestEnemy()
{
    C_Human *enemy = NearestEnemy();
    if (!enemy)
        return false;
    LookTowardIfAside(WorldPos4Of(enemy->GetBodyFrame()));
    return true;
}

void C_EnemyController::DropHandsItem()
{
    C_Item *item = m_human->GetContainer(ITEM_CONTAINER_HANDS)->GetItem();
    if (item == m_handsItem)
        m_handsItem = nullptr;
    if (item)
        m_human->RequestThrowItem(item, ITEM_CONTAINER_HANDS, XYZ(m_viewDir));
}

// ------------------------------------------------------------------------- firing

bool C_EnemyController::IsLowOnAmmo()
{
    return m_handsItem && m_handsItem->GetAmount() < 10.0f;
}

void C_EnemyController::UpdateFiring(int timeDelta)
{
    const uint32_t before = m_moveFlags;
    UpdateFireOrder(timeDelta);
    if (!(before & MOVE_FIRING) || (m_moveFlags & MOVE_FIRING))
        return;
    // A burst ended: an empty single-shot launcher is thrown away; the next burst waits.
    if (m_handsItem && m_handsItem->GetTypeId() == ITEM_TYPE_SINGLE_LAUNCHER && m_handsItem->GetAmount() == 0.0f) {
        DropHandsItem();
        return;
    }
    m_fireWait = GetFireWait();
    m_lowAmmo = IsLowOnAmmo();
    SetBurstTimes(m_target);
    if (m_task->unknown08 & BROKEN_FIGHT_KIND)
        m_fireOrder->SetPauseTime(200, 100);
}

int C_EnemyController::GetFireWait()
{
    // Near (6..10 m): none; up to 15..20 m: 0.3..0.8 s; farther: 0.7..1.8 s walking, else
    // 20..60 ms a metre.
    if (!m_target)
        return 0;
    const float distance = GetDistanceTo(m_target);
    if (!(static_cast<float>(std::rand()) * (1.0f / 32767.0f) * 4.0f + 6.0f <= distance))
        return 0;
    if (!(static_cast<float>(std::rand()) * (1.0f / 32767.0f) * 5.0f + 15.0f <= distance))
        return static_cast<int>(static_cast<float>(std::rand()) * (1.0f / 32767.0f) * 500.0f + 300.0f);
    if (m_move->flags & S_AIMoveOrder::FLAG_ROUTE)
        return static_cast<int>(static_cast<float>(std::rand()) * (1.0f / 32767.0f) * 1100.0f + 700.0f);
    const float low = distance * 20.0f;
    const float r = static_cast<float>(std::rand()) * (1.0f / 32767.0f);
    return static_cast<int>(r * (distance * 60.0f - low) + low);
}

bool C_EnemyController::NeedsReload()
{
    // The weapon is ready and not full (or a reload is pending).
    if (!m_handsItem || m_handsItem->GetTime() > GetGame()->GetMissionTime())
        return false;
    const float rounds = m_handsItem->GetAmount();
    if (!(m_handsItem->GetMaxAmount() == rounds))
        return true;
    return (static_cast<C_WeaponItem *>(m_handsItem)->GetFlags() & WEAPON_RELOAD_PENDING) != 0;
}

void C_EnemyController::SetBurstTimes(C_Actor *target)
{
    int burst = -1;
    int burstSpread = -1;
    int pause = -1;
    int pauseSpread = -1;
    if (target && m_handsItem) {
        const S_BurstTable *table = nullptr;
        switch (BehaviourOf(m_handsItem)) {
        case ITEM_BEHAVIOUR_SIDEARM: table = &BURST_SIDEARM; break;
        case ITEM_BEHAVIOUR_SMG: table = &BURST_RIFLE; break;
        case ITEM_BEHAVIOUR_RIFLE: table = &BURST_4; break;
        case ITEM_BEHAVIOUR_LMG:
        case ITEM_BEHAVIOUR_ASSAULT: table = &BURST_5; break;
        default: break;
        }
        if (table && table->burstFar != -1) {
            const S_vector d = m_human->GetFrame()->GetWorldPos() - target->GetFrame()->GetWorldPos();
            const float distance2 = d.z * d.z + d.x * d.x + d.y * d.y;
            const S_BurstTable &t = *table;
            if (distance2 <= static_cast<float>(t.burstNear2)) {
                burst = t.burstNear;
            } else if (static_cast<float>(t.burstFar2) <= distance2) {
                burst = t.burstFar;
            } else {
                const float x = (distance2 - static_cast<float>(t.burstNear2)) / static_cast<float>(t.burstFar2 - t.burstNear2);
                burst = static_cast<int>((1.0f - x) * static_cast<float>(t.burstNear - t.burstFar) + static_cast<float>(t.burstFar));
            }
            burstSpread = static_cast<int>(static_cast<float>(burst) * 0.1f);
            if (distance2 <= static_cast<float>(t.pauseNear2)) {
                pause = t.pauseNear;
            } else if (static_cast<float>(t.pauseFar2) <= distance2) {
                pause = t.pauseFar;
            } else {
                const float x = (distance2 - static_cast<float>(t.pauseNear2)) / static_cast<float>(t.pauseFar2 - t.pauseNear2);
                pause = static_cast<int>(x * static_cast<float>(t.pauseFar - t.pauseNear) + static_cast<float>(t.pauseNear));
            }
            pauseSpread = static_cast<int>(static_cast<float>(pause) * 0.1f);
        }
    }
    m_fireOrder->SetBurstTime(burst, burstSpread);
    m_fireOrder->SetPauseTime(pause, pauseSpread);
}

bool C_EnemyController::Attack(int /*timeDelta*/)
{
    // Fighting on (morale broken): at once.
    if (IsFightingOn() && m_target) {
        m_fireOrder->SetTarget(m_target);
        return true;
    }
    // While alert and after the wait, with the weapon raised.
    if (m_alertTime != 0 && m_fireWait == 0) {
        if (!m_weaponRaised) {
            RaiseWeapon();
            return true;
        }
        m_fireOrder->SetTarget(m_target);
        return true;
    }
    if (PlanGrenade()) {
        ThrowGrenade();
        m_grenadeTime = 0;
        return false;
    }
    // Waiting: looking at the target.
    if (m_target && m_fire.unknown0C < 1)
        m_facing->SetView(GetDirToFrame(m_target->GetBodyFrame()));
    m_fireOrder->SetTarget(nullptr);
    return false;
}

bool C_EnemyController::PlanGrenade()
{
    C_Item *grenade = m_human->FindBeltItem(ITEM_BEHAVIOUR_THROWN);
    if (!grenade || m_grenadeTime < 10000 || m_alertTime == 0 || !C_AIController::PlanGrenade())
        return false;
    // Not more often than every 40 s divided by the enemies in view.
    const int count = static_cast<int>(m_human->m_seenEnemies.size());
    if (count != 0 && static_cast<float>(m_grenadeTime) < 40.0f / static_cast<float>(count) * 1000.0f)
        return false;
    // Where the enemies 10..50 m away will be when it lands.
    C_ItemComponent *component = grenade->GetDef()->GetComponent();
    const float flight = GetNearestEnemyDistance() * 1.15f / static_cast<C_ProjectileWeaponComp *>(component)->GetMuzzleVelocity();
    std::vector<S_vector4> points;
    for (int i = count; i-- > 0;) {
        const S_vector4 p = PredictPos(m_human->m_seenEnemies[i], flight);
        const S_vector &own = m_human->GetFrame()->GetWorldPos();
        const float dx = own.x - p.x;
        const float dy = own.y - p.y;
        const float dz = own.z - p.z;
        const float distance2 = dx * dx + dy * dy + dz * dz;
        if (distance2 > 2500.0f || distance2 < 100.0f)
            continue;
        points.push_back(p);
    }
    if (points.size() == 1) {
        m_fire.throwPos = points[0];
        return true;
    }
    // Their middle, the farthest one left out until the rest are within 10 m of it.
    while (!points.empty()) {
        float sx = 0.0f;
        float sy = 0.0f;
        float sz = 0.0f;
        for (size_t i = points.size(); i-- > 0;) {
            sx += points[i].x;
            sy += points[i].y;
            sz += points[i].z;
        }
        const float inverse = 1.0f / static_cast<float>(points.size());
        const S_vector4 middle = {inverse * sx, sy * inverse, sz * inverse, points.back().w};
        int farthest = -1;
        float farthest2 = 0.0f;
        for (size_t i = points.size(); i-- > 0;) {
            const float dx = points[i].x - middle.x;
            const float dy = points[i].y - middle.y;
            const float dz = points[i].z - middle.z;
            const float distance2 = dx * dx + dy * dy + dz * dz;
            if (farthest2 < distance2) {
                farthest = static_cast<int>(i);
                farthest2 = distance2;
            }
        }
        if (farthest2 <= 100.0f) {
            m_fire.throwPos = middle;
            return true;
        }
        points.erase(points.begin() + farthest);
    }
    m_grenadeTime = 0;
    return false;
}

// ------------------------------------------------------------------------- the line of fire

bool C_EnemyController::Unknown114(const S_vector *dir)
{
    // No living friend in view (in front) within its body spheres of the line 200 m along
    // `dir` from the body.
    const S_vector segment = *dir * 200.0f;
    S_vector forward = *dir;
    forward.Normalize();
    const S_vector from = m_human->GetBodyFrame()->GetWorldPos();
    const std::vector<C_Human *> &friends = m_human->m_seenFriends;
    for (size_t i = friends.size(); i-- > 0;) {
        C_Human *other = friends[i];
        if (other->GetHealth() <= 0 || other == m_human)
            continue;
        const S_vector &pos = other->GetFrame()->GetWorldPos();
        S_vector toward(pos.x - from.x, 0.0f, pos.z - from.z);
        toward.Normalize();
        if (!(0.0f < toward.x * forward.x + forward.y * toward.y + forward.z * toward.z))
            continue;
        int count = 0;
        const S_vector4 *spheres = reinterpret_cast<const S_vector4 *>(other->m_body.GetUnknown6E0(&count));
        if (count == 0)
            continue;
        const float length2 = segment.x * segment.x + segment.y * segment.y + segment.z * segment.z;
        for (int k = count; k-- > 0;) {
            const S_vector4 &sphere = spheres[k];
            float distance;
            if (length2 >= 1e-8f) {
                const float ox = from.x - sphere.x;
                const float oy = from.y - sphere.y;
                const float oz = from.z - sphere.z;
                const float t = (ox * segment.x + oy * segment.y + oz * segment.z) / length2;
                const S_vector off(ox - segment.x * t, oy - segment.y * t, oz - segment.z * t);
                distance = off.Magnitude();
            } else {
                distance = -1e16f;
            }
            if (distance < sphere.w)
                return false;
        }
    }
    return true;
}

bool C_EnemyController::IsSurrendered()
{
    return !(m_moveFlags & (MOVE_DEAD | MOVE_UNCONSCIOUS)) && m_task && m_task->type == 5
           && m_task->unknown08 == BROKEN_SURRENDERED;
}

// ------------------------------------------------------------------------- saved games

void C_EnemyController::SaveGame(C_chunk &chunk)
{
    chunk.Begin(CHUNK_ENEMY_AI);
    C_AIController::SaveGame(chunk);
    chunk.Ascend();
    chunk.Begin(CHUNK_ENEMY_STATE);
    chunk.Write(m_lowAmmo);
    chunk.Write(m_unknown154);
    chunk.Write(m_homeCheckpoint);
    chunk.Write(m_taskTime);
    chunk.Write(m_unknown14C);
    chunk.Write(m_unknown150);
    chunk.Write(m_alertRange);
    chunk.Write(m_alertTime);
    chunk.Write(m_grenadeTime);
    chunk.Write(m_fireWait);
    chunk.Write(m_gunCheckTime);
    chunk.Write(m_moraleCheckTime);
    chunk.Write(m_coverTime);
    chunk.Write(m_homeRadius);
    chunk.Write(m_aimHeight);
    chunk.Write(m_enemyFlags);
    // (The morale check and the cover time a second time.)
    chunk.Write(m_moraleCheckTime);
    chunk.Write(m_coverTime);
    chunk.Write(m_unarmedTime);
    chunk.Write(&m_homePos, SAVED_VECTOR_SIZE);
    chunk.Ascend();
    chunk.Begin(CHUNK_ENEMY_TIMES);
    chunk.Write(m_surrenderTime);
    chunk.Write(m_enemySeenTime);
    chunk.Ascend();
    if (m_target)
        WriteActorChunk(chunk, CHUNK_ENEMY_TARGET, m_target);
    if (m_task) {
        if (m_task->type == 2 && m_event) {
            chunk.Begin(CHUNK_ENEMY_EVENT);
            m_event->SaveGame(chunk);
            chunk.Ascend();
        } else if (m_task->type == 1 && m_task->unknown08 == GUARD_TO_GUN && m_gun) {
            WriteActorChunk(chunk, CHUNK_ENEMY_GUN, m_gun);
        }
    }
    chunk.Begin(CHUNK_ENEMY_TARGET_CHANGED);
    chunk.Write(m_targetChanged);
    chunk.Ascend();
}

void C_EnemyController::LoadGame(C_chunk &chunk)
{
    m_target = nullptr;
    m_homeCover.clear();
    uint16_t id;
    while (!chunk.AtEnd() && chunk.Descend(id)) {
        switch (id) {
        case CHUNK_ENEMY_AI:
            C_AIController::LoadGame(chunk);
            break;
        case CHUNK_ENEMY_STATE:
            ReadIfFits(chunk, m_lowAmmo);
            ReadIfFits(chunk, m_unknown154);
            ReadIfFits(chunk, m_homeCheckpoint);
            ReadIfFits(chunk, m_taskTime);
            ReadIfFits(chunk, m_unknown14C);
            ReadIfFits(chunk, m_unknown150);
            ReadIfFits(chunk, m_alertRange);
            chunk.Read(m_alertTime);
            chunk.Read(m_grenadeTime);
            chunk.Read(m_fireWait);
            chunk.Read(m_gunCheckTime);
            chunk.Read(m_moraleCheckTime);
            chunk.Read(m_coverTime);
            chunk.Read(m_homeRadius);
            chunk.Read(m_aimHeight);
            chunk.Read(m_enemyFlags);
            chunk.Read(m_moraleCheckTime);
            chunk.Read(m_coverTime);
            chunk.Read(m_unarmedTime);
            chunk.Read(&m_homePos, SAVED_VECTOR_SIZE);
            break;
        case CHUNK_ENEMY_TARGET:
            m_target = static_cast<C_Human *>(ReadActor(chunk));
            break;
        case CHUNK_ENEMY_EVENT:
            m_event = AllocHumanEvent();
            m_event->LoadGame(chunk);
            break;
        case CHUNK_ENEMY_GUN:
            m_gun = ReadActor(chunk);
            break;
        case CHUNK_ENEMY_SURRENDER_OLD:
            chunk.Read(m_surrenderTime);
            m_enemySeenTime = 0;
            break;
        case CHUNK_ENEMY_TIMES:
            chunk.Read(m_surrenderTime);
            chunk.Read(m_enemySeenTime);
            break;
        case CHUNK_ENEMY_TARGET_CHANGED:
            chunk.Read(m_targetChanged);
            break;
        default:
            break;
        }
        chunk.Ascend();
    }
    // The post's points and guns again (an aggressive human has none).
    if (m_moveFlags & MOVE_AI_AGGRESSIVE) {
        m_homePoints.clear();
        m_homeCover.clear();
        m_guns.clear();
        return;
    }
    FindHomeNeighbours();
    FindHomeCover();
    FindGuns();
}
