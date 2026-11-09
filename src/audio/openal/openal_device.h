// The OpenAL Soft backend's own declarations, for the tests: they render the backend's output
// through OpenAL Soft's loopback device instead of a sound card.
#pragma once

#include "audio/audio.h"

namespace ls3d::audio::openal {

// The devices the backend opens from now on are OpenAL Soft's loopback device (stereo, 32-bit
// float, `rate` frames a second, no HRTF or limiter), rendered by RenderLoopback. False when the
// library cannot be loaded or has no loopback device.
bool UseLoopback(int rate);
// Mixes `frames` stereo frames of the last loopback device opened into `out`.
void RenderLoopback(float *out, int frames);

}  // namespace ls3d::audio::openal
