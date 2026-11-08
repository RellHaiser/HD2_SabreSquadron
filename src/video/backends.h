// The video device's backends, for ChooseDevice (video/video.cpp).
#pragma once

#include "video/video.h"

namespace ls3d::video {

// Bink and DirectShow (video/win32): the default.
Device *WindowsVideoDevice();
// FFmpeg (video/ffmpeg); null when its libraries cannot be loaded.
Device *FFmpegVideoDevice();

}  // namespace ls3d::video
