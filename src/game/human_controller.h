// The controllers that drive a human (C_Human::GetController()): the player's input, the AI
// of squad members, enemies and civilians, the mission scripts and, on a network client, the
// state received from the server.
//
// The base class is hand-ported (game/human_controller.cpp; its network messages in
// game/controller_messages.*), and so is the AI's base C_AIController (game/ai_*.cpp). The
// derived controllers are not yet (their code is at 0x00524c60..0x00547xxx and 0x00548xxx..
// 0x00591xxx, translated mechanically in src/tier1): for them this header declares the
// members C_Human and the ported script code call by name. Methods the original calls
// without a vtable are declared with their addresses.
#pragma once

#include <cstdint>
#include <vector>

#include "game/ai_parts.h"
#include "game/controller_messages.h"
#include "ls3d/math.h"

class C_Actor;
class C_BitStream;
class C_Car;
class C_chunk;
class C_FormationShape;
class C_Checkpoints;
class C_Human;
class C_HumanEvents;
class C_Item;
class C_NetObject;
class C_Script;
class C_NetActionMessage;
class I3D_frame;
struct S_Checkpoint;
struct S_RememberedSound;

// C_HumanController::GetType().
enum E_HumanController {
    HUMAN_CONTROLLER_NONE = 0,
    HUMAN_CONTROLLER_PLAYER = 1,          // input of a player (also for remote players on the server)
    HUMAN_CONTROLLER_SQUAD = 3,           // single player: squad member the player does not control
    HUMAN_CONTROLLER_ENEMY = 4,           // enemy AI
    HUMAN_CONTROLLER_AI = 5,              // AI driven by the human's mission script
    HUMAN_CONTROLLER_NEUTRAL = 6,         // civilian / allied NPC AI
    HUMAN_CONTROLLER_REMOTE = 7,          // network client: copies what the server sends
    HUMAN_CONTROLLER_SNIPER = 8,          // sniper AI (HUMAN_SetSniper; vtable 0x00811618)
};

// C_HumanController::GetWeaponStance() (the first-person view's poses of the same values).
enum E_WeaponStance {
    WEAPON_STANCE_LOWERED = 1,
    WEAPON_STANCE_RAISED = 2,
};

// C_HumanController::GetMoveFlags() bits identified so far (names partly inferred).
enum : uint32_t {
    MOVE_LYING = 0x00000001,
    MOVE_CROUCHING = 0x00000002,
    MOVE_STANDING = 0x00000004,
    MOVE_STANCE_MASK = 0x00000007,
    MOVE_MOVING_MASK = 0x000000F8,        // some kind of movement
    // Directions of the moving animations (C_HumanAnimPlayer: 0xBE / 0xC6 forward, 0xBF / 0xC7
    // back, 0xC0 / 0xC8 and 0xC1 / 0xC9 the sides, the diagonals combined; the ladder: 0x6A
    // up as forward, 0x6B / 0x6C down as back; directions partly inferred).
    MOVE_8 = 0x00000008,                  // forward (with STATE_MARCHING: trains endurance)
    MOVE_FORWARD = MOVE_8,
    MOVE_BACK = 0x00000010,
    MOVE_LEFT = 0x00000020,
    MOVE_RIGHT = 0x00000040,
    MOVE_AI_MODE_MASK = 0x00000700,       // SetAIMode() modes 0x100 / 0x200 / 0x400 (the command
                                          // map's "aggresive", "defensive", "passive")
    // Skips the player's sight tests; a multiplayer player with it may switch to spectating
    // after 3 s (inferred: the human is dead).
    MOVE_DEAD = 0x00000800,
    // Paces, from the least to the most visible (C_Human::GetVisibility); none of them set
    // is the fastest.
    // (The command map's buttons: 0x1000 "slow", 0x2000 "walk", 0x4000 "run", 0x10000 "sprint".)
    MOVE_PACE_1 = 0x00001000,
    MOVE_PACE_3 = 0x00002000,
    MOVE_PACE_4 = 0x00004000,
    MOVE_PACE_2 = 0x00008000,
    MOVE_PACE_MARCH = 0x00010000,         // gait 5 (C_HumanWork::SetGait)
    MOVE_PACE_MASK = 0x0001F000,
    MOVE_SWIMMING = 0x00040000,
    MOVE_MEDIUM_MASK = 0x001E0000,        // SetMoveFlags replaces it as a group (0x1E0000: cleared)
    // The animations 0xA2 / 0xA4 and 0xA3 / 0xA5 (leaning, inferred).
    MOVE_LEAN_1 = 0x01000000,
    MOVE_LEAN_2 = 0x02000000,
    MOVE_LEAN_MASK = 0x03000000,
    MOVE_UNCONSCIOUS = 0x04000000,
    MOVE_DEAF = 0x40000000,               // ignores sights, sounds and being shot at
};

// C_HumanController::GetStateFlags() bits identified so far (inferred).
enum : uint32_t {
    STATE_SNEAKING = 0x00001000,          // single player: trains stealth
    STATE_MARCHING = 0x00010000,          // single player: trains endurance (with MOVE_8)
    STATE_TREADING = 0x00016000,          // any of these sets off a mine stepped on
};

// Order given to a human (C_HumanController::IssueCommand; HUMAN_BoardVehicle). Polymorphic
// in the original (vtable 0x0080f720, six no-op methods). The commands of category 2, which
// C_HumanController::ExecuteCommand carries out (names partly inferred):
enum E_HumanCommandType {
    HUMAN_COMMAND_VEHICLE = 2,                          // the category of all of them
    HUMAN_COMMAND_LOCK_DOOR = 0x00,                     // the door's key if carried, else -1
    HUMAN_COMMAND_UNLOCK_DOOR = 0x01,
    HUMAN_COMMAND_DEFUSE_MINE = 0x02,                   // a temporary actor; 4 s
    HUMAN_COMMAND_ENTER_VEHICLE = 0x03,                 // C_HumanController::UseActor: also radios, switches, photo places
    HUMAN_COMMAND_PLANT_EXPLOSIVE = 0x04,               // an explosive place; 3 s
    HUMAN_COMMAND_LEAVE_VEHICLE = 0x05,
    HUMAN_COMMAND_PICK_UP_BODY = 0x06,
    HUMAN_COMMAND_PICK_LOCK = 0x0E,                     // 0.5 .. 5.5 s by the lockpicking skill
    HUMAN_COMMAND_HEAL = 0x0F,                          // another human; 3 s
    HUMAN_COMMAND_CUT_WIRE = 0x10,                      // a temporary C_WireCut; 3.9 s
    HUMAN_COMMAND_LAY_MINE = 0x11,                      // the mine in the hands; 2 s
    HUMAN_COMMAND_TIE_UP = 0x13,                        // 5 s
    HUMAN_COMMAND_PLACE_CAR_CHARGE = 0x14,              // the hands item (behaviour 11) on a car
    HUMAN_COMMAND_OPEN_DOOR = 0x1B,
    HUMAN_COMMAND_CLOSE_DOOR = 0x1C,
    HUMAN_COMMAND_USE_LADDER = 0x1E,
    HUMAN_COMMAND_CLIMB = 0x1F,                         // BUTTON_CLIMB
    HUMAN_COMMAND_DELIVER_DOCUMENTS = 0x22,             // a document place
    HUMAN_COMMAND_HEAL_SELF = 0x23,                     // param1: the medkit's uid; 3 s
    HUMAN_COMMAND_BACKSTAB = 0x24,                      // 1 s
    HUMAN_COMMAND_DISARM_EXPLOSIVE = 0x26,              // an explosive place; 4 s
};
struct S_HumanCommand {
    int category = 0;
    int type = 0;
    int param1 = 0;                                     // the actor's network id (or the medkit's uid)
    int param2 = 0;                                     // the seat
};

// C_HumanController::RequestAction() actions (names partly inferred).
enum E_ControllerAction {
    CONTROLLER_ACTION_LOWER_WEAPON = 0,
    CONTROLLER_ACTION_RAISE_WEAPON = 1,
    CONTROLLER_ACTION_FIRE = 2,
    CONTROLLER_ACTION_THROW_BELT_GRENADE = 4,           // body flag 0x4000: the belt grenade leaves the hand
    CONTROLLER_ACTION_RELOAD = 5,
    CONTROLLER_ACTION_THROW = 6,                        // body flag 0x1000: the hands item leaves the hand
    CONTROLLER_ACTION_THROW_2 = 7,                      // body flag 0x2000
    CONTROLLER_ACTION_8 = 8,                            // body flag 0x20000 (the hands item; the knife's stab)
    CONTROLLER_ACTION_MELEE_2 = 9,                      // the knife's second attack (FireSecondary)
    CONTROLLER_ACTION_EMPTY = 0xD,                      // the click of an empty weapon
    // A charged throw released (the player): the body's throw of the hands item starts; the
    // item leaves the hand with CONTROLLER_ACTION_THROW / THROW_2.
    CONTROLLER_ACTION_START_THROW = 0xE,
    CONTROLLER_ACTION_START_THROW_2 = 0xF,
    CONTROLLER_ACTION_START_BELT_THROW = 0x10,          // BUTTON_THROW
};

// An entry of a squad member's order queue (squad AI, released by ReleaseHumanOrder()).
struct S_HumanOrder;
void ReleaseHumanOrder(S_HumanOrder *order);            // 0x00583980

// The squad members' combat orders (S_HumanOrder types 0xC..0x11): three modes in pairs, the
// second of a pair also sets the controller's byte +0x18A (C_SquadController::GetCombatOrder /
// SetCombatOrder; meanings not identified).
enum E_SquadCombatOrder {
    SQUAD_ORDER_MODE0 = 0x0C,
    SQUAD_ORDER_MODE0_ALT = 0x0D,
    SQUAD_ORDER_MODE2 = 0x0E,
    SQUAD_ORDER_MODE2_ALT = 0x0F,
    SQUAD_ORDER_MODE1 = 0x10,
    SQUAD_ORDER_MODE1_ALT = 0x11,
};

// The input a controller drives the human with (the player's from the keys, the remote
// controller's decoded from the network): C_HumanController::m_axes and m_buttons, 64 values
// each. The indexes the base controller reads (names partly inferred):
constexpr int CONTROL_COUNT = 64;
enum E_ControlAxis {
    AXIS_FORWARD = 0,
    AXIS_BACK = 1,
    AXIS_LEFT = 2,
    AXIS_RIGHT = 3,
    AXIS_FIRE = 6,                                      // held fire (cleared for the weapons of behaviour 2 / 4 / 6)
    AXIS_LEAN_LEFT = 10,                                // MOVE_LEAN_1
    AXIS_LEAN_RIGHT = 11,                               // MOVE_LEAN_2
    AXIS_RELOAD = 13,                                   // reloads the seat weapon (the AI gunners)
};
enum E_ControlButton {
    BUTTON_FIRE = 6,
    BUTTON_7 = 7,                                       // a grenade's second throw
    BUTTON_CROUCH = 8,                                  // toggles
    BUTTON_LIE = 9,                                     // toggles
    BUTTON_DIVE = 12,                                   // with forward: a dive (10 stamina)
    BUTTON_RELOAD = 13,
    BUTTON_THROW = 16,                                  // a grenade from the belt
    BUTTON_CLIMB = 17,                                  // command 0x1F (the body's action 30)
};

// Network messages a controller queues (game/controller_messages.h).
struct S_ControllerMessage;

// The control state a remote side replays (S_MsgControlState::controlBits, 9 bits; decoded by
// C_HumanController::DecodeControls): the stance, the pace, the leaning, the side walked to.
uint32_t EncodeControlBits(const float *axes, uint32_t moveFlags);   // 0x005ac550 (__thiscall on the axes)

// Base of all controllers (vtable 0x00810fb8; 0xC0 bytes, 16-byte aligned; 0x00582ff0..
// 0x0058c510 without the AI task pools). Most of its work is the interactions with actors
// (doors, mines, vehicles, bodies, medkits ...) that the commands start and EndInteraction
// completes, the inventory requests, speech, and the controls of the player and remote
// controllers (ApplyMoveControls / ApplyWeaponControls).
class alignas(16) C_HumanController {
public:
    explicit C_HumanController(E_HumanController type);   // 0x00582ff0
    C_HumanController(const C_HumanController &) = delete;
    C_HumanController &operator=(const C_HumanController &) = delete;

    // The local player or the driver steers the vehicle with the controls (cars, submarines).
    virtual void ControlVehicle();                                          // 0x00 0x00583bc0
    // Adjusts the direction a shot is fired in (the AI scatters it by its aim; base: true,
    // nothing changes).
    virtual bool AdjustAim(S_vector *dir);                                  // 0x04 0x00583090
    // Asks the server for a body action (E_ControllerAction; the player and remote controllers
    // send it): from `pos` along `dir`, a throw's `strength`; a reload passes the magazine used
    // up and the rounds loaded, the others -1. Base: nothing.
    virtual void RequestAction(int action, const S_vector4 *pos, const S_vector4 *dir, float strength,
                               int ammoUid, int rounds);                    // 0x08 (empty)
    // Ends the interaction going on (doors, mines, wire cutting, ...); `completed`: carry out
    // its effect.
    virtual void EndInteraction(bool completed);                            // 0x0C 0x00584e30
    // Human `humanId` was healed by `health` with item `itemUid` (removed if `removeItem`):
    // the messages, and on the clients the health.
    virtual void Heal(int humanId, int itemUid, int health, bool removeItem);   // 0x10 0x00588870
    // Uses the medkit `itemUid` (-1: the first one carried) on human `humanId` (false: no
    // patient that needs it, or no medkit; nobody reads the result).
    virtual bool UseMedkit(int humanId, int itemUid);                       // 0x14 0x00588aa0
    // (Frees the queued messages: the original only freed the vectors.)
    virtual ~C_HumanController();                                           // 0x18 0x00583280 (0x005832a0)
    // Takes control of `human` from `previous` (null when there was none).
    virtual void Attach(C_Human *human, C_HumanController *previous);       // 0x1C 0x005832f0
    virtual void Detach();                                                  // 0x20 0x00583690
    virtual void Update(int /*frameTime*/) {}                               // 0x24 0x0056f090
    virtual void Tick(int /*timeDelta*/) {}                                 // 0x28 0x0056f0a0
    virtual void SaveGame(C_chunk &chunk);                                  // 0x2C 0x00587550
    virtual void LoadGame(C_chunk &chunk);                                  // 0x30 0x00587830
    // The sight module's view test of a meleeTracker / a human (the AI's and the player's
    // controllers): `visibility` 100 when the target is within the human's sight range
    // (the target's GetVisibility, the camera's view distance) and inside its field of view
    // (50 degrees each side, 60 for the squad), else 0; `status` 99, or 500 when the test
    // is skipped (move flag 0x800, or MOVE_DEAF for the AI; no caller reads it).
    virtual void TestSightOfMeleeTracker(C_Actor * /*meleeTracker*/, int * /*status*/, float * /*visibility*/) {}          // 0x34 0x005830c0
    virtual void TestSightOfHuman(C_Human * /*target*/, int * /*status*/, float * /*visibility*/) {}     // 0x38 0x005830b0
    // `actor` came within / left the human's proximity range (C_Human::OnProximity).
    virtual void OnActorNear(C_Actor * /*actor*/, float /*distance*/) {}    // 0x3C 0x005830d0
    virtual void OnActorGone(C_Actor * /*actor*/, float /*distance*/) {}    // 0x40 0x005830e0
    // Issues the command to leave the vehicle (HUMAN_COMMAND_LEAVE_VEHICLE).
    virtual void RequestLeaveVehicle();                                     // 0x44 0x00588c20
    virtual const S_vector &GetFacingDir();                                 // 0x48 0x005830f0
    virtual const S_vector &GetViewDir();                                   // 0x4C 0x00584b50
    virtual void SetFacingDir(const S_vector4 &dir);                        // 0x50 0x00583100
    virtual void SetViewDir(const S_vector4 &dir);                          // 0x54 0x00583120
    // Puts the height of `pos` (where the locomotion moves the human) on the AI's path:
    // interpolated along the segment walked (true; the locomotion then limits the climb), or
    // the waypoint's height while the human is within its radius (false). Base: false.
    virtual bool AdjustToPathHeight(S_vector *pos);                         // 0x58 0x00583140
    // The light at the body measured by the controller (`intensity`, `reference`; false:
    // measure it).
    virtual bool Unknown5C(float *intensity, float *reference);             // 0x5C 0x00583150
    // Multiplayer: the player's human is waiting (dead or between rounds) and the player
    // watches the others.
    virtual void SetSpectating(bool on);                                    // 0x60 0x00524d30
    virtual bool IsSpectating();                                            // 0x64 0x00524d40
    virtual void OnUniformChanged(int uid);                                 // 0x68 0x00588160
    // Inventory requests (the player's controller sends them to the server, the others carry
    // them out; containers are ITEM_CONTAINER_*).
    // The item lying in the world as temporary actor `actorId`.
    virtual void RequestPickUpItem(int actorId, int container);             // 0x6C 0x00587bc0
    virtual void RequestThrowItem(int uid, const S_vector &velocity);       // 0x70 0x00587be0
    virtual bool RequestMoveItem(int uid, int otherId, int fromContainer, int toContainer, int slot);   // 0x74 0x00587bf0
    virtual bool RequestTransferItem(int uid, int arg, int otherId, int otherUid, int container);      // 0x78 0x00587e00
    // Item `index` of supply box `boxId`.
    virtual void RequestTakeFromBox(int boxId, int index);                  // 0x7C 0x00587ba0
    // Body state flags of the human's work object, every frame.
    virtual void OnBodyFlags(uint32_t flags);                               // 0x80 0x005839d0
    virtual void OnDeath();                                                 // 0x84 0x005836e0
    virtual void OnKilled();                                                // 0x88 0x00583940
    virtual void OnKnockedOut();                                            // 0x8C 0x00524d50
    // Sets MOVE_UNCONSCIOUS like OnKnockedOut (no controller overrides either; the client
    // side of a knock-out, C_Human::SetUnconscious).
    virtual void SetUnconscious();                                          // 0x90 0x00524d60
    virtual void OnKilledEnemy(C_Human * /*victim*/) {}                     // 0x94 0x00583160
    virtual void OnEnemySeen(C_Human * /*human*/) {}                        // 0x98 0x00583170
    virtual void OnEnemyLost(C_Human * /*human*/) {}                        // 0x9C 0x00583180
    virtual void OnBodySeen(C_Human * /*human*/) {}                         // 0xA0 0x00583190
    // The actor the AI fired at last (base: null).
    virtual C_Actor *GetLastTarget() { return nullptr; }                    // 0xA4 0x005831a0
    virtual void OnBodyHeard(C_Actor * /*source*/) {}                       // 0xA8 0x005831b0
    virtual void OnFriendHeard(C_Actor * /*source*/) {}                     // 0xAC 0x005831c0
    virtual void OnLoudSound(C_Actor * /*source*/) {}                       // 0xB0 0x005831d0
    virtual void OnSoundHeard(S_RememberedSound * /*sound*/) {}             // 0xB4 0x005831e0
    virtual void OnShotAt(C_Actor * /*shooter*/, const S_vector & /*velocity*/) {}   // 0xB8 0x005831f0
    // Damage taken (`health` is what is left; `dir` normalised).
    virtual void OnHit(int /*type*/, int /*zone*/, int /*health*/, C_Actor * /*attacker*/,
                       const S_vector & /*pos*/, const S_vector & /*dir*/) {}   // 0xBC 0x00583200
    virtual void OnMessage(C_BitStream * /*msg*/) {}                        // 0xC0 0x00583210
    virtual void OnMessage2(C_BitStream * /*msg*/) {}                       // 0xC4 0x00583220
    virtual bool OnCollisionEx(C_Actor *other, const S_vector *pos, int a3, int a4, int a5, int a6,
                               int a7);                                     // 0xC8 0x00583230
    // AI behaviour (0x100 aggressive, 0x200 defensive, 0x400 zombie).
    virtual void SetAIMode(int mode);                                       // 0xCC 0x00524d70
    // Sets the groups of move flags present in `flags` (stance, directions, paces, speed,
    // leaning); 0 clears the directions and the leaning.
    virtual void SetMoveFlags(uint32_t flags);                              // 0xD0 0x00584b60
    // The AI's route (base: null).
    virtual std::vector<S_AIWaypoint *> *GetWaypoints() { return nullptr; } // 0xD4 0x00583240
    virtual void IssueCommand(const S_HumanCommand &command);               // 0xD8 0x00587540
    // Carries out a command (E_HumanCommand: the interactions with doors and other actors,
    // the vehicle seats; the command's actor network id in param1). False when it could not
    // start (no such actor, a seat taken ...; the server then does not pass it on); commands
    // it does not know count as carried out.
    virtual bool ExecuteCommand(const S_HumanCommand &command);             // 0xDC 0x00586200
    // Enters a vehicle's seat (param2), or uses a radio, switch or photo place (false: not).
    virtual bool UseActor(const S_HumanCommand &command);                   // 0xE0 0x00585d10
    // Leaves the vehicle (else drops a carried body, or ends the interaction).
    virtual void LeaveVehicle();                                            // 0xE4 0x00585a60
    virtual int UnknownE8();                                                // 0xE8 0x00524d90
    // Accuracy bonus of the human's aim (added to the weapon's, in %).
    virtual float GetAimBonus();                                            // 0xEC 0x00524da0
    virtual void Freeze(bool /*on*/) {}                                     // 0xF0 0x00583250
    // Handles an event of the human's queue; returns the event to release.
    virtual struct S_HumanEvent *HandleEvent(struct S_HumanEvent *event) { return event; }   // 0xF4 0x0056f0b0
    // Queues network message 0xF: the other side's C_HumanMovement::OnRemoteAction replays
    // `request` (E_RemoteRequest: the climbs over obstacles, entering a ladder from the top).
    virtual void SendRemoteRequest(int request);                            // 0xF8 0x00588d20
    // Sends the queued messages to the clients (`toClients`) or to the server (the AI
    // controllers; C_Human::DetachFromWorld flushes with IsServer()). Base: nothing.
    virtual void FlushMessages(bool /*toClients*/) {}                       // 0xFC 0x00583260
    virtual bool IsSurrendered();                                           // 0x100 0x00524db0
    // Base: not lying (MOVE_LYING clear); the scripted and remote controllers: false.
    virtual bool Unknown104();                                              // 0x104 0x00589290
    // The AI: its move order's flags 0x30 (0x10: a ladder; base: false).
    virtual bool Unknown108() { return false; }                             // 0x108 0x00583270

    E_HumanController GetType() const { return m_type; }
    uint32_t GetMoveFlags() const { return m_moveFlags; }
    uint32_t GetStateFlags() const { return m_stateFlags; }
    // Where the human looks (passed on to its body every frame).
    const S_vector4 &GetLookAt() const { return m_lookAt; }
    bool HasOrder() const { return m_interaction != -1; }
    bool IsWeaponRaised() const { return m_weaponRaised; }
    // The item in the human's hands as the controller last saw it (UpdateHandsItem).
    C_Item *GetHandsItem() const { return m_handsItem; }
    // ms since the human last made a noise of his own (firing ...; inferred from the uses:
    // noisy humans are easier to see and hear less, calm victims of a kill make it a stealth
    // kill).
    int GetQuietTime() const { return m_quietTime; }
    void ResetQuietTime() { m_quietTime = 0; }

    // Keeps the item in the human's hands for the controller (hands container).
    void UpdateHandsItem();                                                 // 0x00584830
    // Plays speech `sentence` of the human's speech set unless one played within the last
    // second; `delay` ms, `flags` sound flags. A server queues it for the clients near enough.
    // The sample's length in ms (0: not said; the original's callers mostly test only its low
    // byte).
    int Say(int sentence, int delay, uint32_t flags, bool force);           // 0x005882f0
    // Plays a sample of `sentence` (the player's soldiers also show it as a quick message);
    // `silent`: no sound (Say passes its `force`).
    void SayNow(int sentence, bool silent);                                 // 0x00588170
    // WEAPON_STANCE_RAISED while the weapon is raised (+0xAD), else WEAPON_STANCE_LOWERED.
    int GetWeaponStance() const { return m_weaponRaised ? WEAPON_STANCE_RAISED : WEAPON_STANCE_LOWERED; }   // 0x00587530
    // Tells the clients that item `uid` was dropped as temporary actor `actorId`.
    void OnItemDropped(int actorId, int uid, const S_vector *dir, float speed);   // 0x00588ef0
    void RaiseWeapon();                                                     // 0x0058c330
    void LowerWeapon();                                                     // 0x0058c400
    // Counts the interaction down by `timeDelta` and ends it when it runs out or its actor
    // went away; counts the speech delay down.
    void UpdateInteraction(int timeDelta);                                  // 0x00588010
    // Gains or loses stamina for `timeDelta` ms by the pace (the squad and the player).
    void UpdateStamina(uint32_t timeDelta);                                 // 0x00588710
    // The human faces `actor` (within 10 degrees of the view direction).
    bool IsFacing(const C_Actor *actor);                                    // 0x00584890
    // The controls into the body's commands: walking, stance, leaning, the dive, the pace
    // (the player and the remote controllers), and the weapon (firing, throwing, reloading;
    // `fireFlags` go to C_Item::Fire).
    void ApplyMoveControls();                                               // 0x00583c30 (__fastcall)
    void ApplyWeaponControls(int timeDelta, uint32_t fireFlags);            // 0x0058baf0
    // Network clients: the control bits and the velocity of a remote human back into controls.
    void DecodeControls(float *axes, float *buttons, int unused, const float *velocity,
                        const float *viewDir, int unused2, uint32_t bits);  // 0x00584c00
    // Fires `item` (`fire`: C_Item::Fire, else FireSecondary) from where the human aims; the
    // origin and direction are also returned. The result of the fire (FIRE_*).
    int UseItem(C_Item *item, bool fire, uint32_t flags, S_vector4 *origin, S_vector4 *dir);   // 0x0058b6c0
    // The same from a given origin and direction (the player and remote controllers).
    int UseItemAt(C_Item *item, bool fire, uint32_t flags, const S_vector4 &origin,
                  const S_vector4 &dir);                                    // 0x0058bab0
    // Frees the queued network messages.
    void ClearMessagesToServer();                                           // 0x00588c60 (__fastcall)
    void ClearMessagesToClients();                                          // 0x00588cc0 (__fastcall)
    // Queues message 0x17 for the clients (the enemy controller).
    void SendMessage23(bool on);                                            // 0x005890d0

    const S_Checkpoint *GetCurrentCheckpoint() const { return m_currentCheckpoint; }   // +0xA4
    uint32_t GetControlBits() const { return m_controlBits; }
    void SetControlBits(uint32_t bits) { m_controlBits = bits; }

protected:
    // Clears the interaction state (EndInteraction does it at its end).
    void ResetInteraction();
    // The HUD's inventory event `event` when the human is the player's active soldier.
    void NotifyHud(int event);                                              // 0x0058c4d0
    // Queues `message` for the clients / the server.
    void SendToClients(S_ControllerMessage *message) { m_toClients.push_back(message); }
    void SendToServer(S_ControllerMessage *message) { m_toServer.push_back(message); }

    // (+0x04..+0x0F: padding.)
    S_vector4 m_viewDir;                                // +0x10
    S_vector4 m_facingDir;                              // +0x20
    // The walk the body is asked for (the player and the remote controllers: the direction
    // times the speed; C_HumanWork::SetLookAt).
    S_vector4 m_lookAt;                                 // +0x30
    C_HumanEvents *m_events = nullptr;                  // +0x40
    C_Human *m_human = nullptr;                         // +0x44
    E_HumanController m_type;                           // +0x48
    uint32_t m_stance = 0;                              // +0x4C the stance at Attach (MOVE_STANCE_MASK)
    uint32_t m_moveFlags = 0;                           // +0x50 MOVE_*
    // The control state replicated in network games (EncodeControlBits; the net object's
    // NetRead / NetWriteControlBits).
    uint32_t m_controlBits = 0;                         // +0x54
    float m_walkSpeed = 0.0f;                           // +0x58 (0: 10)
    uint32_t m_stateFlags = 0;                          // +0x5C the pace asked for (MOVE_PACE_*)
    int m_interaction = -1;                             // +0x60 the command (E_HumanCommand) going on, -1 none
    int m_interactionTime = -1;                         // +0x64 ms until it completes
    C_Actor *m_interactionTarget = nullptr;             // +0x68
    int m_interactionArg = -1;                          // +0x6C (self-healing: the medkit's uid)
    bool m_spectating = false;                          // +0x70 (player and remote controllers)
    float *m_axes = nullptr;                            // +0x74 E_ControlAxis (CONTROL_COUNT)
    float *m_buttons = nullptr;                         // +0x78 E_ControlButton (CONTROL_COUNT)
    C_NetObject *m_netObject = nullptr;                 // +0x7C network games: the human's object
    std::vector<S_ControllerMessage *> m_toServer;      // +0x80
    std::vector<S_ControllerMessage *> m_toClients;     // +0x90
    C_Checkpoints *m_checkpoints = nullptr;             // +0xA0
    S_Checkpoint *m_currentCheckpoint = nullptr;        // +0xA4 the walking network's point
    C_Item *m_handsItem = nullptr;                      // +0xA8
    bool m_unknownAC = false;                           // +0xAC
    bool m_weaponRaised = false;                        // +0xAD
    int m_quietTime = 0;                                // +0xB0
    bool m_unknownB4 = false;                           // +0xB4
    bool m_alarmRaised = false;                         // +0xB5 an enemy raised the alarm when it took control
    int m_speechDelay = 0;                              // +0xB8 ms until the next Say()
    int m_unknownBC = 0;                                // +0xBC
};

// The local player's controller (vtable 0x00811848, 0x560 bytes; player_controller.cpp,
// player_weapons.cpp, player_net.cpp), also the server's copy of a network player. The keys and
// the mouse become the human's moves and view (HandleInput), the weapon's use (aiming, firing
// with spread and recoil, reloading, grenades thrown with a charge, the knife), the binoculars
// and the scopes, holding the breath, and the squad's commands (spoken, then carried out after
// the sentence). In network games it runs the human's replication: a client sends its controls
// and requests to the server, whose copy replays the state and carries the requests out (the
// net object's messages and the controller messages).
class C_PlayerController : public C_HumanController {
public:
    // The view modes (GetViewMode).
    enum : int {
        VIEW_NORMAL = 0,
        VIEW_BINOCULARS = 1,                            // the belt's binoculars (ITEM_BEHAVIOUR_BINOCULARS)
        VIEW_IRON_SIGHTS = 2,                           // a weapon's scope of aim mode 5 (C_ScopeComp)
        VIEW_SCOPE = 3,                                 // the other aim modes (the reticles)
    };

    C_PlayerController();                               // inline in C_Human::CreateController
    ~C_PlayerController() override = default;           // 0x005a4260 (deleting; the body is the base's 0x005832a0)
    // Body actions for the server (network games; a message of the human's net object).
    void RequestAction(int action, const S_vector4 *pos, const S_vector4 *dir, float strength, int ammoUid,
                       int rounds) override;            // 0x00542920
    // Uses the medkit on the human (Heal; network games: the clients are told, and the medic
    // scores).
    bool UseMedkit(int humanId, int itemUid) override;  // 0x00546dc0
    void Attach(C_Human *human, C_HumanController *previous) override;   // 0x0053b9a0
    // (Also forgets the human.)
    void Detach() override;                             // 0x0053bc50
    void Update(int frameTime) override;                // 0x00540090
    void Tick(int timeDelta) override;                  // 0x00540240
    void SaveGame(C_chunk &chunk) override;             // 0x005461f0
    void LoadGame(C_chunk &chunk) override;             // 0x005464b0
    // Within sight range (the target's visibility and the camera's view distance) and inside
    // a 120-degree triangle ahead of the body.
    void TestSightOfHuman(C_Human *target, int *status, float *visibility) override;    // 0x00542320
    void SetSpectating(bool on) override { m_spectating = on; }   // 0x005a4230
    bool IsSpectating() override { return m_spectating; }         // 0x005a4240
    // The requests go to the server (CTRL_MSG_*).
    void OnUniformChanged(int uid) override;            // 0x0053e0b0
    void RequestPickUpItem(int actorId, int container) override;   // 0x0053e440
    void RequestThrowItem(int uid, const S_vector &velocity) override;   // 0x0053e9d0
    bool RequestMoveItem(int uid, int otherId, int fromContainer, int toContainer, int slot) override;   // 0x0053e610
    bool RequestTransferItem(int uid, int arg, int otherId, int otherUid, int container) override;      // 0x0053e7f0
    void RequestTakeFromBox(int boxId, int index) override;   // 0x0053e270
    void OnDeath() override;                            // 0x0053bd00
    void OnKilled() override;                           // 0x0053be10
    // The soldier comments (single player).
    void OnKilledEnemy(C_Human *victim) override;       // 0x00542300
    void OnEnemySeen(C_Human *human) override;          // 0x005421c0
    void OnEnemyLost(C_Human *human) override;          // 0x005422b0
    // Network clients: the HUD's indicator of the sound's direction.
    void OnSoundHeard(S_RememberedSound *sound) override;   // 0x005449b0
    void OnHit(int type, int zone, int health, C_Actor *attacker, const S_vector &pos,
               const S_vector &dir) override;           // 0x0053db90
    // The server's messages about the human (a client) / a client's (the server).
    void OnMessage(C_BitStream *msg) override;          // 0x005406c0
    void OnMessage2(C_BitStream *msg) override;         // 0x00540db0
    // A ladder touched (network clients: the server is asked to use it).
    bool OnCollisionEx(C_Actor *other, const S_vector *pos, int a3, int a4, int a5, int a6,
                       int a7) override;                // 0x00544b40
    // The command goes to the server (CTRL_MSG_COMMAND).
    void IssueCommand(const S_HumanCommand &command) override;   // 0x0053d340
    // By the stance, the pace and the shooting skill (spread and recoil).
    float GetAimBonus() override;                       // 0x00542b10
    void FlushMessages(bool toClients) override;        // 0x0053f370
    bool Unknown104() override { return false; }        // 0x005a4250

    // Reads and drops the messages of a human that does not exist (yet) on this side.
    static void HandleMessage(C_BitStream *msg);        // 0x005418e0 __fastcall
    static void HandleMessage2(C_BitStream *msg);       // 0x00541cb0 __fastcall
    // The player's keys and mouse (C_game::HandleInput, through C_Human::PlayerHandleInput):
    // the view, the HUD switch, the weapon keys latched for the tick, the pace.
    void HandleInput(float *held, float *pressed);      // 0x0053bf90
    // An action command of the HUD (a squad command) for the server.
    void SendActionCommand(const S_ActionCommand &command);   // 0x0053d910
    // Co-op: asks the server for the equipment of human `humanId` (its inventory screen).
    void RequestEquipment(int humanId);                 // 0x0053f1b0
    // The human took damage (the local player's view kicks; the hit's zone is not used).
    void OnDamaged(int type, int zone, const S_vector &dir);   // 0x00545dd0
    // VIEW_*: the binoculars and the scopes (their HUD and animations).
    void SetViewMode(int mode);                         // 0x005469e0
    // A hit for the server to apply (fall damage): C_Human::Hit(type, damage, 0, 0, the actor
    // `attackerId`, zone).
    void RequestDamage(int type, int damage, int attackerId, int zone);   // 0x00547270
    // Multiplayer: tells the clients the soldier's class (the net game).
    void SendSoldierClass(int soldierClass);            // 0x005470b0
    bool IsHoldingBreath() const { return m_holdingBreath; }   // byte +0x4D4
    bool GetOrdersChanged() const { return m_ordersChanged; }   // byte +0x525
    int GetViewMode() const { return m_viewMode; }      // +0x52C
    // Objectives games: the VIP (S_SoldierState::vip).
    bool IsVip() const { return m_vip; }                // byte +0x550
    void SetVip(bool vip) { m_vip = vip; }
    // What a squad member taking over goes on with (C_SquadController::Attach): the squad's
    // flags (C_SquadController::FLAG_*) and combat order of the soldier.
    uint32_t GetSquadFlags() const { return m_squadFlags; }       // +0x528
    int GetSquadCombatOrder() const { return m_squadCombatOrder; }   // +0x548
    // (The command module's combat orders to the squad.)
    void SetSquadCombatOrder(int order) { m_squadCombatOrder = order; }

private:
    // --- player_controller.cpp ---------------------------------------------------------------
    // Holds the breath (the scope steadies) or lets it go; 3.5 s at most.
    void HoldBreath(bool on);                           // 0x0053cd90
    // Spoken squad commands: whispered or not (the command module), the pending command
    // carried out after the sentence, its body gesture.
    static void SetWhisperingFor(int command);          // 0x0053d520
    void ExecutePendingCommand();                       // 0x0053d5b0 (__fastcall)
    bool OnSquadCommand(const S_MsgAction &message);    // 0x0053d5d0
    void PlayCommandGesture(const S_MsgAction &message);   // 0x0053d870
    // Commands 0x21 / 0x22: a target marked along `dir` for the air or artillery support.
    bool DesignateTarget(int command, const S_vector &origin, const S_vector &dir);   // 0x005467c0
    // Says `sentence` (single player: whispered while the squad whispers).
    void SayNear(int sentence, int delay);              // 0x00542060
    // --- player_weapons.cpp ------------------------------------------------------------------
    void UpdateWeaponReady(int timeDelta);              // 0x00545b90
    void UpdateZoom();                                  // 0x005457e0
    void WeaponControls(int timeDelta);                 // 0x005433b0
    // The throw being charged: released, it goes (true: the charge is going on or ended).
    bool UpdateCharge(int timeDelta);                   // 0x00542e20
    void MeleeStrike();                                 // 0x00542b80 (__fastcall)
    void ApplySpread(S_vector &dir);                    // 0x00545140
    void ApplyRecoil(S_vector4 &view);                  // 0x005454d0
    // --- player_net.cpp ----------------------------------------------------------------------
    // The server's copy of a client's human: the replicated state replayed.
    void UpdateRemote(int frameTime);                   // 0x0053f520
    // The local human: the controls applied, the state replicated.
    void TickLocal(int timeDelta);                      // 0x0053f9b0
    // The net object's messages: the actions (the server carries them out and passes them
    // on), the dive, the binoculars.
    void ProcessNetMessages();                          // 0x005404f0
    bool ServerAction(const C_NetActionMessage &message);   // 0x005441e0

    bool m_whispering = false;                          // +0xC0 the squad whispers (the command module)
    float m_controlAxes[CONTROL_COUNT] = {};            // +0xC4 the controls of the tick (copied / replicated)
    float m_controlButtons[CONTROL_COUNT] = {};         // +0x1C4
    float m_unknown2C4[CONTROL_COUNT] = {};             // +0x2C4 (only cleared)
    float m_unknown3C4[CONTROL_COUNT] = {};             // +0x3C4 (only cleared)
    // The weapon keys pressed since the last tick (buttons 0x10, 6, 7 and 0xD).
    int m_latchThrow = 0;                               // +0x4C4
    int m_latchFire = 0;                                // +0x4C8
    int m_latchFire2 = 0;                               // +0x4CC
    int m_latchReload = 0;                              // +0x4D0
    bool m_holdingBreath = false;                       // +0x4D4
    int m_breath = 3500;                                // +0x4D8 ms
    S_ActionCommand m_pendingCommand;                   // +0x4E0 (0x40 bytes saved)
    int m_commandDelay = 0;                             // +0x520 ms until the spoken command is carried out
    bool m_unknown524 = false;                          // +0x524 (only saved and loaded)
    bool m_ordersChanged = true;                        // +0x525 cleared at the next tick
    uint32_t m_squadFlags = 0;                          // +0x528
    int m_viewMode = -1;                                // +0x52C VIEW_*
    int m_scopeDelay = 0;                               // +0x530 ms (a reload in the scope)
    int m_raisedTime = 0;                               // +0x534 ms the weapon stays raised after use
    bool m_charging = false;                            // +0x538 a throw being charged
    int m_chargeTime = 0;                               // +0x53C ms (1 s at most)
    int m_throwDelay = 0;                               // +0x540 ms after a throw
    int m_chargeAction = 0;                             // +0x544 the throw's body action (6, 7, 0xD)
    int m_squadCombatOrder = 0xC;                       // +0x548
    bool m_vip = false;                                 // +0x550
};

// What C_AIController::UpdateMove returns (S_AIMoveOrder::unknown4C; names inferred).
enum E_MoveResult {
    MOVE_RESULT_DOOR_ALARM = -1,                        // a closed door while the alert state is 2
    MOVE_RESULT_MAKING_WAY = -2,                        // stepping off a point a car stands on
    MOVE_RESULT_WAITING = -3,                           // waiting for a car to leave the route
    MOVE_RESULT_WAITED = -4,                            // ... in vain
    MOVE_RESULT_STUCK = -5,                             // 30 steps without getting nearer the waypoint
    MOVE_RESULT_LADDER_BUSY = -7,                       // someone else on the ladder, or the alert state is 2
    MOVE_RESULT_NO_END_POINT = -8,                      // no walking point at the destination
    MOVE_RESULT_NO_START_POINT = -9,                    // no walking point where the human stands
    MOVE_RESULT_NO_PATH = -10,
    MOVE_RESULT_DOOR_LOCKED = -11,                      // (the squad)
    MOVE_RESULT_MOVING = 1,
    MOVE_RESULT_STAYING = 2,                            // S_AIMoveOrder::unknown58
    MOVE_RESULT_ENDED = 4,                              // the route ran out (arrived) or is replaced
};

// Base of the AI controllers (vtable 0x00810e20, 70 slots; 0x140 bytes). The derived
// controllers decide; this carries it out: a move order along a route of waypoints over the
// walking network (doors, ladders, climbs, the cars standing on it), a fire order, where the
// human looks and how alert it is, a car to drive along the roads. It also senses for them
// (the sight tests, sounds, hits) and runs the AI human's network side. Original source
// 0x0056ee90..0x00582000: ai_controller.cpp, ai_senses.cpp, ai_combat.cpp, ai_movement.cpp.
class C_AIController : public C_HumanController {
public:
    explicit C_AIController(E_HumanController type);   // 0x0056ee90
    // (Fix: also puts the waypoints and the task back into their pools.)
    ~C_AIController() override;                         // 0x0056f0e0 (0x0056f0c0)
    void Attach(C_Human *human, C_HumanController *previous) override;   // 0x0056f200
    void Detach() override;                             // 0x0056f630
    void SaveGame(C_chunk &chunk) override;             // 0x00576fe0
    void LoadGame(C_chunk &chunk) override;             // 0x005774a0

    // --- slots of the base -------------------------------------------------------------
    bool AdjustAim(S_vector *dir) override;             // 0x005722b0
    void RequestAction(int action, const S_vector4 *pos, const S_vector4 *dir, float strength,
                       int ammoUid, int rounds) override;   // 0x005785b0
    void TestSightOfMeleeTracker(C_Actor *meleeTracker, int *status, float *visibility) override;         // 0x0056fc80
    void TestSightOfHuman(C_Human *target, int *status, float *visibility) override;    // 0x00570280
    const S_vector &GetFacingDir() override;            // 0x0056fbf0
    const S_vector &GetViewDir() override;              // 0x0056fc00
    void SetFacingDir(const S_vector4 &dir) override;   // 0x0056fc10
    void SetViewDir(const S_vector4 &dir) override;     // 0x0056fc20
    bool AdjustToPathHeight(S_vector *pos) override;    // 0x0057fbc0
    // The light values of the walking points around the human (S_Checkpoint::GetValuePair;
    // set 0 lying, 1 standing or moving), interpolated along the segment walked.
    bool Unknown5C(float *intensity, float *reference) override;   // 0x0057f880
    void OnBodyFlags(uint32_t flags) override;          // 0x005783d0
    void OnKilledEnemy(C_Human *victim) override;       // 0x00573fb0
    void OnEnemySeen(C_Human *human) override;          // 0x00574070
    void OnEnemyLost(C_Human *human) override;          // 0x00574130
    void OnBodySeen(C_Human *human) override;           // 0x005741f0
    void OnBodyHeard(C_Actor *source) override;         // 0x00574280
    void OnFriendHeard(C_Actor *source) override;       // 0x00574310
    void OnLoudSound(C_Actor *source) override;         // 0x00574480
    void OnSoundHeard(S_RememberedSound *sound) override;   // 0x00574d70
    void OnShotAt(C_Actor *shooter, const S_vector &velocity) override;   // 0x00574cb0
    void OnHit(int type, int zone, int health, C_Actor *attacker, const S_vector &pos,
               const S_vector &dir) override;           // 0x00574560
    void OnMessage(C_BitStream *msg) override;          // 0x00577ce0 (clients: the server's)
    void OnMessage2(C_BitStream *msg) override;         // 0x00578190 (the server: a client's)
    bool OnCollisionEx(C_Actor *other, const S_vector *pos, int a3, int a4, int a5, int a6,
                       int a7) override;                // 0x005765f0
    void SetMoveFlags(uint32_t flags) override;         // 0x0056fc30
    void IssueCommand(const S_HumanCommand &command) override;   // 0x00571c10
    void Freeze(bool on) override;                      // 0x005786c0
    void FlushMessages(bool toClients) override;        // 0x00577b30
    // The fire order's target (S_AIFire::orderTarget).
    C_Actor *GetLastTarget() override { return m_fire.orderTarget; }   // 0x00524dd0
    std::vector<S_AIWaypoint *> *GetWaypoints() override { return &m_waypoints; }   // 0x00524dc0
    bool Unknown108() override;                         // 0x00524de0

    // --- slots of its own --------------------------------------------------------------
    // Puts the task back into its pool.
    virtual void ReleaseTask();                         // 0x10C 0x00575820
    // Whether to throw a grenade (the enemy: and where, S_AIFire::throwPos). Base: a grenade
    // on the belt, the human neither firing nor lying.
    virtual bool PlanGrenade();                         // 0x110 0x00573210
    // A test along direction `dir` (the squad and the enemy normalise it and cast 200 m
    // ahead; the other AIs: true).
    virtual bool Unknown114(const S_vector *dir) = 0;   // 0x114

    // --- called by the scripts and the other controllers --------------------------------
    void SetAlertState(int state);                      // 0x0056f1e0
    // Looks around (script HUMAN_LookAround), or toward `pos`.
    void LookAround(int type);                          // 0x00570a10
    void LookAroundAt(int type, const S_vector4 &pos);  // 0x00570980
    // Drives the car the human is in to road point `roadCheckpoint` (CHECKPOINT_NONE: stop)
    // at `speed` m/s; the script continues at `label` on arrival.
    void Drive(int roadCheckpoint, float speed, int label);   // 0x00577a60
    void SetIgnoreCircumvention(bool on) { m_ignoreCircumvention = on; }   // byte +0x11C
    // The walking point the human is at and the next waypoint's (CHECKPOINT_NONE: none).
    void GetPathPoints(uint16_t *current, uint16_t *next);   // 0x0057a390
    C_Actor *GetFireTarget() const { return m_fire.target; }   // +0xD0
    // The human is within `radius` of `pos`.
    bool IsHumanWithin(const S_vector *pos, float radius);   // 0x0057fe40
    // The distance to the fire target (not its square, whatever the original's name says).
    float GetTargetDistance2() const;                   // 0x00575350 (DS 0x00577e50)
    // How far the facing direction points at `target`.
    float GetViewDotTo(const C_Actor *target) const;    // 0x00575410 (DS 0x00577f40)
    // The muzzle of the hands' weapon, the item's own frame for anything else, zero without one.
    void GetWeaponPos(S_vector4 *pos) const;            // 0x005715f0 (DS 0x005730c0)
    // Within 3 m of the player's selected soldier.
    bool IsNearSelectedSoldier() const;                 // 0x0057ee30 (DS 0x00583de0)
    void ClearRoute();                                  // 0x0057f7d0 (DS 0x00584a10)
    // Moves the first waypoint here once the human has passed it towards the second.
    void SkipPassedWaypoint();                          // 0x0057b1b0 (DS 0x0057f1d0)

protected:
    // --- senses (ai_senses.cpp) ----------------------------------------------------------
    C_Human *PickTargetFromSeat();                      // 0x00570ca0
    C_Human *PickTarget(const S_vector *dir);           // 0x005710b0
    float GetDistanceTo(const C_Actor *actor) const;    // 0x00573240
    float GetNearestEnemyDistance() const;              // 0x005733e0
    S_vector4 GetAimDirTo(const C_Actor *target) const; // 0x00575450
    S_vector4 GetDirToFrame(const I3D_frame *frame) const;   // 0x00575670
    void TurnToSound(const S_RememberedSound *sound);   // 0x00573900
    void TurnToSound2(const S_RememberedSound *sound);  // 0x00573c50
    void TurnToward(const S_RememberedSound *sound, bool unlessLooking, int pause);

    // --- combat (ai_combat.cpp) ----------------------------------------------------------
    float GetAimSpread();                               // 0x00572120
    void ThrowGrenade();                                // 0x00572de0
    void UpdateTimers(int timeDelta);                   // 0x00571680
    bool ExtendBurst(uint32_t time);                    // 0x00571a10
    // Turns the body toward `to` (on a ladder the view too): S_AIFacing::SetFacing's result.
    int TurnTowards(const S_vector4 &from, const S_vector4 &to);   // 0x00571730
    int ApplyAlertState();                              // 0x00575850 (its int argument is not used)
    int UpdateLook(int timeDelta);                      // 0x00575940
    bool StartFiring();                                 // 0x00571a40
    void StopFiring(uint32_t pause);                    // 0x00572c20
    void PressReload();                                 // 0x00572c00
    void UpdateFiring(int timeDelta);                   // 0x00572af0
    uint32_t CheckFireOrder();                          // 0x00575d50
    uint32_t CheckReload();                             // 0x005762b0
    uint32_t UpdateFireOrder(int timeDelta);            // 0x00576350
    void UpdateCheckpointStance();                      // 0x00576610
    void SetStance(int stance);                         // 0x005767f0

    // --- network (ai_controller.cpp) -----------------------------------------------------
    void UpdateNetwork(int frameTime);                  // 0x0056f710

    // --- driving (ai_movement.cpp) -------------------------------------------------------
    // Drives the car along the driver's path: 1 when it arrived (or stopped), else 0.
    int UpdateDriving(int timeDelta);                   // 0x00578770 (0x00578790)
    int OnDriveArrived(C_Car *car);                     // 0x005796d0
    // Throttle, brake and steering toward `aim` (the curve's point, `tangent` there) from
    // `ahead` (where the car will be) facing `carDir`; `last`: slowing down for `end`. In
    // reverse the brake drives backward.
    void SteerForward(C_Car *car, const S_vector &ahead, const S_vector &carDir, const S_vector &aim,
                      const S_vector &tangent, bool last, const S_vector &end);   // 0x00579760
    void SteerReverse(C_Car *car, const S_vector &ahead, const S_vector &carDir, const S_vector &aim,
                      const S_vector &tangent, bool last, const S_vector &end);   // 0x00579d50
    // The speed to drive at: the script's, or the car's +0x780 times the pace's factor.
    float GetDriveSpeed(const C_Car *car) const;        // (inline in both)

    // --- walking (ai_movement.cpp) -------------------------------------------------------
    // Carries out the move order for `timeDelta` ms (E_MoveResult).
    int UpdateMove(int timeDelta);                      // 0x0057a750
    // The destination of the move order needs a new route (false: arrived, or the route
    // still leads there).
    bool NeedsNewRoute();                               // 0x0057a420
    // The cars standing on walking points: steps off one, waits for one to leave the route
    // (false: the move ended).
    bool CheckCarsOnRoute();                            // 0x0057a610
    bool MakeWay();                                     // 0x0057b270
    // Makes the route to the destination (false: none; the result says why).
    bool PlanRoute();                                   // 0x0057bd00
    bool RouteToPos(const S_vector4 &from, const S_vector4 &to);   // 0x0057aaf0
    bool RouteToCheckpoint(const S_vector4 &from, uint16_t checkpoint);   // 0x0057b2e0
    bool RouteAwayFromEnemies(const S_vector4 &from);   // 0x0057b740
    // The walking point a route from `from` starts at (on a ladder: the end it climbs to).
    int GetRouteStart(const S_vector4 &from) const;
    // The points the path search avoids (enemies: CHECKPOINT_FLAG_NO_STOP).
    uint32_t GetRouteExcludeFlags() const;
    // Fills the route with the points of `path`.
    void AddPathWaypoints(const std::vector<uint16_t> &path);
    // On a ladder, a route back over it starts at the other end.
    void TrimLadderStart();
    // Moves the waypoints within their radius onto the line between their neighbours, then
    // fixes the heights of the type 15 runs.
    void SmoothRoute(const S_vector4 &from, const S_vector4 &to);   // 0x0057bdc0
    void SmoothRouteTo(const S_vector4 &from);          // 0x0057c2b0
    void FixRouteHeights(const S_vector4 &from, const S_vector4 &to);   // 0x0057c760
    bool StartRoute();                                  // 0x0057ca70
    // One step along the route (a door, a ladder, type 15 points or the walking points).
    void FollowStep(int timeDelta);                     // 0x0057f670
    void UseLadder(int timeDelta);                      // 0x0057cc50
    int PassDoor(int timeDelta);                        // 0x0057cf80
    // The human reached the first waypoint (false also while getting nearer).
    bool ReachedWaypoint();                             // 0x0057d5a0
    void FollowType15Route(int timeDelta);              // 0x0057da40
    void FollowRoute(int timeDelta);                    // 0x0057dbd0
    // Presses the climb command (network games: once a second at most).
    void Climb();                                       // 0x0057db70
    // Steers the walk toward the first waypoint: the facing, the walking axes, the pace.
    void WalkToWaypoint();                              // 0x0057e030
    // The axes and the facing of walking along `dir` (normalized, xz) in `mode` (1 forward or
    // back, 2 sideways, 3 diagonally); `dot` / `left`: dir against the view.
    void SetWalkAxes(int mode, const S_vector &dir, float dot, bool left);
    // The pace the move order asks for, held back where the human must walk.
    void SetWalkPace(bool lying);
    // Stand up near the special points and the others (the movement flags put aside).
    void UpdateMoveStance();                            // 0x0057ec60
    bool IsOnPlainPoints();                             // 0x0057edc0
    bool IsAwayFromOthers();                            // 0x0057ee00
    // Stepping aside (C_HumanAIMemory::unknown00) while no route is followed.
    void UpdateAvoidance();                             // 0x0057f610
    bool HasAvoidance();                                // 0x0057eec0
    bool CanAvoid();                                    // 0x0057ef00
    void WalkAvoiding();                                // 0x0057ef30
    // The move ends with `result` (E_MoveResult): the route forgotten.
    void EndMove(int result);                           // 0x0057f6f0
    // The pace may not be raised: a point that asks for it, a ladder or a door next.
    bool MustSlowDown();                                // 0x0057fda0
    // The marching pace is not allowed (only relaxed, standing, unarmed, straight ahead).
    bool IsMarchBlocked();                              // 0x0057fd30
    // The walking point the human is at (m_currentCheckpoint), as it walks on.
    void UpdateCurrentCheckpoint();                     // 0x0057ff10
    // The human is within the radius of the segment between the two waypoints (interpolated).
    bool IsNearSegment(const S_AIWaypoint &a, const S_AIWaypoint &b);   // 0x005802a0
    S_Checkpoint *NearerPoint(const S_AIWaypoint &a, const S_AIWaypoint &b);   // 0x00580460
    // Steps round the humans in the way on type 15 points (detour waypoints).
    void AvoidOthersOnRoute(bool reached);              // 0x00581e30
    void InsertDetours(std::vector<C_Human *> &blockers);   // 0x00581b00 (also given the route, always m_waypoints)

    S_AIFire m_fire;                                    // +0xC0
    S_AITask *m_task = nullptr;                         // +0xF0
    // The route: the first is the waypoint walked to.
    std::vector<S_AIWaypoint *> m_waypoints;            // +0xF4
    S_AIWaypoint *m_waypoint = nullptr;                 // +0x104 the waypoint passed last
    int m_lookAroundTime = 0;                           // +0x108 ms
    int m_unknown10C = 0;                               // +0x10C ms since the route started
    int m_unknown110 = 0;                               // +0x110 ms (TurnToward's pause)
    int m_unknown114 = -1;                              // +0x114
    int m_unknown118 = -1;                              // +0x118
    bool m_ignoreCircumvention = false;                 // +0x11C (saved as an empty chunk)
    int m_unknown120 = 0;                               // +0x120 ms until the next climb (network games)
    S_AIFacing *m_facing = nullptr;                     // +0x124
    S_AIFireOrder *m_fireOrder = nullptr;               // +0x128
    S_AIMoveOrder *m_move = nullptr;                    // +0x12C
    C_AIDriver *m_driver = nullptr;                     // +0x130
    S_AIAlertState *m_alert = nullptr;                  // +0x134
};

// `controller` as an AI controller, null for the player's and the remote ones.
inline C_AIController *AsAIController(C_HumanController *controller)
{
    switch (controller ? controller->GetType() : HUMAN_CONTROLLER_NONE) {
    case HUMAN_CONTROLLER_SQUAD:
    case HUMAN_CONTROLLER_ENEMY:
    case HUMAN_CONTROLLER_AI:
    case HUMAN_CONTROLLER_NEUTRAL:
    case HUMAN_CONTROLLER_SNIPER:
        return static_cast<C_AIController *>(controller);
    default:
        return nullptr;
    }
}

// The action a squad member carries out (C_SquadController +0x1E0, 0x48 bytes; the original's
// _S_CURRENTACTIONDESC): walk to `pos` (or the target), turn, then the command of `type`. The
// squad commands of the command module (C_SquadCommander) and the order queue set it.
struct S_SquadAction {
    // Starts action `type` on `target` at `pos` facing `dir` (0x00534340): the state is reset;
    // `keepGoing`: an attack / follow action goes on after the first command.
    void Set(C_Actor *target, int type, const S_vector4 &pos, const S_vector4 &dir, bool keepGoing, float radius);
    void SaveGame(C_chunk &chunk) const;                // 0x00539690
    void LoadGame(C_chunk &chunk);                      // 0x0053a470

    S_vector4 pos = {};                                 // +0x00 where to go
    S_vector4 dir = {};                                 // +0x10 the direction to face there
    bool arrived = false;                               // +0x20 within `radius` of it
    bool turned = false;                                // +0x21 facing the target / dir
    bool finished = true;                               // +0x22 no action going on
    bool keepGoing = false;                             // +0x23 the attacks and the follow go on
    bool noCommand = false;                             // +0x24 only go there (no command at the end)
    bool started = false;                               // +0x25 the walk began
    bool completed = true;                              // +0x26
    bool skipTurn = false;                              // +0x27 the command without turning first
    bool notifyModule = false;                          // +0x28 (with noCommand: the command module's 0x00456f00)
    bool useDir = false;                                // +0x29 face `dir` (else the target / pos)
    int targetId = -1;                                  // +0x2C the target's network id
    int slot = 0;                                       // +0x30 the seat / the box's item
    int delay = 0;                                      // +0x34 ms before the next step
    uint32_t stance = 0;                                // +0x38 the stance at the end (MOVE_STANCE_MASK, 0 none)
    float radius = 0.0f;                                // +0x3C m
    int type = 0;                                       // +0x40 E_SquadAction
    C_Actor *target = nullptr;                          // +0x44
};

// S_SquadAction::type: the commands (E_HumanCommandType) and the AI's own (names partly
// inferred).
enum E_SquadAction {
    SQUAD_ACTION_ATTACK_VEHICLE = 0x16,                 // fire at a car / a static weapon (in a seat: its gun)
    SQUAD_ACTION_SEARCH_BODY = 0x17,                    // take the ammo and weapons of a dead human
    SQUAD_ACTION_SWAP_UNIFORM = 0x18,                   // put on an unconscious human's uniform
    SQUAD_ACTION_ATTACK = 0x19,                         // fire at a target
    SQUAD_ACTION_ATTACK_SILENTLY = 0x1A,                // ... with a silenced weapon
    SQUAD_ACTION_FOLLOW = 0x20,                         // follow the leader (the target)
    SQUAD_ACTION_21 = 0x21,                             // take the target's items (behaviours 0x14 / 0x16 / 0x15)
    SQUAD_ACTION_THROW = 0x25,                          // throw a grenade at `pos`
    SQUAD_ACTION_BOUND = 0x28,                          // hops from cover to cover along a direction
    SQUAD_ACTION_MOVE_TO = 0x29,                        // go to a point of the map
    SQUAD_ACTION_MOVE_NEAR = 0x2A,
    SQUAD_ACTION_ORDER_MOVE = 0x34,                     // the order queue's walks (also firing a launcher)
};

// Single player squad members (vtable 0x008109f8, 83 slots; 0x260 bytes, 16-byte aligned; the
// original's C_PlayerAiBrain, by its debug strings): the player's soldiers the player does not
// control. They carry out the squad commands of the command module (follow, hold, bound, move,
// use an actor, combat orders) and a queue of orders from the map (S_HumanOrder,
// game/squad_order.h), fight on their own, pick their weapons, take cover, step aside and
// follow the leader's formation. Original source 0x00524c60..0x0053b2c0: squad_controller.cpp,
// squad_actions.cpp, squad_combat.cpp, squad_movement.cpp.
class C_SquadController : public C_AIController {
    friend class C_SquadCommander;                      // the commands, the manoeuvres, the groups
public:
    static C_SquadController *Create();                 // 0x00524c60 __cdecl
    // (Fix: also puts the orders back into their pool.)
    ~C_SquadController() override;                      // 0x00524e30 (0x00524e10)

    // --- slots of the base -------------------------------------------------------------
    void ControlVehicle() override;                     // 0x00531930
    void EndInteraction(bool completed) override;       // 0x005324a0
    void Attach(C_Human *human, C_HumanController *previous) override;   // 0x00524e70
    void Detach() override;                             // 0x00525110
    void Update(int frameTime) override;                // 0x00525da0
    void Tick(int timeDelta) override;                  // 0x00526550
    void SaveGame(C_chunk &chunk) override;             // 0x00538cb0
    void LoadGame(C_chunk &chunk) override;             // 0x00539990
    void OnUniformChanged(int uid) override;            // 0x0052f6f0
    // (The base's OnDeath twice, the orders cancelled between.)
    void OnDeath() override;                            // 0x00525130
    void OnEnemySeen(C_Human *human) override;          // 0x0052e5b0
    void OnEnemyLost(C_Human *human) override;          // 0x0052e6f0
    void OnBodySeen(C_Human *human) override;           // 0x0052e6d0
    void OnLoudSound(C_Actor *source) override;         // 0x0052e750
    void OnShotAt(C_Actor *shooter, const S_vector &velocity) override;   // 0x0052e780
    void OnHit(int type, int zone, int health, C_Actor *attacker, const S_vector &pos,
               const S_vector &dir) override;           // 0x0052eaf0
    // The base controller's: the squad's commands are not sent to the clients.
    void IssueCommand(const S_HumanCommand &command) override { C_HumanController::IssueCommand(command); }   // 0x005263f0
    S_HumanEvent *HandleEvent(S_HumanEvent *event) override;   // 0x00528580
    // Not while holding (flag 1); else the base's.
    bool Unknown104() override;                         // 0x00532880
    // Nobody of the team in the line of fire along `dir` (200 m).
    bool Unknown114(const S_vector *dir) override;      // 0x114 0x0052f1f0

    // --- slots of its own --------------------------------------------------------------
    // FLAG_* (what a player taking the member over goes on with).
    virtual uint32_t GetFlags() { return m_flags; }     // 0x118 0x00524e00
    virtual void AddOrder(S_HumanOrder *order);         // 0x11C 0x0052ed00
    virtual void InsertOrder(S_HumanOrder *order, int index);   // 0x120 0x0052eb20
    virtual void SetOrder(int index, S_HumanOrder *order);      // 0x124 0x0052ee60
    // Drops every order (the last one taken off turns the member back when `turn`) and ends
    // the action.
    virtual void CancelOrders(bool turn);               // 0x128 0x0052ef20
    virtual void RemoveOrder(int index, bool turn);     // 0x12C 0x0052efa0
    // The queue changed during the current game tick.
    virtual bool HaveOrdersChanged();                   // 0x130 0x0052f110
    virtual int GetOrderCount();                        // 0x134 0x0052f130
    virtual S_HumanOrder *GetOrder(int index);          // 0x138 0x0052f150
    // The icons of the member's panel: what it does (0..19, the temporary ones 16..19 for
    // 2 s) and its mode (0..39, 12..39 for 2 s).
    virtual int GetStatusIcon();                        // 0x13C 0x00538590
    virtual int GetModeIcon();                          // 0x140 0x005385b0
    // Sneaking and whispering, or a silent combat order (the _ALT ones).
    virtual bool IsQuiet() { return m_silentMode || m_quiet; }   // 0x144 0x005385d0
    // The current combat order (E_SquadCombatOrder).
    virtual int GetCombatOrder();                       // 0x148 0x005385f0

    // --- the command module's squad commands (squad_actions.cpp) -------------------------
    void SetCombatOrder(int order);                     // 0x005380c0
    // Follows the leader (`follow`), else stays; `quiet`: sneaking and whispering.
    void SetFollow(bool follow, bool quiet);            // 0x005377f0
    // Stays where it is in cover (`notify`: tells the module when idle).
    void HoldPosition(bool quiet, bool notify);         // 0x00537e50
    // Fires at what it sees around `pos` for a while (`on`), or stops.
    void Engage(bool on, const S_vector4 &pos);         // 0x005382d0
    // The orders wait (`on`) or go on.
    void HoldOrders(bool on);                           // 0x00538490
    // Follows the selected member (`fromOrders`: the order queue's kind 4; the orders go).
    void FollowSelected(bool fromOrders);               // 0x005357e0
    // The action ends (its command `type` done): what the member does next.
    void FinishAction(int type);                        // 0x00535c00
    // Drops the action going on.
    void DropAction();                                  // 0x00535730 (__fastcall)
    // Says `sentence` (its whispered form, the next one, with `alt` when quiet) after `delay` ms.
    void SayReply(int sentence, bool alt, int delay, bool force);   // 0x005383d0
    // Marks the first order done if it is of kind 6 / 7.
    void EndOrderKind6();                               // 0x00538430 (__fastcall)
    void EndOrderKind7();                               // 0x00538460 (__fastcall)
    void UpdateModeIcon();                              // 0x00538640
    // The actions on actors.
    void Act21(C_Actor *target);                        // 0x005343e0
    void DeliverDocuments(C_Actor *place);              // 0x00534400
    void Follow(C_Human *leader);                       // 0x00534460
    // Gets into `vehicle`'s seat `seat`, walking to `pos`.
    void EnterVehicle(C_Actor *vehicle, int seat, const S_vector4 &pos);   // 0x00534510
    // `item`: which of a supply box's items (kept in the action's slot).
    void PickUp(C_Actor *actor, int item);              // 0x005346c0
    void LockDoor(C_Actor *door);                       // 0x00534840
    void UnlockDoor(C_Actor *door);                     // 0x00534860
    void OpenDoor(C_Actor *door);                       // 0x00534880
    void CloseDoor(C_Actor *door);                      // 0x005348f0
    void UseActor(C_Actor *actor);                      // 0x00534910
    void DefuseMine(C_Actor *mine);                     // 0x00534a00
    void DisarmExplosive(C_Actor *place);               // 0x00534a70
    void SearchBody(C_Actor *body);                     // 0x00534af0
    void PlantExplosive(C_Actor *place);                // 0x00534b30
    void SwapUniform(C_Actor *human);                   // 0x00534bf0
    void CutWire(C_Actor *wire);                        // 0x00534c40
    void PickLock(C_Actor *door);                       // 0x00534f80
    void OrderLeaveVehicle(C_Actor *vehicle);           // 0x00534ff0
    void Attack(C_Actor *target);                       // 0x00535050
    void AttackSilently(C_Actor *target);               // 0x00535110
    void AttackVehicle(C_Actor *target);                // 0x00535250
    void TieUp(C_Actor *human);                         // 0x005353a0
    void MoveNear(const S_vector4 &pos, bool quiet);    // 0x00535440
    // An order's walk to `pos`, waiting `delay` ms there.
    void OrderMove(const S_vector4 &pos, int delay, bool notifyModule);   // 0x005354f0
    void ThrowGrenadeAt(C_Actor *target);               // 0x00535580
    void Heal(C_Actor *human);                          // 0x00535630
    void ThrowAt(const S_vector4 &pos);                 // 0x00535910
    void FireLauncherAt(const S_vector4 &pos);          // 0x00535a60
    void MoveTo(const S_vector4 &pos, const S_vector4 &dir, bool quiet, uint32_t stance);   // 0x00537a40
    void Bound(const S_vector4 &dir, bool quiet);       // 0x00537c60
    // The command module's weapon checks (0x0045bd10).
    C_Item *FindWeapon(int behaviour, bool scoped, bool silenced, int *container);   // 0x005304b0
    bool HasSeatWeapon();                               // 0x005328a0 (__fastcall)
    bool HasProjectileSeatWeapon();                     // 0x005328d0 (__fastcall)

    // A grenade was thrown from `pos` along `dir` by `thrower` (takes cover from it).
    void OnGrenadeThrown(const S_vector &pos, const S_vector &dir, C_Actor *thrower);   // 0x00530ff0

private:
    C_SquadController();                                // (inline in Create)

    // --- squad_controller.cpp ------------------------------------------------------------
    void SelfHeal(int timeDelta);                       // 0x00526400 (timeDelta unused)
    void UpdateIdle(int timeDelta);                     // 0x00536120 (timeDelta unused)
    void UpdateFollowPace();                            // 0x005376f0
    void UpdateIconTimers(int timeDelta);               // 0x00537690 / 0x005376c0
    void SetStatusIcon(int icon);                       // 0x00538510
    void SetModeIcon(int icon);                         // 0x00538550
    // Tells the command module the member is idle (no orders).
    void NotifyIfIdle();                                // 0x00538390 (__fastcall)
    // Follows the selected member of the player's team (if any).
    void FollowSelectedMember();                        // 0x0052f0e0 (__fastcall)
    void ControlSubmarine();
    void FollowSubmarine(C_Actor *other);               // 0x00531a50
    void SaveOrders(C_chunk &chunk);                    // 0x00539340
    bool LoadOrder(C_chunk &chunk);                     // 0x0053abd0
    void LoadOrders(C_chunk &chunk);                    // 0x0053b180

    // --- squad_actions.cpp -----------------------------------------------------------------
    // Starts action `type` on `actor` at its position (`notify`: NotifyIfIdle).
    void ActOn(int type, C_Actor *actor, bool keepGoing, float radius, bool notify);   // 0x00534620
    // Drops a carried body (a command) before the action.
    void DropCarriedBody();
    bool ProcessOrders(int timeDelta);                  // 0x005282e0
    void StartOrder(int timeDelta);                     // 0x00529f60
    void SetOrderTask(S_HumanOrder *order);             // 0x0052a1e0
    void SetIdleTask(S_HumanOrder *order);              // 0x0052a3b0
    // Walks the queue's last direction: a watch order (kind 2) at `pos` facing `dir`.
    // `model` gives the flags and the argument (null: the member's move flags); the original
    // reads the queue's first slot.
    void QueueWatchOrder(const S_vector4 &pos, const S_vector4 &dir, S_HumanOrder *model);   // 0x00531f60
    bool ExecuteAction(int timeDelta);                  // 0x00528950
    bool IsActionNeeded();                              // 0x005387c0 (__fastcall)
    bool IsAtTarget(C_Actor *actor);                    // 0x00535e70 (DS 0x005283d0)
    void ShowActionIcon(int type);                      // 0x005374a0
    bool SearchBodyItems(C_Human *body);                // 0x00537300
    bool TakeItems(C_Actor *from, int behaviour);       // 0x00537350
    bool LootAmmo(C_Human *body);                       // 0x00536dc0
    bool LootWeapon(C_Human *body);                     // 0x00536fb0
    bool LootItemsOfType(C_Human *body, int typeId);    // 0x00536d30
    void Equip(C_Item *item, int *slot);                // 0x00538770
    C_Item *FindWeaponFor(C_Actor *target, int *container);   // 0x00538980

    // --- squad_combat.cpp ------------------------------------------------------------------
    void UpdateCombat(int timeDelta, bool ordersDone);  // 0x0052b810
    bool VehicleCombat(int timeDelta);                  // 0x00527fd0
    void UpdateGunner(int timeDelta);                   // 0x00532ba0
    void AimSeatWeapon(C_Actor *target);                // 0x00532910
    void GetVehicleAimDir(S_vector4 *dir, C_Actor *target);   // 0x0052c510
    // A meleeTracker the team saw within `range` that is near (4 m) or in sight.
    C_Actor *FindNearMeleeTracker(float range);                  // 0x00532a20
    C_Actor *FindMeleeTrackerTarget(float range);                // 0x0052b180
    void AimAt(C_Actor *target);                        // 0x0052b580
    C_Human *PickCombatTarget(const S_vector *dir);     // 0x0052d0d0
    C_Human *FindEnemyTargeting(bool friendOnly);       // 0x0052f160
    // A plane to fire at (an air attack, else a Junkers 88 and the frame aimed at).
    C_Actor *FindAirTarget(I3D_frame **frame);          // 0x0052cea0
    void SetBurstTimes(C_Actor *target);                // 0x0052a5d0
    void GetBurstDefaults(int *stopMin, int *stopSpread, int *fireMin, int *fireSpread);   // 0x0052b080
    bool CanFireAt();                                   // 0x0052a810 (__fastcall)
    bool HasClearLine();                                // 0x0052ac20 (__fastcall)
    void Melee(C_Actor *target);                        // 0x00536a30
    bool WeaponCheckDue();                              // 0x0052f700 (__fastcall)
    void BuildWeaponPrefs(C_Actor *target, std::vector<uint32_t> &prefs);   // 0x0052f750
    C_Item *PickWeapon(const std::vector<uint32_t> &prefs, int *container);   // 0x00530930
    void ChooseWeapon(C_Actor *target);                 // 0x00530a40
    bool IsWeaponSuitable(C_Actor *target, C_Item *item);   // 0x00530b30
    bool UpdateWeaponChoice();                          // 0x00530d40
    bool ReloadFromBelt();                              // 0x005318c0 (__fastcall)
    bool FindStimuli(int type, std::vector<int> &indexes, bool filter);   // 0x00527560
    bool ReactToStimuli();                              // 0x005276e0
    void TakeHit(int type, int zone, int health, C_Actor *attacker, const S_vector &pos,
                 const S_vector &dir, float force);    // 0x0052e7c0
    void UpdateAlertPreset();                           // 0x005325c0 (__fastcall)
    void ChooseCoverFacing(S_vector4 *dir, uint32_t *stance);   // 0x00525c90
    void CoverStance(bool firing);                      // 0x00537fb0
    void AdjustStanceForThreat();                       // 0x00537b60 (__fastcall)

    // --- squad_movement.cpp ----------------------------------------------------------------
    void UpdatePace(int timeDelta, bool ordersDone);    // 0x0052d460
    void StepAside(const S_vector4 &pos, const S_vector4 &dir, bool sideways, bool frontOnly);   // 0x0052dc10
    void StepAsideFrom(C_Actor *actor);                 // 0x005356e0 (DS 0x00527a10)
    void MakeRoomForLeader();                           // 0x0052e310
    // A walking point named "#<target's frame>" to attack `target` from (the map's).
    bool FindAttackPoint(S_vector4 *pos, float *radius, C_Actor *target);   // 0x00532190
    bool AtSpecialPoint();                              // 0x005324d0 (__fastcall)
    void FormationStep(const S_vector4 &target);        // (inline in StepAside / OnGrenadeThrown)

    // The flags at +0x174 (names partly inferred).
    enum : uint32_t {
        FLAG_1 = 0x001,                                 // set by RemoveOrder / scripted; the action reroutes
        FLAG_2 = 0x002,                                 // the order's own stance (not the leader's)
        FLAG_HOLD_FIRE = 0x004,                         // combat orders 0xE / 0xF
        FLAG_THROW = 0x008,                             // a grenade throw at the action's pos
        FLAG_ENGAGED = 0x010,                           // Engage
        FLAG_HOLDING = 0x020,                           // HoldPosition
        FLAG_LAUNCHER = 0x200,                          // a launcher fired at the action's pos
    };

    S_vector4 m_engagePos = {};                         // +0x140 Engage's
    S_vector4 m_boundDir = {};                          // +0x150 Bound's
    S_vector4 m_holdDir = {};                           // +0x160 the facing kept when idle
    uint32_t m_pace = MOVE_PACE_3;                      // +0x170 the pace set last (MOVE_PACE_*)
    uint32_t m_flags = 0;                               // +0x174 FLAG_*
    uint32_t m_orderStance = 0;                         // +0x178 the stance the order asks for
    uint32_t m_coverStance = 0;                         // +0x17C
    uint32_t m_orderPace = 0;                           // +0x180 the pace the order asks for
    int m_idleCheckpoint = -1;                          // +0x184 (SetIdleTask)
    bool m_quiet = false;                               // +0x188 sneaking, whispering
    bool m_following = true;                            // +0x189
    bool m_silentMode = false;                          // +0x18A the _ALT combat orders
    bool m_lyingToFire = false;                         // +0x18B (LMGs)
    bool m_steppedAside = false;                        // +0x18C
    bool m_ordersHeld = false;                          // +0x18D HoldOrders
    bool m_restartOrder = false;                        // +0x18E the first order changed (SetOrder)
    int m_boundHops = 0;                                // +0x190
    int m_engageTime = 0;                               // +0x194 ms
    int m_eventTime = 0;                                // +0x198 ms checking an event
    int m_alertTime = 0;                                // +0x19C ms
    int m_weaponCheckTime = 0;                          // +0x1A0 ms
    int m_formationTime = 0;                            // +0x1A4 ms (UpdateIdle's)
    int m_fireMoveTime = 0;                             // +0x1A8 ms of stopping to fire (< 0) / walking on (> 0)
    int m_airCheckTime = 0;                             // +0x1AC ms until the planes are looked for
    int m_followDelay = 0;                              // +0x1B0 ms
    int m_statusIconTime = 0;                           // +0x1B4 ms
    int m_modeIconTime = 0;                             // +0x1B8 ms
    int m_ordersChangedTime = 0;                        // +0x1BC the game time + 1 of the last change
    float m_coverRadius = 0.0f;                         // +0x1C0 m
    float m_friendlyFire = 0.0f;                        // +0x1C4 the player's damage (decays)
    int m_statusIcon = 0;                               // +0x1C8
    int m_tempStatusIcon = -1;                          // +0x1CC
    int m_modeIcon = 0;                                 // +0x1D0
    int m_tempModeIcon = -1;                            // +0x1D4
    int m_combatMode = 0;                               // +0x1D8 0 (0xC/0xD), 1 (0x10/0x11), 2 (0xE/0xF)
    S_SquadAction m_action;                             // +0x1E0
    bool m_gunnerLooksAhead = false;                    // +0x230 the gunner looks along the vehicle
    int m_selfHealTime = 0;                             // +0x234 ms
    C_Human *m_pickedTarget = nullptr;                  // +0x238 (only an unreferenced function sets it)
    C_Actor *m_airTarget = nullptr;                     // +0x23C (FindAirTarget)
    I3D_frame *m_airTargetFrame = nullptr;              // +0x240 (a Junkers 88's)
    bool m_mineNear = false;                            // +0x244
    int m_mineCheckTime = 100;                          // +0x248 ms
    std::vector<S_HumanOrder *> m_orders;               // +0x24C
};

// Enemy AI (vtable 0x00811730, 0x1E0 bytes; enemy_controller.cpp, enemy_tasks.cpp,
// enemy_combat.cpp): the soldiers the player fights once the alarm took them out of their
// script (C_ScriptedController::ResetAI; when it is over they go back to the script). One task
// (S_AITask) at a time, by its type: 1 guard the post (home position; fights from there, mans
// a static gun near it), 2 search (the event it is about: heard, seen, hit), 3 combat (keeps
// the weapon's range from the target over the walking network's cover), 5 morale broken
// (surrenders, flees or fights on), 6 gunner (the seat weapon of the vehicle it is in). The AI
// mode (MOVE_AI_*) sets the alert range and how long the human waits before it reacts:
// aggressive (0x100) attacks at once, defensive (0x200) waits for the enemy to come near or
// face it, zombie (0x400, also when it is crippled) is slow to react. Morale (STAT_MORALE)
// rises and falls with what happens; low morale breaks the human.
class C_EnemyController : public C_AIController {
public:
    C_EnemyController() : C_AIController(HUMAN_CONTROLLER_ENEMY) {}   // inline in C_Human::CreateController
    // (Fix: also puts the event of a search task back into its pool.)
    ~C_EnemyController() override;                     // 0x005a4490 (0x005a4470)
    void Attach(C_Human *human, C_HumanController *previous) override;   // 0x005478c0
    // (Forgets the points and the guns around the post.)
    void Detach() override;                             // 0x00547cf0
    void Update(int frameTime) override;                // 0x00548130
    void Tick(int timeDelta) override;                  // 0x00548170
    void SaveGame(C_chunk &chunk) override;             // 0x0054c000
    void LoadGame(C_chunk &chunk) override;             // 0x0054c430
    void SetAIMode(int mode) override;                  // 0x00558400
    S_HumanEvent *HandleEvent(S_HumanEvent *event) override;   // 0x00548490
    // Morale broken and surrendered (task 5 in state SURRENDERED).
    bool IsSurrendered() override;                      // 0x00558e20
    // The mission time the human surrendered at (-1: not).
    int UnknownE8() override { return m_surrenderTime; }   // 0x005a4460
    // (Also forgets the search task's event.)
    void ReleaseTask() override;                        // 0x00548040
    // Whether to throw a grenade, and where (S_AIFire::throwPos): at the middle of the enemies
    // seen 10..50 m away where they stand when it lands (not more than 10 m apart; once in
    // a while by their number).
    bool PlanGrenade() override;                        // 0x005584b0
    // No friend in view within the body spheres of the line of fire (200 m along `dir`).
    bool Unknown114(const S_vector *dir) override;      // 0x00558e50

    // The tasks' states (S_AITask::unknown08; names inferred).
    enum : int {
        GUARD_LOOK = 0, GUARD_PICK_TARGET = 1, GUARD_TO_GUN = 2, GUARD_TO_COVER = 3,
        GUARD_MOVE_FIRING = 5, GUARD_MOVE = 6, GUARD_HOLD = 7, GUARD_FIRE = 8,
        // The guard's and the combat task's against a target shielded in a vehicle: the
        // launcher (from a point in range), else hiding beside the vehicle.
        STATE_LAUNCHER = 100, STATE_TO_LAUNCHER_POINT = 0x65, STATE_LAUNCHER_FIRE = 0x66,
        STATE_ATTACK_DONE = 200, STATE_HIDE_FROM_VEHICLE = 300, STATE_TO_HIDING = 0x12D,
        STATE_HIDING = 0x12E,
        SEARCH_START = 0, SEARCH_IDLE = 2, SEARCH_GO = 3, SEARCH_TURN = 4, SEARCH_LOOK = 5,
        SEARCH_TO_POINT = 6, SEARCH_WALK = 7, SEARCH_AIM = 8, SEARCH_AROUND = 9,
        SEARCH_AROUND_WALK = 10, SEARCH_TO_SOURCE = 11, SEARCH_WAIT = 0xC, SEARCH_HOME = 0xD,
        SEARCH_CLIMBING = 0xE, SEARCH_OFF_LADDER = 0xF,
        COMBAT_PICK_TARGET = 0, COMBAT_MOVE = 1, COMBAT_TO_RANGE = 2, COMBAT_TO_COVER = 3,
        COMBAT_FIRE = 4, COMBAT_LOOK = 5, COMBAT_APPROACH = 6,
        BROKEN_CALM = 0x1000000, BROKEN_START = 0x1000001, BROKEN_SURRENDER = 0x2000001,
        BROKEN_SURRENDERING = 0x2000002, BROKEN_SURRENDERED = 0x2000003, BROKEN_FLEEING = 0x4000001,
        BROKEN_FLEE = 0x4000002, BROKEN_FLEE_WALK = 0x4000003, BROKEN_FLED = 0x4000004,
        BROKEN_FIGHT = 0x8000001, BROKEN_FIGHT_PICK = 0x8000002, BROKEN_FIGHT_ON = 0x8000003,
        BROKEN_FIGHT_WAIT = 0x8000004,
        // The kinds of the broken states (their bits).
        BROKEN_SURRENDER_KIND = 0x2000000, BROKEN_FLEE_KIND = 0x4000000, BROKEN_FIGHT_KIND = 0x8000000,
        GUNNER_START = 0, GUNNER_WATCH = 1, GUNNER_PICK = 2, GUNNER_FIRE = 3, GUNNER_PAUSE = 4,
        GUNNER_DONE = 5, GUNNER_GIVE_UP = 6, GUNNER_LEAVE = 7,
    };

private:
    // --- enemy_controller.cpp: life cycle, events, the task choice, firing ------------------
    // The AI modes' ranges and delays (aggressive / defensive / zombie).
    bool SetupAggressive();                             // 0x00547ad0 (__fastcall)
    bool SetupDefensive();                              // 0x00547af0 (__fastcall)
    bool SetupZombie();                                 // 0x00547bf0 (__fastcall)
    // The previous controller's sounds and its alarm as stimuli.
    bool TakeOver(C_HumanController *previous);         // 0x005518f0
    void FindHomeNeighbours();                          // 0x00551b00 (__fastcall)
    void FindHomeCover();                               // 0x00551b50 (__fastcall)
    void FindGuns();                                    // 0x00551ba0 (__fastcall)
    // Back to the mission script (morale restored).
    void ReturnToScript();                              // 0x00547d80
    // An enemy within the alert range or facing the human: it reacts after the mode's delay.
    void CheckThreat();                                 // 0x00547da0 (__fastcall)
    void UpdateTimers(int timeDelta);                   // 0x00547e70
    // The weapon in the hands has fewer than 10 rounds.
    bool IsLowOnAmmo();                                 // 0x00548010 (__fastcall)
    void UpdateFiring(int timeDelta);                   // 0x00548080
    S_HumanEvent *HandleTaskEvent(S_HumanEvent *event); // 0x005482a0
    S_HumanEvent *HandleCommonEvent(S_HumanEvent *event);   // 0x005482f0
    // Crippled: zombie; aggressive and hurt (< 50 %): defensive.
    void UpdateMode();                                  // 0x005484b0
    void ChooseTask();                                  // 0x00548520
    void ChangeMorale(int delta);                       // 0x00548580
    // In a seat with a weapon of a car, a submarine, a static weapon or a Junkers.
    bool HasSeatWeapon();                               // 0x00548590 (__fastcall)
    // The task's bookkeeping and the change of tasks; null: no task (back to the script).
    S_AITask *UpdateTask(int timeDelta);                // 0x00548610
    void RunTask(int timeDelta);                        // 0x00548840
    void StartGuard();                                  // 0x00548890
    void StartSearch();                                 // 0x00549360
    void StartCombat();                                 // 0x0054ab00
    void StartBroken();                                 // 0x0054b730
    void StartGunner();                                 // 0x0054cb70
    // The stance (the aim height factor of it with it).
    void SetEnemyStance(int stance);                    // 0x00559360
    bool HasWeapon();                                   // 0x005593d0 (__fastcall)
    bool DrawWeapon();                                  // 0x00559420 (__fastcall)
    // A weapon that is not a launcher into the hands (from the long gun place or the belt).
    bool TakeFirearm();                                 // 0x005594b0 (__fastcall)
    bool HasLauncherInHands();                          // 0x00559570 (__fastcall)
    // A launcher into the hands from the long gun place.
    bool TakeLauncher();                                // 0x00559590 (__fastcall)
    bool HasLauncher();                                 // 0x00559660 (__fastcall)
    // The nearest seen enemy (not the target) who is not in an armed vehicle.
    C_Human *NearestTargetOnFoot();                     // 0x005596d0 (__fastcall)
    // ms to wait before firing at the target (by its distance).
    int GetFireWait();                                  // 0x00558110 (__fastcall)
    bool NeedsReload();                                 // 0x00558230 (__fastcall)
    bool SeesEnemy(const C_Human *target) const;        // 0x005582a0
    void TestSightNow(const C_Human *target);           // 0x005582c0
    bool SeesEnemies() const;                           // 0x005582f0 (__fastcall)
    // Task 5 fighting on.
    bool IsFightingOn() const;                          // 0x00558960 (__fastcall)
    C_Human *NearestEnemy();                            // 0x00558980 (__fastcall)
    bool LookAtNearestEnemy();                          // 0x00558a60
    // Throws away the item in the hands (an empty launcher).
    void DropHandsItem();                               // 0x00558aa0 (__fastcall)
    // Fires at the target (raising the weapon first, after the wait), or throws a grenade.
    bool Attack(int timeDelta);                         // 0x00558ae0 (timeDelta not used)
    void SetBurstTimes(C_Actor *target);                // 0x00558bd0

    // --- enemy_tasks.cpp: guard, search, morale broken ------------------------------------
    // Off a ladder.
    bool IsNotClimbing();                               // 0x00553cc0 (__fastcall)
    // In the scene's primary sector (not in a building).
    bool IsOutdoors();                                  // 0x00553ce0 (__fastcall)
    // The pace word of running: outdoors 0x10008, else walking 0x4008 (inline).
    uint32_t RunPace();
    // No item in the hands, or one of behaviour 0, 3, 7 or 9 (walked with raised).
    bool IsLightWeapon();                               // 0x00554760 (__fastcall)
    void GuardEvent(S_HumanEvent *event);               // 0x00548900
    void UpdateGuard(int timeDelta);                    // 0x00548b00
    void SearchEvent(S_HumanEvent *event);              // 0x00549940
    bool IsSameSource(const S_HumanEvent *event);       // 0x00549700
    void SearchSameSource(S_HumanEvent *event);         // 0x00549760
    void SetSearchEvent(S_HumanEvent *event);           // 0x00554c30 (0x00554d10)
    void SearchStrongerEvent(S_HumanEvent *event);      // 0x00554d20
    void SearchFriendHeard(S_HumanEvent *event);        // 0x00554e20
    void UpdateSearch(int timeDelta);                   // 0x00549ae0 (timeDelta not used)
    // Where to walk to search for the event (CHECKPOINT_NONE: nowhere).
    int FindSearchPoint();                              // 0x00553eb0
    // A point 10..20 m away beyond the event, to search around it.
    int FindSearchAround();                             // 0x005541f0 (__fastcall)
    // Walks (pace 0x4008, alert 1), a heavy weapon lowered.
    void WalkCarefully();                               // 0x005547b0
    // True when `pos` is within 40 degrees ahead of the body; else turns toward it.
    bool LookTowardIfAside(const S_vector4 &pos);       // 0x00554840
    // Looks around on the side of `pos` for 4..10 s.
    void LookAroundToward(const S_vector4 &pos);        // 0x00557f70
    int FindCoverToward(const S_vector4 &pos);          // 0x00557540
    void BrokenEvent(S_HumanEvent *event);              // 0x0054b770
    void UpdateBroken(int timeDelta);                   // 0x0054b8f0

    // --- enemy_combat.cpp: combat, the gunner, points to go to ------------------------------
    void CombatEvent(S_HumanEvent *event);              // 0x0054ab70
    void UpdateCombat(int timeDelta);                   // 0x0054ac40
    // -1 nearer than the weapon's range to the target, 1 farther, 0 within.
    int CompareRange();                                 // 0x00551db0
    int FindCombatPoint();                              // 0x00551e10
    void GetWeaponRange(float *nearest, float *farthest);   // 0x00551e80
    float GetMinDistance();                             // 0x00551fc0 (__fastcall)
    float GetMaxDistance();                             // 0x00552040 (__fastcall)
    bool IsInWeaponRange();                             // 0x005520e0 (__fastcall)
    // Weapons fired walking (behaviours 0, 2, 3, 7, 9).
    bool CanFireMoving();                               // 0x00552140 (__fastcall)
    // Nothing between `from` and the target's body.
    bool HasLineTo(const S_vector4 &from);              // 0x00552190
    // The target far (20 m) and level enough to fire at lying, with a rifle or machine gun,
    // nobody near, outdoors, a clear line low ahead.
    bool CanFireLying();                                // 0x00552200
    // The stance the cover of point `cp` gives against `target` (or the enemies seen).
    bool TakeCoverStance(const S_Checkpoint *cp, C_Human *target);   // 0x005525f0
    void ChooseCombatStance();                          // 0x00552970
    // Where to fight from, by the range: back, nearer, or cover in range.
    int FindRetreatPoint();                             // 0x00552b00 (__fastcall)
    int FindApproachPoint(const S_Checkpoint *from, C_Human *target);   // 0x00554e80 (0x00553530)
    int FindFlankPoint(const S_vector4 &pos);           // 0x005572f0 (0x00553550)
    int FindCoverInRange();                             // 0x00553580 (__fastcall)
    bool HasFriendNearby();                             // 0x00553b90 (__fastcall)
    bool ShouldStay();                                  // 0x00553be0 (__fastcall)
    bool MoveToCombatPoint(int checkpoint);             // 0x00553d10
    // Down and away: lowers the weapon, stands and runs (pace 0x10000).
    void RunForCover();                                 // 0x00554800
    int FindLauncherPoint(C_Human *target);             // 0x00555400
    int FindPointBesideVehicle(C_Human *target);        // 0x00555ca0
    int PickCoverAwayFrom(C_Human *target);             // 0x005579e0
    int NearestPoint(const S_vector4 &pos, const std::vector<uint16_t> &points, float *distance2);   // 0x00557d10
    // A static gun of the post to man (C_ActorsModule's, from the walking points of type 11).
    C_Actor *FindGunToUse(int timeDelta);               // 0x0054e600 (timeDelta not used)
    bool IsGunUseless(C_Actor *gun);                    // 0x0054e390
    C_Actor *NearestGun(const std::vector<C_Actor *> &guns);   // 0x0054e520
    void GunnerEvent(S_HumanEvent *event);              // 0x0054cbb0
    void UpdateGunner(int timeDelta);                   // 0x0054cf70 (timeDelta not used)
    C_Human *PickGunnerTarget();                        // 0x0054d300
    bool IsThreatToGunner(C_Human *human);              // 0x0054d570
    bool IsGunnerThreatened();                          // 0x0054d5c0
    int GetGunnerDelay();                               // 0x0054d620 (__fastcall)
    S_vector GetGunAimDir(bool scatter);                // 0x0054d800
    void LeaveGun();                                    // 0x0054dc10
    // The seat weapon cannot point at `target` (also 0.5 m higher).
    bool IsOutOfGunReach(C_Human *target);              // 0x0054dc50
    bool IsAtStaticWeapon();                            // 0x0054dcf0 (__fastcall)
    void AimGun();                                      // 0x0054dd20
    void AimAndFireGun();                               // 0x0054ded0
    bool IsGunEmpty();                                  // 0x0054e7a0 (__fastcall)
    // Turns the gun toward the likeliest enemy (seen or heard, not in sight).
    void SweepGun();                                    // 0x0054e7d0 (__fastcall)
    // The best enemy in view to fire at with the seat weapon.
    C_Human *PickSeenTarget();                          // 0x0054f040
    void AimAndFireCannon();                            // 0x0054f280
    bool IsLineOfFireFree(const S_vector4 &pos);        // 0x0054f910

    // S_HumanEvent of a search task (type 2), or C_Actor of a guard task going to a gun
    // (type 1, state GUARD_TO_GUN): +0x148.
    enum : uint32_t {
        FLAG_BRAVE = 0x01,                              // morale >= 90 at Attach
        FLAG_STEADY = 0x02,                             // morale >= 75
        FLAG_DONE = 0x04,                               // back to the script
        FLAG_NO_ALARM_DONE = 0x08,                      // the script has no OnAlarmDone block: stays
        FLAG_GUNNER = 0x10,                             // at a seat weapon when the first task was chosen
        FLAG_CANNON = 0x20,                             // the seat weapon is a cannon (component kind 2)
    };

    bool m_targetChanged = false;                       // +0x140 the gun turned to a new target (wait a tick)
    uint32_t m_enemyFlags = 0;                          // +0x144 FLAG_*
    S_HumanEvent *m_event = nullptr;                    // +0x148 a search task's event
    C_Actor *m_gun = nullptr;                           // +0x148 a guard task's static gun (the same field)
    int m_unknown14C = 0;                               // +0x14C (only saved)
    int m_unknown150 = 0;                               // +0x150 (only saved)
    bool m_unknown154 = false;                          // +0x154 (only saved)
    std::vector<uint16_t> m_homePoints;                 // +0x158 the walking points within the home radius
    std::vector<uint16_t> m_homeCover;                  // +0x168 the cover points among them
    S_vector4 m_homePos = {};                           // +0x180 where the human was at Attach
    int m_homeCheckpoint = -1;                          // +0x190 its walking point
    float m_homeRadius = 0.0f;                          // +0x194 m
    int m_taskTime = 0;                                 // +0x198 ms since the task started
    C_Human *m_target = nullptr;                        // +0x19C
    float m_aimHeight = 0.0f;                           // +0x1A0 by the stance (0.45 standing, 0.59 crouching, 0 lying)
    int m_alertRange = 0;                               // +0x1A4 m
    // ms the human stays alert: defensive and zombie humans fire only meanwhile (an enemy
    // near or facing them starts it; aggressive ones always).
    int m_alertTime = 0;                                // +0x1A8
    int m_grenadeTime = 0;                              // +0x1AC ms since the last grenade
    int m_fireWait = 0;                                 // +0x1B0 ms before firing
    std::vector<C_Actor *> m_guns;                      // +0x1B4 static guns at the post
    int m_gunCheckTime = 0;                             // +0x1C4 ms
    int m_moraleCheckTime = 5000;                       // +0x1C8 ms
    int m_coverTime = 0;                                // +0x1CC ms off a route in combat
    int m_unarmedTime = 0;                              // +0x1D0 ms with empty hands
    bool m_lowAmmo = false;                             // +0x1D4
    int m_surrenderTime = -1;                           // +0x1D8 mission time
    int m_enemySeenTime = 0;                            // +0x1DC ms enemies in view
};

// AI driven by the human's mission script (vtable 0x00811500, 0x1F0 bytes;
// scripted_controller.cpp): the HUMAN_* script commands. The script moves the human from
// point to point (each move ends at a script label), sets its pace and stance, makes it
// fire at an actor or a frame for a time, or attack while it walks (AttackMoveTo: it picks
// its targets and fires or stops to fire by the weapon); it keeps its place in a formation
// or waits for the player. What the human perceives raises the script's alarm (OnAlarm)
// once, with its kind and position, until the script calls it off; the script then hands
// the human over to the AI of its role (ResetAI).
class C_ScriptedController : public C_AIController {
public:
    // The kinds of alarm (GetAlarmType; SetAlarmType switches them off and on).
    enum : uint32_t {
        ALARM_SHOT_AT = 0x001,                          // shot at, or a loud sound (not a friend's)
        ALARM_GUNFIRE = 0x002,                          // a sound of the weapon shooting chapter
        ALARM_FOOTSTEPS = 0x004,                        // of the puppet movement chapter
        ALARM_OBJECT_NOISE = 0x008,                     // of the object manipulation chapter
        ALARM_EXPLOSION = 0x010,
        ALARM_ENEMY_SEEN = 0x020,
        ALARM_HIT = 0x040,                              // a bullet, a blow or an explosion hit him
        ALARM_BODY_HEARD = 0x080,
        ALARM_FRIEND_HEARD = 0x100,                     // at the enemy the friend sees, if any
        ALARM_BODY_SEEN = 0x200,
    };

    // AttackMoveTo's attack (0x14 bytes, +0x198): while the human walks, it looks for a
    // target now and then and fires at it walking, stopping, or crouching, by its weapon.
    struct S_Attack {
        // state
        enum : uint32_t {
            STATE_NONE = 0,
            STATE_DECIDE = 0x10,                        // looks for a target when `time` runs out
            STATE_20 = 0x20,                            // (not set anywhere seen)
            STATE_FIRING = 0x40,                        // at the target for `time`
            STATE_STOP = 0x80,                          // ends at the next tick
        };
        void Start();                                   // 0x00591eb0 (__fastcall)
        void Stop();                                    // 0x00591ed0 (__fastcall; only while active)
        void Clear();                                   // 0x00591ee0 (__fastcall; the constructor's)
        bool SaveGame(C_chunk &chunk) const;            // 0x00591aa0
        bool LoadGame(C_chunk &chunk);                  // 0x00591c10

        bool active = false;                            // +0x00
        uint32_t state = STATE_NONE;                    // +0x04
        int time = 0;                                   // +0x08 ms
        uint32_t stance = 0;                            // +0x0C MOVE_STANCE_MASK while firing (0: the script's)
        C_Actor *target = nullptr;                      // +0x10
    };

    C_ScriptedController() : C_AIController(HUMAN_CONTROLLER_AI) {}   // inline in C_Human::CreateController
    ~C_ScriptedController() override = default;         // 0x005a42d0 (0x005a42b0)
    void Attach(C_Human *human, C_HumanController *previous) override;   // 0x0058d810
    // (Also forgets the human.)
    void Detach() override;                             // 0x0058d8e0
    void Update(int frameTime) override;                // 0x0058e1c0
    void Tick(int timeDelta) override;                  // 0x0058e210
    void SaveGame(C_chunk &chunk) override;             // 0x005907d0
    void LoadGame(C_chunk &chunk) override;             // 0x00590b60
    // Steers a submarine (the others: nothing).
    void ControlVehicle() override;                     // 0x005911f0
    void OnEnemySeen(C_Human *human) override;          // 0x00590060
    void OnBodySeen(C_Human *human) override;           // 0x0058ffe0
    void OnBodyHeard(C_Actor *source) override;         // 0x00590250
    void OnFriendHeard(C_Actor *source) override;       // 0x00590170
    void OnLoudSound(C_Actor *source) override;         // 0x005900e0
    void OnSoundHeard(S_RememberedSound *sound) override;   // 0x005902d0
    void OnShotAt(C_Actor *shooter, const S_vector &velocity) override;   // 0x00590380
    void OnHit(int type, int zone, int health, C_Actor *attacker, const S_vector &pos,
               const S_vector &dir) override;           // 0x00590400
    bool Unknown104() override { return false; }        // 0x005a42a0
    bool Unknown114(const S_vector * /*dir*/) override { return true; }         // 0x005a4290

    // --- the script's commands (C_Human's HUMAN_* forwarders) ----------------------------
    void SetScript(C_Script *script) { m_script = script; }   // 0x005907c0
    // The script takes the human back: its OnAlarmDone block runs.
    void StartScript();                                 // 0x00590700
    // The formation the human keeps its place in (null: none; C_Formation).
    void SetFormation(C_FormationShape *formation);     // 0x0058d900
    // Switches alarm kinds (ALARM_*) on (`set`) or off. Switching ALARM_ENEMY_SEEN on with
    // enemies in view raises it at once.
    void SetAlarmType(uint32_t flags, bool set);        // 0x0058e650
    uint32_t GetAlarmType() const { return m_alarmType; }   // 0x0058e700
    // Where the alarm went off (the AI that takes over looks there).
    const S_vector4 &GetAlarmPos() const { return m_alarmPos; }   // +0x140
    // Off: the alarm may go off again (at once with enemies in view). On: nothing.
    void SetAlarm(bool on);                             // 0x0058e710
    // Walks to the walking point; the script goes on at `label` there.
    void MoveTo(int16_t checkpoint, int label);         // 0x0058e780
    // The same, attacking on the way.
    void AttackMoveTo(int16_t checkpoint, int label);   // 0x00591470
    void MoveToAlarm(int label);                        // 0x00590760
    // Stops while the player is farther than sqrt(stopDist2), goes on within
    // sqrt(resumeDist2); `watch`: looks at him meanwhile.
    void SetWaitForPlayer(bool on, float stopDist2, float resumeDist2, bool watch);   // 0x0058e850
    uint16_t FindCheckpoint(const char *name) const;    // 0x0058e8a0
    // Fires at the actor / toward the frame for `time` ms (`fake`: the fire flags of the
    // weapon controls); the script goes on at `label` then.
    void AttackActor(C_Actor *target, int time, bool fake, int label);      // 0x0058f1f0
    void AttackFrame(I3D_frame *target, int time, bool fake, int label);    // 0x0058f260
    // The pace of the moves.
    void SetMoveModeRun();                              // 0x0058f2d0
    void SetMoveModeWalk();                             // 0x0058f2f0
    void SetMoveModeSilence();                          // 0x0058f310
    void SetMoveModeGuard();                            // 0x0058f330
    // The stance (seated: standing up instead).
    void SetMoveModeLie();                              // 0x0058f350
    void SetMoveModeCrouch();                           // 0x0058f370
    void SetMoveModeStand();                            // 0x0058f390
    void SetMoveModeStandFast();                        // 0x0058f420
    void Sit(I3D_frame *seat);                          // 0x0058f4b0
    // Sits down cross-legged ("sedimturek").
    void ActivityTurk();                                // 0x0058f540
    // Deaf and blind (MOVE_DEAF) / not.
    void DisableEvents();                               // 0x0058f5d0
    void EnableEvents();                                // 0x0058f5f0
    // The AI mode (MOVE_AI_MODE_MASK).
    void SetMode(int mode);                             // 0x0058f610
    // Ends the move, the attack and the firing.
    void StopAction();                                  // 0x0058f640
    // Hands the human over to the AI of its role.
    void ResetAI();                                     // 0x0058f680
    void PickObject(I3D_frame *object, bool oneHand);   // 0x0058f700
    void DropObject();                                  // 0x0058f790
    void TakeWeapon();                                  // 0x0058f830
    void HolsterWeapon();                               // 0x0058f880
    void Reload();                                      // 0x0058f8c0
    // Plays an activity (at a frame); its length in ms.
    int PlayActivity(int activity);                     // 0x0058f8d0
    int PlayActivity(int activity, I3D_frame *target);  // 0x0058f970
    // Frozen, deaf and blind (network games: the clients are told).
    void Suspend(bool on);                              // 0x0058faa0
    void SetOptimized(int mode);                        // 0x0058fcc0 (the carry mode)
    bool SeesEnemy() const;                             // 0x0058fcf0
    bool SeesFriend() const { return false; }           // 0x0058fd20
    // Turns toward the frame (a seat weapon in the vehicle too).
    void TurnAt(I3D_frame *target);                     // 0x005904c0
    // A named animation (network games: the clients are told; a looped one is also sent to
    // the clients that join later).
    void SetAnimation(const char *name, int blendTime, int cancelTime, bool loop);   // 0x00591230
    // (Not called: the C_Human forwarders of these four have no callers.)
    void AdvanceScript(int a, int b);                   // 0x0058e880 (both unused)
    bool HasAIMode() const;                             // 0x0058f630
    void PlaySpeech(int group);                         // 0x0058fa10
    void LookAround1(int arg);                          // 0x00590750 (LookAround(1); arg unused)

    // The looped animation of SetAnimation (empty: none) and its third argument.
    const char *GetAnimationName() const { return m_animation; }   // char[] +0x150
    int GetAnimationFrame() const { return m_animationFrame; }      // +0x188
    bool IsSuspended() const { return m_suspended; }    // +0x1D2
    // The script's move modes (pace and stance; a squad member taking over follows quietly
    // with MOVE_PACE_1 or MOVE_PACE_2, and takes combat order 0x10 with 0x100).
    uint32_t GetScriptFlags() const { return m_scriptFlags; }   // +0x1B0

private:
    // Raises the alarm of `kind` at `pos` unless one went off already or it is switched off.
    void RaiseAlarm(uint32_t kind, const S_vector4 &pos);
    // The move to `checkpoint` (MoveTo, AttackMoveTo): false when the human stands there
    // already (the script goes on at once).
    bool StartMove(int16_t checkpoint, int label);
    // Keeps the human's place in the formation: the pace by the distance to it (false: no
    // formation, or the human leads it).
    bool FollowFormation();                             // 0x0058d970
    // Turns the vehicle's seat weapon toward the turn target, or the vehicle's way.
    void TurnSeatWeapon();                              // 0x0058db20
    // AttackActor / AttackFrame: fires for m_fireTime.
    void UpdateFireTime(int timeDelta);                 // 0x0058e500
    // From the seat weapon to the target (`scatter`: off by up to 6 % of the distance).
    S_vector GetSeatAimDir(bool scatter);               // 0x0058e8b0
    void AimAndFireSeatWeapon();                        // 0x0058ebd0
    // A meleeTracker's alert (event 0x10000017) becomes a sound heard by the human and the humans
    // near him.
    void ProcessMeleeTrackerAlerts();                            // 0x0058fd30
    // AttackMoveTo's attack: a new target and how to fire at it, or the end of the firing.
    void DecideAttack();                                // 0x00591550
    void UpdateAttack(int timeDelta);                   // 0x00591930

    S_vector4 m_alarmPos = {};                          // +0x140 (uninitialized in the original)
    char m_animation[56] = {};                          // +0x150
    int m_animationFrame = 0;                           // +0x188
    bool m_waitForPlayer = false;                       // +0x18C
    bool m_watchPlayer = false;                         // +0x18D
    float m_stopDist2 = 0.0f;                           // +0x190 m^2
    float m_resumeDist2 = 0.0f;                         // +0x194 m^2
    S_Attack m_attack;                                  // +0x198
    C_Script *m_script = nullptr;                       // +0x1AC the human's frame's script
    uint32_t m_scriptFlags = 0;                         // +0x1B0 MOVE_STANCE_MASK and MOVE_PACE_* of the moves
    uint32_t m_alarmType = 0;                           // +0x1B4 the last alarm (ALARM_*)
    uint32_t m_ignoredAlarms = 0;                       // +0x1B8 ALARM_* switched off
    bool m_holdingObject = false;                       // +0x1BC PickObject
    bool m_fireFromSeat = false;                        // +0x1BD AttackActor / AttackFrame in a vehicle's seat
    uint8_t m_unknown1BE[2] = {};                       // +0x1BE (only saved and loaded)
    int m_fireTime = 0;                                 // +0x1C0 ms
    C_Actor *m_fireTarget = nullptr;                    // +0x1C4
    I3D_frame *m_fireFrame = nullptr;                   // +0x1C8 (no fire target)
    I3D_frame *m_turnTarget = nullptr;                  // +0x1CC
    bool m_turning = false;                             // +0x1D0 the seat weapon turns (TurnAt)
    bool m_seated = false;                              // +0x1D1 Sit / ActivityTurk, until a stand
    bool m_suspended = false;                           // +0x1D2
    int m_carryMode = -1;                               // +0x1D4 SetOptimized's
    int m_label = -1;                                   // +0x1D8 where the script goes on after the move
    int m_fireLabel = -1;                               // +0x1DC ... after the firing
    C_FormationShape *m_formation = nullptr;            // +0x1E0
};

// Civilian / allied NPC AI (vtable 0x008113e8, 0x140 bytes; scripted_controller.cpp): the AI
// base's moves and senses without its reactions to enemies, shots, sounds and hits, and
// without a Tick of its own.
class C_NeutralController : public C_AIController {
public:
    C_NeutralController() : C_AIController(HUMAN_CONTROLLER_NEUTRAL) {}   // inline in C_Human::CreateController
    ~C_NeutralController() override = default;          // 0x005a43e0 (0x005a43c0)
    void Attach(C_Human *human, C_HumanController *previous) override;   // 0x005a42f0
    // (Also forgets the human.)
    void Detach() override;                             // 0x005a4310
    void Update(int /*frameTime*/) override {}          // 0x005a4330
    void Tick(int /*timeDelta*/) override {}            // 0x005a4340
    void OnKilledEnemy(C_Human * /*victim*/) override {}    // 0x005a4350
    void OnEnemySeen(C_Human * /*human*/) override {}       // 0x005a4360
    void OnEnemyLost(C_Human * /*human*/) override {}       // 0x005a4370
    void OnLoudSound(C_Actor * /*source*/) override {}      // 0x005a4380
    void OnSoundHeard(S_RememberedSound * /*sound*/) override {}   // 0x005a4390
    void OnShotAt(C_Actor * /*shooter*/, const S_vector & /*velocity*/) override {}   // 0x005a43a0
    void OnHit(int /*type*/, int /*zone*/, int /*health*/, C_Actor * /*attacker*/, const S_vector & /*pos*/,
               const S_vector & /*dir*/) override {}    // 0x005a43b0
    bool Unknown114(const S_vector * /*dir*/) override { return true; }         // 0x005a42e0
};

// A network client's copy of another player's human (vtable 0x008112d8, 0x2D0 bytes;
// remote_controller.cpp): it replays what the server sends (the replicated position, view and
// control bits, the actions of its net object, the controller messages).
class C_RemoteController : public C_HumanController {
public:
    C_RemoteController() : C_HumanController(HUMAN_CONTROLLER_REMOTE) {}   // inline in C_Human::CreateController
    ~C_RemoteController() override = default;           // 0x005a4450 (0x005a4430)
    void Attach(C_Human *human, C_HumanController *previous) override;   // 0x00592140
    // (Also forgets the human.)
    void Detach() override;                             // 0x00592200
    void Update(int frameTime) override;                // 0x00592a60
    void Tick(int /*timeDelta*/) override {}            // 0x00592e30
    // The requests the client makes on this human (items moved, dropped) go to the server.
    bool RequestMoveItem(int uid, int otherId, int fromContainer, int toContainer, int slot) override;   // 0x00592320
    bool RequestTransferItem(int uid, int arg, int otherId, int otherUid, int container) override;      // 0x00592500
    void RequestThrowItem(int uid, const S_vector &velocity) override;   // 0x005926e0
    void OnMessage(C_BitStream *msg) override;          // 0x00592fe0
    // The body's weapon state from its flags (raised: bit 6, bit 8 to +0xAC).
    void OnBodyFlags(uint32_t flags) override;          // 0x005941b0
    void FlushMessages(bool toClients) override;        // 0x00592220
    void SetSpectating(bool on) override { m_spectating = on; }   // 0x005a43f0
    bool IsSpectating() override { return m_spectating; }         // 0x005a4400
    // The server's state (CTRL_MSG_23).
    bool IsSurrendered() override { return m_surrendered; }      // 0x005a4410
    bool Unknown104() override { return false; }        // 0x005a4420
    // Objectives games: the VIP.
    void SetVip(bool vip) { m_vip = vip; }
    bool IsVip() const { return m_vip; }

private:
    // The net object's messages: the actions, the dive, the binoculars.
    void ProcessNetMessages();                          // 0x00592e40
    void PlayAction(const C_NetActionMessage &message); // 0x00593890
    // A squad command's gesture (CTRL_MSG_ACTION).
    void PlayCommandGesture(const S_MsgAction &message);   // 0x00594110

    float m_controlAxes[CONTROL_COUNT] = {};            // +0xC0
    float m_controlButtons[CONTROL_COUNT] = {};         // +0x1C0
    bool m_vip = false;                                 // +0x2C0
    bool m_surrendered = false;                         // +0x2C1
};

// Sniper AI (vtable 0x00811618, 0x180 bytes; sniper_controller.cpp): a human the script made
// a sniper (HUMAN_SetSniper) once the alarm took it out of the script (C_ScriptedController::
// ResetAI). It keeps to its post, the walking points within the sniper radius, and fires from
// its sniper points (type 10) at the enemies in view, or turns to the sources of what it
// heard; a zombie sniper only turns and fires from where it is. Some time after the last
// enemy or sound it goes back to the script. One task (S_AITask of type 3) at a time.
class C_SniperController : public C_AIController {
public:
    C_SniperController() : C_AIController(HUMAN_CONTROLLER_SNIPER) {}   // inline in C_Human::CreateController
    ~C_SniperController() override = default;           // 0x005a4540 (0x005a4520)
    void Attach(C_Human *human, C_HumanController *previous) override;   // 0x00559a50
    // (Also forgets the human and the post's points.)
    void Detach() override;                             // 0x00559c20
    void Update(int frameTime) override;                // 0x00559da0
    void Tick(int timeDelta) override;                  // 0x00559de0
    void SaveGame(C_chunk &chunk) override;             // 0x0055c820
    void LoadGame(C_chunk &chunk) override;             // 0x0055cb50
    S_HumanEvent *HandleEvent(S_HumanEvent *event) override;   // 0x00559f60
    bool Unknown114(const S_vector * /*dir*/) override { return true; }         // 0x005a4510

    // The task's states (S_AITask::unknown08; names inferred). SNIPER_TO_POINT and
    // SNIPER_AT_POINT are not reached from the others.
    enum : int {
        SNIPER_IDLE = 0, SNIPER_TO_POINT = 1, SNIPER_AT_POINT = 2, SNIPER_DECIDE = 3, SNIPER_TURN = 4,
        SNIPER_MOVING = 5, SNIPER_FIRE = 6,
    };

private:
    void UpdateTimers(int timeDelta);                   // 0x00559c60
    // Throws away the item in the hands (an empty launcher).
    void DropHandsItem();                               // 0x00559cf0 (__fastcall)
    void UpdateFiring(int timeDelta);                   // 0x00559d40
    // The task's bookkeeping; null: no task (back to the script).
    S_AITask *UpdateTask(int timeDelta);                // 0x00559eb0
    // Done with nothing to go back to the script for: drops the task.
    void CheckDone(int timeDelta);                      // 0x0055a0e0 (timeDelta not used)
    void RunTask(int timeDelta);                        // 0x0055a100 (0x0055a1c0)
    void StartTask();                                   // 0x0055a130
    void UpdateZombie(int timeDelta);                   // 0x0055a6e0 (timeDelta not used)
    void UpdateSniper(int timeDelta);                   // 0x0055ad50 (timeDelta not used)
    void TestSightOfTarget();                           // 0x0055b6c0 (__fastcall)
    // Crouches at `cp` when it covers that, but not standing, from every enemy in view.
    void TakeCoverStance(const S_Checkpoint *cp);       // 0x0055b6f0
    // Enemies in view or stimuli tracked.
    bool HasSomethingToDo();                            // 0x0055b860 (__fastcall)
    bool SeesEnemies();                                 // 0x0055b8a0 (__fastcall)
    bool SeesTarget();                                  // 0x0055b8d0 (__fastcall)
    // The source of the oldest stimulus, which is dropped.
    C_Actor *TakeStimulusSource();                      // 0x0055b920 (__fastcall)
    // The stance to fire at the target from `cp` over its cover.
    void ChooseStance(const S_Checkpoint *cp);          // 0x0055bc40
    // The post's point with the most cover from the enemies in view.
    int FindCoverPoint();                               // 0x0055bd40 (__fastcall)
    // The sniper point to fire at the target from (the target: what was heard / seen).
    int FindFirePointHeard();                           // 0x0055c0b0 (__fastcall)
    int FindFirePointSeen();                            // 0x0055c450 (__fastcall)

    enum : uint32_t {
        FLAG_BRAVE = 0x01,                              // initial morale over 90
        FLAG_STEADY = 0x02,                             // initial morale over 75
        FLAG_DONE = 0x04,                               // back to the script
        FLAG_NO_ALARM_DONE = 0x08,                      // the script has no OnAlarmDone block: stays
    };

    S_vector4 m_homePos = {};                           // +0x140 where the human was at Attach (saved without w)
    uint32_t m_startStance = 0;                         // +0x150 its stance then (only saved)
    uint32_t m_sniperFlags = 0;                         // +0x154 FLAG_*
    int m_taskTime = 0;                                 // +0x158 ms since the task started
    C_Human *m_target = nullptr;                        // +0x15C
    std::vector<uint16_t> m_points;                     // +0x160 the walking points within the sniper radius
    std::vector<uint16_t> m_sniperPoints;               // +0x170 those of them of type 10
};
