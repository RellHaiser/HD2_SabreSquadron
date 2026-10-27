// Humans: the player's squad, enemies, civilians and the other players (actor type 3,
// "Human", 0x0059be30..0x005aa790; the body part 0x005ab330..0x005ac4c0 is in human_body.cpp).
//
// A human is a C_Actor bound to an animated model. What it does is decided by its
// controller (human_controller.h: player input, AI, mission script or network); its
// animation, the work it does with its hands and the models of what it carries belong to the
// three objects of its body part (S_HumanBody). The human itself keeps the soldier data
// (stats, single player progress), damage and death, the inventory containers, what it
// sees and hears, and the network state.
#pragma once

#include <cstdint>
#include <deque>
#include <vector>

#include "common/c_str.h"
#include "game/actor.h"
#include "game/human_controller.h"
#include "game/soldier.h"
#include "ls3d/i3d.h"

class C_BitStream;
struct S_ActionCommand;
class C_chunk;
class C_FormationShape;
class C_HumanEquipment;
class C_HumanMovement;
class C_HumanWork;
class C_Item;
class C_ItemContainer;
class C_SpeechSet;
class C_SpeechSets;
class C_WeaponItem;
struct S_HeardSound;

// Roles (C_Human::GetRole()), set in the mission file.
enum E_HumanRole {
    HUMAN_ROLE_PLAYER = 1,                // a soldier of the player's squad / a network player
    HUMAN_ROLE_ENEMY = 2,
    HUMAN_ROLE_NEUTRAL = 3,               // default; friendly to the allies
    HUMAN_ROLE_NPC_TEAM = 4,              // allied NPC (counts as a member of the player's side)
    HUMAN_ROLE_NPC_TEAM_5 = 5,            // NPC on the enemy's side
};

// Sides as C_Human::GetSide() sees them (what a human looks like; item definitions use the
// same values in C_ItemDef::GetTeam()).
enum E_HumanSide {
    SIDE_ALLIES = 0,
    SIDE_AXIS = 1,
    SIDE_NEUTRAL = 2,
};

// Teams as C_Human::GetTeam() returns them (multiplayer: the player's team).
enum E_HumanTeam {
    TEAM_AXIS = 0,
    TEAM_ALLIES = 1,
    TEAM_NONE = 2,
};

// C_Human::GetEnvironmentFlags() bits (the ground's material flags).
enum : uint32_t {
    HUMAN_ENV_DEADLY = 0x2000,                          // multiplayer: the player dies there
    HUMAN_ENV_RESTRICTED = 0x4000,                      // multiplayer: a warning is shown
};

// Sound a human heard, kept 3 s (the pool at 0x008ab158 hands them out).
struct S_RememberedSound {                              // 0x20 bytes
    S_vector pos;
    float posW;
    int chapter;                                        // +0x10 E_SoundChapter
    C_Actor *source;                                    // +0x14
    uint32_t time;                                      // +0x18 timeGetTime()
    bool refreshed;                                     // +0x1C heard again while remembered
};
// Allocates / returns entries of the remembered-sound pool (0x008ab158; its free list of
// list nodes is at 0x008ab1a0). The entry gets `chapter`, `pos`, `source` and `time` and is
// not refreshed (a __thiscall method of the pool in the original).
S_RememberedSound *AllocRememberedSound(int chapter, const S_vector4 &pos, C_Actor *source, uint32_t time);   // 0x00582740
void FreeRememberedSound(S_RememberedSound *sound);     // inline (free list 0x008ab1a0, 0x005aac60)

// What the AI of a human remembers (0x110 bytes; the AI code uses it, the human keeps it).
struct C_HumanAIMemory {
    explicit C_HumanAIMemory(C_Human *owner) { Reset(owner); }   // 0x0059e340
    // (Inline in the original wherever the human restarts.)
    void Reset(C_Human *owner)
    {
        this->owner = owner;
        unknownE0 = 0;
        unknownF0 = 0;
        unknownEC = 0;
        unknownE4 = 0;
        unknown104 = true;
        unknown100 = -1;
        unknown20[0] = unknown20[1] = unknown20[2] = 0;
        unknown40[0] = unknown40[1] = unknown40[2] = 0;
        unknown00[0] = unknown00[1] = unknown00[2] = 0.0f;
        unknown10 = false;
        unknown109 = false;
        unknown10A = false;
        unknown106 = true;
        unknown105 = true;
        unknown80 = INVALID;
        unknown70 = INVALID;
    }
    bool SaveGame(C_chunk &chunk);                      // 0x005641b0
    bool LoadGame(C_chunk &chunk);                      // 0x005643e0

    static constexpr uint32_t INVALID = 0x7FC00000;     // a quiet NaN marks "not set"

    // +0x00 a step aside from the others in the way (the AI walks along it while it is
    // longer than 0.1 m; inferred).
    float unknown00[3];
    uint8_t unknown0C[4];
    bool unknown10;                                     // +0x10
    uint8_t unknown11[0x0F];
    uint32_t unknown20[3];                              // +0x20
    uint8_t unknown2C[0x14];
    uint32_t unknown40[3];                              // +0x40
    uint8_t unknown4C[0x24];
    uint32_t unknown70;                                 // +0x70 float bits
    uint8_t unknown74[0x0C];
    uint32_t unknown80;                                 // +0x80 float bits
    uint8_t unknown84[0x5C];
    int unknownE0;                                      // +0xE0
    uint16_t unknownE4;                                 // +0xE4
    uint8_t unknownE6[2];
    C_Human *owner;                                     // +0xE8
    int unknownEC;                                      // +0xEC
    int unknownF0;                                      // +0xF0
    uint8_t unknownF4[0x0C];
    int unknown100;                                     // +0x100
    bool unknown104;                                    // +0x104
    bool unknown105;                                    // +0x105
    bool unknown106;                                    // +0x106
    uint8_t unknown107[2];
    bool unknown109;                                    // +0x109
    bool unknown10A;                                    // +0x10A
    uint8_t unknown10B[5];
};

// What the AI of a human perceived (a shot heard, an enemy seen, being hit ...): 0x40 bytes,
// from g_humanEventPool. Raised to C_HumanEvents' queue for the controller to handle, or
// tracked by the AI (C_HumanEvents::AddStimulus) until it times out.
struct alignas(16) S_HumanEvent {
    uint32_t type = 0;                                  // kind (HUMAN_EVENT_*); top nibble: priority 1..4
    int timeLeft = 0;                                   // +0x04 ms (raised events: 10 s)
    int age = 0;                                        // +0x08 ms
    int actorId = -1;                                   // +0x0C actor id (ActorToId), -1 none
    int sourceId = -1;                                  // +0x10 the actor that caused it, -1 none
    uint8_t unknown14[0x0C] = {};
    S_vector4 pos = {};                                 // +0x20 (saved without w)
    int unknown30 = 0;                                  // +0x30
    uint8_t unknown34[0x0C] = {};

    // Fills in a new event (10 s to live; the actors by their ids).
    S_HumanEvent *Init(const C_Actor *actor, uint32_t eventType, const C_Actor *source, const S_vector4 &position,
                       int value30);                    // 0x005750f0
    bool SaveGame(C_chunk &chunk) const;                // 0x005aca70
    bool LoadGame(C_chunk &chunk);                      // 0x005accd0
};
static_assert(sizeof(S_HumanEvent) == 0x40, "S_HumanEvent layout");

// S_HumanEvent::type values identified so far.
enum : uint32_t {
    HUMAN_EVENT_PRIORITY_MASK = 0xF0000000,
    HUMAN_EVENT_PRIORITY_SHIFT = 28,
    HUMAN_EVENT_HIT = 0x10000016,         // C_AIController::OnHit: sourceId is the attacker
    HUMAN_EVENT_MELEE_TRACKER_ALERT = 0x10000017,   // to a meleeTracker's owner: it found a target (sourceId)
};

// The event pool (0x008ac430, blocks of 50). The AI code takes events from it inline.
S_HumanEvent *AllocHumanEvent();
void ReleaseHumanEvent(S_HumanEvent *event);            // 0x005add20 __cdecl
// Actor <-> actor id (C_Actor::GetNetId) as events keep them; -1 / null for none.
int ActorToId(const C_Actor *actor);                    // 0x005aca30 __cdecl
C_Actor *IdToActor(int id);                             // 0x005aca50 __cdecl

// What a human's AI is told about (0xA0 bytes; 0x005ac950..0x005af110, human_events.cpp):
// the queue of raised events, the stimuli it tracks, the actors in its proximity range
// (periodic links, C_Human::OnProximity) and the sniper post of HUMAN_SetSniper.
class C_HumanEvents {
public:
    void Reset();                                       // 0x005ae450
    // What the enemy AI's morale gains every 5 s (less the enemies in view).
    int GetMoraleGain() const { return 2; }             // 0x005ae940
    bool SaveGame(C_chunk &chunk);                      // 0x005ad040
    bool LoadGame(C_chunk &chunk);                      // 0x005ad390
    // Ages the events; drops those whose time ran out.
    void Tick(int timeDelta);                           // 0x005ad700

    // An actor entered / left the proximity range; false when it already was / was not in
    // it (C_Human::OnProximity then ignores the change). `distance` is passed, not used.
    bool AddNearbyActor(const C_Actor *actor, float distance);      // 0x005ae640
    bool RemoveNearbyActor(const C_Actor *actor, float distance);   // 0x005ae810
    // Nearby actor `index` (null if gone).
    C_Actor *GetNearbyActor(int index) const;           // 0x005ae230
    int GetNearbyActorCount() const { return static_cast<int>(m_nearbyActors.size()); }
    // No nearby actor is within 2 m of `pos`.
    bool IsAwayFromNearbyActors(const S_vector &pos) const;   // 0x005ae890
    // (Not called.)
    void AddUnknownActor(const C_Actor *actor);         // 0x005ae950

    // The queue the controller handles (C_Human::ProcessEvents).
    void Raise(S_HumanEvent *event);                    // 0x005adac0
    bool HasPending() const { return !m_pending.empty(); }
    S_HumanEvent *PopFront();                           // 0x005adc50

    // Tracks `event` for `time` ms; it replaces a tracked event about the same actor (keeping
    // the longer time and, with `keepPriority`, the higher priority).
    void AddStimulus(int time, S_HumanEvent *event, bool keepPriority);   // 0x005adee0
    // The tracked stimulus that matters most at `pos` (time left / distance * priority).
    S_HumanEvent *GetStrongestStimulus(const S_vector &pos) const;   // 0x005ae310
    // Whoever hit the human last (while that is tracked).
    C_Actor *GetAttacker() const;                       // 0x005ae270

    // Script HUMAN_SetSniper: the human stays within `radius` of `pos`.
    void SetSniper(bool on, float radius, const S_vector &pos);   // 0x005ae610
    // The tracked stimuli (the squad AI drops some itself).
    std::deque<S_HumanEvent *> &GetStimuli() { return m_stimuli; }
    // The sniper post (HUMAN_SetSniper).
    const S_vector4 &GetSniperPos() const { return m_sniperPos; }
    float GetSniperRadius() const { return m_sniperRadius; }
    bool IsSniper() const { return m_sniper; }

private:
    friend class C_ScriptedController;                  // the meleeTracker alerts in m_pending

    S_vector4 m_sniperPos = {};                         // +0x00 (saved without w)
    float m_sniperRadius = 0.0f;                        // +0x10 m
    bool m_sniper = false;                              // +0x14
    std::deque<S_HumanEvent *> m_pending;               // +0x18 raised, not handled yet
    std::deque<S_HumanEvent *> m_stimuli;               // +0x48 tracked by the AI
    std::vector<int> m_nearbyActors;                    // +0x78 actor ids
    std::vector<int> m_unknownActors;                   // +0x88 actor ids (only saved and loaded)
};

// Speech (0x005af7a0..0x005b0e10, human_speech.cpp). A speech set gives every sentence (alarm,
// orders, pain ...) a few samples; the samples are sound codes, decimal 9S VV NNNN with a set
// digit S, a variant VV and the sample NNNN.

// A sentence of a speech set: its samples in a shuffled order, so that the same sample does
// not come twice in a row (0x1C bytes).
struct S_SpeechSentence {
    std::vector<int> samples;                           // +0x04 sound codes
    size_t cursor = 0;                                  // +0x10 an iterator in the original
    uint32_t nextTime = 0;                              // +0x14 mission time before which it is not said
    bool enabled = false;                               // +0x18 the set has samples for it

    // Not said again within `delay` ms.
    void SetDelay(int delay);                           // 0x005af7d0
    bool IsWaiting() const;                             // 0x005af7f0
    bool CanSay() const;                                // 0x005af810
    bool HasSamples() const;                            // 0x005af850
    // Puts `sample` at a random place behind the cursor.
    void AddSample(int sample);                         // 0x005b0570
    // Takes the first sample and shuffles it back in (inline in 0x005af8e0 / 0x005afa60).
    int Take();
};

// One set of Tables\speech.set (0xF24 bytes), shared by the humans that use it.
class C_SpeechSet {
public:
    static constexpr int SENTENCE_COUNT = 0x8A;

    C_SpeechSet() = default;                            // 0x005b0340 (body: C_SpeechSet::Init)
    ~C_SpeechSet();                                     // 0x005afdc0 (body: C_SpeechSet::Cleanup)
    C_SpeechSet(const C_SpeechSet &) = delete;
    C_SpeechSet &operator=(const C_SpeechSet &) = delete;

    int GetId() const { return m_id; }
    // A sample of `sentence` unless it was said within its delay (-1 then, also for a
    // sentence without samples); `delay` > 0 starts a new delay.
    int Say(int sentence, int delay);                   // 0x005af8e0
    // A sample of `sentence` regardless of its delay; -1 when it has none.
    int SayNow(int sentence);                           // 0x005afa60
    void SetDelay(int sentence, int delay);             // 0x005afd10

private:
    friend class C_SpeechSets;

    void *m_unknown00 = nullptr;                        // +0x00 freed by the destructor, never set
    int m_id = 0;                                       // +0x04 index in Tables\speech.set
    S_SpeechSentence m_sentences[SENTENCE_COUNT];       // +0x08
    int m_refCount = 0;                                 // +0xF20
};

// The speech sets loaded so far, a singleton (0x424 bytes, 0x008ac448).
class C_SpeechSets {
public:
    C_SpeechSets() = default;                           // inline in C_Human::GetSpeechSets
    ~C_SpeechSets() { Clear(); }
    C_SpeechSets(const C_SpeechSets &) = delete;
    C_SpeechSets &operator=(const C_SpeechSets &) = delete;

    // Set `id` (0: the players' one), loaded on first use; null if missing.
    C_SpeechSet *Acquire(int id);                       // 0x005afe00
    void Release(C_SpeechSet *set);                     // 0x005b0390
    // Deletes every set.
    void Clear();                                       // 0x005afd60
    // Every sentence may be said again (inline in C_game::RestartMission).
    void ResetDelays();

private:
    std::vector<C_SpeechSet *> m_sets;                  // +0x04
    // (+0x10: the C_chunk Acquire reads Tables\speech.set with.)
};

// Sound codes of speech samples (9S VV NNNN).
uint32_t SpeechCode_Base(uint32_t code);                // 0x005b0440 __fastcall (90 00 NNNN)
int SpeechCode_GetSet(uint32_t code);                   // 0x005b0460 __fastcall
int SpeechCode_GetVariant(uint32_t code);               // 0x005b0490 __fastcall
void SpeechCode_SetSet(uint32_t *code, int set);        // 0x005b04d0 __fastcall
void SpeechCode_SetVariant(uint32_t *code, int variant);   // 0x005b0510 __fastcall

// Animation, work and equipment objects of a human (see human_body.h).
struct S_HumanBody {                                    // 0x18 bytes
    C_Human *owner = nullptr;
    int group = 0;                                      // +0x04 C_Human::GetGroup()
    C_HumanMovement *movement = nullptr;                // +0x08 0xDD0 bytes
    C_HumanWork *work = nullptr;                        // +0x0C 0xE0 bytes
    C_HumanEquipment *equipment = nullptr;              // +0x10 0x230 bytes
    bool crippled = false;                              // +0x14 three hits in the legs

    // Creates the three objects; false never happens.
    bool Create(C_Human *owner, int group);             // 0x005ab330
    // Frees them.
    bool Destroy();                                     // 0x005aba90
    void Reset();                                       // 0x005ab4c0
    bool SaveGame(C_chunk &chunk);                      // 0x005ab4e0
    bool LoadGame(C_chunk &chunk);                      // 0x005ab7a0
    // (Always succeed.)
    static bool StartMission(bool server) { (void)server; return true; }   // 0x005aba70 / 0x005aba80
    static void StopMission(bool server) { (void)server; }                 // 0x005abb10 / 0x005abb20
    void Update(int frameTime);                         // 0x005abc20
    void Tick(int timeDelta);                           // 0x005abc70

    // Where the human faces / looks / aims (from its controller, every frame).
    void SetFacingDir(const S_vector &dir);             // 0x005ac0a0
    void SetLookAt(const S_vector4 &target);            // 0x005ac080
    void SetViewDir(const S_vector &dir);               // 0x005ac0c0
    // Body flags of the work object for the controller; clears the one-frame ones.
    uint32_t TakeFlags();                               // 0x005ac0d0

    // Plays the reaction to a hit (`flags`: zone / kind bits) pushing the body with `force`.
    void ApplyHit(uint32_t flags, const S_vector &pos, const S_vector &force);   // 0x005abbd0
    // Death animation (`killer` may be null).
    void Die(C_Actor *killer);                          // 0x005abbe0
    void PlayDeath();                                   // 0x005abbf0
    // Three leg hits: limps from now on.
    void Cripple();                                     // 0x005ac370
    void Ragdoll();                                     // 0x005ac420
    void ResetMovement();                               // 0x005ac440

    // Body commands (S_BodyCommand, vtable 0x00810b48).
    void Execute(const struct S_BodyCommand &command);  // 0x005abd90
    void PlayWorkAnimation(int animation, I3D_frame *frame);   // 0x005abb50
    void DoAction(int action, I3D_frame *frame, void *arg);    // 0x005abe30
    void SetPose(int pose, const void *arg);            // 0x005abf60
    // An interaction with `target` (E_Interaction and flags; `arg`: the seat, the door's open).
    void Interact(C_Actor *target, uint32_t interaction, int arg);   // 0x005abcc0
    void PlayAnimation(const char *name, int a, int b, int c);       // 0x005ac4a0

    // (Not called: 0x005abb30, 0x005abb80, 0x005ac230, 0x005ac330.)
    void Play(int animation);                           // 0x005abb30
    void SetStance(int stance);                         // 0x005abb80
    float GetSpeed() const;                             // 0x005ac230 (movement +0xB70)
    bool CanRespawn() const;                            // 0x005ac330
    void SetPlayerControlled(bool on);                  // 0x005ac070
    void SetModelVisible(bool on);                      // 0x005ac0f0
    void SetCarryMode(int mode);                        // 0x005ac110
    void Freeze(bool on);                               // 0x005ac170
    void RestoreCollision(bool unknown);                // 0x005ac1b0
    void RemoveCollision(bool unknown);                 // 0x005ac1c0
    void SetMovementFlags(uint32_t flags);              // 0x005ac390
    void ClearMovementFlags(uint32_t flags);            // 0x005ac3e0
    uint32_t GetMovementStance() const;                 // 0x005ac3f0
    int GetStance() const;                              // 0x005ac210 (work +0x4C)
    int GetWorkState() const;                           // 0x005ac220 (work +0x50)
    int GetClimbState() const;                          // 0x005ac2b0
    int GetClimbTarget() const;                         // 0x005ac2e0
    bool IsWorkState5() const;                          // 0x005ac310
    float GetVisibilityFactor() const;                  // 0x005ac350
    float GetLightLevel() const;                        // 0x005ac360
    bool GetEquipmentFlag1() const;                     // 0x005ac400
    bool IsLeftFootForward() const;                     // 0x005ac410
    // Ground material record under the human (null when unknown).
    const uint32_t *GetGround() const;                  // 0x005ac430
    // (`arg` goes down to C_HumanWeaponPose::Unknown7a8760, which does not use it.)
    void Unknown5ac470(int arg);                        // 0x005ac470
    I3D_frame *GetUnknown6E0(int *out) const;           // 0x005ac1d0
    I3D_sector *GetSector() const;                      // 0x005ac4c0
    // Lengths (ms, -1: none) of table animations (forwarders called by the AI controllers;
    // __thiscall in the original, passing the movement to the __stdcall table functions).
    static int GetActivityAnimLength(int id, int variant);          // 0x005ac1f0
    static int GetCarryAnimLength(int id, int variant, int carryMode);   // 0x005ac200
};

// S_BodyCommand::kind.
enum E_BodyCommand {
    BODY_COMMAND_PLAY = 0,                // work animation arg0
    BODY_COMMAND_WORK_ANIMATION = 1,      // arg0 at the frame arg1 (if any)
    BODY_COMMAND_STANCE = 2,              // arg0 (crippled humans limp instead)
    BODY_COMMAND_3 = 3,
    BODY_COMMAND_ACTION = 4,              // S_HumanBody::DoAction(arg0, frame arg1, arg2)
    BODY_COMMAND_POSE = 5,                // S_HumanBody::SetPose(arg0, data)
    BODY_COMMAND_INTERACT = 6,            // S_HumanBody::Interact(target arg2, interaction arg0, arg1)
};

// Command for S_HumanBody::Execute() (vtable 0x00810b48; C_Human::ExecuteBodyCommand()).
struct S_BodyCommand {
    virtual ~S_BodyCommand() = default;
    int unknown04 = 0;
    int kind = 0;                                       // +0x08
    int arg0 = 0;                                       // +0x0C
    int arg1 = 0;                                       // +0x10
    void *arg2 = nullptr;                               // +0x14
    uint8_t unknown18[8] = {};
    uint8_t data[0x10] = {};                            // +0x20
};

class alignas(16) C_Human : public C_Actor {            // "Human", vtable 0x008111e0, 0x2D0 bytes
public:
    C_Human();                                          // 0x0059bf00
    ~C_Human() override;                                // 0x0059e3e0 (0x0059c4b0)

    // --- C_Actor ---------------------------------------------------------------------
    // Players, allied and enemy-side NPCs: the soldier's full name; others the text of
    // their name id (or "enemy" / "civilian").
    C_str *GetName() override;                          // 0x005a9980
    // Creates the item containers.
    void Init() override;                               // 0x005a2740
    void Done() override;                               // 0x005a2a40
    // What the human can carry: its strength.
    int GetHitPoints() override { return GetStat(STAT_STRENGTH); }   // 0x0059c2b0
    void Update(int frameTime) override;                // 0x0059fe30
    void Tick(int timeDelta) override;                  // 0x0059ffd0
    void OnMessage(C_BitStream *msg) override;          // 0x005a5f60
    void Unknown20(C_BitStream *msg) override;          // 0x005a5f70
    bool SetFrame(I3D_frame *frame) override;           // 0x0059e5e0
    bool WriteProperties(C_chunk &chunk) override;      // 0x0059e6c0
    bool ReadProperties(C_chunk &chunk) override;       // 0x0059e800
    void CopyFrom(const C_Actor *source) override;      // 0x0059e650
    void StartMission(bool server) override;            // 0x0059f2c0
    void StopMission(bool server) override;             // 0x0059fa60
    void RestartMission(bool server) override;          // 0x0059d100
    bool SaveGame(C_chunk &chunk) override;             // 0x0059d5d0
    bool LoadGame(C_chunk &chunk) override;             // 0x0059ddf0
    void WriteState(C_BitStream *msg) override;         // 0x005a9c40 (0x005aa770)
    void ReadState(C_BitStream *msg) override;          // 0x005aa1b0 (0x005aa780)
    // A car faster than 2 m/s runs the human over (killed by its driver).
    bool OnCollisionEx(C_Actor *other, const S_vector *pos, int a3, int a4, int a5, int a6, int a7) override;   // 0x005a07c0
    void Hit(E_HitType type, float amount, const S_vector &pos, const S_vector &dir, C_Actor *attacker,
             int zone) override;                        // 0x005a1c90
    int GetMaterialId(I3D_frame *) override { return 66; }   // 0x0059c2a0
    void GetBoxCorners(S_vector4 corners[8]) override;  // 0x005a85f0

    // --- identity ----------------------------------------------------------------------
    E_HumanRole GetRole() const { return m_role; }
    int GetGroup() const { return m_group; }
    int GetProfileIndex() const { return m_profileIndex; }
    S_SoldierProfile *GetProfile() const { return m_profile; }
    const S_vector4 &GetScale() const { return m_scale; }
    // (Set in multiplayer: no uniform swaps with or for it; the signals offer none.)
    bool GetUnknown1A5() const { return m_unknown1a5; }
    bool IsCarnageEnemy() const { return m_carnage; }
    // Played by the local player: controller type 1 in single player, the local player
    // slot in a network game.
    bool IsLocalPlayer() const;                         // 0x005a0890
    // Simulated by this machine as a server (not a player).
    bool IsServerSimulated() const;                     // 0x005aa790
    // E_HumanTeam (multiplayer: the network player's team; `pending` asks for the team
    // the player switches to).
    int GetTeam(bool pending) const;                    // 0x005a8820
    void SetPlayerTeam(int team, bool pending);         // 0x005a8800
    // E_HumanSide by appearance (single player: uniforms disguise the player's soldiers).
    int GetSide() const;                                // 0x005a84f0
    // `other` (a human, or in single player also a meleeTracker) is on the human's side.
    bool IsFriendOf(const C_Actor *other) const;        // 0x005a0ab0
    // `other` is not on the human's side.
    bool IsHostileTo(const C_Actor *other) const { return !IsFriendOf(other); }   // 0x005a0980
    // Player vs. enemy pair that is nevertheless friendly (a disguised soldier).
    bool IsDisguisedEnemy(const C_Human *other) const;  // 0x005a0a50
    // Names for messages (multiplayer players: the network player's name).
    const char *GetPlayerName() const;                  // 0x005a81b0
    const char *GetLastName() const;                    // 0x005a81c0
    const char *GetFirstName() const;                   // 0x005a8220 (not called)
    const char *GetShortName() const;                   // 0x005a82c0
    const char *GetFullName() const;                    // 0x005a8320
    int GetProfileUnknown24() const;                    // 0x005a8380

    // --- soldier data --------------------------------------------------------------------
    int GetHealth() const { return m_stats[STAT_HEALTH]; }
    bool IsFadedOut() const { return m_fadedOut; }
    bool IsAlive() const { return m_stats[STAT_HEALTH] > 0; }   // 0x0041d540
    bool IsDead() const { return m_stats[STAT_HEALTH] < 1; }    // 0x0041d530
    // Stats 0..9; 10 returns the hearing range (STAT_HEARING_RANGE).
    int GetStat(int stat) const;                        // 0x005a6600
    // Stats 0..8 as factors (health / 800, the others / 100).
    float GetStatFactor(int stat) const;                // 0x005a6530
    void SetSightRange(int range) { m_stats[STAT_SIGHT_RANGE] = range; }
    void SetHearRange(int range) { m_stats[STAT_HEARING_RANGE] = range; }
    // Sets stats 1, 2, 4, 6, 7 and 8 (and their initial values) from `skills` (indexed
    // like the stats).
    void SetSkills(const int *skills);                  // 0x005a96a0
    // Changes the morale stat (at most its initial value).
    void ChangeMorale(int delta);                       // 0x005a64f0
    // Health in % of the initial health (a tiny rest counts as 1).
    int GetHealthPercent() const;                       // 0x005a5ff0
    void SetHealthPercent(int percent);                 // 0x005a6060
    int GetStaminaPercent() const;                      // 0x005a62a0
    // Binds the human to roster entry `index` and takes its stats.
    void SetProfile(int index);                         // 0x0059ea90
    // Sets the human up from a soldier state (role, stats or `stats`, equipment, speech).
    void Setup(S_SoldierState &state, int role, int group, const int *stats, int playerSlot);   // 0x0059edc0
    // Fills `state` with the human's (network state / end of a mission).
    void GetSoldierState(S_SoldierState &state) const;  // 0x005a9040
    // Single player experience.
    void AddUnknownProgress(float amount);              // 0x005a92f0 (not called)
    void AddEndurance(float amount);                    // 0x005a93a0
    void AddMorale(float amount);                       // 0x005a9410 (not called)
    void AddShooting(float amount);                     // 0x005a9480
    void AddStealth(float amount);                      // 0x005a94f0
    void AddFirstAid(float amount);                     // 0x005a9560
    void AddLockpick(float amount);                     // 0x005a95d0
    void AddExperience(int amount);                     // 0x005a9640
    // Face texture (and the profile's).
    void SetTexture(const char *name);                  // 0x005a87c0
    void ApplyProfileFace();                            // 0x005a8790

    // --- state -------------------------------------------------------------------------
    bool IsUnconscious() const;                         // 0x005a6380
    // Takes part in the game: always in single player; a player's human in a network game
    // once it has spawned (and until DetachFromWorld()); false without a controller.
    bool IsInPlay() const;                              // 0x005a63b0
    // The multiplayer zone whose capture the player's HUD shows.
    void SetMpZone(int zoneId) { m_mpZone = zoneId; }
    // Multiplayer: the player's human waits (dead or between rounds).
    bool IsSpectating() const;                          // 0x005a63f0
    bool IsInWater() const;                             // 0x005a6420
    // Controlled by the network (client copies). (Not called.)
    bool IsRemote() const;                              // 0x005a08d0
    // Body speed while moving, 0 otherwise. (Not called.)
    float GetMoveSpeed() const;                         // 0x005a5c50
    // Enemy that gave up.
    bool IsSurrendered() const;                         // 0x005a6450
    // Leaves / joins the world while the actor stays: freezes the body and turns the frame off.
    void Freeze(bool on);                               // 0x005a5d50
    // Fades the model out / in over `time` ms (effects module).
    void FadeOut(int time);                             // 0x005a86f0
    void FadeIn(int time);                              // 0x005a8740
    // Leaves the vehicle and drops sight and hearing registrations before the human is
    // removed.
    void DetachFromWorld();                             // 0x005a7fe0
    // Disguise of a single player soldier (0..100, grows while nothing of his side is worn).
    void UpdateDisguise(float amount);                  // 0x005a60d0
    void SetDisguise(float value);                      // 0x005a6250
    int GetDisguisePercent() const;                     // 0x005a6280
    // How well the human can be seen (stance, movement, light).
    float GetVisibility() const;                        // 0x005a59e0
    // Movement speed in m/s (ejected shells inherit it).
    const S_vector &GetVelocity() const { return reinterpret_cast<const S_vector &>(m_velocity); }
    float GetUnknown94() const { return m_unknown94; }  // 0x005a97a0
    void SetUnknown94(float value);                     // 0x005a9760
    // Progress bar of a timed action (lockpicking, a vehicle's weapon, a multiplayer zone).
    bool GetActionProgress(int *time, int *total, int *textId) const;   // 0x005a9830
    // Starts the action bar: `time` ms of the interaction with text `textId` (-1: none; the
    // controllers, inline in the original).
    void SetActionProgress(int time, int textId) { m_actionTime = time; m_actionTotal = time; m_actionText = textId; }
    // Multiplayer zone actor the human stands in, if it is active; -1 otherwise.
    int GetActiveMPZone() const;                        // 0x005a9920
    // The human's periodic link (PeriodicLinks_Add; -1 on clients).
    int GetPeriodicLink() const { return m_periodicLink; }

    // --- controller ----------------------------------------------------------------------
    C_HumanController *GetController() const { return m_controller; }
    int GetControllerType() const;                      // 0x005a5fe0
    // The controller when GetControllerType() is HUMAN_CONTROLLER_AI.
    C_ScriptedController *GetScriptedController() const { return static_cast<C_ScriptedController *>(m_controller); }
    // Replaces the controller (no-op if it already has that type).
    void CreateController(E_HumanController type);      // 0x005a3f20
    // Player controller (single player: `active` = the player controls this soldier).
    void AssignPlayerController(bool active);           // 0x005a3e60
    // Marks a player / team human as script-controlled.
    void SetScripted(bool on);                          // 0x005a3df0
    // Switches to the scripted controller and starts it.
    void StartScriptedController();                     // 0x005af680
    void IssueCommand(const S_HumanCommand &command);   // 0x005a45b0
    bool HasOrder() const;                              // 0x005a45c0
    const S_vector &GetFacingDir() const;               // 0x005a80d0
    const S_vector &GetViewDir() const;                 // 0x005a80e0
    void SetFacingDir(const S_vector4 &dir);            // 0x005a84d0
    void SetViewDir(const S_vector4 &dir);              // 0x005a84e0
    float GetAccuracy() const;                          // 0x005a8100
    uint32_t GetMoveMode() const;                       // 0x005a5c70 (flags & MOVE_MOVING_MASK)
    uint32_t GetStateFlags() const;                     // 0x005a5c80
    uint32_t GetAIModeFlags() const;                    // 0x005a5cb0
    uint32_t GetStanceFlags() const;                    // 0x005a5d10
    int GetMoveSpeedClass() const;                      // 0x005a5cc0
    int GetStance() const;                              // 0x005a5d20 (2 is lower than standing: crouching, inferred)
    // Same as the scripts' HUMAN_SetAIMode, through whatever controller the human has.
    void SetControllerAIMode(int mode);                 // 0x005a5ca0
    // The human made a noise (the local player's: the action music starts).
    void MakeNoise();                                   // 0x005a6480
    void KnockOut(C_Human *by);                         // 0x005a62e0
    // The client side of KnockOut: only the controller's SetUnconscious (`by` is unused).
    void SetUnconscious(C_Human *by);                   // 0x005a6360
    // Thin wrappers of controller slots.
    // The sight module's view tests (C_HumanController::TestSightOfMeleeTracker / TestSightOfHuman).
    // The local player answers the meleeTracker test with status 500 and leaves `visibility` as it was.
    void TestSightOfMeleeTracker(C_Actor *meleeTracker, int *status, float *visibility);                  // 0x005a59a0
    void TestSightOfHuman(C_Human *target, int *status, float *visibility);             // 0x005a59d0
    // The controller's C_HumanController::SetMoveFlags (the animation player tells it the
    // direction of the animation played).
    void SetMoveFlags(uint32_t flags);                  // 0x005a5c90
    // The locomotion's next position on the AI's path (C_HumanController::AdjustToPathHeight).
    bool AdjustToPathHeight(S_vector *pos) const;       // 0x005a66f0
    bool ControllerUnknown5C(float *intensity, float *reference) const;   // 0x005a6710
    // The controller issues the command to leave the vehicle.
    void RequestLeaveVehicle();                         // 0x005a97b0
    // Tells the other side to replay a climb (E_RemoteRequest; no controller: nothing).
    void SendRemoteRequest(int request);                // 0x005a9680
    bool ControllerUnknown104() const;                  // 0x005aa7b0
    // The controller's weapon stance (WEAPON_STANCE_*; the first-person view follows it).
    int GetWeaponStance() const;                        // 0x005a80f0
    // Enemy / scripted AI only. (Not called.)
    void GetAIPathPoints(uint16_t *current, uint16_t *next);   // 0x005a85d0
    void Unknown5a8470(int) {}                          // 0x005a8470 (empty, not called)
    // Player controller members.
    // The frame's controls (C_game::HandleInput; null: none).
    void HandlePlayerInput(float *held, float *pressed);   // 0x005a3de0
    // Multiplayer spawn at `pos` facing `dir` with the equipment of `state`.
    void Respawn(const S_vector4 &pos, const S_vector4 &dir, S_SoldierState *state, const int *stats);   // 0x0059f050
    // The speech sets of the game (a singleton).
    static C_SpeechSets *GetSpeechSets();               // 0x0059ef80
    // ms since the human died (multiplayer).
    int GetDeadTime() const { return m_deadTime; }
    // Co-op: the inventory screen of human `humanId` (its equipment asked of the server).
    void PlayerRequestEquipment(int humanId);            // 0x005a45a0
    void PlayerSendActionCommand(const S_ActionCommand &command);   // 0x005a45e0
    void PlayerSetViewMode(int arg);                  // 0x005a8190
    // Network games: the fall damage for the server (the player's controller).
    void PlayerRequestDamage(int type, int damage, int attackerId, int zone);   // 0x005a9bf0
    bool IsPlayerHoldingBreath() const;                   // 0x005a6430
    int GetPlayerViewMode() const;                    // 0x005a8170
    // Squad orders (single player squad members).
    void AddOrder(S_HumanOrder *order);                 // 0x005a5f30
    void InsertOrder(S_HumanOrder *order, int index);   // 0x005a5f20
    void SetOrder(int index, S_HumanOrder *order);      // 0x005a5f40
    void CancelOrders();                                // 0x005a5e90
    void RemoveOrder(int index);                        // 0x005a5eb0
    bool HaveOrdersChanged() const;                     // 0x005a5ed0
    int GetOrderCount() const;                          // 0x005a5f00
    S_HumanOrder *GetOrder(int index) const;            // 0x005a5f50
    // The squad member panel's icons (-1: not a squad member).
    int GetSquadStatusIcon() const;                     // 0x005a88b0
    int GetSquadModeIcon() const;                       // 0x005a88d0
    // Network messages for player slots (`human` may be null: the message is read and
    // dropped).
    static void HandleMessage(C_Human *human, C_BitStream *msg);    // 0x005a5fb0 __fastcall
    static void HandleMessage2(C_Human *human, C_BitStream *msg);   // 0x005a5f80 __fastcall

    // --- body ------------------------------------------------------------------------------
    C_HumanMovement *GetMovement() const { return m_body.movement; }
    C_HumanWork *GetWork() const { return m_body.work; }
    // Models of the carried items (holsters, belt, hands).
    C_HumanEquipment *GetEquipment() const { return m_body.equipment; }
    // The body's light level and whether the hands item is put away for an action (the
    // first-person view's calls of S_HumanBody).
    float GetBodyLightLevel() const { return m_body.GetLightLevel(); }
    bool IsHandsItemPutAway() const { return m_body.GetEquipmentFlag1(); }
    // The body's climbing state (0: not climbing) and work state 5 (the interface's menus).
    int GetBodyClimbState() const { return m_body.GetClimbState(); }
    bool IsBodyWorkState5() const { return m_body.IsWorkState5(); }
    // (The command map calls it on the squad and the enemies seen while the game is paused.)
    void BodyUnknown5ac470(int arg) { m_body.Unknown5ac470(arg); }
    // Frame of the body model (positions for area checks).
    I3D_frame *GetBodyFrame() const { return m_bodyFrame; }
    void ExecuteBodyCommand(const S_BodyCommand &command);  // 0x005a45f0
    void SetCarryMode(int mode);                        // 0x005a5e40
    void PlayAnimation(const char *name, int a, int b, int c);   // 0x005a9c20
    // The left foot is the nearer one to where the body is in 2 s (the compass' wobble).
    bool IsLeftFootForward() const;                     // 0x005a8160
    // Takes the body's collision volumes out of the collision world while the human's own
    // shots are traced, and puts them back (the shot code always passes false).
    void RemoveCollision(bool unknown) { m_body.RemoveCollision(unknown); }    // 0x005ac1c0
    void RestoreCollision(bool unknown) { m_body.RestoreCollision(unknown); }  // 0x005ac1b0
    // Bone positions (aiming): a random one / one the shooter can see.
    const S_vector &GetRandomBonePos() const;           // 0x005a3cc0
    const S_vector &GetAimPoint(const C_Human *shooter) const;   // 0x005a3ab0
    // Environment flags under the human (vehicles report their own).
    uint32_t GetEnvironmentFlags() const;               // 0x005a9720 (HUMAN_ENV_*)

    // --- vehicles / carried bodies -----------------------------------------------------------
    // Vehicle (or static weapon) the human is using, if any.
    C_Actor *GetVehicle() const { return m_vehicle; }
    int GetVehicleSeat() const { return m_seat; }       // 0 = driver
    C_Actor *GetCarried() const { return m_carried; }
    // The seat the human is in (C_HumanIdle::UpdateInteraction).
    void SetVehicle(C_Actor *vehicle, int seat)
    {
        m_vehicle = vehicle;
        m_seat = seat;
    }
    // Forgets the vehicle (a carried body taken out of its seat).
    void ClearVehicle()
    {
        m_vehicle = nullptr;
        m_seat = -1;
    }
    void DropCarried();                                 // 0x005a97c0

    // --- damage ------------------------------------------------------------------------------
    // Network client: a hit the server reports (new `health`; `pos` relative to the human).
    void ApplyRemoteHit(int type, int zone, int health, int killerId, const S_vector &pos, const S_vector &dir);   // 0x005a15d0
    // Tells the AI of the human that `shooter` shoots at it (ignored for local players and
    // deaf humans).
    void NotifyShotAt(C_Actor *shooter, const S_vector &velocity);   // 0x005a5960

    // --- inventory ---------------------------------------------------------------------------
    // Set when the items change (moves, reloads); cleared every frame.
    void SetInventoryChanged(bool on) { m_inventoryChanged = on; }
    bool IsInventoryChanged() const { return m_inventoryChanged; }
    C_Item *GetHandsItem() const;                       // 0x005a2ac0
    I3D_frame *GetHandsItemFrame() const;               // 0x005a2ad0
    C_Item *GetLongGunItem() const;                     // 0x005a2af0
    C_Item *GetUniformItem() const;                     // 0x005a6da0
    // The item in the hands if it is a weapon.
    C_WeaponItem *GetHandsWeapon() const;               // 0x005a2b10
    // The item in the hands changed (tells the controller).
    void OnHandsChanged();                              // 0x005a6a90
    // The uniform changed to item `uid` (tells the controller).
    void OnUniformChanged(int uid);                     // 0x005a6920
    // Dresses the human in the uniform item `uid` (server / single player; the client
    // version only on clients). False if there is no such item.
    bool PutOnUniform(int uid);                         // 0x005a6930
    void ClientPutOnUniform(int uid);                   // 0x005a6a00
    // Next item of type `typeId` in the containers (head, long gun, hands, uniform, backpack,
    // belt, backpack contents); `*container` receives where it is.
    C_Item *FindItem(int typeId, int *container = nullptr) const;   // 0x005a78f0
    // Item with the unique id `uid` in the containers, null if none.
    C_Item *FindItemByUid(int uid) const;               // 0x005a6ff0
    C_Item *FindItemInContainer(int uid, int container) const;   // 0x005a6db0
    // First item whose definition has behaviour `behaviour` (*container 7: none).
    C_Item *FindItemByBehaviour(int behaviour, int *container) const;   // 0x005a71b0
    C_Item *FindItemByRecordType(int recordType) const; // 0x005a7430
    // Belt item of behaviour `behaviour` that is not busy.
    C_Item *FindBeltItem(int behaviour) const;          // 0x005a6730
    C_Item *GetMedkit() const;                          // 0x005a7e00
    C_Item *FindBeltItem19() const;                     // 0x005a7ed0
    // The first item of behaviour `behaviour` (null: none) and in `*container` its container
    // (C_HumanController::UseMedkit uses the item).
    C_Item *FindContainerOfBehaviour(int behaviour, int *container) const;   // 0x005a7810
    // Every item of behaviour `behaviour` with its container.
    bool CollectItems(int behaviour, std::vector<C_Item *> &items, std::vector<int> &containers) const;   // 0x005a7850
    // Number of items of type `typeId` (the first group found).
    int CountItems(int typeId) const;                   // 0x005a6aa0
    int CountAmmo(int typeId, bool backpack) const;     // 0x005a6c50
    int CountThrownItems() const;                       // 0x005a6d10
    bool IsWearingDivingGear() const;                   // 0x005a66b0
    // Takes item `uid` out of the containers (`destroy`: delete it as well).
    void RemoveItem(int uid, bool destroy);             // 0x005a7b70
    void RemoveItemsFromStore(int typeId);              // 0x005a7b30
    void CreateItemInStore(int typeId, int container);  // 0x005af760
    // Fills the equipment fields of `state`.
    void GetEquipmentState(S_SoldierState &state) const;   // 0x005a8b50
    // Creates the items of `state` (and stores the new uids in it on network games).
    void CreateEquipment(S_SoldierState *state);        // 0x0059eb60
    // Inventory requests to the controller (they go to the server).
    bool RequestMoveItem(C_Item *item, C_Actor *other, int fromContainer, int toContainer, int slot);   // 0x005a2b40
    bool RequestTransferItem(C_Item *item, int arg, C_Human *other, C_Item *otherItem, int container);   // 0x005a2bb0
    void RequestThrowItem(C_Item *item, int arg, const S_vector &dir);   // 0x005a2c50
    // Item `index` of supply box `box` (C_HumanController::RequestTakeFromBox).
    void RequestTakeFromBox(C_Actor *box, int index);   // 0x005a2cf0
    // The item lying in the world as temporary actor `itemActor`.
    void RequestPickUpItem(C_Actor *itemActor);         // 0x005a2d20
    // Weapons in the hands when there is nothing there.
    bool DrawLongGun();                                 // 0x005a67c0
    bool DrawSidearm(int reason);                       // 0x005a6830
    // Server side of the requests (and the client side of what the server decided).
    bool ServerMoveItem(int uid, int fromActorId, int fromContainer, int *toContainer, int slot);   // 0x005a2d50
    void ClientMoveItem(int uid, int fromActorId, int fromContainer, int toContainer, int slot);    // 0x005a2e10
    bool ServerTransferItem(int uid, int container, int otherId, int otherUid, int otherContainer);   // 0x005a2e90
    void ClientTransferItem(int uid, int container, int otherId, int otherUid, int otherContainer);   // 0x005a2f50
    void DropItem(int uid, const S_vector &velocity);  // 0x005a3490
    void ClientDropItem(int tempActorId, int uid, const S_vector &velocity);   // 0x005a3750
    // Drops what the human carries (weapon in the hands, multiplayer pick-ups).
    void DropAllItems();                                // 0x005a2fe0
    bool TakeFromSupplyBox(int boxId, int index, int *container, int *uid);   // 0x005a37c0
    void ClientTakeFromSupplyBox(int boxId, int index, int container, int uid);   // 0x005a38f0
    bool PickUpItem(int tempActorId, int *container);   // 0x005a39d0
    void ClientPickUpItem(int tempActorId, int container);   // 0x005a3a40

    // --- senses ----------------------------------------------------------------------------
    // Sound module callback for the human's hearing.
    static void __stdcall OnSoundHeard(const S_HeardSound *sound);   // 0x005a5430
    // Sight module callback: `lost` 0 = target `targetId` came into view, 1 = out of view.
    void OnSightEvent(int lost, int targetId);          // 0x005a5250
    bool HasSeenEnemy(const C_Human *human) const;      // 0x005a5e50
    // Hostile humans in view now.
    const std::vector<C_Human *> &GetSeenEnemies() const { return m_seenEnemies; }
    // Sight module registrations (-1: none).
    int GetSightId() const { return m_sightId; }
    int GetSightTargetId() const { return m_sightTargetId; }
    // Hearing / sight registrations (suspended humans drop them).
    void StopListening();                               // 0x005a7f50
    void StartListening();                              // 0x005a7fa0
    // Event of the human's event queue started / ended.
    // Periodic links (proximity checks): `other` came within / left the range at `distance`.
    void OnProximity(C_Actor *other, float distance, bool ended, bool unknown);   // 0x005a83f0
    void ProcessEvents();                               // 0x005a8480
    void RaiseEvent(S_HumanEvent *event);               // 0x005a8b30

    // --- squad -----------------------------------------------------------------------------
    C_Human *GetLeader() const { return m_leader; }     // 0x005a9100
    C_FormationShape *GetFormation() const { return m_formation; }   // 0x005a9110
    // Follows `leader` (null: none); the leader gets a formation of the followers.
    void SetLeader(C_Human *leader);                    // 0x005a91a0
    void DisbandFormation();                            // 0x005a9120
    int GetFormationType() const;                       // 0x005a9960

    // --- chat / messages ------------------------------------------------------------------
    // Quick message `index` of the interface said by a player (shown to his team).
    void SayQuickMessage(int index);                    // 0x005a46a0
    void SetChatText(const char *text);                 // 0x005a9b30
    // The last chat line (multiplayer; shown on the team map while its time runs).
    const char *GetChatText() const { return m_chatText; }
    int GetChatTime() const { return m_chatTime; }
    // Prints game text `textId` (a printf format) in `color` (0..255 components).
    static void PrintText(const C_Human *human, const S_vector &color, int textId, ...);   // 0x005a4600
    // Same, prefixed with "<short name>: ".
    static void PrintNamedText(const C_Human *human, const S_vector &color, int textId, ...);   // 0x005a4770

    // --- script commands (HUMAN_*), forwarded to the scripted controller ----------------------
    bool IsAISuspended() const;                         // 0x005a5d30
    void SetAIOptimized(int mode);                      // 0x005af5c0
    void Suspend(bool suspend);                         // 0x005af570
    void ResetAI();                                     // 0x005af450
    void SetAlarmType(uint32_t flags, bool set);        // 0x005af150
    uint32_t GetAlarmType() const;                      // 0x005af170
    bool SeesEnemy() const;                             // 0x005af620
    bool SeesFriend() const;                            // 0x005af640
    void SetAlarm(bool on);                             // 0x005af190
    int FindCheckpoint(const char *name) const;         // 0x005af210, 0xFFFF if missing
    // Movement / attack orders report completion to the script label `label`.
    void MoveTo(int checkpoint, int label);             // 0x005af1d0
    void AttackMoveTo(int checkpoint, int label);       // 0x005af1b0
    void AttackActor(C_Actor *target, int time, bool fake, int label);      // 0x005af230
    void AttackFrame(I3D_frame *target, int time, bool fake, int label);    // 0x005af250
    void MoveToAlarm(int label);                        // 0x005af6e0
    void Reload();                                      // 0x005af4f0
    void StopAction();                                  // 0x005af430
    void SetAnimation(const char *name, int blendTime, int cancelTime, bool loop);   // 0x005af780
    void TurnAt(I3D_frame *target);                     // 0x005af660
    void LookAround(int type);                          // 0x005af6a0
    void SetWaitForPlayer(bool on, float stopDist2, float resumeDist2, bool watch);   // 0x005af5e0
    void SetSniper(bool on, float radius);              // 0x005af700
    void SetMoveModeRun();                              // 0x005af270
    void SetMoveModeWalk();                             // 0x005af290
    void SetMoveModeSilence();                          // 0x005af2b0
    void SetMoveModeGuard();                            // 0x005af2d0
    void SetMoveModeLie();                              // 0x005af2f0
    void SetMoveModeCrouch();                           // 0x005af310
    void SetMoveModeStand();                            // 0x005af330
    void SetMoveModeStandFast();                        // 0x005af350
    // AI mode of an AI-controlled human (0x100 aggressive, 0x200 defensive, 0x400 zombie).
    void SetAIMode(int mode);                           // 0x005af3f0
    void TakeWeapon();                                  // 0x005af4b0
    void HolsterWeapon();                               // 0x005af4d0
    void RaiseWeapon();                                 // 0x005af740
    void LowerWeapon();                                 // 0x005af750
    void PickObject(I3D_frame *object, bool oneHand);   // 0x005af470
    void DropObject();                                  // 0x005af490
    // Plays an idle activity; returns its duration in ms (0 if not AI-controlled).
    int PlayActivity(int activity);                     // 0x005af510
    int PlayActivity(int activity, I3D_frame *target);  // 0x005af530
    void ActivityTurk();                                // 0x005af390
    void Sit(I3D_frame *seat);                          // 0x005af370
    void EnableEvents();                                // 0x005af3d0
    void DisableEvents();                               // 0x005af3b0
    // Scripted controller members without a script command (not called).
    void ScriptedAdvanceScript(int a, int b);           // 0x005af1f0
    bool ScriptedHasAIMode();                       // 0x005af410
    void ScriptedPlaySpeech(int arg);                // 0x005af550
    void ScriptedLookAround1(int arg);                // 0x005af6c0

private:
    // The controllers' base class and the AI's work on the human's state directly.
    friend class C_HumanController;
    friend class C_AIController;
    friend class C_SquadController;
    friend class C_EnemyController;
    friend class C_SniperController;
    friend class C_ScriptedController;
    friend class C_PlayerController;
    friend class C_RemoteController;

    // Mission restarts per game kind (RestartMission()).
    void RestartSinglePlayer(bool server);              // 0x0059c4d0
    void RestartCoop();                                 // 0x0059ca00
    void RestartMultiplayer();                          // 0x0059cec0
    void SaveState(C_chunk &chunk);                     // 0x0059d190 (chunk 0x3E9)
    void LoadState(C_chunk &chunk);                     // 0x0059d970
    // Multiplayer: asks for a respawn once the death animation has ended.
    void RequestRespawn();                              // 0x0059f010
    void ApplyDifficulty();                             // 0x005a8990
    void TrainSkills(int timeDelta);                    // 0x005a88f0
    void OnDeath();                                     // 0x005a0220
    // Client side of a death (and the server's for the killed human's own effects).
    void ShowDeath();                                   // 0x005a03e0
    // This human (the killer) killed `victim` with damage `type`.
    void OnKilledHuman(C_Human *victim, int type);      // 0x005a0be0
    // Multiplayer messages about hits and kills.
    void ShowHitMessage(int type, int zone, int killerId);   // 0x005a0da0
    void TickBleeding(int timeDelta);                   // 0x005a0420
    void UpdateBleeding(int frameTime);                 // 0x005a0480
    void UpdateVelocity(int timeDelta);                 // 0x005a0660
    void ClearContainers();                             // 0x005a29c0
    void ForgetOldSounds();                             // 0x005a5380
    void UpdateSeenHumans();                            // 0x005a50a0
    void AddSeenBody(C_Human *human);                   // 0x005a4840
    void AddSeenEnemy(C_Human *human);                  // 0x005a4a10
    void AddSeenFriend(C_Human *human);                 // 0x005a4be0
    void AddSeenTraitor(C_Human *human);                // 0x005a4da0
    void RemoveSeenTraitor(C_Human *human);             // 0x005a4f60
    void RemoveSeenFriend(C_Human *human);              // 0x005a4fc0
    void RemoveSeenEnemy(C_Human *human);               // 0x005a5020
    // Network games: IsFriendOf().
    bool IsFriendOfInNetGame(const C_Human *other) const;   // 0x005a09a0
    void ClearHeardSounds();
    void ResetAIMemory();
    // The movement stops and the model is hidden (DetachFromWorld) / back (restarts).
    void SetBodyDetached(bool on);

    bool m_initialized = false;                         // +0x090 StartMission set the human up
    float m_unknown94 = 0.0f;                           // +0x094 passed on to the movement
    S_HumanBody m_body;                                 // +0x098
    C_HumanController *m_controller = nullptr;          // +0x0B0
    C_HumanEvents *m_events = nullptr;                  // +0x0B4
    S_SoldierProfile *m_profile = nullptr;              // +0x0B8
    C_str m_faceTexture;                                // +0x0BC
    S_SoldierProgress m_progress;                       // +0x0C0
    int m_stealthTime = 0;                              // +0x0E4 ms towards the next stealth gain
    int m_enduranceTime = 0;                            // +0x0E8 ms towards the next endurance gain
    int m_stats[STAT_COUNT] = {HUMAN_FULL_HEALTH, 80, 80, 80, 80, 80, 50, 30, 20, 50, 0, 80};   // +0x0EC
    int m_initialStats[STAT_COUNT] = {HUMAN_FULL_HEALTH, 80, 80, 80, 80, 80, 50, 30, 20, 50, 0, 80};   // +0x11C
    float m_stamina = 0.0f;                             // +0x14C
    bool m_fadedOut = false;                            // +0x150
    float m_bleeding = 0.0f;                            // +0x154 s left
    int m_bloodDropTime = 0;                            // +0x158 ms
    int m_legHits = 0;                                  // +0x15C
    C_Actor *m_vehicle = nullptr;                       // +0x160
    int m_seat = -1;                                    // +0x164
    float m_disguise = 0.0f;                            // +0x16C single player: 0..100
    int m_profileIndex = -1;                            // +0x170
    C_Actor *m_carried = nullptr;                       // +0x174 a body being carried
    bool m_inventoryChanged = false;                    // +0x178
    int m_actionTime = 0;                               // +0x17C ms left
    int m_actionTotal = 0;                              // +0x180
    int m_actionText = 0;                               // +0x184
    int m_periodicLink = -1;                            // +0x188
    std::vector<S_HumanOrder *> m_pendingOrders;        // +0x18C
    E_HumanRole m_role = HUMAN_ROLE_NEUTRAL;            // +0x19C
    int m_group = 1;                                    // +0x1A0
    bool m_carnage = false;                             // +0x1A4 counted as a carnage enemy
    bool m_unknown1a5 = false;                          // +0x1A5
    bool m_hostileToPlayers = false;                    // +0x1A6
    int m_nameTextId = -1;                              // +0x1A8 -1: default name of the role
    int m_unknown1ac = 0;                               // +0x1AC
    S_vector4 m_velocity = {};                          // +0x1B0 m/s
    S_vector4 m_prevVelocity = {};                      // +0x1C0
    S_vector4 m_lastPos = {};                           // +0x1D0
    S_vector4 m_scale = {};                             // +0x1E0 (1, 1, 1) from StartMission; the member panel's
                                                        // photo takes it (I3D_material::SetAmbient)
    int m_unknown200 = 0;                               // +0x200
    int m_deadTime = 0;                                 // +0x204 ms since death (multiplayer respawn)
    int m_aliveTime = 0;                                // +0x208 ms since spawn (spawn protection)
    bool m_inPlay = false;                              // +0x20C multiplayer: a player's human takes part
    int m_mpZone = 0;                                   // +0x210 actor id of the multiplayer zone
    char m_chatText[64] = {};                           // +0x214
    int m_chatTime = 0;                                 // +0x254 ms the chat text stays
    int m_sightId = -1;                                 // +0x258 sight module observer
    int m_sightTargetId = -1;                           // +0x25C sight module target
    I3D_frame *m_bodyFrame = nullptr;                   // +0x260
    std::vector<C_Human *> m_seenEnemies;               // +0x264 hostile humans in view
    std::vector<C_Human *> m_seenFriends;               // +0x274 friendly humans in view
    std::vector<C_Human *> m_seenBodies;                // +0x284
    std::vector<C_Human *> m_seenTraitors;              // +0x294 disguised enemies in view (IsDisguisedEnemy)
    int m_soundListener = -1;                           // +0x2A4
    std::vector<S_RememberedSound *> m_heardSounds;     // +0x2A8
    int m_unknown2b8 = 0;                               // +0x2B8
    int m_unknown2bc = 0;                               // +0x2BC
    C_SpeechSet *m_speechSet = nullptr;                 // +0x2C0
    C_HumanAIMemory *m_aiMemory = nullptr;              // +0x2C4
    C_FormationShape *m_formation = nullptr;            // +0x2C8 formation this human leads
    C_Human *m_leader = nullptr;                        // +0x2CC
};

// Human creator (g_actorCreators[ACTOR_HUMAN], vtable 0x008111d4).
class C_HumanCreator : public C_ActorCreator {
public:
    C_Actor *Create() override;                         // 0x0059be60
    const char *GetName() override { return "Human"; }  // 0x0059bed0
    void Destroy(C_Actor *actor) override;              // 0x0059bee0
};
