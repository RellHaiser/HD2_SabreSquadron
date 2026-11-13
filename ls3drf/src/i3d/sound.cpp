// I3D_sound (0x10054310 - 0x100560f0). LS3DRF has no ISND: what needed the sound driver takes
// LS3DF's path of a driver that never initialised (sound.h).
#include "i3d/sound.h"

#include <cmath>

#include "core/fpu.h"
#include "i3d/area.h"
#include "i3d/driver.h"
#include "i3d/sector.h"

using namespace ls3d;
using namespace ls3d::i3d;

namespace {

bool Same(float a, float b) { return IsTinyBits(std::fabs(a - b)); }

// cos of half an angle (FCOS of the 24-bit product; the result rounded once to float).
float HalfCos(float angle) { return static_cast<float>(std::cos(static_cast<double>(angle * 0.5f))); }

}  // namespace

// 0x10054310
void I3D_sound::Init()
{
    I3D_frame::Init();
    volume = 1.0f;
    lastVolume = 1.0f;
    frequency = 1.0f;
    nearRange = 1.0f;
    nearScaled = 1.0f;
    midFraction = 1.0f;
    farRange = 10.0f;
    farScaled = 10.0f;
    velocity.x = 0.0f;
    velocity.y = 0.0f;
    velocity.z = 0.0f;
    soundFlags = 0x208f;
    state = SNDST_STOPPED;
    changeBase = 0;
    stopTime = 0;
    unknown130 = 0;
    playPosition = 0;
    sound = nullptr;
    source = nullptr;
    loop = false;
    outVolume = 0.0f;
    sourceFlags = 0;
    farScaledSq = 100.0f;
    midScaled = 0.0f;
    midVolume = 0.0f;
    soundType = I3DSOUND_NONE;
    coneInner = 1.04719758f;
    coneOuter = 1.57079637f;
    velocity.w = 0.0f;                  // (the original stores an unset local)
    envSector = nullptr;
    areaFadeTime = 0;
    areaFade = -1.0f;
    areaFadeDuration = 0;
    priority = 0;
    coneInnerCos = static_cast<float>(std::cos(static_cast<double>(0.523598790f)));
    coneOuterCos = static_cast<float>(std::cos(static_cast<double>(0.785398185f)));
    g_driver->soundFrames.PushBack(this);
}

// LS3DRF 0x1002d090 (from LS3DF 0x100544b0): no ISND sound or source to release.
void I3D_sound::Uninit()
{
    SetOn(false);
    // (DeleteSound takes the sound out of `sectors` while this walks it: the walk reads the stale
    // entries the removals leave behind, as the original does.)
    I3D_sector **s = sectors.Data();
    for (int n = sectors.Size(); n != 0; --n, ++s)
        (*s)->DeleteSound(this);
    sectors.Clear();
    I3D_area **a = areas.Data();
    for (int n = areas.Size(); n != 0; --n, ++a)
        (*a)->DeleteSound(this);
    areas.Clear();
    g_driver->soundFrames.RemoveSwap(this);
    I3D_frame::Uninit();
}

// 0x100560f0
void I3D_sound::Load() { I3D_frame::Load(); }

// LS3DRF 0x1002b150 (from LS3DF 0x100545a0): no sound driver; a shared return-0 stub, so I3D_OK.
I3D_RESULT I3D_sound::Open(const char *, uint32_t) { return I3D_OK; }

// LS3DRF 0x1002d160 (from LS3DF 0x100548d0): no ISND sound, never playing.
bool I3D_sound::IsPlaying(bool) { return false; }

// LS3DRF 0x1002d170 (from LS3DF 0x100549a0): stores the type only (no equality test, no SNDF_TYPE).
void I3D_sound::SetSoundType(int newType)
{
    soundType = newType;
}

// 0x10054a40
void I3D_sound::SetRange(float nearRange_, float farRange_, float midFraction_, float midVolume_)
{
    if (Same(nearRange, nearRange_) && Same(farRange, farRange_) && Same(midFraction, midFraction_) &&
        Same(midVolume, midVolume_))
        return;
    nearRange = nearRange_;
    midFraction = midFraction_;
    farRange = farRange_;
    midVolume = midVolume_;
    soundFlags |= SNDF_RANGE;
}

// 0x10054b20
void I3D_sound::SetCone(float inner, float outer)
{
    if (Same(coneInner, inner) && Same(coneOuter, outer))
        return;
    coneInner = inner;
    coneOuter = outer;
    if (inner > 3.14159274f)
        coneInner = 3.14159274f;
    if (outer > 3.14159274f)
        coneOuter = 3.14159274f;
    soundFlags |= SNDF_CONE;
    coneInnerCos = HalfCos(coneInner);
    coneOuterCos = HalfCos(coneOuter);
}

// 0x10054c00
void I3D_sound::SetOutVolume(float v)
{
    if (Same(outVolume, v))
        return;
    outVolume = v;
    soundFlags |= SNDF_OUT_VOLUME;
}

// 0x10054c40
void I3D_sound::SetVolume(float v)
{
    if (Same(volume, v))
        return;
    volume = v;
    soundFlags |= SNDF_VOLUME;
}

// 0x10054c80
void I3D_sound::SetLoop(bool on)
{
    loop = on;
    if (on)
        sourceFlags |= SNDP_LOOP;
    else
        sourceFlags &= ~SNDP_LOOP;
    if (frameFlags & FRMFLAGS_ON)
        SetOn(true);
}

// LS3DRF 0x10018f90 (from LS3DF 0x10054cd0): no ISND sound to position.
I3D_RESULT I3D_sound::SetCurrTime(int) { return I3D_OK; }

// 0x10054d50
void I3D_sound::SetFrequency(float ratio)
{
    if (Same(frequency, ratio))
        return;
    if (ratio >= 0.01f && ratio <= 100.0f) {
        frequency = ratio;
        soundFlags |= SNDF_FREQUENCY;
    }
}

// 0x10054e80
void I3D_sound::GetRange(float &nearRange_, float &farRange_, float &midFraction_, float &midVolume_)
{
    nearRange_ = nearRange;
    farRange_ = farRange;
    midFraction_ = midFraction;
    midVolume_ = midVolume;
}

// 0x10054ec0
void I3D_sound::GetCone(float &inner, float &outer)
{
    inner = coneInner;
    outer = coneOuter;
}

// LS3DRF 0x1002ea40 (from LS3DF 0x10054ee0 / 0x10054f00): one stub for both; no ISND sound.
int I3D_sound::GetCurrTime() { return 0; }
int I3D_sound::GetLength() { return 0; }

// 0x10054dc0
void I3D_sound::SetVelocity(const S_vector4 &v)
{
    if (Same(velocity.x, v.x) && Same(velocity.y, v.y) && Same(velocity.z, v.z))
        return;
    velocity = v;
    soundFlags |= SNDF_VELOCITY;
}

// LS3DRF 0x1002b0a0 (from LS3DF 0x10055200): a no-op (Refresh is empty).
void I3D_sound::UpdateSound() { Refresh(); }

// 0x1001bb10
void __stdcall I3D_sound::SetOn(bool on) { SetActive(on, false); }

// LS3DRF 0x1002d5b0 (from LS3DF 0x10054f30): no ISND sound to start or stop.
void I3D_sound::SetActive(bool on, bool) { I3D_frame::SetOn(on); }

// 0x10055210: no ISND sound to update.
void I3D_sound::Refresh() {}

// LS3DRF 0x1002d5d0 (from LS3DF 0x10055ec0): the frame's part only; no sound settings copied.
I3D_RESULT I3D_sound::Duplicate(I3D_frame *frame) { return I3D_frame::Duplicate(frame); }
