// C_Human: sight and hearing - the humans in view (sorted into enemies, friends, disguised
// enemies and bodies by the sight module's events), remembered sounds and the human event
// queue (original source 0x005a4840..0x005a5960, 0x005a5d50, 0x005a7f50..0x005a7fa0,
// 0x005a83f0..0x005a8480).
#include "game/human.h"

#include <windows.h>
#include <mmsystem.h>

#include "game/actors_module.h"
#include "game/human_body.h"
#include "game/human_internal.h"
#include "game/pending.h"
#include "game/sound_module.h"
#include "game/sound_table.h"
#include "game/world.h"

namespace {

// Sounds are remembered this long.
constexpr int SOUND_MEMORY_TIME = 3000;
// Kinds of sight module targets (C_SightTarget::GetKind()).
enum : int {
    SIGHT_KIND_HUMAN = 0,
    SIGHT_KIND_MELEE_TRACKER = 1,
};

bool Contains(const std::vector<C_Human *> &list, const C_Human *human)
{
    for (size_t i = list.size(); i-- > 0;) {
        if (list[i] == human)
            return true;
    }
    return false;
}

// Erases every occurrence; returns how many there were.
int EraseAll(std::vector<C_Human *> &list, const C_Human *human)
{
    int count = 0;
    for (size_t i = list.size(); i-- > 0;) {
        if (list[i] == human) {
            list.erase(list.begin() + static_cast<std::ptrdiff_t>(i));
            ++count;
        }
    }
    return count;
}

}  // namespace

// ------------------------------------------------------------------- humans in view

void C_Human::AddSeenBody(C_Human *human)
{
    if (Contains(m_seenBodies, human))
        return;
    m_seenBodies.push_back(human);
    m_controller->OnBodySeen(human);
}

void C_Human::AddSeenEnemy(C_Human *human)
{
    if (Contains(m_seenEnemies, human))
        return;
    m_seenEnemies.push_back(human);
    m_controller->OnEnemySeen(human);
}

void C_Human::AddSeenFriend(C_Human *human)
{
    if (!Contains(m_seenFriends, human))
        m_seenFriends.push_back(human);
}

void C_Human::AddSeenTraitor(C_Human *human)
{
    if (!Contains(m_seenTraitors, human))
        m_seenTraitors.push_back(human);
}

void C_Human::RemoveSeenTraitor(C_Human *human)
{
    EraseAll(m_seenTraitors, human);
}

void C_Human::RemoveSeenFriend(C_Human *human)
{
    EraseAll(m_seenFriends, human);
}

void C_Human::RemoveSeenEnemy(C_Human *human)
{
    for (size_t i = m_seenEnemies.size(); i-- > 0;) {
        if (m_seenEnemies[i] == human) {
            m_seenEnemies.erase(m_seenEnemies.begin() + static_cast<std::ptrdiff_t>(i));
            m_controller->OnEnemyLost(human);
        }
    }
}

bool C_Human::HasSeenEnemy(const C_Human *human) const
{
    return Contains(m_seenEnemies, human);
}

void C_Human::OnSightEvent(int lost, int targetId)
{
    if (!m_controller)
        return;
    // Deaf humans do not take in new sights (only lose them).
    if (lost != 1 && (m_controller->GetMoveFlags() & MOVE_DEAF) != 0)
        return;
    C_SightTarget *target = g_sightModule.GetTarget(targetId);
    // Fix: the original uses the target without checking that it still exists.
    if (!target)
        return;
    if (target->GetKind() == SIGHT_KIND_HUMAN) {
        C_Human *other = static_cast<C_Human *>(target->GetActor());
        if (other == this)
            return;
        if (lost == 0) {
            if (other->m_stats[STAT_HEALTH] < 1)
                AddSeenBody(other);
            else if (IsHostileTo(other))
                AddSeenEnemy(other);
            else if (IsDisguisedEnemy(other))
                AddSeenTraitor(other);
            else
                AddSeenFriend(other);
            return;
        }
        RemoveSeenEnemy(other);
        RemoveSeenTraitor(other);
        RemoveSeenFriend(other);
    } else if (target->GetKind() == SIGHT_KIND_MELEE_TRACKER) {
        // The player's soldiers report the meleeTrackers they see.
        if (m_role != HUMAN_ROLE_PLAYER && GetControllerType() != HUMAN_CONTROLLER_SQUAD)
            return;
        C_MeleeTracker *meleeTracker = static_cast<C_MeleeTracker *>(target->GetActor());
        if (meleeTracker->IsAlive())
            g_teamManager.GetPlayerTeam()->AddSeenMeleeTracker(static_cast<C_MeleeTracker *>(target->GetActor()));
    }
}

void C_Human::UpdateSeenHumans()
{
    if (m_stats[STAT_HEALTH] <= 0)
        return;
    // Targets whose classification changed are taken out of view and seen again.
    std::vector<int> changed;
    for (size_t i = m_seenEnemies.size(); i-- > 0;) {
        C_Human *enemy = m_seenEnemies[i];
        if (enemy->m_stats[STAT_HEALTH] < 1)
            changed.push_back(enemy->m_sightTargetId);
    }
    for (size_t i = m_seenTraitors.size(); i-- > 0;) {
        C_Human *traitor = m_seenTraitors[i];
        if (traitor->m_stats[STAT_HEALTH] < 1) {
            changed.push_back(traitor->m_sightTargetId);
        } else if (IsHostileTo(traitor)) {
            changed.push_back(traitor->m_sightTargetId);
        } else {
            // Watching a disguised enemy wears his disguise down.
            bool visible = false;
            g_sightModule.CanSee(m_sightId, traitor->m_sightTargetId, nullptr, &visible);
            if (visible)
                traitor->UpdateDisguise(-1.0f);
        }
    }
    for (size_t i = changed.size(); i-- > 0;) {
        OnSightEvent(1, changed[i]);
        OnSightEvent(0, changed[i]);
    }
}

// ----------------------------------------------------------------------------- sounds

void C_Human::ForgetOldSounds()
{
    const uint32_t now = timeGetTime();
    for (size_t i = m_heardSounds.size(); i-- > 0;) {
        S_RememberedSound *sound = m_heardSounds[i];
        if (static_cast<int>(now - sound->time) > SOUND_MEMORY_TIME) {
            if (sound)
                FreeRememberedSound(sound);
            m_heardSounds.erase(m_heardSounds.begin() + static_cast<std::ptrdiff_t>(i));
        }
    }
}

void __stdcall C_Human::OnSoundHeard(const S_HeardSound *sound)
{
    I3D_frame *listener = sound->listener;
    if (!listener) {
        OutputDebugStringA("EVENT SOUNT WITHOUT HUMAN and FRAME!!!");
        return;
    }
    C_Human *human = nullptr;
    if (S_FrameData *data = listener->GetData())
        human = static_cast<C_Human *>(data->actor);
    if (!human) {
        const C_fstr text("EVENT SOUNT WITHOUT HUMAN (%s)!!!", listener->GetName());
        OutputDebugStringA(text);
        return;
    }
    C_Actor *source = sound->source;
    if (human == source)
        return;
    C_HumanController *controller = human->m_controller;
    if ((controller->GetMoveFlags() & MOVE_DEAF) != 0)
        return;
    // Right after making noise themselves, humans may miss quieter sounds.
    if (controller->GetQuietTime() < 251 && sound->intensity < RandFloat() * 0.7f)
        return;
    const S_vector delta = sound->pos - human->m_frame->GetWorldPos();
    if (static_cast<float>(human->m_stats[STAT_HEARING_RANGE]) < delta.Magnitude())
        return;

    switch (sound->chapter) {
    case SOUND_CHAPTER_PUPPET_MOVEMENT:
        // Footsteps of friends, and any while sitting in a car, go unheard.
        if (human->IsFriendOf(source))
            return;
        if (human->m_vehicle && human->m_vehicle->GetType() == ACTOR_CAR)
            return;
        // (The original also queries the source's movement class here and discards it.)
        break;
    case SOUND_CHAPTER_OBJECT_MANIPULATION:
    case SOUND_CHAPTER_WEAPON_MANIPULATION:
    case SOUND_CHAPTER_EXPLOSION:
    case SOUND_CHAPTER_VEHICLE_NOT_CAR:
        if (source && human->IsFriendOf(source))
            return;
        break;
    case SOUND_CHAPTER_WEAPON_SHOOTING:
    case SOUND_CHAPTER_WEAPON_SHOOTING_DIST:
        if (human->IsFriendOf(source)) {
            controller->OnFriendHeard(source);
            return;
        }
        break;
    case SOUND_CHAPTER_BULLET_HIT:
        // Only bullets striking close by count (they are not remembered).
        if (sound->intensity <= 0.95f)
            return;
        controller->OnLoudSound(source);
        return;
    case SOUND_CHAPTER_FALLING_HIT:
    case SOUND_CHAPTER_VERSION:
    case SOUND_CHAPTER_INTERFACE:
    case SOUND_CHAPTER_ACTION:
        return;
    case SOUND_CHAPTER_SPEECH:
        // The last cry of a dying human (the source is a human here).
        if (static_cast<C_Human *>(source)->m_stats[STAT_HEALTH] < 1)
            controller->OnBodyHeard(source);
        break;
    default:
        break;
    }

    // A sound already remembered from the same source is refreshed.
    for (size_t i = human->m_heardSounds.size(); i-- > 0;) {
        S_RememberedSound *remembered = human->m_heardSounds[i];
        if (remembered->source == source && remembered->chapter == sound->chapter) {
            remembered->pos = sound->pos;
            remembered->posW = sound->posW;
            remembered->time = timeGetTime();
            remembered->refreshed = true;
            controller->OnSoundHeard(remembered);
            return;
        }
    }
    const S_vector4 pos = {sound->pos.x, sound->pos.y, sound->pos.z, sound->posW};
    S_RememberedSound *remembered = AllocRememberedSound(sound->chapter, pos, source, timeGetTime());
    human->m_heardSounds.push_back(remembered);
    controller->OnSoundHeard(remembered);
}

void C_Human::StopListening()
{
    if (m_sightId != -1)
        g_sightModule.SetObserverState(m_sightId, SIGHT_OBSERVER_OFF);
    if (m_soundListener != -1)
        GetSoundModule()->RemoveListener(m_soundListener);
    m_soundListener = -1;
}

void C_Human::StartListening()
{
    if (m_sightId != -1)
        g_sightModule.SetObserverState(m_sightId, SIGHT_OBSERVER_ON);
    m_soundListener = GetSoundModule()->AddListener(m_frame, OnSoundHeard);
}

// ------------------------------------------------------------------------------ events

void C_Human::OnProximity(C_Actor *other, float distance, bool ended, bool unknown)
{
    if (!ended) {
        if (m_events && !m_events->AddNearbyActor(other, distance))
            return;
        if (m_controller)
            m_controller->OnActorNear(other, distance);
    } else if (!unknown) {
        if (m_events && !m_events->RemoveNearbyActor(other, distance))
            return;
        if (m_controller)
            m_controller->OnActorGone(other, distance);
    }
}

void C_Human::ProcessEvents()
{
    while (m_events->HasPending()) {
        S_HumanEvent *event = m_events->PopFront();
        ReleaseHumanEvent(m_controller->HandleEvent(event));
    }
}

void C_Human::RaiseEvent(S_HumanEvent *event)
{
    if (m_events)
        m_events->Raise(event);
}

// -------------------------------------------------------------------------- freezing

void C_Human::Freeze(bool on)
{
    g_actorsModule.SetActorActive(this, !on);
    if (!on) {
        // Occupants of hidden seats stay invisible.
        const bool hiddenSeat = m_vehicle && (m_vehicle->GetSeatFlags(m_seat) & SEAT_FLAG_HIDDEN) != 0;
        m_frame->SetOn(!hiddenSeat);
    } else {
        m_frame->SetOn(false);
    }
    m_frame->Update();
    m_body.Freeze(on);
    if (m_controller)
        m_controller->Freeze(on);
    if (!on) {
        if (m_sightTargetId != -1)
            g_sightModule.SetTargetState(m_sightTargetId, SIGHT_TARGET_SHOWN);
        if (m_role == HUMAN_ROLE_PLAYER && g_teamManager.GetPlayerTeam()->GetActivePlayer() == this)
            g_teamManager.GetPlayerTeam()->UpdateCameraTarget();
    } else if (m_sightTargetId != -1) {
        g_sightModule.SetTargetState(m_sightTargetId, SIGHT_TARGET_HIDDEN);
    }
}

void C_Human::Suspend(bool suspend)
{
    if (m_controller->GetType() == HUMAN_CONTROLLER_AI) {
        GetScriptedController()->Suspend(suspend);
        return;
    }
    Freeze(suspend);
    if (suspend)
        StopListening();
    else
        StartListening();
}
