// IGraph: texture search directories, image file headers and image resampling
// (0x1006dec0 - 0x1006f040, 0x1006fbd0 - 0x10070d30). Nothing adds a search directory: AddDir is
// not in LS3DRF's vtable.
#include <cstdio>
#include <cstring>

#include "core/c_vector.h"
#include "core/debug.h"
#include "core/i3d_result.h"
#include "external/rw_data.h"
#include "igraph/igraph.h"
#include "igraph/igraph_state.h"

using namespace ls3d;
using namespace ls3d::ig;

namespace {

C_vector<char *> g_textureDirs(0x20);           // 0x100d1ef4, each ending with '\'
char g_lastDir[0x100];                          // 0x100d21d8 where the last texture was found
bool g_lastDirValid = false;                    // 0x100d22d8

bool FileExists(const char *name)
{
    const int h = dtaOpen(name, false);
    if (h < 0)
        return false;
    dtaClose(h);
    return true;
}

}  // namespace

// 0x1006fbd0: the path of a texture file: as given when it has a directory and exists, else in the
// directory of the previous find, else in the search directories (last added first); "" if none.
void FindTextureFile(const char *fileName, char *path)
{
    if (std::strchr(fileName, '\\') && FileExists(fileName)) {
        std::strcpy(path, fileName);
        return;
    }
    if (g_lastDirValid) {
        std::strcpy(path, g_lastDir);
        std::strcat(path, fileName);
        if (FileExists(path))
            return;
    }
    for (int i = g_textureDirs.Size(); i--;) {
        std::strcpy(path, g_textureDirs[i]);
        std::strcat(path, fileName);
        if (FileExists(path)) {
            std::strcpy(g_lastDir, g_textureDirs[i]);
            g_lastDirValid = true;
            return;
        }
    }
    g_lastDirValid = false;
    path[0] = 0;
}

// 0x1006dec0
HRESULT __stdcall IGraph::GetImageInfo(const char *fileName, uint16_t *width, uint16_t *height, uint8_t *bitDepth,
                                       uint32_t flags, int *handle)
{
    if (flags & 1)
        return 0;
    const char *ext = std::strrchr(fileName, '.');
    if (!ext)
        return I3DERR_BADTEXTURE;
    int kind;
    if (ext[1] == 'b' || ext[1] == 'B')
        kind = 0;                               // .bmp
    else if (ext[1] == 't' || ext[1] == 'T')
        kind = 1;                               // .tga
    else
        return I3DERR_BADTEXTUREFORMAT;
    const int h = dtaOpen(fileName, false);
    if (h == -1)
        return I3DERR_NOFILE;
    uint8_t header[0x12];
    switch (kind) {
    case 1:
        if (dtaRead(h, header, 0x12) <= 0) {
            dtaClose(h);
            return I3DERR_FILESYSTEM;
        }
        std::memcpy(width, header + 0xc, 2);
        std::memcpy(height, header + 0xe, 2);
        *bitDepth = header[0x10];
        break;
    case 0: {
        BITMAPINFOHEADER info;
        if (dtaRead(h, header, 0xe) <= 0 || dtaRead(h, &info, 0x28) <= 0) {
            dtaClose(h);
            return I3DERR_FILESYSTEM;
        }
        *width = static_cast<uint16_t>(info.biWidth);
        *height = static_cast<uint16_t>(info.biHeight);
        *bitDepth = static_cast<uint8_t>(info.biBitCount);
        break;
    }
    default:
        dbgPrintf("IGraph::GetImageInfo() - Unsupported file format %s !", fileName);
        dtaClose(h);
        return I3DERR_BADTEXTUREFORMAT;
    }
    if (handle) {
        *handle = h;
        dtaSeek(h, 0, 0);
        return 0;
    }
    dtaClose(h);
    return 0;
}

// 0x10070160 (the body is ls3d::ig::ResizeImage, igraph/resize_image.cpp)
bool __stdcall IGraph::ResizeImage(int srcWidth, int srcHeight, int srcPitch, const uint8_t *src, int dstWidth,
                                   int dstHeight, int dstPitch, uint8_t **dst, int bpp, bool filter)
{
    return ig::ResizeImage(srcWidth, srcHeight, srcPitch, src, dstWidth, dstHeight, dstPitch, dst, bpp, filter);
}
