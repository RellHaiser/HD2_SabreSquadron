// A destructible scene object (C_AnimationActor, actor type 25, vtable 0x008119b8, 0xF0 bytes;
// original 0x005b3100..0x005b66e0): a model frame of the scene with hit points. When it is
// destroyed ("stopped": its hit points ran out, or the script or the network says so) the intact
// frame is hidden and an animated copy loaded from "Anim name" is shown playing its destruction;
// collision boxes follow the "BODYCOLB<nn>" frames (the intact frame's, then the copy's), an
// explosion sound and particles mark it, and the frame's script is told. The model "#mchescape"
// (an aircraft) has wings that destroy the cars they sweep through, and itself with them.
#pragma once

#include <cstdint>
#include <vector>

#include "common/c_str.h"
#include "game/actor.h"
#include "game/collision.h"

class I3D_model;

// What the animation actor's hardest material resists (GetArmour; the squad picks its weapons by it).
enum : int {
    ANIMATION_ARMOUR_HEAVY = 1,                         // a material of penetration cost over 1500
    ANIMATION_ARMOUR_LIGHT = 2,                         // over 505: armoured
    ANIMATION_ARMOUR_NONE = 3,
};

class alignas(16) C_AnimationActor : public C_Actor {   // "Animation", vtable 0x008119b8
public:
    C_AnimationActor();                                 // 0x005b3740
    ~C_AnimationActor() override;                       // 0x005b3930 (scalar deleting 0x005b3910)

    // Destroys it (once): the collision frame's static collision off, the collision boxes gone,
    // the explosion sound and particles (clients, unless `immediately` or loading), the intact
    // frame hidden and the copy shown playing its animation, the script told (server), the
    // clients told (multiplayer server).
    void Stop(bool immediately);                        // 0x005b55e0
    // The copy's animation is not advanced any more (the script's state 666).
    void HoldAnimation() { m_animationHeld = true; }
    bool IsStopped() const { return m_stopped; }
    // "Show no info flag": the signals and the roll menu offer nothing on it.
    bool ShowsNoInfo() const { return m_showNoInfo; }
    int GetArmour() const { return m_armour; }          // ANIMATION_ARMOUR_*
    // The collision boxes removed, or made from the frame's again (the script module's client
    // messages, C_ScriptModule::HandleClientMessage 0x0042b650).
    void SetCollisionsOn(bool on);                      // 0x005b54c0

    // The step (Step); Tick runs it too on a server without a client side (quirk kept: a
    // dedicated server steps twice).
    void Update(int frameTime) override;                // 0x14 0x005b5490 (a thunk)
    void Tick(int timeDelta) override;                  // 0x18 0x005b54a0
    void OnMessage(C_BitStream *msg) override;          // 0x1C 0x005b58d0
    // Only a model frame will do.
    bool SetFrame(I3D_frame *frame) override;           // 0x2C 0x005b3710
    bool ReadProperties(C_chunk &chunk) override;       // 0x34 0x005b5af0
    void CopyFrom(const C_Actor *source) override;      // 0x38 0x005b3a00
    void StartMission(bool server) override;            // 0x3C 0x005b3be0
    void StopMission(bool server) override;             // 0x40 0x005b5000
    void RestartMission(bool server) override;          // 0x44 0x005b31c0
    bool SaveGame(C_chunk &chunk) override;             // 0x48 0x005b3210
    bool LoadGame(C_chunk &chunk) override;             // 0x4C 0x005b3450
    void WriteState(C_BitStream *msg) override;         // 0x50 0x005b6130
    void ReadState(C_BitStream *msg) override;          // 0x54 0x005b6150
    // Damage adds up (with hit points over 0, not stopped); past the hit points it is destroyed
    // on the next step.
    void Hit(E_HitType type, float amount, const S_vector &pos, const S_vector &dir, C_Actor *attacker,
             int flags) override;                       // 0xD4 0x005b5eb0
    int GetMaterialId(I3D_frame *part) override;        // 0xD8 0x005b5f20

private:
    // A collision box on a "BODYCOLB<nn>" frame (0xE0 bytes, 16-byte aligned).
    struct alignas(16) S_Collision {
        S_Collision(I3D_frame *frame, uint32_t flags, uint32_t type, C_Actor *actor);   // 0x005b4850
        ~S_Collision();                                 // 0x005b4dc0 (out of the collision world)
        // The box from the frame's bound (a dummy's box), its place from the frame's world matrix
        // without the scale; `add` also takes the material from the frame's "matid" property and
        // adds the volume to the world.
        void Refresh(bool add);                         // 0x005b48b0 (DS 0x005c6670)

        S_matrix invMatrix;                             // +0x00
        S_matrix matrix;                                // +0x40
        S_CollisionVolume volume;                       // +0x80
    };
    // A wing of "#mchescape" (0x20 bytes, 16-byte aligned): its bound's sphere swept along its moves.
    struct alignas(16) S_Wing {
        explicit S_Wing(I3D_frame *frame);              // 0x005b5f40
        ~S_Wing();                                      // 0x005b5f60
        void Reset();                                   // 0x005b5f70
        // Moved over 0.2 m: the sphere swept from the last place, a car met (or a human in one)
        // destroyed; true when that was a car.
        bool Sweep();                                   // 0x005b5fb0

        S_vector4 lastPos;                              // +0x00 the bound's centre (w: its radius)
        I3D_frame *frame;                               // +0x10 (holds a reference)
    };

    // The animation, the collision boxes and the wings (Update; Tick on servers).
    void Step(int time);                                // 0x005b5190
    // The animation run to its end; the collision boxes from the copy's frames.
    void Finish();                                      // 0x005b50c0
    // The boxes of `frame`'s "BODYCOLB00", "BODYCOLB01", ... (until one is missing).
    void CreateCollisions(I3D_frame *frame);            // 0x005b3ac0
    void AddCollision(I3D_frame *frame, uint32_t flags, uint32_t type);   // 0x005b4de0
    void DeleteCollisions();
    // The boxes and the copy gone, the frame shown, the damage reset.
    void ReleaseModel();                                // 0x005b4790
    // Collision primitives of `frame` switched on or off (COLLISION_FLAG_DISABLED).
    static void EnablePrimitives(I3D_frame *frame, bool enable);   // 0x005b5550
    // EnumFrames callback (`context`: the actor): the armour of the first hard material.
    static uint32_t __stdcall ArmourProc(I3D_frame *frame, void *context);   // 0x005b45b0
    // The copy's animation notes: "$<file> <near> <far>" plays a sound file, "@<group>" an action
    // sound, at the frame.
    static int __stdcall NoteProc(I3D_frame *frame, uint32_t message, uintptr_t note, uintptr_t);   // 0x005b4610

    C_str m_animName;                                   // +0x90 "Anim name"
    C_str m_collisionFrameName;                         // +0x94 "Collision frame name"
    I3D_frame *m_collisionFrame = nullptr;              // +0x98 (Stop finds it)
    I3D_frame *m_particleFrame = nullptr;               // +0x9C the frame's "particle"
    bool m_animating = false;                           // +0xA0 the copy's animation runs
    bool m_stopped = false;                             // +0xA1
    bool m_permanentRefresh = false;                    // +0xA2 "Permanent collision refresh"
    bool m_destroy = false;                             // +0xA3 the hit points ran out
    bool m_showNoInfo = false;                          // +0xA4 "Show no info flag"
    int m_explosionSound = -1;                          // +0xA8 "Sound ID from chapter explosion"
    int m_particleId = -1;                              // +0xAC "Particle ID"
    int m_nameTextId = -1;                              // +0xB0 "Actor name text ID"
    I3D_model *m_model = nullptr;                       // +0xB4 "<frame>#aanim", the animated copy
    bool m_animationHeld = false;                       // +0xB8 (HoldAnimation)
    int m_refreshes = 0;                                // +0xBC collision refreshes left after a load
    std::vector<S_Wing *> m_wings;                      // +0xC0
    float m_hitPoints = 0.0f;                           // +0xD0 "Hitpoints" (not over 0: indestructible)
    float m_damage = 0.0f;                              // +0xD4 (-1: destroyed)
    int m_armour = ANIMATION_ARMOUR_NONE;               // +0xD8 ANIMATION_ARMOUR_*
    std::vector<S_Collision *> m_collisions;            // +0xDC
};

// The animation actors' creator (g_actorCreators[ACTOR_ANIMATION], vtable 0x008119a8).
class C_AnimationActorCreator : public C_ActorCreator {
public:
    C_Actor *Create() override;                         // 0x005b3130
    const char *GetName() override { return "Animation"; }   // 0x005b3190
    void Destroy(C_Actor *actor) override;              // 0x005b31a0
};

// The mission editor's description of the actor's properties (ReadProperties' chunks): filled by
// the static initialiser 0x005b5910 at 0x008ac450 (0x24-byte records: kind, chunk, the name twice
// around a "%1" format, then min / max / initial value, a string's initial text at +0x18), read by
// nothing in the game.
struct S_AnimationActorProperty {
    uint16_t kind;                                      // 3 bool, 4 int, 5 float, 9 string
    uint16_t chunk;                                     // the ReadProperties chunk id
    const char *name;
    float min, max, initial;                            // (numbers; ints are stored as ints)
};
extern const S_AnimationActorProperty g_animationActorProperties[8];
