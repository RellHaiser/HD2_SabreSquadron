// I3D_emitor and the particles' shared buffers (0x1003ab60 - 0x1003ed10; 0x1003ab60, 0x1003ab90,
// 0x1003abc0 / 0x1003abe0, 0x1003e790 and 0x1003ebf0 are the static initialisers (and an atexit
// destructor) of this file's globals: header copies of a zero vector and two (0, 0, 0, 1) vectors,
// the default texture rectangle and the sort keys).
#include "i3d/emitter.h"

#include <cmath>
#include <cstdlib>
#include <cstring>
#include <initializer_list>
#include <new>

#include "core/debug.h"
#include "core/fpu.h"
#include "external/rw_data.h"
#include "i3d/camera.h"
#include "i3d/driver.h"
#include "i3d/i3d_links.h"
#include "i3d/loader.h"
#include "i3d/material.h"
#include "i3d/mesh.h"
#include "i3d/object.h"
#include "i3d/render_helpers.h"
#include "i3d/render_passes.h"
#include "i3d/render_state.h"
#include "igraph/igraph.h"
#include "igraph/igraph_state.h"
#include "igraph/itexture.h"

using namespace ls3d;
using namespace ls3d::i3d;
using gfx::g_pGfx;
using ig::g_rs;

namespace {

// Quads per shared vertex buffer (0x5554 vertices): the write position at kMaxQuads means the
// buffer in turn has not been locked since it was drawn.
constexpr int kMaxQuads = 0x1555;

// The quads' index buffer (0x100be6c0) and the two vertex buffers (0x100be6f0) used in turn
// (0x100be718), the lock of the current one (0x100be714) and its write position in quads
// (0x100a6898).
S_IBAlloc s_ib;
S_VBAlloc s_vbs[2];
int s_vbIndex;
uint8_t *s_lockedVertices;
int s_writeQuad = kMaxQuads;

// The depth sort keys of an emitter's particles (0x100be708; freed at exit).
struct S_particleKeys {
    S_sortKey *keys = nullptr;
    uint32_t capacity = 0;
    uint32_t count = 0;

    ~S_particleKeys() { ::operator delete(keys); }
    // Room for `n` keys, the old ones copied (0x1003eb90, shared with the render queue's keys;
    // false when out of memory).
    bool Grow(uint32_t n)
    {
        auto *p = static_cast<S_sortKey *>(::operator new(sizeof(S_sortKey) * n, std::nothrow));
        if (!p)
            return false;
        for (uint32_t i = 0; i < count; ++i)
            p[i] = keys[i];
        capacity = n;
        ::operator delete(keys);
        keys = p;
        return true;
    }
};
S_particleKeys s_keys;

// The texture rectangle of a particle without a group (0x100be6cc).
const S_vector4 kWholeTexture = { 0.0f, 0.0f, 1.0f, 1.0f };

S_vector &AsVector(S_vector4 &v) { return reinterpret_cast<S_vector &>(v); }
const S_vector &AsVector(const S_vector4 &v) { return reinterpret_cast<const S_vector &>(v); }

float FloatBits(uint32_t v)
{
    float f;
    std::memcpy(&f, &v, sizeof f);
    return f;
}
uint32_t BitsOf(float f)
{
    uint32_t v;
    std::memcpy(&v, &f, sizeof v);
    return v;
}

// r, g and b clamped to 0 - 1 (NaN kept); w as given.
void ClampColor(S_vector4 &c)
{
    for (float *f : { &c.x, &c.y, &c.z }) {
        if (*f < 0.0f)
            *f = 0.0f;
        else if (*f > 1.0f)
            *f = 1.0f;
    }
}

// The random wait for the next particle (the SDK's `min + rand x random` truncated).
uint32_t RandomInterval(int min, int random)
{
    const float r = RandFloat() * static_cast<float>(random);
    return static_cast<uint32_t>(Ftol(r + static_cast<float>(min)));
}

// The queued item of an emitter's quads (blended; the depth bits or'ed in by the caller).
S_renderItem &QueueItem(I3D_emitor *e)
{
    S_renderItem &item = g_rc.items.PushBackUninit();
    item.visual = e;
    item.sector = g_rc.sector;
    item.texture = e->texture;
    item.sortKey = RIKEY_BLENDED;
    ++g_rc.numBlended;
    return item;
}

}  // namespace

// ---------------------------------------------------------------- the shared buffers

namespace ls3d::i3d {

// 0x1003ae10
void FlushParticles()
{
    if (s_lockedVertices) {
        g_pGfx->Unlock(s_vbs[s_vbIndex].vb);
        s_lockedVertices = nullptr;
    }
}

// 0x1003ae40: after the frame the next buffer takes its turn (when this one was written).
void EndParticles()
{
    if (s_writeQuad > 0 && s_writeQuad != kMaxQuads) {
        if (++s_vbIndex > 1)
            s_vbIndex = 0;
        s_writeQuad = kMaxQuads;
    }
}

}  // namespace ls3d::i3d

// ---------------------------------------------------------------- S_emitorGroup

S_emitorGroup &S_emitorGroup::operator=(const S_emitorGroup &src)
{
    rect = src.rect;
    columns = src.columns;
    rows = src.rows;
    numFrames = src.numFrames;
    loopFrame = src.loopFrame;
    frameTime = src.frameTime;
    // (The original frees its frames first, so assigning a group to itself empties them; the
    // port never does that.)
    frames.Free();
    const int n = src.frames.Size();
    frames.ResizeDiscard(n);
    for (int i = n - 1; i >= 0; --i)
        frames[i] = src.frames[i];
    loopStart = src.loopStart;
    loopLength = src.loopLength;
    return *this;
}

// ---------------------------------------------------------------- I3D_emitor

// (The driver's factory, 0x10019750: the vectors' grow steps. No Init sets firstQuad or the blend
// factors in LS3DRF; the port starts them here: -1 for the render path, 0 where LS3DRF's block holds
// whatever the heap left.)
I3D_emitor::I3D_emitor()
    : groups(nullptr), groupCapacity(0), numGroups(0), groupGrowBy(2), particles(nullptr), particleCapacity(0),
      numParticles(0), particleGrowBy(1), active(0x20), freeSlots(0x20), firstQuad(-1), srcBlend(0), destBlend(0)
{
}

// LS3DRF 0x10010120: the groups freed (LS3DF's emitter has the visuals' shared destructor 0x10019c60,
// which frees none of this).
I3D_emitor::~I3D_emitor()
{
    FreeGroups();
}

void I3D_emitor::FreeGroups()
{
    delete[] groups;                        // 0x1003e8e0
    groups = nullptr;
}

// LS3DRF 0x10022ac0 (from LS3DF 0x1003ae80): no particle lists to set up, no blend factors.
void I3D_emitor::Init()
{
    I3D_visual::Init();
    texture = nullptr;
    SetDefaults();
    emitTime = 0;
    emitWait = 0;
    numEmitted = 0;
    userVertices = false;
    userQuads = 0;
    userDepth = 0.0f;
    fixedGroup = -1;
    visualFlags |= VISF_NO_ZWRITE;
}

// LS3DRF 0x10022b20 (from LS3DF 0x1003af10): only the group count reset (no texture, no particles);
// the groups' storage stays for the next use.
void I3D_emitor::Uninit()
{
    numGroups = 0;
    I3D_frame::Uninit();
}

// LS3DRF's shared empty stub 0x10002470 (marked at the visual's SetWorldSpace; from LS3DF 0x1003af50):
// an emitter keeps no texture.
void I3D_emitor::SetTexture(ITexture *) {}

// LS3DRF 0x10022b30 (from LS3DF 0x1003af80)
I3D_RESULT I3D_emitor::SetParam(int index, uint32_t value)
{
    if (index >= EMTP_COUNT) {
        dbgPrintf("!! Invalid index:%u  I3D_emitor::SetParam()", index);
        return I3DERR_INVALIDPARAMS;
    }
    const float f = FloatBits(value);
    switch (index) {
    case EMTP_INTERVAL:
        intervalMin = static_cast<int>(value);
        emitWait = 0;
        break;
    case EMTP_INTERVAL_RANDOM:
        intervalRandom = static_cast<int>(value);
        emitWait = RandomInterval(intervalMin, intervalRandom);
        break;
    case EMTP_LIFE: lifeMin = static_cast<int>(value); break;
    case EMTP_LIFE_RANDOM: lifeRandom = static_cast<int>(value); break;
    case EMTP_GROWTH_A: growthA = f; break;
    case EMTP_GROWTH_B: growthB = f; break;
    case EMTP_POSITION_SPREAD: positionSpread = f; break;
    case EMTP_DIRECTION_SPREAD: directionSpread = f; break;
    case EMTP_TURN_INTERVAL: turnIntervalMin = static_cast<int>(value); break;
    case EMTP_TURN_INTERVAL_RANDOM: turnIntervalRandom = static_cast<int>(value); break;
    case EMTP_TURN_SPREAD: turnSpread = f; break;
    case EMTP_FADE_IN: fadeIn = f; break;
    case EMTP_FADE_OUT: fadeOut = f; break;
    case EMTP_START_VELOCITY_X: startVelocity.x = f; break;
    case EMTP_START_VELOCITY_X + 1: startVelocity.y = f; break;
    case EMTP_START_VELOCITY_X + 2: startVelocity.z = f; break;
    case EMTP_END_VELOCITY_X: endVelocity.x = f; break;
    case EMTP_END_VELOCITY_X + 1: endVelocity.y = f; break;
    case EMTP_END_VELOCITY_X + 2: endVelocity.z = f; break;
    case EMTP_FLAGS: emitFlags = value & EMITF_MASK; break;
    case EMTP_MAX_PARTICLES:
        maxEmitted = static_cast<int>(value);
        emitting = true;
        break;
    case EMTP_START_VELOCITY_WORLD: startVelocityWorld = static_cast<uint8_t>(value) != 0; break;
    case EMTP_END_VELOCITY_WORLD: endVelocityWorld = static_cast<uint8_t>(value) != 0; break;
    case EMTP_WARM_UP:
        warmUp = static_cast<int>(value);
        if (value)
            Tick(static_cast<int>(value));
        break;
    case EMTP_ROTATION_MIN: rotationMin = f; break;
    case EMTP_ROTATION_MAX: rotationMax = f; break;
    case EMTP_START_SCALE: startScale = f; break;
    case EMTP_START_COLOR:
    case EMTP_END_COLOR: {
        const auto *c = reinterpret_cast<const S_vector4 *>(static_cast<uintptr_t>(value));
        if (!c)
            return I3DERR_INVALIDPARAMS;
        S_vector4 &dst = index == EMTP_START_COLOR ? startColor : endColor;
        dst = *c;
        ClampColor(dst);
        break;
    }
    case EMTP_OPACITY:
        opacity = f;
        if (f > 1.0f)
            opacity = 1.0f;
        if (opacity < 0.0f)
            opacity = 0.0f;
        break;
    default:
        break;
    }
    return I3D_OK;
}

// LS3DRF 0x10023190 (from LS3DF 0x1003b5e0): no EMTP_NUM_PARTICLES case (0).
uint32_t I3D_emitor::GetParam(int index)
{
    if (index >= EMTP_COUNT) {
        dbgPrintf("!! Invalid index:%u  I3D_emitor::GetParam()", index);
        return 0;
    }
    switch (index) {
    case EMTP_INTERVAL: return static_cast<uint32_t>(intervalMin);
    case EMTP_INTERVAL_RANDOM: return static_cast<uint32_t>(intervalRandom);
    case EMTP_LIFE: return static_cast<uint32_t>(lifeMin);
    case EMTP_LIFE_RANDOM: return static_cast<uint32_t>(lifeRandom);
    case EMTP_GROWTH_A: return BitsOf(growthA);
    case EMTP_GROWTH_B: return BitsOf(growthB);
    case EMTP_POSITION_SPREAD: return BitsOf(positionSpread);
    case EMTP_DIRECTION_SPREAD: return BitsOf(directionSpread);
    case EMTP_TURN_INTERVAL: return static_cast<uint32_t>(turnIntervalMin);
    case EMTP_TURN_INTERVAL_RANDOM: return static_cast<uint32_t>(turnIntervalRandom);
    case EMTP_TURN_SPREAD: return BitsOf(turnSpread);
    case EMTP_FADE_IN: return BitsOf(fadeIn);
    case EMTP_FADE_OUT: return BitsOf(fadeOut);
    case EMTP_START_VELOCITY_X: return BitsOf(startVelocity.x);
    case EMTP_START_VELOCITY_X + 1: return BitsOf(startVelocity.y);
    case EMTP_START_VELOCITY_X + 2: return BitsOf(startVelocity.z);
    case EMTP_END_VELOCITY_X: return BitsOf(endVelocity.x);
    case EMTP_END_VELOCITY_X + 1: return BitsOf(endVelocity.y);
    case EMTP_END_VELOCITY_X + 2: return BitsOf(endVelocity.z);
    case EMTP_FLAGS: return emitFlags;
    case EMTP_MAX_PARTICLES: return static_cast<uint32_t>(maxEmitted);
    case EMTP_START_VELOCITY_WORLD: return startVelocityWorld;
    case EMTP_END_VELOCITY_WORLD: return endVelocityWorld;
    case EMTP_WARM_UP: return static_cast<uint32_t>(warmUp);
    case EMTP_ROTATION_MIN: return BitsOf(rotationMin);
    case EMTP_ROTATION_MAX: return BitsOf(rotationMax);
    case EMTP_START_SCALE: return BitsOf(startScale);
    case EMTP_START_COLOR: return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&startColor));
    case EMTP_END_COLOR: return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&endColor));
    case EMTP_OPACITY: return BitsOf(opacity);
    default: return 0;
    }
}

// LS3DRF 0x10023440 (from LS3DF 0x1003b8a0)
void I3D_emitor::SetDefaults()
{
    fadeIn = 0.5f;
    fadeOut = 0.5f;
    // (The velocities' w is left as the original's stack had it; the port sets 0.)
    startVelocity = { 0.0f, 2.0f, 0.0f, 0.0f };
    positionSpread = 0.0f;
    directionSpread = 0.0f;
    turnIntervalMin = 0;
    turnIntervalRandom = 0;
    maxEmitted = 0;
    startVelocityWorld = false;
    endVelocityWorld = false;
    warmUp = 0;
    endVelocity = { 2.0f, 0.0f, 0.0f, 0.0f };
    intervalMin = 100;
    intervalRandom = 60;
    lifeMin = 1800;
    lifeRandom = 600;
    startScale = 1.0f;
    growthA = -0.25f;
    growthB = 0.6f;
    turnSpread = 0.1f;
    rotationMin = -1.2566371f;
    rotationMax = 1.2566371f;
    startColor = { 1.0f, 1.0f, 1.0f, 0.0f };     // (the original's global (1, 1, 1) at 0x100d1570)
    endColor = { 1.0f, 1.0f, 1.0f, 0.0f };
    emitFlags = EMITF_SORTED | EMITF_BLEND;
    emitting = true;
    opacity = 1.0f;
}

// LS3DRF 0x10023b50 (from LS3DF 0x1003bfb0)
void I3D_emitor::EmitOn(bool on)
{
    emitting = on;
    if (on) {
        numEmitted = 0;
        emitWait = 0;
        emitTime = 0;
    }
}

int I3D_emitor::AddGroup(const S_emitorGroup &desc, int index)
{
    S_emitorGroup g;
    g.rect = desc.rect;
    g.columns = desc.columns < 1 ? 1 : desc.columns;
    g.rows = desc.rows < 1 ? 1 : desc.rows;
    g.numFrames = desc.numFrames;
    if (g.numFrames < 1 || g.numFrames > g.rows * g.columns)
        g.numFrames = g.rows * g.columns;
    g.loopFrame = desc.loopFrame;
    // (Quirk kept: a loop frame past the frames takes one frame off the end, not the loop frame
    // back; Prepare guards the frame index.)
    if (g.loopFrame >= g.numFrames)
        --g.numFrames;
    g.frameTime = desc.frameTime;
    g.loopStart = g.frameTime * g.numFrames;
    g.loopLength = (g.numFrames - g.loopFrame) * g.frameTime;
    if (desc.frames.Size() > 0) {
        const int n = desc.frames.Size();
        g.frames.Free();
        g.frames.ResizeDiscard(n);
        for (int i = n - 1; i >= 0; --i)
            g.frames[i] = desc.frames[i];
    } else {
        // The grid row by row.
        const float du = (desc.rect.z - desc.rect.x) / static_cast<float>(g.columns);
        const float dv = (desc.rect.w - desc.rect.y) / static_cast<float>(g.rows);
        S_vector4 r = { desc.rect.x, desc.rect.y, desc.rect.x + du, desc.rect.y + dv };
        g.frames.Reserve(g.numFrames);
        int column = 1;
        for (int k = g.numFrames; k > 0; --k) {
            g.frames.PushBack(r);
            if (++column > g.columns) {
                column = 1;
                r.x = desc.rect.x;
                r.z = desc.rect.x + du;
                r.y = r.w;
                r.w = r.w + dv;
            } else {
                r.x = r.z;
                r.z = r.z + du;
            }
        }
    }
    if (index > -1) {
        // (Fix: the original writes past the groups for an index beyond them; within the array's
        // capacity that slot was unused, so ignoring it is the same.)
        if (index < numGroups) {
            S_emitorGroup &dst = groups[index];
            dst = g;
        }
        return index;
    }
    if (numGroups >= groupCapacity) {
        const int capacity = groupCapacity + groupGrowBy;
        auto *grown = new (std::nothrow) S_emitorGroup[capacity];
        if (!grown)                         // (the original goes on with a null array)
            return -1;
        groupCapacity = capacity;
        for (int i = 0; i < numGroups; ++i)
            grown[i] = groups[i];
        FreeGroups();
        groups = grown;
    }
    groups[numGroups++] = g;
    return numGroups - 1;
}

void I3D_emitor::RemoveGroup(int index)
{
    // (Fix: the original takes any index up to numGroups (also negative ones) and copies the last
    // group to it: for numGroups itself past the array. The port only removes the last one then.)
    if (index < 0 || index > numGroups)
        return;
    --numGroups;
    if (index < numGroups)
        groups[index] = groups[numGroups];
}

// LS3DRF's shared empty stub 0x1002b0a0 (marked at sound UpdateSound; from LS3DF 0x1003d6a0): no
// particles.
void I3D_emitor::ClearParticles() {}

// LS3DRF's shared empty stub 0x10002470 (marked at the visual's SetWorldSpace; from LS3DF 0x1003c530):
// no particles to age or let out.
void I3D_emitor::Tick(int) {}

// LS3DRF 0x10023c00 (from LS3DF 0x1003d340)
I3D_RESULT I3D_emitor::Duplicate(I3D_frame *source)
{
    if (!source || source == this)
        return I3DERR_INVALIDPARAMS;
    I3D_frame::Duplicate(source);
    if (source->type != FRAME_VISUAL)
        return I3D_OK;
    auto *srcVisual = static_cast<I3D_visual *>(source);
    visualFlags = srcVisual->visualFlags & 0x2ff52;
    localBound = srcVisual->localBound;
    if (srcVisual->visualFlags & VISF_WORLD_SPACE) {
        srcVisual->GetWorldBound();
        worldBound = srcVisual->worldBound;
    } else {
        visualFlags &= ~VISF_WBOUND_VALID;
    }
    visualFlags = (visualFlags & ~VISF_OBJECT_PARAMS) | VISF_NO_ZWRITE;
    if (srcVisual->GetVisualType() != FourCC("EMTR")) {
        if (srcVisual->visualFlags & VISF_OBJECT_PARAMS) {
            ITexture *t = nullptr;
            if (I3D_mesh *mesh = static_cast<I3D_object *>(srcVisual)->mesh) {
                if (I3D_mesh_level *level = mesh->GetLevel(0)) {
                    if (level->faceGroups) {
                        if (I3D_material *mat = level->faceGroups[0].material) {
                            if (mat->matFlags & MATF_ANIMATED)
                                mat->UpdateTexture();
                            t = mat->texture;
                        }
                    }
                }
            }
            SetTexture(t);
        }
        return I3D_OK;
    }
    auto *src = static_cast<I3D_emitor *>(source);
    SetTexture(src->texture);
    FreeGroups();
    groupCapacity = 0;
    numGroups = 0;
    if (src->numGroups > 0) {
        groups = new (std::nothrow) S_emitorGroup[src->numGroups];       // 0x1003e940
        if (groups) {
            groupCapacity = src->numGroups;
            numGroups = src->numGroups;
            for (int i = numGroups - 1; i >= 0; --i)
                groups[i] = src->groups[i];
        }
    }
    fixedGroup = src->fixedGroup;
    intervalMin = src->intervalMin;
    intervalRandom = src->intervalRandom;
    lifeMin = src->lifeMin;
    lifeRandom = src->lifeRandom;
    startScale = src->startScale;
    growthA = src->growthA;
    growthB = src->growthB;
    positionSpread = src->positionSpread;
    directionSpread = src->directionSpread;
    fadeIn = src->fadeIn;
    fadeOut = src->fadeOut;
    turnIntervalMin = src->turnIntervalMin;
    turnIntervalRandom = src->turnIntervalRandom;
    turnSpread = src->turnSpread;
    startVelocity = src->startVelocity;
    endVelocity = src->endVelocity;
    emitFlags = src->emitFlags;
    maxEmitted = src->maxEmitted;
    startVelocityWorld = src->startVelocityWorld;
    endVelocityWorld = src->endVelocityWorld;
    rotationMin = src->rotationMin;
    rotationMax = src->rotationMax;
    warmUp = src->warmUp;
    startColor = src->startColor;
    endColor = src->endColor;
    // (The opacity and the custom blend factors are not copied.)
    numEmitted = 0;
    emitWait = 0;
    emitTime = 0;
    emitting = src->emitting;
    return I3D_OK;
}

// LS3DRF 0x10023f60 (from LS3DF 0x1003d6c0): the texture's name and alpha name read and dropped (no
// texture flags, no LoadTexture).
void I3D_emitor::Load()
{
    I3D_frame::Load();
    uint32_t flags = 0;
    dtaRead(g_loadFile, &flags, 4);
    char texName[256], alphaName[256];
    int length = 0;
    dtaRead(g_loadFile, &length, 1);
    if (!length)
        return;
    dtaRead(g_loadFile, texName, length);
    texName[length] = 0;
    if ((flags & MATF_ALPHA) && !(flags & 0xa1000000)) {
        length = 0;
        dtaRead(g_loadFile, &length, 1);
        if (length)
            dtaRead(g_loadFile, alphaName, length);
        alphaName[length] = 0;
    }
}

// LS3DRF 0x10023b80 (from LS3DF 0x1003d0e0): no particles, so always the sphere around the empty
// box's 1e16 corners.
void I3D_emitor::UpdateParticleBound()
{
    ClearParentBounds();
    I3D_bound_volume v{};
    v.bbox.min = S_vector(1e16f, 1e16f, 1e16f);
    v.bbox.max = S_vector(-1e16f, -1e16f, -1e16f);
    v.center = S_vector(0.0f, 0.0f, 0.0f);
    // sqrt(3) x 1e16f rounded once: LS3DRF takes the fsqrt of a folded double (0x5a7623a5); the
    // float steps of LS3DF's code give one ulp less.
    v.radius = 1.7320509e16f;
    SetBoundVolume(v);
}

S_particleVertex *I3D_emitor::LockVertices(int numQuads, float sortDepth)
{
    if (userVertices) {
        userQuads = numQuads;
        userDepth = sortDepth;
    }
    if (!numQuads)
        return nullptr;
    gfx::VertexBuffer *vb = s_vbs[s_vbIndex].vb;
    if (s_lockedVertices && s_writeQuad + numQuads < kMaxQuads) {
        firstQuad = s_writeQuad;
    } else if (s_writeQuad == kMaxQuads) {
        // The buffer's first lock this frame.
        if (numQuads >= kMaxQuads) {
            dbgPrintf("ERROR !! - I3D_emitor::LockVRAMVBuffer() - Allocate more memory for global particle VB !! %d", numQuads);
            return nullptr;
        }
        if (!vb)                            // (fix: no buffers; nothing creates them)
            return nullptr;
        g_pGfx->Lock(vb, 0, 0, &s_lockedVertices, 0);
        firstQuad = 0;
        s_writeQuad = numQuads;
        return reinterpret_cast<S_particleVertex *>(s_lockedVertices);
    } else {
        if (s_writeQuad + numQuads >= kMaxQuads) {
            dbgPrintf("ERROR !! - I3D_emitor::LockVRAMVBuffer() - Allocate more memory for global particle VB !!");
            return nullptr;
        }
        if (!vb)
            return nullptr;
        g_pGfx->Lock(vb, 0, 0, &s_lockedVertices, 0);
        firstQuad = s_writeQuad;
    }
    s_writeQuad += numQuads;
    return reinterpret_cast<S_particleVertex *>(s_lockedVertices + firstQuad * 4 * sizeof(S_particleVertex));
}

void I3D_emitor::Prepare()
{
    if (visualFlags & VISF_MIRRORED)
        return;
    if (userVertices) {
        if (!userQuads)
            return;
        if (firstQuad < 0) {
            dbgPrintf("ERROR !! I3D_emitor::m_AddFGroups() cann't be called without previous I3D_emitor::m_LockVRAMVBuffer() call !!");
            return;
        }
        S_renderItem &item = QueueItem(this);
        const float key = (userDepth + 10000.0f) * 1000.0f;
        item.sortKey |= static_cast<uint32_t>(Fistp(key)) & RIKEY_DEPTH_MASK;
        return;
    }
    const int n = active.Size();
    if (!n)
        return;
    const int itemIndex = g_rc.items.Size();
    QueueItem(this);
    // The slots in drawing order: back to front, else the active list's from the end.
    const uint8_t *slot;
    int stride;
    uint32_t key;
    bool sorted = false;
    if (emitFlags & EMITF_SORTED) {
        s_keys.count = 0;
        if (static_cast<uint32_t>(n) > s_keys.capacity) {
            ::operator delete(s_keys.keys);
            s_keys.keys = static_cast<S_sortKey *>(::operator new(sizeof(S_sortKey) * n, std::nothrow));
            if (s_keys.keys)
                s_keys.capacity = static_cast<uint32_t>(n);
        }
        const S_matrix &view = g_renderCamera->viewMatrix;
        float sum = 0.0f;
        for (int i = n; i != 0;) {
            const int s = active[--i];
            const S_vector4 &pos = particles[s].position;
            const float z = ((pos.z * view.m[2][2] + pos.y * view.m[1][2]) + view.m[0][2] * pos.x) + view.m[3][2];
            sum = z + sum;
            if (s_keys.count == s_keys.capacity && !s_keys.Grow(s_keys.capacity * 2))
                continue;
            s_keys.keys[s_keys.count].key = static_cast<uint32_t>(Ftol(z * 1000.0f));
            s_keys.keys[s_keys.count].index = s;
            ++s_keys.count;
        }
        RadixSort(s_keys.keys, static_cast<int>(s_keys.count));
        key = static_cast<uint32_t>(Fistp((sum / static_cast<float>(n) + 10000.0f) * 1000.0f));
        // (Fix: out of memory some keys are missing, which the original reads anyway; the port
        // then keeps the unsorted order.)
        sorted = s_keys.count == static_cast<uint32_t>(n);
    } else {
        key = static_cast<uint32_t>(Fistp(depth));
    }
    if (sorted) {
        slot = reinterpret_cast<const uint8_t *>(&s_keys.keys[n - 1].index);
        stride = sizeof(S_sortKey);
    } else {
        slot = reinterpret_cast<const uint8_t *>(&active[n - 1]);
        stride = sizeof(int);
    }
    g_rc.items[itemIndex].sortKey |= key & RIKEY_DEPTH_MASK;
    const S_matrix &cam = g_renderCamera->GetMatrix();
    S_particleVertex *v = LockVertices(n, 0.0f);
    if (!v) {
        // (The item goes; the blended count stays, as in the original.)
        g_rc.items.PopBack();
        return;
    }
    for (int k = n; k != 0; --k) {
        const S_particle &p = particles[*reinterpret_cast<const int *>(slot)];
        slot -= stride;
        const uint32_t a = static_cast<uint32_t>(Fistp(p.alpha * 255.0f));
        const uint32_t r = static_cast<uint32_t>(Fistp(p.color.x * 255.0f));
        const uint32_t g = static_cast<uint32_t>(Fistp(p.color.y * 255.0f));
        const uint32_t b = static_cast<uint32_t>(Fistp(p.color.z * 255.0f));
        const uint32_t color = (((a << 8 | r) << 8 | g) << 8) | b;
        // The quad in the camera's plane, turned by the particle's spin, scaled.
        const float angle = static_cast<float>(static_cast<int>(p.age)) * p.rotationSpeed;
        S_matrix q;
        const S_matrix *axes = &cam;
        S_matrix spun;
        if (!IsTinyBits(std::fabs(angle))) {
            spun = RotateZMatrix(angle).RotateByMatrix(cam);
            axes = &spun;
        }
        for (int row = 0; row < 3; ++row) {
            q.m[row][0] = axes->m[row][0] * p.scale;
            q.m[row][1] = axes->m[row][1] * p.scale;
            q.m[row][2] = axes->m[row][2] * p.scale;
            q.m[row][3] = 0.0f;
        }
        q.m[3][0] = p.position.x;
        q.m[3][1] = p.position.y;
        q.m[3][2] = p.position.z;
        q.m[3][3] = p.position.w;
        // Its group's frame at its age.
        const S_vector4 *uv = &kWholeTexture;
        // (Fix: the original takes any group index below numGroups, negative ones too, and any
        // frame index the group's numbers give: a zero frame time divides by zero, a loop frame
        // past the frames reads past them.)
        if (numGroups && numGroups > p.group && p.group >= 0) {
            const S_emitorGroup &grp = groups[p.group];
            const int age = static_cast<int>(p.age);
            int frame = -1;
            if (grp.frameTime > 0) {
                if (age < grp.loopStart)
                    frame = age / grp.frameTime;
                else if (grp.loopLength > 0)
                    frame = (age - grp.loopStart) % grp.loopLength / grp.frameTime + grp.loopFrame;
            }
            if (frame >= 0 && frame < grp.frames.Size())
                uv = &grp.frames[frame];
        }
        auto corner = [&](float x, float y, float u, float tv) {
            const S_vector c = S_vector(x, y, 0.0f) * q;
            v->x = c.x;
            v->y = c.y;
            v->z = c.z;
            v->color = color;
            v->u = u;
            v->v = tv;
            ++v;
        };
        corner(-0.5f, -0.5f, uv->x, uv->w);
        corner(-0.5f, 0.5f, uv->x, uv->y);
        corner(0.5f, -0.5f, uv->z, uv->w);
        corner(0.5f, 0.5f, uv->z, uv->y);
    }
}

void I3D_emitor::Draw(const S_renderItem *item)
{
    int first;
    int quads;
    if (visualFlags & VISF_MIRRORED) {
        // A mirror's copy draws the quads its original wrote.
        auto *original = static_cast<I3D_emitor *>(data);
        first = original->firstQuad * 4;
        quads = userVertices ? original->userQuads : original->active.Size();
    } else {
        first = firstQuad * 4;
        firstQuad = -1;
        quads = userVertices ? userQuads : active.Size();
    }
    const int numVertices = quads * 4;
    if (emitFlags & EMITF_OPAQUE) {
        SetAlphaState(0);
    } else if (emitFlags & EMITF_BLEND) {
        SetAlphaState(1);
        SetBlendState(0);
    } else if (emitFlags & EMITF_CUSTOM_BLEND) {
        SetAlphaState(1);
        g_pGfx->SetRenderState(D3DRS_SRCBLEND, srcBlend);
        g_pGfx->SetRenderState(D3DRS_DESTBLEND, destBlend);
    } else {
        SetAlphaState(1);
        SetBlendState(1);
    }
    g_pGfx->SetTexture(0, item->texture ? item->texture->deviceTexture : nullptr);
    g_pGfx->SetTransform(D3DTS_WORLD, reinterpret_cast<const D3DMATRIX *>(&kIdentityMatrix));
    SetColorOp(0, D3DTOP_MODULATE2X);       // 0x1003ec20
    SetAlphaOp(0, D3DTOP_MODULATE);         // 0x1003ec80
    SetColorOp(1, D3DTOP_DISABLE);          // 0x1003ec50
    // (Set past IGraph's address mode cache.)
    g_pGfx->SetTextureStageState(0, D3DTSS_ADDRESSU, D3DTADDRESS_CLAMP);
    g_pGfx->SetTextureStageState(0, D3DTSS_ADDRESSV, D3DTADDRESS_CLAMP);
    g_pGfx->SetRenderState(D3DRS_LIGHTING, FALSE);
    const S_VBAlloc &vb = s_vbs[s_vbIndex];
    if (vb.buffer) {                        // (fix: no buffers; nothing creates them)
        SetVertexFormat(static_cast<int>(vb.buffer->format), false);
        g_pGfx->SetIndices(s_ib.ib, vb.offset + first);
        g_pGfx->SetStreamSource(0, vb.vb, vb.buffer->stride);
        g_pGfx->DrawIndexedPrimitive(D3DPT_TRIANGLELIST, 0, numVertices, s_ib.offset, numVertices >> 1);
    }
    g_pGfx->SetTextureStageState(0, D3DTSS_ADDRESSU, D3DTADDRESS_WRAP);
    g_pGfx->SetTextureStageState(0, D3DTSS_ADDRESSV, D3DTADDRESS_WRAP);
    g_pGfx->SetRenderState(D3DRS_LIGHTING, TRUE);
    if (emitFlags & EMITF_CUSTOM_BLEND) {
        g_rs.blend = 0;
        g_pGfx->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
        g_pGfx->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
    }
}
