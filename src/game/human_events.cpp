// What the AI of a human perceives: the event pool, S_HumanEvent and C_HumanEvents
// (original source 0x005ac950..0x005af110).
#include "game/human.h"

#include "common/chunk.h"
#include "common/object_pool.h"
#include "game/actors_module.h"

namespace {

// Saved game chunks of S_HumanEvent.
enum : uint16_t {
    CHUNK_EVENT_V1 = 500,                 // pos, time left, type, age (dropped)
    CHUNK_EVENT_ACTOR = 0x1F5,
    CHUNK_EVENT_SOURCE = 0x1F6,
    CHUNK_EVENT_V2 = 0x1F7,               // pos, time left, type, age
    CHUNK_EVENT = 0x1F8,                  // type, age, time left, pos, unknown30
};

// Saved game chunks of C_HumanEvents.
enum : uint16_t {
    CHUNK_EVENTS_SNIPER = 600,
    CHUNK_EVENTS_PENDING = 0x259,         // one per event
    CHUNK_EVENTS_STIMULUS = 0x25A,
    CHUNK_EVENTS_NEARBY = 0x25B,          // one per actor id
    CHUNK_EVENTS_UNKNOWN = 0x25C,
};

// Positions are saved without w.
constexpr int SAVED_POS_SIZE = 12;

// Nearby actors closer than this (squared, m^2) block a position.
constexpr float NEARBY_BLOCK_DIST2 = 4.0f;

C_ObjectPool<S_HumanEvent> g_humanEventPool(50);        // 0x008ac430

}  // namespace

S_HumanEvent *AllocHumanEvent()
{
    return g_humanEventPool.Get();
}

void ReleaseHumanEvent(S_HumanEvent *event)
{
    if (event)
        g_humanEventPool.Put(event);
}

int ActorToId(const C_Actor *actor)
{
    if (!actor)
        return -1;
    return actor->GetNetId();
}

C_Actor *IdToActor(int id)
{
    if (id < 0)
        return nullptr;
    return g_actorsModule.GetActor(id);
}

// ------------------------------------------------------------------------- S_HumanEvent

bool S_HumanEvent::SaveGame(C_chunk &chunk) const
{
    chunk.Begin(CHUNK_EVENT);
    chunk.Write(type);
    chunk.Write(age);
    chunk.Write(timeLeft);
    chunk.Write(&pos, SAVED_POS_SIZE);
    chunk.Write(unknown30);
    chunk.Ascend();
    if (actorId >= 0)
        chunk.WriteChunk(CHUNK_EVENT_ACTOR, actorId);
    if (sourceId >= 0)
        chunk.WriteChunk(CHUNK_EVENT_SOURCE, sourceId);
    return true;
}

bool S_HumanEvent::LoadGame(C_chunk &chunk)
{
    type = 0;
    age = 0;
    timeLeft = 0;
    actorId = -1;
    sourceId = -1;
    pos.x = pos.y = pos.z = 0.0f;
    unknown30 = 0;
    uint16_t id;
    while (!chunk.AtEnd() && chunk.Descend(id)) {
        switch (id) {
        case CHUNK_EVENT_V1:
            chunk.Read(&pos, SAVED_POS_SIZE);
            chunk.Read(timeLeft);
            chunk.Read(type);
            chunk.Read(age);
            // (The age is read and then dropped.)
            age = 0;
            unknown30 = 0;
            break;
        case CHUNK_EVENT_ACTOR:
            actorId = -1;
            chunk.Read(actorId);
            break;
        case CHUNK_EVENT_SOURCE:
            sourceId = -1;
            chunk.Read(sourceId);
            break;
        case CHUNK_EVENT_V2:
            chunk.Read(&pos, SAVED_POS_SIZE);
            chunk.Read(timeLeft);
            chunk.Read(type);
            chunk.Read(age);
            unknown30 = 0;
            break;
        case CHUNK_EVENT:
            chunk.Read(type);
            chunk.Read(age);
            chunk.Read(timeLeft);
            chunk.Read(&pos, SAVED_POS_SIZE);
            chunk.Read(unknown30);
            break;
        default:
            break;
        }
        chunk.Ascend();
    }
    return true;
}

// ------------------------------------------------------------------------ C_HumanEvents

namespace {

// Drops the events of `events` whose time ran out (back to front, like the original).
void AgeEvents(std::deque<S_HumanEvent *> &events, int timeDelta)
{
    for (size_t i = events.size(); i-- > 0;) {
        S_HumanEvent *event = events[i];
        event->age += timeDelta;
        event->timeLeft -= timeDelta;
        if (event->timeLeft < 1) {
            ReleaseHumanEvent(event);
            events.erase(events.begin() + static_cast<std::ptrdiff_t>(i));
        }
    }
}

void ReleaseAll(std::deque<S_HumanEvent *> &events)
{
    for (size_t i = events.size(); i-- > 0;)
        ReleaseHumanEvent(events[i]);
    events.clear();
}

}  // namespace

void C_HumanEvents::Reset()
{
    ReleaseAll(m_pending);
    ReleaseAll(m_stimuli);
    m_nearbyActors.clear();
    m_unknownActors.clear();
    // (The sniper position is kept.)
    m_sniper = false;
    m_sniperRadius = 0.0f;
}

bool C_HumanEvents::SaveGame(C_chunk &chunk)
{
    chunk.Begin(CHUNK_EVENTS_SNIPER);
    chunk.Write(&m_sniperPos, SAVED_POS_SIZE);
    chunk.Write(m_sniperRadius);
    chunk.Write(m_sniper);
    chunk.Ascend();
    for (const S_HumanEvent *event : m_pending) {
        chunk.Begin(CHUNK_EVENTS_PENDING);
        event->SaveGame(chunk);
        chunk.Ascend();
    }
    for (const S_HumanEvent *event : m_stimuli) {
        chunk.Begin(CHUNK_EVENTS_STIMULUS);
        event->SaveGame(chunk);
        chunk.Ascend();
    }
    for (int id : m_nearbyActors)
        chunk.WriteChunk(CHUNK_EVENTS_NEARBY, id);
    for (int id : m_unknownActors)
        chunk.WriteChunk(CHUNK_EVENTS_UNKNOWN, id);
    return true;
}

bool C_HumanEvents::LoadGame(C_chunk &chunk)
{
    Reset();
    uint16_t id;
    while (!chunk.AtEnd() && chunk.Descend(id)) {
        switch (id) {
        case CHUNK_EVENTS_SNIPER:
            chunk.Read(&m_sniperPos, SAVED_POS_SIZE);
            chunk.Read(m_sniperRadius);
            chunk.Read(m_sniper);
            break;
        case CHUNK_EVENTS_PENDING: {
            S_HumanEvent *event = AllocHumanEvent();
            m_pending.push_back(event);
            event->LoadGame(chunk);
            break;
        }
        case CHUNK_EVENTS_STIMULUS: {
            S_HumanEvent *event = AllocHumanEvent();
            m_stimuli.push_back(event);
            event->LoadGame(chunk);
            break;
        }
        case CHUNK_EVENTS_NEARBY: {
            int actorId = -1;
            chunk.Read(actorId);
            m_nearbyActors.push_back(actorId);
            break;
        }
        case CHUNK_EVENTS_UNKNOWN: {
            int actorId = -1;
            chunk.Read(actorId);
            m_unknownActors.push_back(actorId);
            break;
        }
        default:
            break;
        }
        chunk.Ascend();
    }
    return true;
}

void C_HumanEvents::Tick(int timeDelta)
{
    AgeEvents(m_pending, timeDelta);
    AgeEvents(m_stimuli, timeDelta);
}

bool C_HumanEvents::AddNearbyActor(const C_Actor *actor, float /*distance*/)
{
    if (!actor)
        return false;
    const int id = actor->GetNetId();
    for (size_t i = m_nearbyActors.size(); i-- > 0;) {
        if (m_nearbyActors[i] == id)
            return false;
    }
    m_nearbyActors.push_back(id);
    return true;
}

bool C_HumanEvents::RemoveNearbyActor(const C_Actor *actor, float /*distance*/)
{
    if (!actor)
        return false;
    for (size_t i = m_nearbyActors.size(); i-- > 0;) {
        if (m_nearbyActors[i] == actor->GetNetId()) {
            // (The last one takes its place.)
            m_nearbyActors[i] = m_nearbyActors.back();
            m_nearbyActors.pop_back();
            return true;
        }
    }
    return false;
}

C_Actor *C_HumanEvents::GetNearbyActor(int index) const
{
    if (index < 0 || static_cast<size_t>(index) >= m_nearbyActors.size())
        return nullptr;
    const int id = m_nearbyActors[static_cast<size_t>(index)];
    if (id == -1)
        return nullptr;
    return g_actorsModule.GetActorById(id);
}

bool C_HumanEvents::IsAwayFromNearbyActors(const S_vector &pos) const
{
    for (size_t i = m_nearbyActors.size(); i-- > 0;) {
        const C_Actor *actor = g_actorsModule.GetActorById(m_nearbyActors[i]);
        if (actor && (pos - actor->GetFrame()->GetWorldPos()).Square() < NEARBY_BLOCK_DIST2)
            return false;
    }
    return true;
}

void C_HumanEvents::AddUnknownActor(const C_Actor *actor)
{
    if (actor)
        m_unknownActors.push_back(actor->GetNetId());
}

void C_HumanEvents::Raise(S_HumanEvent *event)
{
    m_pending.push_back(event);
}

S_HumanEvent *C_HumanEvents::PopFront()
{
    if (m_pending.empty())
        return nullptr;
    S_HumanEvent *event = m_pending.front();
    m_pending.pop_front();
    return event;
}

void C_HumanEvents::AddStimulus(int time, S_HumanEvent *event, bool keepPriority)
{
    event->timeLeft = time;
    if (event->actorId < 0) {
        if (event->sourceId >= 0) {
            for (size_t i = m_stimuli.size(); i-- > 0;) {
                S_HumanEvent *tracked = m_stimuli[i];
                if (tracked->sourceId != event->sourceId)
                    continue;
                if (time < tracked->timeLeft)
                    event->timeLeft = tracked->timeLeft;
                // (The whole type of the tracked event is taken, not only its priority.)
                if (keepPriority && (event->type & HUMAN_EVENT_PRIORITY_MASK) < (tracked->type & HUMAN_EVENT_PRIORITY_MASK))
                    event->type = tracked->type;
                ReleaseHumanEvent(tracked);
                m_stimuli[i] = event;
                return;
            }
        }
    } else {
        for (size_t i = m_stimuli.size(); i-- > 0;) {
            S_HumanEvent *tracked = m_stimuli[i];
            if (tracked->actorId != event->actorId)
                continue;
            // (`keepPriority` is not applied here.)
            if (time < tracked->timeLeft)
                event->timeLeft = tracked->timeLeft;
            ReleaseHumanEvent(tracked);
            m_stimuli[i] = event;
            return;
        }
    }
    m_stimuli.push_back(event);
}

S_HumanEvent *C_HumanEvents::GetStrongestStimulus(const S_vector &pos) const
{
    S_HumanEvent *strongest = nullptr;
    float strongestScore = 0.0f;
    for (S_HumanEvent *event : m_stimuli) {
        const float distance = (S_vector(event->pos.x, event->pos.y, event->pos.z) - pos).Magnitude();
        const float priority = static_cast<float>(event->type >> HUMAN_EVENT_PRIORITY_SHIFT);
        // (At distance 0 the score is infinite.)
        const float score = static_cast<float>(event->timeLeft) * 0.001f / distance * priority;
        if (score > strongestScore) {
            strongestScore = score;
            strongest = event;
        }
    }
    return strongest;
}

C_Actor *C_HumanEvents::GetAttacker() const
{
    for (size_t i = m_stimuli.size(); i-- > 0;) {
        if (m_stimuli[i]->type == HUMAN_EVENT_HIT)
            return IdToActor(m_stimuli[i]->sourceId);
    }
    return nullptr;
}

void C_HumanEvents::SetSniper(bool on, float radius, const S_vector &pos)
{
    m_sniper = on;
    m_sniperRadius = radius;
    // (The original copies the w of the world matrix row, 1.)
    m_sniperPos = {pos.x, pos.y, pos.z, 1.0f};
}
