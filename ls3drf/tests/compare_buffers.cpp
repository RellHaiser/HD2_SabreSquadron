// Differential test of the vertex / index buffer managers (src/i3d/vb_manager.cpp,
// ib_manager.cpp): the original LS3DRF.dll's g_ibManager / g_vbManager and the port's run the same
// random operations - allocations of every usage, stride, layout and size (empty ones too), frees
// (also of records without a buffer), and the managers' FreeUnused (all or one buffer) and
// ReleaseDefaultPool, which do nothing. After every operation the allocation records must be
// identical: the buffer objects' fields, each record's pointer to its memory, the memory's size and
// alignment (buffer_check.h).
//
// Usage: compare_buffers.exe [original LS3DRF.dll] [operations] [seed]
//
// test-sources: src/i3d/ib_manager.cpp src/i3d/vb_manager.cpp
#include "../src/i3d/vb_manager.h"
#include "buffer_check.h"
#include "test_paths.h"

namespace {

// ---------------------------------------------------------------- allocations

constexpr int kAllocs = 400;
S_IBAlloc g_ibAlloc[2][kAllocs];
S_VBAlloc g_vbAlloc[2][kAllocs];

void CompareAll()
{
    for (int k = 0; k < kAllocs; ++k) {
        CompareAlloc("IB alloc", g_ibAlloc[0][k], g_ibAlloc[1][k]);
        CompareAlloc("VB alloc", g_vbAlloc[0][k], g_vbAlloc[1][k]);
    }
}

// ---------------------------------------------------------------- operations

uint32_t RandUsage()
{
    static const uint32_t kUsages[] = {0x10, 0x18, 0x8, 0x208, 0x210, 0x0};
    return kUsages[Rand(Rand(4) == 0 ? 6 : 2)];
}

uint32_t RandCount(uint32_t most) { return Rand(40) == 0 ? 0 : Rand(8) == 0 ? 1 + Rand(3) : 1 + Rand(static_cast<int>(most)); }

long g_allocs, g_empty, g_frees;

void RunOp()
{
    void *const ibm = &Orig<uint8_t>(0x10047ce0);
    void *const vbm = &Orig<uint8_t>(0x100588a0);
    const int k = Rand(kAllocs);
    switch (Rand(16)) {
    case 0:
    case 1:
    case 2:
    case 3: {
        if (g_ibAlloc[1][k].buffer)
            break;                          // (a second Alloc into a record leaks the first, in both)
        g_lastOp = "IB Alloc";
        const uint32_t usage = RandUsage();
        const uint32_t count = Rand(16) == 0 ? 0x8000 + Rand(0x10000) : RandCount(3000);
        using AllocFn = bool(__fastcall *)(void *, void *, uint32_t, uint32_t, S_IBAlloc *);
        const bool ro = OrigFn<AllocFn>(0x1002ed90)(ibm, nullptr, usage, count, &g_ibAlloc[0][k]);
        const bool rp = g_ibManager.Alloc(usage, count, &g_ibAlloc[1][k]);
        Check("IB Alloc", ro, rp);
        ++g_allocs;
        g_empty += !count;
        break;
    }
    case 4:
    case 5:
    case 6: {
        if (!g_ibAlloc[1][k].buffer && Rand(4) != 0)
            break;
        g_lastOp = "IB Free";
        OrigFn<void(__fastcall *)(void *, void *, S_IBAlloc *)>(0x1002ede0)(ibm, nullptr, &g_ibAlloc[0][k]);
        FreeIB(&g_ibAlloc[1][k]);
        ++g_frees;
        break;
    }
    case 7:
        g_lastOp = "IB FreeUnused";
        OrigFn<void(__fastcall *)(void *, void *)>(0x100300a0)(ibm, nullptr);
        g_ibManager.FreeUnused();
        break;
    case 8:
    case 9:
    case 10:
    case 11: {
        if (g_vbAlloc[1][k].buffer)
            break;
        g_lastOp = "VB Alloc";
        static const uint32_t kStrides[] = {0x20, 0x18, 0x28, 0x10, 0x30, 0x1C};
        const uint32_t usage = RandUsage(), stride = kStrides[Rand(Rand(3) == 0 ? 6 : 2)];
        const uint32_t count = Rand(16) == 0 ? 5000 + Rand(9000) : RandCount(1500);
        const uint32_t format = static_cast<uint32_t>(Rand(4));
        const bool newBuffer = Rand(16) == 0;
        using AllocFn = bool(__fastcall *)(void *, void *, uint32_t, uint32_t, uint32_t, S_VBAlloc *, uint32_t, char);
        const bool ro = OrigFn<AllocFn>(0x10030000)(vbm, nullptr, usage, stride, count, &g_vbAlloc[0][k], format,
                                                    newBuffer ? 1 : 0);
        const bool rp = g_vbManager.Alloc(usage, stride, count, &g_vbAlloc[1][k], format, newBuffer);
        Check("VB Alloc", ro, rp);
        ++g_allocs;
        g_empty += !count;
        break;
    }
    case 12:
    case 13:
    case 14: {
        if (!g_vbAlloc[1][k].buffer && Rand(4) != 0)
            break;
        g_lastOp = "VB Free";
        OrigFn<void(__fastcall *)(void *, void *, S_VBAlloc *)>(0x10030070)(vbm, nullptr, &g_vbAlloc[0][k]);
        FreeVB(&g_vbAlloc[1][k]);
        ++g_frees;
        break;
    }
    default:
        if (Rand(2)) {
            // All buffers, or one (a record's).
            g_lastOp = "VB FreeUnused";
            const bool one = Rand(2) != 0;
            OrigFn<void(__fastcall *)(void *, void *, C_VertexBuffer *)>(0x1002b0a0)(vbm, nullptr,
                                                                                  one ? g_vbAlloc[0][k].buffer : nullptr);
            g_vbManager.FreeUnused(one ? g_vbAlloc[1][k].buffer : nullptr);
        } else {
            g_lastOp = "VB ReleaseDefaultPool";
            OrigFn<void(__fastcall *)(void *, void *)>(0x100300a0)(vbm, nullptr);
            g_vbManager.ReleaseDefaultPool();
        }
        break;
    }
}

}  // namespace

int main(int argc, char **argv)
{
    SetUnhandledExceptionFilter(&CrashFilter);
    const char *origPath = OriginalDllPath(argc > 1 ? argv[1] : nullptr, "LS3DRF.dll");
    const long ops = argc > 2 ? std::atol(argv[2]) : 200000;
    const uint32_t seed = argc > 3 ? static_cast<uint32_t>(std::strtoul(argv[3], nullptr, 0)) : 1;
    if (!SetUpBuffers(origPath))
        return 2;
    g_rng.seed(seed);

    CompareAll();
    for (g_op = 0; g_op < ops; ++g_op) {
        RunOp();
        CompareAll();
    }
    std::printf("coverage: allocations %ld (empty %ld), frees %ld; blocks now %zu\n", g_allocs, g_empty, g_frees,
                g_origSizes.size());
    std::printf("compare_buffers: %ld operations, seed %u: identical\n", ops, seed);
    std::fflush(stdout);
    TerminateProcess(GetCurrentProcess(), 0);
    return 0;
}
