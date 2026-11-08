// I3D_material, I3D_anim_material (0x1002b030 - 0x1002c400).
#include "i3d/material.h"

#include <cstdlib>
#include <cstring>

#include "external/rw_data.h"
#include "igraph/itexture.h"
#include "i3d/driver.h"
#include "i3d/loader.h"

using namespace ls3d;
using namespace ls3d::i3d;

namespace {

// A name from the model file: a length byte, then the characters.
void ReadName(char (&buffer)[256], int handle)
{
    int length = 0;
    dtaRead(handle, &length, 1);
    if (length)
        dtaRead(handle, buffer, length);
    buffer[length] = 0;
}

}  // namespace

// ---------------------------------------------------------------- I3D_material

void *I3D_material::operator new(size_t size) noexcept { return AlignedAlloc(size, 16); }
void I3D_material::operator delete(void *p) { AlignedFree(p); }

I3D_material::I3D_material()
    : refCount(1), matFlags(1), texture(nullptr), envTexture(nullptr), diffuse{1.0f, 1.0f, 1.0f, 0.0f}, ambient{},
      specular{1.0f, 1.0f, 1.0f, 0.0f}, emissive{}, power(0.0f), opacity(1.0f), alpha(1.0f)
{
}

// LS3DRF 0x10019370 (from LS3DF 0x1002b0b0): the scalar deleting destructor; no textures to release.
I3D_material::~I3D_material() {}

// LS3DRF 0x10019390 (from LS3DF 0x1002b120)
uint32_t I3D_material::Release()
{
    if (--refCount < 0)
        return 0;
    if (refCount)
        return refCount;
    delete this;
    return 0;
}

// LS3DRF 0x100193b0 (from LS3DF 0x1002b140)
I3D_RESULT I3D_material::SetAmbient(const S_vector &c)
{
    ambient.x = c.x;
    ambient.y = c.y;
    ambient.z = c.z;
    return I3D_OK;
}

// LS3DRF 0x100193d0 (from LS3DF 0x1002b160)
I3D_RESULT I3D_material::SetDiffuse(const S_vector &c)
{
    diffuse.x = c.x;
    diffuse.y = c.y;
    diffuse.z = c.z;
    return I3D_OK;
}

// LS3DRF 0x100193f0 (from LS3DF 0x1002b180)
I3D_RESULT I3D_material::SetEmissive(const S_vector &c)
{
    emissive.x = c.x;
    emissive.y = c.y;
    emissive.z = c.z;
    return I3D_OK;
}

// LS3DRF 0x10019410 (from LS3DF 0x1002b1a0)
I3D_RESULT I3D_material::SetSpecular(const S_vector &c)
{
    specular.x = c.x;
    specular.y = c.y;
    specular.z = c.z;
    return I3D_OK;
}

// LS3DRF 0x10019430 (from LS3DF 0x1002b1c0)
I3D_RESULT I3D_material::SetPower(float value)
{
    power = value;
    return I3D_OK;
}

// LS3DRF 0x10019440 (from LS3DF 0x1002b1d0)
I3D_RESULT I3D_material::SetOpacity(float value)
{
    opacity = value;
    return I3D_OK;
}

// LS3DRF's shared return-0 stub 0x10018f90 (marked at sound SetCurrTime; from LS3DF 0x1002b1e0 and
// 0x1002b280): no textures; the material and its flags stay as they are.
I3D_RESULT I3D_material::SetTexture(ITexture *) { return I3D_OK; }

I3D_RESULT I3D_material::SetEnvTexture(ITexture *) { return I3D_OK; }

I3D_RESULT I3D_material::CopyFrom(const I3D_material *source)
{
    if (!source)
        return I3DERR_INVALIDPARAMS;
    matFlags = source->matFlags & ~MATF_ANIMATED;
    std::memcpy(&diffuse, &source->diffuse, 0x44);
    opacity = source->opacity;
    alpha = source->alpha;
    SetTexture(source->texture);
    SetEnvTexture(source->envTexture);
    return I3D_OK;
}

// LS3DRF 0x10019500 (from LS3DF 0x1002b3b0)
I3D_RESULT I3D_material::Set2Sided()
{
    matFlags |= MATF_TWO_SIDED;
    return I3D_OK;
}

// LS3DRF 0x10019450 (from LS3DF 0x1002b300)
I3D_RESULT I3D_material::SetEnvironment(ITexture *env, uint32_t bits8, uint32_t bits12, float value)
{
    I3D_RESULT result = SetEnvTexture(env);
    if (result < 0)
        return result;
    if ((result = SetBits8(bits8)) < 0)
        return result;
    if ((result = SetBits12(bits12)) < 0)
        return result;
    result = SetAlpha(value);
    return result < 0 ? result : I3D_OK;
}

// LS3DRF 0x10019510 (from LS3DF 0x1002b3c0)
I3D_RESULT I3D_material::SetBits8(uint32_t value)
{
    matFlags = (matFlags & ~MATF_BITS8_MASK) | value << 8;
    return I3D_OK;
}

// LS3DRF 0x10019530 (from LS3DF 0x1002b3e0)
I3D_RESULT I3D_material::SetBits12(uint32_t value)
{
    matFlags = (matFlags & ~MATF_BITS12_MASK) | value << 12;
    return I3D_OK;
}

// LS3DRF 0x10019550 (from LS3DF 0x1002b400)
I3D_RESULT I3D_material::SetBits0(uint32_t value)
{
    matFlags = (matFlags & ~MATF_BITS0_MASK) | value;
    return I3D_OK;
}

// LS3DRF 0x10019570 (from LS3DF 0x1002b420)
I3D_RESULT I3D_material::SetAlpha(float value)
{
    if (value < 0.0f)
        alpha = 0.0f;
    else if (alpha > 1.0f)
        alpha = 1.0f;
    else
        alpha = value;
    return I3D_OK;
}

uint32_t I3D_material::TextureFlags()
{
    const uint32_t flags = matFlags;
    uint32_t tf = flags >> 5 & ITC_STREAMED;
    if (g_driverStates[DRVSTATE_TEX_DITHER])
        tf |= ITC_DITHER;
    if (g_driverStates[DRVSTATE_TEX_TRUECOLOR] || (flags & 0x100000))
        tf |= ITC_TRUECOLOR;
    else if (g_driverStates[DRVSTATE_TEX_COMPRESS] && !(flags & 0x200000))
        tf |= ITC_COMPRESS;
    if ((flags & 0x800000) && g_driverStates[DRVSTATE_TEX_MIPMAP])
        tf |= ITC_MIPMAP;
    if (g_driverStates[DRVSTATE_TEX_HALF] && !(flags & 0x400000))
        tf |= ITC_HALF;
    if (flags & 0x8000)
        tf |= ITC_KEEP_PIXELS;
    if (flags & MATF_COLORKEY)
        tf |= flags & 0x1000000 ? (ITC_ALPHA_1BIT | ITC_SOURCE_ALPHA) : ITC_ALPHA_1BIT;
    else if (flags & MATF_ALPHA)
        tf |= flags & 0x1000000 ? ITC_SOURCE_ALPHA : ITC_USE_ALPHA;
    // (A 1-bit alpha with mip maps blends.)
    if ((tf & (ITC_MIPMAP | ITC_ALPHA_1BIT)) == (ITC_MIPMAP | ITC_ALPHA_1BIT))
        matFlags |= MATF_ALPHA;
    return tf;
}

// LS3DRF 0x10019aa0 (from LS3DF 0x1002bbe0): the names are read and dropped (no textures). The texture
// flags are still worked out first, for their one side effect: a mip-mapped 1-bit alpha sets
// MATF_ALPHA, which decides whether an alpha name follows.
void I3D_material::Load()
{
    TextureFlags();
    char name[256];
    if (!(matFlags & MATF_ENV_TEXTURE)) {
        dtaRead(g_loadFile, &diffuse, 0x48);
    } else {
        dtaRead(g_loadFile, &diffuse, 0x4C);
        ReadName(name, g_loadFile);
    }
    int length = 0;
    dtaRead(g_loadFile, &length, 1);
    if (!length)
        return;
    dtaRead(g_loadFile, name, length);
    if ((matFlags & MATF_ALPHA) && !(matFlags & 0xa1000000))
        ReadName(name, g_loadFile);
}

// ---------------------------------------------------------------- I3D_anim_material

I3D_anim_material::I3D_anim_material() : loopStart(0), interval(1), envLoopStart(0), envInterval(1)
{
    textures.frames.SetGrowBy(8);
    textures.lastTime = g_driverTime;
    textures.elapsed = 0;
    textures.frame = 0;
    envTextures.frames.SetGrowBy(8);
    envTextures.lastTime = g_driverTime;
    envTextures.elapsed = 0;
    envTextures.frame = 0;
    matFlags |= MATF_ANIMATED;
}

// LS3DRF 0x10019680 (from LS3DF 0x1002b530): only the members' destructors free the frames' storage;
// no textures to release.
I3D_anim_material::~I3D_anim_material() {}

// LS3DRF 0x10019a60 (from LS3DF 0x1002bba0)
I3D_RESULT I3D_anim_material::SetTexture(ITexture *newTexture)
{
    if (textures.frames.Size())
        return I3DERR_UNSUPPORTED;
    return I3D_material::SetTexture(newTexture);
}

// LS3DRF 0x10019a80 (from LS3DF 0x1002bbc0)
I3D_RESULT I3D_anim_material::SetEnvTexture(ITexture *newTexture)
{
    if (envTextures.frames.Size())
        return I3DERR_UNSUPPORTED;
    return I3D_material::SetEnvTexture(newTexture);
}

// LS3DRF 0x100198d0 (from LS3DF 0x1002b840): no frames to release or copy (Load fills none); the
// colors, flags and timing are copied.
I3D_RESULT I3D_anim_material::CopyFrom(const I3D_material *source)
{
    textures.elapsed = 0;
    textures.lastTime = g_driverTime;
    textures.frame = 0;
    texture = nullptr;
    envTextures.elapsed = 0;
    envTextures.lastTime = g_driverTime;
    envTextures.frame = 0;
    envTexture = nullptr;
    // (The original reads a null source's flags below. Refused here.)
    if (!source)
        return I3D_OK;
    matFlags = source->matFlags & ~MATF_ANIMATED;
    std::memcpy(&diffuse, &source->diffuse, 0x44);
    opacity = source->opacity;
    alpha = source->alpha;
    SetTexture(source->texture);
    SetEnvTexture(source->envTexture);
    if (!(source->matFlags & MATF_ANIMATED))
        return I3D_OK;
    const auto *src = static_cast<const I3D_anim_material *>(source);
    matFlags |= MATF_ANIMATED;
    interval = src->interval;
    loopStart = src->loopStart;
    envInterval = src->envInterval;
    envLoopStart = src->envLoopStart;
    return I3D_OK;
}

void I3D_anim_material::UpdateSequence(Sequence &seq, ITexture *&current, int start, int period, bool alwaysStamp)
{
    const int n = seq.frames.Size();
    if (!n)
        return;
    if (n > 1 && period) {
        if (!seq.lastTime) {
            current = seq.frames[seq.frame];
            seq.lastTime = g_driverTime;
            seq.elapsed = 0;
            return;
        }
        const int dt = g_driverTime - seq.lastTime;
        if (dt) {
            const uint32_t steps = static_cast<uint32_t>(seq.elapsed + dt) / static_cast<uint32_t>(period);
            if (!steps) {
                seq.elapsed += dt;
            } else {
                seq.elapsed += dt - period * static_cast<int>(steps);
                if (start < 0) {
                    seq.frame = std::rand() % n;
                } else {
                    seq.frame += static_cast<int>(steps);
                    if (n <= seq.frame)
                        seq.frame = start + (seq.frame - start) % (n - start);
                }
            }
            seq.lastTime = g_driverTime;
        }
        current = seq.frames[seq.frame];
        return;
    }
    current = seq.frames[0];
    if (alwaysStamp)
        seq.lastTime = g_driverTime;
}

void I3D_anim_material::UpdateTexture() { UpdateSequence(textures, texture, loopStart, interval, false); }

void I3D_anim_material::UpdateEnvTexture() { UpdateSequence(envTextures, envTexture, envLoopStart, envInterval, true); }

// LS3DRF 0x10019990 (from LS3DF 0x1002bad0)
bool I3D_anim_material::SetFrame(int frame)
{
    const int n = textures.frames.Size();
    if (!n)
        return false;
    if (frame < 0) {
        textures.lastTime = g_driverTime;
        textures.frame = 0;
    } else {
        if (n <= frame)
            frame %= n;
        textures.frame = frame;
        textures.lastTime = 0;
    }
    texture = textures.frames[textures.frame];
    return true;
}

// LS3DRF 0x100199f0 (from LS3DF 0x1002bb30)
bool I3D_anim_material::SetEnvFrame(int frame)
{
    const int n = envTextures.frames.Size();
    if (!n)
        return false;
    if (frame < 0) {
        envTextures.lastTime = g_driverTime;
        envTextures.frame = 0;
    } else {
        if (n <= frame)
            frame %= n;
        envTextures.frame = frame;
        envTextures.lastTime = 0;
    }
    // (The original takes the frame of the other sequence here; past the end of this one it read
    // beyond the array: then this sequence's own frame.)
    const int k = textures.frame < n ? textures.frame : envTextures.frame;
    envTexture = envTextures.frames[k];
    return true;
}

// LS3DRF 0x10019c70 (from LS3DF 0x1002be30): the names, frame counts and timing are read; no textures
// or frames are loaded, no texture flags worked out (so no MATF_ALPHA from them, unlike the plain
// material) and no frame is set.
void I3D_anim_material::Load()
{
    char name[256];
    if (!(matFlags & MATF_ENV_TEXTURE)) {
        dtaRead(g_loadFile, &diffuse, 0x48);
    } else {
        dtaRead(g_loadFile, &diffuse, 0x4C);
        ReadName(name, g_loadFile);
    }
    int length = 0;
    dtaRead(g_loadFile, &length, 1);
    if (length) {
        dtaRead(g_loadFile, name, length);
        if ((matFlags & MATF_ALPHA) && !(matFlags & 0xa1000000))
            ReadName(name, g_loadFile);
    }
    int frames = 0, envFrames = 0;
    dtaRead(g_loadFile, &frames, 1);
    dtaRead(g_loadFile, &envFrames, 1);
    dtaRead(g_loadFile, &loopStart, 0x10);      // loopStart, interval, envLoopStart, envInterval
}
