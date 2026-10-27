// A school of fish (C_Fish, actor type 32, vtable 0x00812538, 0xE0 bytes; original
// 0x005db780..0x005de650): copies of the "ryba" model's "ryba" frame swimming around the actor's
// frame. The school speeds up along its direction, slows down, waits a few seconds and turns
// (within 135 degrees each way, up or down by 27), sinking or rising back to level as it goes,
// sliding along what it meets; each fish keeps its place in the school, which spreads out while
// the school goes fast and draws in when it turns, and sways about it. All this only while the
// camera is within the school's radius + 15 m (the school's frame is hidden otherwise).
#pragma once

#include <cstdint>
#include <vector>

#include "game/actor.h"
#include "ls3d/math.h"

class alignas(16) C_Fish : public C_Actor {             // "Fish", vtable 0x00812538
public:
    C_Fish();                                           // 0x005dbd80
    // (The fish are not deleted: StopMission does that.)
    ~C_Fish() override = default;                       // 0x005dbe20 (scalar deleting 0x005dbe00)

    // The fish move and sway, the school's frame along the velocity Tick set.
    void Update(int frameTime) override;                // 0x14 0x005dcb10
    // The speed and direction: speeding up to the top speed, slowing to a tenth of it, a pause
    // of 3..5 s and a new direction; the velocity the frame moves at until the next tick.
    void Tick(int timeDelta) override;                  // 0x18 0x005dbec0
    void OnMessage(C_BitStream *) override {}           // 0x1C 0x005dbde0
    void Unknown20(C_BitStream *) override {}           // 0x20 0x005dbdf0
    // A frame only (null: false), then the base's.
    bool SetFrame(I3D_frame *frame) override { return frame && C_Actor::SetFrame(frame); }   // 0x2C 0x005dbe60
    // (WriteProperties 0x005b61e0 is the base's.) The top speed, the radius and the count, raw.
    bool ReadProperties(C_chunk &chunk) override;       // 0x34 0x005de300
    // (Null: nothing.) The base's, the top speed, the count, the radius.
    void CopyFrom(const C_Actor *source) override;      // 0x38 0x005dbe80
    // Once until StopMission: the fish made ("ryba" from the model cache), the direction the
    // frame's (made level), waiting 1..2 s.
    void StartMission(bool server) override;            // 0x3C 0x005dd900
    // The fish deleted, the frame shown.
    void StopMission(bool server) override;             // 0x40 0x005de260
    // Stopped and started on the server side (whatever `server` is).
    void RestartMission(bool server) override;          // 0x44 0x005db870
    // Chunk 9999 holding 1000 the direction, 2000 the state, 3000 the pause, 4000 the time,
    // 5000 the speed and 6000 the velocity.
    bool SaveGame(C_chunk &chunk) override;             // 0x48 0x005db890
    bool LoadGame(C_chunk &chunk) override;             // 0x4C 0x005dba50

private:
    struct alignas(16) S_FishMember {                   // 0x30 bytes (_aligned_malloc'd, zeroed)
        S_vector4 pos = {};                             // +0x00 its place in the school (the frame's space)
        S_vector4 target = {};                          // +0x10 where the place is going
        I3D_frame *frame = nullptr;                     // +0x20 the copy of "ryba", linked to the school's frame
        float phase = 0.0f;                             // +0x24 of the sway (0..2 pi)
        float rate = 0.0f;                              // +0x28 of the sway (0.5..1)
    };

    // The school's frame shown while the camera is within the radius + 15 m (true), hidden
    // otherwise (false).
    bool UpdateVisibility();                            // 0x005dca60 (__fastcall)

    S_vector4 m_dir = {};                               // +0x90 the direction the school swims
    S_vector4 m_velocity = {};                          // +0xA0 m/s (the school's frame, until the next tick)
    int m_state = 0;                                    // +0xB0 FISH_* (fish.cpp)
    int m_pause = 0;                                    // +0xB4 ms left (FISH_PAUSE)
    int m_time = 0;                                     // +0xB8 ms since StartMission (the sway)
    float m_topSpeed = 1.0f;                            // +0xBC m/s
    float m_radius = 2.0f;                              // +0xC0 m (the school; the slide's sphere)
    int m_count = 10;                                   // +0xC4 fish
    bool m_started = false;                             // +0xC8
    float m_speed = 0.0f;                               // +0xCC m/s
    std::vector<S_FishMember *> m_fish;                 // +0xD0
};

// The fish's creator (g_actorCreators[ACTOR_FISH], vtable 0x0081252c).
class C_FishCreator : public C_ActorCreator {
public:
    C_Actor *Create() override;                         // 0x005db7e0
    const char *GetName() override { return "Fish"; }   // 0x005db840
    void Destroy(C_Actor *actor) override;              // 0x005db850
};
