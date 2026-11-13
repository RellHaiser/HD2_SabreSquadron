// The lightmap patch of the lit objects' levels. (No lightmap atlas, the original's "LightMap":
// LS3DRF has none.)
#pragma once

#include <cstdint>

#include "core/layout.h"

namespace ls3d::i3d {

// A lightmap patch (0x1C bytes). A uniform patch is a 3 x 3 marker with its color instead of
// pixels.
struct S_lightmapPart {
    int16_t sourceWidth;                    // +0x00 the image's size
    int16_t sourceHeight;                   // +0x02
    int16_t width;                          // +0x04 in the atlas (halved while it is full)
    int16_t height;                         // +0x06
    int area;                               // +0x08 width x height
    float u;                                // +0x0C its place in the atlas texture (+ 2: turned)
    float v;                                // +0x10
    union {
        uint8_t *pixels;                    // +0x14 BGR, sourceWidth x sourceHeight
        uint32_t color;                     // ... a uniform patch's 0xAARRGGBB (Construct: alpha 0xFF)
    };
    void *texels;                           // +0x18 width x height in the atlas texture's format
};
LS3D_LAYOUT32(sizeof(S_lightmapPart) == 0x1C, "S_lightmapPart layout");

// LS3DF's atlas placement of a level's patches. LS3DRF has none; S_lightmapLevel::lightmap stays
// for the LS3DF layout.
class C_lightmap;

}  // namespace ls3d::i3d
