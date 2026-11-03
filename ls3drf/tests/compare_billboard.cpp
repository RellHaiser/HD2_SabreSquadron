// Differential test of the billboard's facing matrix (src/i3d/billboard.cpp:
// I3D_billboard::ComputeMatrix): the original LS3DRF.dll's 0x10002710 and the port's run on the
// same billboard (a copy of the same random bytes each), with random modes (out of range too),
// axes, flags (world space, the inverse matrix valid or not), world matrices, scales, parents,
// cameras (at the frame's position, on its axes, near and far) and output matrices (some modes
// write only part of them). After every call the output matrix and the whole billboard (the
// inverse matrix it may make) must be identical, bit for bit.
//
// The world matrix is always valid (the frame's update is compare_frame's): the port's
// UpdateWMatrixProc fails the test if it is called. A parent is a zeroed frame (only whether
// there is one counts). No other part of the billboard is called.
//
// Usage: compare_billboard.exe [original LS3DRF.dll] [operations] [seed]
//
// test-sources: src/i3d/billboard.cpp src/math/math.cpp src/core/debug.cpp
#include <windows.h>

#include <float.h>

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <random>

#include "../src/i3d/billboard.h"
#include "test_paths.h"

using namespace ls3d;
using namespace ls3d::i3d;

namespace {

uint8_t *g_base;
template <class F> F OrigFn(uint32_t va) { return reinterpret_cast<F>(g_base + (va - 0x10000000)); }
template <class T> T &Field(void *object, size_t offset) { return *reinterpret_cast<T *>(static_cast<uint8_t *>(object) + offset); }

std::mt19937 g_rng;
long g_op;
const char *g_lastOp = "start";

int Rand(int n) { return static_cast<int>(g_rng() % static_cast<uint32_t>(n)); }
float RandFloat(float lo, float hi) { return lo + (hi - lo) * static_cast<float>(g_rng() % 100001) / 100000.0f; }

[[noreturn]] void Fail(const char *what)
{
    std::printf("MISMATCH after op %ld (%s): %s\n", g_op, g_lastOp, what);
    std::exit(1);
}

}  // namespace

// ---------------------------------------------------------------- engine parts not linked

namespace ls3d::i3d {
int g_loadFile;
}  // namespace ls3d::i3d

void I3D_frame::UpdateWMatrixProc() { Fail("the port's UpdateWMatrixProc called"); }
I3D_RESULT __stdcall I3D_frame::Duplicate(I3D_frame *) { Fail("I3D_frame::Duplicate called"); }
void __stdcall I3D_visual::UpdateWBoundProc() { Fail("I3D_visual::UpdateWBoundProc called"); }
void I3D_object::Init() { Fail("I3D_object::Init called"); }
void I3D_object::Load() { Fail("I3D_object::Load called"); }
I3D_RESULT __stdcall I3D_object::SetMesh(I3D_mesh *, uint32_t) { Fail("I3D_object::SetMesh called"); }

namespace {

// ---------------------------------------------------------------- billboards

constexpr size_t kSize = sizeof(I3D_billboard);
alignas(16) uint8_t g_bb[2][kSize];      // [0] original side, [1] port side
alignas(16) uint8_t g_parent[sizeof(I3D_frame)];
alignas(16) S_matrix g_out[2];

// Coverage: calls per mode (-1 .. 5, [0] = -1), mode 4's growth (none, grown, clamped), modes 2's
// with a parent, inverses made.
long g_modes[7], g_grow[3], g_scaledParent, g_inverses;

float RandCoord()
{
    switch (Rand(8)) {
    case 0: return 0.0f;
    case 1: return static_cast<float>(Rand(5) - 2);
    default: return RandFloat(-2.0f, 2.0f);
    }
}

// A random billboard on both sides, a camera and an output matrix.
void Setup(S_vector &camera)
{
    for (size_t i = 0; i < kSize; i += 4)
        Field<float>(g_bb[0], i) = RandFloat(-2.0f, 2.0f);
    std::memset(g_bb[0], 0, 0x10);                                   // the vtable and padding
    std::memset(g_bb[0] + 0xE4, 0, 0x120 - 0xE4);                    // names, pointers, links
    for (int i = 0; i < 16; ++i)
        Field<float>(g_bb[0], 0x40 + 4 * i) = RandCoord();
    I3D_billboard &b = *reinterpret_cast<I3D_billboard *>(g_bb[0]);
    if (Rand(4) == 0)
        b.worldMatrix.Identity();
    b.frameFlags = (g_rng() & ~FRMFLAGS_WMATRIX_VALID & 0xFFFFu) | FRMFLAGS_WMATRIX_VALID;
    b.parent = Rand(2) ? reinterpret_cast<I3D_frame *>(g_parent) : nullptr;
    b.scale = S_vector(RandFloat(0.1f, 3.0f), RandFloat(0.1f, 3.0f), RandFloat(0.1f, 3.0f));
    b.visualFlags = g_rng() & ~(VISF_WORLD_SPACE | VISF_INVMATRIX_VALID);
    if (Rand(4) == 0)
        b.visualFlags |= VISF_WORLD_SPACE;
    if (Rand(3) == 0)
        b.visualFlags |= VISF_INVMATRIX_VALID;
    b.axis = Rand(5) - 1;
    b.mode = Rand(7) - 1;
    std::memcpy(g_bb[1], g_bb[0], kSize);

    const S_vector pos(b.worldMatrix.m[3][0], b.worldMatrix.m[3][1], b.worldMatrix.m[3][2]);
    switch (Rand(6)) {
    case 0:  // at the frame's position
        camera = pos;
        break;
    case 1: {  // on one of its axes
        camera = pos;
        float(&c)[3] = reinterpret_cast<float(&)[3]>(camera);
        c[Rand(3)] += RandFloat(-100.0f, 100.0f);
        break;
    }
    default: {  // 0 .. 100 away
        S_vector d(RandFloat(-1.0f, 1.0f), RandFloat(-1.0f, 1.0f), RandFloat(-1.0f, 1.0f));
        const float r = RandFloat(0.0f, 100.0f);
        camera = S_vector(pos.x + d.x * r, pos.y + d.y * r, pos.z + d.z * r);
        break;
    }
    }
    for (int i = 0; i < 16; ++i)
        g_out[0].m[i / 4][i % 4] = RandFloat(-2.0f, 2.0f);
    g_out[1] = g_out[0];

    ++g_modes[b.mode + 1];
    if (b.mode == BBRDMODE_SCREEN_SCALED && b.parent)
        ++g_scaledParent;
    if ((b.mode == BBRDMODE_AXIS_SPHERE || b.mode == BBRDMODE_AXIS) && b.axis >= 0 && b.axis <= 2 &&
        !(b.visualFlags & (VISF_WORLD_SPACE | VISF_INVMATRIX_VALID)))
        ++g_inverses;
    if (b.mode == BBRDMODE_SCREEN_LOCAL) {
        const float dx = camera.x - pos.x, dy = camera.y - pos.y, dz = camera.z - pos.z;
        const float f = std::sqrt(dz * dz + dy * dy + dx * dx) * 0.05f;
        ++g_grow[f < 1.0f ? 0 : f > 3.0f ? 2 : 1];
    }
}

void RunOp()
{
    S_vector camera;
    Setup(camera);
    g_lastOp = "ComputeMatrix";
    OrigFn<void(__fastcall *)(void *, void *, const S_vector *, S_matrix *)>(0x10002710)(g_bb[0], nullptr, &camera,
                                                                                         &g_out[0]);
    reinterpret_cast<I3D_billboard *>(g_bb[1])->ComputeMatrix(camera, g_out[1]);
    for (int i = 0; i < 16; ++i) {
        const float a = g_out[0].m[i / 4][i % 4], p = g_out[1].m[i / 4][i % 4];
        if (std::memcmp(&a, &p, 4)) {
            char what[96];
            std::snprintf(what, sizeof what, "mode %d axis %d: out[%d][%d] original %.9g, port %.9g",
                          Field<int>(g_bb[0], 0x214), Field<int>(g_bb[0], 0x210), i / 4, i % 4, a, p);
            Fail(what);
        }
    }
    for (size_t i = 0; i < kSize; i += 4) {
        if (Field<uint32_t>(g_bb[0], i) != Field<uint32_t>(g_bb[1], i)) {
            char what[96];
            std::snprintf(what, sizeof what, "mode %d: billboard +0x%zX original 0x%08X, port 0x%08X",
                          Field<int>(g_bb[0], 0x214), i, Field<uint32_t>(g_bb[0], i), Field<uint32_t>(g_bb[1], i));
            Fail(what);
        }
    }
}

LONG WINAPI CrashFilter(EXCEPTION_POINTERS *info)
{
    std::printf("CRASH (0x%08lx at %p) in op %ld (%s)\n", info->ExceptionRecord->ExceptionCode,
                info->ExceptionRecord->ExceptionAddress, g_op, g_lastOp);
    std::fflush(stdout);
    return EXCEPTION_EXECUTE_HANDLER;
}

}  // namespace

int main(int argc, char **argv)
{
    SetUnhandledExceptionFilter(&CrashFilter);
    const char *origPath = OriginalDllPath(argc > 1 ? argv[1] : nullptr, "LS3DRF.dll");
    const long ops = argc > 2 ? std::atol(argv[2]) : 200000;
    const uint32_t seed = argc > 3 ? static_cast<uint32_t>(std::strtoul(argv[3], nullptr, 0)) : 1;
    HMODULE dll = LoadLibraryA(origPath);
    if (!dll) {
        std::printf("cannot load %s (%lu)\n", origPath, GetLastError());
        return 2;
    }
    g_base = reinterpret_cast<uint8_t *>(dll);
    _controlfp(_PC_24, _MCW_PC);
    g_rng.seed(seed);

    for (g_op = 0; g_op < ops; ++g_op)
        RunOp();
    std::printf("coverage: modes -1..5 %ld %ld %ld %ld %ld %ld %ld, mode 2 with a parent %ld, inverses made %ld, "
                "mode 4 not grown / grown / clamped %ld / %ld / %ld\n",
                g_modes[0], g_modes[1], g_modes[2], g_modes[3], g_modes[4], g_modes[5], g_modes[6], g_scaledParent,
                g_inverses, g_grow[0], g_grow[1], g_grow[2]);
    std::printf("compare_billboard: %ld operations, seed %u: identical\n", ops, seed);
    return 0;
}
