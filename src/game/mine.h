// A mine (C_Mine, actor type 2, vtable 0x00812f08, 0x120 bytes; original 0x005fde00..0x005ff150): a
// temporary actor laid by a human (or placed for the mission) from a mine item. Two seconds after it
// is laid it arms: a sphere around it (COLLISION_TYPE_MINE) that a human's feet or a car's wheels
// meet (OnSteppedOn), and it explodes with its item's damage and range. Item 0x47 (radius 0.4 m)
// ignores humans, 0x46 (0.15 m) ignores cars; the layer's own steps never set it off.
#pragma once

#include <cstdint>
#include <vector>

#include "game/actor.h"
#include "game/collision.h"

class C_Human;

class alignas(16) C_Mine : public C_Actor {             // "Mine", vtable 0x00812f08
public:
    C_Mine() : C_Actor(ACTOR_MINE) {}                   // 0x005fdec0
    ~C_Mine() override = default;                       // 0x005fdf60 (scalar deleting 0x005fdf40)

    // Laid by `owner` (null: the mission's) from item `itemId`: sunk into the ground along the
    // frame's z axis (0.07 m for item 0x46, 0.08 m for 0x47), shown, named after the item; it arms
    // two seconds later. `materialId` is its material (callers pass -1).
    void Setup(C_Human *owner, int itemId, int materialId);   // 0x005fe010
    int GetItemId() const { return m_itemId; }          // +0x90
    // Who laid it (null for mines placed in the mission).
    C_Human *GetOwner() const { return m_owner; }       // +0x9C
    // Disarms it (and tells the clients; a server removes it).
    void Defuse();                                      // 0x005feee0 (__fastcall)
    // A human stepped on the sphere, or a car's wheel met it (the culprit: the layer, else the
    // human or the car's driver): it explodes (servers remove it, the clients are told).
    void OnSteppedOn(C_Actor *actor, int unused);       // 0x005fefb0 (the human passes -1, the car the wheel)

    // The arming countdown: Update on clients, Tick on a server without a client side.
    void Update(int frameTime) override;                // 0x14 0x005fee90
    void Tick(int timeDelta) override;                  // 0x18 0x005fee70
    // A client: 0 the explosion's effects, 1 removed (defused).
    void OnMessage(C_BitStream *msg) override;          // 0x1C 0x005feb50
    // (Slot 0x20 0x005feba0 is empty, as the base's.)
    // The frame is named "Actor_mine".
    bool SetFrame(I3D_frame *frame) override;           // 0x2C 0x005fdf90
    bool WriteProperties(C_chunk &) override { return false; }   // 0x30 0x005fdf10 (DS 0x006230d0)
    bool ReadProperties(C_chunk &) override { return false; }    // 0x34 0x005fdf20 (DS 0x006230f0)
    void CopyFrom(const C_Actor *) override {}          // 0x38 0x005fdf30 (not even the base's)
    void StartMission(bool server) override;            // 0x3C 0x005fdfb0
    void StopMission(bool server) override;             // 0x40 0x005fdfd0
    void RestartMission(bool) override {}               // 0x44 0x005fe430
    // Chunk 1: the place, the direction, the countdown and the flags; chunk 2: the layer's id.
    bool SaveGame(C_chunk &chunk) override;             // 0x48 0x005fe440
    // Quirk kept: false (the module does not look).
    bool LoadGame(C_chunk &chunk) override;             // 0x4C 0x005fe800
    int GetMaterialId(I3D_frame *) override { return m_materialId; }   // 0xD8 0x005feeb0

private:
    // The sphere 0.2 m above the frame, added; the countdown over.
    void Arm();                                         // 0x005febb0
    // The countdown runs on by `time` ms (while it runs).
    void CountDown(int time);                           // 0x005fee50
    void Advance(int time);                             // 0x005fee10
    // Defused on the server: taken off the temporary list.
    void Remove();                                      // 0x005feec0
    // The explosion's sound and effects at the sphere (a client), then taken off the list.
    void Detonate();                                    // 0x005fef50

    int m_itemId = 0;                                   // +0x90
    int m_armTime = 0;                                  // +0x94 ms since it was laid; -1 once armed
    bool m_armed = false;                               // +0x98
    bool m_volumeAdded = false;                         // +0x99
    C_Human *m_owner = nullptr;                         // +0x9C
    S_CollisionVolume m_volume;                         // +0xA0 a sphere
    int m_materialId = 0;                               // +0x100
    std::vector<int> m_unknown104;                      // +0x104 (not used by the mine's code)
};

// The mines' creator (g_actorCreators[ACTOR_MINE], vtable 0x00812ef8).
class C_MineCreator : public C_ActorCreator {
public:
    C_Actor *Create() override;                         // 0x005fde30
    const char *GetName() override { return "Mine"; }   // 0x005fde90
    void Destroy(C_Actor *actor) override;              // 0x005fdea0
};
