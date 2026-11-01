// Test support shared by compare_buffers and compare_mesh: the original LS3DRF.dll's address
// helpers, mismatch reports, and the buffers' memory. LS3DRF's vertex and index buffers are heap
// blocks of their own (vb_manager.h); the original's four calls that make and free that memory
// (IB Alloc's operator new, IB Free's delete, VB Alloc's _aligned_malloc, VB Free's _aligned_free)
// are patched to the test's, which take the same functions of the test's C runtime and record each
// block's size as the heap reports it. Both sides' blocks then compare by size: the original's
// from the record, the port's from the heap (_msize / _aligned_msize).
#pragma once

#include <windows.h>

#include <malloc.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <new>
#include <random>
#include <string>

#include "../src/i3d/vb_manager.h"

namespace {

using namespace ls3d;

uint8_t *g_base;
template <class T> T &Orig(uint32_t va) { return *reinterpret_cast<T *>(g_base + (va - 0x10000000)); }
template <class F> F OrigFn(uint32_t va) { return reinterpret_cast<F>(g_base + (va - 0x10000000)); }

std::mt19937 g_rng;
long g_op;
std::string g_lastOp = "start";
int Rand(int n) { return static_cast<int>(g_rng() % static_cast<uint32_t>(n)); }

[[noreturn]] void Fail(const std::string &what, long a, long b)
{
    std::printf("MISMATCH after op %ld (%s): %s: original %ld (0x%lx), port %ld (0x%lx)\n", g_op, g_lastOp.c_str(),
                what.c_str(), a, static_cast<unsigned long>(a), b, static_cast<unsigned long>(b));
    std::exit(1);
}
void Check(const std::string &what, long a, long b)
{
    if (a != b)
        Fail(what, a, b);
}

// ---------------------------------------------------------------- the original's buffer memory

std::map<const void *, size_t> g_origSizes;     // the original's live blocks

void *__cdecl OrigNew(size_t size)
{
    void *p = ::operator new(size);
    g_origSizes[p] = _msize(p);
    return p;
}
void __cdecl OrigDelete(void *p)
{
    g_origSizes.erase(p);
    ::operator delete(p);
}
void *__cdecl OrigAlignedMalloc(size_t size, size_t alignment)
{
    void *p = _aligned_malloc(size, alignment);
    if (p)
        g_origSizes[p] = _aligned_msize(p, alignment, 0);
    return p;
}
void __cdecl OrigAlignedFree(void *p)
{
    g_origSizes.erase(p);
    _aligned_free(p);
}

// The original's `call rel32` at `va` (to `target`) calls `hook` instead. False when the bytes
// are not the expected call.
bool PatchCall(uint32_t va, uint32_t target, const void *hook)
{
    uint8_t *at = &Orig<uint8_t>(va);
    int32_t rel;
    std::memcpy(&rel, at + 1, 4);
    if (at[0] != 0xE8 || va + 5 + static_cast<uint32_t>(rel) != target) {
        std::printf("the original's call at 0x%08x is not to 0x%08x\n", va, target);
        return false;
    }
    DWORD old;
    VirtualProtect(at, 5, PAGE_EXECUTE_READWRITE, &old);
    rel = static_cast<int32_t>(reinterpret_cast<uintptr_t>(hook) - reinterpret_cast<uintptr_t>(at + 5));
    std::memcpy(at + 1, &rel, 4);
    VirtualProtect(at, 5, old, &old);
    FlushInstructionCache(GetCurrentProcess(), at, 5);
    return true;
}

// A block's size: the original's recorded (-2 unknown), the port's from its heap; -1 for none.
long OrigIBSize(const void *p)
{
    const auto it = g_origSizes.find(p);
    return !p ? -1 : it == g_origSizes.end() ? -2 : static_cast<long>(it->second);
}
long PortIBSize(void *p) { return p ? static_cast<long>(_msize(p)) : -1; }
long OrigVBSize(const void *p) { return OrigIBSize(p); }
long PortVBSize(void *p) { return p ? static_cast<long>(_aligned_msize(p, 16, 0)) : -1; }

// A pointer as an offset into an allocation's memory: -1 null, -2 outside it.
long OffsetIn(const void *ptr, const void *data, long size)
{
    if (!ptr)
        return -1;
    const auto *p = static_cast<const uint8_t *>(ptr), *d = static_cast<const uint8_t *>(data);
    return d && p >= d && p - d <= size ? static_cast<long>(p - d) : -2;
}

// Both sides' allocation records. A freed record keeps its memory pointer (dangling) and
// offset: compared for presence only.
void CompareAlloc(const std::string &what, const S_IBAlloc &a, const S_IBAlloc &b)
{
    Check(what + " buffer", a.buffer != nullptr, b.buffer != nullptr);
    Check(what + " offset", a.offset, b.offset);
    Check(what + " ib", a.ib != nullptr, b.ib != nullptr);
    if (!b.buffer)
        return;
    Check(what + " ib is the memory", a.ib == a.buffer->data, b.ib == b.buffer->data);
    Check(what + " unknown04", a.buffer->unknown04, b.buffer->unknown04);
    Check(what + " size", OrigIBSize(a.buffer->data), PortIBSize(b.buffer->data));
}
void CompareAlloc(const std::string &what, const S_VBAlloc &a, const S_VBAlloc &b)
{
    Check(what + " buffer", a.buffer != nullptr, b.buffer != nullptr);
    Check(what + " offset", a.offset, b.offset);
    Check(what + " vb", a.vb != nullptr, b.vb != nullptr);
    if (!b.buffer)
        return;
    const C_VertexBuffer *o = a.buffer, *p = b.buffer;
    Check(what + " vb is the memory", a.vb == o->data, b.vb == p->data);
    Check(what + " unknown00", o->unknown00, p->unknown00);
    Check(what + " format", o->format, p->format);
    Check(what + " usage", o->usage, p->usage);
    Check(what + " stride", o->stride, p->stride);
    Check(what + " aligned", reinterpret_cast<uintptr_t>(o->data) % 16, reinterpret_cast<uintptr_t>(p->data) % 16);
    Check(what + " size", OrigVBSize(o->data), PortVBSize(p->data));
}

// Loads the original LS3DRF.dll and patches its buffer memory calls. False when it cannot.
bool SetUpBuffers(const char *origPath)
{
    HMODULE dll = LoadLibraryA(origPath);
    if (!dll) {
        std::printf("cannot load %s (%lu)\n", origPath, GetLastError());
        return false;
    }
    g_base = reinterpret_cast<uint8_t *>(dll);
    if (!Orig<void *>(0x10058b78)) {
        std::printf("the original's driver does not exist\n");
        return false;
    }
    return PatchCall(0x1002edb4, 0x1003150b, reinterpret_cast<const void *>(&OrigNew)) &&
           PatchCall(0x1002edf3, 0x100314b0, reinterpret_cast<const void *>(&OrigDelete)) &&
           PatchCall(0x10030046, 0x10031519, reinterpret_cast<const void *>(&OrigAlignedMalloc)) &&
           PatchCall(0x10030084, 0x10031561, reinterpret_cast<const void *>(&OrigAlignedFree));
}

LONG WINAPI CrashFilter(EXCEPTION_POINTERS *info)
{
    std::printf("CRASH (0x%08lx at %p) in op %ld (%s)\n", info->ExceptionRecord->ExceptionCode,
                info->ExceptionRecord->ExceptionAddress, g_op, g_lastOp.c_str());
    std::fflush(stdout);
    return EXCEPTION_EXECUTE_HANDLER;
}

}  // namespace
