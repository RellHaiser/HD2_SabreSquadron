// C_IBManager, C_IndexBuffer (LS3DRF 0x1002ed90 - 0x1002ee0c).
#include "i3d/vb_manager.h"

namespace ls3d {

C_IBManager g_ibManager;

// LS3DRF 0x1002ed90 (from LS3DF 0x10058550)
bool C_IBManager::Alloc(uint32_t, uint32_t count, S_IBAlloc *alloc)
{
    auto *b = new C_IndexBuffer;
    b->data = static_cast<uint16_t *>(::operator new(count * 2));
    alloc->buffer = b;
    alloc->offset = 0;
    alloc->ib = b->data;
    return true;
}

// LS3DRF 0x1002ede0 (from LS3DF 0x100587d0)
void FreeIB(S_IBAlloc *alloc)
{
    C_IndexBuffer *b = alloc->buffer;
    if (!b)
        return;
    if (b->data)
        ::operator delete(b->data);
    delete b;
    alloc->buffer = nullptr;
}

}  // namespace ls3d
