// C_SniperController, the sniper AI (game/human_controller.h; original source
// 0x00559a50..0x0055cb50, 0x005a4510..0x005a4540).
#include "game/human_controller.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <deque>
#include <vector>

#include "common/chunk.h"
#include "game/actor.h"
#include "game/actors_module.h"
#include "game/checkpoints.h"
#include "game/controller_internal.h"
#include "game/enemy_internal.h"
#include "game/human.h"
#include "game/human_movement.h"
#include "game/items.h"
#include "game/pending.h"
#include "game/soldier.h"
#include "ls3d/i3d.h"
#include "script/script.h"

namespace {

// The saved game's chunks.
enum : uint16_t {
    CHUNK_SNIPER_AI = 0x139D,                           // the AI base's
    CHUNK_SNIPER_STATE = 0x13BB,
    CHUNK_SNIPER_TARGET = 0x13BC,                       // actor id
    CHUNK_SNIPER_POINTS = 0x13BD,
    CHUNK_SNIPER_SNIPER_POINTS = 0x13BE,
};
constexpr int SAVED_VECTOR_SIZE = 12;
// The item type of a single-shot launcher, thrown away when empty (inferred).
constexpr int ITEM_TYPE_SINGLE_LAUNCHER = 0x26;
// The paces (S_AIMoveOrder::unknown54).
constexpr uint32_t PACE_TO_POINT = MOVE_PACE_4 | MOVE_AI_AGGRESSIVE;   // 0x4100 (as in the original)
constexpr uint32_t PACE_TO_FIRE_POINT = MOVE_PACE_4;                    // 0x4000

template <class T>
void ReadIfFits(C_chunk &chunk, T &value)
{
    if (chunk.RemainingSize() >= static_cast<int>(sizeof(T)))
        chunk.Read(value);
}

// A bell-shaped random number 0..1: four quarter rand()s summed.
float RandBell()
{
    int sum = std::rand() >> 2;
    sum += std::rand() >> 2;
    sum += std::rand() >> 2;
    sum += std::rand() >> 2;
    return static_cast<float>(sum) * (1.0f / 32767.0f);
}

// ms the sniper waits, around `middle` (`spread` wide).
int BellTime(float spread, float middle)
{
    return static_cast<int>((RandBell() - 0.5f) * spread + middle);
}

int IndexOf(const C_Checkpoints *checkpoints, const S_Checkpoint *point)
{
    const int index = checkpoints->GetIndex(point);
    return index >= checkpoints->GetCount() || index < 0 ? CHECKPOINT_NONE : index;
}

}  // namespace

// ------------------------------------------------------------------------- life cycle

void C_SniperController::Attach(C_Human *human, C_HumanController *previous)
{
    C_AIController::Attach(human, previous);
    m_sniperFlags = 0;
    m_task = nullptr;
    m_target = nullptr;
    m_startStance = m_moveFlags & MOVE_STANCE_MASK;
    m_homePos = WorldPos4Of(m_human->GetFrame());
    if (previous) {
        // The post: the walking points within the sniper radius; the sniper points among them.
        const int nearest = m_checkpoints->FindNearest(XYZ(m_events->GetSniperPos()), 5.0f, -1.0f);
        m_checkpoints->GetNeighbours(nearest, m_events->GetSniperRadius(), &m_points);
        for (size_t i = m_points.size(); i-- > 0;) {
            if (m_checkpoints->Get(m_points[i])->type == CHECKPOINT_TYPE_SNIPER)
                m_sniperPoints.push_back(m_points[i]);
        }
        // A weapon into the hands: the long gun, else the sidearm.
        if (!m_human->GetContainer(ITEM_CONTAINER_HANDS)->IsLocked() && !m_handsItem && !m_human->DrawLongGun())
            m_human->DrawSidearm(2);
        const S_FrameData *data = m_human->GetFrame()->GetData();
        if (!data || !data->script || !data->script->HasOnAlarmDone())
            m_sniperFlags |= FLAG_NO_ALARM_DONE;
        if (m_human->m_initialStats[STAT_MORALE] > 90)
            m_sniperFlags |= FLAG_BRAVE;
        if (m_human->m_initialStats[STAT_MORALE] > 75)
            m_sniperFlags |= FLAG_STEADY;
    }
    if (!(m_moveFlags & MOVE_AI_MODE_MASK))
        m_moveFlags |= MOVE_AI_DEFENSIVE;
}

void C_SniperController::Detach()
{
    C_AIController::Detach();
    m_human = nullptr;
    m_points.clear();
}

void C_SniperController::Update(int frameTime)
{
    if (!(m_moveFlags & (MOVE_DEAD | MOVE_UNCONSCIOUS)))
        UpdateNetwork(frameTime);
    FlushMessages(true);
}

void C_SniperController::Tick(int timeDelta)
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
        ApplyAlertState();
        UpdateMove(timeDelta);
        UpdateLook(timeDelta);
        UpdateFiring(timeDelta);
        ApplyMoveControls();
        ApplyWeaponControls(timeDelta, 0);
        FlushMessages(true);
        return;
    }
    // No task: back to the script.
    FlushMessages(true);
    m_human->StartScriptedController();
}

void C_SniperController::UpdateTimers(int timeDelta)
{
    C_AIController::UpdateTimers(timeDelta);
    // A target that is gone.
    if (m_target && g_actorsModule.GetActor(m_target->GetNetId()) != m_target)
        m_target = nullptr;
    if (m_task) {
        const int left = m_task->unknown10 - timeDelta;
        m_task->unknown0C += timeDelta;
        m_task->unknown10 = left < 0 ? 0 : left;
    }
    std::fill(std::begin(g_aiAxes), std::end(g_aiAxes), 0.0f);
    std::fill(std::begin(g_aiButtons), std::end(g_aiButtons), 0.0f);
    m_axes = g_aiAxes;
    m_buttons = g_aiButtons;
}

void C_SniperController::DropHandsItem()
{
    C_Item *item = m_human->GetContainer(ITEM_CONTAINER_HANDS)->GetItem();
    if (item == m_handsItem)
        m_handsItem = nullptr;
    if (item)
        m_human->RequestThrowItem(item, ITEM_CONTAINER_HANDS, GetViewDir());
}

void C_SniperController::UpdateFiring(int timeDelta)
{
    // A burst ended: an empty single-shot launcher is thrown away.
    const uint32_t before = m_moveFlags;
    UpdateFireOrder(timeDelta);
    if ((before & MOVE_FIRING) && !(m_moveFlags & MOVE_FIRING) && m_handsItem
        && m_handsItem->GetTypeId() == ITEM_TYPE_SINGLE_LAUNCHER && m_handsItem->GetAmount() == 0.0f)
        DropHandsItem();
}

// ------------------------------------------------------------------------- events

S_HumanEvent *C_SniperController::HandleEvent(S_HumanEvent *event)
{
    switch (event->type) {
    case AI_EVENT_ENEMY_LOST:
        m_events->AddStimulus(30000, event, false);
        return nullptr;
    case AI_EVENT_SOUND_0:
    case AI_EVENT_SOUND_2:
        // A sound of a source not in view: looks at it, and keeps it as a stimulus.
        if (!IdToActor(event->sourceId) || m_human->HasSeenEnemy(static_cast<C_Human *>(IdToActor(event->sourceId))))
            return event;
        m_facing->SetLookAt(static_cast<C_Human *>(IdToActor(event->sourceId))->GetBodyFrame());
        m_events->AddStimulus(20000, event, false);
        return nullptr;
    case AI_EVENT_HIT_5:
        // Hit: the burst goes on 0.1..0.4 s longer.
        ExtendBurst(static_cast<uint32_t>(static_cast<int>(Rand01() * 300.0f + 100.0f)));
        if (IdToActor(event->sourceId) && !m_human->HasSeenEnemy(static_cast<C_Human *>(IdToActor(event->sourceId))))
            m_facing->SetLookAt(static_cast<C_Human *>(IdToActor(event->sourceId))->GetBodyFrame());
        [[fallthrough]];
    case AI_EVENT_SHOT_AT:
    case AI_EVENT_LOUD_SOUND:
        // Looks at a shooter not in view.
        if (IdToActor(event->sourceId) && !m_human->HasSeenEnemy(static_cast<C_Human *>(IdToActor(event->sourceId))))
            m_facing->SetLookAt(static_cast<C_Human *>(IdToActor(event->sourceId))->GetBodyFrame());
        return event;
    default:
        return event;
    }
}

// ------------------------------------------------------------------------- the task

S_AITask *C_SniperController::UpdateTask(int timeDelta)
{
    m_taskTime += timeDelta;
    if (!m_task) {
        StartTask();
        return m_task;
    }
    m_human->ProcessEvents();
    // Low morale: nothing more (the original calls an empty function here, 0x0055b6a0).
    if (m_task->type != 5 && !(m_sniperFlags & FLAG_BRAVE) && m_human->GetStat(STAT_MORALE) < 20)
        return m_task;
    if (m_task->type == 5 && m_human->GetStat(STAT_MORALE) > 40) {
        StartTask();
        return m_task;
    }
    CheckDone(timeDelta);
    return m_task;
}

void C_SniperController::CheckDone(int /*timeDelta*/)
{
    if ((m_sniperFlags & FLAG_DONE) && !(m_sniperFlags & FLAG_NO_ALARM_DONE))
        ReleaseTask();
}

void C_SniperController::RunTask(int timeDelta)
{
    // (A task of type 5, which nothing gives the sniper, runs an empty function, 0x0055b6b0.)
    if (!m_task || m_task->type != 3)
        return;
    if (m_moveFlags & MOVE_AI_ZOMBIE)
        UpdateZombie(timeDelta);
    else
        UpdateSniper(timeDelta);
}

void C_SniperController::StartTask()
{
    ReleaseTask();
    const S_vector4 zero = {0.0f, 0.0f, 0.0f, 0.0f};    // (w: a stale local in the original)
    m_task = AllocTask3(SNIPER_DECIDE, nullptr, zero);
    m_taskTime = 0;
}

void C_SniperController::UpdateZombie(int /*timeDelta*/)
{
    // From where it stands: fires at the enemies in view, turns to what was heard.
    S_AITask *task = m_task;
    if (task->unknown08 == SNIPER_IDLE) {
        // Nothing for 5..11 s: back to the script.
        if (HasSomethingToDo())
            task->unknown08 = SNIPER_DECIDE;
        else if (task->unknown10 == 0)
            m_sniperFlags |= FLAG_DONE;
    }
    if (task->unknown08 == SNIPER_DECIDE) {
        if (!HasSomethingToDo()) {
            task->unknown08 = SNIPER_IDLE;
            task->unknown10 = BellTime(6000.0f, 8000.0f);
        } else if (SeesEnemies()) {
            const std::vector<C_Human *> &enemies = m_human->m_seenEnemies;
            m_target = enemies.size() == 1 ? enemies[0] : PickTarget(nullptr);
            m_alert->Set(2);
            task->unknown08 = SNIPER_FIRE;
        } else {
            m_target = static_cast<C_Human *>(TakeStimulusSource());
            task->unknown08 = SNIPER_TURN;
            task->unknown10 = 1000;
            m_alert->Set(0);
        }
    }
    if (task->unknown08 == SNIPER_TURN) {
        // Turns toward the source (looking for it when it is ahead) for a second.
        if (m_target && !SeesTarget() && m_target->GetHealth() > 0 && task->unknown10 != 0) {
            const S_matrix &m = m_human->GetBodyFrame()->GetMatrix();
            S_vector body(m.m[2][0], m.m[2][1], m.m[2][2]);
            NormalizeBy(body, body.y * body.y + body.z * body.z + body.x * body.x);
            const S_vector &own = m_human->GetFrame()->GetWorldPos();
            const S_vector &pos = m_target->GetFrame()->GetWorldPos();
            S_vector to(pos.x - own.x, pos.y - own.y, pos.z - own.z);
            NormalizeBy(to, to.x * to.x + to.y * to.y + to.z * to.z);
            // (The body's direction normalized a second time, as in the original.)
            NormalizeBy(body, body.y * body.y + body.z * body.z + body.x * body.x);
            if (body.z * to.z + body.y * to.y + body.x * to.x <= 0.76f) {
                if (m_facing->LookToward({to.x, to.y, to.z, 0.0f}))
                    task->unknown08 = SNIPER_DECIDE;
            } else {
                TestSightOfTarget();
                task->unknown08 = SNIPER_DECIDE;
            }
        } else {
            task->unknown08 = SNIPER_DECIDE;
        }
    }
    if (task->unknown08 == SNIPER_FIRE) {
        if (m_target && m_target->GetHealth() > 0 && SeesTarget()) {
            m_fireOrder->SetTarget(m_target);
            return;
        }
        m_fireOrder->SetTarget(nullptr);
        task->unknown08 = SNIPER_DECIDE;
        m_alert->Set(0);
    }
}

void C_SniperController::UpdateSniper(int /*timeDelta*/)
{
    // From its sniper points: to the one to fire at the target from, then fires 3..5 s.
    S_AITask *task = m_task;
    if (task->unknown08 == SNIPER_IDLE) {
        // Nothing for 12..20 s: back to the script.
        if (HasSomethingToDo())
            task->unknown08 = SNIPER_DECIDE;
        else if (task->unknown10 == 0)
            m_sniperFlags |= FLAG_DONE;
    }
    if (task->unknown08 == SNIPER_AT_POINT && task->unknown10 == 0)
        task->unknown08 = SNIPER_DECIDE;
    if (task->unknown08 == SNIPER_TO_POINT) {
        // To the post's point with the most cover: there already, 5..13 s; else it walks there
        // and waits 3 s.
        const int point = FindCoverPoint();
        if (point == CHECKPOINT_NONE) {
            task->unknown08 = SNIPER_DECIDE;
        } else {
            if (IndexOf(m_checkpoints, m_currentCheckpoint) == point) {
                TakeCoverStance(m_currentCheckpoint);
                task->unknown10 = BellTime(8000.0f, 9000.0f);
            } else {
                TakeCoverStance(m_checkpoints->Get(point));
                m_move->unknown54 = PACE_TO_POINT;
                m_move->SetCheckpoint(static_cast<int16_t>(point));
                // (A random number times 0.)
                task->unknown10 = static_cast<int>(Rand01() * 0.0f + 3000.0f);
            }
            task->unknown08 = SNIPER_AT_POINT;
        }
    }
    if (task->unknown08 == SNIPER_DECIDE) {
        if (!HasSomethingToDo()) {
            task->unknown08 = SNIPER_IDLE;
            task->unknown10 = BellTime(8000.0f, 16000.0f);
        } else {
            int point;
            if (SeesEnemies()) {
                m_target = PickTarget(nullptr);
                point = FindFirePointSeen();
            } else {
                m_target = static_cast<C_Human *>(TakeStimulusSource());
                point = FindFirePointHeard();
            }
            if (point != IndexOf(m_checkpoints, m_currentCheckpoint) && point != CHECKPOINT_NONE) {
                ChooseStance(m_checkpoints->Get(point));
                m_move->unknown54 = PACE_TO_FIRE_POINT;
                m_move->SetCheckpoint(static_cast<int16_t>(point));
                task->unknown08 = SNIPER_MOVING;
                return;
            }
            ChooseStance(m_currentCheckpoint);
            if (SeesTarget()) {
                m_alert->Set(2);
                task->unknown08 = SNIPER_FIRE;
                task->unknown10 = BellTime(2000.0f, 4000.0f);
            } else {
                m_alert->Set(0);
                task->unknown08 = SNIPER_TURN;
                task->unknown10 = 1000;
            }
        }
    }
    if (task->unknown08 == SNIPER_MOVING && (m_move->flags & S_AIMoveOrder::FLAG_DONE)) {
        if (SeesTarget()) {
            m_alert->Set(2);
            task->unknown08 = SNIPER_FIRE;
            task->unknown10 = BellTime(2000.0f, 4000.0f);
        } else {
            m_alert->Set(0);
            task->unknown08 = SNIPER_TURN;
            task->unknown10 = 1000;
        }
    }
    if (task->unknown08 == SNIPER_TURN) {
        // Turns toward the target (looking for it when it is ahead) for a second.
        if (m_target && m_target->GetHealth() > 0 && !SeesTarget() && task->unknown10 != 0) {
            const S_matrix &m = m_human->GetBodyFrame()->GetMatrix();
            S_vector body(m.m[2][0], m.m[2][1], m.m[2][2]);
            NormalizeBy(body, body.x * body.x + body.y * body.y + body.z * body.z);
            const S_vector &own = m_human->GetFrame()->GetWorldPos();
            const S_vector &pos = m_target->GetFrame()->GetWorldPos();
            S_vector to(pos.x - own.x, pos.y - own.y, pos.z - own.z);
            NormalizeBy(to, to.x * to.x + to.y * to.y + to.z * to.z);
            // (The body's direction normalized a second time, as in the original.)
            NormalizeBy(body, body.x * body.x + body.y * body.y + body.z * body.z);
            if (body.x * to.x + body.z * to.z + body.y * to.y <= 0.76f) {
                if (m_facing->LookToward({to.x, to.y, to.z, 0.0f}))
                    task->unknown08 = SNIPER_DECIDE;
            } else {
                TestSightOfTarget();
                task->unknown08 = SNIPER_DECIDE;
            }
        } else {
            task->unknown08 = SNIPER_DECIDE;
        }
    }
    if (task->unknown08 == SNIPER_FIRE) {
        if (!m_target || m_target->GetHealth() < 1 || !SeesTarget()) {
            m_fireOrder->SetTarget(nullptr);
            task->unknown08 = SNIPER_DECIDE;
            m_alert->Set(0);
        } else {
            m_fireOrder->SetTarget(m_target);
        }
        if (task->unknown10 == 0) {
            task->unknown08 = SNIPER_DECIDE;
            m_alert->Set(0);
        }
    }
}

// ------------------------------------------------------------------------- helpers

void C_SniperController::TestSightOfTarget()
{
    if (m_target)
        g_sightModule.TestNow(m_human->GetSightId(), m_target->GetSightTargetId());
}

void C_SniperController::TakeCoverStance(const S_Checkpoint *cp)
{
    if (!cp)
        return;
    const std::vector<C_Human *> &enemies = m_human->m_seenEnemies;
    // Standing in cover from every enemy in view: nothing to do.
    bool covered = true;
    for (size_t i = enemies.size(); i-- > 0;) {
        const S_vector &pos = enemies[i]->GetFrame()->GetWorldPos();
        const S_vector dir(pos.x - cp->pos.x, pos.y - cp->pos.y, pos.z - cp->pos.z);
        if (!cp->GetCoverStances(S_Checkpoint::GetDirectionBit(dir), 4)) {
            covered = false;
            break;
        }
    }
    if (covered)
        return;
    // Crouching in cover from them all: a standing human crouches.
    for (size_t i = enemies.size(); i-- > 0;) {
        const S_vector &pos = enemies[i]->GetFrame()->GetWorldPos();
        const S_vector dir(pos.x - cp->pos.x, pos.y - cp->pos.y, pos.z - cp->pos.z);
        if (!(cp->cover2 & S_Checkpoint::GetDirectionBit(dir)))
            return;
    }
    if (m_moveFlags & MOVE_STANDING)
        SetStance(MOVE_CROUCHING);
}

bool C_SniperController::HasSomethingToDo()
{
    return !m_human->m_seenEnemies.empty() || !m_events->GetStimuli().empty();
}

bool C_SniperController::SeesEnemies()
{
    return !m_human->m_seenEnemies.empty();
}

bool C_SniperController::SeesTarget()
{
    if (!m_target)
        return false;
    const std::vector<C_Human *> &enemies = m_human->m_seenEnemies;
    for (size_t i = enemies.size(); i-- > 0;) {
        if (enemies[i] == m_target)
            return true;
    }
    return false;
}

C_Actor *C_SniperController::TakeStimulusSource()
{
    std::deque<S_HumanEvent *> &stimuli = m_events->GetStimuli();
    if (stimuli.empty())
        return nullptr;
    S_HumanEvent *oldest = stimuli.front();
    C_Actor *source = IdToActor(oldest->sourceId);
    ReleaseHumanEvent(oldest);
    stimuli.pop_front();
    return source;
}

void C_SniperController::ChooseStance(const S_Checkpoint *cp)
{
    // Crouching where the point does not cover that from the target, else standing where it
    // does not cover standing.
    if (!m_target || !cp)
        return;
    const S_vector &pos = m_target->GetFrame()->GetWorldPos();
    const S_vector dir(pos.x - cp->pos.x, pos.y - cp->pos.y, pos.z - cp->pos.z);
    if (!cp->GetCoverStances(S_Checkpoint::GetDirectionBit(dir), 2)) {
        SetStance(MOVE_CROUCHING);
        return;
    }
    if (!(cp->cover4 & S_Checkpoint::GetDirectionBit(dir)))
        SetStance(MOVE_STANDING);
}

int C_SniperController::FindCoverPoint()
{
    // The post's point with the most cover from the enemies in view: standing cover 3 (the
    // weight of nearness, 30 / distance squared, rounded to an int: 0 from 6 m on, counts it
    // as 2 or 1 then), crouching 2, lying 1.
    if (m_points.empty() || !m_checkpoints)
        return CHECKPOINT_NONE;
    std::vector<int> scores(m_points.size(), 0);
    const std::vector<C_Human *> &enemies = m_human->m_seenEnemies;
    for (size_t i = m_points.size(); i-- > 0;) {
        const S_Checkpoint *cp = m_checkpoints->Get(m_points[i]);
        const S_vector &own = m_human->GetFrame()->GetWorldPos();
        const S_vector d(cp->pos.x - own.x, cp->pos.y - own.y, cp->pos.z - own.z);
        const float distance2 = d.z * d.z + d.y * d.y + d.x * d.x;
        const int weight = static_cast<int>(distance2 > 0.0f ? 30.0f / distance2 : 30.0f);
        for (size_t k = enemies.size(); k-- > 0;) {
            const S_vector dir = enemies[k]->GetFrame()->GetWorldPos() - cp->pos;
            const uint8_t cover = cp->GetCoverStances(S_Checkpoint::GetDirectionBit(dir), 7);
            int score;
            if ((cover & 4) * weight != 0)
                score = 3;
            else if (cover & 2)
                score = 2;
            else
                score = cover & 1;
            scores[i] += score;
        }
    }
    int best = CHECKPOINT_NONE;
    int bestScore = 0;
    for (size_t i = scores.size(); i-- > 0;) {
        if (bestScore < scores[i]) {
            best = m_points[i];
            bestScore = scores[i];
        }
    }
    return best;
}

int C_SniperController::FindFirePointHeard()
{
    // The sniper point to fire at the target (heard) from: cover from it crouching 3, lying 2,
    // none 0, standing 1 (unless the weight of nearness and distance to the target is 0).
    if (m_sniperPoints.empty() || !m_checkpoints || !m_target)
        return CHECKPOINT_NONE;
    std::vector<int> scores(m_sniperPoints.size(), 0);
    for (size_t i = m_sniperPoints.size(); i-- > 0;) {
        const S_Checkpoint *cp = m_checkpoints->Get(m_sniperPoints[i]);
        const S_vector &own = m_human->GetFrame()->GetWorldPos();
        const S_vector d(cp->pos.x - own.x, cp->pos.y - own.y, cp->pos.z - own.z);
        const float distance2 = d.z * d.z + d.y * d.y + d.x * d.x;
        const S_vector &pos = m_target->GetFrame()->GetWorldPos();
        const S_vector t(cp->pos.x - pos.x, cp->pos.y - pos.y, cp->pos.z - pos.z);
        const float nearness = distance2 > 1.0f ? 30.0f / distance2 : 30.0f;
        const float weight = (t.z * t.z + t.y * t.y + t.x * t.x) * 0.5f + nearness;
        const uint32_t cover = CoverAgainst(cp, pos, 7);
        int score;
        if (static_cast<float>(cover & 4) * weight == 0.0f)
            score = (cover & 2) ? 3 : static_cast<int>(cover & 1) * 2;
        else
            score = 1;
        scores[i] += score;
    }
    int best = CHECKPOINT_NONE;
    int bestScore = -1;
    for (size_t i = scores.size(); i-- > 0;) {
        if (bestScore < scores[i]) {
            best = m_sniperPoints[i];
            bestScore = scores[i];
        }
    }
    return best;
}

int C_SniperController::FindFirePointSeen()
{
    // As FindFirePointHeard, the cover summed over the enemies in view.
    if (m_sniperPoints.empty() || !m_checkpoints || !m_target)
        return CHECKPOINT_NONE;
    std::vector<int> scores(m_sniperPoints.size(), 0);
    const std::vector<C_Human *> &enemies = m_human->m_seenEnemies;
    for (size_t i = m_sniperPoints.size(); i-- > 0;) {
        const S_Checkpoint *cp = m_checkpoints->Get(m_sniperPoints[i]);
        const S_vector &own = m_human->GetFrame()->GetWorldPos();
        const S_vector d(cp->pos.x - own.x, cp->pos.y - own.y, cp->pos.z - own.z);
        const float distance2 = d.z * d.z + d.y * d.y + d.x * d.x;
        const S_vector &pos = m_target->GetFrame()->GetWorldPos();
        const S_vector t(cp->pos.x - pos.x, cp->pos.y - pos.y, cp->pos.z - pos.z);
        const float nearness = distance2 > 1.0f ? 30.0f / distance2 : 30.0f;
        const float weight = (t.z * t.z + t.y * t.y + t.x * t.x) * 0.5f + nearness;
        for (size_t k = enemies.size(); k-- > 0;) {
            const uint32_t cover = CoverAgainst(cp, enemies[k]->GetFrame()->GetWorldPos(), 7);
            int score;
            if (static_cast<float>(cover & 4) * weight == 0.0f)
                score = (cover & 2) ? 3 : static_cast<int>(cover & 1) * 2;
            else
                score = 1;
            scores[i] += score;
        }
    }
    int best = CHECKPOINT_NONE;
    int bestScore = -1;
    for (size_t i = scores.size(); i-- > 0;) {
        if (bestScore < scores[i]) {
            best = m_sniperPoints[i];
            bestScore = scores[i];
        }
    }
    return best;
}

// ------------------------------------------------------------------------- saved games

void C_SniperController::SaveGame(C_chunk &chunk)
{
    chunk.Begin(CHUNK_SNIPER_AI);
    C_AIController::SaveGame(chunk);
    chunk.Ascend();
    chunk.Begin(CHUNK_SNIPER_STATE);
    chunk.Write(&m_homePos, SAVED_VECTOR_SIZE);
    chunk.Write(m_startStance);
    chunk.Write(m_sniperFlags);
    chunk.Write(m_taskTime);
    chunk.Ascend();
    if (m_target) {
        const int id = m_target->GetNetId();
        chunk.WriteChunk(CHUNK_SNIPER_TARGET, &id, 4);
    }
    if (!m_points.empty()) {
        chunk.Begin(CHUNK_SNIPER_POINTS);
        for (size_t i = 0; i < m_points.size(); ++i)
            chunk.Write(&m_points[i], 2);
        chunk.Ascend();
    }
    if (!m_sniperPoints.empty()) {
        chunk.Begin(CHUNK_SNIPER_SNIPER_POINTS);
        for (size_t i = 0; i < m_sniperPoints.size(); ++i)
            chunk.Write(&m_sniperPoints[i], 2);
        chunk.Ascend();
    }
}

void C_SniperController::LoadGame(C_chunk &chunk)
{
    m_target = nullptr;
    m_points.clear();
    m_sniperPoints.clear();
    uint16_t id;
    while (!chunk.AtEnd() && chunk.Descend(id)) {
        switch (id) {
        case CHUNK_SNIPER_AI:
            C_AIController::LoadGame(chunk);
            break;
        case CHUNK_SNIPER_STATE:
            chunk.Read(&m_homePos, SAVED_VECTOR_SIZE);
            ReadIfFits(chunk, m_startStance);
            ReadIfFits(chunk, m_sniperFlags);
            ReadIfFits(chunk, m_taskTime);
            break;
        case CHUNK_SNIPER_TARGET: {
            int actorId = 0;
            chunk.Read(actorId);
            C_Actor *actor = g_actorsModule.GetActorById(actorId);
            if (actor && actor->GetType() == ACTOR_HUMAN)
                m_target = static_cast<C_Human *>(actor);
            break;
        }
        case CHUNK_SNIPER_POINTS:
            while (chunk.RemainingSize() > 0) {
                uint16_t point = 0;
                chunk.Read(&point, 2);
                m_points.push_back(point);
            }
            break;
        case CHUNK_SNIPER_SNIPER_POINTS:
            while (chunk.RemainingSize() > 0) {
                uint16_t point = 0;
                chunk.Read(&point, 2);
                m_sniperPoints.push_back(point);
            }
            break;
        default:
            break;
        }
        chunk.Ascend();
    }
}
