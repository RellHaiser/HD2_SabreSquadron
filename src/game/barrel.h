// A barrel (C_Barrel, actor type 5, vtable 0x00813220, 0x210 bytes; original 0x00606250..0x0060a6f0):
// a barrel of liquid whose surface ("dekl") stands at the level. Shots through its body make holes
// below the level (a leak particle each, sorted by height; with "exit holes" also where the shot
// leaves); the level sinks by the leak rate a second for each hole until it is empty, the holes
// above it stopping. Shots and bumps slosh the surface (and bumps tip the "body" by 5 degrees and
// back). Its hit points used up while the level is over the explosion level, it is destroyed: the
// model of the destroyed barrel ("<name>_destroyed") in its place, an explosion, the frame's script
// told. Its box (COLLISION_TYPE_BARREL) is on the server. Clients send what hit them to the
// server, which plays it.
#pragma once

#include <cstdint>
#include <vector>

#include "common/c_str.h"
#include "game/actor.h"
#include "game/collision.h"
#include "ls3d/math.h"

class C_BitStream;
class C_chunk;
class I3D_frame;
class I3D_model;

class alignas(16) C_Barrel : public C_Actor {           // "Barrel", vtable 0x00813220
public:
    C_Barrel();                                         // 0x00606380
    ~C_Barrel() override;                               // 0x00606490 (scalar deleting 0x00606430)

    // The body's tip, the surface's slosh, the leak (Update always; Tick: also, but on a pure
    // client).
    void Update(int frameTime) override { Animate(frameTime); }   // 0x14 0x006071b0
    void Tick(int timeDelta) override;                  // 0x18 0x006071c0
    // Kind (3 bits) 0 a shot through the body, 1 a bump (the body too with its bit), 2 destroyed,
    // 3 a shot into the surface.
    void OnMessage(C_BitStream *msg) override;          // 0x1C 0x00609220
    bool SetFrame(I3D_frame *frame) override { return C_Actor::SetFrame(frame); }   // 0x2C 0x00606360 (DS 0x0062d850)
    // Chunks 2000 the surface's name, 3000 the full level, 4000 the empty level, 5000 the leak
    // rate, 6000 the leak effect, 7000 the body's name, 8000 the destroyed model, 9000 the hit
    // points, 10000 the box out when destroyed, 11000 the explosion effect, 12000 the explosion
    // level, 13000 exit holes. Quirk kept: a chunk of another id is entered and not left.
    bool ReadProperties(C_chunk &chunk) override;       // 0x34 0x0060a280
    void CopyFrom(const C_Actor *source) override;      // 0x38 0x00606520
    // The box (the server), the level full, the surface and body found (their start placement),
    // the hit points, the radius.
    void StartMission(bool server) override;            // 0x3C 0x00606650
    // Everything back (the destroyed model away); the holes forgotten (quirk kept: their
    // particles are not stopped).
    void StopMission(bool server) override;             // 0x40 0x00606bb0
    void RestartMission(bool) override;                 // 0x44 0x00606340
    // Chunk 1 holding the base's chunk, 1000 empty, 2000 the level (the lowest hole's height when
    // it leaks), 3000 the hit points (with hit points at all).
    bool SaveGame(C_chunk &chunk) override;             // 0x48 0x00609a30
    bool LoadGame(C_chunk &chunk) override;             // 0x4C 0x00609c70
    // A bump: the surface sloshes and the body tips away from the other actor.
    bool OnCollisionEx(C_Actor *other, const S_vector *pos, int, int, int, int, int) override;   // 0x5C 0x00609510
    // Damage (destroyed: see above); then a bullet into the surface (`part` it) or a bullet / an
    // explosion into the body: holes and a slosh.
    void Hit(E_HitType type, float amount, const S_vector &pos, const S_vector &dir, C_Actor *attacker,
             int part) override;                        // 0xD4 0x00608e40
    // The part's material, else 11.
    int GetMaterialId(I3D_frame *part) override;        // 0xD8 0x006094f0

private:
    struct S_Hole {                                     // 8 bytes (new)
        float height;                                   // in the surface's parent's space
        int particle;
    };

    void Animate(int time);                             // 0x00606da0
    // The surface at the level.
    void SetSurfaceLevel();                             // 0x00606b10
    // The slosh from a hit or bump at `pos` (the body's space) along `dir`: the body tipped (with
    // `tipBody`), the surface's two extremes 1 degree either way of level about the hit's axis.
    void Slosh(const S_vector &pos, const S_vector &dir, bool tipBody);   // 0x00607410
    // Destroyed (`loading`: a saved game; no script, no explosion).
    void Destroy(bool loading);                         // 0x00607930
    // A hole at `pos` (world) at `height`, sorted in.
    void AddHole(const S_vector &pos, float height);    // 0x00608150
    // Between the empty level and the level.
    bool IsLeakHeight(float height) const;              // 0x006083c0
    void HitBody(const S_vector &pos, const S_vector &dir);    // 0x00608590
    void HitSurface(const S_vector &pos, const S_vector &dir); // 0x00608a30
    // The message `kind` (3 bits) with its vectors to the server.
    C_BitStream *CreateMessage(uint32_t kind);

    S_quat m_surfaceRot = {};                           // +0x90 the surface's start rotation
    S_quat m_sloshFrom = {};                            // +0xA0
    S_quat m_sloshTo = {};                              // +0xB0
    float m_emptyLevel = 0.1f;                          // +0xC0
    float m_fullLevel = 1.0f;                           // +0xC4
    float m_level = 0.0f;                               // +0xC8 the surface's height
    float m_leakRate = 0.1f;                            // +0xCC a hole a second
    float m_sloshAmount = 0.0f;                         // +0xD0
    float m_sloshPhase = 0.0f;                          // +0xD4 (the last cosine)
    bool m_empty = false;                               // +0xD8
    bool m_sloshing = false;                            // +0xD9
    bool m_sloshDamped = false;                         // +0xDA
    int m_leakEffect = 90;                              // +0xDC
    int m_sloshTime = 0;                                // +0xE0 ms
    C_str m_surfaceName = "dekl";                       // +0xE4
    I3D_frame *m_surface = nullptr;                     // +0xE8
    S_quat m_bodyRot = {};                              // +0xF0 the body's start rotation
    S_quat m_bodyTipRot = {};                           // +0x100
    S_vector m_bodyPos = {0.0f, 0.0f, 0.0f};            // +0x110 its start position (w not needed)
    I3D_frame *m_body = nullptr;                        // +0x120
    C_str m_bodyName = "body";                          // +0x124
    bool m_bodyTipping = false;                         // +0x128
    bool m_bodyTippingBack = false;                     // +0x129
    int m_bodyTipTime = 0;                              // +0x12C ms
    C_str m_destroyedModel;                             // +0x130 file
    int m_explosionEffect = 20;                         // +0x134
    int m_hitPoints = 1000;                             // +0x138
    float m_health = 0.0f;                              // +0x13C
    float m_explosionLevel = 0.2f;                      // +0x140
    bool m_volumeOutWhenDestroyed = false;              // +0x144
    I3D_model *m_destroyed = nullptr;                   // +0x148
    S_matrix m_invMatrix = {};                          // +0x150
    S_CollisionVolume m_volume;                         // +0x190
    std::vector<S_Hole *> m_holes;                      // +0x1F0 lowest first
    bool m_exitHoles = false;                           // +0x200
    float m_radius = 0.0f;                              // +0x204 (where shots leave it)
};

// The barrels' creator (g_actorCreators[ACTOR_BARREL], vtable 0x00813210).
class C_BarrelCreator : public C_ActorCreator {
public:
    C_Actor *Create() override;                         // 0x006062b0
    const char *GetName() override { return "Barrel"; } // 0x00606310
    void Destroy(C_Actor *actor) override;              // 0x00606320
};
