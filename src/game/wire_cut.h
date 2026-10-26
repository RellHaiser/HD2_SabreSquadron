// A breakable fence (C_WireCut, actor type 35, vtable 0x00812750, 0xB0 bytes; original
// 0x005e2070..0x005e2a10): made for each breakable fence of the scene when the mission starts
// (C_TemporaryActorsList::CreateBreakables), on the fence's physics door. Wire cutters or the
// script cut it: the door breaks along the cut, the frame's script and the clients are told, and
// in a local game the walking points on either side of the fence are joined through the gap.
#pragma once

#include <cstdint>

#include "game/actor.h"
#include "game/checkpoints.h"
#include "ls3d/math.h"

class C_PhysDoor;

class alignas(16) C_WireCut : public C_Actor {          // "WireCut", vtable 0x00812750
public:
    C_WireCut();                                        // 0x005e2130
    ~C_WireCut() override = default;                    // 0x005e2240 (scalar deleting 0x005e2220)

    // Cuts the fence along `dir`; `hideBrokenPart` also hides the door's broken part (the script
    // VM: true; the AI and loading: false). False when the door does not break.
    bool Cut(const S_vector &dir, bool hideBrokenPart); // 0x005e2510
    bool IsCut() const;                                 // 0x005e24c0
    void SetDoor(C_PhysDoor *door) { m_door = door; }
    // The walking points on either side of the fence (CHECKPOINT_NONE: none).
    int GetPoint(int side) const { return m_points[side]; }

    // A client: the server's cut (its direction).
    void OnMessage(C_BitStream *msg) override;          // 0x1C 0x005e25f0
    // (Slot 0x20 0x005e2620 is empty, as the base's; SetFrame 0x005e2250 is the base's.)
    void CopyFrom(const C_Actor *source) override { C_Actor::CopyFrom(source); }   // 0x38 0x005e2200
    // The server side finds the points on either side (a local game only).
    void StartMission(bool server) override;            // 0x3C 0x005e2260
    void StopMission(bool server) override;             // 0x40 0x005e23e0
    void RestartMission(bool) override {}               // 0x44 0x005e2210
    // The cut's direction in chunk 1000 (not the base's actor chunk).
    bool SaveGame(C_chunk &chunk) override;             // 0x48 0x005e2630
    // A saved cut is made again (the door breaks, the clients and the script are told again).
    bool LoadGame(C_chunk &chunk) override;             // 0x4C 0x005e2730
    void WriteState(C_BitStream *msg) override;         // 0x50 0x005e2930
    void ReadState(C_BitStream *msg) override;          // 0x54 0x005e29a0
    // The part's "matid", else 11.
    int GetMaterialId(I3D_frame *part) override;        // 0xD8 0x005e2910

private:
    // A client: the door breaks along `dir` (no hiding, script or state of its own).
    void OnCut(const S_vector &dir);                    // 0x005e25c0
    // A local game: the two points joined, both of type CHECKPOINT_TYPE_FENCE_GAP.
    void OpenWay();                                     // 0x005e2480
    void SetPointsType(uint8_t type);                   // 0x005e2420

    C_PhysDoor *m_door = nullptr;                       // +0x90
    uint16_t m_points[2] = {CHECKPOINT_NONE, CHECKPOINT_NONE};   // +0x94
    S_vector m_cutDir = {0.0f, 0.0f, 0.0f};             // +0xA0 (4 floats in the original) 0: not cut
};

// The fences' creator (g_actorCreators[ACTOR_WIRE_CUT], vtable 0x00812740).
class C_WireCutCreator : public C_ActorCreator {
public:
    C_Actor *Create() override;                         // 0x005e20a0
    const char *GetName() override { return "WireCut"; }   // 0x005e2100
    void Destroy(C_Actor *actor) override;              // 0x005e2110
};
