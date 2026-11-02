// I3D_emitor (0x1003ab60 - 0x1003ed10; 0x1003ab60, 0x1003ab90,
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
#include "igraph/igraph.h"
#include "igraph/igraph_state.h"
#include "igraph/itexture.h"

using namespace ls3d;
using namespace ls3d::i3d;

namespace {

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

}  // namespace

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
