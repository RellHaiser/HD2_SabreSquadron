// A submarine (C_Submarine, actor type 20, vtable 0x00812650, 0x120 bytes; original 0x005de660..
// 0x005e2070, four source files): a boat with two seats (the pilot's: "SEAT00", a gunner's:
// "SEAT01") that the pilot drives at up to 6 m/s, turning and diving (to its "Max waterlevel"
// property at the most); its hull's boxes (the "BODYCOLB00" frame's children) stop it and turn it
// away; its "BODY" bobs and rolls; its "sroub" propeller turns; five engine and water sounds.
#pragma once

#include <cstdint>
#include <vector>

#include "game/actor.h"
#include "ls3d/math.h"

class C_CameraFrameKeeper;
struct S_FrameVolume;

class alignas(16) C_Submarine : public C_Actor {        // "Submarine", vtable 0x00812650
public:
    C_Submarine();                                      // 0x005df000
    ~C_Submarine() override;                            // 0x005df2e0 (scalar deleting 0x005df2c0)

    // The pilot's controls: forward (-1..1), sideways (the turn's), up (the climb's).
    void SetControls(float forward, float side, float up);   // 0x005e0dd0
    // m/s along its heading (-6..6; the squad lets it run down).
    float GetSpeed() const { return m_speed; }          // +0xF4
    void SetSpeed(float speed) { m_speed = speed; }
    // How far the body is raised by its bobbing (m; +0xF0).
    float GetBob() const { return m_bob; }
    // The turn rate (-0.5..0.5).
    void SetTurn(float turn) { m_turn = turn; }         // +0xF8

    // Clients (and a local game): the body's bobbing, the controls, the move, the camera.
    void Update(int frameTime) override;                // 0x14 0x005e0e20
    // A server without a client side: Update.
    void Tick(int timeDelta) override;                  // 0x18 0x005e0e00
    // The base's, then the body's place, the seats ("SEAT%02d", their flags from "SEAT<digits>"
    // user properties) and the propeller.
    bool SetFrame(I3D_frame *frame) override;           // 0x2C 0x005dee00
    // (WriteProperties 0x005b61e0 is the base's.) Chunk 0 the version (2) first, then 1 the maximum
    // water level; another id is entered and not left (quirk kept).
    bool ReadProperties(C_chunk &chunk) override;       // 0x34 0x005dfe50
    void CopyFrom(const C_Actor *source) override { C_Actor::CopyFrom(source); }   // 0x38 0x005df3c0
    // A server: the hull's boxes, the sounds (paused), the pilot's camera keeper.
    void StartMission(bool server) override;            // 0x3C 0x005df3d0
    // On the server side or a pure client: the body back, everything reset, the sounds stopped.
    void StopMission(bool server) override;             // 0x40 0x005dfca0
    // (The sounds are forgotten: the sound module stopped them all.) Stopped and started on each
    // side the game runs.
    void RestartMission(bool server) override;          // 0x44 0x005de7c0
    // The base's, then chunks 0x457 the speed, 0x8AE the turn, 0xD05 the climb.
    bool SaveGame(C_chunk &chunk) override;             // 0x48 0x005de830
    // The base's, the frame's place and heading taken, the boxes moved, the chunks.
    bool LoadGame(C_chunk &chunk) override;             // 0x4C 0x005dea30

    // --- the seats (0 the pilot's, 1) ---
    bool IsSeatBlocked(int seat) override;              // 0x6C 0x005e0270
    bool SetSeatBlocked(int seat, bool blocked) override;   // 0x70 0x005e0290
    // In the seat's frame (the boat's without one), at its origin facing +z; the pilot's camera
    // back first.
    void SetSeatOccupant(int seat, C_Actor *occupant) override;   // 0x74 0x005e0320
    C_Actor *GetSeatOccupant(int seat) override;        // 0x78 0x005e0410
    int FindSeat(const C_Actor *occupant) override;     // 0x7C 0x005e0430
    // Out into the primary sector where it is; the pilot's controls let go. (Quirk kept: returns
    // null, the seat already emptied.)
    C_Actor *LeaveSeat(int seat) override;              // 0x80 0x005e0460
    // Out to a free spot beside a seat (PutOutside), then LeaveSeat.
    bool ExitSeat(int seat) override;                   // 0x84 0x005e04f0
    int GetSeatCount() override { return SEAT_COUNT; }  // 0x88 0x005e0c90
    uint32_t GetSeatFlags(int seat) override;           // 0x8C 0x005e0ca0
    // The seat's camera dummy ("cameraFPV*" / "camera3RD*", else the seat's parent's), looked for
    // with the occupant unlinked.
    I3D_frame *GetSeatCamera(int seat) override;        // 0x90 0x005e0cc0
    // "name<text>" in the seat frame's user properties (-1: none).
    int GetSeatText(int seat) override;                 // 0x94 0x005e02c0

    static constexpr int SEAT_COUNT = 2;

private:
    struct S_Seat {                                     // 0x10 bytes
        bool blocked = false;                           // +0x00
        C_Actor *occupant = nullptr;                    // +0x04
        I3D_frame *frame = nullptr;                     // +0x08 "SEAT%02d"
        uint32_t flags = 0;                             // +0x0C bits of the "SEAT<digits>" property
    };

    // Speeds, controls and boxes gone, everyone out.
    void Reset();                                       // 0x005df650
    // A hull box around `frame`, kept.
    void AddVolume(I3D_frame *frame, uint32_t flags, uint32_t type);   // 0x005dfa80
    // The boxes follow their frames.
    void UpdateVolumes();                               // 0x005e0e60
    // The boxes into (out of) the world.
    void SetVolumesActive(bool active);                 // 0x005e1010
    // A server: the body bobs (0.05 m) and rolls with the game's time.
    void Bob(uint32_t time);                            // 0x005e14d0
    // The controls move the speeds: the speed (±6) and the climb (±1) by 1 per s, decaying by 1
    // per s without; the turn by 0.1 per s (±0.5), decaying by 0.5 per s.
    void ApplyControls(int frameTime);                  // 0x005e1d90
    // A server: the move, the turn, the climb, the collisions, the propeller and the sounds.
    void Move(int frameTime);                           // 0x005e15e0
    // The hull's boxes (from the second) swept from where they were: on a hit it turns away (by
    // the speed) and goes back (false).
    bool CheckCollision(float time);                    // 0x005e1090
    // A spot for the occupant of `seat` 1.35 m to its side (`otherSide`: the left) on the ground
    // below, facing the seat; false when the way there is blocked.
    bool FindExit(int seat, S_vector4 *pos, S_vector4 *dir, bool otherSide);   // 0x005e0530
    // `actor` out beside its seat (else beside the other one): placed, facing the seat.
    bool PutOutside(C_Actor *actor);                    // 0x005e0b30

    S_vector4 m_bodyPos = {};                           // +0x90 the body's local position (SetFrame)
    std::vector<S_FrameVolume *> m_volumes;             // +0xA0 the hull's boxes
    I3D_frame *m_body = nullptr;                        // +0xB0 "BODY"
    I3D_frame *m_propeller = nullptr;                   // +0xB4 "sroub"
    S_Seat m_seats[SEAT_COUNT];                         // +0xB8
    float m_maxWaterLevel = 100.0f;                     // +0xD8 m (the highest it climbs)
    int m_engineSound = -1;                             // +0xDC sound slots (group 7 of chapter 8)
    int m_diveSound = -1;                               // +0xE0 (group 0)
    int m_riseSound = -1;                               // +0xE4 (group 1)
    int m_surfaceSound = -1;                            // +0xE8 (group 2, at the propeller)
    int m_underwaterSound = -1;                         // +0xEC (group 3)
    float m_bob = 0.0f;                                 // +0xF0 m the body is raised by
    float m_speed = 0.0f;                               // +0xF4
    float m_turn = 0.0f;                                // +0xF8
    float m_climb = 0.0f;                               // +0xFC m/s
    float m_forward = 0.0f;                             // +0x100 the controls
    float m_side = 0.0f;                                // +0x104
    float m_up = 0.0f;                                  // +0x108
    bool m_volumesActive = false;                       // +0x10C
    C_CameraFrameKeeper *m_camera = nullptr;            // +0x110 the pilot seat's third person camera
};

// The submarines of the mission (0x008ac9a8).
const std::vector<C_Actor *> &GetSubmarines();          // 0x005de7b0

// The submarines' creator (g_actorCreators[ACTOR_SUBMARINE], vtable 0x00812644).
class C_SubmarineCreator : public C_ActorCreator {
public:
    C_Actor *Create() override;                         // 0x005de6c0
    const char *GetName() override { return "Submarine"; }   // 0x005de720
    void Destroy(C_Actor *actor) override;              // 0x005de730
};
