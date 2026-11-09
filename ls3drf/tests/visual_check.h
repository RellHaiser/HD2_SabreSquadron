// Test support for the visual and frame compares (compare_lens, compare_morph, ...): the original
// LS3DRF.dll and the whole port, linked in, make the same objects, get the same operations through
// the same vtable slots (the port's are LS3DRF's) and must then hold the same fields. The fields
// come from the compiler's report of the port's layout (layouts_gen.h). Pointers cannot be equal:
// two different values that are both readable addresses pass, unless one of them is a known object,
// whose partner must then be the other. A test skips a field only with its reason at the call.
#pragma once

#include <windows.h>

#include <float.h>

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <initializer_list>
#include <map>
#include <random>
#include <string>
#include <vector>

#include "../src/core/debug.h"
#include "../src/i3d/driver.h"
#include "../src/i3d/frame.h"
#include "../src/i3d/material.h"
#include "../src/i3d/mesh.h"
#include "../src/igraph/igraph.h"
#include "layouts_gen.h"
#include "test_paths.h"

namespace vc {

inline uint8_t *g_base;
template <class T> T &Orig(uint32_t va) { return *reinterpret_cast<T *>(g_base + (va - 0x10000000)); }
template <class F> F OrigFn(uint32_t va) { return reinterpret_cast<F>(g_base + (va - 0x10000000)); }

inline std::mt19937 g_rng;
inline int Rand(int n) { return static_cast<int>(g_rng() % static_cast<uint32_t>(n)); }
inline float RandF(float lo, float hi) { return std::uniform_real_distribution<float>(lo, hi)(g_rng); }

inline long g_op;
inline std::string g_lastOp = "start";
inline std::map<std::string, long> g_coverage;
inline void Count(const char *what, long n = 1) { g_coverage[what] += n; }

[[noreturn]] inline void Fail(const std::string &what)
{
    std::printf("MISMATCH after op %ld (%s): %s\n", g_op, g_lastOp.c_str(), what.c_str());
    std::fflush(stdout);
    TerminateProcess(GetCurrentProcess(), 1);
    std::exit(1);
}

// An object of each side.
struct Pair {
    uint8_t *o = nullptr, *p = nullptr;
    explicit operator bool() const { return o != nullptr; }
};

// The known objects: original -> port.
inline std::map<const void *, const void *> g_partner, g_partnerBack;
inline void Register(Pair x)
{
    g_partner[x.o] = x.p;
    g_partnerBack[x.p] = x.o;
}
inline void Forget(Pair x)
{
    g_partner.erase(x.o);
    g_partnerBack.erase(x.p);
}

template <class R, class... A> R CallV(void *object, int slot, A... args)
{
    using F = R(__stdcall *)(void *, A...);
    return (*reinterpret_cast<F **>(object))[slot / 4](object, args...);
}

template <class T> T Side(T value, int) { return value; }
inline uint8_t *Side(Pair value, int side) { return side ? value.p : value.o; }

// The same stdcall slot on both sides, nothing to compare.
template <class... A> void Do(Pair x, int slot, A... args)
{
    CallV<void>(x.o, slot, Side(args, 0)...);
    CallV<void>(x.p, slot, Side(args, 1)...);
}

inline bool IsAddress(uint32_t v)
{
    if (v < 0x10000)
        return false;
    MEMORY_BASIC_INFORMATION mbi;
    return VirtualQuery(reinterpret_cast<const void *>(static_cast<uintptr_t>(v)), &mbi, sizeof mbi) && mbi.State == MEM_COMMIT &&
           !(mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD));
}

// Bits equal, both NaN, subnormals one unit apart (x87 and SSE round them differently), or two addresses that are not known to be others'.
inline bool SameValue(uint32_t a, uint32_t b)
{
    if (a == b)
        return true;
    // (Above 0xFFFF: a small integer must not pass as a subnormal float.)
    if (!(a & 0x7f800000) && !(b & 0x7f800000) && (a | b) >= 0x10000 && (a ^ b) < 0x80000000u && (a - b + 1) <= 2)
        return true;
    float fa, fb;
    std::memcpy(&fa, &a, 4);
    std::memcpy(&fb, &b, 4);
    if (fa != fa && fb != fb)
        return true;
    if (!IsAddress(a) || !IsAddress(b))
        return false;
    const auto ia = g_partner.find(reinterpret_cast<const void *>(static_cast<uintptr_t>(a)));
    const auto ib = g_partnerBack.find(reinterpret_cast<const void *>(static_cast<uintptr_t>(b)));
    if (ia != g_partner.end())
        return ia->second == reinterpret_cast<const void *>(static_cast<uintptr_t>(b));
    return ib == g_partnerBack.end();
}

// The same stdcall slot on both sides; the results must agree (`name` in the report).
template <class R, class... A> R Get(const char *name, Pair x, int slot, A... args)
{
    const R ro = CallV<R>(x.o, slot, Side(args, 0)...);
    const R rp = CallV<R>(x.p, slot, Side(args, 1)...);
    uint32_t a = 0, b = 0;
    std::memcpy(&a, &ro, sizeof ro < 4 ? sizeof ro : 4);
    std::memcpy(&b, &rp, sizeof rp < 4 ? sizeof rp : 4);
    if constexpr (sizeof(R) < 4) {
        a &= (1u << (8 * sizeof(R))) - 1;
        b &= (1u << (8 * sizeof(R))) - 1;
    }
    if (!SameValue(a, b)) {
        char t[128];
        std::snprintf(t, sizeof t, "%s returned %08x (original), %08x (port)", name, a, b);
        Fail(t);
    }
    return ro;
}

// A __thiscall virtual slot (Init, Uninit, MatrixChanged, Load), through __fastcall.
inline void CallThis(void *object, int slot)
{
    using F = void(__fastcall *)(void *, void *);
    (*reinterpret_cast<F **>(object))[slot / 4](object, nullptr);
}
inline void DoThis(Pair x, int slot)
{
    CallThis(x.o, slot);
    CallThis(x.p, slot);
}

// (Writing a test: VC_SURVEY in the environment lists each differing field once and goes on.)
inline bool g_survey = std::getenv("VC_SURVEY") != nullptr;
inline std::map<unsigned, int> g_seen;

// All fields of a class; `skip`: offsets (of dwords) not compared.
template <size_t N>
void Compare(const char *cls, Pair x, const S_layoutRow (&rows)[N], std::initializer_list<unsigned> skip = {})
{
    for (const S_layoutRow &r : rows) {
        if (!std::strcmp(r.name, "{vfptr}") || !std::strncmp(r.name, "padding", 7) || !std::strcmp(r.name, "<pad>"))
            continue;
        if (!std::strcmp(r.name, "name") || !std::strcmp(r.name, "userProps")) {
            // (Strings by content.)
            const auto *a = *reinterpret_cast<const char *const *>(x.o + r.offset);
            const auto *b = *reinterpret_cast<const char *const *>(x.p + r.offset);
            if (!a != !b)
                Fail(std::string(cls) + "." + r.name + ": one is null");
            if (a && IsAddress(static_cast<uint32_t>(reinterpret_cast<uintptr_t>(a))) &&
                IsAddress(static_cast<uint32_t>(reinterpret_cast<uintptr_t>(b))) &&
                std::strcmp(a, b))
                Fail(std::string(cls) + "." + r.name + ": '" + a + "' (original), '" + b + "' (port)");
            continue;
        }
        for (unsigned off = r.offset; off < r.offset + r.size; off += r.size % 4 ? 1 : 4) {
            bool skipped = false;
            for (unsigned s : skip)
                skipped |= s == off;
            if (skipped)
                continue;
            uint32_t a = 0, b = 0;
            std::memcpy(&a, x.o + off, r.size % 4 ? 1 : 4);
            std::memcpy(&b, x.p + off, r.size % 4 ? 1 : 4);
            if (r.size % 4 ? a != b : !SameValue(a, b)) {
                char t[160];
                std::snprintf(t, sizeof t, "%s.%s +0x%x: original %08x, port %08x", cls, r.name, off, a, b);
                if (g_survey) {
                    if (g_seen[off]++ == 0)
                        std::printf("after op %ld (%s): %s\n", g_op, g_lastOp.c_str(), t);
                    continue;
                }
                Fail(t);
            }
        }
    }
}

// The frame offsets the original fills with stack garbage (the box pads and scaleW).
#define VC_FRAME_SKIP 0x1C, 0x2C, 0xDC
// The visual base's fields nothing writes: its unknown block, the bounds' pads, depth, unknown1DC.
#define VC_VISUAL_SKIP VC_FRAME_SKIP, 0x124, 0x128, 0x12C, 0x17C, 0x18C, 0x1AC, 0x1BC, 0x1D8, 0x1DC

constexpr uint32_t kDriverRef = 0x10058b78;
inline uint8_t *g_origDriver;
inline Pair g_driverPair;

inline Pair MakeFrame(int type)
{
    Pair x{CallV<uint8_t *>(g_origDriver, 0x24, type), reinterpret_cast<uint8_t *>(ls3d::i3d::g_driver->CreateFrame(type))};
    Register(x);
    return x;
}
inline Pair MakeVisual(uint32_t fourCC)
{
    Pair x{CallV<uint8_t *>(g_origDriver, 0x20, fourCC), reinterpret_cast<uint8_t *>(ls3d::i3d::g_driver->CreateVisual(fourCC))};
    Register(x);
    return x;
}

// The original's heap (its C runtime's own).
inline void *OrigNew(size_t size) { return OrigFn<void *(__cdecl *)(size_t)>(0x1003150b)(size); }
inline void OrigDelete(void *p) { OrigFn<void(__cdecl *)(void *)>(0x100314b0)(p); }

// A C_vector field of an original object, at `v`: data, capacity, count, grow.
inline uint8_t *&VecData(uint8_t *v) { return *reinterpret_cast<uint8_t **>(v); }
inline int &VecCount(uint8_t *v) { return *reinterpret_cast<int *>(v + 8); }

// ---------------------------------------------------------------- random values

inline float RandCoord()
{
    switch (Rand(8)) {
    case 0: return 0.0f;
    case 1: return Rand(2) ? 1.0f : -1.0f;
    default: return RandF(-100.0f, 100.0f);
    }
}
inline S_vector RandVector() { return S_vector(RandCoord(), RandCoord(), RandCoord()); }
inline S_vector RandUnit()
{
    S_vector v;
    do {
        v = S_vector(RandF(-1, 1), RandF(-1, 1), RandF(-1, 1));
    } while (v.Dot(v) < 0.01f);
    const float l = 1.0f / std::sqrt(v.Dot(v));
    return S_vector(v.x * l, v.y * l, v.z * l);
}

// ---------------------------------------------------------------- meshes of both sides

constexpr int kMats = 6;
inline Pair g_mats[kMats];

struct S_vtx {
    float x, y, z, w;
    float nx, ny, nz, nw;
};

inline void MakeMaterials()
{
    for (Pair &m : g_mats) {
        m = {CallV<uint8_t *>(g_origDriver, 0x28), reinterpret_cast<uint8_t *>(ls3d::i3d::g_driver->CreateMaterial())};
        Register(m);
        const S_vector d(RandF(0, 1), RandF(0, 1), RandF(0, 1));
        Do(m, 0x08, &d);
        Do(m, 0x18, RandF(0, 1));
    }
}

// A mesh of 1 - `maxLevels` levels, the same on both sides (a new reference on each).
inline Pair MakeMesh(int maxLevels)
{
    Pair mesh{CallV<uint8_t *>(g_origDriver, 0x3C), reinterpret_cast<uint8_t *>(ls3d::i3d::g_driver->CreateMesh())};
    Register(mesh);
    const int levels = 1 + Rand(maxLevels);
    for (int i = 0; i < levels; ++i) {
        std::vector<S_vtx> v(3 + Rand(6));
        for (S_vtx &a : v) {
            a = {RandF(-10, 10), RandF(-10, 10), RandF(-10, 10), 1, RandF(-1, 1), RandF(-1, 1), RandF(-1, 1), 0};
        }
        std::vector<I3D_triface> f(1 + Rand(5));
        for (I3D_triface &t : f) {
            t.v[0] = static_cast<uint16_t>(Rand(static_cast<int>(v.size())));
            t.v[1] = static_cast<uint16_t>(Rand(static_cast<int>(v.size())));
            t.v[2] = static_cast<uint16_t>(Rand(static_cast<int>(v.size())));
        }
        Pair level{CallV<uint8_t *>(g_origDriver, 0x38), reinterpret_cast<uint8_t *>(ls3d::i3d::g_driver->CreateMeshLevel())};
        Register(level);
        Do(level, 0x24, static_cast<const void *>(v.data()), static_cast<int>(v.size()));
        Pair fg{CallV<uint8_t *>(g_origDriver, 0x34), reinterpret_cast<uint8_t *>(ls3d::i3d::g_driver->CreateFaceGroup())};
        Register(fg);
        Do(fg, 0x00, g_mats[Rand(kMats)]);
        CallV<I3D_triface *>(fg.o, 0x04, static_cast<const I3D_triface *>(f.data()), static_cast<int>(f.size()));
        CallV<I3D_triface *>(fg.p, 0x04, static_cast<const I3D_triface *>(f.data()), static_cast<int>(f.size()));
        Do(level, 0x1C, fg);
        CallV<I3D_RESULT>(mesh.o, 0x08, level.o, i);
        CallV<I3D_RESULT>(mesh.p, 0x08, level.p, i);
        CallV<uint32_t>(level.o, 0x00);
        CallV<uint32_t>(level.p, 0x00);
    }
    return mesh;
}

// Uninit and Init (the name first let go: Uninit leaves the freed pointer in the frame).
inline void Recycle(Pair x)
{
    Get<I3D_RESULT>("SetName (none)", x, 0x34, static_cast<const char *>(nullptr));
    DoThis(x, 0x64);
    DoThis(x, 0x60);
}

// ---------------------------------------------------------------- operations every frame / visual has

const char *const kNames[] = {nullptr, "", "a", "ab", "visual", "Frame01"};

// A frame's transform, on / off, parent, name and hierarchy values.
inline void FrameOp(Pair x, Pair parent)
{
    switch (Rand(12)) {
    case 0: {
        g_lastOp = "SetWorldPos";
        const S_vector v = RandVector();
        Do(x, 0x08, &v);
        break;
    }
    case 1: {
        g_lastOp = "SetWorldPosDir";
        const S_vector v = RandVector(), d = RandUnit();
        Do(x, 0x14, &v, &d, RandF(-3, 3));
        break;
    }
    case 2:
        g_lastOp = "SetDir";
        {
            const S_vector d = RandUnit();
            Do(x, 0x1C, &d, RandF(-3, 3));
        }
        break;
    case 3:
        g_lastOp = "Update";
        Do(x, 0x24);
        break;
    case 4: {
        g_lastOp = "SetOn";
        const int on = Rand(2);
        Do(x, 0x30, on);
        break;
    }
    case 5: {
        g_lastOp = "SetName";
        const char *n = kNames[Rand(6)];
        Get<I3D_RESULT>("SetName", x, 0x34, n);
        break;
    }
    case 6:
        g_lastOp = "LinkTo";
        if (Rand(2)) {
            Get<I3D_RESULT>("LinkTo", x, 0x38, parent, 0u);
        } else {
            Get<I3D_RESULT>("LinkTo (none)", x, 0x38, static_cast<uint8_t *>(nullptr), 0u);
        }
        break;
    case 7:
        g_lastOp = "UpdateBound";
        Do(x, 0x58);
        break;
    case 8:
        g_lastOp = "SetHierarchyLightLevel";
        Do(x, 0x50, RandF(0, 1));
        break;
    case 9:
        g_lastOp = "SetHierarchyAlpha";
        Do(x, 0x54, RandF(0, 1));
        break;
    case 10:
        g_lastOp = "MatrixChanged";
        DoThis(x, 0x68);
        break;
    default:
        g_lastOp = "Tick";
        Do(x, 0x20, Rand(100));
        break;
    }
    Count("frame ops");
}

// The visual base slots.
inline void VisualOp(Pair x)
{
    switch (Rand(6)) {
    case 0: {
        g_lastOp = "SetBBox";
        const S_vector c = RandVector();
        const float h = RandF(0.1f, 5);
        float box[8] = {c.x - h, c.y - h, c.z - h, 0, c.x + h, c.y + h, c.z + h, 0};
        Do(x, 0x78, static_cast<const void *>(box));
        break;
    }
    case 1:
        g_lastOp = "UpdateLocalBound";
        Do(x, 0x90);
        break;
    case 2: {
        g_lastOp = "SetModeA";
        const int m = Rand(4);
        Do(x, 0x80, m);
        Get<int>("GetModeA", x, 0x84);
        break;
    }
    case 3: {
        g_lastOp = "SetModeB";
        const int m = Rand(4);
        Do(x, 0x88, m);
        Get<int>("GetModeB", x, 0x8C);
        break;
    }
    case 4:
        g_lastOp = "GetVisualType";
        Get<uint32_t>("GetVisualType", x, 0x04);
        break;
    default:
        g_lastOp = "NumChildren";
        Get<int>("NumChildren", x, 0x3C);
        break;
    }
    Count("visual ops");
}

// Fields of a new object that nothing writes (heap contents): from the original's, so that a later
// write on one side shows.
inline void Seed(Pair x, unsigned from, unsigned to)
{
    std::memcpy(x.p + from, x.o + from, to - from);
}

// A fake texture (the same on both sides; its Release and AddRef do nothing).
inline void __stdcall FakeNoOp(void *) {}
inline void *g_fakeVtable[64];
inline uint8_t g_fakeTexture[0x44];
inline void *FakeTexture()
{
    if (!g_fakeVtable[0]) {
        for (void *&slot : g_fakeVtable)
            slot = reinterpret_cast<void *>(&FakeNoOp);
        std::memcpy(g_fakeTexture, &g_fakeVtable[0], 0);
        *reinterpret_cast<void ***>(g_fakeTexture) = g_fakeVtable;
    }
    return g_fakeTexture;
}

// The mesh visuals' slots (a new mesh pair each time).
inline void ObjectOp(Pair x)
{
    switch (Rand(6)) {
    case 0: {
        g_lastOp = "SetMesh";
        Pair mesh = MakeMesh(3);
        Do(x, 0x94, mesh, 0u);                  // (the original returns whatever eax holds)
        Do(mesh, 0x00);
        Count("SetMesh");
        break;
    }
    case 1:
        g_lastOp = "SetLightLevel";
        Do(x, 0x9C, RandF(0, 1));
        Count("SetLightLevel");
        break;
    case 2:
        g_lastOp = "SetAlpha";
        Do(x, 0xA0, RandF(0, 1));
        Count("SetAlpha");
        break;
    case 3:
        g_lastOp = "RefreshLevel";
        Do(x, 0xA4, Rand(3));
        Count("RefreshLevel");
        break;
    case 4:
        g_lastOp = "RefreshLevels";
        Do(x, 0xA8);
        Count("RefreshLevels");
        break;
    default:
        g_lastOp = "SetWorldSpace off";
        Do(x, 0x7C, 0);
        Count("SetWorldSpace");
        break;
    }
}

// ---------------------------------------------------------------- the run

using OpFn = void (*)();

inline LONG WINAPI CrashFilter(EXCEPTION_POINTERS *info)
{
    std::printf("CRASH (0x%08lx at %p, original at %p) in op %ld (%s)\n", info->ExceptionRecord->ExceptionCode,
                info->ExceptionRecord->ExceptionAddress, g_base, g_op, g_lastOp.c_str());
    std::fflush(stdout);
    TerminateProcess(GetCurrentProcess(), 3);
    return EXCEPTION_EXECUTE_HANDLER;
}

// Loads the original and makes the port's engine objects; then `setUp`, `ops` times `op` and `compare`.
inline int Run(int argc, char **argv, const char *name, OpFn setUp, OpFn op, OpFn compare, long defaultOps = 20000)
{
    SetUnhandledExceptionFilter(&CrashFilter);
    const char *origPath = OriginalDllPath(argc > 1 ? argv[1] : nullptr, "LS3DRF.dll");
    const long ops = argc > 2 ? std::atol(argv[2]) : defaultOps;
    const uint32_t seed = argc > 3 ? static_cast<uint32_t>(std::strtoul(argv[3], nullptr, 0)) : 1;
    HMODULE dll = LoadLibraryA(origPath);
    if (!dll) {
        std::printf("cannot load %s (%lu)\n", origPath, GetLastError());
        return 2;
    }
    g_base = reinterpret_cast<uint8_t *>(dll);
    Orig<uint8_t>(0x10058b72) = 1;              // no log
    g_origDriver = Orig<uint8_t *>(kDriverRef);
    if (!g_origDriver) {
        std::printf("the original's driver does not exist\n");
        return 2;
    }
    ls3d::g_logDisabled = true;
    _controlfp(_PC_24, _MCW_PC);
    g_pIGraph = new IGraph;
    ls3d::i3d::g_driver = new I3D_driver;
    g_rng.seed(seed);
    MakeMaterials();
    setUp();
    g_lastOp = "create";
    compare();
    for (g_op = 0; g_op < ops; ++g_op) {
        op();
        compare();
    }
    std::string text;
    for (const auto &c : g_coverage)
        text += " " + c.first + " " + std::to_string(c.second) + ",";
    std::printf("coverage:%s\n", text.c_str());
    std::printf("%s: %ld operations, seed %u: identical\n", name, ops, seed);
    std::fflush(stdout);
    TerminateProcess(GetCurrentProcess(), 0);
    return 0;
}

}  // namespace vc
