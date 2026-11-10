// The sight module ("Sight", module 5; the object 0x0089b288; game/sight.cpp, original source
// 0x0046d5d0..0x004702f0): who sees whom. Humans and meleeTrackers register as observers and as
// targets; every observer keeps a record per target. Friends count as seen from the start,
// hostiles are tested: the observer's view test (C_Human::TestSightOfHuman / ...OfMeleeTracker,
// the meleeTracker's AI), then a ray to one of the target's body parts (its collision volume
// frames "d", "z" (else "f"), "i", "m", "q"; a meleeTracker's "c", "f", "k", "o"), a part a test.
// The tick spends a budget of tests, each pair once a second, and calls the owners back
// (C_Human::OnSightEvent, C_MeleeTracker::OnSightEvent) when a target comes into view or leaves it.
// The server side runs it.
#pragma once

#include <cstdint>
#include <vector>

#include "game/module.h"

class C_Actor;
class C_Human;
class C_MeleeTracker;
class I3D_frame;

// What humans and meleeTrackers register with (the observers' and targets' mask).
enum : uint32_t {
    SIGHT_ALL = 0xF,
};
// C_SightModule::SetObserverState() states.
enum : int {
    SIGHT_OBSERVER_OFF = 0,                             // nothing seen any more, no tests
    SIGHT_OBSERVER_ON = 1,
};
// C_SightModule::SetTargetState() states.
enum : int {
    SIGHT_TARGET_ALIVE = 0,                             // light objects (collision type 2) block the view
    SIGHT_TARGET_NOT_ALIVE = 1,                         // they do not (not used by the game)
    SIGHT_TARGET_DEAD = 2,                              // the friends test it again: seen as a body
    SIGHT_TARGET_HIDDEN = 3,                            // frozen / suspended: out of everyone's view
    SIGHT_TARGET_SHOWN = 4,
};
// C_SightModule::GetVisibleParts() bits: the parts in the order of the targets' tables.
enum : uint32_t {
    SIGHT_PART_BODY = 0x01,
    SIGHT_PART_HEAD = 0x02,
    SIGHT_PART_LEFT_ARM = 0x04,
    SIGHT_PART_RIGHT_ARM = 0x08,
    SIGHT_PART_LEGS = 0x10,
};
// An observer's relation to a target (S_SightRecord::relation).
enum : int {
    SIGHT_FRIEND = 0,                                   // seen, not tested (oneself too)
    SIGHT_NEUTRAL = 1,                                  // not tested
    SIGHT_HOSTILE = 2,                                  // tested
};

// An observer's record of a target (0x68 bytes).
struct S_SightRecord {
    uint32_t parts = 0;                                 // +0x00 the parts seen (bit per part)
    int seen = 0;                                       // +0x04 how many (as counted: see Update)
    int seenBefore = 0;                                 // +0x08 the count before the last update
    bool visible = false;                               // +0x0C the last part tested was seen
    int part = 0;                                       // +0x10 the part tested last
    int relation = SIGHT_FRIEND;                        // +0x14 SIGHT_FRIEND / NEUTRAL / HOSTILE
    // Per part: what the meleeTrackers' view test wrote (100: in view; the humans' stay 0 or
    // are reset to 0), and the times of the last and the next test (mission ms).
    float view[5] = {};                                 // +0x18
    uint32_t lastTest[5] = {};                          // +0x2C
    uint32_t nextTest[5] = {};                          // +0x40
    uint32_t unused54[5] = {};                          // +0x54
};

// A registered target (0x34 bytes; base vtable 0x0080ffa4, ctor 0x0046eac0).
class C_SightTarget {
public:
    C_SightTarget(I3D_frame *frame, uint32_t mask, int index);   // 0x0046eac0
    virtual ~C_SightTarget() = default;                 // (0x0046ecd0 frees the parts)
    C_SightTarget(const C_SightTarget &) = delete;
    C_SightTarget &operator=(const C_SightTarget &) = delete;

    virtual C_Actor *GetActor() = 0;                    // vtable +0x00
    virtual int GetKind() = 0;                          // vtable +0x04 0 human, 1 meleeTracker
    // The parts looked up again (the human's model changed).
    virtual void Refresh() {}                           // vtable +0x08 0x0046eb10

    I3D_frame *frame;                                   // +0x04 (null: removed)
    uint32_t mask;                                      // +0x08 (0: a free slot)
    int index;                                          // +0x0C
    // Observers' masks that test it whatever their record says (nothing in the game sets it).
    uint32_t testedBy = 0;                              // +0x10
    int observer = -1;                                  // +0x14 its owner's observer
    std::vector<I3D_frame *> parts;                     // +0x18 the body parts' frames
    bool alive = false;                                 // +0x28 SIGHT_TARGET_ALIVE
    bool hidden = false;                                // +0x29 SIGHT_TARGET_HIDDEN
    int partCount = 0;                                  // +0x2C 5 / 4
};

class C_HumanSightTarget : public C_SightTarget {       // vtable 0x0080ffb0, 0x34 bytes
public:
    C_HumanSightTarget(C_Human *human, uint32_t mask, int index);   // 0x0046ec40
    C_Actor *GetActor() override;                       // 0x0046ecb0
    int GetKind() override { return 0; }                // 0x0046ecc0 (DS 0x0048a7b0)
    void Refresh() override;                            // 0x0046eb20

    C_Human *human;                                     // +0x30
};

class C_MeleeTrackerSightTarget : public C_SightTarget {   // vtable 0x0080ffbc, 0x34 bytes
public:
    C_MeleeTrackerSightTarget(C_MeleeTracker *meleeTracker, uint32_t mask, int index);   // 0x0046ecf0
    C_Actor *GetActor() override;                       // 0x0046edc0
    int GetKind() override { return 1; }                // 0x0046edd0 (DS 0x0048a930)

    C_MeleeTracker *meleeTracker;                       // +0x30
};

// A registered observer (0x2C bytes; base vtable 0x0080ff68, ctor 0x0046d6f0).
class C_SightObserver {
public:
    C_SightObserver(I3D_frame *frame, uint32_t mask, int index);   // 0x0046d6f0
    virtual ~C_SightObserver() = default;               // (0x004700b0 / 0x00470070 free the records)
    C_SightObserver(const C_SightObserver &) = delete;
    C_SightObserver &operator=(const C_SightObserver &) = delete;

    // The tick's test of the target (`mask`: the target's testedBy bits that matched, unused).
    virtual void Update(uint32_t mask, int target, int timeDelta) = 0;   // vtable +0x00
    // Whether the record wants a test (the next part's view is not 100).
    virtual bool WantsTest(int target) = 0;             // vtable +0x04
    // Tests now (true: seen, or seen already).
    virtual bool TestNow(int target) = 0;               // vtable +0x08
    // The count of parts seen (`before`: the count before; friends: every part, visible).
    virtual int CanSee(int target, int *before, bool *visible) = 0;   // vtable +0x0C
    // The target's parts and the bits seen (friends: all five).
    virtual const std::vector<I3D_frame *> *GetVisibleParts(int target, uint32_t *parts) = 0;   // vtable +0x10

    // Resets record `target` to `relation` (appended when it is the next one; false past it).
    bool SetRelation(int target, int relation);         // 0x0046d600

    I3D_frame *frame;                                   // +0x04 (null: removed)
    uint32_t mask;                                      // +0x08 (0: a free slot)
    int index;                                          // +0x0C
    int target = -1;                                    // +0x10 its owner's target
    std::vector<S_SightRecord> records;                 // +0x14 per target
    bool disabled = false;                              // +0x24 SIGHT_OBSERVER_OFF
};

class C_HumanSightObserver : public C_SightObserver {   // vtable 0x0080ff7c, 0x2C bytes
public:
    C_HumanSightObserver(C_Human *human, uint32_t mask, int index);   // 0x0046d730
    void Update(uint32_t mask, int target, int timeDelta) override;   // 0x0046dcf0
    bool WantsTest(int target) override;                // 0x0046d840
    bool TestNow(int target) override;                  // 0x0046d8c0
    int CanSee(int target, int *before, bool *visible) override;   // 0x0046db60
    const std::vector<I3D_frame *> *GetVisibleParts(int target, uint32_t *parts) override;   // 0x0046dc10

    C_Human *human;                                     // +0x28

private:
    // The next part's ray (true: seen; `skipped`: not due yet, the old answer kept).
    bool TestPart(int target, int timeDelta, bool *skipped);   // 0x0046df80
};

class C_MeleeTrackerSightObserver : public C_SightObserver {   // vtable 0x0080ff90, 0x2C bytes
public:
    C_MeleeTrackerSightObserver(C_MeleeTracker *meleeTracker, uint32_t mask, int index);   // 0x0046d760
    void Update(uint32_t mask, int target, int timeDelta) override;   // 0x0046dea0
    bool WantsTest(int target) override;                // 0x0046dc70
    bool TestNow(int) override { return true; }         // 0x0046d790
    int CanSee(int, int *, bool *) override { return 0; }   // 0x0046d7a0
    // No parts (a static empty vector; the bits untouched).
    const std::vector<I3D_frame *> *GetVisibleParts(int target, uint32_t *parts) override;   // 0x0046d7b0

    C_MeleeTracker *meleeTracker;                       // +0x28

private:
    bool TestPart(int target, int timeDelta);           // 0x0046e3b0
};

class C_SightModule : public C_Module {                // vtable 0x0080ffc8
public:
    C_SightModule(int id, const char *name);            // 0x0046ee90
    ~C_SightModule() override;                          // 0x0046ef40

    // The budget of tests (each pair once a second), after a 4 s wait at the mission's start.
    void Tick(int timeDelta) override;                  // 0x0046f000
    // (The server side drops everything.)
    void LoadMissionData(bool server) override;         // 0x0046eee0
    void FreeMissionData(bool server) override;         // 0x0046eef0
    void StartMission(bool server) override;            // 0x0046efd0
    void StopMission(bool server) override;             // 0x0046eff0
    void RestartMission(bool server) override;          // 0x00470090
    bool SaveGame(C_chunk &) override { return true; }  // 0x0046ef00 (DS 0x0048ab10)
    bool LoadGame(C_chunk &) override { return true; }  // 0x0046ef10 (DS 0x0048ab30)

    // Registration ids (a free slot reused, else appended); -1 on failure (the frame registered
    // already, no human or meleeTracker; a target's mask 0 or over 15).
    int AddObserver(I3D_frame *frame, uint32_t mask);   // 0x0046f350
    int AddTarget(I3D_frame *frame, uint32_t mask);     // 0x0046f9d0
    bool RemoveObserver(int id);                        // 0x0046fd50
    bool RemoveTarget(int id);                          // 0x0046fdf0
    void SetObserverState(int id, int state);           // 0x0046f8c0
    void SetTargetState(int id, int state);             // 0x0046f700
    // The observer's answers (its vtable +0x08, +0x0C, +0x10).
    bool TestNow(int observer, int target);             // 0x0046f280
    int CanSee(int observer, int target, int *before, bool *visible);   // 0x0046f320
    void GetVisibleParts(int observer, int target, uint32_t *parts);   // 0x0046f2a0

    // Registered target `id`, null if there is none (inlined by the callers).
    C_SightTarget *GetTarget(int id) const;
    // Ms left of the 4 s the module waits at a mission's start (the air attacks wait while
    // over 2000).
    int GetStartDelay() const { return m_startDelay; }

private:
    friend class C_HumanSightObserver;
    friend class C_MeleeTrackerSightObserver;

    void Clear();                                       // 0x0046ffb0

    // (+0x04 the C_Module part.)
    std::vector<C_SightTarget *> m_targets;             // +0x0C
    std::vector<C_SightObserver *> m_observers;         // +0x1C
    size_t m_observerCursor = 0;                        // +0x2C the tick's next observer
    size_t m_targetCursor = 0;                          // +0x30 its target
    int m_startDelay = 0;                               // +0x34 ms
};
extern C_SightModule g_sightModule;                     // 0x0089b288 (static init 0x0046ee60)
// Ms gathered towards the next tests (starts at 1).
extern float g_sightTime;                               // 0x00839390
