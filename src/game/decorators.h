// The decorators (game/decorators.cpp, original source 0x0047ab90..0x0047ea60): grass, flowers and
// other small models strewn on the ground around the camera (or the soldier), from the mission's
// decorations.bin (per ground material: a set of decorators with their chances) and
// decorator_sl.bin (a map of the ground's light). A grid of cells around the viewer holds one model
// each; animated decorators sway (their mesh's vertices moved), bendable ones bend away from the
// soldiers walking through. The GEffects module owns them (+0x20, g_decorators); the console's
// "decorators" switch and the detail option DETAIL_DECORATORS turn them off.
#pragma once

#include <cstdint>
#include <vector>

#include "common/c_str.h"
#include "ls3d/math.h"

class C_chunk;
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

    float chance;                                       // +0x00 the set's sum up to this one, % (1)
    float unknown04;                                    // +0x04 (0; version 4+)
    C_str model;                                        // +0x08 the model's name
    S_DecoratorPool *pool;                              // +0x0C the models of that name (Start)
    bool loadFailed;                                    // +0x10 the model did not load
    bool bendable;                                      // +0x11 (1)
    bool randomRotation;                                // +0x12 (1; version 5+)
    bool alignToGround;                                 // +0x13 (0; version 8+)
    float amplitude;                                    // +0x14 the sway's (0.2; version 6+)
    float frequency;                                    // +0x18 (1; version 7+)
    I3D_mesh *mesh = nullptr;                           // +0x1C the animated mesh
    std::vector<float *> vertices;                      // +0x20 its vertices that sway
    std::vector<S_vector4> restPositions;               // +0x30 theirs at rest (an aligned array, grown by 8)
    int animation;                                      // +0x40 0 none, 1 / 2 the sway along x / z (version 6+)
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
    float minSlope;                                     // +0x10 the ground's normal's y at least (0.95; version 3+)
    float groundCover;                                  // +0x14 (0.5; version 9+)
};

// The models of one name not in use (0x14 bytes).
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
    int set = 0;                                        // +0x00 the ground's material
    I3D_frame *model = nullptr;                         // +0x04
    int decorator = -1;                                 // +0x08 its index in the set
    S_DecoratorBend *bend = nullptr;                    // +0x0C
    bool bending = false;                               // +0x10
};

// A model bent away from a soldier (0x24 bytes).
struct S_DecoratorBend {
    I3D_frame *model = nullptr;                         // +0x00
    float unknown04 = 0.0f;                             // +0x04
    float unknown08 = 0.0f;                             // +0x08
    float unknown0C = 0.0f;                             // +0x0C
    float unknown10 = 0.0f;                             // +0x10
    float unknown14 = 0.0f;                             // +0x14
    int unknown18 = 0;                                  // +0x18
    int unknown1C = 8000;                               // +0x1C
    S_DecoratorCell *cell = nullptr;                    // +0x20
};

// The cells around the viewer (C_Decorators +0x04; 0x50 bytes).
class C_DecoratorGrid {
public:
    // The cells made (none when the grid is empty), far from everywhere.
    void Init();                                        // 0x0047e0f0
    // The bends deleted, the cells' models released, the cells deleted.
    void Free();                                        // 0x0047ea60

    int m_sizeX = 60;                                   // +0x00
    int m_sizeZ = 60;                                   // +0x04
    float m_cellSize = 0.5f;                            // +0x08 m
    int m_originX = -1;                                 // +0x0C in cells
    int m_originZ = -1;                                 // +0x10
    int m_shiftX = 0;                                   // +0x14 the ring buffer's start
    int m_shiftZ = 0;                                   // +0x18
    float m_unknown1C = 0.0f;                           // +0x1C
    float m_unknown20 = 0.0f;                           // +0x20
    // (Quirk kept: m_sizeX rows of m_sizeZ cells, indexed [z][x]; the two are always equal.)
    S_DecoratorCell **m_cells = nullptr;                // +0x24
    std::vector<S_DecoratorBend *> m_bends;             // +0x28
    int m_unknown38 = 0;                                // +0x38 the update's time
    float m_unknown3C = 0.05f;                          // +0x3C
    bool m_unknown40 = false;                           // +0x40
    int m_unknown44 = 0;                                // +0x44
    int m_unknown48 = 0;                                // +0x48
    bool m_firstPerson = false;                         // +0x4C the view at Init
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
    void Update(int timeDelta);                         // 0x0047bcc0
    // How much ground of material `material` hides a lying / crouching human (1 = none).
    float GetGroundCover(unsigned material);            // 0x0047b4c0

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

    friend struct S_DecoratorsTest;
};
extern C_Decorators *g_decorators;                      // 0x0089bbb0
// The detail option DETAIL_DECORATORS as last seen.
extern bool g_decoratorsEnabled;                        // 0x0089b749
