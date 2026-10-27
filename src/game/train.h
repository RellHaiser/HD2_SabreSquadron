// A train (C_Train, actor type 14, vtable 0x00811e78, 0x8250 bytes; original 0x005bbdb0..0x005c0ce0,
// two source files): wagons (the train's "Wagon" properties: user frames of the scene, each with
// its "BODY", wheels "WHL%02d" / "WHR%02d" and their rods) that a script sets on a track; it runs
// along the track's keys, the wagons placed behind each other on the polyline of the keys, the
// bodies swaying, the wheels turning, the steam engine's rods following the wheels. It marks the
// checkpoints it is about to cross (CHECKPOINT_FLAG_UNDER_CAR) and runs humans over. Sounds: the
// steam, two whistles, the brakes, the wheels and the chuffing (in four sets by speed).
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "game/actor.h"
#include "game/track.h"
#include "ls3d/math.h"

class I3D_sound;
struct S_Checkpoint;
struct S_FrameVolume;
struct S_SoundFrame;

class alignas(16) C_Train : public C_Actor {            // "Train", vtable 0x00811e78
public:
    C_Train();                                          // 0x005bcfb0
    ~C_Train() override;                                // 0x005bd0f0 (scalar deleting 0x005bd0d0)

    // The whistle (the near one when the camera is within 48 m).
    void Start();                                       // 0x005bc970
    // Follows the named track (or stops following when `on` is false).
    void SetTrack(bool on, const char *track)           // 0x0041d590 (an inline)
    {
        if (on)
            FollowTrack(track);
        m_running = on;
    }

    // Running: the time on, Move.
    void Update(int frameTime) override;                // 0x14 0x005bcf80
    void Tick(int) override {}                          // 0x18 0x005bcf70
    // A model only: it becomes the first wagon (the frame's name), then the base's.
    bool SetFrame(I3D_frame *frame) override;           // 0x2C 0x005bd1a0
    // (WriteProperties 0x005b61e0 is the base's.) The wagons dropped, then chunk 1 the version (2 or
    // 3), then 8 the train's name (dropped), 10 a wagon (its frame's name), 0x14 its length, 0x1E
    // the gap behind it; another id is entered and not left (quirk kept).
    bool ReadProperties(C_chunk &chunk) override;       // 0x34 0x005be360
    void CopyFrom(const C_Actor *source) override { C_Actor::CopyFrom(source); }   // 0x38 0x005bd480
    // Once: the wagons' frames (into the primary sector), their bodies, wheels and boxes, the
    // sounds.
    void StartMission(bool server) override;            // 0x3C 0x005bd820
    // The sounds, the wagons back where they were, their boxes, the wheels and the path gone.
    void StopMission(bool server) override;             // 0x40 0x005be1b0
    // Stopped and started on the server's side.
    void RestartMission(bool server) override;          // 0x44 0x005befe0
    // Chunk 1000 running (the track's name, the key, the wagons' places), else 2000.
    bool SaveGame(C_chunk &chunk) override;             // 0x48 0x005bf000
    bool LoadGame(C_chunk &chunk) override;             // 0x4C 0x005bf250
    // `time` ms along the track (with `force`, placed even when it did not move): the wagons, the
    // wheels, the sounds, the checkpoints.
    virtual void Move(uint32_t time, bool force);       // 0xE0 0x005bff10

    static constexpr int CHUFF_SETS = 4;
    static constexpr int CHUFF_SOUNDS = 4;

private:
    struct alignas(16) S_Wagon {                        // 0x50 bytes (_aligned_malloc'd)
        S_vector4 savedPos = {};                        // +0x00 the frame's local position at the start
        S_quat savedRot = {};                           // +0x10 and rotation
        std::string name;                               // +0x20 the frame's
        I3D_frame *frame = nullptr;                     // +0x30
        I3D_frame *body = nullptr;                      // +0x34 "BODY"
        float length = 10.0f;                           // +0x38 m
        float gap = 3.0f;                               // +0x3C m to the next wagon
        I3D_sound *wheelSound = nullptr;                // +0x40
        S_SoundFrame *wheelSoundFrame = nullptr;        // +0x44
        S_FrameVolume *volume = nullptr;                // +0x48 the box (flags 0x40, type 0x2000)
    };
    struct S_Wheel {                                    // 0x14 bytes (held by pointer in the original)
        I3D_frame *wheel = nullptr;                     // +0x00 "WHL%02d" / "WHR%02d", a visual
        I3D_frame *rod = nullptr;                       // +0x04 "THL%02d" / "THR%02d" under it, kept level
        I3D_frame *connectingRod = nullptr;             // +0x08 "DTHL%02d" / "DTHR%02d" under it
        I3D_frame *crosshead = nullptr;                 // +0x0C "STHL00" / "STHR00" under that, kept level
        float radius = 0.0f;                            // +0x10 half the visual's height
    };
    struct S_ChuffSet {                                 // 0x2C bytes ("l_sts<set + 1><a..d>.wav")
        I3D_sound *sounds[CHUFF_SOUNDS];                // +0x00
        S_SoundFrame *frames[CHUFF_SOUNDS];             // +0x10
        float minSpeed;                                 // +0x20 km/h above which it plays (0, 12, 24, 36)
        int count;                                      // +0x24 sounds loaded
        int next;                                       // +0x28 the last one played
    };

    // The sounds loaded (`load`) or freed.
    void SetSounds(bool load);                          // 0x005bca90
    // The track found, the path built, the train's place on the track: the first key past the
    // train's length (quirk kept: none, and the train is neither placed nor moved); the first
    // wagon there, then Move(1000).
    void FollowTrack(const char *track);                // 0x005bd490
    // The checkpoints along the track: the first one within 2 m of a key (5 m search), then the
    // links going on most straight ahead.
    void BuildPath();                                   // 0x005bc2a0
    // The link from the path's last point going on the most straight (more than 60° off: none).
    S_Checkpoint *FindNextCheckpoint() const;           // 0x005bbe70
    void ClearPath();                                   // 0x005bc7c0
    // The path's points within 5 s (at the speed) ahead marked, those more than the train's
    // length behind unmarked.
    void UpdateCrossings();                             // 0x005bc5a0
    void ClearWagons();                                 // 0x005be780
    // The current set's next chuff.
    void PlayChuff();                                   // 0x005bf4e0
    // The wheels turned by `distance` m, the rods after them (a crosshead's turn: a chuff).
    void UpdateWheels(uint32_t time, float distance, int unused);   // 0x005bf580

    I3D_sound *m_steam = nullptr;                       // +0x90 "l_steam.wav", looping
    I3D_sound *m_whistleNear = nullptr;                 // +0x94 "l_whisnr.wav"
    I3D_sound *m_whistleFar = nullptr;                  // +0x98 "l_whisfr.wav"
    I3D_sound *m_brakeLoop = nullptr;                   // +0x9C "l_braklp.wav", looping
    I3D_sound *m_brake = nullptr;                       // +0xA0 "l_brake.wav"
    I3D_sound *m_jar = nullptr;                         // +0xA4 "l_jar.wav"
    S_SoundFrame *m_steamFrame = nullptr;               // +0xA8
    S_SoundFrame *m_whistleNearFrame = nullptr;         // +0xAC
    S_SoundFrame *m_whistleFarFrame = nullptr;          // +0xB0
    S_SoundFrame *m_brakeFrame = nullptr;               // +0xB4
    S_SoundFrame *m_jarFrame = nullptr;                 // +0xB8
    S_SoundFrame *m_brakeLoopFrame = nullptr;           // +0xBC
    int m_brakeTimer = 0;                               // +0xC0 ms to the next squeal
    int m_jarTimer = 0;                                 // +0xC4 ms to the next jar
    float m_brakeFade = 0.0f;                           // +0xC8 the brake loop fading out (0..1)
    float m_trainLength = 0.0f;                         // +0xCC m (the gaps, the lengths but the last's)
    S_ChuffSet m_chuffSets[CHUFF_SETS] = {};            // +0xD0
    std::vector<S_Checkpoint *> m_path;                 // +0x180 (held 8-byte points in the original)
    int m_lastMarked = -1;                              // +0x190 path index of the last point marked
    int m_firstMarked = -1;                             // +0x194 and of the first one still marked
    int m_chuffSet = -1;                                // +0x198
    bool m_running = false;                             // +0x19C following the track
    std::vector<S_Wagon *> m_wagons;                    // +0x1A0
    bool m_started = false;                             // +0x1B0
    C_TrackPlayer m_player;                             // +0x1C0 its track +0x208, its key +0x20C
    std::vector<S_Wheel> m_wheels;                      // +0x8230
    float m_crossheadHeight = -0.3f;                    // +0x8240 the crossheads' slide (connecting rods' grandparents' y)
    uint32_t m_time = 0;                                // +0x8244 ms running (the bodies' sway)
    float m_speed = 0.0f;                               // +0x8248 km/h
};

// The trains' creator (g_actorCreators[ACTOR_TRAIN], vtable 0x00811e60).
class C_TrainCreator : public C_ActorCreator {
public:
    C_Actor *Create() override;                         // 0x005bbde0
    const char *GetName() override { return "Train"; }  // 0x005bbe40
    void Destroy(C_Actor *actor) override;              // 0x005bbe50
};
