// The periodic links (0x006e0080..0x006e0f40, the object at 0x008aee64): every pair of registered actors is checked for being within reach of each
// other (C_Human::OnProximity) less often the farther apart they are. A pair sits in one of ten buckets, the n-th looked at every 100 * 2^n ms.
#include <cmath>
#include <vector>

#include "common/chunk.h"
#include "game/actor.h"
#include "game/actors_module.h"
#include "game/human.h"
#include "game/pending.h"
#include "ls3d/i3d.h"

namespace {

const int BUCKET_COUNT = 10;
const int TICK_MS = 100;
// How fast each kind of actor can close the distance (0x008435c8; only 0 and 3 are used).
const float RATE_TABLE[] = {5.0f, 0.0f, 0.0f, 20.0f, 0.0f};

struct S_Entry {                                        // 8 bytes
    C_Actor *actor;
    float rate;
};

struct S_Link {                                         // 3 bytes
    uint8_t first;                                      // the lower entry
    uint8_t second;
    bool within;
};

struct S_Bucket {                                       // 0x18 bytes
    int timer;
    int period;
    std::vector<S_Link *> links;
};

enum : uint16_t {
    CK_LINKS = 0x17EB,
    CK_ENTRY = 1000,
    CK_BUCKETS = 2000,
};

class C_PeriodicLinks {
public:
    C_PeriodicLinks()                                   // 0x006e0480
    {
        for (int i = 0; i < BUCKET_COUNT; ++i) {
            S_Bucket *const bucket = new S_Bucket;
            bucket->period = TICK_MS << i;
            bucket->timer = bucket->period + TICK_MS * i;
            m_buckets.push_back(bucket);
        }
    }
    ~C_PeriodicLinks()                                  // 0x006e06b0
    {
        Clear();
        for (S_Bucket *bucket : m_buckets)
            delete bucket;
    }

    int Add(C_Actor *actor, int rateIndex);
    void Remove(int id);
    void SetRate(int id, int rateIndex);
    void Tick();
    void Clear();
    bool Save(C_chunk &chunk) const;
    bool Load(C_chunk &chunk);

private:
    void Update(unsigned bucketIndex, int linkIndex, bool initial);   // 0x006e0750

    std::vector<S_Bucket *> m_buckets;                  // +0x04
    std::vector<S_Entry *> m_entries;                   // +0x14 (with holes)
};

C_PeriodicLinks s_links;                                // 0x008aee64

int C_PeriodicLinks::Add(C_Actor *actor, int rateIndex)
{
    if (!(g_gameRole & 1))
        return -1;
    S_Entry *const entry = new S_Entry;
    size_t id = 0;
    while (id < m_entries.size() && m_entries[id])
        ++id;
    if (id < m_entries.size())
        m_entries[id] = entry;
    else
        m_entries.push_back(entry);
    entry->actor = actor;
    entry->rate = RATE_TABLE[rateIndex];

    const E_ActorType type = actor->GetType();
    const bool vehicle = type == ACTOR_CAR || type == static_cast<E_ActorType>(0x25);
    for (size_t other = 0; other < m_entries.size(); ++other) {
        if (other == id)
            continue;
        // A vehicle is only linked with humans. (Fix: the original's test of a hole in the entries crashes.)
        if (vehicle && !(m_entries[other] && m_entries[other]->actor->GetType() == ACTOR_HUMAN))
            continue;
        S_Link *const link = new S_Link;
        link->first = static_cast<uint8_t>(id < other ? id : other);
        link->second = static_cast<uint8_t>(id > other ? id : other);
        link->within = false;
        m_buckets[0]->links.push_back(link);
        Update(0, static_cast<int>(m_buckets[0]->links.size()) - 1, true);
    }
    return static_cast<int>(id);
}

void C_PeriodicLinks::Remove(int id)
{
    if (!(g_gameRole & 1) || id < 0 || static_cast<size_t>(id) >= m_entries.size() || !m_entries[id])
        return;
    delete m_entries[id];
    m_entries[id] = nullptr;
    for (size_t b = m_buckets.size(); b-- != 0;) {
        std::vector<S_Link *> &links = m_buckets[b]->links;
        for (size_t i = links.size(); i-- != 0;) {
            if (links[i]->second == id || links[i]->first == id) {
                delete links[i];
                links[i] = links.back();
                links.pop_back();
            }
        }
    }
    while (!m_entries.empty() && !m_entries.back())
        m_entries.pop_back();
}

void C_PeriodicLinks::SetRate(int id, int rateIndex)
{
    if (!(g_gameRole & 1) || id < 0 || static_cast<size_t>(id) >= m_entries.size() || !m_entries[id])
        return;
    const float before = m_entries[id]->rate;
    m_entries[id]->rate = RATE_TABLE[rateIndex];
    if (!(before < m_entries[id]->rate))
        return;
    // Faster: its pairs are looked at again now.
    for (size_t b = m_buckets.size(); b-- != 0;) {
        for (size_t i = m_buckets[b]->links.size(); i-- != 0;) {
            const S_Link *const link = m_buckets[b]->links[i];
            if (link->second == id || link->first == id)
                Update(static_cast<unsigned>(b), static_cast<int>(i), false);
        }
    }
}

void C_PeriodicLinks::Update(unsigned bucketIndex, int linkIndex, bool initial)
{
    S_Bucket *const from = m_buckets[bucketIndex];
    S_Link *const link = from->links[linkIndex];
    const S_Entry *const first = m_entries[link->first];
    const S_Entry *const second = m_entries[link->second];
    if (!first || !second)
        return;
    const C_Actor *const firstActor = first->actor;
    const C_Actor *const secondActor = second->actor;
    S_vector between;
    if (initial)
        between = firstActor->GetFrame()->GetWorldPos() - secondActor->GetFrame()->GetWorldPos();
    else
        between = firstActor->GetPosition() - secondActor->GetPosition();

    const E_ActorType vehicle2 = static_cast<E_ActorType>(0x25);
    float reach = 5.0f;
    if (firstActor->GetType() == ACTOR_CAR || secondActor->GetType() == ACTOR_CAR)
        reach = 7.5f;
    else if (firstActor->GetType() == vehicle2 || secondActor->GetType() == vehicle2)
        reach = 10.0f;

    const float squared = between.x * between.x + between.z * between.z + between.y * between.y;
    float distance;
    if (squared < 1e-8f)
        distance = 0.0f;
    else if (std::fabs(squared - 1.0f) < 1e-8f)
        distance = 1.0f;
    else
        distance = std::sqrt(squared);
    distance -= reach;
    const bool outside = distance > 0.0f;
    distance = std::fabs(distance);

    float rate = second->rate + first->rate;
    if (rate == 0.0f)
        rate = 0.01f;
    // The bucket for when they could meet at the earliest.
    const unsigned bucketCount = static_cast<unsigned>(m_buckets.size());
    unsigned target = bucketCount - 1;
    for (unsigned u = 0; u < bucketCount - 1; ++u) {
        if (1000.0f / rate * distance < static_cast<float>(m_buckets[u + 1]->period)) {
            target = u;
            break;
        }
    }

    bool changed = false;
    if (outside) {
        if (link->within || initial) {
            link->within = false;
            changed = true;
        }
    } else if (!link->within || initial) {
        link->within = true;
        changed = true;
    }

    // OnProximity's two flags are bucket numbers, in effect bools. Kept: both come from those, however odd.
    unsigned flagUnknown = bucketIndex;
    unsigned flagEnded = target;
    bool notify = true;
    if (changed) {
        if (target == bucketIndex) {
            flagUnknown = outside ? 0 : 1;
            flagEnded = outside ? 1 : 0;
        }
    } else if (!outside) {
        notify = false;
    } else {
        if (target == bucketIndex)
            return;
        if (bucketIndex == 0 || target == 0)
            notify = false;
    }
    if (notify && initial && flagUnknown == 0) {
        if (!link->within) {
            notify = false;
        } else {
            flagUnknown = 1;
            flagEnded = 0;
        }
    }
    if (notify && reach == 5.0f) {
        static_cast<C_Human *>(first->actor)->OnProximity(second->actor, distance, flagEnded != 0, flagUnknown != 0);
        static_cast<C_Human *>(second->actor)->OnProximity(first->actor, distance, flagEnded != 0, flagUnknown != 0);
    }

    if (target != bucketIndex) {
        m_buckets[target]->links.push_back(link);
        from->links[linkIndex] = from->links.back();
        from->links.pop_back();
    }
}

void C_PeriodicLinks::Tick()
{
    for (size_t b = 0; b < m_buckets.size(); ++b) {
        S_Bucket *const bucket = m_buckets[b];
        bucket->timer -= TICK_MS;
        if (bucket->timer == 0) {
            bucket->timer = bucket->period;
            for (size_t i = bucket->links.size(); i-- != 0;)
                Update(static_cast<unsigned>(b), static_cast<int>(i), false);
        }
    }
}

void C_PeriodicLinks::Clear()
{
    for (S_Entry *entry : m_entries)
        delete entry;
    m_entries.clear();
    for (S_Bucket *bucket : m_buckets) {
        for (S_Link *link : bucket->links)
            delete link;
        bucket->links.clear();
    }
}

bool C_PeriodicLinks::Save(C_chunk &chunk) const
{
    chunk.Begin(CK_LINKS);
    for (const S_Entry *entry : m_entries) {
        chunk.Begin(CK_ENTRY);
        // (Fix: a hole, on which the original crashes, goes as no actor; the load keeps it a hole.)
        const int actorId = entry ? entry->actor->GetNetId() : -1;
        const float rate = entry ? entry->rate : 0.0f;
        chunk.Write(&actorId, 4);
        chunk.Write(&rate, 4);
        chunk.Ascend();
    }
    chunk.Begin(CK_BUCKETS);
    for (const S_Bucket *bucket : m_buckets) {
        chunk.Write(&bucket->timer, 4);
        chunk.Write(&bucket->period, 4);
        const int count = static_cast<int>(bucket->links.size());
        chunk.Write(&count, 4);
        for (const S_Link *link : bucket->links) {
            chunk.Write(&link->first, 1);
            chunk.Write(&link->second, 1);
            chunk.Write(&link->within, 1);
        }
    }
    chunk.Ascend();
    chunk.Ascend();
    return true;
}

bool C_PeriodicLinks::Load(C_chunk &chunk)
{
    Clear();
    uint16_t id;
    if (!chunk.Descend(id))
        return true;
    if (id == CK_LINKS) {
        while (chunk.HasMore()) {
            if (!chunk.Descend(id))
                break;
            if (id == CK_ENTRY) {
                int actorId = -1;
                float rate = 0.0f;
                chunk.Read(&actorId, 4);
                chunk.Read(&rate, 4);
                C_Actor *const actor = actorId == -1 ? nullptr : g_actorsModule.GetActorById(actorId);
                if (actor)
                    m_entries.push_back(new S_Entry{actor, rate});
                else
                    m_entries.push_back(nullptr);       // (fix: an entry without its actor crashes the original's next update)
            } else if (id == CK_BUCKETS) {
                for (S_Bucket *bucket : m_buckets) {
                    chunk.Read(&bucket->timer, 4);
                    chunk.Read(&bucket->period, 4);
                    int count = 0;
                    chunk.Read(&count, 4);
                    for (int i = 0; i < count; ++i) {
                        S_Link *const link = new S_Link{0, 0, false};
                        chunk.Read(&link->first, 1);
                        chunk.Read(&link->second, 1);
                        chunk.Read(&link->within, 1);
                        bucket->links.push_back(link);
                    }
                }
            }
            chunk.Ascend();
        }
    }
    chunk.Ascend();
    return true;
}

}  // namespace

int PeriodicLinks_Add(C_Actor *actor, int arg) { return s_links.Add(actor, arg); }
void PeriodicLinks_Remove(int id) { s_links.Remove(id); }
void PeriodicLinks_SetRate(int id, int rate) { s_links.SetRate(id, rate); }
void PeriodicLinks_Tick() { s_links.Tick(); }
void PeriodicLinks_Clear() { s_links.Clear(); }
bool PeriodicLinks_Save(C_chunk &chunk) { return s_links.Save(chunk); }
bool PeriodicLinks_Load(C_chunk &chunk) { return s_links.Load(chunk); }
