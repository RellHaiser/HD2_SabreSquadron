// C_VBManager, C_VertexBuffer (LS3DRF 0x10030000 - 0x1003009d).
#include "i3d/vb_manager.h"

#include "i3d/i3d.h"

namespace ls3d {

C_VBManager g_vbManager;

// LS3DRF 0x10030000 (from LS3DF 0x10066bf0)
bool C_VBManager::Alloc(uint32_t usage, uint32_t stride, uint32_t count, S_VBAlloc *alloc, uint32_t format, bool)
{
    auto *b = new C_VertexBuffer;
    b->format = format;
    b->stride = stride;
    b->usage = usage;
    b->data = static_cast<uint8_t *>(i3d::AlignedAlloc(stride * count, 16));
    alloc->buffer = b;
    alloc->offset = 0;
    alloc->vb = b->data;
    return true;
}

// LS3DRF 0x10030070 (from LS3DF 0x10066e90)
void FreeVB(S_VBAlloc *alloc)
{
    C_VertexBuffer *b = alloc->buffer;
    if (!b)
        return;
    if (b->data)
        i3d::AlignedFree(b->data);
    delete b;
    alloc->buffer = nullptr;
}

}  // namespace ls3d
