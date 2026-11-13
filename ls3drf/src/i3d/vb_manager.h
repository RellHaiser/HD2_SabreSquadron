// C_VBManager / C_IBManager: the meshes' vertex and index memory (LS3DRF 0x1002ed90 - 0x100300a0;
// LS3DF's VBManager.cpp / IBManager.cpp pool device buffers).
//
// LS3DRF has no device: every allocation is a heap block of its own (offset 0) in a small buffer
// object, both made by Alloc and deleted by Free. The managers hold nothing; their FreeUnused and
// ReleaseDefaultPool do nothing. A buffer's lock is its memory, its unlock nothing.
#pragma once

#include <cstdint>

#include "core/layout.h"
#include "dx/d3d8_types.h"

namespace ls3d {

class C_IndexBuffer;
class C_VertexBuffer;

// A mesh's buffer (what the managers' Alloc fills in; all zero when none).
struct S_IBAlloc {
    C_IndexBuffer *buffer;                  // +0x00 (Free clears only this)
    uint16_t *ib;                           // +0x04 the buffer's memory
    uint32_t offset;                        // +0x08 in indices (0)
};
struct S_VBAlloc {
    C_VertexBuffer *buffer;                 // +0x00 (Free clears only this)
    uint8_t *vb;                            // +0x04 the buffer's memory
    uint32_t offset;                        // +0x08 in vertices (0)
};

// One allocation's 16-bit indices (0xC bytes).
class C_IndexBuffer {
public:
    C_IndexBuffer() : unknown04(0) {}

    // The meshes' inline lock and unlock: the memory, whatever the flags; nothing.
    uint16_t *Lock(uint32_t) { return data; }
    void Unlock() {}

    uint16_t *data;                         // +0x00 operator new
    uint32_t unknown04;                     // +0x04 0, read by nothing
    uint32_t unknown08;                     // +0x08 (not initialised)
};
LS3D_LAYOUT32(sizeof(C_IndexBuffer) == 0xC, "C_IndexBuffer layout");

// One allocation's vertices (0x1C bytes).
class C_VertexBuffer {
public:
    C_VertexBuffer() : unknown00(0), usage(0), data(nullptr) {}

    uint8_t *Lock(uint32_t) { return data; }
    void Unlock() {}

    uint32_t unknown00;                     // +0x00 0, read by nothing
    uint32_t unknown04;                     // +0x04 (not initialised)
    uint32_t format;                        // +0x08 the vertex layout the mesh asked for (0 - 3, 8)
    uint32_t usage;                         // +0x0C D3DUSAGE_*
    uint32_t stride;                        // +0x10 bytes per vertex
    uint8_t *data;                          // +0x14 16-byte aligned
    uint32_t unknown18;                     // +0x18 (not initialised)
};
LS3D_LAYOUT32(sizeof(C_VertexBuffer) == 0x1C, "C_VertexBuffer layout");

class C_IBManager {
public:
    // `count` indices in a buffer of their own; true (the original writes through a failed new).
    bool Alloc(uint32_t usage, uint32_t count, S_IBAlloc *alloc);
    void FreeUnused() {}                    // 0x100300a0 (nothing)
};

class C_VBManager {
public:
    // `count` vertices of `stride` bytes in a buffer of their own (`newBuffer` unused); true.
    bool Alloc(uint32_t usage, uint32_t stride, uint32_t count, S_VBAlloc *alloc, uint32_t format, bool newBuffer);
    void FreeUnused(C_VertexBuffer * = nullptr) {}  // 0x1002b0a0 (nothing)
    void ReleaseDefaultPool() {}            // 0x100300a0 (nothing)
};

extern C_VBManager g_vbManager;                 // 0x100588a0
extern C_IBManager g_ibManager;                 // 0x10047ce0

// An allocation's memory and buffer deleted; the caller clears the rest.
void FreeIB(S_IBAlloc *alloc);
void FreeVB(S_VBAlloc *alloc);

}  // namespace ls3d
