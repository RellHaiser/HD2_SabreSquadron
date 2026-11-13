// A bicycle (C_Bike, actor type 31, vtable 0x00812440, 0x1D0 bytes; original 0x005d8760..0x005db780):
// a script puts a human on it (HUMAN_Bike) and has it ridden along the road checkpoints to a
// point. The human is suspended meanwhile and a copy of its model ("<name>_ridedupl") sits on the
// frame "ram", pedalling ("bike_legs") and steering ("bike_ride"); the bike turns towards the next
// point, its frame and handlebars ("riditka") lagging behind, rolls along the ground under its
// front wheel ("kolo1", 0.35 m), and turns its wheels ("kolo1", "kolo2") and cranks ("kliky").
// Its box (COLLISION_TYPE_PIKE) moves along.
#pragma once

#include <cstdint>
#include <vector>

#include "game/actor.h"
#include "game/collision.h"
#include "game/sound_module.h"                          // S_SoundRequest
#include "ls3d/math.h"

class I3D_model;

class alignas(16) C_Bike : public C_Actor {             // "Bike", vtable 0x00812440
public:
    C_Bike();                                           // 0x005d9460
    // (The rider's model is not released: StopMission does that.)
    ~C_Bike() override = default;                       // 0x005d9530 (scalar deleting 0x005d9510)

    // Ridden by the human of `rider` (its frame) along the road to the checkpoint `roadCheckpoint`:
    // from the point nearest the bike (within 5 m; none: nothing), along the path found (none:
    // nothing); a rider not yet on it mounts (its human suspended, the speed 0). CHECKPOINT_NONE:
    // the rider gets off where the bike is (the human back and placed there), the bike stops.
    // (Quirk kept: `speed` is not used; the bike rides at its own top speed.)
    void Drive(int roadCheckpoint, float speed, I3D_frame *rider);   // 0x005d8f60

    // Ridden: the next point reached (within its radius, at least 3 m) is dropped, the last one
    // stops the ride and advances the rider's script; else the speed rises by 1 m/s a second to
    // the top. The bike turns towards the point, its frame and handlebars after it. Then it moves
    // on (slowing by 1 m/s a second without a path) along the handlebars' direction, its front
    // wheel 0.35 m over the ground (falling 2 m/s without one), the wheels, the cranks and the
    // rider's pedalling turned by the distance; the box follows.
    void Update(int frameTime) override;                // 0x14 0x005d95c0
    void Tick(int) override {}                          // 0x18 0x005db0d0
    void OnMessage(C_BitStream *) override {}           // 0x1C 0x005db770
    void Unknown20(C_BitStream *) override {}           // 0x20 0x005d9500
    // A frame only (null: false), then the base's.
    bool SetFrame(I3D_frame *frame) override { return frame && C_Actor::SetFrame(frame); }   // 0x2C 0x005d9570 (DS 0x005f4f90)
    // (WriteProperties 0x005b61e0 is the base's.) The top speed and a value not used, raw.
    // Quirk kept: the top speed read is put back to what it was (7 km/h stays).
    bool ReadProperties(C_chunk &chunk) override;       // 0x34 0x005db640
    void CopyFrom(const C_Actor *source) override;      // 0x38 0x005d9590
    // Once until StopMission: the box, the parts found, the shadow.
    void StartMission(bool server) override;            // 0x3C 0x005db0e0
    // The rider's model released, the box removed, the path dropped, the frame "ram" and the
    // cranks straight again.
    void StopMission(bool server) override;             // 0x40 0x005db4c0
    // The rider off, stopped and started on the server side.
    void RestartMission(bool server) override;          // 0x44 0x005d8880
    // Ridden: chunk 2000 holding 5000 the rider's frame's name, 6000 the path, 3000 the speed and
    // 4000 the pedals' phase; else an empty chunk 1000.
    bool SaveGame(C_chunk &chunk) override;             // 0x48 0x005d88b0
    bool LoadGame(C_chunk &chunk) override;             // 0x4C 0x005d8b90

private:
    // The rider's model: a copy of `rider`'s on the frame "ram", with the animations.
    void MountRider(I3D_frame *rider);                  // 0x005d9230

    S_matrix m_invMatrix = {};                          // +0x90 the box's
    bool m_started = false;                             // +0xD0
    S_SoundRequest m_sound;                             // +0xE0 at the bike (StartMission; not played)
    float m_topSpeed = 7.0f;                            // +0x120 km/h
    int m_unknown124 = 0;                               // +0x124 (a property; not used)
    S_CollisionVolume m_volume;                         // +0x130
    bool m_driving = false;                             // +0x190 ridden along the path
    std::vector<uint16_t> m_path;                       // +0x194 road checkpoints, the next first
    float m_speed = 0.0f;                               // +0x1A4 m/s
    I3D_model *m_riderModel = nullptr;                  // +0x1A8 "<rider>_ridedupl"
    I3D_frame *m_rider = nullptr;                       // +0x1AC the rider's frame
    float m_pedalPhase = 0.0f;                          // +0x1B0 rad of the cranks not yet shown
    I3D_frame *m_frontWheel = nullptr;                  // +0x1B4 "kolo1"
    I3D_frame *m_rearWheel = nullptr;                   // +0x1B8 "kolo2"
    I3D_frame *m_handlebars = nullptr;                  // +0x1BC "riditka"
    I3D_frame *m_body = nullptr;                        // +0x1C0 "ram" (the rider's seat)
    I3D_frame *m_cranks = nullptr;                      // +0x1C4 "kliky"
};

// The bikes' creator (g_actorCreators[ACTOR_BIKE], vtable 0x00812430).
class C_BikeCreator : public C_ActorCreator {
public:
    C_Actor *Create() override;                         // 0x005d87f0
    const char *GetName() override { return "Bike"; }   // 0x005d8850
    void Destroy(C_Actor *actor) override;              // 0x005d8860
};
