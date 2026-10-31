// Particle effects — source files 0x0043f2a0 (particles), 0x00440440 (emitters) and
// 0x00443ce0 (C_ParticlesModule).
//
// tables\effects.def defines the effects. An effect has a texture, a blend mode and
// one or more particle kinds (S_ParticleDef); a particle kind says how many particles
// are emitted (a burst at the start, then a continuous rate that may fade out and
// repeat), where they start, how big and opaque they are and which texture frames
// they animate through. A particle moves through motion phases (S_ParticlePhase):
// each gives a direction, a duration, gravity, acceleration, spin and the size,
// opacity and color the particle reaches at the end of the phase.
//
// A running effect is a C_ParticleEmitter. Its particles are drawn as textured
// quads of one engine particle visual, rebuilt every frame (back to front, at most
// 500) while the visual was on screen in the previous frame.
//
// The module keeps the effects started by game code (identified by handles that
// grow with every start) and the mission's static emitters (missions\<name>\
// effects.bin, placed in the editor). Finished emitters are kept per effect for
// reuse.
#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <vector>

#include "common/c_str.h"
#include "game/module.h"
#include "ls3d/math.h"

class C_chunk;
class I3D_frame;
class I3D_scene;
class I3D_particle_visual;

// Moves `dir` by a random amount within `variance` (along the variance and around
// the direction) and renormalizes it. Not called in the game.
void RandomizeDirection(S_vector &dir, const S_vector &variance);   // 0x0043f2d0

// Texture rectangle of one animation frame.
struct S_ParticleFrame {
    float u0, v0;
    float u1, v1;
};

// Kinds of effect definition objects (S_EffectItem::m_type).
enum E_EffectItemType {
    EFFECT_ITEM_EFFECT = 1,
    EFFECT_ITEM_PARTICLE = 2,
    EFFECT_ITEM_PHASE = 3,
};

// Common base of the definitions (vtable 0x0080f9e8).
struct S_EffectItem {
    explicit S_EffectItem(int type) : m_type(type) {}
    virtual ~S_EffectItem() = default;                      // 0x00444840

    int m_type;                                             // +0x04 E_EffectItemType
};

// Motion phase of a particle (effects.def chunk 0xCB), vtable 0x0080f9ec, 0x90 bytes.
struct S_ParticlePhase : S_EffectItem {
    S_ParticlePhase() : S_EffectItem(EFFECT_ITEM_PHASE) {}

    C_str m_name;                                           // +0x08 chunk 0x12D (not used)
    S_vector m_dir = {0, 0, 0};                             // +0x10 zero = +z; emitter space
    float m_dirW = 0.0f;                                    //       unless the particle is world space
    S_vector m_dirVariance = {0, 0, 0};                     // +0x20
    int m_duration = 1000;                                  // +0x30 ms
    int m_durationVariance = 0;                             // +0x34
    float m_gravity = 0.0f;                                 // +0x38 direction change per second
    float m_rotSpeed = 0.0f;                                // +0x3C radians per second
    float m_rotSpeedVariance = 0.0f;                        // +0x40
    float m_endSize = 1.0f;                                 // +0x44 size at the end of the phase
    float m_endSizeVariance = 0.0f;                         // +0x48
    S_vector m_color = {0.5f, 0.5f, 0.5f};                  // +0x50 RGB, 0..1
    float m_colorW = 0.0f;
    S_vector m_colorVariance = {0, 0, 0};                   // +0x60 scaled by one random factor
    float m_unknown70 = 1.0f;                               // +0x70 chunk 0x12E (not used)
    float m_unknown74 = 0.0f;                               // +0x74 chunk 0x12E (not used)
    float m_endAlpha = 0.0f;                                // +0x78 opacity at the end of the phase
    float m_endAlphaVariance = 0.0f;                        // +0x7C
    float m_accel = 0.0f;                                   // +0x80 speed change per second
    float m_accelVariance = 0.0f;                           // +0x84
};

// S_ParticleDef::m_flags.
enum : uint32_t {
    // Particles stay where they were emitted (absolute world positions and
    // directions) instead of moving along with the emitter.
    PARTICLE_DEF_WORLD_SPACE = 1,
};

// One particle kind of an effect (effects.def chunk 0x67), vtable 0x0080f9e4, 0xA0 bytes.
struct S_ParticleDef : S_EffectItem {
    S_ParticleDef() : S_EffectItem(EFFECT_ITEM_PARTICLE) {}
    ~S_ParticleDef() override;                              // 0x00444eb0

    void Load(C_chunk &chunk);                              // 0x004448a0

    C_str m_name;                                           // +0x08 chunk 0xC9 (not used)
    std::vector<S_ParticleFrame> m_frames;                  // +0x0C texture animation
    int m_loopFrame = 0;                                    // +0x1C continues here after the last frame
    int m_frameTime = 100;                                  // +0x20 ms per frame
    int m_burstMin = 0;                                     // +0x24 particles emitted at the start:
    int m_burstMax = 0;                                     // +0x28 min + random * max
    int m_rate = 0;                                         // +0x2C particles per second afterwards
    int m_unknown30 = 0;                                    // +0x30 (not used)
    int m_fadeStart = 0;                                    // +0x34 ms after which the rate drops
    int m_fadeRate = 0;                                     // +0x38 rate drop per second
    int m_repeat = 0;                                       // +0x3C emission cycles, -1 = forever
    int m_delay = 0;                                        // +0x40 ms before a repeated cycle
    uint32_t m_flags = 0;                                   // +0x44 PARTICLE_DEF_*
    S_vector m_pos = {0, 0, 0};                             // +0x50 start position (emitter space)
    S_vector m_posVariance = {0, 0, 0};                     // +0x60
    float m_size = 1.0f;                                    // +0x70
    float m_sizeVariance = 0.0f;                            // +0x74
    float m_speed = 1.0f;                                   // +0x78 units per second
    float m_speedVariance = 0.0f;                           // +0x7C
    float m_alpha = 1.0f;                                   // +0x80
    float m_alphaVariance = 0.0f;                           // +0x84
    float m_rotation = 0.0f;                                // +0x88 radians
    float m_rotationVariance = 0.0f;                        // +0x8C
    std::vector<std::unique_ptr<S_ParticlePhase>> m_phases; // +0x90
};

// S_EffectDef::m_blendMode: the visual's blend render state is 1 << mode, except
// for BLEND_MODE_CUSTOM, which uses m_srcBlend / m_dstBlend (D3DBLEND values).
enum : int {
    BLEND_MODE_CUSTOM = 3,
};

// An effect of tables\effects.def (chunk 0x3F2), vtable 0x0080f9e0, 0x38 bytes.
struct S_EffectDef : S_EffectItem {
    S_EffectDef() : S_EffectItem(EFFECT_ITEM_EFFECT) {}
    ~S_EffectDef() override;                                // 0x00445070

    void Load(C_chunk &chunk);                              // 0x00444490
    // Has a texture and particle kinds, each with frames, and some kind has phases.
    bool IsUsable() const;                                  // 0x004454f0

    C_str m_name;                                           // +0x08 chunk 0x65
    C_str m_texture;                                        // +0x0C chunk 0x66
    bool m_lit = false;                                     // +0x10 brightness follows the scene lights
    int m_blendMode = 1;                                    // +0x14
    float m_range = 20.0f;                                  // +0x18 (only stored, squared)
    float m_unknown1C = 50.0f;                              // +0x1C (not used)
    int m_srcBlend = 5;                                     // +0x20 D3DBLEND_SRCALPHA
    int m_dstBlend = 6;                                     // +0x24 D3DBLEND_INVSRCALPHA
    std::vector<std::unique_ptr<S_ParticleDef>> m_particles;   // +0x28
};

// S_Particle::m_flags.
enum : uint32_t {
    PARTICLE_WORLD_SPACE = 1,       // position is absolute (else relative to the emitter)
};

// One live particle (taken from g_particlePool), 0xB0 bytes.
struct alignas(16) S_Particle {
    bool Init(const S_matrix &emitterMatrix, S_ParticleDef *def, int time, int defIndex);   // 0x0043f520
    // Starts motion phase m_phase; false when the particle has no more phases.
    bool StartPhase(const S_matrix &emitterMatrix);         // 0x0043f730
    void Tick(int time);                                    // 0x0043fb40
    void Save(C_chunk &chunk) const;                        // 0x0043fdf0
    void Load(C_chunk &chunk);                              // 0x00440080

    // Position in the world (`emitterMatrix` = the emitter's world matrix).
    S_vector GetWorldPos(const S_matrix &emitterMatrix) const
    {
        return (m_flags & PARTICLE_WORLD_SPACE) ? m_matrix(3) : m_matrix(3) + emitterMatrix(3);
    }

    S_matrix m_matrix;                                      // +0x00 row 2 = direction, row 3 = position
    int m_frame = 0;                                        // +0x40 texture frame
    int m_numFrames = 0;                                    // +0x44
    int m_frameTimer = 0;                                   // +0x48 -1 = not animated
    float m_size = 0.0f;                                    // +0x4C
    float m_sizeSpeed = 0.0f;                               // +0x50 per ms
    float m_rotation = 0.0f;                                // +0x54 radians
    float m_rotSpeed = 0.0f;                                // +0x58 per ms
    int m_lifetime = 0;                                     // +0x5C ms left in the phase
    float m_alpha = 0.0f;                                   // +0x60
    float m_alphaSpeed = 0.0f;                              // +0x64 per ms
    float m_speed = 0.0f;                                   // +0x68 units per second
    float m_accel = 0.0f;                                   // +0x6C per ms
    float m_gravity = 0.0f;                                 // +0x70 per ms
    S_vector m_color = {0, 0, 0};                           // +0x80
    float m_colorW = 0.0f;
    S_ParticleDef *m_def = nullptr;                         // +0x90
    int m_phase = 0;                                        // +0x94
    S_ParticleFrame m_uv = {};                              // +0x98 current frame (saved, not drawn)
    uint32_t m_flags = 0;                                   // +0xA8 PARTICLE_*
    int m_defIndex = 0;                                     // +0xAC in the effect's particle kinds
};

// Free particles, refilled in blocks (g_particlePool, vtable 0x0080f990).
class C_ParticlePool {
public:
    explicit C_ParticlePool(int growBy);                    // 0x00440450
    ~C_ParticlePool();                                      // 0x004404a0
    C_ParticlePool(const C_ParticlePool &) = delete;
    C_ParticlePool &operator=(const C_ParticlePool &) = delete;

    S_Particle *Alloc();
    void Free(S_Particle *particle);

private:
    void Grow();                                            // 0x004437c0

    std::vector<S_Particle *> m_free;                       // (a node list in the original)
    int m_growBy;                                           // +0x0C
};

extern C_ParticlePool g_particlePool;                       // 0x008999d4

// Emission state of one particle kind of an emitter (saved as chunk 0x2AFA).
struct S_EmitterState {
    int delay;                                              // +0x00 ms until emission (re)starts
    int timer;                                              // +0x04 ms since the last emitted particle
    int age;                                                // +0x08 ms of the current cycle
    int interval;                                           // +0x0C ms between particles, 0 = none
    float rate;                                             // +0x10 particles per second
    int repeat;                                             // +0x14 cycles left, -1 = forever
};
static_assert(sizeof(S_EmitterState) == 0x18, "S_EmitterState is saved as is");

// C_ParticleEmitter::m_flags.
enum : uint32_t {
    EMITTER_EMITTING = 1,           // keeps emitting particles
    EMITTER_FINISHING = 2,          // Finish() was called
    EMITTER_HIDDEN = 4,             // nothing is simulated or drawn
};

// A running effect (0xE0 bytes): its particles and the visual that draws them.
class alignas(16) C_ParticleEmitter {
public:
    C_ParticleEmitter();                                    // 0x00440530
    // Emitter restored from a saved game (Load() supplies the rest).
    C_ParticleEmitter(S_EffectDef *def, I3D_scene *scene);  // 0x004405e0
    ~C_ParticleEmitter();                                   // 0x004406a0
    C_ParticleEmitter(const C_ParticleEmitter &) = delete;
    C_ParticleEmitter &operator=(const C_ParticleEmitter &) = delete;

    // Starts `def` at `matrix` (world space); with a parent the emitter moves along
    // with it. `faceCamera`: quads face the camera, else they face along the
    // emitter's z axis.
    void Init(I3D_scene *scene, I3D_frame *parent, const S_matrix &matrix, S_EffectDef *def,
              bool faceCamera);                             // 0x00440800
    // Starts the same effect again (reused emitter).
    void Restart(I3D_scene *scene, I3D_frame *parent, const S_matrix &matrix,
                 bool faceCamera);                          // 0x00440b20
    // Hides the visual and lets go of the parent (before reuse or deletion).
    void Stop();                                            // 0x00440d80
    // Simulates `time` ms and rebuilds the quads; false once the effect is over.
    bool Tick(int time);                                    // 0x004427a0
    // Rebuilds the quads without simulating (game paused; the frame time passed is not used).
    void Draw(int time);                                    // 0x00442860
    // Stops emitting and leaves the parent, so the remaining particles die out
    // where they are.
    void Finish();                                          // 0x00443210
    void ToggleEmission() { m_flags ^= EMITTER_EMITTING; }  // (inline in 0x004458b0)

    void Save(C_chunk &chunk) const;                        // 0x004428e0
    void Load(C_chunk &chunk);                              // 0x00442c80

private:
    // Common part of Init() and Restart().
    void Start(I3D_scene *scene, I3D_frame *parent, const S_matrix &matrix, bool faceCamera,
               bool firstStart);
    // Loads the effect's texture into the visual and sets its blending.
    void SetupMaterial();
    // Follows the parent: m_matrix = m_localMatrix * parent's world matrix.
    void UpdateMatrix();
    // The visual was drawn in the last rendered frame (or was never drawn yet).
    bool WasVisible();

    // (The original also takes the matrix to emit from; its callers pass &m_matrix.)
    void Emit(S_ParticleDef *def, int time, int defIndex);  // 0x00440dc0
    void EmitBursts();                                      // 0x00440fe0
    void EmitContinuous(int time);                          // 0x004410b0
    void RemoveDead();                                      // 0x004412d0
    void TickParticles(int time);                           // 0x00441390
    // Average view depth, bounds of the visual and (if `collect`) the draw order.
    void Sort(bool collect);                                // 0x00441460
    void BuildQuads();                                      // 0x00441a00
    // Always camera facing variant of BuildQuads() (not called in the game).
    void BuildQuadsCameraAligned();                         // 0x00442100

    I3D_scene *m_scene = nullptr;                           // +0x00
    I3D_particle_visual *m_visual;                          // +0x04 "MR_Particle", own reference
    S_matrix m_matrix;                                      // +0x10 world
    S_matrix m_localMatrix;                                 // +0x50 relative to m_parent
    I3D_frame *m_parent = nullptr;                          // +0x90 holds a reference
    int m_unknown94 = 0;                                    // +0x94 set to 100 (not used)
    float m_rangeSq = 0.0f;                                 // +0x98 effect range squared (not used)
    uint32_t m_flags = 0;                                   // +0x9C EMITTER_*
    float m_brightness = 1.0f;                              // +0xA0 scene light at the start position
    bool m_empty = false;                                   // +0xA4 no particles and none to come
    float m_depth = 0.0f;                                   // +0xA8 average view depth
    S_EffectDef *m_def = nullptr;                           // +0xAC
    std::vector<S_Particle *> m_particles;                  // +0xB0
    std::vector<S_EmitterState> m_states;                   // +0xC0 one per particle kind
    bool m_faceCamera = false;                              // +0xD0
};

// Running emitter of the module.
struct S_ActiveEmitter {
    C_ParticleEmitter *emitter;                             // +0x00
    int handle;                                             // +0x04 -1 for static emitters
    int effect;                                             // +0x08 index in tables\effects.def
    bool transient;                                         // +0x0C not stored in saved games
};

class C_ParticlesModule : public C_Module {                 // "Particles", vtable 0x0080f9a0, 0x1048 bytes
public:
    // Free emitters are kept for this many effects.
    static constexpr int MAX_POOLED_EFFECTS = 256;

    C_ParticlesModule(int id, const char *name);            // 0x00443d30
    ~C_ParticlesModule() override;                          // 0x00443e40

    void Tick(int timeDelta) override {}                    // 0x004455a0
    // Advances all emitters (they are only redrawn while the game is paused).
    void Update(int timeDelta) override;                    // 0x004455b0
    void LoadMissionData(bool server) override;             // 0x00444110
    void FreeMissionData(bool server) override {}           // 0x00444130
    void StartMission(bool server) override;                // 0x00443f30
    void StopMission(bool server) override;                 // 0x00443fe0
    void RestartMission(bool server) override;              // 0x00447730
    bool SaveGame(C_chunk &chunk) override;                 // 0x004478f0
    bool LoadGame(C_chunk &chunk) override;                 // 0x00447c20

    // Starts effect `effect` at `matrix` (see C_ParticleEmitter::Init); returns its
    // handle, -1 if the effect does not exist or is incomplete. `transient` effects
    // are left out of saved games.
    int Create(int effect, const S_matrix &matrix, I3D_frame *parent, bool faceCamera,
               bool transient);                             // 0x004451b0
    int CreateAt(int effect, const S_vector &pos, const S_vector &dir, I3D_frame *parent,
                 bool faceCamera, bool transient);          // 0x00445150
    // Removes an effect immediately.
    void Destroy(int handle);                               // 0x00445800
    void ToggleEmission(int handle, int unused);            // 0x004458b0 (callers pass 0)
    // Stops an effect's emission; it is removed when its particles are gone.
    void Finish(int handle);                                // 0x004458d0
    // Creates the mission's static emitters (missions\<name>\effects.bin).
    void LoadStaticEmitters();                              // 0x00445d90
    // The scene the effects go into (C_game::LoadMission sets the game's before the static ones).
    void SetScene(I3D_scene *scene) { m_scene = scene; }

private:
    S_ActiveEmitter *FindActive(int handle, int *outIndex); // 0x00445740
    void DestroyAll();                                      // 0x004458f0
    void DeleteFreeEmitters();
    void LoadEffects();                                     // 0x00444140
    void FreeEffects();                                     // 0x00445980
    void AddStaticEmitter(int effect, const S_matrix &matrix, I3D_frame *parent);   // 0x00445af0
    // Versions 1 (matrices relative to the parent frame) and 2 (world matrices).
    void LoadStaticEmitterList(C_chunk &chunk, bool relativeToParent);   // 0x00446070, 0x004464a0

    std::array<std::vector<C_ParticleEmitter *>, MAX_POOLED_EFFECTS> m_freeEmitters;   // +0x000C
    I3D_scene *m_scene = nullptr;                           // +0x100C
    std::vector<S_ActiveEmitter> m_active;                  // +0x1010 sorted by handle
    std::vector<std::unique_ptr<S_EffectDef>> m_effects;    // +0x1020
    std::vector<S_ActiveEmitter> m_static;                  // +0x1030
    int m_lastHandle = -1;                                  // +0x1040 -1 = none created yet
    bool m_started = false;                                 // +0x1044
};

extern C_ParticlesModule g_particlesModule;                 // 0x008999f8
