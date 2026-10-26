// Footprints and blood marks on the ground near humans (client side; original source
// 0x0078a890..0x0078ccf0): projector visuals ('PROJ') placed by the movement's footsteps and
// blood pools, faded out after a while or when there are too many of a human's, driven and
// saved by the items module.
#pragma once

#include <cstdint>

#include "ls3d/math.h"

class C_chunk;
class C_Human;
class I3D_frame;

// A footprint or blood mark (pooled; 0x14 bytes).
struct S_SurfaceMark {
    uint32_t time = 0;                                  // +0x00 mission time when placed (blood: ms of fading)
    int humanId = 0;                                    // +0x04 net id of the human who left it
    I3D_frame *visual = nullptr;                        // +0x08 the projector visual
    int kind = 0;                                       // +0x0C footprint kind (SurfaceMarks_FindKind)
    bool left = false;                                  // +0x10 left foot
};

// Footprints are made (and the footsteps test the ground for them): 1 after
// SurfaceMarks_Init, then the "footprints" detail setting.
extern uint32_t g_surfaceMarksEnabled;                  // 0x0098ad60

void SurfaceMarks_Init();                               // 0x0078a950 (first human of a mission)
void SurfaceMarks_Done();                               // 0x0078aa50 (last human)
// Takes all marks off the scene and keeps them for reuse (restarts, loads).
void SurfaceMarks_Clear();                              // 0x0078ac30
void SurfaceMarks_Free();                               // 0x0078ade0
// A new mark from the pool.
S_SurfaceMark *SurfaceMarks_NewMark(int unused);        // 0x0078a910 (a pool method; callers pass 1)
// Kind of footprint left on ground `material` (-2 / -3 bloody, -4 / -5 wet); -1: none.
int SurfaceMarks_FindKind(int material);                // (table 0x0084f548, inline in the footsteps)
// Puts a footprint at `pos` turned by `angle` (radians about y) into `sector`.
void SurfaceMarks_AddFootprint(S_SurfaceMark *mark, const S_vector &pos, float angle, I3D_frame *sector);   // 0x0078ae70
// Remembers a blood pool at `pos` (stepping into it makes bloody footprints).
void SurfaceMarks_AddBlood(const S_vector &pos);        // 0x0078b090
// A blood pool near `pos` (0.42 m across, 0.2 m up or down).
bool SurfaceMarks_IsBloodAt(const S_vector &pos);       // 0x0078b160
void SurfaceMarks_Tick(int timeDelta);                  // 0x0078b1d0
bool SurfaceMarks_Save(C_chunk &chunk);                 // 0x0078b6a0
bool SurfaceMarks_Load(C_chunk &chunk);                 // 0x0078bc50
// Hides (`hide`) or shows the footprints of a human (null: of everybody). The original's
// human fading passes the inverse (fading out shows them, fading in hides them); kept.
void SurfaceMarks_SetHumanVisible(C_Human *human, bool hide);   // 0x0078b5e0
