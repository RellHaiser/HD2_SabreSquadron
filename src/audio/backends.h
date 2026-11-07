// The sound device's backends, for ChooseDevice (audio/audio.cpp).
#pragma once

#include "audio/audio.h"

namespace ls3d::audio {

// DirectSound (audio/dsound): the default.
Device *DirectSoundDevice();
// OpenAL Soft (audio/openal); null when its library cannot be loaded.
Device *OpenALDevice();

}  // namespace ls3d::audio
