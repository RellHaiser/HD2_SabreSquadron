// The video device's choice of backend (video/video.h).
#include "video/video.h"

#include "core/enhance.h"
#include "video/backends.h"

namespace ls3d::video {

void ChooseDevice()
{
    static bool chosen;
    if (chosen)
        return;
    chosen = true;
    if (!Enhancements().ffmpeg)
        return;
    if (Device *ffmpeg = FFmpegVideoDevice()) {
        g_pVideo = ffmpeg;
        EnhancementLog("LS3DF.ini FFmpeg: movies and videos through FFmpeg");
    } else {
        EnhancementLog("LS3DF.ini FFmpeg: the FFmpeg libraries (avcodec-61.dll ...) not found, videos through Bink "
                       "and DirectShow");
    }
}

}  // namespace ls3d::video
