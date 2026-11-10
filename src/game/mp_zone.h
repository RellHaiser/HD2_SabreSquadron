// A multiplayer zone (C_MPZone, actor type 37, vtable 0x00812b20, 0x120 bytes; original
// 0x005ebde0..0x005edcf0): an area (its dummy frame's box, or the sphere around that box) a team
// takes by staying in it for its capture time, shown by one of three flags; the occupation games'
// zones and spawn areas (the "<frame>spawn" frame's children). A zone of an objective (+0xA4)
// changes the objective's status instead of awarding points.
#pragma once

#include <cstdint>

#include "game/actor.h"
#include "ls3d/math.h"

struct S_VertexAnimPlayer;

class alignas(16) C_MPZone : public C_Actor {           // "MPZone", vtable 0x00812b20
public:
    C_MPZone();                                         // 0x005ebed0
    ~C_MPZone() override = default;                     // 0x005ebfc0 (scalar deleting 0x005ebfa0)

    // Its number in the occupation's order.
    int GetNumber() const { return m_number; }          // +0x90
    // The team holding it (TEAM_NONE: neutral; spawns there).
    int GetTeam() const { return m_team; }              // +0x98
    // The objective it stands for (-1: a plain zone).
    int GetObjective() const { return m_objective; }    // +0xA4
    // The script (EnableSpawnZone): `team` holds it, the clients told.
    void EnableForSide(int team);                       // 0x005ed900
    // Its next spawn position (going round its spawn points with `cycle`, else false once all
    // were used: then it turns neutral); up to 32 tries for a free one.
    bool PickSpawnPosition(S_vector4 *pos, S_vector4 *dir, bool cycle);   // 0x005ed700
    // The spawn positions start over.
    void ResetSpawnIndex() { m_spawnIndex = 0; }        // +0x100
    // The capture's progress is shown to the local player (who is in it).
    bool IsRunning() const { return m_progressShown; }  // +0xF1
    // The capture's progress bar: the time left and the total (ms), the text.
    bool GetProgress(int *time, int *total, int *textId) const;   // 0x005ec840

    // Clients: the local player's presence (once a second on a pure client), the progress shown.
    void Update(int frameTime) override;                // 0x14 0x005ec540
    // The server: who is in it (once a second), team points while held, the capture.
    void Tick(int timeDelta) override;                  // 0x18 0x005ec060
    // 0 a capture starts, 1 it stops, 2 a new team (2 bits).
    void OnMessage(C_BitStream *msg) override;          // 0x1C 0x005ed390
    bool SetFrame(I3D_frame *frame) override;           // 0x2C 0x005ebfd0
    // Version 0x8001, then the number, the shape, the team, the text, the flags' names, the
    // objective, the capture time and the both-ways byte (raw).
    bool WriteProperties(C_chunk &chunk) override;      // 0x30 0x005ed920
    bool ReadProperties(C_chunk &chunk) override;       // 0x34 0x005eda60
    // The base's and the properties (not the starting team).
    void CopyFrom(const C_Actor *source) override;      // 0x38 0x005ebff0
    // Once until StopMission: the flags and the spawn frame found, the flag's animation player;
    // a client shows the team again.
    void StartMission(bool server) override;            // 0x3C 0x005ed110
    // (Not deactivated.) The frames let go.
    void StopMission(bool server) override;             // 0x40 0x005ed260
    // Its starting team again, the spawn points from the first.
    void RestartMission(bool server) override;          // 0x44 0x005ed8b0
    // (Nothing saved: false.)
    bool SaveGame(C_chunk &) override { return false; } // 0x48 0x005ebf80 (DS 0x0060c9d0)
    bool LoadGame(C_chunk &) override { return false; } // 0x4C 0x005ebf90 (DS 0x0060c9f0)
    // The team (2 bits).
    void WriteState(C_BitStream *msg) override;         // 0x50 0x005ed2d0
    void ReadState(C_BitStream *msg) override;          // 0x54 0x005ed2f0

private:
    // The new team's flag shown (the waving animation on its "vlajka" visual) and the objective
    // icon updated; nothing when it holds it already.
    void ApplyTeam();                                   // 0x005ec8b0
    // Whether `point` (in the frame's space) is in the zone: inside the box (strictly), or nearer
    // than `radius` to `center`.
    bool Contains(const S_vector &point, const I3D_bbox &box, const S_vector &center, float radius) const;   // 0x005ecad0
    // The players of each team in the zone (0 or 1 each: counting stops at the first) and whether
    // the local player is.
    void CountPresence(int present[3], const I3D_bbox &box);   // 0x005ecbe0
    // `points` for every living player in the zone.
    void AwardPlayers(int points);                      // 0x005ec310
    // The server, once a second: a capture starts or stops.
    void CheckCapture();                                // 0x005ecf20
    // Whether `team` may take it: with the map's ordered zones (mission +0x60 == 1), the zone
    // before (axis) or after (allies) must be the team's, or no zone the team's and this one
    // neutral.
    bool CanTake(int team) const;                       // 0x005ed7e0
    // Spawn point `index`: its world position and its z axis.
    void GetSpawnPoint(int index, S_vector4 *pos, S_vector4 *dir) const;   // 0x005ed550
    // The clients told: `kind`, the team with kind 2.
    void SendState(uint32_t kind);                      // 0x005ed320

    int m_number = 0;                                   // +0x90
    int m_shape = 0;                                    // +0x94 0 the box, 1 the sphere around it
    int m_team = 2;                                     // +0x98 the team holding it (-1: being shown again)
    int m_startTeam = 2;                                // +0x9C
    int m_textId = -1;                                  // +0xA0 the progress bar's text
    int m_objective = -1;                               // +0xA4
    int m_captureTime = 0;                              // +0xA8 s
    bool m_bothWays = true;                             // +0xAC any team takes it (else only the starting
                                                        //       team's enemy, through neutral at half time)
    bool m_resetPending = false;                        // +0xAD shown again at the next Update
    char m_flagNames[3][16] = {};                       // +0xAE the axis, allied, neutral flag frames
    S_VertexAnimPlayer *m_flagAnim = nullptr;           // +0xE0 the shown flag's animation
    int m_newTeam = 2;                                  // +0xE4 the team to show
    int m_checkTime = 0;                                // +0xE8 ms since the last check
    int m_progress = 0;                                 // +0xEC ms of the capture
    bool m_localInside = false;                         // +0xF0
    bool m_progressShown = false;                       // +0xF1
    int m_present[3] = {};                              // +0xF4 the teams' players in it (0 / 1)
    int m_spawnIndex = 0;                               // +0x100
    I3D_frame *m_flags[3] = {};                         // +0x104 (held)
    I3D_frame *m_spawnFrame = nullptr;                  // +0x110 (held) the spawn points' parent
    int m_captureState = 0;                             // +0x114 MPZONE_CAPTURE_*
    bool m_started = false;                             // +0x118
};

// The capture's states.
enum : int {
    MPZONE_CAPTURE_NONE = 0,
    MPZONE_CAPTURE_STOPPED = 1,
    MPZONE_CAPTURE_RUNNING = 2,
};

// The multiplayer zones' creator (g_actorCreators[ACTOR_MP_ZONE], vtable 0x00812b14).
class C_MPZoneCreator : public C_ActorCreator {
public:
    C_Actor *Create() override;                         // 0x005ebe40
    const char *GetName() override { return "MPZone"; } // 0x005ebea0
    void Destroy(C_Actor *actor) override;              // 0x005ebeb0
};
