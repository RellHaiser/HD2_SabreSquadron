// The sound device's choice of backend (audio/audio.h).
#include "audio/audio.h"

#include "audio/backends.h"
#include "core/enhance.h"

namespace ls3d::audio {

void ChooseDevice()
{
    Device *device = DirectSoundDevice();
    if (Enhancements().openal) {
        if (Device *openal = OpenALDevice()) {
            device = openal;
            EnhancementLog("LS3DF.ini OpenAL: sound through OpenAL Soft");
        } else {
            EnhancementLog("LS3DF.ini OpenAL: soft_oal.dll / OpenAL32.dll not found, sound through DirectSound");
        }
    }
    g_pAudio = device;
}

}  // namespace ls3d::audio
