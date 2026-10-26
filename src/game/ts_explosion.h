// A timed scripted explosion (C_TSExplosion, "Explosion [TS]", actor type 38, vtable 0x008136d0,
// 0xD0 bytes; original 0x0060ed90..0x00610010). The script triggers it: the server counts its
// delay down and then explodes (area damage at the frame); the clients, told at once, count down
// too (a sound at the start) and then play the explosion's particles and sound and leave a scorch
// decal under it.
#pragma once

#include <cstdint>

#include "game/actor.h"

class IImage;

// C_TSExplosion::GetState() bits.
enum : uint32_t {
    TS_EXPLOSION_COUNTING = 0x1,                        // the server's countdown runs
    TS_EXPLOSION_CLIENT_COUNTING = 0x2,                 // a client's countdown runs
    TS_EXPLOSION_EXPLODED = 0x4,                        // the server's explosion is over
    TS_EXPLOSION_SCORCHED = 0x8,                        // a client's effects and decal are made
};

class alignas(16) C_TSExplosion : public C_Actor {      // "Explosion [TS]", vtable 0x008136d0
public:
    C_TSExplosion() : C_Actor(ACTOR_TS_EXPLOSION) {}    // 0x0060ee50
    ~C_TSExplosion() override = default;                // 0x0060ee90 (scalar deleting 0x0060ee70)

    // The script's start (the clients told; `unused`: the script VM passes 1).
    void Trigger(int unused);                           // 0x0060fb10
    // TS_EXPLOSION_* bits.
    int GetState() const { return static_cast<int>(m_flags); }   // +0x90

    // The server's countdown (and explosion).
    void Tick(int timeDelta) override;                  // 0x18 0x0060fab0
    // A client's countdown (and effects).
    void Update(int frameTime) override;                // 0x14 0x0060fae0
    // A client: the countdown starts.
    void OnMessage(C_BitStream *msg) override;          // 0x1C 0x0060f970
    // (Slot 0x20 0x0060f990 is empty, as the base's.)
    // The properties to their defaults, then the base's.
    bool SetFrame(I3D_frame *frame) override;           // 0x2C 0x0060eea0
    // Raw (no chunks): a version byte, then the properties.
    bool WriteProperties(C_chunk &chunk) override;      // 0x30 0x0060efd0
    bool ReadProperties(C_chunk &chunk) override;       // 0x34 0x0060f110
    // The properties from another TS explosion (not the base's).
    void CopyFrom(const C_Actor *source) override;      // 0x38 0x0060f350
    // The countdowns from 0; the client side loads the decal's texture (and is not activated
    // when that fails).
    void StartMission(bool server) override;            // 0x3C 0x0060eef0
    void StopMission(bool server) override;             // 0x40 0x0060ef60
    void RestartMission(bool server) override;          // 0x44 0x0060f3c0
    // Chunk 0xBE: chunk 0 the state, 1 a client's time, 2 the server's.
    bool SaveGame(C_chunk &chunk) override;             // 0x48 0x0060f420
    bool LoadGame(C_chunk &chunk) override;             // 0x4C 0x0060f640
    // One bit: triggered at all.
    void WriteState(C_BitStream *msg) override;         // 0x50 0x0060f9a0
    // Set: the decal, and with "particles on join" the particles.
    void ReadState(C_BitStream *msg) override;          // 0x54 0x0060f9c0

private:
    // The server's countdown over: the area damage 1 m above the frame.
    void ServerStep();                                  // 0x0060fd90
    // A client's countdown: its first step plays the countdown's sound; over, the particles,
    // the explosion's sound and the decal.
    void ClientStep();                                  // 0x0060fb60
    // The scorch decal 0.5 m above the frame, projected down.
    void MakeScorch();                                  // 0x0060fe50
    // The decal out of the scene (Fix: and released; the original leaks it).
    void RemoveScorch();
    // The particle effect `effect` at the frame (upward).
    void PlayParticles(int effect);

    uint32_t m_flags = 0;                               // +0x90 TS_EXPLOSION_*
    int m_countdownSound = -1;                          // +0x94 (explosion chapter group; -1: none)
    int m_delay = -1;                                   // +0x98 ms
    int m_clientTime = 0;                               // +0x9C ms
    int m_serverTime = 0;                               // +0xA0 ms
    int m_explosionSound = -1;                          // +0xA4 (not reset by SetFrame)
    float m_range = 0.0f;                               // +0xA8 m
    float m_damage = 0.0f;                              // +0xAC
    int m_particle = -1;                                // +0xB0
    int m_particle2 = -1;                               // +0xB4
    int m_joinParticles = 0;                            // +0xB8 ReadState plays the particles too
    I3D_frame *m_scorch = nullptr;                      // +0xBC the decal (a projector visual)
    IImage *m_texture = nullptr;                        // +0xC0 "x_expl00.tga"
};

// The TS explosions' creator (g_actorCreators[ACTOR_TS_EXPLOSION], vtable 0x008136c0).
class C_TSExplosionCreator : public C_ActorCreator {
public:
    C_Actor *Create() override;                         // 0x0060edc0
    const char *GetName() override { return "Explosion [TS]"; }   // 0x0060ee20
    void Destroy(C_Actor *actor) override;              // 0x0060ee30
};
