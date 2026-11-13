// A place for an explosive charge (C_ExplosivePlace, actor type 22, vtable 0x008134f0, 0x180 bytes;
// original 0x0060c170..0x0060da80): an objective a human places a charge from an item at (state 1;
// its frame's "explosive_on" child shows instead of "explosive_off"), which then counts its delay
// down and blows up (area damage, state 2), or is disarmed (state 0 again). A box volume
// (COLLISION_TYPE_LIGHT_OBJECT) lets the use test find it while it is empty or placed.
#pragma once

#include <cstdint>

#include "game/actor.h"
#include "game/collision.h"
#include "ls3d/math.h"

class C_Human;

enum : int {
    EXPLOSIVE_EMPTY = 0,                                // waiting for a charge
    EXPLOSIVE_PLACED = 1,                               // counting down
    EXPLOSIVE_EXPLODED = 2,
    EXPLOSIVE_HIDDEN = 3,
    EXPLOSIVE_STOPPED = 4,                              // between missions (both frames shown)
};

class alignas(16) C_ExplosivePlace : public C_Actor {   // "Explosive place", vtable 0x008134f0
public:
    C_ExplosivePlace();                                 // 0x0060c230
    ~C_ExplosivePlace() override = default;             // 0x0060c2e0 (DS 0x00634eb0) (scalar deleting 0x0060c2c0, DS 0x00634e80)

    // The script's state (EXPLOSIVE_*), the clients told unless the game is local.
    void SetState(int state);                           // 0x0060d300
    int GetState() const { return m_state; }            // +0x94
    // `human` places the charge it carries (while empty): it leaves his inventory, the clients are
    // told, the countdown starts.
    bool Place(C_Human *human);                         // 0x0060d6f0
    // The item type of the charge.
    int GetItemId() const { return m_itemId; }          // +0xA0
    // Empty again, the clients told (`human` is not used).
    bool Disarm(C_Human *human);                        // 0x0060d9f0
    // Ms until the placed charge goes off (the HUD's explosive timer).
    int GetTimeLeft() const { return m_timeLeft; }      // +0xC8

    // A client: the countdown (Tick counts it on the server).
    void Update(int frameTime) override;                // 0x14 0x0060c370
    // A timed charge counts its delay down and explodes.
    void Tick(int timeDelta) override;                  // 0x18 0x0060c330
    // 0 exploded (the effects), 1 disarmed, 2 placed (who and the item), 3 a state.
    void OnMessage(C_BitStream *msg) override;          // 0x1C 0x0060cda0
    void Unknown20(C_BitStream *) override {}           // 0x20 0x0060ce50
    // (SetFrame 0x0060c2f0 is the base's.)
    // Chunks 100 the name's text and the timer flag, 101 the item, 102 the delay, 103 the sound,
    // effect, damage and range, 104 placed at the start, 105 hidden while empty.
    bool WriteProperties(C_chunk &chunk) override;      // 0x30 0x0060c6e0
    bool ReadProperties(C_chunk &chunk) override;       // 0x34 0x0060ca30
    // The base's, the timer flag and the name's text.
    void CopyFrom(const C_Actor *source) override;      // 0x38 0x0060c300
    // Once until StopMission: the box volume made, the frames found, empty (or placed).
    void StartMission(bool server) override;            // 0x3C 0x0060c3b0
    void StopMission(bool server) override;             // 0x40 0x0060c6b0
    // Each side the game runs is stopped and started again.
    void RestartMission(bool server) override;          // 0x44 0x0060cf20
    // Chunk 3000: the state, the time left, who placed it (not the base's actor chunk).
    bool SaveGame(C_chunk &chunk) override;             // 0x48 0x0060cf70
    bool LoadGame(C_chunk &chunk) override;             // 0x4C 0x0060d0c0
    void WriteState(C_BitStream *msg) override;         // 0x50 0x0060ce60
    void ReadState(C_BitStream *msg) override;          // 0x54 0x0060cea0
    void Hit(E_HitType, float, const S_vector &, const S_vector &, C_Actor *, int) override {}   // 0xD4 0x0060da70
    // The part's material, else the frame's, else 11.
    int GetMaterialId(I3D_frame *part) override;        // 0xD8 0x0060cee0

private:
    // The state's frames, volume and name (only the state stored without the two frames; nothing
    // when it is the state already).
    void ApplyState(int state);                         // 0x0060d380
    // The countdown is over: the area damage from who placed it, the clients told.
    void Explode(int planterId);                        // 0x0060d870
    // Without who placed it: exploded and the clients told, no damage (quirk kept).
    void ExplodeHarmless();                             // 0x0060d810
    // A client: exploded, the explosion's effects.
    void OnExploded();                                  // 0x0060d980
    // A client: `humanId` placed the item `uid`.
    void OnPlaced(int humanId, int uid);                // 0x0060d7b0
    void OnDisarmed();                                  // 0x0060da50

    bool m_started = false;                             // +0x90
    int m_state = EXPLOSIVE_STOPPED;                    // +0x94
    bool m_timed = true;                                // +0x98 the placed charge counts its delay down
    int m_nameTextId = 0x1BF9;                          // +0x9C the name while empty
    int m_itemId = 0x44;                                // +0xA0
    int m_delay = 0;                                    // +0xA4 ms
    int m_sound = -1;                                   // +0xA8 the explosion's sound group
    int m_effect = -1;                                  // +0xAC the explosion's effect
    int m_damage = 0;                                   // +0xB0
    int m_range = 0;                                    // +0xB4
    bool m_startPlaced = false;                         // +0xB8 placed when the mission starts
    bool m_hidden = false;                              // +0xB9 hidden while empty
    I3D_frame *m_onFrame = nullptr;                     // +0xBC "explosive_on"
    I3D_frame *m_offFrame = nullptr;                    // +0xC0 "explosive_off"
    int m_planterId = -1;                               // +0xC4 the actor id of who placed it
    int m_timeLeft = 0;                                 // +0xC8 ms
    S_CollisionVolume m_volume;                         // +0xD0
    S_matrix m_invMatrix;                               // +0x130 the frame's inverse world matrix (the volume's)
    bool m_volumeAdded = false;                         // +0x170
};

// The explosive places' creator (g_actorCreators[ACTOR_EXPLOSIVE_PLACE], vtable 0x008134e0).
class C_ExplosivePlaceCreator : public C_ActorCreator {
public:
    C_Actor *Create() override;                         // 0x0060c1a0
    const char *GetName() override { return "Explosive place"; }   // 0x0060c200
    void Destroy(C_Actor *actor) override;              // 0x0060c210
};
