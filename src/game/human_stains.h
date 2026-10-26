// The blood stains on a human's body (C_HumanWeaponPose::m_stains; detailed single player
// only; original source 0x00796890..0x0079aa70).
#pragma once

#include <cstdint>

#include "ls3d/math.h"

class C_chunk;
class C_HumanWeaponPose;

// The stain textures shared by the humans (the singleton at 0x0098adf0; detailed single
// player only).
class C_StainManager {
public:
    void Init(int unknown);                             // 0x00796890 (the callers pass 0)
    void Reset();                                       // 0x00796bf0 (Init(0) again)
    void Done();                                        // 0x00796c00
};
extern C_StainManager g_stainManager;                   // 0x0098adf0

class C_HumanStains {
public:
    explicit C_HumanStains(C_HumanWeaponPose *pose);    // 0x00798660
    ~C_HumanStains();                                   // 0x007986b0
    C_HumanStains(const C_HumanStains &) = delete;
    C_HumanStains &operator=(const C_HumanStains &) = delete;

    // Binds the stains to the pose's model (`mode` of C_HumanWeaponPose::BindSkeleton).
    void Bind(char mode);                               // 0x00798720
    void SaveGame(C_chunk &chunk);                      // 0x00798930
    void LoadGame(C_chunk &chunk);                      // 0x00798bc0
    // Reads past the stains of a saved game (a body without them now).
    static void SkipSaved(C_chunk &chunk);              // 0x00798f70
    void SetVisible(bool on);                           // 0x007997b0
    // A hit at `pos` from `dir` (the zone bits of the hit).
    void AddHit(const S_vector &pos, const S_vector &dir, uint32_t flags);   // 0x00799bc0
};
