// Particle effects — see particles.h.
#include "game/particles.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "common/chunk.h"
#include "common/color.h"
#include "common/radix_sort.h"
#include "game/game.h"
#include "ls3d/i3d.h"
#include "ls3d/igraph.h"

namespace {

// tables\effects.def
enum : uint16_t {
    CHUNK_EFFECTS = 1000,                   // root
    CHUNK_EFFECT = 0x3F2,                   // S_EffectDef
    CHUNK_EFFECT_LOOK = 100,                // lit flag, blend mode
    CHUNK_EFFECT_NAME = 0x65,
    CHUNK_EFFECT_TEXTURE = 0x66,
    CHUNK_EFFECT_PARTICLE = 0x67,           // S_ParticleDef
    CHUNK_EFFECT_RANGE = 0x68,
    CHUNK_EFFECT_BLEND = 0x69,              // custom blend factors
    CHUNK_PARTICLE_PARAMS = 200,
    CHUNK_PARTICLE_NAME = 0xC9,
    CHUNK_PARTICLE_FRAMES = 0xCA,
    CHUNK_PARTICLE_PHASE = 0xCB,            // S_ParticlePhase
    CHUNK_PARTICLE_FLAGS = 0xCC,
    CHUNK_PHASE_PARAMS = 300,
    CHUNK_PHASE_NAME = 0x12D,
    CHUNK_PHASE_UNKNOWN = 0x12E,
};

// missions\<name>\effects.bin
enum : uint16_t {
    CHUNK_STATIC_EMITTERS_V1 = 0x65,        // root, version 1.0
    CHUNK_STATIC_EMITTERS_V2 = 0xC9,        // root, version 2.01
    CHUNK_STATIC_EMITTER = 1000,            // matrix, effect, sub-chunk with the parent name
};

// Saved games.
enum : uint16_t {
    CHUNK_SAVE_EFFECT = 10000,              // handle, effect, emitter
    CHUNK_SAVE_STATIC = 0x2711,             // effect, emitter
    CHUNK_SAVE_LAST_HANDLE = 0x2712,
    CHUNK_SAVE_EMITTER = 11000,
    CHUNK_SAVE_EMITTER_PARENT = 0x2AF9,
    CHUNK_SAVE_EMITTER_STATE = 0x2AFA,
    CHUNK_SAVE_EMITTER_PARTICLE = 0x2AFB,   // particle kind index, particle
    CHUNK_SAVE_PARTICLE = 0x2B66,
};

// Particles drawn per emitter, and the most an emitter emits continuously.
constexpr int MAX_QUADS = 500;
constexpr int MAX_PARTICLES = 500;

// Visual render state of the blend mode (I3D_particle_visual::SetRenderState).
constexpr int VISUAL_STATE_BLEND = 0x13;
constexpr uint32_t VISUAL_BLEND_CUSTOM = 0x80;

// Effects that keep running while the game is paused.
constexpr int EFFECT_UNPAUSED_1 = 0x2C;
constexpr int EFFECT_UNPAUSED_2 = 0x2D;

constexpr float TWO_PI = 6.2831855f;

// base + random in [-variance, variance).
float Vary(float base, float variance)
{
    const float spread = (RandFloat() - 0.5f) * variance;
    return spread + spread + base;
}

// Milliseconds between particles at `rate` per second (0 when not emitting).
int EmissionInterval(float rate)
{
    return rate == 0.0f ? 0 : static_cast<int>(1.0f / rate * 1000.0f);
}

S_EmitterState InitialState(const S_ParticleDef &def)
{
    S_EmitterState state;
    state.delay = 0;
    state.timer = 0;
    state.age = 0;
    state.rate = static_cast<float>(def.m_rate);
    state.interval = EmissionInterval(state.rate);
    state.repeat = def.m_repeat;
    return state;
}

// Sort keys of the emitter being drawn: view depth in mm and particle index.
struct S_SortKey {
    int depth;
    int index;
};
static_assert(sizeof(S_SortKey) == sizeof(S_RadixPair), "S_SortKey is sorted as S_RadixPair");
std::vector<S_SortKey> g_particleSortKeys;                  // 0x008999ec (count 0x008999f4)

// Rotation by `angle` around z.
S_matrix ZRotation(float angle)
{
    S_matrix m = IdentityMatrix();
    const float c = std::cos(angle);
    const float s = std::sin(angle);
    m.m[0][0] = c;
    m.m[0][1] = s;
    m.m[1][0] = -s;
    m.m[1][1] = c;
    return m;
}

// Quad transform of a particle: `orientation` rotated by the particle's spin and
// scaled by its size, at its world position.
S_matrix QuadMatrix(const S_matrix &orientation, const S_Particle &particle,
                    const S_matrix &emitterMatrix)
{
    const float size = particle.m_size;
    S_matrix m = std::fabs(particle.m_rotation) < 1e-8f
                     ? orientation
                     : ZRotation(particle.m_rotation).RotateByMatrix(orientation);
    for (int row = 0; row < 3; ++row) {
        m.m[row][0] *= size;
        m.m[row][1] *= size;
        m.m[row][2] *= size;
    }
    m(3) = particle.GetWorldPos(emitterMatrix);
    return m;
}

// Packs a particle color: `rgb` scaled to 0..255 (not clamped; like the original,
// out-of-range channels spill into their neighbours).
uint32_t PackColor(float alpha, const float rgb[3])
{
    const uint32_t a = static_cast<uint32_t>(std::lrint(alpha * 255.0f));
    const uint32_t r = static_cast<uint32_t>(std::lrint(rgb[0] * 255.0f));
    const uint32_t g = static_cast<uint32_t>(std::lrint(rgb[1] * 255.0f));
    const uint32_t b = static_cast<uint32_t>(std::lrint(rgb[2] * 255.0f));
    return ((a << 8 | r) << 8 | g) << 8 | b;
}

// Writes the 4 vertices of one particle quad.
void FillQuad(I3D_particle_vertex *v, const S_matrix &m, uint32_t color, const S_ParticleFrame &frame)
{
    static const S_vector corners[4] = {
        {-0.5f, 0.5f, 0.0f}, {0.5f, 0.5f, 0.0f}, {-0.5f, -0.5f, 0.0f}, {0.5f, -0.5f, 0.0f}};
    const float us[4] = {frame.u0, frame.u0, frame.u1, frame.u1};
    const float vs[4] = {frame.v0, frame.v1, frame.v0, frame.v1};
    for (int i = 0; i < 4; ++i) {
        v[i].pos = corners[i] * m;
        v[i].diffuse = color;
        v[i].u = us[i];
        v[i].v = vs[i];
    }
}

} // namespace

void RandomizeDirection(S_vector &dir, const S_vector &variance)
{
    if (variance.Square() == 0.0f)
        return;
    const S_vector around = dir.Cross(variance);
    const float along = (RandFloat() - 0.5f) * 2.0f;
    const float side = (RandFloat() - 0.5f) * 2.0f;
    dir += around * side + variance * along;
    dir.Normalize();
}

// ---------------------------------------------------------------------------
// S_Particle
// ---------------------------------------------------------------------------

bool S_Particle::Init(const S_matrix &emitterMatrix, S_ParticleDef *def, int time, int defIndex)
{
    m_defIndex = defIndex;
    m_def = def;
    m_matrix.Identity();
    m_numFrames = static_cast<int>(def->m_frames.size());

    if (def->m_pos.Square() >= 1e-16f || def->m_posVariance.Square() >= 1e-16f) {
        const S_vector base = def->m_pos;
        const float x = Vary(base.x, def->m_posVariance.x);
        const float y = Vary(base.y, def->m_posVariance.y);
        const float z = Vary(base.z, def->m_posVariance.z);
        m_matrix(3) = S_vector(x, y, z);
    }
    if (def->m_flags & PARTICLE_DEF_WORLD_SPACE) {
        m_flags = PARTICLE_WORLD_SPACE;
        m_matrix(3) += emitterMatrix(3);
    } else {
        m_flags = 0;
    }

    m_phase = 0;
    m_frame = 0;
    m_frameTimer = m_numFrames < 2 ? -1 : def->m_frameTime;
    m_rotation = Vary(def->m_rotation, def->m_rotationVariance);
    m_size = Vary(def->m_size, def->m_sizeVariance);
    m_alpha = Vary(def->m_alpha, def->m_alphaVariance);
    if (!StartPhase(emitterMatrix))
        return false;
    m_speed = Vary(def->m_speed, def->m_speedVariance);
    if (time != 0)
        Tick(time);
    return true;
}

bool S_Particle::StartPhase(const S_matrix &emitterMatrix)
{
    if (static_cast<std::size_t>(m_phase) >= m_def->m_phases.size())
        return false;
    const S_ParticlePhase &phase = *m_def->m_phases[m_phase];

    S_vector dir = phase.m_dir;
    if (dir.Square() < 1e-16f)
        dir = S_vector(0.0f, 0.0f, 1.0f);
    if (phase.m_dirVariance.Square() >= 1e-16f) {
        dir.x = Vary(dir.x, phase.m_dirVariance.x);
        dir.y = Vary(dir.y, phase.m_dirVariance.y);
        dir.z = Vary(dir.z, phase.m_dirVariance.z);
        dir.Normalize();
    }
    if (!(m_flags & PARTICLE_WORLD_SPACE))
        dir = dir.RotateByMatrix(emitterMatrix);
    m_matrix.SetDir3(dir);

    m_gravity = phase.m_gravity * 0.001f;
    m_lifetime = static_cast<int>((RandFloat() - 0.5f) * 2.0f * static_cast<float>(phase.m_durationVariance) +
                                  static_cast<float>(phase.m_duration));
    if (m_lifetime < 1)
        m_lifetime = 1;
    const float seconds = static_cast<float>(m_lifetime) * 0.001f;

    const float endSize = Vary(phase.m_endSize, phase.m_endSizeVariance);
    m_sizeSpeed = (endSize - m_size) / seconds * 0.001f;
    const float endAlpha = std::min(std::max(Vary(phase.m_endAlpha, phase.m_endAlphaVariance), 0.0f), 1.0f);
    m_alphaSpeed = (endAlpha - m_alpha) / seconds * 0.001f;
    m_accel = Vary(phase.m_accel, phase.m_accelVariance) * 0.001f;
    m_rotSpeed = Vary(phase.m_rotSpeed, phase.m_rotSpeedVariance) * 0.001f;

    m_color = phase.m_color;
    m_colorW = phase.m_colorW;
    if (phase.m_colorVariance.Square() >= 1e-16f)
        m_color += phase.m_colorVariance * RandFloat();
    return true;
}

void S_Particle::Tick(int time)
{
    m_lifetime -= time;
    if (m_lifetime < 0) {
        time += m_lifetime;
        m_lifetime = 0;
    }
    const float t = static_cast<float>(time);

    // The movement uses the direction with this tick's gravity applied (the
    // matrix keeps it normalized).
    S_vector dir = m_matrix(2);
    if (m_gravity != 0.0f) {
        dir.y -= t * m_gravity;
        m_matrix.SetDir3(dir);
    }
    if (m_accel != 0.0f)
        m_speed += t * m_accel;
    m_matrix(3) += dir * m_speed * t * 0.001f;

    if (m_rotSpeed != 0.0f) {
        m_rotation += t * m_rotSpeed;
        if (m_rotation < 0.0f)
            m_rotation += TWO_PI;
        else if (m_rotation > TWO_PI)
            m_rotation -= TWO_PI;
    }
    if (m_sizeSpeed != 0.0f)
        m_size += t * m_sizeSpeed;
    if (m_alphaSpeed != 0.0f)
        m_alpha = std::min(std::max(m_alpha + t * m_alphaSpeed, 0.0f), 1.0f);

    if (m_frameTimer > -1) {
        m_frameTimer += time;
        if (m_frameTimer > m_def->m_frameTime) {
            m_frameTimer = 0;
            if (++m_frame == m_numFrames)
                m_frame = m_def->m_loopFrame;
            m_uv = m_def->m_frames[m_frame];
        }
    }
}

void S_Particle::Save(C_chunk &chunk) const
{
    chunk.Begin(CHUNK_SAVE_PARTICLE);
    chunk.Write(m_matrix);
    chunk.Write(m_uv);
    chunk.Write(m_color);
    chunk.Write(m_frame);
    chunk.Write(m_numFrames);
    chunk.Write(m_frameTimer);
    chunk.Write(m_lifetime);
    chunk.Write(m_phase);
    chunk.Write(m_flags);
    chunk.Write(m_alpha);
    chunk.Write(m_alphaSpeed);
    chunk.Write(m_speed);
    chunk.Write(m_accel);
    chunk.Write(m_gravity);
    chunk.Write(m_size);
    chunk.Write(m_sizeSpeed);
    chunk.Write(m_rotation);
    chunk.Write(m_rotSpeed);
    chunk.Ascend();
}

void S_Particle::Load(C_chunk &chunk)
{
    uint16_t id;
    while (chunk.RemainingSize() > 0 && chunk.Descend(id)) {
        if (id == CHUNK_SAVE_PARTICLE) {
            chunk.Read(m_matrix);
            chunk.Read(m_uv);
            chunk.Read(m_color);
            chunk.Read(m_frame);
            chunk.Read(m_numFrames);
            chunk.Read(m_frameTimer);
            chunk.Read(m_lifetime);
            chunk.Read(m_phase);
            chunk.Read(m_flags);
            chunk.Read(m_alpha);
            chunk.Read(m_alphaSpeed);
            chunk.Read(m_speed);
            chunk.Read(m_accel);
            chunk.Read(m_gravity);
            chunk.Read(m_size);
            chunk.Read(m_sizeSpeed);
            chunk.Read(m_rotation);
            chunk.Read(m_rotSpeed);
        }
        chunk.Ascend();
    }
}

// ---------------------------------------------------------------------------
// C_ParticlePool
// ---------------------------------------------------------------------------

C_ParticlePool g_particlePool(0x400);

C_ParticlePool::C_ParticlePool(int growBy) : m_growBy(growBy)
{
    Grow();
}

C_ParticlePool::~C_ParticlePool()
{
    // Particles still owned by emitters at exit are not freed (as in the original).
    for (S_Particle *particle : m_free)
        delete particle;
}

void C_ParticlePool::Grow()
{
    for (int i = 0; i < m_growBy; ++i)
        m_free.push_back(new S_Particle);
}

S_Particle *C_ParticlePool::Alloc()
{
    if (m_free.empty())
        Grow();
    S_Particle *particle = m_free.back();
    m_free.pop_back();
    return particle;
}

void C_ParticlePool::Free(S_Particle *particle)
{
    if (particle)
        m_free.push_back(particle);
}

// ---------------------------------------------------------------------------
// Effect definitions
// ---------------------------------------------------------------------------

S_ParticleDef::~S_ParticleDef() = default;
S_EffectDef::~S_EffectDef() = default;

void S_EffectDef::Load(C_chunk &chunk)
{
    m_particles.clear();
    char text[1000];
    uint16_t id;
    while (chunk.RemainingSize() > 0 && chunk.Descend(id)) {
        switch (id) {
        case CHUNK_EFFECT_LOOK:
            chunk.Read(m_lit);
            chunk.Read(m_blendMode);
            break;
        case CHUNK_EFFECT_NAME:
            chunk.ReadString(text, sizeof(text));
            m_name = text;
            break;
        case CHUNK_EFFECT_TEXTURE:
            chunk.ReadString(text, sizeof(text));
            m_texture = text;
            break;
        case CHUNK_EFFECT_PARTICLE: {
            auto particle = std::make_unique<S_ParticleDef>();
            particle->Load(chunk);
            m_particles.push_back(std::move(particle));
            break;
        }
        case CHUNK_EFFECT_RANGE:
            chunk.Read(m_range);
            chunk.Read(m_unknown1C);
            break;
        case CHUNK_EFFECT_BLEND:
            chunk.Read(m_srcBlend);
            chunk.Read(m_dstBlend);
            break;
        }
        chunk.Ascend();
    }
}

bool S_EffectDef::IsUsable() const
{
    if (m_texture.IsEmpty() || m_particles.empty())
        return false;
    bool anyPhases = false;
    for (const auto &particle : m_particles) {
        if (particle->m_frames.empty())
            return false;
        if (!particle->m_phases.empty())
            anyPhases = true;
    }
    return anyPhases;
}

void S_ParticleDef::Load(C_chunk &chunk)
{
    m_phases.clear();
    m_frames.clear();
    char text[1000];
    uint16_t id;
    while (chunk.RemainingSize() > 0 && chunk.Descend(id)) {
        switch (id) {
        case CHUNK_PARTICLE_PARAMS:
            chunk.Read(m_burstMin);
            chunk.Read(m_burstMax);
            chunk.Read(m_rate);
            chunk.Read(m_unknown30);
            chunk.Read(m_fadeStart);
            chunk.Read(m_fadeRate);
            chunk.Read(m_repeat);
            chunk.Read(m_delay);
            chunk.Read(m_pos);
            chunk.Read(m_posVariance);
            chunk.Read(m_size);
            chunk.Read(m_sizeVariance);
            chunk.Read(m_speed);
            chunk.Read(m_speedVariance);
            chunk.Read(m_alpha);
            chunk.Read(m_alphaVariance);
            chunk.Read(m_rotation);
            chunk.Read(m_rotationVariance);
            break;
        case CHUNK_PARTICLE_NAME:
            chunk.ReadString(text, sizeof(text));
            m_name = text;
            break;
        case CHUNK_PARTICLE_FRAMES: {
            chunk.Read(m_loopFrame);
            chunk.Read(m_frameTime);
            int count = 0;
            chunk.Read(count);
            for (int i = 0; i < count; ++i) {
                S_ParticleFrame frame;
                chunk.Read(frame);
                m_frames.push_back(frame);
            }
            break;
        }
        case CHUNK_PARTICLE_PHASE: {
            auto phase = std::make_unique<S_ParticlePhase>();
            uint16_t phaseId;
            while (chunk.RemainingSize() > 0 && chunk.Descend(phaseId)) {
                switch (phaseId) {
                case CHUNK_PHASE_PARAMS:
                    chunk.Read(phase->m_dir);
                    chunk.Read(phase->m_dirVariance);
                    chunk.Read(phase->m_duration);
                    chunk.Read(phase->m_durationVariance);
                    chunk.Read(phase->m_gravity);
                    chunk.Read(phase->m_rotSpeed);
                    chunk.Read(phase->m_rotSpeedVariance);
                    chunk.Read(phase->m_endSize);
                    chunk.Read(phase->m_endSizeVariance);
                    chunk.Read(phase->m_color);
                    chunk.Read(phase->m_colorVariance);
                    chunk.Read(phase->m_endAlpha);
                    chunk.Read(phase->m_endAlphaVariance);
                    chunk.Read(phase->m_accel);
                    chunk.Read(phase->m_accelVariance);
                    break;
                case CHUNK_PHASE_NAME:
                    chunk.ReadString(text, sizeof(text));
                    phase->m_name = text;
                    break;
                case CHUNK_PHASE_UNKNOWN:
                    chunk.Read(phase->m_unknown70);
                    chunk.Read(phase->m_unknown74);
                    break;
                }
                chunk.Ascend();
            }
            m_phases.push_back(std::move(phase));
            break;
        }
        case CHUNK_PARTICLE_FLAGS:
            chunk.Read(m_flags);
            break;
        }
        chunk.Ascend();
    }
}

// ---------------------------------------------------------------------------
// C_ParticleEmitter
// ---------------------------------------------------------------------------

C_ParticleEmitter::C_ParticleEmitter()
{
    m_visual = static_cast<I3D_particle_visual *>(I3DGetDriver()->CreateVisual(I3DVISUAL_EMITTER));
    m_visual->Unknown1E1() = 1;
    m_visual->SetName("MR_Particle");
}

C_ParticleEmitter::C_ParticleEmitter(S_EffectDef *def, I3D_scene *scene) : C_ParticleEmitter()
{
    m_scene = scene;
    m_def = def;
    m_faceCamera = true;
}

C_ParticleEmitter::~C_ParticleEmitter()
{
    Stop();
    while (!m_particles.empty()) {
        g_particlePool.Free(m_particles.back());
        m_particles.pop_back();
    }
    m_states.clear();
    m_visual->Release();
}

void C_ParticleEmitter::Init(I3D_scene *scene, I3D_frame *parent, const S_matrix &matrix, S_EffectDef *def,
                             bool faceCamera)
{
    m_rangeSq = def->m_range * def->m_range;
    m_def = def;
    Start(scene, parent, matrix, faceCamera, true);
}

void C_ParticleEmitter::Restart(I3D_scene *scene, I3D_frame *parent, const S_matrix &matrix, bool faceCamera)
{
    Start(scene, parent, matrix, faceCamera, false);
}

void C_ParticleEmitter::Start(I3D_scene *scene, I3D_frame *parent, const S_matrix &matrix, bool faceCamera,
                              bool firstStart)
{
    m_scene = scene;
    m_parent = parent;
    m_matrix = matrix;
    m_unknown94 = 100;
    m_flags = EMITTER_EMITTING;
    if (!parent) {
        m_localMatrix.Identity();
    } else {
        S_matrix toParent;
        toParent.Inverse(parent->GetMatrix());
        m_localMatrix = m_matrix * toParent;
        parent->AddRef();
    }
    m_brightness = m_def->m_lit ? GetGame()->GetLightIntensity(matrix(3), nullptr, true) : 1.0f;

    if (firstStart) {
        SetupMaterial();
        for (const auto &particle : m_def->m_particles)
            m_states.push_back(InitialState(*particle));
    } else {
        for (std::size_t i = 0; i < m_states.size(); ++i)
            m_states[i] = InitialState(*m_def->m_particles[i]);
    }

    m_visual->RenderStamp() = -1;
    m_visual->SetWorldMatrixInline(m_matrix);
    if (!m_parent) {
        m_visual->LinkTo(nullptr, 0);
        m_scene->UpdateFrameSector(m_visual, m_visual->GetWorldPos());
    } else {
        m_visual->LinkTo(m_parent, I3DLINK_KEEP_WORLD);
    }
    m_visual->Update();
    EmitBursts();
    m_visual->SetOn(true);
    m_faceCamera = faceCamera;
}

void C_ParticleEmitter::SetupMaterial()
{
    IImage *texture = nullptr;
#ifndef HD2_DEDICATED   // no textures in LS3DRF.dll
    if (GetIGraph()->LoadTexture(m_def->m_texture, 0, 2, &texture) == I3D_OK) {
        m_visual->SetTexture(texture);
        texture->Release();
    }
#endif
    if (m_def->m_blendMode == BLEND_MODE_CUSTOM) {
        m_visual->SetRenderState(VISUAL_STATE_BLEND, VISUAL_BLEND_CUSTOM);
        m_visual->SetSrcBlend(m_def->m_srcBlend);
        m_visual->SetDstBlend(m_def->m_dstBlend);
    } else {
        m_visual->SetRenderState(VISUAL_STATE_BLEND, 1u << (m_def->m_blendMode & 0x1F));
    }
}

void C_ParticleEmitter::Stop()
{
    m_visual->LinkTo(nullptr, 0);
    if (m_parent)
        m_parent->Release();
    m_parent = nullptr;
    m_visual->SetOn(false);
}

void C_ParticleEmitter::UpdateMatrix()
{
    if (m_parent)
        m_matrix = m_localMatrix * m_parent->GetMatrix();
}

void C_ParticleEmitter::Emit(S_ParticleDef *def, int time, int defIndex)
{
    S_Particle *particle = g_particlePool.Alloc();
    if (particle->Init(m_matrix, def, time, defIndex))
        m_particles.push_back(particle);
    else
        g_particlePool.Free(particle);
}

void C_ParticleEmitter::EmitBursts()
{
    for (std::size_t i = 0; i < m_def->m_particles.size(); ++i) {
        S_ParticleDef *def = m_def->m_particles[i].get();
        if (def->m_burstMin == 0 && def->m_burstMax == 0)
            continue;
        const int count = static_cast<int>(RandFloat() * static_cast<float>(def->m_burstMax) +
                                           static_cast<float>(def->m_burstMin));
        // (`> 0`: the original counts down to exactly 0.)
        for (int n = count; n > 0; --n) {
            UpdateMatrix();
            Emit(def, 0, static_cast<int>(i));
        }
    }
}

void C_ParticleEmitter::EmitContinuous(int time)
{
    if (m_particles.size() >= static_cast<std::size_t>(MAX_PARTICLES))
        return;

    for (std::size_t i = 0; i < m_def->m_particles.size(); ++i) {
        S_EmitterState &state = m_states[i];
        if (state.delay >= 1) {
            state.delay -= time;
            if (state.delay >= 1)
                continue;
        }
        if (state.interval <= 0)
            continue;

        state.age += time;
        S_ParticleDef *def = m_def->m_particles[i].get();
        if (def->m_fadeRate != 0 && state.age > def->m_fadeStart) {
            state.rate -= static_cast<float>(def->m_fadeRate * time) * 0.001f;
            state.interval = EmissionInterval(state.rate);
        }

        if (state.interval < 1) {
            // Cycle over: repeat it after the delay, or stop.
            if (state.repeat >= 2)
                --state.repeat;
            else if (state.repeat != -1)
                continue;
            state.delay = def->m_delay;
            state.rate = static_cast<float>(def->m_rate);
            state.interval = EmissionInterval(state.rate);
            state.age = 0;
            state.timer = 0;
        } else {
            state.timer += time;
            // Each particle starts as old as the time since its emission point.
            for (int count = state.timer / state.interval; count > 0; --count) {
                UpdateMatrix();
                Emit(def, state.timer, static_cast<int>(i));
                state.timer -= state.interval;
            }
        }
        m_empty = false;
    }
}

void C_ParticleEmitter::RemoveDead()
{
    auto dead = std::stable_partition(m_particles.begin(), m_particles.end(),
                                      [](const S_Particle *p) { return p->m_lifetime != 0; });
    for (auto it = dead; it != m_particles.end(); ++it)
        g_particlePool.Free(*it);
    m_particles.erase(dead, m_particles.end());
}

void C_ParticleEmitter::TickParticles(int time)
{
    UpdateMatrix();
    for (S_Particle *particle : m_particles) {
        particle->Tick(time);
        if (particle->m_lifetime == 0) {
            ++particle->m_phase;
            particle->StartPhase(m_matrix);
        }
    }
}

bool C_ParticleEmitter::WasVisible()
{
    int &stamp = m_visual->RenderStamp();
    if (stamp == -1) {
        stamp = 0;
        return true;
    }
    return stamp == m_scene->GetRenderStamp();
}

void C_ParticleEmitter::Sort(bool collect)
{
    g_particleSortKeys.clear();
    const int count = static_cast<int>(m_particles.size());
    if (m_empty) {
        if (count == 0)
            return;
        m_empty = false;
    }
    if (count == 0)
        return;
    if (collect)
        g_particleSortKeys.reserve(count);

    const S_matrix &view = m_scene->GetActiveCamera()->GetViewMatrix();
    m_depth = 0.0f;
    UpdateMatrix();
    const S_matrix &toVisual = m_visual->GetInvMatrix();

    I3D_bound_volume bound = {};
    bound.bbox.min = S_vector(1e16f, 1e16f, 1e16f);
    bound.bbox.max = S_vector(-1e16f, -1e16f, -1e16f);
    for (int i = 0; i < count; ++i) {
        const S_Particle &particle = *m_particles[i];
        S_vector pos = particle.GetWorldPos(m_matrix);
        const float depth = pos.x * view.m[0][2] + pos.z * view.m[2][2] + pos.y * view.m[1][2] + view.m[3][2];
        m_depth += depth;
        if (collect)
            g_particleSortKeys.push_back({static_cast<int>(depth * 1000.0f), i});

        // Bounds in the visual's space: the corners of the particle's quad stay
        // within sqrt(3)/2 of its size around the center.
        pos *= toVisual;
        const float extent = particle.m_size * 0.5f * 0.8660255f;
        bound.bbox.min.x = std::min(bound.bbox.min.x, pos.x - extent);
        bound.bbox.min.y = std::min(bound.bbox.min.y, pos.y - extent);
        bound.bbox.min.z = std::min(bound.bbox.min.z, pos.z - extent);
        bound.bbox.max.x = std::max(bound.bbox.max.x, pos.x + extent);
        bound.bbox.max.y = std::max(bound.bbox.max.y, pos.y + extent);
        bound.bbox.max.z = std::max(bound.bbox.max.z, pos.z + extent);
    }
    if (collect) {
        // Nearest first (the keys as unsigned numbers).
        RadixSortPairs(reinterpret_cast<S_RadixPair *>(g_particleSortKeys.data()), static_cast<int>(g_particleSortKeys.size()));
    }
    m_depth /= static_cast<float>(count);

    if (bound.bbox.min.x <= bound.bbox.max.x) {
        const S_vector half = (bound.bbox.max - bound.bbox.min) * 0.5f;
        bound.center = half + bound.bbox.min;
        bound.radius = std::sqrt(half.Square());
    } else {
        bound.radius = 0.0f;
    }
    m_visual->SetBoundVolume(bound);
    m_visual->Update();
}

void C_ParticleEmitter::BuildQuads()
{
    const int count = std::min(static_cast<int>(g_particleSortKeys.size()), MAX_QUADS);
    if (count == 0) {
#ifndef HD2_DEDICATED   // no particle quads in LS3DRF.dll
        m_visual->LockQuads(0, 0.0f);
#endif
        return;
    }

    S_matrix orientation = m_scene->GetActiveCamera()->GetMatrix();
    if (!m_faceCamera)
        orientation.SetDir(-m_matrix(2));

#ifndef HD2_DEDICATED   // no particle quads in LS3DRF.dll
    I3D_particle_vertex *vertices = m_visual->LockQuads(count, m_depth);
#else
    I3D_particle_vertex *vertices = nullptr;
#endif
    if (!vertices)
        return;
    UpdateMatrix();

    // Back to front among the `count` nearest.
    for (int i = count; i-- > 0;) {
        const S_Particle &particle = *m_particles[g_particleSortKeys[i].index];
        float hsv[3];
        RgbToHsv(&particle.m_color.x, hsv);
        hsv[2] *= m_brightness;
        float rgb[3];
        HsvToRgb(hsv, rgb);
        const S_matrix m = QuadMatrix(orientation, particle, m_matrix);
        FillQuad(vertices, m, PackColor(particle.m_alpha, rgb), particle.m_def->m_frames[particle.m_frame]);
        vertices += 4;
    }
}

void C_ParticleEmitter::BuildQuadsCameraAligned()
{
    const int count = std::min(static_cast<int>(g_particleSortKeys.size()), MAX_QUADS);
    if (count == 0) {
#ifndef HD2_DEDICATED   // no particle quads in LS3DRF.dll
        m_visual->LockQuads(0, 0.0f);
#endif
        return;
    }

    const S_matrix &orientation = m_scene->GetActiveCamera()->GetMatrix();
#ifndef HD2_DEDICATED   // no particle quads in LS3DRF.dll
    I3D_particle_vertex *vertices = m_visual->LockQuads(count, m_depth);
#else
    I3D_particle_vertex *vertices = nullptr;
#endif
    if (!vertices)
        return;
    UpdateMatrix();

    for (int i = count; i-- > 0;) {
        const S_Particle &particle = *m_particles[g_particleSortKeys[i].index];
        float hsv[3];
        RgbToHsv(&particle.m_color.x, hsv);
        hsv[2] = m_brightness;      // (replaces the value instead of scaling it)
        float rgb[3];
        HsvToRgb(hsv, rgb);
        const S_matrix m = QuadMatrix(orientation, particle, m_matrix);
        FillQuad(vertices, m, PackColor(particle.m_alpha, rgb), particle.m_def->m_frames[particle.m_frame]);
        vertices += 4;
    }
}

bool C_ParticleEmitter::Tick(int time)
{
    m_empty = true;
    if (m_flags & EMITTER_HIDDEN) {
#ifndef HD2_DEDICATED   // no particle quads in LS3DRF.dll
        m_visual->LockQuads(0, 0.0f);
#endif
        return true;
    }
    const bool visible = WasVisible();
    TickParticles(time);
    if (m_flags & EMITTER_EMITTING)
        EmitContinuous(time);
    Sort(visible);
    if (visible)
        BuildQuads();
#ifndef HD2_DEDICATED   // no particle quads in LS3DRF.dll
    else
        m_visual->LockQuads(0, 0.0f);
#endif
    RemoveDead();
    return !m_empty;
}

void C_ParticleEmitter::Draw(int /*time*/)
{
    if (m_flags & EMITTER_HIDDEN) {
#ifndef HD2_DEDICATED   // no particle quads in LS3DRF.dll
        m_visual->LockQuads(0, 0.0f);
#endif
        return;
    }
    const bool visible = WasVisible();
    Sort(visible);
    if (visible)
        BuildQuads();
#ifndef HD2_DEDICATED   // no particle quads in LS3DRF.dll
    else
        m_visual->LockQuads(0, 0.0f);
#endif
}

void C_ParticleEmitter::Finish()
{
    m_flags = (m_flags & ~EMITTER_EMITTING) | EMITTER_FINISHING;
    if (!m_parent)
        return;
    UpdateMatrix();
    m_parent->Release();
    m_parent = nullptr;
    m_visual->LinkTo(m_scene->GetSector(m_matrix(3)), 0);
    m_visual->Update();
}

void C_ParticleEmitter::Save(C_chunk &chunk) const
{
    chunk.Begin(CHUNK_SAVE_EMITTER);
    chunk.Write(m_matrix);
    chunk.Write(m_localMatrix);
    chunk.Write(m_faceCamera);
    chunk.Write(m_flags);
    chunk.Write(m_brightness);
    chunk.Write(static_cast<uint32_t>(m_empty));
    chunk.Write(m_depth);
    chunk.Ascend();

    if (m_parent) {
        chunk.Begin(CHUNK_SAVE_EMITTER_PARENT);
        chunk.WriteSizedString(m_parent->GetName());
        chunk.Ascend();
    }
    for (const S_EmitterState &state : m_states)
        chunk.WriteChunk(CHUNK_SAVE_EMITTER_STATE, state);
    for (const S_Particle *particle : m_particles) {
        chunk.Begin(CHUNK_SAVE_EMITTER_PARTICLE);
        chunk.Write(particle->m_defIndex);
        particle->Save(chunk);
        chunk.Ascend();
    }
}

void C_ParticleEmitter::Load(C_chunk &chunk)
{
    uint16_t id;
    while (chunk.RemainingSize() > 0 && chunk.Descend(id)) {
        switch (id) {
        case CHUNK_SAVE_EMITTER: {
            chunk.Read(m_matrix);
            chunk.Read(m_localMatrix);
            chunk.Read(m_faceCamera);
            chunk.Read(m_flags);
            chunk.Read(m_brightness);
            uint32_t empty = 0;
            chunk.Read(empty);
            m_empty = empty == 1;
            chunk.Read(m_depth);
            break;
        }
        case CHUNK_SAVE_EMITTER_PARENT: {
            char name[0x400];
            chunk.ReadSizedString(name, sizeof(name));
            m_parent = m_scene->FindFrame(name, 0xFFFF);
            if (m_parent)
                m_parent->AddRef();
            break;
        }
        case CHUNK_SAVE_EMITTER_STATE: {
            S_EmitterState state;
            chunk.Read(state);
            m_states.push_back(state);
            break;
        }
        case CHUNK_SAVE_EMITTER_PARTICLE: {
            int defIndex = -1;
            chunk.Read(defIndex);
            if (defIndex >= 0 && static_cast<std::size_t>(defIndex) < m_def->m_particles.size()) {
                S_Particle *particle = g_particlePool.Alloc();
                particle->m_def = m_def->m_particles[defIndex].get();
                particle->m_defIndex = defIndex;
                particle->Load(chunk);
                m_particles.push_back(particle);
            }
            break;
        }
        }
        chunk.Ascend();
    }

    // The visual stays unparented, in the sector at the emitter's position.
    SetupMaterial();
    m_scene->UpdateFrameSector(m_visual, m_matrix(3));
    m_visual->Update();
    m_visual->SetOn(true);
}

// ---------------------------------------------------------------------------
// C_ParticlesModule
// ---------------------------------------------------------------------------

C_ParticlesModule g_particlesModule(MODULE_PARTICLES, "Particles");

C_ParticlesModule::C_ParticlesModule(int id, const char *name) : C_Module(id, name) {}

C_ParticlesModule::~C_ParticlesModule()
{
    // Emitters left at exit are not deleted (their visuals belong to the engine,
    // which is gone by then).
    FreeEffects();
}

void C_ParticlesModule::DeleteFreeEmitters()
{
    for (int effect = MAX_POOLED_EFFECTS - 1; effect >= 0; --effect) {
        std::vector<C_ParticleEmitter *> &list = m_freeEmitters[effect];
        while (!list.empty()) {
            delete list.back();
            list.pop_back();
        }
    }
}

void C_ParticlesModule::StartMission(bool server)
{
    if (m_started)
        return;
    m_lastHandle = -1;
    LoadEffects();
    DeleteFreeEmitters();
    m_started = true;
}

void C_ParticlesModule::StopMission(bool server)
{
    if (!m_started)
        return;
    DestroyAll();
    DeleteFreeEmitters();
    FreeEffects();
    m_lastHandle = -1;
    for (S_ActiveEmitter &entry : m_static)
        delete entry.emitter;
    m_static.clear();
    m_started = false;
}

void C_ParticlesModule::LoadMissionData(bool server)
{
    m_scene = GetGame()->GetScene();
}

void C_ParticlesModule::RestartMission(bool server)
{
    for (auto it = m_active.rbegin(); it != m_active.rend(); ++it)
        delete it->emitter;
    m_active.clear();
    for (auto it = m_static.rbegin(); it != m_static.rend(); ++it)
        delete it->emitter;
    m_static.clear();

    if (server) {
        LoadStaticEmitters();
        m_lastHandle = -1;
        DeleteFreeEmitters();
        m_started = true;
    }
}

void C_ParticlesModule::LoadEffects()
{
#ifdef HD2_DEDICATED    // the server has no particles: Create only returns -1 (DS 0x0043dff0)
    return;
#endif
    if (!m_effects.empty())
        return;

    C_chunk chunk;
    if (!chunk.Open("tables\\effects.def", false)) {
        dbgPrintf("Could not load data from \"tables\\effects.def\"");
        return;
    }
    uint16_t id = 0;
    if (!chunk.Descend(id) || id != CHUNK_EFFECTS) {
        dbgPrintf("Could not load data from \"tables\\effects.def\" wrong version");
        return;
    }
    while (chunk.RemainingSize() > 0 && chunk.Descend(id)) {
        if (id == CHUNK_EFFECT) {
            auto effect = std::make_unique<S_EffectDef>();
            effect->Load(chunk);
            m_effects.push_back(std::move(effect));
        }
        chunk.Ascend();
    }
}

void C_ParticlesModule::FreeEffects()
{
    m_effects.clear();
}

int C_ParticlesModule::Create(int effect, const S_matrix &matrix, I3D_frame *parent, bool faceCamera,
                              bool transient)
{
    if (effect < 0 || static_cast<std::size_t>(effect) >= m_effects.size())
        return -1;
    S_EffectDef *def = m_effects[effect].get();
    if (!def->IsUsable())
        return -1;

    C_ParticleEmitter *emitter = nullptr;
    // (The original has free lists for 256 effects only and does not check.)
    if (effect < MAX_POOLED_EFFECTS && !m_freeEmitters[effect].empty()) {
        emitter = m_freeEmitters[effect].back();
        m_freeEmitters[effect].pop_back();
    }
    if (emitter) {
        emitter->Restart(m_scene, parent, matrix, faceCamera);
    } else {
        emitter = new C_ParticleEmitter;
        emitter->Init(m_scene, parent, matrix, def, faceCamera);
    }

    const int handle = ++m_lastHandle;
    m_active.push_back({emitter, handle, effect, transient});
    return handle;
}

int C_ParticlesModule::CreateAt(int effect, const S_vector &pos, const S_vector &dir, I3D_frame *parent,
                                bool faceCamera, bool transient)
{
    S_matrix matrix;
    matrix.Identity();
    matrix.SetDir3(dir);
    matrix(3) = pos;
    return Create(effect, matrix, parent, faceCamera, transient);
}

void C_ParticlesModule::Update(int timeDelta)
{
    if (!m_scene)
        return;

    const bool paused = GetGame()->IsPaused();
    for (S_ActiveEmitter &entry : m_static) {
        if (paused)
            entry.emitter->Draw(timeDelta);
        else
            entry.emitter->Tick(timeDelta);
    }

    if (m_lastHandle == -1)
        return;
    for (std::size_t i = 0; i < m_active.size();) {
        S_ActiveEmitter &entry = m_active[i];
        if (paused && entry.effect != EFFECT_UNPAUSED_1 && entry.effect != EFFECT_UNPAUSED_2) {
            entry.emitter->Draw(timeDelta);
            ++i;
            continue;
        }
        if (entry.emitter->Tick(timeDelta)) {
            ++i;
            continue;
        }
        // Over: keep the emitter for reuse.
        entry.emitter->Stop();
        if (entry.effect < MAX_POOLED_EFFECTS)
            m_freeEmitters[entry.effect].push_back(entry.emitter);
        else
            delete entry.emitter;
        m_active.erase(m_active.begin() + i);
    }
}

S_ActiveEmitter *C_ParticlesModule::FindActive(int handle, int *outIndex)
{
    auto it = std::lower_bound(m_active.begin(), m_active.end(), handle,
                               [](const S_ActiveEmitter &entry, int h) { return entry.handle < h; });
    if (it == m_active.end() || it->handle != handle)
        return nullptr;
    *outIndex = static_cast<int>(it - m_active.begin());
    return &*it;
}

void C_ParticlesModule::Destroy(int handle)
{
    int index;
    if (S_ActiveEmitter *entry = FindActive(handle, &index)) {
        delete entry->emitter;
        m_active.erase(m_active.begin() + index);
    }
}

void C_ParticlesModule::ToggleEmission(int handle, int /*unused*/)
{
    int index;
    if (S_ActiveEmitter *entry = FindActive(handle, &index))
        entry->emitter->ToggleEmission();
}

void C_ParticlesModule::Finish(int handle)
{
    int index;
    if (S_ActiveEmitter *entry = FindActive(handle, &index))
        entry->emitter->Finish();
}

void C_ParticlesModule::DestroyAll()
{
    for (S_ActiveEmitter &entry : m_active)
        delete entry.emitter;
    m_active.clear();
}

void C_ParticlesModule::AddStaticEmitter(int effect, const S_matrix &matrix, I3D_frame *parent)
{
    if (effect < 0 || static_cast<std::size_t>(effect) >= m_effects.size() || !m_effects[effect]->IsUsable())
        return;
    auto *emitter = new C_ParticleEmitter;
    emitter->Init(m_scene, parent, matrix, m_effects[effect].get(), true);
    m_static.push_back({emitter, -1, effect, false});
}

void C_ParticlesModule::LoadStaticEmitters()
{
#ifdef HD2_DEDICATED    // no effects.bin in the server
    return;
#endif
    if (m_effects.empty())
        LoadEffects();

    const C_fstr fileName("missions\\%s\\effects.bin", GetGame()->GetMissionName());
    C_chunk chunk;
    if (!chunk.Open(fileName, false)) {
        dbgPrintf("Effects.bin - Error: could not open \"%s\"", fileName.c_str());
        return;
    }
    uint16_t id = 0;
    chunk.Descend(id);
    if (id == CHUNK_STATIC_EMITTERS_V1) {
        LoadStaticEmitterList(chunk, true);
    } else if (id == CHUNK_STATIC_EMITTERS_V2) {
        LoadStaticEmitterList(chunk, false);
    } else {
        dbgPrintf("Effects.bin - Error: different versions");
        return;
    }
    chunk.Ascend();
}

void C_ParticlesModule::LoadStaticEmitterList(C_chunk &chunk, bool relativeToParent)
{
    S_matrix matrix;
    matrix.Identity();
    int effect = -1;
    int count = 0;
    uint16_t id = 0xFFFF;
    while (chunk.RemainingSize() > 0) {
        chunk.Descend(id);
        if (id != CHUNK_STATIC_EMITTER) {
            dbgPrintf("Effects.bin - Error: error in chunks..");
            return;
        }
        chunk.Read(matrix);
        chunk.Read(effect);
        I3D_frame *parent = nullptr;
        if (chunk.RemainingSize() > 0) {
            uint16_t nameId;
            chunk.Descend(nameId);
            char name[1000];
            chunk.ReadString(name, sizeof(name));
            chunk.Ascend();
            parent = GetGame()->GetScene()->FindFrame(name, 0xFFFF);
        }
        chunk.Ascend();

        if (relativeToParent && parent)
            matrix = matrix * parent->GetMatrix();
        AddStaticEmitter(effect, matrix, parent);
        ++count;
    }
    if (relativeToParent)
        dbgPrintf("ParticleManager ver: 1.0: %d static particles initialized.", count);
    else
        dbgPrintf("ParticleManager ver: 2.01: %d static particles initialized.", count);
}

bool C_ParticlesModule::SaveGame(C_chunk &chunk)
{
    chunk.WriteChunk(CHUNK_SAVE_LAST_HANDLE, m_lastHandle);
    // Both lists are written last to first.
    for (auto it = m_active.rbegin(); it != m_active.rend(); ++it) {
        if (it->transient)
            continue;
        chunk.Begin(CHUNK_SAVE_EFFECT);
        chunk.Write(it->handle);
        chunk.Write(it->effect);
        it->emitter->Save(chunk);
        chunk.Ascend();
    }
    for (auto it = m_static.rbegin(); it != m_static.rend(); ++it) {
        chunk.Begin(CHUNK_SAVE_STATIC);
        chunk.Write(it->effect);
        it->emitter->Save(chunk);
        chunk.Ascend();
    }
    return true;
}

bool C_ParticlesModule::LoadGame(C_chunk &chunk)
{
    uint16_t id;
    while (chunk.RemainingSize() > 0 && chunk.Descend(id)) {
        if (id == CHUNK_SAVE_EFFECT || id == CHUNK_SAVE_STATIC) {
            int handle = -1;
            if (id == CHUNK_SAVE_EFFECT)
                chunk.Read(handle);
            int effect = -1;
            chunk.Read(effect);
            // (Range check added; the original indexes the effect table directly.)
            if (effect >= 0 && static_cast<std::size_t>(effect) < m_effects.size()) {
                auto *emitter = new C_ParticleEmitter(m_effects[effect].get(), m_scene);
                emitter->Load(chunk);
                // (The original leaves `transient` uninitialized here.)
                const S_ActiveEmitter entry = {emitter, handle, effect, false};
                (id == CHUNK_SAVE_EFFECT ? m_active : m_static).push_back(entry);
            }
        } else if (id == CHUNK_SAVE_LAST_HANDLE) {
            chunk.Read(m_lastHandle);
        }
        chunk.Ascend();
    }
    // Bug fix: the effects were saved last to first, but FindActive() searches the
    // list by ascending handle; without re-sorting, handles of loaded effects were
    // mostly not found (Destroy/Finish had no effect on them).
    std::sort(m_active.begin(), m_active.end(),
              [](const S_ActiveEmitter &a, const S_ActiveEmitter &b) { return a.handle < b.handle; });
    return true;
}
