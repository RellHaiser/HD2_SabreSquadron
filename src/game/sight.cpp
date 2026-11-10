// The sight module (0x0046d5d0..0x004702f0): see game/sight.h.
#include "game/sight.h"

#include <cstring>

#include "game/actor.h"
#include "game/collision.h"
#include "game/game.h"
#include "game/human.h"
#include "game/melee_tracker.h"
#include "ls3d/i3d.h"

C_SightModule g_sightModule(MODULE_SIGHT, "Sight");     // 0x0089b288
float g_sightTime = 1.0f;                               // 0x00839390

namespace {

// The body parts (bit, collision volume frame) of the targets' tables.
struct S_SightPart {
    uint32_t bit;
    const char *name;
};
const S_SightPart HUMAN_PARTS[] = {{SIGHT_PART_BODY, "d"}, {SIGHT_PART_HEAD, "z"}, {SIGHT_PART_LEFT_ARM, "i"},
                                   {SIGHT_PART_RIGHT_ARM, "m"}, {SIGHT_PART_LEGS, "q"}};   // 0x00839338
const S_SightPart MELEE_TRACKER_PARTS[] = {{1, "c"}, {2, "f"}, {4, "k"}, {8, "o"}};          // 0x00839368
const char *const HUMAN_HEAD_FALLBACK = "f";            // 0x008390ec

// What a part's ray passes (light objects when the target is not alive).
constexpr uint32_t SIGHT_RAY_TYPES = 0x2CC100;
constexpr uint32_t SIGHT_RAY_TYPES_MELEE_TRACKER = 0x208000;
constexpr uint32_t SIGHT_RAY_FLAGS = 0x100000;

C_SightTarget *ValidTarget(int id)
{
    return g_sightModule.GetTarget(id);
}

uint32_t RayTypes(int id, uint32_t types)
{
    // (The original reads the flag of a null target when the id is not valid.)
    const C_SightTarget *target = ValidTarget(id);
    return (target && target->alive ? 0 : COLLISION_TYPE_LIGHT_OBJECT) | types;
}

// Finds the part frames by their short names (EnumFrames' callbacks 0x0046ebf0 / 0x0046ede0).
struct S_PartSearch {
    I3D_frame **found;
    const char *name;
};
uint32_t __stdcall FindPart(I3D_frame *frame, void *context)
{
    auto *search = static_cast<S_PartSearch *>(context);
    if (_stricmp(frame->GetShortName(), search->name) != 0)
        return 0;
    *search->found = frame;
    return 2;
}

template <size_t N> void FindParts(std::vector<I3D_frame *> &parts, I3D_frame *model, const S_SightPart (&table)[N])
{
    parts.clear();
    for (size_t i = 0; i < N; ++i) {
        parts.push_back(nullptr);
        S_PartSearch search = {&parts[i], table[i].name};
        if (model)
            model->EnumFrames(FindPart, &search, FRAME_MASK_VOLUME, nullptr);
    }
}

// The relation of an observer's owner to a target's (friend, hostile; neither: neutral).
void Relation(C_Actor *observer, C_Actor *target, bool *isFriend, bool *isHostile)
{
    *isFriend = false;
    *isHostile = false;
    if (observer->GetType() == ACTOR_HUMAN) {
        auto *human = static_cast<C_Human *>(observer);
        *isFriend = human->IsFriendOf(target);
        *isHostile = human->IsHostileTo(target);
        if (target->GetType() == ACTOR_HUMAN) {
            // The player's side and the enemy are always hostile.
            const E_HumanRole a = human->GetRole(), b = static_cast<C_Human *>(target)->GetRole();
            const bool player = a == HUMAN_ROLE_PLAYER || b == HUMAN_ROLE_PLAYER;
            const bool enemy = a == HUMAN_ROLE_ENEMY || b == HUMAN_ROLE_ENEMY;
            if (player && enemy) {
                *isFriend = false;
                *isHostile = true;
            }
        }
    } else if (observer->GetType() == ACTOR_MELEE_TRACKER) {
        if (target->GetType() == ACTOR_HUMAN) {
            const E_HumanRole role = static_cast<C_Human *>(target)->GetRole();
            *isFriend = role == HUMAN_ROLE_ENEMY || role == HUMAN_ROLE_NEUTRAL;
            *isHostile = role == HUMAN_ROLE_PLAYER || role == HUMAN_ROLE_NPC_TEAM || role == HUMAN_ROLE_NPC_TEAM_5;
        } else {
            *isFriend = target->GetType() == ACTOR_MELEE_TRACKER;
        }
    }
}

void NotifyFound(C_Actor *observer, int target)
{
    if (observer->GetType() == ACTOR_HUMAN)
        static_cast<C_Human *>(observer)->OnSightEvent(0, target);
    else if (observer->GetType() == ACTOR_MELEE_TRACKER)
        static_cast<C_MeleeTracker *>(observer)->OnSightEvent(0, target);
}

// A human observer told that a target left its view.
void NotifyLostByHuman(const C_SightObserver *observer, int target)
{
    if (C_Actor *actor = observer->frame ? GetFrameActor(observer->frame) : nullptr) {
        if (actor->GetType() == ACTOR_HUMAN)
            static_cast<C_Human *>(actor)->OnSightEvent(1, target);
    }
}

}  // namespace

// ---- targets ---------------------------------------------------------------------------------

C_SightTarget::C_SightTarget(I3D_frame *frame_, uint32_t mask_, int index_)   // 0x0046eac0
    : frame(frame_), mask(mask_), index(index_)
{
}

C_HumanSightTarget::C_HumanSightTarget(C_Human *human_, uint32_t mask_, int index_)   // 0x0046ec40
    : C_SightTarget(human_->GetFrame(), mask_, index_), human(human_)
{
    partCount = 5;
    Refresh();
}

C_Actor *C_HumanSightTarget::GetActor()                 // 0x0046ecb0
{
    return human;
}

void C_HumanSightTarget::Refresh()                      // 0x0046eb20
{
    FindParts(parts, human ? human->GetFrame() : nullptr, HUMAN_PARTS);
    if (!parts[1])
        parts[1] = human->GetFrame()->FindChildFrame(HUMAN_HEAD_FALLBACK, FRAME_MASK_VOLUME);
}

C_MeleeTrackerSightTarget::C_MeleeTrackerSightTarget(C_MeleeTracker *meleeTracker_, uint32_t mask_, int index_)   // 0x0046ecf0
    : C_SightTarget(meleeTracker_->GetFrame(), mask_, index_), meleeTracker(meleeTracker_)
{
    partCount = 4;
    FindParts(parts, meleeTracker ? meleeTracker->GetFrame() : nullptr, MELEE_TRACKER_PARTS);
}

C_Actor *C_MeleeTrackerSightTarget::GetActor()          // 0x0046edc0
{
    return meleeTracker;
}

// ---- observers -------------------------------------------------------------------------------

C_SightObserver::C_SightObserver(I3D_frame *frame_, uint32_t mask_, int index_)   // 0x0046d6f0
    : frame(frame_), mask(mask_), index(index_)
{
}

bool C_SightObserver::SetRelation(int target, int relation)   // 0x0046d600
{
    if (target < 0 || static_cast<size_t>(target) > records.size())
        return false;
    if (static_cast<size_t>(target) == records.size()) {
        S_SightRecord record;
        record.relation = relation;
        records.push_back(record);
        return true;
    }
    S_SightRecord &record = records[target];
    record.parts = 0;
    record.seen = 0;
    record.seenBefore = 0;
    record.part = 0;
    record.visible = false;
    record.relation = relation;
    return true;
}

C_HumanSightObserver::C_HumanSightObserver(C_Human *human_, uint32_t mask_, int index_)   // 0x0046d730 (DS 0x00488be0)
    : C_SightObserver(human_->GetFrame(), mask_, index_), human(human_)
{
}

bool C_HumanSightObserver::WantsTest(int target)        // 0x0046d840
{
    const C_SightTarget *t = ValidTarget(target);
    if (!t)
        return false;
    int next = records[target].part + 1;
    if (next == t->partCount)
        next = 0;
    return records[target].view[next] != 100.0f;
}

bool C_HumanSightObserver::TestNow(int target)          // 0x0046d8c0
{
    C_SightTarget *t = ValidTarget(target);
    if (!t || t->hidden)
        return false;
    S_SightRecord &record = records[target];
    if (record.parts != 0)
        return true;
    const S_vector eye = human->GetBodyFrame()->GetWorldPos();
    C_Actor *other = t->GetActor();
    if (other == human)
        return false;
    bool collisionOff = false;
    C_Actor *vehicle = nullptr;
    C_Human *otherHuman = static_cast<C_Human *>(other);
    if (t->GetKind() == 0) {
        collisionOff = true;
        otherHuman->RemoveCollision(false);
        vehicle = otherHuman->GetVehicle();
        int status;
        float visibility = 0.0f;
        human->TestSightOfHuman(otherHuman, &status, &visibility);
        if (visibility != 100.0f) {
            otherHuman->RestoreCollision(false);
            return false;
        }
    }
    for (size_t i = t->parts.size(); i-- > 0;) {
        const S_vector dir = t->parts[i]->GetWorldPos() - eye;
        const S_CollisionVolume *hit = g_collisionWorld.RayTest(eye, dir, nullptr, nullptr,
                                                                RayTypes(target, SIGHT_RAY_TYPES), SIGHT_RAY_FLAGS);
        if (!hit || (vehicle && (hit->flags & COLLISION_FLAG_DYNAMIC) && hit->actor == vehicle)) {
            record.part = static_cast<int>(i);
            record.parts |= 1u << i;
            ++record.seen;
            human->OnSightEvent(0, target);
            if (collisionOff)
                otherHuman->RestoreCollision(false);
            return true;
        }
    }
    if (collisionOff)
        otherHuman->RestoreCollision(false);
    return false;
}

int C_HumanSightObserver::CanSee(int target, int *before, bool *visible)   // 0x0046db60
{
    const S_SightRecord &record = records[target];
    if (record.relation == SIGHT_FRIEND) {
        // (The original reads a null target's count when the id is not valid.)
        const C_SightTarget *t = ValidTarget(target);
        const int count = t ? t->partCount : 0;
        if (before)
            *before = count;
        if (visible)
            *visible = true;
        return count;
    }
    if (before)
        *before = record.seenBefore;
    if (visible)
        *visible = record.visible;
    return record.seen;
}

const std::vector<I3D_frame *> *C_HumanSightObserver::GetVisibleParts(int target, uint32_t *parts)   // 0x0046dc10
{
    const C_SightTarget *t = ValidTarget(target);
    if (parts)
        *parts = records[target].relation == SIGHT_FRIEND ? 0x1F : records[target].parts;
    // (The original returns the parts of a null target's +0x18 when the id is not valid.)
    return t ? &t->parts : nullptr;
}

bool C_HumanSightObserver::TestPart(int target, int timeDelta, bool *skipped)   // 0x0046df80
{
    S_SightRecord &record = records[target];
    const uint32_t before = record.parts;
    C_SightTarget *t = ValidTarget(target);
    if (!t)
        return false;
    if (++record.part == t->partCount)
        record.part = 0;
    const int part = record.part;
    const uint32_t now = GetGame()->GetMissionTime();
    if (record.nextTest[part] > now) {
        GetGame()->Print("SAFE : %d, MissionTime : %d, REST: %d", record.nextTest[part], now, record.nextTest[part] - now);
        *skipped = true;
        return before != 0;
    }
    record.nextTest[part] = 0;
    I3D_frame *partFrame = t->parts[part];
    C_Actor *other = t->GetActor();
    int wait = 0;
    if (record.view[part] == 0.0f)
        wait = timeDelta;
    if (record.view[part] > 0.0f) {
        wait = static_cast<int>(GetGame()->GetMissionTime() - record.lastTest[part]);
        record.lastTest[part] = GetGame()->GetMissionTime();
    }
    record.nextTest[part] = GetGame()->GetMissionTime() + wait;
    const S_vector eye = human->GetBodyFrame()->GetWorldPos();
    if (!partFrame)
        return false;
    const S_vector dir = partFrame->GetWorldPos() - eye;
    uint32_t types = RayTypes(target, 0);
    bool collisionOff = false, volumesIgnored = false;
    C_Actor *vehicle = nullptr;
    if (t->GetKind() == 0) {
        collisionOff = true;
        static_cast<C_Human *>(other)->RemoveCollision(false);
        vehicle = static_cast<C_Human *>(other)->GetVehicle();
    } else if (t->GetKind() == 1) {
        volumesIgnored = true;
        S_CollisionVolume **volumes = static_cast<C_MeleeTracker *>(other)->GetBodyVolumes();
        if (volumes && *volumes)
            g_collisionWorld.SetIgnoredVolumes(volumes, 2, 0);
        types |= COLLISION_TYPE_LIGHT_OBJECT;
    }
    S_vector hitPos;
    const S_CollisionVolume *hit = g_collisionWorld.RayTest(eye, dir, &hitPos, nullptr, types | SIGHT_RAY_TYPES,
                                                            SIGHT_RAY_FLAGS);
    if (collisionOff)
        static_cast<C_Human *>(other)->RestoreCollision(false);
    else if (volumesIgnored)
        g_collisionWorld.SetIgnoredVolume(nullptr, 0);
    if (!hit || (vehicle && (hit->flags & COLLISION_FLAG_DYNAMIC) && hit->actor == vehicle))
        return true;
    record.view[part] = 0.0f;
    return false;
}

void C_HumanSightObserver::Update(uint32_t, int target, int timeDelta)   // 0x0046dcf0
{
    if (disabled)
        return;
    S_SightRecord &record = records[target];
    if (record.relation != SIGHT_HOSTILE)
        return;
    const uint32_t before = record.parts;
    record.seenBefore = record.seen;
    bool skipped = false;
    // (The original takes a null target's actor when the id is not valid.)
    C_SightTarget *t = ValidTarget(target);
    if (!t)
        return;
    C_Actor *other = t->GetActor();
    int status;
    float visibility;
    if (other->GetType() == ACTOR_HUMAN) {
        // A dead one is not looked at again once seen.
        if (static_cast<C_Human *>(other)->GetHealth() < 1 && before != 0)
            return;
        human->TestSightOfHuman(static_cast<C_Human *>(other), &status, &visibility);
    } else if (other->GetType() == ACTOR_MELEE_TRACKER) {
        human->TestSightOfMeleeTracker(other, &status, &visibility);
    } else {
        return;
    }
    if (visibility != 100.0f) {
        if (before != 0)
            human->OnSightEvent(1, target);
        record.parts = 0;
        record.seen = 0;
        record.visible = false;
        return;
    }
    const bool seen = TestPart(target, timeDelta, &skipped);
    const uint32_t bit = 1u << record.part;
    if (seen) {
        record.parts |= bit;
        if (before != record.parts)
            ++record.seen;
        record.visible = true;
    } else {
        record.parts &= ~bit;
        if (before != record.parts)
            --record.seen;
        record.visible = false;
    }
    if (skipped)
        return;
    if (before == 0) {
        if (record.parts != 0)
            human->OnSightEvent(0, target);
    } else if (record.parts == 0) {
        human->OnSightEvent(1, target);
    }
}

C_MeleeTrackerSightObserver::C_MeleeTrackerSightObserver(C_MeleeTracker *meleeTracker_, uint32_t mask_, int index_)   // 0x0046d760 (DS 0x00488c20)
    : C_SightObserver(meleeTracker_->GetFrame(), mask_, index_), meleeTracker(meleeTracker_)
{
}

bool C_MeleeTrackerSightObserver::WantsTest(int target) // 0x0046dc70
{
    const C_SightTarget *t = ValidTarget(target);
    if (!t)
        return false;
    int next = records[target].part + 1;
    if (next == t->partCount)
        next = 0;
    return records[target].view[next] != 100.0f;
}

const std::vector<I3D_frame *> *C_MeleeTrackerSightObserver::GetVisibleParts(int, uint32_t *)   // 0x0046d7b0
{
    static const std::vector<I3D_frame *> none;         // 0x0098c1b0
    return &none;
}

bool C_MeleeTrackerSightObserver::TestPart(int target, int timeDelta)   // 0x0046e3b0
{
    C_SightTarget *t = ValidTarget(target);
    if (!t)
        return false;
    S_SightRecord &record = records[target];
    if (++record.part == t->partCount)
        record.part = 0;
    const int part = record.part;
    const uint32_t now = GetGame()->GetMissionTime();
    if (static_cast<int>(record.nextTest[part] - now) > 0)
        return false;
    record.nextTest[part] = 0;
    I3D_frame *partFrame = t->parts[part];
    C_Actor *other = t->GetActor();
    // Fix: no wait for a negative view; the original leaves the variable unset there.
    int wait = 0;
    if (record.view[part] == 0.0f)
        wait = timeDelta;
    if (record.view[part] > 0.0f) {
        wait = static_cast<int>(GetGame()->GetMissionTime() - record.lastTest[part]);
        record.lastTest[part] = GetGame()->GetMissionTime();
    }
    if (t->GetKind() != 0 && t->GetKind() != 1)
        return false;
    int status;
    meleeTracker->TestSight(other, &status, &record.view[part]);
    record.nextTest[part] = GetGame()->GetMissionTime() + wait;
    if (record.view[part] != 100.0f)
        return false;
    S_vector eye;
    if (I3D_frame *body = meleeTracker->GetBodyFrame()) {
        eye = body->GetWorldPos();
    } else {
        // Fix: without the body frame the frame's place; the original then reads the null body
        // frame all the same.
        I3D_frame *frame_ = meleeTracker->GetFrame();
        if (!frame_)
            return false;
        eye = frame_->GetWorldPos();
    }
    if (!partFrame)
        return false;
    const S_vector dir = partFrame->GetWorldPos() - eye;
    S_vector hitPos;
    const S_CollisionVolume *hit = g_collisionWorld.RayTest(eye, dir, &hitPos, nullptr,
                                                            RayTypes(target, SIGHT_RAY_TYPES_MELEE_TRACKER), SIGHT_RAY_FLAGS);
    // (A meleeTracker sees the target when its ray meets it.)
    if (hit && (hit->flags & COLLISION_FLAG_DYNAMIC) && hit->actor == other)
        return true;
    record.view[part] = 0.0f;
    return false;
}

void C_MeleeTrackerSightObserver::Update(uint32_t, int target, int timeDelta)   // 0x0046dea0
{
    if (disabled)
        return;
    S_SightRecord &record = records[target];
    if (record.relation != SIGHT_HOSTILE)
        return;
    const uint32_t before = record.parts;
    record.seenBefore = record.seen;
    if (TestPart(target, timeDelta)) {
        record.parts |= 1u << record.part;
        record.visible = true;
    } else {
        record.parts &= ~(1u << record.part);
        record.visible = false;
    }
    if (before == 0) {
        if (record.parts != 0)
            meleeTracker->OnSightEvent(0, target);
    } else if (record.parts == 0) {
        meleeTracker->OnSightEvent(1, target);
    }
}

// ---- the module ------------------------------------------------------------------------------

C_SightModule::C_SightModule(int id, const char *name) : C_Module(id, name)   // 0x0046ee90
{
}

C_SightModule::~C_SightModule() = default;              // 0x0046ef40 (the vectors only)

C_SightTarget *C_SightModule::GetTarget(int id) const
{
    if (id < 0 || static_cast<size_t>(id) >= m_targets.size())
        return nullptr;
    C_SightTarget *target = m_targets[id];
    return target && target->mask != 0 ? target : nullptr;
}

void C_SightModule::Clear()                             // 0x0046ffb0
{
    for (size_t i = m_observers.size(); i-- > 0;)
        delete m_observers[i];
    m_observers.clear();
    for (size_t i = m_targets.size(); i-- > 0;)
        delete m_targets[i];
    m_targets.clear();
}

void C_SightModule::LoadMissionData(bool server)        // 0x0046eee0
{
    if (server)
        Clear();
}

void C_SightModule::FreeMissionData(bool server)        // 0x0046eef0
{
    if (server)
        Clear();
}

void C_SightModule::StartMission(bool server)           // 0x0046efd0
{
    if (server) {
        m_observerCursor = 0;
        m_targetCursor = 0;
        m_startDelay = 4000;
    }
}

void C_SightModule::StopMission(bool server)            // 0x0046eff0
{
    if (server)
        Clear();
}

void C_SightModule::RestartMission(bool)                // 0x00470090
{
    Clear();
    m_startDelay = 4000;
}

void C_SightModule::Tick(int timeDelta)                 // 0x0046f000
{
    if (m_startDelay != 0) {
        m_startDelay -= timeDelta;
        if (m_startDelay <= 0)
            m_startDelay = 0;
        return;
    }
    g_sightTime = static_cast<float>(timeDelta) + g_sightTime;
    const float pairs = static_cast<float>(static_cast<int>(m_targets.size() * m_observers.size()));
    int budget = static_cast<int>(g_sightTime * 0.001f * pairs);
    if (budget == 0)
        return;
    g_sightTime = g_sightTime - 1000.0f / pairs * static_cast<float>(budget);
    if (m_targetCursor >= m_targets.size())
        m_targetCursor = 0;
    const size_t first = m_targetCursor;
    C_SightTarget *t = m_targets[m_targetCursor];
    if (!t)
        return;
    for (;;) {
        const uint32_t testedBy = t->testedBy;
        if (t->mask != 0) {
            const int own = t->observer;
            if (own != -1) {
                for (;;) {
                    if (m_observerCursor >= m_observers.size()) {
                        m_observerCursor = 0;
                        break;
                    }
                    C_SightObserver *o = m_observers[m_observerCursor];
                    if (!o)
                        return;
                    const int reverse = o->target;
                    if (static_cast<size_t>(own) != m_observerCursor && o->mask != 0) {
                        C_SightTarget *current = m_targets[m_targetCursor];
                        if (!current)
                            return;
                        const uint32_t bits = current->testedBy & o->mask;
                        const bool wants = o->WantsTest(static_cast<int>(m_targetCursor));
                        if ((bits != 0 || wants) && !m_targets[m_targetCursor]->hidden)
                            o->Update(bits, static_cast<int>(m_targetCursor), timeDelta);
                        // The other way round: the target's owner looking at the observer's.
                        if (reverse != -1) {
                            C_SightObserver *back = m_observers[own];
                            const uint32_t backMask = back->mask;
                            back->WantsTest(reverse);   // (the answer is not used)
                            if (backMask != 0 && !m_targets[reverse]->hidden) {
                                if (!m_observers[own])
                                    return;
                                m_observers[own]->Update(backMask, reverse, timeDelta);
                            }
                        }
                    }
                    ++m_observerCursor;
                    if (budget <= 0)
                        return;
                    --budget;
                }
            }
        }
        if (++m_targetCursor >= m_targets.size())
            m_targetCursor = 0;
        if (m_targetCursor == first)
            return;
        if (testedBy == 0) {
            // Quirk kept: the budget is charged for the observers left (and thanked for those
            // gone past the end).
            budget += static_cast<int>(m_observerCursor) - static_cast<int>(m_observers.size());
            m_observerCursor = 0;
            if (budget <= 0)
                return;
        }
        t = m_targets[m_targetCursor];
        if (!t)
            return;
    }
}

bool C_SightModule::TestNow(int observer, int target)   // 0x0046f280
{
    return m_observers[observer]->TestNow(target);
}

void C_SightModule::GetVisibleParts(int observer, int target, uint32_t *parts)   // 0x0046f2a0
{
    m_observers[observer]->GetVisibleParts(target, parts);
}

int C_SightModule::CanSee(int observer, int target, int *before, bool *visible)   // 0x0046f320
{
    return m_observers[observer]->CanSee(target, before, visible);
}

int C_SightModule::AddObserver(I3D_frame *frame, uint32_t mask)   // 0x0046f350
{
    // A free slot from the end (the frame must not be registered above it).
    size_t slot = m_observers.size();
    for (size_t i = m_observers.size(); i-- > 0;) {
        C_SightObserver *o = m_observers[i];
        if (!o)
            continue;
        if (o->frame == frame)
            return -1;
        if (o->mask == 0) {
            slot = i;
            break;
        }
    }
    if (!frame)
        return -1;
    C_Actor *actor = GetFrameActor(frame);
    if (!actor)
        return -1;
    C_SightObserver *observer;
    if (actor->GetType() == ACTOR_HUMAN)
        observer = new C_HumanSightObserver(static_cast<C_Human *>(actor), mask, static_cast<int>(slot));
    else if (actor->GetType() == ACTOR_MELEE_TRACKER)
        observer = new C_MeleeTrackerSightObserver(static_cast<C_MeleeTracker *>(actor), mask, static_cast<int>(slot));
    else
        return -1;
    if (slot == m_observers.size()) {
        m_observers.push_back(observer);
    } else {
        delete m_observers[slot];
        m_observers[slot] = observer;
    }
    const int id = static_cast<int>(slot);
    for (size_t t = 0; t < m_targets.size(); ++t) {
        C_SightTarget *target = m_targets[t];
        // Quirk kept: a null target gets no record (the later ones then fall out of step).
        if (!target)
            continue;
        if (!target->frame) {
            observer->SetRelation(static_cast<int>(t), SIGHT_NEUTRAL);
            continue;
        }
        if (target->frame == frame) {
            target->observer = id;
            observer->target = static_cast<int>(t);
        }
        C_Actor *other = GetFrameActor(target->frame);
        if (!other) {
            observer->SetRelation(static_cast<int>(t), SIGHT_HOSTILE);
            continue;
        }
        if (other == actor) {
            observer->SetRelation(static_cast<int>(t), SIGHT_FRIEND);
            continue;
        }
        bool isFriend, isHostile;
        Relation(actor, other, &isFriend, &isHostile);
        if (isFriend) {
            observer->SetRelation(static_cast<int>(t), SIGHT_FRIEND);
            NotifyFound(actor, static_cast<int>(t));
        } else {
            observer->SetRelation(static_cast<int>(t), isHostile ? SIGHT_HOSTILE : SIGHT_NEUTRAL);
        }
    }
    return id;
}

int C_SightModule::AddTarget(I3D_frame *frame, uint32_t mask)   // 0x0046f9d0
{
    if (mask == 0 || mask > 0xF)
        return -1;
    size_t slot = m_targets.size();
    for (size_t i = m_targets.size(); i-- > 0;) {
        C_SightTarget *t = m_targets[i];
        if (!t)
            continue;
        if (t->frame == frame)
            return -1;
        if (t->mask == 0) {
            slot = i;
            break;
        }
    }
    C_Actor *actor = frame ? GetFrameActor(frame) : nullptr;
    C_SightTarget *target = nullptr;
    if (actor && actor->GetType() == ACTOR_HUMAN)
        target = new C_HumanSightTarget(static_cast<C_Human *>(actor), mask, static_cast<int>(slot));
    else if (actor && actor->GetType() == ACTOR_MELEE_TRACKER)
        target = new C_MeleeTrackerSightTarget(static_cast<C_MeleeTracker *>(actor), mask, static_cast<int>(slot));
    // Quirk kept: a frame of another actor (or none) registers a null target.
    if (slot == m_targets.size()) {
        m_targets.push_back(target);
    } else {
        delete m_targets[slot];
        m_targets[slot] = target;
    }
    const int id = static_cast<int>(slot);
    for (size_t i = m_observers.size(); i-- > 0;) {
        C_SightObserver *o = m_observers[i];
        if (!o || o->mask == 0)
            continue;
        if (o->frame == frame) {
            if (C_SightTarget *t = m_targets[slot])
                t->observer = static_cast<int>(i);
            o->target = id;
        }
        if (!target)
            continue;
        int relation = SIGHT_HOSTILE;
        if (target->GetKind() == 0 || target->GetKind() == 1) {
            // (The original takes a removed observer's actor as null and then reads it.)
            C_Actor *watcher = o->frame ? GetFrameActor(o->frame) : nullptr;
            if (watcher == actor) {
                relation = SIGHT_FRIEND;
            } else if (!watcher || (watcher->GetType() != ACTOR_HUMAN && watcher->GetType() != ACTOR_MELEE_TRACKER)) {
                relation = SIGHT_NEUTRAL;
            } else {
                bool isFriend, isHostile;
                Relation(watcher, actor, &isFriend, &isHostile);
                if (isFriend) {
                    o->SetRelation(id, SIGHT_FRIEND);
                    NotifyFound(watcher, id);
                    continue;
                }
                relation = isHostile ? SIGHT_HOSTILE : SIGHT_NEUTRAL;
            }
        }
        o->SetRelation(id, relation);
    }
    return id;
}

bool C_SightModule::RemoveObserver(int id)              // 0x0046fd50
{
    if (id < 0 || static_cast<size_t>(id) >= m_observers.size() || !m_observers[id])
        return false;
    C_SightObserver *o = m_observers[id];
    o->mask = 0;
    o->frame = nullptr;
    if (o->target != -1) {
        m_targets[o->target]->observer = -1;
        o->target = -1;
    }
    o->records.clear();
    return true;
}

bool C_SightModule::RemoveTarget(int id)                // 0x0046fdf0
{
    if (id < 0 || static_cast<size_t>(id) >= m_targets.size())
        return false;
    for (size_t i = m_observers.size(); i-- > 0;) {
        C_SightObserver *o = m_observers[i];
        if (!o || o->mask == 0 || !o->frame)
            continue;
        S_SightRecord &record = o->records[id];
        if (record.parts != 0) {
            record.parts = 0;
            record.seen = 0;
            NotifyLostByHuman(o, id);
        }
    }
    C_SightTarget *t = m_targets[id];
    if (!t)
        return false;
    t->mask = 0;
    t->testedBy = 0;
    t->frame = nullptr;
    if (t->observer != -1) {
        m_observers[t->observer]->target = -1;
        t->observer = -1;
    }
    t->parts.clear();
    return true;
}

void C_SightModule::SetObserverState(int id, int state) // 0x0046f8c0
{
    if (id < 0 || static_cast<size_t>(id) >= m_observers.size() || m_observers[id]->mask == 0)
        return;
    C_SightObserver *o = m_observers[id];
    if (state == SIGHT_OBSERVER_OFF) {
        o->disabled = true;
        for (size_t i = o->records.size(); i-- > 0;) {
            if (o->records[i].parts != 0)
                NotifyLostByHuman(o, static_cast<int>(i));
            o->records[i].parts = 0;
            o->records[i].seen = 0;
        }
    } else if (state == SIGHT_OBSERVER_ON) {
        o->disabled = false;
    }
}

void C_SightModule::SetTargetState(int id, int state)   // 0x0046f700
{
    C_SightTarget *t = GetTarget(id);
    if (!t)
        return;
    switch (state) {
    case SIGHT_TARGET_ALIVE:
        t->alive = true;
        break;
    case SIGHT_TARGET_NOT_ALIVE:
        t->alive = false;
        break;
    case SIGHT_TARGET_DEAD:
        // The friends test the body from now on (the observer's own target excepted, it is told
        // that it lost sight of itself).
        for (size_t i = m_observers.size(); i-- > 0;) {
            C_SightObserver *o = m_observers[i];
            if (o->mask == 0 || o->records[id].relation != SIGHT_FRIEND)
                continue;
            o->records[id].relation = SIGHT_HOSTILE;
            if (C_Actor *actor = o->frame ? GetFrameActor(o->frame) : nullptr) {
                if (actor->GetType() == ACTOR_HUMAN && o->target != id)
                    static_cast<C_Human *>(actor)->OnSightEvent(1, id);
            }
        }
        break;
    case SIGHT_TARGET_HIDDEN:
        t->hidden = true;
        for (size_t i = m_observers.size(); i-- > 0;) {
            C_SightObserver *o = m_observers[i];
            if (!o || !o->frame || o->mask == 0)
                continue;
            if (o->records[id].parts != 0)
                NotifyLostByHuman(o, id);
            o->records[id].parts = 0;
            o->records[id].seen = 0;
        }
        break;
    case SIGHT_TARGET_SHOWN:
        t->hidden = false;
        break;
    }
}
