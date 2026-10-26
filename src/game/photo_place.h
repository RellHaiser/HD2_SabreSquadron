// A place to photograph (C_PhotoPlace, actor type 15, vtable 0x00813400, 0x140 bytes; original
// 0x0060b610..0x0060c170): an objective a human takes a photo of. Its name is a text of its
// properties; it has a box volume around its frame (COLLISION_TYPE_PLACE).
#pragma once

#include <cstdint>

#include "game/actor.h"
#include "game/collision.h"
#include "ls3d/math.h"

class C_Human;

class alignas(16) C_PhotoPlace : public C_Actor {       // "Photo place", vtable 0x00813400
public:
    C_PhotoPlace();                                     // 0x0060b6d0
    ~C_PhotoPlace() override = default;                 // 0x0060b7c0 (scalar deleting 0x0060b7a0)

    // 1 once photographed.
    int GetState() const { return m_photographed; }     // byte +0x91
    // `human` photographs it (`human` is not used).
    void TakePhoto(C_Human *) { m_photographed = true; }   // 0x0060c130

    // (Update 0x0060b810, Tick 0x0060b800, OnMessage 0x0060be20 and slot 0x20 0x0060be30 are
    // empty and SetFrame 0x0060b7d0 is the base's, as are the base's.)
    bool WriteProperties(C_chunk &chunk) override;      // 0x30 0x0060bb80
    bool ReadProperties(C_chunk &chunk) override;       // 0x34 0x0060bc80
    void CopyFrom(const C_Actor *source) override;      // 0x38 0x0060b7e0
    // Once until StopMission: not photographed, the name from its text, the box volume.
    void StartMission(bool server) override;            // 0x3C 0x0060b820
    void StopMission(bool server) override;             // 0x40 0x0060bb50
    // Not photographed any more (nothing else).
    void RestartMission(bool) override { m_photographed = false; }   // 0x44 0x0060be80
    // Chunk 3000: photographed (not the base's actor chunk).
    bool SaveGame(C_chunk &chunk) override;             // 0x48 0x0060be90
    bool LoadGame(C_chunk &chunk) override;             // 0x4C 0x0060bf90
    // The part's "matid", else the frame's, else 11.
    int GetMaterialId(I3D_frame *part) override;        // 0xD8 0x0060be40

private:
    bool m_started = false;                             // +0x90
    bool m_photographed = false;                        // +0x91
    int m_textId = -1;                                  // +0x94 the name's text
    S_CollisionVolume m_volume;                         // +0xA0
    S_matrix m_invMatrix;                               // +0x100 the frame's inverse world matrix (the volume's)
};

// The photo places' creator (g_actorCreators[ACTOR_PHOTO_PLACE], vtable 0x008133f0).
class C_PhotoPlaceCreator : public C_ActorCreator {
public:
    C_Actor *Create() override;                         // 0x0060b640
    const char *GetName() override { return "Photo place"; }   // 0x0060b6a0
    void Destroy(C_Actor *actor) override;              // 0x0060b6b0
};
