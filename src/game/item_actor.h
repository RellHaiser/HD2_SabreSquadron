// An item lying in the world (C_ItemActor, actor type 11, vtable 0x00811aa8, 0x6C0 bytes; original
// 0x005b6710..0x005b7fd9): the item's own model as a rigid body, kept in g_temporaryActors -- the
// mission's placed items, the ones humans drop. Shots and explosions push it; the server sends
// where it came to rest. In multiplayer a dropped item disappears after 30 to 40 s (not the
// pick-ups), and a dropped pick-up goes back to its place after 15 s.
#pragma once

#include <cstdint>

#include "game/actor.h"
#include "game/rigid_body.h"

class C_Item;

class alignas(16) C_ItemActor : public C_Actor {        // "Item", vtable 0x00811aa8
public:
    C_ItemActor();                                      // 0x005b67d0
    ~C_ItemActor() override;                            // 0x005b6880 (scalar deleting 0x005b6860)

    // Puts `item` (the actor's frame is its model) into the world, starting with `velocity`.
    void SetupItem(C_Item *item, const S_vector &velocity);   // 0x005b69b0
    // The item lying there (set by SetupItem()).
    C_Item *GetItem() const { return m_item; }
    // Gives up the item (it was picked up) before the actor is destroyed.
    void ReleaseItem();                                 // 0x005b6980
    // The item goes back to its placement after `ms` (a dropped pick-up; the drop's server side).
    void SetRespawnTime(int ms) { m_respawnTimer = ms; }   // (inline in DropItem 0x00478ee0)
    // Moves the item onto `target` (the script's command) and tells the clients.
    void TeleportTo(I3D_frame *target);                 // 0x005b7920

    // The body's step and the hit sound's pause.
    void Update(int frameTime) override;                // 0x14 0x005b7480
    // The body's step (server only), the return to the placement and the end of the life.
    void Tick(int timeDelta) override;                  // 0x18 0x005b7320
    void OnMessage(C_BitStream *msg) override;          // 0x1C 0x005b7110
    void Unknown20(C_BitStream *) override {}           // 0x20 0x005b7300
    bool SetFrame(I3D_frame *frame) override;           // 0x2C 0x005b68d0
    bool WriteProperties(C_chunk &) override { return false; }   // 0x30 0x005b6830
    bool ReadProperties(C_chunk &) override { return false; }    // 0x34 0x005b6840
    void CopyFrom(const C_Actor *) override {}          // 0x38 0x005b6850
    void StartMission(bool server) override;            // 0x3C 0x005b6900
    // Stops the body and destroys the frame and the item with the actor.
    void StopMission(bool server) override;             // 0x40 0x005b6920
    void RestartMission(bool) override {}               // 0x44 0x005b6b70
    bool SaveGame(C_chunk &chunk) override;             // 0x48 0x005b6b80
    bool LoadGame(C_chunk &chunk) override;             // 0x4C 0x005b6e20
    void WriteState(C_BitStream *) override {}          // 0x50 0x005b70f0
    void ReadState(C_BitStream *) override {}           // 0x54 0x005b7100
    // A hit pushes the item (not the pick-ups, nor in its first 300 ms).
    void Hit(E_HitType type, float amount, const S_vector &pos, const S_vector &dir, C_Actor *attacker,
             int flags) override;                       // 0xD4 0x005b74d0
    int GetMaterialId(I3D_frame *) override { return 11; }   // 0xD8 0x005b74c0 (the default's value)

private:
    // The kinds of the item's messages (3 bits).
    enum : uint32_t {
        MSG_REMOVE = 0,                                 // its life ended
        MSG_PLACEMENT = 1,                              // where it rests: position, rotation
        MSG_PUSH = 2,                                   // an impulse: point, direction, length
        MSG_TELEPORT = 3,                               // onto the frame named
    };

    // The body from the item's model with `velocity` (SetupItem's and ResetBody's shared code).
    void StartBody(const S_vector &velocity);
    // The body anew from the frame's placement, at rest.
    void ResetBody();                                   // 0x005b7da0
    // Onto `target`, then into the world where that put it.
    void AttachTo(I3D_frame *target);                   // 0x005b76a0
    void OnTeleport(I3D_frame *target);                 // 0x005b79a0 (clients)
    // The impulse `impulse` at `point` (world space), and tells the clients.
    void Push(const S_vector &impulse, const S_vector &point);    // 0x005b79c0
    void OnPush(const S_vector &impulse, const S_vector &point);  // 0x005b7c30 (clients)
    void SendPlacement();                               // 0x005b7c50
    void OnPlacement(const S_vector &pos, const S_vector &axis, float angle);   // 0x005b7d50 (clients)
    // Back to the placement the item had in the mission, and tells the clients.
    void Respawn();                                     // 0x005b7ec0
    void PlayHitSound();                                // 0x005b7570
    // The body's callback (`context`: the actor): it passes through humans, sounds on other
    // contacts and sends where it came to rest.
    static int __fastcall OnContact(int event, int unused, void *context,
                                    const S_RigidBodyContact *contact);   // 0x005b7630

    C_Item *m_item = nullptr;                           // +0x90
    C_RigidBody m_body;                                 // +0xA0
    // (SetupItem sets the counters, StartMission the flag; the original leaves them unset until then.)
    int m_lifeTime = -1;                                // +0x6A0 ms left (-1: stays)
    int m_soundTimer = 0;                               // +0x6A4 ms until a hit sounds again
    bool m_fading = false;                              // +0x6A8 the fade-out has started
    int m_respawnTimer = -1;                            // +0x6AC ms until it goes back (-1: never)
    int m_age = 0;                                      // +0x6B0 ms since SetupItem (hits push it from 300 on)
};

// The item actors' creator (g_actorCreators[ACTOR_ITEM], vtable 0x00811a98).
class C_ItemActorCreator : public C_ActorCreator {
public:
    C_Actor *Create() override;                         // 0x005b6740
    const char *GetName() override { return "Item"; }   // 0x005b67a0
    void Destroy(C_Actor *actor) override;              // 0x005b67b0
};
