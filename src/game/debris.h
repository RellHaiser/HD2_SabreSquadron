// Debris (C_Debris, actor type 12, vtable 0x00812e18, 0x6A0 bytes; original 0x005fd9f0..0x005fddc0):
// a copy of a visual thrown off a car (its parts, a part's lamp, a wheel) as a rigid body, kept
// in g_temporaryActors (CreateDebris). A part's debris fades out and is removed after 30 to 40 s;
// a wheel's stays.
#pragma once

#include <cstdint>

#include "game/actor.h"
#include "game/rigid_body.h"

class alignas(16) C_Debris : public C_Actor {           // "Debris", vtable 0x00812e18
public:
    C_Debris();                                         // 0x005fdab0
    ~C_Debris() override;                               // 0x005fdb50 (scalar deleting 0x005fdb30)

    // A box: the body set up from `params` on the actor's frame, a life of 30 to 40 s, fading out
    // at its end when `fadeOut` (else it stays).
    void Setup(const S_DebrisParams &params, bool fadeOut);   // 0x005fdc50
    // A rolling disc (a car's wheel); it never fades out.
    void SetupWheel(const S_WheelDebrisParams &params); // 0x005fdc00

    // The body's step, then the life: half a second before its end the model starts to fade, at
    // its end the actor is removed (with fade-out).
    void Update(int frameTime) override;                // 0x14 0x005fdd10
    void Tick(int) override {}                          // 0x18 0x005fdd00
    void OnMessage(C_BitStream *) override {}           // 0x1C 0x005fdce0
    void Unknown20(C_BitStream *) override {}           // 0x20 0x005fdcf0
    bool SetFrame(I3D_frame *frame) override;           // 0x2C 0x005fdba0
    bool WriteProperties(C_chunk &) override { return false; }   // 0x30 0x005fdb00 (DS 0x00622ab0)
    bool ReadProperties(C_chunk &) override { return false; }    // 0x34 0x005fdb10 (DS 0x00622ad0)
    void CopyFrom(const C_Actor *) override {}          // 0x38 0x005fdb20
    void StartMission(bool server) override;            // 0x3C 0x005fdbb0
    void StopMission(bool server) override;             // 0x40 0x005fdbd0
    void RestartMission(bool) override {}               // 0x44 0x005fdcb0
    // (Debris is not saved.)
    bool SaveGame(C_chunk &) override { return false; } // 0x48 0x005fdcc0 (DS 0x00622d50)
    bool LoadGame(C_chunk &) override { return false; } // 0x4C 0x005fdcd0 (DS 0x00622d70)
    int GetMaterialId(I3D_frame *) override { return 11; }   // 0xD8 0x005fddc0 (the default's value) (DS 0x00622ed0)

private:
    C_RigidBody m_body;                                 // +0x90
    // Fix: the three are set here; the original leaves them unset (a wheel's debris, which
    // SetupWheel never gives the fade-out flag, then fades and goes away or stays at random).
    int m_lifeTime = 0;                                 // +0x690 ms left (-1: counted out)
    bool m_fading = false;                              // +0x694 the fade-out has started
    bool m_fadeOut = false;                             // +0x695 fade out and go at the end
};

// Debris's creator (g_actorCreators[ACTOR_DEBRIS], vtable 0x00812e08).
class C_DebrisCreator : public C_ActorCreator {
public:
    C_Actor *Create() override;                         // 0x005fda20
    const char *GetName() override { return "Debris"; } // 0x005fda80
    void Destroy(C_Actor *actor) override;              // 0x005fda90
};
