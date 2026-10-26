// A switch (C_Switch, actor type 13, vtable 0x008139d0, 0x150 bytes; original
// 0x006131d0..0x006140c0): a lever or the like with states 1 to 3, each
// a child frame named by its number ("1", "2", ...) shown in the place of "1". Humans flip it to
// the next state (Toggle), the script sets it; on the server the frame's script hears of each
// change, and a sound of the object manipulation chapter can mark it.
#pragma once

#include <cstdint>

#include "game/actor.h"
#include "game/collision.h"
#include "ls3d/math.h"

class alignas(16) C_Switch : public C_Actor {           // "Switch", vtable 0x008139d0
public:
    C_Switch();                                         // 0x00613450
    ~C_Switch() override = default;                     // 0x00613540 (scalar deleting 0x00613520)

    // The state (out of 1..3: 1) shown, the script told (server, unless quiet), the sound played.
    void SetState(int state);                           // 0x006136f0
    int GetState() const { return m_state; }            // +0xD4
    // To the next state; after the last state frame back to 1.
    void Toggle();                                      // 0x00613640 (__fastcall)

    // The state raw (the base's actor chunk is not written).
    bool Unknown24(C_chunk &chunk) override;            // 0x24 0x006133a0
    bool Unknown28(C_chunk &chunk) override;            // 0x28 0x006133d0
    // The state frames counted and placed at "1"; state 0 (that is 1) set.
    bool SetFrame(I3D_frame *frame) override;           // 0x2C 0x00613550 (the rest split off at 0x00613567)
    bool WriteProperties(C_chunk &chunk) override;      // 0x30 0x00613c10
    bool ReadProperties(C_chunk &chunk) override;       // 0x34 0x00613e10
    void CopyFrom(const C_Actor *source) override;      // 0x38 0x006136c0
    // The box volume (on the server side, or a client's); the state kept for StopMission.
    void StartMission(bool server) override;            // 0x3C 0x00613920
    void StopMission(bool server) override;             // 0x40 0x00613bd0
    // Quietly (the script is not told).
    void RestartMission(bool server) override;          // 0x44 0x00613290
    bool SaveGame(C_chunk &chunk) override;             // 0x48 0x006132b0
    bool LoadGame(C_chunk &chunk) override;             // 0x4C 0x006132f0
    void WriteState(C_BitStream *msg) override;         // 0x50 0x00613680
    void ReadState(C_BitStream *msg) override;          // 0x54 0x006136a0
    // (Hit 0xD4 0x00613390 is empty, as the base's.)

private:
    S_matrix m_invMatrix;                               // +0x90 the frame's inverse world matrix (the volume's)
    int m_initialState = 0;                             // +0xD0 the state at StartMission
    int m_state = 0;                                    // +0xD4 1..3
    int m_sound = -1;                                   // +0xD8 "Sound" (object manipulation group; -1: none)
    int m_stateCount = 0;                               // +0xDC frames "1", "2", ...
    bool m_quiet = false;                               // +0xE0 the script is not told (unset in the original's ctor)
    S_CollisionVolume m_volume;                         // +0xF0
};

// The switches' creator (g_actorCreators[ACTOR_SWITCH], vtable 0x008139c0).
class C_SwitchCreator : public C_ActorCreator {
public:
    C_Actor *Create() override;                         // 0x00613200
    const char *GetName() override { return "Switch"; } // 0x00613260
    void Destroy(C_Actor *actor) override;              // 0x00613270
};
