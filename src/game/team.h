// Teams of humans: the player's squad (C_Team), the players of a co-operative game
// (C_CoopTeam), the formations the mission scripts give their AI humans (C_Formation), and
// the module that keeps them all (C_TeamManager "TeamMgr", g_teamManager).
//
// Single player: the squad's soldiers with the role HUMAN_ROLE_PLAYER take turns as the
// active (controlled) one; one of the others can be the commanded one (the target of the
// squad orders). The teams also keep the enemies their members have seen (C_EnemyRecords),
// which the interface's map and the squad AI use.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "game/module.h"
#include "ls3d/math.h"

class C_Actor;
class C_chunk;
class C_MeleeTracker;
class C_Human;
class I3D_frame;

// ---------------------------------------------------------------------------
// Formations

enum E_FormationType {
    FORMATION_FOLLOW = 3,                               // humans following a leader (C_Human::SetLeader)
    FORMATION_LINE = 5,
    FORMATION_COLUMN = 6,
    FORMATION_QUAD_2X3 = 7,
    FORMATION_QUAD_3X2 = 8,
    FORMATION_ARROW_SHARP = 9,
    FORMATION_ARROW_INVERT = 10,
    FORMATION_NEXT = 12,                                // SetType: the next of types 1..10
    FORMATION_NONE = 13,                                // frees the slots
};

// A member's place in a formation (0xA0 bytes).
struct alignas(16) S_FormationSlot {
    S_vector4 basePos = {};                             // +0x00 from the table
    S_vector4 scaledPos = {};                           // +0x10 by the shape's matrix
    S_vector4 target = {};                              // +0x20 where it should be
    S_vector4 current = {};                             // +0x30 moving toward it
    S_vector4 dir = {};                                 // +0x40 faces
    S_vector4 baseDir = {};                             // +0x50 from the table
    S_vector4 scaledDir = {};                           // +0x60 by the shape's matrix
    C_Human *member = nullptr;                          // +0x70
    C_Actor *unknown74 = nullptr;                       // +0x74 (only saved and loaded)
    float distance = 0.0f;                              // +0x78 from the first slot, by the shape
    bool pushedAside = false;                           // +0x7C
    uint16_t checkpoint = 0xFFFF;                       // +0x7E
    int seat = 0;                                       // +0x80 in a vehicle
    int vehicleId = -1;                                 // +0x84
    bool dirty = false;                                 // +0x88 the place has changed
    std::vector<uint16_t> path;                         // +0x8C to the first slot's checkpoint
    uint16_t pathFrom = 0xFFFF;                         // +0x9C its checkpoint when found
};

// Shape of a formation and its member slots (0xA0 bytes, 16-byte aligned; humans that lead
// one keep it in C_Human::GetFormation()). game/formation.cpp.
class alignas(16) C_FormationShape {
public:
    static constexpr int SLOT_COUNT = 6;

    C_FormationShape();                                 // 0x005949c0
    ~C_FormationShape();                                // 0x00594a70 (SetType(FORMATION_NONE))
    void SetType(E_FormationType type);                 // 0x00597420
    void SetScale(const S_vector &scale);               // 0x005977f0
    E_FormationType GetType() const { return m_type; }  // 0x00597b40 (+0x78)
    int GetMemberCount() const { return m_memberCount; }   // +0x7C
    bool AddMember(C_Human *member);                    // 0x00597f80
    bool RemoveMember(C_Human *member);                 // 0x005981c0
    bool SetLeader(C_Human *leader);                    // 0x00597c50
    C_Human *GetLeader() const { return m_leader; }     // +0x74
    // Squad members of the formation that hold (squad commands 0xC..0xF) switch to 0x10 / 0x11.
    void OnLeaderDied();                                // 0x00595710
    void Tick(int timeDelta);                           // 0x0059ba70
    bool SaveGame(C_chunk &chunk);                      // 0x005942d0
    bool LoadGame(C_chunk &chunk);                      // 0x00594560
    // Byte +0x8C: set for the scripts' formations (C_Formation). While it is clear the shape
    // plans its slots again whenever the leader moves (0x0059af30; meaning inferred).
    void SetUnknown8C(bool on) { m_scripted = on; }
    // The slot array exists (+0x90, SLOT_COUNT entries of 0xA0 bytes).
    bool HasSlots() const { return m_slots != nullptr; }
    // Human in slot `slot` (+0x70 of the entry), null if free.
    C_Human *GetSlotMember(int slot) const { return m_slots[slot].member; }
    // The slot of `member` (false: none).
    bool FindSlot(const C_Human *member, int *slot) const;              // 0x00598290
    // Where `member`'s slot is and its walking point (true only when the place has changed since last asked).
    bool GetSlotTarget(const C_Human *member, S_vector4 *pos, uint16_t *checkpoint);   // 0x00598320
    // The direction `member`'s slot faces (false: no slot).
    bool GetSlotDir(const C_Human *member, S_vector4 *dir);             // 0x005983c0
    // The vehicle seat of `member`'s slot (the vehicle's id in `*vehicleId`; 0 without one).
    int GetSlotSeat(const C_Human *member, int *vehicleId);             // 0x00598460
    // The slot's place is planned again (byte +0x88 of the entry).
    void InvalidateSlot(const C_Human *member);                         // 0x005982d0
    // Puts `member`'s slot at `pos` (no walking point; planned again).
    void SetSlotTarget(const C_Human *member, const S_vector4 &pos);    // 0x00599040
    // Plans slot `slot`'s place on the walking network: as far along the way from the first slot as the shape says.
    void PlanSlot(int slot);                                            // 0x0059af30
    // Lines up around a door's point instead (0xFFFF: back to the shape).
    void SetDoorPoint(uint16_t checkpoint) { m_doorState = -1; m_doorPoint = checkpoint; }   // 0x00594a50

private:
    void SetDir(const S_vector &dir);                   // 0x00597760
    void ApplyMatrix();                                 // 0x005978e0
    void MoveSlots(int timeDelta);                      // 0x0059bb60
    // (0x005990c0; a stack argument in the original, not used)
    void FollowLeader();
    // (0x0059a2e0; a stack argument in the original, not used)
    void LineUpAtDoor();
    void BoardVehicle(C_Actor *vehicle);                // 0x005957d0
    void KeepApart();                                   // 0x00598550
    void PushAside(int slot, float distance);           // 0x00598810
    bool FitOnNetwork(int slot);                        // 0x0059b7b0

    S_matrix m_matrix;                                  // +0x00
    S_vector4 m_dir = {};                               // +0x40
    S_vector4 m_scale = {};                             // +0x50
    S_vector4 m_lastDir = {};                           // +0x60
    bool m_changed = false;                             // +0x70
    C_Human *m_leader = nullptr;                        // +0x74
    E_FormationType m_type = FORMATION_FOLLOW;          // +0x78
    int m_memberCount = 0;                              // +0x7C
    int m_mode = 0;                                     // +0x80 1 by the shape, 2 at a door
    uint16_t m_doorPoint = 0xFFFF;                      // +0x84
    int m_doorState = -1;                               // +0x88
    bool m_scripted = false;                            // +0x8C
    S_FormationSlot *m_slots = nullptr;                 // +0x90
};
// Scale of the formations of humans following a leader.
extern S_vector g_defaultFormationScale;                // 0x0089ae50

// ---------------------------------------------------------------------------
// Enemy records

// An enemy a member of the team has seen (0x10 bytes).
struct S_EnemyRecord {
    C_Human *enemy;                                     // +0x00
    int seenBy;                                         // +0x04 members that see it now
    bool alive;                                         // +0x08 cleared when it is recorded dead
    int forgetTime;                                     // +0x0C ms left once nobody sees it (0: kept)
};

// The enemies a team knows (C_Team +0x38, the co-op team +0x28; 0x20 bytes). Records of
// living enemies come first: [0, GetAliveCount()). Members report what they see (Add / Remove
// from their controllers); an enemy nobody sees any more is forgotten after FORGET_TIME.
class C_EnemyRecords {
public:
    static constexpr int FORGET_TIME = 10000;           // ms
    static constexpr int FADE_TIME = 200;               // ms, the tactical view's fades

    C_EnemyRecords() = default;
    ~C_EnemyRecords();                                  // (inline in the teams' destructors)
    C_EnemyRecords(const C_EnemyRecords &) = delete;
    C_EnemyRecords &operator=(const C_EnemyRecords &) = delete;

    // A member sees `enemy` (again).
    void Add(C_Human *enemy);                           // 0x0045e410
    // A member does not see `enemy` any more.
    void Remove(C_Human *enemy);                        // 0x0045e540
    // Counts down the forget times; forgotten records are dropped.
    void Update(int timeDelta);                         // 0x0045e2f0
    // Update() unless this machine is a client.
    void UpdateOnServer(int timeDelta);                 // 0x0045e380
    // Drops every record (the teams' Clear / RestartMission inline it).
    void Clear();
    bool Contains(const C_Human *enemy) const;
    // The tactical view (the interface's planning map) is up: enemies fade in when they are
    // seen and out when they are lost (byte +0x1C; the interface sets it directly).
    void SetTacticalView(bool on) { m_tacticalView = on; }
    // The list changed during the last Add / Update (byte +0x10).
    bool HasChanged() const { return m_changed; }
    int GetCount() const { return static_cast<int>(m_records.size()); }
    const S_EnemyRecord *GetRecord(int index) const { return m_records[index]; }
    int GetAliveCount() const { return m_aliveCount; }
    int GetDeadCount() const { return m_deadCount; }

private:
    // Moves the record of `enemy` behind the living ones.
    void MarkDead(C_Human *enemy);                      // 0x0045e3b0

    std::vector<S_EnemyRecord *> m_records;             // +0x00 (first +0x04)
    bool m_changed = false;                             // +0x10
    int m_aliveCount = 0;                               // +0x14
    int m_deadCount = 0;                                // +0x18 (counted, not read)
    bool m_tacticalView = false;                        // +0x1C
};

// ---------------------------------------------------------------------------
// Teams

// Members of a team (vtable 0x0080fcd8, 9 slots; 0x28 bytes).
class C_TeamBase {
public:
    explicit C_TeamBase(int id);                        // 0x004617f0
    // Drops the members.
    virtual void Clear();                               // 0x00 0x004619c0
    virtual ~C_TeamBase();                              // 0x04 0x00461910 (0x00461930)
    // Adds `human` unless it is null or a member already.
    virtual bool AddMember(C_Human *human);             // 0x08 0x00461aa0
    // Removes `human` (the last member takes its place); `index` (may be null) gets the index
    // it had.
    virtual bool RemoveMember(C_Human *human, int *index);   // 0x0C 0x00461c70
    // Every frame / every game tick.
    virtual void Update(int /*timeDelta*/) {}           // 0x10 0x00461d20
    virtual void Tick(int /*timeDelta*/) {}             // 0x14 0x00461d10
    // Single player: Clear() (`server` is not used).
    virtual void RestartMission(bool server);           // 0x18 0x00461d30
    // Chunk 0x9A4: the members' actor ids and the name.
    virtual bool SaveGame(C_chunk &chunk);              // 0x1C 0x00461d50
    virtual bool LoadGame(C_chunk &chunk);              // 0x20 0x00461f30

    bool IsMember(const C_Human *human) const;          // 0x00461a20
    // Index of `human`, -1 if it is not a member.
    int IndexOf(const C_Human *human) const;            // 0x00461a60
    int GetMemberCount() const;                         // 0x00462300
    C_Human *GetMember(int index) const;                // 0x004619f0 (null when out of range)
    int GetAliveCount() const;                          // 0x004622c0
    int GetId() const { return m_id; }

protected:
    std::vector<C_Human *> m_members;                   // +0x04 (first +0x08)
    std::string m_name;                                 // +0x14 saved and loaded; nothing else uses it
    int m_id;                                           // +0x24 formations: their index; teams: -1
};

// The player's squad (vtable 0x0080fc20, 10 slots; 0xCC bytes). In multiplayer it holds the
// players' humans; the active player is this machine's own.
class C_Team : public C_TeamBase {
public:
    // Distance (m) within which members share what they see (GetEnemySeenNearby & co.).
    static constexpr float NEARBY_DISTANCE = 40.0f;

    C_Team();                                           // 0x0045d1f0
    ~C_Team() override;                                 // 0x0045d2a0 (0x0045d2e0)
    // Also the selection, the records, the volumes and the meleeTrackers.
    void Clear() override;                              // 0x0045d3a0
    // Also the interface's member panel and the member's collision volumes.
    bool AddMember(C_Human *human) override;            // 0x0045e5d0
    using C_TeamBase::RemoveMember;
    // Forgets enemies nobody has seen for a while.
    void Update(int timeDelta) override;                // 0x0045ddc0
    // Also the environment check (deadly / restricted areas, single player).
    void Tick(int timeDelta) override;                  // 0x0045dd90
    // Keeps the members; drops the selection (an only member becomes the active one).
    void RestartMission(bool server) override;          // 0x0045dde0
    // Chunk 0xDFB { 1000 { C_TeamBase } }.
    bool SaveGame(C_chunk &chunk) override;             // 0x0045de90
    bool LoadGame(C_chunk &chunk) override;             // 0x0045e040
    // Removes `human` from the team and the interface; fixes the selection.
    virtual bool RemoveMember(C_Human *human);          // 0x24 0x0045e670

    // Adds (`member`) or removes an allied NPC.
    void SetNPCMember(C_Human *npc, bool member);       // 0x0045d100

    // --- selection -------------------------------------------------------------------------
    // The soldier the player controls; while spectating (multiplayer) the watched human.
    C_Human *GetActivePlayer() const;                   // 0x0045d1d0 (+0x2C)
    // The member chosen with SelectMember (multiplayer: null while it spectates).
    C_Human *GetSelectedMember() const;                 // 0x0045d160 (+0x28)
    // The member the squad orders go to.
    C_Human *GetCommandedMember() const;                // 0x0045d1b0 (+0x30)
    int GetActiveIndex() const { return m_active; }
    int GetCommandedIndex() const { return m_commanded; }
    // Makes `human` the active and the selected member.
    void SelectMember(C_Human *human);                  // 0x0045da60
    // Makes member `index` (alive, HUMAN_ROLE_PLAYER) the active one ("You are switched to").
    bool SwitchTo(int index);                           // 0x0045dc20
    // Index of the next / previous living HUMAN_ROLE_PLAYER member after the active one;
    // 0 when the search comes back to the active one (the interface's switch keys).
    int GetNextControllable() const;                    // 0x0045d660
    int GetPreviousControllable() const;                // 0x0045d9d0
    // Makes member `index` (alive, a human) the commanded one; -1 clears it. Commanding the
    // selected member takes the player's control from it.
    bool SetCommandedIndex(int index);                  // 0x0045dcc0
    bool SetCommanded(C_Human *human);                  // 0x0045dd40 (null / not a member: clears)
    // Gives the player's controller back to the active member (after a vehicle seat).
    void ReturnControl();                               // 0x0045da40
    // Shows or hides the active soldier's model for the camera (hidden in first person view
    // and in a seat that hides its occupant).
    void UpdateCameraTarget();                          // 0x0045dac0
    // The human the camera follows while the player spectates (null: the own soldier).
    void SetWatchedHuman(C_Human *human);               // 0x0045e8f0 (+0x58)
    C_Human *GetWatchedHuman() const { return m_watchedHuman; }

    // --- members ---------------------------------------------------------------------------
    // Drops the enemies `human` saw; single player: the mission fails when no member of the
    // player's role is left alive.
    void OnMemberDied(C_Human *human);                  // 0x0045e790
    // A living member carries an item of type `itemId`.
    bool HasItem(int itemId) const;                     // 0x0045e9a0
    // A living member other than `human` carries a medkit.
    bool HasMedkitBesides(const C_Human *human) const;  // 0x0045ea00
    // Warns the living squad members other than `thrower` of a grenade thrown from `pos`
    // along `dir` (their controllers, 0x00530ff0).
    void NotifyGrenadeThrown(const S_vector &pos, const S_vector &dir, C_Actor *thrower);   // 0x0045eab0
    // Single player below the hardest difficulty: the living members get full health.
    void HealMembers();                                 // 0x0045f020
    // Squad members holding (commands 0xE / 0xF) within 30 m of `human` switch to 0x10 / 0x11.
    void ReleaseHoldingMembers(const C_Human *human);   // 0x0045f110
    // A member's actor id is below C_ActorsModule::PLAYER_SLOTS (the game menu keeps it).
    bool HasMemberInPlayerSlots() const;                // 0x0045d120
    // A random collision volume of member `index`'s model.
    I3D_frame *GetRandomVolume(int index) const;        // 0x0045d6d0

    // --- enemies ---------------------------------------------------------------------------
    C_EnemyRecords &GetEnemyRecords() { return m_enemyRecords; }   // this + 0x38
    // `enemy` has a record (living or dead).
    bool IsEnemyKnown(const C_Human *enemy) const;      // 0x0045e920
    // The recorded enemies some member sees now (`enemies` is cleared first).
    void GetSeenEnemies(std::vector<C_Human *> &enemies) const;          // 0x0045eb30
    // The same, living and conscious only.
    void GetSeenLivingEnemies(std::vector<C_Human *> &enemies) const;    // 0x0045ed10
    // Makes the sight module test every living member against the non-friendly humans now
    // (the interface opening the tactical view).
    void RefreshSight();                                // 0x0045f080
    // A random enemy seen by the first other living member within NEARBY_DISTANCE of `human`
    // that sees any.
    C_Human *GetEnemySeenNearby(const C_Human *human) const;    // 0x0045f280
    // A random enemy that a living member within NEARBY_DISTANCE sees and `human` has not
    // seen; `skipCarCrew`: not one in a car with +0x5E0 set.
    C_Human *GetEnemyUnseenBy(const C_Human *human, bool skipCarCrew) const;   // 0x0045f3a0
    // MeleeTrackers a member has seen (the squad AI reads them).
    void AddSeenMeleeTracker(C_MeleeTracker *meleeTracker);                        // 0x0045d4a0
    const std::vector<C_MeleeTracker *> &GetSeenMeleeTrackers() const { return m_seenMeleeTrackers; }   // +0xBC
    // (The squad drops the dead ones from it.)
    std::vector<C_MeleeTracker *> &GetSeenMeleeTrackers() { return m_seenMeleeTrackers; }
    // Collects member `index`'s collision volumes (frames of type 11) into its list (also after
    // a new uniform, C_GEffectsModule::DressUniform).
    void CollectVolumes(int index);                     // 0x0045d710

private:
    static uint32_t __stdcall AddVolume(I3D_frame *frame, void *list);   // 0x0045d7c0
    // Single player, while the mission is not decided: the members' environment flags
    // (HUMAN_ENV_DEADLY fails the mission, HUMAN_ENV_RESTRICTED shows a warning).
    void CheckEnvironment();                            // 0x0045ef00

    int m_selected = -1;                                // +0x28
    int m_active = -1;                                  // +0x2C
    int m_commanded = -1;                               // +0x30
    uint32_t m_environment = 0;                         // +0x34 the members' HUMAN_ENV_* last tick
    C_EnemyRecords m_enemyRecords;                      // +0x38
    C_Human *m_watchedHuman = nullptr;                  // +0x58
    // Per member: its model's collision volumes (heap lists of 0x10 bytes).
    std::vector<std::vector<I3D_frame *> *> m_volumes;  // +0x5C
    std::vector<void *> m_unused6C[5];                  // +0x6C constructed and freed only
    std::vector<C_MeleeTracker *> m_seenMeleeTrackers;                    // +0xBC
};

// The players of a co-operative game, on the server (vtable 0x0080fc4c, 10 slots; 0x4C bytes).
class C_CoopTeam : public C_TeamBase {
public:
    C_CoopTeam();                                       // 0x0045fb90
    ~C_CoopTeam() override;                             // 0x0045fbf0 (0x0045fc10)
    // Also the records (the server's StopMission inlines it).
    void Clear() override;                              // 0x0045fc80
    void Update(int /*timeDelta*/) override {}          // 0x0045fd70
    void Tick(int timeDelta) override;                  // 0x0045fd50
    // Drops the records (the members stay).
    void RestartMission(bool server) override;          // 0x0045fd80
    using C_TeamBase::RemoveMember;
    virtual bool RemoveMember(C_Human *human);          // 0x24 0x0045fdf0
    // Drops the enemies `human` saw.
    void OnMemberDied(C_Human *human);                  // 0x0045fe10
    C_EnemyRecords &GetEnemyRecords() { return m_enemyRecords; }   // this + 0x28

private:
    C_EnemyRecords m_enemyRecords;                      // +0x28
    int m_unknown48 = 0;                                // +0x48 cleared with the records, not read
};

// Humans the mission script keeps in a formation behind a leader (vtable 0x0080fc74,
// 9 slots; 0x2C bytes). Only humans driven by their script (HUMAN_CONTROLLER_AI) join.
class C_Formation : public C_TeamBase {
public:
    // A sharp arrow led by `leader`; `id`: its index in the team manager.
    C_Formation(C_Human *leader, int id);               // 0x004608a0
    ~C_Formation() override;                            // 0x00460960 (0x00460980)
    bool AddMember(C_Human *human) override;            // 0x004603c0
    bool RemoveMember(C_Human *human, int *index) override;   // 0x00460850
    void Tick(int timeDelta) override;                  // 0x00460a10
    void RestartMission(bool /*server*/) override {}    // 0x00460940
    // Chunk 9999 { 1000 { the shape } } (the members are not saved).
    bool SaveGame(C_chunk &chunk) override;             // 0x00460410
    bool LoadGame(C_chunk &chunk) override;             // 0x004605c0

    C_FormationShape *GetShape() const { return m_shape; }   // +0x28
    C_Human *GetLeader() const;

private:
    C_FormationShape *m_shape;                          // +0x28
};

class C_TeamManager : public C_Module {                 // "TeamMgr", vtable 0x0080fc98 (0x138 bytes)
public:
    C_TeamManager(int id, const char *name);            // 0x00461430
    ~C_TeamManager() override;                          // 0x004614c0 (0x004614e0)

    // Clients: the 0x0089ad40 module and the formations.
    void Tick(int timeDelta) override;                  // 0x00460ae0
    void StartMission(bool server) override;            // 0x00460aa0
    void StopMission(bool server) override;             // 0x00460b30
    // Drops the formations; restarts the teams.
    void RestartMission(bool server) override;          // 0x00460c20
    // Chunk 0x2694 { 1000 { the player's team }, 2000 { leader id, formation } ... }.
    bool SaveGame(C_chunk &chunk) override;             // 0x00460cd0
    bool LoadGame(C_chunk &chunk) override;             // 0x00460fb0

    C_Team *GetPlayerTeam() { return &m_playerTeam; }   // 0x00461410 (this + 0x1C)
    C_CoopTeam *GetCoopTeam() { return &m_coopTeam; }   // 0x00461420 (this + 0xE8)
    // The formation `leader` leads; `create`: a new one when there is none.
    C_Formation *GetFormation(C_Human *leader, bool create);   // 0x00461290
    void DestroyFormation(C_Formation *formation);      // 0x00461390
    // Takes `member` out of the formation it is in.
    bool RemoveFromFormation(C_Human *member);          // 0x00461240

private:
    std::vector<C_Formation *> m_formations;            // +0x0C (first +0x10)
    C_Team m_playerTeam;                                // +0x1C
    C_CoopTeam m_coopTeam;                              // +0xE8
    bool m_clientRunning = false;                       // +0x134 between the client's start and stop (not read)
    bool m_unknown135 = false;                          // +0x135 cleared by the server's stop (not read)
};
extern C_TeamManager g_teamManager;                     // 0x0089ae78
