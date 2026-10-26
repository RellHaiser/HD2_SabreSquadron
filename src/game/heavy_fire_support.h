// The heavy fire support ("HeavyFireSupportModule", module 16; the object 0x0089b2c0;
// game/heavy_fire_support.cpp, original source 0x00470500..0x004736d0): the air attacks and the
// artillery strikes the player's radios call in (C_PlayerController::DesignateTarget). The server
// runs them, one of each kind per team at a time, and sends them to the clients, which show the
// planes, the blasts and the radio messages.
#pragma once

#include <cstdint>
#include <vector>

#include "common/c_str.h"
#include "game/module.h"
#include "ls3d/math.h"

class C_BitStream;
class C_Human;
class I3D_frame;
class I3D_sound;

// The request of an air attack (C_PlayerController::DesignateTarget).
struct S_AirAttackRequest {                             // 0x24 bytes
    float x, z;                                         // +0x00 the target: the ground hit along the designation
    float dirX, dirZ;                                   // +0x08 the way the planes fly (the designation reversed)
    int kind;                                           // +0x10 S_AirSupportKind 0..15 (single player: 1 allies, 0 axis)
    int planes;                                         // +0x14 1..3
    float height;                                       // +0x18 the planes' height (60 m above the target)
    C_Human *caller;                                    // +0x1C
    int team;                                           // +0x20 E_HumanTeam
};
// The request of an artillery strike.
struct S_ArtilleryRequest {                             // 0x14 bytes
    float x, z;                                         // +0x00 the target (the shells fall from 300 m)
    C_Human *caller;                                    // +0x08
    int kind;                                           // +0x0C the side's map value (not used)
    int team;                                           // +0x10 E_HumanTeam
};

// A kind of air attack (tables\airsupport.tbl: a chunk per kind, 1000 the model and then the
// values 2000 speed, 3000 sound, 4000 bombs, 6000 damage, 5000 range).
struct S_AirSupportKind {                               // 0x18 bytes (defaults 0x00472210)
    C_str model;                                        // +0x00 the planes' model
    float speed = 30.0f;                                // +0x04 m/s
    int soundGroup = 0;                                 // +0x08 the engine (SOUND_CHAPTER_VEHICLE_NOT_CAR)
    int bombsPerPlane = 2;                              // +0x0C the blasts each plane adds (clients)
    float damage = 1000.0f;                             // +0x10 the explosion's (server)
    float range = 10.0f;                                // +0x14 m: the explosion's; the blasts fall within it

    void SetDefaults();                                 // 0x00472210 (__fastcall)
};

// An air attack under way (0x80 bytes, 16-byte aligned): the planes fly in along the direction
// at the kind's speed and are over the target after 15 s, when the server sets off an
// explosion on the ground below and the clients scatter the planes' blasts around it; it ends
// 15 s later. The clients play the caller's radio messages (the request, then the planes'
// arrival when the first is over).
class alignas(16) C_AirStrike {
public:
    C_AirStrike(const S_vector4 &target, const S_vector4 &dir, const S_AirSupportKind &kind, int planes, int team,
                C_Human *caller);                       // 0x00470530
    ~C_AirStrike();                                     // 0x00471090 (__fastcall)
    C_AirStrike(const C_AirStrike &) = delete;
    C_AirStrike &operator=(const C_AirStrike &) = delete;

    // The frame's step (false: over).
    bool Update(int timeDelta);                         // 0x00471170 (thunk 0x00471140)
    // The game tick's step: only without a client side (a dedicated server).
    bool TickServer(int timeDelta);                     // 0x00471150
    int GetTeam() const { return m_team; }

private:
    // A plane (0x20 bytes, 16-byte aligned): its model hangs on the one before it.
    struct alignas(16) S_Plane {
        S_vector4 offset = {};                          // +0x00 behind the plane before it (not set for the first)
        I3D_frame *model = nullptr;                     // +0x10
        int sound = -1;                                 // +0x14 the engine's sound slot
        ~S_Plane();                                     // 0x00471750 (__fastcall; constructor 0x00471730)
    };

    S_vector4 m_pos = {};                               // +0x00 the lead plane
    S_vector4 m_target = {};                            // +0x10
    S_vector4 m_dir = {};                               // +0x20 normalized
    float m_speed = 0.0f;                               // +0x30 m/s
    std::vector<S_Plane *> m_planes;                    // +0x34
    int64_t m_time = 0;                                 // +0x48 ms until over the target
    int m_blastTimer = 0;                               // +0x50 ms until the next blast
    int m_blastsLeft = 0;                               // +0x54
    int m_bombsPerPlane = 0;                            // +0x58
    float m_range = 0.0f;                               // +0x5C m
    float m_damage = 0.0f;                              // +0x60
    int m_team = 0;                                     // +0x64 (clients: 2)
    C_Human *m_caller = nullptr;                        // +0x68
    int m_speechTimer = 0;                              // +0x6C ms until the second radio message
    I3D_sound *m_speech = nullptr;                      // +0x70 the radio (clients)
};

// An artillery strike under way (0x40 bytes, 16-byte aligned): the shells hit 7 s after the
// call, the server's explosion on the ground below the point and 20 blasts within 20 m on the
// clients; it ends 7 s later. The clients play the caller's radio messages.
class alignas(16) C_ArtilleryStrike {
public:
    C_ArtilleryStrike(const S_vector4 &target, int team, C_Human *caller);   // 0x004719f0
    // (The destructor 0x00471b10 is empty.)

    bool Update(int timeDelta);                         // 0x00471b50 (thunk 0x00471b20)
    bool TickServer(int timeDelta);                     // 0x00471b30
    int GetTeam() const { return m_team; }

private:
    S_vector4 m_target = {};                            // +0x00 300 m above the ground
    int64_t m_time = 7000;                              // +0x10 ms until the shells hit
    int m_blastTimer = 0;                               // +0x18 ms until the next blast
    int m_blastsLeft = 0;                               // +0x1C
    int m_blastCount = 20;                              // +0x20
    float m_range = 20.0f;                              // +0x24 m
    float m_damage = 1000.0f;                           // +0x28
    int m_team = 0;                                     // +0x2C (clients: 2)
    C_Human *m_caller = nullptr;                        // +0x30
    int m_speechTimer = 0;                              // +0x34 ms until the second radio message
};

// The interface the player controller calls (vtable 0x00810058: two pure slots).
class I_HeavyFireSupport {
public:
    // Results of RequestAirAttack.
    enum : int {
        REQUEST_SENT = 0,
        REQUEST_BUSY = 1,                               // the team's air attack is under way
        REQUEST_NOT_SERVER = 2,                         // (RequestArtillery always returns it)
        REQUEST_BAD_KIND = 3,
    };
    virtual int RequestAirAttack(const S_AirAttackRequest &request) = 0;   // 0x00
    virtual int RequestArtillery(const S_ArtilleryRequest &request) = 0;   // 0x04

protected:
    ~I_HeavyFireSupport() = default;
};

// The module (vtable 0x00810050 with the C_Module part's 0x00810010 at +4; 0x1B4 bytes).
class C_HeavyFireSupport : public I_HeavyFireSupport, public C_Module {
public:
    static constexpr int KIND_COUNT = 16;

    // (Reads tables\airsupport.tbl.)
    C_HeavyFireSupport(int id, const char *name);       // 0x00471fc0
    // (The strikes left are not freed; StopMission frees them.)
    ~C_HeavyFireSupport() override = default;           // 0x004720d0 (deleting 0x00472080)

    // The server: a strike at the request's point unless one of the team's is under way;
    // the clients get it (module message).
    int RequestAirAttack(const S_AirAttackRequest &request) override;   // 0x00472e50
    int RequestArtillery(const S_ArtilleryRequest &request) override;   // 0x00472ce0

    void Tick(int timeDelta) override;                  // 0x00472c60
    void Update(int timeDelta) override;                // 0x00472510
    // Every team may call again.
    void StartMission(bool server) override;            // 0x00472250
    // The strikes go.
    void StopMission(bool server) override;             // 0x00472270
    // The clients' strikes (the server's requests).
    bool OnMessage(C_BitStream *msg) override;          // 0x00472590

private:
    bool LoadTable();                                   // 0x004732b0 (__fastcall)
    void RemoveAirStrike(int index);                    // 0x00472350
    void RemoveArtillery(int index);                    // 0x004723f0
    // The flag at m_busy[index]; null past the four (see m_busy).
    bool *BusyFlag(int index);

    // (+0x04 the C_Module part.)
    S_AirSupportKind m_kinds[KIND_COUNT];               // +0x10
    std::vector<C_AirStrike *> m_airStrikes;            // +0x190
    std::vector<C_ArtilleryStrike *> m_artillery;       // +0x1A0
    // A strike of the team under way (the server's): [team] the air attacks of teams 0 / 1,
    // [2 + team] the artillery's. The original indexes the bytes from here by the team: the
    // clients' strikes (team 2) clear the artillery flag of team 0 (an air attack's) and the
    // byte after the object (an artillery strike's: the low byte of g_vertexAnimModule's
    // vtable pointer).
    bool m_busy[4] = {};                                // +0x1B0
};
I_HeavyFireSupport *GetHeavyFireSupport();             // 0x00471fb0 (&g_heavyFireSupport 0x0089b2c0)
