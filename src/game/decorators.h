// The decorators (game/decorators.cpp, original source 0x0047ab90..0x00480360): grass, flowers and
// other small models strewn on the ground around the camera (or the soldier), from the mission's
// decorations.bin (per ground material: a set of decorators with their chances) and
// decorator_sl.bin (a map of the ground's light). A grid of cells around the viewer holds one model
// each, refilled as the viewer moves; animated decorators sway (their mesh's vertices moved),
// bendable ones bend away from the soldier and the cars going through. The GEffects module owns
// them (+0x20, g_decorators); the console's "decorators" switch and the detail option
// DETAIL_DECORATORS turn them off.
#pragma once

#include <cstdint>
#include <vector>

#include "common/c_str.h"
#include "ls3d/math.h"

class C_Car;
class C_chunk;
class C_Human;
class I3D_frame;
class I3D_mesh;

struct S_DecoratorPool;

// A decorator of a set (decorations.bin; 0x44 bytes).
struct S_Decorator {
    S_Decorator() { Reset(); }
    ~S_Decorator();                                     // 0x0047aed0
    S_Decorator(const S_Decorator &) = delete;
    S_Decorator &operator=(const S_Decorator &) = delete;
    // The defaults; the animated mesh released.
    void Reset();                                       // 0x0047d030
    // The animated mesh taken from `model`'s object visual (its first child): of each level the
    // vertices within 0.1 m of the line through the bound's top corner along its xz diagonal
    // (all of them when the diagonal is 0), with their positions at rest.
    void PrepareAnimated(I3D_frame *model);             // 0x0047baa0

    float chance;                                       // +0x00 the set's sum up to this one, % (1)
    float height;                                       // +0x04 when not 0, the models' y (0; version 4+)
    C_str model;                                        // +0x08 the model's name
    S_DecoratorPool *pool;                              // +0x0C the models of that name (Start)
    bool loadFailed;                                    // +0x10 the model did not load
    bool bendable;                                      // +0x11 (1)
    bool randomRotation;                                // +0x12 (1; version 5+)
    bool alignToGround;                                 // +0x13 (0; version 8+)
    float amplitude;                                    // +0x14 the sway's (0.2; version 6+)
    float frequency;                                    // +0x18 (1; version 7+)
    I3D_mesh *mesh = nullptr;                           // +0x1C the animated mesh (a reference)
    std::vector<float *> vertices;                      // +0x20 its vertices that sway
    std::vector<S_vector4> restPositions;               // +0x30 theirs at rest (an aligned array, grown by 8)
    int animation;                                      // +0x40 0 none, 1 / 2 the sway's two forms (version 6+)
};

// The decorators of a ground material (0x18 bytes).
struct S_DecoratorSet {
    S_DecoratorSet() { Reset(); }                       // 0x0047adc0
    ~S_DecoratorSet() { Reset(); }                      // 0x0047ae10
    S_DecoratorSet(const S_DecoratorSet &) = delete;
    S_DecoratorSet &operator=(const S_DecoratorSet &) = delete;
    // The decorators deleted; the defaults.
    void Reset();                                       // 0x0047ae60
    // From the set's chunk of decorations.bin (`version` its top chunk's id); false when the
    // chunk's header could not be read.
    bool Load(C_chunk &chunk, int version);             // 0x0047cc90

    std::vector<S_Decorator *> decorators;              // +0x04
    float minSlope;                                     // +0x10 the ground's normal's y above (0.95; version 3+)
    float groundCover;                                  // +0x14 (0.5; version 9+)
};

// The models of one name (0x14 bytes): the first the original, the others not in use.
struct S_DecoratorPool {
    explicit S_DecoratorPool(const char *name) : name(name) {}   // 0x0047ac40
    ~S_DecoratorPool();                                 // 0x0047ad10
    S_DecoratorPool(const S_DecoratorPool &) = delete;
    S_DecoratorPool &operator=(const S_DecoratorPool &) = delete;

    C_str name;                                         // +0x00 lowercase
    std::vector<I3D_frame *> models;                    // +0x08
};

// A cell of the grid (0x14 bytes).
struct S_DecoratorBend;
struct S_DecoratorCell {
    // A model of set `set` (the decorator picked at random), shown; `randomRotation`, `height`
    // and `alignToGround` the decorator's. (`seed`, the cell's, is passed on and not used.)
    I3D_frame *Place(int set, int seed, bool *randomRotation, float *height, bool *alignToGround);   // 0x0047e240
    // The model hidden and given back to its pool.
    void Clear();                                       // 0x0047e290

    int set = 0;                                        // +0x00 the ground's material
    I3D_frame *model = nullptr;                         // +0x04
    int decorator = -1;                                 // +0x08 its index in the set
    S_DecoratorBend *bend = nullptr;                    // +0x0C
    bool bending = false;                               // +0x10
};

// A model bent away from a soldier or a car (0x24 bytes).
struct S_DecoratorBend {
    // The roll on towards the target (a spring swings back at 0.9 the angle and speed until under
    // a degree), the light level back; false once it is done.
    bool Update(int time);                              // 0x0047f200

    I3D_frame *model = nullptr;                         // +0x00
    float target = 0.0f;                                // +0x04 radians of roll
    float angle = 0.0f;                                 // +0x08
    float speed = 0.0f;                                 // +0x0C
    float light = 0.0f;                                 // +0x10 the model's light level
    float lightTarget = 0.0f;                           // +0x14
    int springing = 0;                                  // +0x18
    int unknown1C = 8000;                               // +0x1C (not read)
    S_DecoratorCell *cell = nullptr;                    // +0x20
};

// The cells around the viewer (C_Decorators +0x04; 0x50 bytes): a ring buffer of m_sizeX x
// m_sizeZ cells, refilled row by row as the viewer moves.
class C_DecoratorGrid {
public:
    // The cells made (none when the grid is empty), far from everywhere.
    void Init();                                        // 0x0047e0f0
    // The bends deleted, the cells' models released, the cells deleted.
    void Free();                                        // 0x0047ea60
    // Centred on (x, z): the cells passed refilled (all when far off), then the bends around the
    // player's soldier (on foot) and the cars nearby.
    void Follow(float x, float z);                      // 0x0047eb80
    // The bends on (deleted when done).
    void UpdateBends(int time);                         // 0x0047f150

private:
    // Cell (row z, column x) of the window given a decorator anew: the ground below found, a
    // decorator picked by a seed from the cell's place, placed, lit, turned and scaled.
    void Refill(int z, int x);                          // 0x0047e2d0
    void RefillAll();                                   // 0x0047e930
    void RefillColumn(int x);                           // 0x0047ea00
    void RefillRow(int z);                              // 0x0047ea30
    // The window one cell on along x / z (back: the other way).
    void ShiftX(bool back);                             // 0x0047e960
    void ShiftZ(bool back);                             // 0x0047e9b0
    void BendAround(C_Human *human);                    // 0x0047f4e0
    void BendAround(C_Car *car);                        // 0x0047f730
    // The bendable models within `radius` of `pos` bent (or swung, beyond sqrt(innerSq)) away
    // from the move (dirX, dirZ) if within sqrt(outerSq); those inside darkened to `darken` of their
    // light. `fade`: their alpha by the distance in cells (the first person view).
    void Bend(const S_vector &pos, float dirX, float dirZ, float innerSq, float outerSq, float radius,
              float darken, bool fade);                 // 0x0047fae0

public:
    int m_sizeX = 60;                                   // +0x00
    int m_sizeZ = 60;                                   // +0x04
    float m_cellSize = 0.5f;                            // +0x08 m
    int m_originX = -1;                                 // +0x0C in cells
    int m_originZ = -1;                                 // +0x10
    int m_shiftX = 0;                                   // +0x14 the ring buffer's start
    int m_shiftZ = 0;                                   // +0x18
    int m_subX = 0;                                     // +0x1C the rows refilled of the next column
    int m_subZ = 0;                                     // +0x20 the columns refilled of the next row
    // (Quirk kept: m_sizeX rows of m_sizeZ cells, indexed [z][x]; the two are always equal.)
    S_DecoratorCell **m_cells = nullptr;                // +0x24
    std::vector<S_DecoratorBend *> m_bends;             // +0x28
    int m_time = 0;                                     // +0x38 the update's time
    float m_fadeAlpha = 0.05f;                          // +0x3C the near models' alpha (first person)
    bool m_fading = false;                              // +0x40 the alpha changing (a stance or view change)
    int m_fadeTime = 0;                                 // +0x44 ms of it left (1000)
    int m_stance = 0;                                   // +0x48 the soldier's last
    bool m_firstPerson = false;                         // +0x4C the view last seen
};

class C_Decorators {                                    // 0xA4 bytes
public:
    // (Both inlined in the GEffects module's constructor and destructor.)
    C_Decorators();
    ~C_Decorators();
    C_Decorators(const C_Decorators &) = delete;
    C_Decorators &operator=(const C_Decorators &) = delete;

    // The cells' models shown or hidden (only shown while the detail option allows).
    void SetOn(bool on);                                // 0x0047b010
    // Everything loaded deleted (the grid kept).
    void Clear();                                       // 0x0047c020
    void Free();                                        // 0x0047b090
    // The mission's decorations.bin and decorator_sl.bin.
    void Load();                                        // 0x0047c530
    // Each decorator given its model pool (the animated ones listed), the grid made; off
    // unless the detail option allows.
    void Start();                                       // 0x0047b0b0
    // The detail option followed; the animated decorators swayed, the grid moved to the camera
    // (the player's soldier, unless in a cutscene or the tactical view).
    void Update(int timeDelta);                         // 0x0047bcc0
    // How much ground of material `material` hides a lying / crouching human (1 = none).
    float GetGroundCover(unsigned material);            // 0x0047b4c0
    // The ground's normal's y a set needs above (1000: none).
    float GetMinSlope(unsigned material);               // 0x0047b480
    // A model of set `set`'s decorator picked by RandomFloat(100) (`*decorator` its index; null
    // when none is picked or its model does not load); a pool's spare, a copy of its original,
    // or the original loaded (a copy kept in the pool).
    // (`seed` not used.)
    I3D_frame *TakeModel(unsigned set, int seed, int *decorator, bool *randomRotation, float *height,
                         bool *alignToGround);          // 0x0047b500
    void ReturnModel(int set, I3D_frame *model, int decorator);   // 0x0047b8e0
    // The light map at (x, z), 0..1 (bilinear; 0 without the map).
    float SampleLight(float x, float z);                // 0x0047d110

private:
    // decorator_sl.bin.
    void LoadLightMap();                                // 0x0047c930
    void FreeLightMap();                                // 0x0047d0c0

    bool m_on = false;                                  // +0x00
    C_DecoratorGrid m_grid;                             // +0x04
    std::vector<S_DecoratorPool *> m_pools;             // +0x54
    std::vector<S_Decorator *> m_animated;              // +0x64
    // The light map: a byte per cell, m_lightWidth wide, from (m_lightX, m_lightZ).
    float m_lightX = 0.0f;                              // +0x74
    float m_lightZ = 0.0f;                              // +0x78
    int m_unknown7C = 0;                                // +0x7C
    int m_unknown80 = 0;                                // +0x80
    uint8_t *m_light = nullptr;                         // +0x84
    float m_lightCellSize = 0.0f;                       // +0x88
    int m_lightWidth = 0;                               // +0x8C
    int m_lightHeight = 0;                              // +0x90
    std::vector<S_DecoratorSet *> m_sets;               // +0x94 by ground material

    friend class C_DecoratorGrid;
    friend struct S_DecoratorsTest;
};
extern C_Decorators *g_decorators;                      // 0x0089bbb0
// The detail option DETAIL_DECORATORS as last seen.
extern bool g_decoratorsEnabled;                        // 0x0089b749
