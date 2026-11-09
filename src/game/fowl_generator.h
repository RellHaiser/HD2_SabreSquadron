// A flock of birds (C_FowlGenerator, actor type 29, vtable 0x00811d80, 0xD0 bytes; original
// 0x005ba100..0x005bbdb0): birds sitting around its frame that fly up when a human or the camera
// comes near, a footstep or a shot is heard close by, or the script says so. Mode 0 takes over the
// mission's birds within its radius, mode 1 makes its own when they fly up, mode 2 makes its own
// at StartMission (on the ground below their places): a square of the model's copies (the count's
// square root a side) over twice the radius.
#pragma once

#include <cstdint>
#include <vector>

#include "common/c_str.h"
#include "game/actor.h"

class C_Bird;
struct S_HeardSound;

class alignas(16) C_FowlGenerator : public C_Actor {    // "FowlGenerator", vtable 0x00811d80
public:
    C_FowlGenerator();                                  // 0x005ba4d0
    // (The birds are not deleted: StopMission does that.)
    ~C_FowlGenerator() override = default;              // 0x005ba600 (scalar deleting 0x005ba5e0)

    // The birds fly up (mode 1 makes them first) once: their sound on a client within a second
    // of `time` (ms since), the clients told with `send`.
    void Start(bool send, int time);                    // 0x005bb140

    // The birds fly on (the ones gone deleted); the time since they flew up.
    void Update(int frameTime) override;                // 0x14 0x005bb3c0
    // Once a second until they fly: the players, the player's humans, the camera near?
    void Tick(int timeDelta) override;                  // 0x18 0x005bb4d0
    // They flew up (the message is empty).
    void OnMessage(C_BitStream *) override { Start(false, 0); }   // 0x1C 0x005bb280
    // (SetFrame 0x005ba4b0 and WriteProperties 0x005b61e0 are the base's.)
    // Chunk 0 the version (1) first, then 1 the mode, 2 the radius, 3 the distance that sets them
    // off, 4 the count, 5 the model's name; the others are left.
    bool ReadProperties(C_chunk &chunk) override;       // 0x34 0x005bb7e0
    void CopyFrom(const C_Actor *source) override { C_Actor::CopyFrom(source); }   // 0x38 0x005ba660
    // The birds taken over (mode 0) or made (mode 2), the server's sound listener.
    void StartMission(bool server) override;            // 0x3C 0x005ba670
    // The birds deleted (the mission's back in their places), the listener let go on a server.
    void StopMission(bool server) override;             // 0x40 0x005bb290
    // Stopped and started on the server side.
    void RestartMission(bool server) override;          // 0x44 0x005ba1c0
    // Chunk 0x10E1: flown, and then the time since.
    bool SaveGame(C_chunk &chunk) override;             // 0x48 0x005ba1e0
    bool LoadGame(C_chunk &chunk) override;             // 0x4C 0x005ba300

private:
    // The sound module's listener: a footstep or a shot heard loud enough sets them off.
    static void __stdcall OnSoundHeard(const S_HeardSound *sound);   // 0x005ba860
    // Mode 1 and 2: the birds made.
    void CreateBirds();                                 // 0x005ba8b0 (DS 0x005ce1a0)

    std::vector<C_Bird *> m_birds;                      // +0x90
    int m_mode = 0;                                     // +0xA0
    int m_listener = -1;                                // +0xA4 the sound module's
    float m_radius = 2.0f;                              // +0xA8
    float m_triggerDistance = 5.0f;                     // +0xAC
    int m_count = 0;                                    // +0xB0
    C_str m_model = "ptacekletacek";                    // +0xB4 the bird's model
    int m_checkTime = 0;                                // +0xB8 ms to the next look around
    bool m_flown = false;                               // +0xBC
    int m_time = 0;                                     // +0xC0 ms since they flew up
};

// The fowl generators' creator (g_actorCreators[ACTOR_FOWL_GENERATOR], vtable 0x00811d70).
class C_FowlGeneratorCreator : public C_ActorCreator {
public:
    C_Actor *Create() override;                         // 0x005ba130
    const char *GetName() override { return "FowlGenerator"; }   // 0x005ba190
    void Destroy(C_Actor *actor) override;              // 0x005ba1a0
};
