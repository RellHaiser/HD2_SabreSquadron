// A ladder (C_Ladder, actor type 8, vtable 0x00812a30, 0x190 bytes; original 0x005ea300..0x005ebde0):
// what humans climb. Its frame's "up*" and "down*" dummies give its ends; the frame's z axis, made
// level, the way a climber faces it. Two box volumes (COLLISION_TYPE_LADDER) hold the walkers off:
// the ladder's own box, raised to 0.3 m above its top, and a thin slab at the top's height. The
// human movement code asks CanUse and keeps the list of climbers.
#pragma once

#include <cstdint>
#include <vector>

#include "game/actor.h"
#include "game/collision.h"
#include "ls3d/math.h"

class C_Human;

class alignas(16) C_Ladder : public C_Actor {           // "Ladder", vtable 0x00812a30
public:
    C_Ladder();                                         // 0x005ea3d0
    // Fix: the volumes of a ladder destroyed while its mission runs are taken out of the world and
    // let go (the original leaves them, their actor gone).
    ~C_Ladder() override;                               // 0x005ea590 (scalar deleting 0x005ea570, DS 0x0060a8c0)

    // The world positions of its ends ("up*" and "down*") and the way a climber faces it (level).
    const S_vector4 &GetTop() const { return m_top; }   // +0xA0
    const S_vector4 &GetBottom() const { return m_bottom; }   // +0xB0
    const S_vector4 &GetDir() const { return m_dir; }   // +0xD0
    // The sound groups of its steps and of climbing it (-1: none).
    int GetStepSound() const { return m_stepSound; }    // +0x174
    int GetClimbSound() const { return m_climbSound; }  // +0x178
    // The byte at +0x180 (property version 3): looser angles to get on at the bottom, and getting
    // off there steps 1.25 m back and 0.25 m down instead of 0.15 m back (meaning not identified).
    bool GetUnknown180() const { return m_unknown180; }
    // Whether a human at `pos` facing `dir` moving at `velocity` (cm/s) can get on: at its bottom
    // (looking up, at most 0.5 m above it) facing and walking towards it, at its top (moving, not
    // looking up, at most 0.3 m below it) facing it and walking backwards over the edge.
    bool CanUse(const S_vector4 &pos, const S_vector4 &dir, const S_vector &velocity, bool lookingUp) const;   // 0x005eb1d0
    // The humans on it.
    const std::vector<C_Human *> &GetClimbers() const { return m_climbers; }   // +0x164
    void AddClimber(C_Human *human);                    // 0x005eb8c0
    // (The last one takes its place.)
    void RemoveClimber(C_Human *human);                 // 0x005eba70

    // (Update 0x005ea520, Tick 0x005ea530, OnMessage 0x005ea540 and slot 0x20 0x005ea550 are
    // empty, as are the base's.)
    // Its ends ("up*", "down*" dummies; false without them), the way to face it, the material
    // ("matid" in the first visual's user properties; 25 by default), then the base's.
    bool SetFrame(I3D_frame *frame) override;           // 0x2C 0x005ea5d0
    // Version 3, the unknown at +0x170, the sound groups, the unknown at +0x17C and +0x180.
    bool WriteProperties(C_chunk &chunk) override;      // 0x30 0x005ebaf0
    bool ReadProperties(C_chunk &chunk) override;       // 0x34 0x005ebbd0
    void CopyFrom(const C_Actor *source) override { C_Actor::CopyFrom(source); }   // 0x38 0x005ea4c0
    // Relinked to its parent (the world transform kept); once until StopMission: the volumes.
    void StartMission(bool server) override;            // 0x3C 0x005ead20
    void StopMission(bool server) override;             // 0x40 0x005eb120
    // Nobody on it.
    void RestartMission(bool) override { m_climbers.clear(); }   // 0x44 0x005ea4d0
    bool SaveGame(C_chunk &) override { return true; }  // 0x48 0x005ea500 (not the base's)
    bool LoadGame(C_chunk &) override { return true; }  // 0x4C 0x005ea510
    int GetMaterialId(I3D_frame *) override { return m_materialId; }   // 0xD8 0x005ea560

private:
    // The volumes out of the world and let go.
    void FreeVolumes();

    bool m_started = false;                             // +0x90
    S_vector4 m_top = {};                               // +0xA0 the "up*" dummy's world position
    S_vector4 m_bottom = {};                            // +0xB0 the "down*" dummy's
    S_CollisionVolume *m_volume = nullptr;              // +0xC0 the ladder's box (_aligned_malloc'd)
    S_CollisionVolume *m_topVolume = nullptr;           // +0xC4 the slab at its top
    S_vector4 m_dir = {};                               // +0xD0 the frame's z axis, level
    S_matrix m_matrix;                                  // +0xE0 the frame's world matrix without its scale
    S_matrix m_invMatrix;                               // +0x120 its inverse (the volumes')
    std::vector<C_Human *> m_climbers;                  // +0x160
    int m_unknown170 = -1;                              // +0x170 (property; not used here)
    int m_stepSound = -1;                               // +0x174
    int m_climbSound = -1;                              // +0x178
    int m_unknown17C = 1;                               // +0x17C (property version 2; not used here)
    bool m_unknown180 = false;                          // +0x180
    int m_materialId = 0;                               // +0x184 (set by SetFrame)
};

// The ladders' creator (g_actorCreators[ACTOR_LADDER], vtable 0x00812a24).
class C_LadderCreator : public C_ActorCreator {
public:
    C_Actor *Create() override;                         // 0x005ea330
    const char *GetName() override { return "Ladder"; } // 0x005ea3a0
    void Destroy(C_Actor *actor) override;              // 0x005ea3b0
};

// The human movement code holds the ladder it uses as an actor.
inline const C_Ladder *AsLadder(const C_Actor *ladder) { return static_cast<const C_Ladder *>(ladder); }
inline int GetLadderSoundGroup(const C_Actor *ladder) { return AsLadder(ladder)->GetClimbSound(); }
inline int GetLadderStepSoundGroup(const C_Actor *ladder) { return AsLadder(ladder)->GetStepSound(); }
inline const S_vector4 &GetLadderTop(const C_Actor *ladder) { return AsLadder(ladder)->GetTop(); }
inline const S_vector4 &GetLadderBottom(const C_Actor *ladder) { return AsLadder(ladder)->GetBottom(); }
inline const S_vector4 &GetLadderDir(const C_Actor *ladder) { return AsLadder(ladder)->GetDir(); }
inline const std::vector<C_Human *> &GetLadderClimbers(const C_Actor *ladder) { return AsLadder(ladder)->GetClimbers(); }
inline void AddLadderClimber(C_Actor *ladder, C_Human *human) { static_cast<C_Ladder *>(ladder)->AddClimber(human); }
inline void RemoveLadderClimber(C_Actor *ladder, C_Human *human) { static_cast<C_Ladder *>(ladder)->RemoveClimber(human); }
inline bool CanUseLadder(C_Actor *ladder, const S_vector4 &pos, const S_vector4 &dir, const S_vector &velocity,
                         bool lookingUp)
{
    return AsLadder(ladder)->CanUse(pos, dir, velocity, lookingUp);
}
inline bool GetLadderFlag180(const C_Actor *ladder) { return AsLadder(ladder)->GetUnknown180(); }
