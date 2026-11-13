// A clock (C_Clock, actor type 27, vtable 0x00811b98, 0xE0 bytes; original 0x005b8010..0x005b8860):
// a model whose "hour", "min" and "sec" frames turn about their z axes with the time since the
// mission started; the hands move once a second.
#pragma once

#include <cstdint>

#include "game/actor.h"
#include "ls3d/math.h"

class alignas(16) C_Clock : public C_Actor {            // "Clock", vtable 0x00811b98
public:
    C_Clock() : C_Actor(ACTOR_CLOCK) {}                 // 0x005b8270 (DS 0x005cb000)
    ~C_Clock() override = default;                      // 0x005b82b0 (DS 0x005cb060) (scalar deleting 0x005b8290, DS 0x005cb030)

    // The time runs on by `frameTime` ms; with a second hand, once over a second has gathered,
    // the hands turn to the time (quirk kept: without a second hand none of them turns).
    void Update(int frameTime) override;                // 0x14 0x005b8610
    // The hands are forgotten (StartMission finds them).
    bool SetFrame(I3D_frame *frame) override;           // 0x2C 0x005b8240
    void CopyFrom(const C_Actor *source) override { C_Actor::CopyFrom(source); }   // 0x38 0x005b82c0
    // The client side: the hands found, their rotations kept, the time from 0. (Neither this nor
    // StopMission activates the actor.)
    void StartMission(bool server) override;            // 0x3C 0x005b82d0
    // The client side: the hands back at their kept rotations.
    void StopMission(bool server) override;             // 0x40 0x005b84e0
    void RestartMission(bool server) override;          // 0x44 0x005b80d0
    // The time as two raw floats (no chunk; not the base's actor chunk).
    bool SaveGame(C_chunk &chunk) override;             // 0x48 0x005b80f0
    bool LoadGame(C_chunk &chunk) override;             // 0x4C 0x005b8150

private:
    // EnumFrames callback (`context`: the clock): the hands by their names.
    static uint32_t __stdcall FindHandsProc(I3D_frame *frame, void *context);   // 0x005b8430

    S_quat m_hourRot;                                   // +0x90 the hands' rotations at the start
    S_quat m_minuteRot;                                 // +0xA0
    S_quat m_secondRot;                                 // +0xB0
    float m_time = 0.0f;                                // +0xC0 seconds since the start
    float m_tick = 0.0f;                                // +0xC4 seconds gathered since the hands moved
    // (The original leaves the hands unset until SetFrame.)
    I3D_frame *m_hour = nullptr;                        // +0xC8 "hour"
    I3D_frame *m_minute = nullptr;                      // +0xCC "min"
    I3D_frame *m_second = nullptr;                      // +0xD0 "sec"
};

// The clocks' creator (g_actorCreators[ACTOR_CLOCK], vtable 0x00811b8c).
class C_ClockCreator : public C_ActorCreator {
public:
    C_Actor *Create() override;                         // 0x005b8040
    const char *GetName() override { return "Clock"; }  // 0x005b80a0
    void Destroy(C_Actor *actor) override;              // 0x005b80b0
};
