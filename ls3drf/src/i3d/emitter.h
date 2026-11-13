// I3D_emitor: the particle emitter visual (EMTR; vtable 0x1009c050, 0x300 bytes; 0x1003aa80 -
// 0x1003ed10).
//
// While emitting (EmitOn) it lets out a particle every interval (intervalMin + random up to
// intervalRandom ms) at its position (spread at random up to positionSpread), moving along a
// direction that turns from the start velocity's to the end velocity's over the particle's life
// (or towards new random directions around the end one every turn interval), at a speed going
// from the start velocity's length to the end one's. A particle grows (growth per second, between
// two limits), spins, fades in and out over fractions of its life and changes its color from the
// start color to the end one; it is drawn as a quad facing the camera, its texture rectangle the
// frame of its group's animation (AddGroup) at its age.
//
// In LS3DF all emitters write their quads into two shared dynamic vertex buffers in turn (0x5554
// vertices each; one shared index buffer of quads): Prepare locks space for the visible particles
// of the pass, Draw draws them from where Prepare put them. Instead of particles an application can
// write quads of its own through LockVertices (SetUserVertices). LS3DRF has none of it.
//
// LS3DRF's emitor is a 0x2d0-byte object (vtable 0x1003ce38): LS3DF's fields up to fixedGroup, then
// the emit wait and count, and no particles, particle lists or quad index (LS3DF's +0x208 - +0x23B);
// everything after sits 0x34 lower, 0x30 from the start velocity on (it is 16-byte aligned).
#pragma once

#include "core/c_vector.h"
#include "core/layout.h"
#include "i3d/visual.h"

class I3D_emitor;
class ITexture;

// I3D_emitor::emitFlags (SetParam EMTP_FLAGS; masked by EMITF_MASK).
enum : uint32_t {
    EMITF_OPAQUE = 0x00000001,              // drawn without blending
    EMITF_BLEND = 0x00000002,               // source alpha / inverse source alpha (else additive)
    EMITF_UNKNOWN4 = 0x00000004,            // (kept by SetParam, not used)
    EMITF_CUSTOM_BLEND = 0x00000080,        // srcBlend / destBlend
    EMITF_SORTED = 0x00000100,              // the particles drawn back to front
    EMITF_MASK = 0x00000187,
};

// I3D_emitor::SetParam / GetParam indices.
enum E_EMITOR_PARAM {
    EMTP_INTERVAL = 0x00,                   // int ms between particles (resets the wait)
    EMTP_INTERVAL_RANDOM = 0x01,            // int ms added at random (a new wait drawn)
    EMTP_LIFE = 0x02,                       // int ms
    EMTP_LIFE_RANDOM = 0x03,                // int ms added at random
    EMTP_GROWTH_A = 0x04,                   // float scale change per second: random between A ...
    EMTP_GROWTH_B = 0x05,                   // ... and B
    EMTP_POSITION_SPREAD = 0x06,            // float
    EMTP_DIRECTION_SPREAD = 0x07,           // float (the end direction's)
    EMTP_TURN_INTERVAL = 0x08,              // int ms (0: the direction turns once over the life)
    EMTP_TURN_INTERVAL_RANDOM = 0x09,       // int ms
    EMTP_TURN_SPREAD = 0x0A,                // float
    EMTP_FADE_IN = 0x0B,                    // float fraction of the life (negative: random start opacity)
    EMTP_FADE_OUT = 0x0C,                   // float fraction of the life
    EMTP_START_VELOCITY_X = 0x0D,           // float x 3 (direction and speed in units per second)
    EMTP_END_VELOCITY_X = 0x10,             // float x 3
    EMTP_FLAGS = 0x13,                      // EMITF_*
    EMTP_MAX_PARTICLES = 0x14,              // int particles let out before emitting stops (0: no limit); starts emitting
    EMTP_START_VELOCITY_WORLD = 0x15,       // bool: the start velocity is in world space (else the frame's)
    EMTP_END_VELOCITY_WORLD = 0x16,         // bool
    EMTP_WARM_UP = 0x17,                    // int ms ticked at once (when set)
    EMTP_ROTATION_MIN = 0x18,               // float radians per second
    EMTP_ROTATION_MAX = 0x19,               // float
    EMTP_START_SCALE = 0x1A,                // float
    EMTP_NUM_PARTICLES = 0x1B,              // (GetParam) the particle slots made
    EMTP_START_COLOR = 0x1D,                // const S_vector4 * (r, g, b clamped to 0 - 1)
    EMTP_END_COLOR = 0x1E,                  // const S_vector4 *
    EMTP_OPACITY = 0x22,                    // float 0 - 1
    EMTP_COUNT = 0x23,
};

// The frames of a particle animation (AddGroup; 0x3C bytes): a grid of `columns` x `rows`
// rectangles of `rect`, or the rectangles in `frames` when there are any.
struct S_emitorGroup {
    S_emitorGroup() : frames(8) {}           // 0x1003ecf0
    S_emitorGroup(const S_emitorGroup &) = delete;
    // 0x1003e820
    S_emitorGroup &operator=(const S_emitorGroup &src);

    S_vector4 rect;                         // +0x00 u0, v0, u1, v1
    int columns;                            // +0x10 (at least 1)
    int rows;                               // +0x14 (at least 1)
    int numFrames;                          // +0x18 (1 - columns x rows)
    int loopFrame;                          // +0x1C the frame the animation loops back to
    int frameTime;                          // +0x20 ms per frame
    ls3d::C_vector<S_vector4> frames;       // +0x24 (grow 8) each frame's u0, v0, u1, v1
    int loopStart;                          // +0x34 frameTime x numFrames: the age the loop starts at
    int loopLength;                         // +0x38 (numFrames - loopFrame) x frameTime
};
LS3D_LAYOUT32(sizeof(S_emitorGroup) == 0x3C, "S_emitorGroup layout");

class I3D_emitor : public I3D_visual {
public:
    I3D_emitor();
    // LS3DRF 0x100100d0: the scalar deleting destructor (0x5C).
    ~I3D_emitor() override;

    // 0x20 0x1003c530: the particles aged; while emitting, the particles due let out.
    void __stdcall Tick(int time) override;
    // 0x4C 0x1003d340: the frame and visual; of another emitter its texture, groups and parameters
    // (not the opacity), of an object visual its first face group's texture.
    I3D_RESULT __stdcall Duplicate(I3D_frame *source) override;
    void Init() override;                                                      // 0x60 0x1003ae80
    void Uninit() override;                                                    // 0x64 0x1003af10
    // 0x70 0x1003d6c0: the frame, then the texture (flags as a material's, name, alpha name).
    void Load() override;
    virtual void __stdcall SetTexture(ITexture *newTexture);                  // 0x94 0x1003af50
    // 0x98 0x1003af80: EMTP_* (I3DERR_INVALIDPARAMS for other indices and null colors).
    virtual I3D_RESULT __stdcall SetParam(int index, uint32_t value);
    virtual uint32_t __stdcall GetParam(int index);                            // 0x9C 0x1003b5e0 (0 for others)
    virtual void __stdcall SetDefaults();                                      // 0xA0 0x1003b8a0
    // 0xA4 0x1003bfb0: emitting on or off (on: the wait and the emitted count start again).
    virtual void __stdcall EmitOn(bool on);
    // LS3DRF 0x10011a60 (from LS3DF 0x1001ba90)
    virtual void __stdcall SetSrcBlend(uint32_t blend) { srcBlend = blend; }    // 0xA8 0x1001ba90
    // LS3DRF 0x10011a80 (from LS3DF 0x1001bab0)
    virtual uint32_t __stdcall GetSrcBlend() { return srcBlend; }             // 0xAC 0x1001bab0
    // LS3DRF 0x10011a90 (from LS3DF 0x1001bac0)
    virtual void __stdcall SetDestBlend(uint32_t blend) { destBlend = blend; }  // 0xB0 0x1001bac0
    // LS3DRF 0x10011ab0 (from LS3DF 0x1001bae0)
    virtual uint32_t __stdcall GetDestBlend() { return destBlend; }           // 0xB4 0x1001bae0
    // 0xB8 0x1001baf0: the group every particle gets (-1: one at random).
    virtual void __stdcall SetGroup(int group) { fixedGroup = group; }
    // 0xBC 0x1003ba20: the group at `index` replaced (-1: a new one added); returns its index.
    virtual int __stdcall AddGroup(const S_emitorGroup &group, int index);
    // 0xC0 0x1003bed0: the group at `index` taken out (the last one moves into its place).
    virtual void __stdcall RemoveGroup(int index);
    // 0xC4 0x1003d6a0: every particle gone (the slots too).
    virtual void __stdcall ClearParticles();
    // 0xC8 0x1003d0e0: the local bound around the particles.
    virtual void __stdcall UpdateParticleBound();
    // (No LockVertices, LS3DF's slot 0xD4 0x1003d860: LS3DRF has none.)

    bool emitting;                          // +0x1E0 (SetDefaults: on)
    bool userVertices;                      // +0x1E1 the quads come from the application (LS3DF's LockVertices)
    uint8_t padding1E2[2];
    int userQuads;                          // +0x1E4 ... their count
    float userDepth;                        // +0x1E8 ... their sort depth
    uint32_t emitFlags;                     // +0x1EC EMITF_* (0x102)
    ITexture *texture;                      // +0x1F0 (a reference)
    S_emitorGroup *groups;                  // +0x1F4 (array new)
    int groupCapacity;                      // +0x1F8
    int numGroups;                          // +0x1FC
    int groupGrowBy;                        // +0x200 (2)
    int fixedGroup;                         // +0x204 (-1: random)
    uint32_t emitTime;                      // +0x208 ms waited for the next particle (LS3DF: +0x23C)
    uint32_t emitWait;                      // +0x20C ms the next particle waits (LS3DF: +0x240)
    int numEmitted;                         // +0x210 since emitting started (LS3DF: +0x244)
    // (No particle slots, live and free lists or quad index, LS3DF's +0x208 - +0x23B: LS3DRF has
    // none.)
    uint32_t srcBlend;                      // +0x214 D3DBLEND (EMITF_CUSTOM_BLEND; 5; LS3DF: +0x248)
    uint32_t destBlend;                     // +0x218 (6) (LS3DF: +0x24C)
    int intervalMin;                        // +0x21C (100) (LS3DF: +0x250)
    int intervalRandom;                     // +0x220 (60) (LS3DF: +0x254)
    int lifeMin;                            // +0x224 (1800) (LS3DF: +0x258)
    int lifeRandom;                         // +0x228 (600) (LS3DF: +0x25C)
    float startScale;                       // +0x22C (1) (LS3DF: +0x260)
    float growthA;                          // +0x230 (-0.25) (LS3DF: +0x264)
    float growthB;                          // +0x234 (0.6) (LS3DF: +0x268)
    float positionSpread;                   // +0x238 (LS3DF: +0x26C)
    float directionSpread;                  // +0x23C (LS3DF: +0x270)
    float fadeIn;                           // +0x240 (0.5) (LS3DF: +0x274)
    float fadeOut;                          // +0x244 (0.5) (LS3DF: +0x278)
    int turnIntervalMin;                    // +0x248 (LS3DF: +0x27C)
    int turnIntervalRandom;                 // +0x24C (LS3DF: +0x280)
    float turnSpread;                       // +0x250 (0.1) (LS3DF: +0x284)
    uint32_t unknown254[3];                 // +0x254 (LS3DF: +0x288, 2 words)
    S_vector4 startVelocity;                // +0x260 (0, 2, 0) (LS3DF: +0x290)
    S_vector4 endVelocity;                  // +0x270 (2, 0, 0) (LS3DF: +0x2A0)
    int maxEmitted;                         // +0x280 (0: no limit) (LS3DF: +0x2B0)
    bool startVelocityWorld;                // +0x284 (LS3DF: +0x2B4)
    bool endVelocityWorld;                  // +0x285 (LS3DF: +0x2B5)
    uint8_t padding286[2];
    float rotationMin;                      // +0x288 (-0.4 pi per second) (LS3DF: +0x2B8)
    float rotationMax;                      // +0x28C (0.4 pi) (LS3DF: +0x2BC)
    int warmUp;                             // +0x290 (LS3DF: +0x2C0)
    uint32_t unknown294[3];                 // +0x294 (LS3DF: +0x2C4)
    S_vector4 startColor;                   // +0x2A0 (1, 1, 1) (LS3DF: +0x2D0)
    S_vector4 endColor;                     // +0x2B0 (1, 1, 1) (LS3DF: +0x2E0)
    float opacity;                          // +0x2C0 (1) (LS3DF: +0x2F0)
    uint32_t padding2C4[3];                 // +0x2C4 (LS3DRF's pad to 0x2d0: nothing addresses it)

private:
    void FreeGroups();
};
LS3D_LAYOUT32(sizeof(I3D_emitor) == 0x2D0, "I3D_emitor layout");
LS3D_LAYOUT32(offsetof(I3D_emitor, texture) == 0x1F0, "I3D_emitor layout");
LS3D_LAYOUT32(offsetof(I3D_emitor, fixedGroup) == 0x204, "I3D_emitor layout");
LS3D_LAYOUT32(offsetof(I3D_emitor, srcBlend) == 0x214, "I3D_emitor layout");
LS3D_LAYOUT32(offsetof(I3D_emitor, startVelocity) == 0x260, "I3D_emitor layout");
LS3D_LAYOUT32(offsetof(I3D_emitor, startColor) == 0x2A0, "I3D_emitor layout");
LS3D_LAYOUT32(offsetof(I3D_emitor, opacity) == 0x2C0, "I3D_emitor layout");
