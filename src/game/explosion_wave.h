// An explosion spreading through the world (0x006ddb50..0x006df880). S_Explosion::Execute collects what stands in its range; the "timed events"
// (TimedEvents_* in game/pending.h, the only kind there is) move the wave outward each tick and hit each thing as it reaches it.
#pragma once

#include <vector>

#include "ls3d/math.h"

class C_Actor;
class C_chunk;
class I3D_frame;
class I3D_sector;
struct S_Explosion;

// A frame in the wave's way, in the order the wave reaches them.
struct S_WaveVictim {                                   // 0x20 bytes
    S_vector4 offset;                                   // +0x00 from the explosion to the frame (only x, y, z count)
    I3D_frame *frame = nullptr;                         // +0x10 referenced
    float distance = 0.0f;                              // +0x14 squared, to the frame's box
    S_WaveVictim *next = nullptr;                       // +0x18
};

class C_ExplosionWave {                                 // 0x40 bytes
public:
    C_ExplosionWave() = default;                        // 0x006de370
    explicit C_ExplosionWave(const S_Explosion &explosion);   // 0x006de390 (DS 0x007270c0)
    ~C_ExplosionWave();                                 // 0x006de2b0
    C_ExplosionWave(const C_ExplosionWave &) = delete;
    C_ExplosionWave &operator=(const C_ExplosionWave &) = delete;

    // Moves on by `timeDelta` ms; false when nothing was left to reach.
    bool Step(int timeDelta);                           // 0x006deb50
    void Save(C_chunk &chunk) const;                    // 0x006dee10
    void Load(C_chunk &chunk);                          // 0x006df0b0

private:
    // (0x006de110; the original also passes the explosion's position, its own)
    void Hit(I3D_frame *frame, float amount, const S_vector &dir, bool lessEffects);
    bool IsHumanExposed(const C_Actor *human) const;    // 0x006de930

    S_vector4 m_pos = {};                               // +0x00
    float m_radius = 0.0f;                              // +0x10 grows 0.2 per ms; compared with the squared distances
    float m_damage = 0.0f;                              // +0x14
    float m_range = 0.0f;                               // +0x18 grows to the farthest victim's squared distance
    C_Actor *m_source = nullptr;                        // +0x1C
    S_WaveVictim *m_victims = nullptr;                  // +0x20
    bool m_sameSectorHumans = false;                    // +0x24 S_Explosion's flag: humans in its sector need no line of sight
    I3D_sector *m_sector = nullptr;                     // +0x28
    std::vector<C_Actor *> m_hit;                       // +0x2C actors hit already (each once)
};

// The actor a frame belongs to, or its model's (0x006de0d0).
C_Actor *GetFrameOrModelActor(I3D_frame *frame);
