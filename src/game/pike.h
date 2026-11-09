// A pike (C_Pike, actor type 28, vtable 0x00811c90, 0x210 bytes; original 0x005b88c0..0x005ba100): a
// barrier the script raises and lowers. Its frame turns about an axis from its starting rotation, at
// a speed (rad/s) up to an angle; a turn that would hit something (rays along its box's edges) is
// undone and the barrier waits 0.2 s. Sounds when it starts and stops moving, a loop while it moves.
// A box volume (COLLISION_TYPE_PIKE) follows it.
#pragma once

#include <cstdint>

#include "game/actor.h"
#include "game/collision.h"
#include "game/sound_module.h"                          // S_SoundRequest
#include "ls3d/math.h"

enum : int {
    PIKE_DOWN = 0,                                      // at angle 0
    PIKE_UP = 1,                                        // at its largest angle
    PIKE_RAISING = 2,
    PIKE_LOWERING = 3,
};

class alignas(16) C_Pike : public C_Actor {             // "Pike", vtable 0x00811c90
public:
    C_Pike();                                           // 0x005b8d00
    ~C_Pike() override = default;                       // 0x005b8df0 (scalar deleting 0x005b8dd0)

    // The script: the frame's script told, then 2 raises it, 3 lowers it (the clients told);
    // others only go to the script.
    void SetState(int state);                           // 0x005b8ea0
    int GetState() const { return m_state; }            // +0x184

    // A client (or a local game): the turn, the end sounds, the volume.
    void Update(int frameTime) override;                // 0x14 0x005b9470
    // A server without a client side: the turn and the volume.
    void Tick(int timeDelta) override;                  // 0x18 0x005b9340
    // 0 raising, 1 lowering: the start sound and the loop.
    void OnMessage(C_BitStream *msg) override;          // 0x1C 0x005b9ec0
    void Unknown20(C_BitStream *) override {}           // 0x20 0x005b8dc0
    bool SetFrame(I3D_frame *frame) override;           // 0x2C 0x005b8e00
    // (WriteProperties 0x005b61e0 is the base's.) The state, the largest angle, the speed, the
    // axis and the three sounds, raw.
    bool ReadProperties(C_chunk &chunk) override;       // 0x34 0x005b9c80
    // The base's and the properties.
    void CopyFrom(const C_Actor *source) override;      // 0x38 0x005b8e20
    // Once until StopMission: the starting rotation kept, a server raises one that was up, the
    // volume, the sound's place.
    void StartMission(bool server) override;            // 0x3C 0x005b9670
    // The starting rotation back, the volume out, the loop stopped.
    void StopMission(bool server) override;             // 0x40 0x005b9bc0
    // Stopped and started on the server side.
    void RestartMission(bool server) override;          // 0x44 0x005b8990
    // Chunk 1234: the state, the wait, the angle.
    bool SaveGame(C_chunk &chunk) override;             // 0x48 0x005b89b0
    bool LoadGame(C_chunk &chunk) override;             // 0x4C 0x005b8b00
    // Raising or up: 0, else 1.
    void WriteState(C_BitStream *msg) override;         // 0x50 0x005ba000
    void ReadState(C_BitStream *msg) override;          // 0x54 0x005ba070
    void Hit(E_HitType, float, const S_vector &, const S_vector &, C_Actor *, int) override {}   // 0xD4 0x005b8980

private:
    // The clients told: 0 raising, 1 lowering.
    void SendState(uint32_t kind);                      // 0x005b9e70
    // The frame turned to the angle; true (the rotation back) when the box would hit something
    // (`time` is not used).
    bool Turn(int time);                                // 0x005b8f00 (DS 0x005cc0d0)
    // The volume made again around the frame (added to the world with `add`) and moved.
    void UpdateVolume(bool add);                        // 0x005b97b0 (DS 0x005ccba0)

    S_vector4 m_axis = {0.0f, 1.0f, 0.0f, 0.0f};        // +0x90 (w: a stale local in the original)
    S_quat m_startRot = {1.0f, 0.0f, 0.0f, 0.0f};       // +0xA0 the frame's rotation at StartMission
    S_matrix m_invMatrix;                               // +0xB0 the volume's
    S_matrix m_matrix;                                  // +0xF0 the frame's world matrix without its scale
    bool m_started = false;                             // +0x130
    S_SoundRequest m_sound;                             // +0x140 (object manipulation chapter, at the frame)
    int m_loopSound = -1;                               // +0x180 the moving loop's slot
    int m_state = PIKE_DOWN;                            // +0x184
    int m_blockedTime = 0;                              // +0x188 ms left to wait after a blocked turn
    float m_maxAngle = 1.5707964f;                      // +0x18C rad
    float m_speed = 1.5707964f;                         // +0x190 rad/s
    int m_lowSound = -1;                                // +0x194 starting up, arriving down (-1: none)
    int m_highSound = -1;                               // +0x198 starting down, arriving up
    int m_moveSound = -1;                               // +0x19C the loop while moving
    float m_angle = 0.0f;                               // +0x1A0 rad
    S_CollisionVolume m_volume;                         // +0x1B0
};

// The pikes' creator (g_actorCreators[ACTOR_PIKE], vtable 0x00811c84).
class C_PikeCreator : public C_ActorCreator {
public:
    C_Actor *Create() override;                         // 0x005b88f0
    const char *GetName() override { return "Pike"; }   // 0x005b8950
    void Destroy(C_Actor *actor) override;              // 0x005b8960
};
