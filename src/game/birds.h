// The mission's birds (game/birds.cpp, original source 0x00479790..0x0047ab10): the scene's
// "ptacekletacek" models sitting about, hopping now and then; one flies off when disturbed (the
// fowl generator's flocks, game/fowl_generator.cpp, use them too). The effects module collects,
// updates and frees them.
#pragma once

#include <vector>

#include "ls3d/math.h"

class I3D_frame;

// A bird's states (the flight's update).
enum : int {
    BIRD_TAKING_OFF = 0,                                // up 10 m, then BIRD_CLIMBING
    BIRD_CLIMBING = 1,                                  // gravity bends the flight down, then BIRD_GLIDING
    BIRD_GLIDING = 2,                                   // until far off (then gone)
    BIRD_SITTING = 3,                                   // hops now and then
};

class C_Bird {                                          // 0x70 bytes (_aligned_malloc 16)
public:
    // On `model`, whose frame data then points at the bird (a bird on the model before leaves
    // its start place to this one); `ownModel`: the model is the bird's (removed from the scene
    // with it), else it goes back to g_spareBirdModels (hidden, at its start) when the bird is
    // deleted. The model is linked to the primary sector; the wings are "Object03" / "Object01".
    C_Bird(I3D_frame *model, bool ownModel);            // 0x004797c0
    ~C_Bird();                                          // 0x004799e0
    C_Bird(const C_Bird &) = delete;
    C_Bird &operator=(const C_Bird &) = delete;

    // Flying on; false once it has flown off (or without its wings).
    bool Update(int time);                              // 0x0047a2e0
    // Off it goes, the way its model faces and up (once).
    void FlyAway();                                     // 0x0047a2f0
    I3D_frame *GetModel() const { return m_model; }     // +0x50
    bool OwnsModel() const { return m_ownModel; }       // +0x60

private:
    // `time` ms of the flight; a part of it may run again in the next state.
    bool Fly(float time);                               // 0x00479c70

    // (+0x0C, +0x1C, +0x3C: the vectors' w, not used.)
    S_vector m_velocity = {0.0f, 0.0f, 0.0f};           // +0x00 per ms
    S_vector m_startPos;                                // +0x10 where it sits
    S_quat m_startRot;                                  // +0x20
    S_vector m_pos;                                     // +0x30
    int m_unknown40 = 0;                                // +0x40 5..10 s at random (not read)
    float m_wingPhase = 0.0f;                           // +0x44
    float m_timer = 0.0f;                               // +0x48 ms to the next hop / landing
    bool m_flownAway = false;                           // +0x4C
    bool m_flapping = false;                            // +0x4D
    I3D_frame *m_model = nullptr;                       // +0x50
    I3D_frame *m_leftWing = nullptr;                    // +0x54 "Object03"
    I3D_frame *m_rightWing = nullptr;                   // +0x58 "Object01"
    float m_speed = 5.0f;                               // +0x5C m/s
    bool m_ownModel = false;                            // +0x60
    int m_state = BIRD_SITTING;                         // +0x64
};

// The birds sitting in the mission and the models of the birds deleted (hidden, to be used
// again by the fowl generator).
extern std::vector<C_Bird *> g_birds;                   // 0x0089bb90
extern std::vector<I3D_frame *> g_spareBirdModels;      // 0x0089bba0
// Each bird flown on; those flown off are deleted (the last one into its place).
void UpdateBirds(int timeDelta);                        // 0x0047a700
// A bird for each scene model with "ptacekletacek" in its name.
void CollectBirds();                                    // 0x0047a790
// Every bird deleted; the spare models shown again.
void FreeBirds();                                       // 0x0047aa10
