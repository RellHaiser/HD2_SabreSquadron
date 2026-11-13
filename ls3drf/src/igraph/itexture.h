// ITexture: LS3DF's render device texture (vtable 0x1009dc34, 0x44 bytes; ITexture.cpp region
// 0x10073720 - 0x1007a100). (No ITexture code, image loading or DXT encoder: LS3DRF has none.) The
// class stays declared for the texture pointers of the materials, emitors, projectors and shadows,
// and the flags for those a material asks for.
#pragma once

#include <cstdint>

// ITexture::Create / IGraph::LoadTexture flags ("ITC_*" in the original's messages).
enum : uint32_t {
    ITC_FROM_MEMORY = 0x00000001,       // the name points at a BITMAPINFOHEADER image in memory
    ITC_SOURCE_ALPHA = 0x00000002,      // use the image's own alpha (32-bit; 16-bit: its top bit)
    ITC_USE_ALPHA = 0x00000004,         // a separate opacity map (alpha file name)
    ITC_ALPHA_1BIT = 0x00000008,        // 1-bit alpha (colour key: palette entry 0 / black)
    ITC_STREAMED = 0x00000040,          // (LS3DF) loaded on demand by the texture thread; ignored here
    ITC_DITHER = 0x00000080,            // Floyd-Steinberg dithering into 16-bit formats
    ITC_MIPMAP = 0x00000100,
    ITC_COMPRESS = 0x00000200,          // DXT, when the device has it
    ITC_TRUECOLOR = 0x00000400,         // A8R8G8B8
    ITC_KEEP_PIXELS = 0x00001000,       // keep the 32-bit copy of level 0 (pixels)
    ITC_HALF = 0x00002000,              // half the image size
    ITC_CLEAR = 0x00004000,             // ITC_EMPTY: fill with 0
    ITC_EMPTY = 0x00008000,             // no image: an empty width x height texture
    ITC_DEFAULT = 0x00010000,           // IGraph's 8 x 8 white texture (LoadTexture)
    ITC_BMP_UNPADDED = 0x00040000,      // BMP rows without the 4-byte padding
    ITC_NOCACHE = 0x00200000,           // not shared through IGraph's cached texture list
    ITC_NORESIZE = 0x00400000,          // keep the image size (no power of 2, no maximum)
    ITC_PIXEL_MASK = 0x01000000,        // (internal) pixels is a 1-bit mask of the alpha
    // Options a .dds file does not take (LoadTexture clears them).
    ITC_DDS_IGNORED = ITC_DITHER | ITC_MIPMAP | ITC_COMPRESS | ITC_TRUECOLOR | ITC_HALF,
};

class ITexture;
