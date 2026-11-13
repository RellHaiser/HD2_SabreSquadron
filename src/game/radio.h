// A radio (C_Radio, actor type 7, vtable 0x00813310, 0x150 bytes; original 0x0060a6f0..0x0060b610):
// an objective a human uses (an item may be needed, a side may be required). Using it turns it to
// state 2 and tells everyone, who then hear its sound. A box volume around its frame
// (COLLISION_TYPE_PLACE).
#pragma once

#include <cstdint>

#include "game/actor.h"
#include "game/collision.h"
#include "ls3d/math.h"

class C_Human;

class alignas(16) C_Radio : public C_Actor {            // "Radio", vtable 0x00813310
public:
    C_Radio();                                          // 0x0060a7b0
    ~C_Radio() override = default;                      // 0x0060a8b0 (scalar deleting 0x0060a890)

    // The state (the script's SetActorState; 1 on, 2 used); the clients are told unless the game
    // is local.
    void SetState(int state);                           // 0x0060b570
    int GetState() const { return m_state; }            // +0xA0
    // The item type it needs (-1: none).
    int GetItemId() const { return m_itemId; }          // +0x98
    // `human` uses it: he carries its item (if it needs one) and is of its side (2: either); state
    // 2, everyone told. (The state is not tested.)
    bool Use(C_Human *human);                           // 0x0060b4c0
    // `human` may use it: it is on (state 1), and as Use.
    bool CanUse(C_Human *human) const;                  // 0x0060b460

    // The state from the network; 2 plays the sound at the frame.
    void OnMessage(C_BitStream *msg) override;          // 0x1C 0x0060b090
    // (Update 0x0060a920, Tick 0x0060a910 and slot 0x20 0x0060b160 are empty and SetFrame 0x0060a8c0
    // is the base's, as are the base's.)
    bool WriteProperties(C_chunk &chunk) override;      // 0x30 0x0060ac30
    bool ReadProperties(C_chunk &chunk) override;       // 0x34 0x0060ae40
    void CopyFrom(const C_Actor *source) override;      // 0x38 0x0060a8d0
    // Once until StopMission: the box volume, state 1.
    void StartMission(bool server) override;            // 0x3C 0x0060a930
    void StopMission(bool server) override;             // 0x40 0x0060ac00
    void RestartMission(bool) override { m_state = 1; } // 0x44 0x0060b1b0
    // Chunk 3000: the state (not the base's actor chunk).
    bool SaveGame(C_chunk &chunk) override;             // 0x48 0x0060b1c0
    bool LoadGame(C_chunk &chunk) override;             // 0x4C 0x0060b2c0
    // The part's "matid", else the frame's, else 11.
    int GetMaterialId(I3D_frame *part) override;        // 0xD8 0x0060b170 (DS 0x00633790)

private:
    // `human` carries the item and is of the side (Use, CanUse).
    bool Accepts(C_Human *human) const;

    bool m_started = false;                             // +0x90
    int m_sound = -1;                                   // +0x94 the sound when used (object manipulation group)
    int m_itemId = -1;                                  // +0x98
    int m_side = 2;                                     // +0x9C 0 / 1 (C_Human::GetTeam), 2 either
    int m_state = 0;                                    // +0xA0 (unset in the original's ctor)
    S_CollisionVolume m_volume;                         // +0xB0
    S_matrix m_invMatrix;                               // +0x110 the frame's inverse world matrix (the volume's)
};

// The radios' creator (g_actorCreators[ACTOR_RADIO], vtable 0x00813304).
class C_RadioCreator : public C_ActorCreator {
public:
    C_Actor *Create() override;                         // 0x0060a720
    const char *GetName() override { return "Radio"; }  // 0x0060a780
    void Destroy(C_Actor *actor) override;              // 0x0060a790
};
